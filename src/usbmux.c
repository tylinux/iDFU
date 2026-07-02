// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// usbmuxd v1 (plist) client implementation. Talks to /var/run/usbmuxd
// using 16-byte little-endian headers plus XML plist bodies.
#include "usbmux.h"
#include "plist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef USBMUXD_SOCKET
#	define USBMUXD_SOCKET "/var/run/usbmuxd"
#endif

#define USBMUX_HEADER_SIZE  16
#define USBMUX_VERSION      1u
#define USBMUX_MSG_PLIST    8u

struct __attribute__((packed)) um_header {
	uint32_t length;
	uint32_t version;
	uint32_t message;
	uint32_t tag;
};

static int
connect_to_usbmuxd(void) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0) return -1;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, USBMUXD_SOCKET, sizeof addr.sun_path - 1);
	if(connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool
read_full(int fd, void *buf, size_t n) {
	uint8_t *p = buf;
	while(n > 0) {
		ssize_t r = read(fd, p, n);
		if(r <= 0) return false;
		p += r; n -= (size_t)r;
	}
	return true;
}

static bool
write_full(int fd, const void *buf, size_t n) {
	const uint8_t *p = buf;
	while(n > 0) {
		ssize_t w = write(fd, p, n);
		if(w <= 0) return false;
		p += w; n -= (size_t)w;
	}
	return true;
}

static bool
send_plist(int fd, uint32_t tag, plist_node_t *msg) {
	char *xml = NULL;
	size_t xml_len = 0;
	if(!plist_to_xml(msg, &xml, &xml_len)) return false;
	struct um_header h;
	h.length = USBMUX_HEADER_SIZE + (uint32_t)xml_len;
	h.version = USBMUX_VERSION;
	h.message = USBMUX_MSG_PLIST;
	h.tag = tag;
	bool ok = write_full(fd, &h, sizeof h) && write_full(fd, xml, xml_len);
	free(xml);
	return ok;
}

static plist_node_t *
recv_plist(int fd, const char **err) {
	struct um_header h;
	if(!read_full(fd, &h, sizeof h)) {
		if(err) *err = "usbmux: short read (header)";
		return NULL;
	}
	if(h.length < USBMUX_HEADER_SIZE) {
		if(err) *err = "usbmux: bogus length";
		return NULL;
	}
	uint32_t plen = h.length - USBMUX_HEADER_SIZE;
	uint8_t *buf = malloc(plen ? plen : 1);
	if(!buf) return NULL;
	if(!read_full(fd, buf, plen)) {
		if(err) *err = "usbmux: short read (body)";
		free(buf);
		return NULL;
	}
	plist_node_t *node = plist_from_data(buf, plen, err);
	free(buf);
	/* On a bad version, daemon may answer with a binary RESULT message:
	 * message=1, body=uint32 result (6 = BADVERSION). We accept plists only. */
	return node;
}

static plist_node_t *
make_base_msg(const char *msg_type) {
	plist_node_t *d = plist_new_null();
	d->kind = PLIST_DICT;
	plist_dict_set(d, "BundleID", plist_new_string("org.idfu.idfu"));
	plist_dict_set(d, "ClientVersionString", plist_new_string("iDFU 0.1"));
	plist_dict_set(d, "MessageType", plist_new_string(msg_type));
	plist_dict_set(d, "ProgName", plist_new_string("idfu"));
	plist_dict_set(d, "kLibUSBMuxVersion", plist_new_uint(3));
	return d;
}

bool
usbmux_find_first_usb_device(uint32_t *out_device_id, char *out_udid, size_t udid_sz,
                             const char **err) {
	int fd = connect_to_usbmuxd();
	if(fd < 0) {
		if(err) *err = "cannot connect to " USBMUXD_SOCKET
		               " (usbmuxd daemon not running?)";
		return false;
	}
	plist_node_t *req = make_base_msg("ListDevices");
	bool ok = send_plist(fd, 1, req);
	plist_free(req);
	if(!ok) { if(err) *err = "usbmux: send ListDevices failed"; close(fd); return false; }

	plist_node_t *resp = recv_plist(fd, err);
	close(fd);
	if(!resp) return false;

	/* expected: { MessageType:"Result" } for some or directly DeviceList. The
	 * ListDevices response top-level dict has key "DeviceList" (array of dicts
	 * with a "Properties" dict containing DeviceID/SerialNumber/ConnectionType). */
	plist_node_t *list = plist_dict_get(resp, "DeviceList");
	if(!list || list->kind != PLIST_ARRAY) {
		/* Some daemons answer with MessageType:"Result" first; treat no list as empty. */
		plist_free(resp);
		if(err) *err = "usbmux: no DeviceList in response";
		return false;
	}
	bool found = false;
	for(size_t i = 0; i < list->v.arr.count && !found; ++i) {
		plist_node_t *entry = list->v.arr.items[i];
		plist_node_t *props = plist_dict_get(entry, "Properties");
		if(!props) props = entry;
		const char *conn = plist_dict_get_string(props, "ConnectionType");
		if(conn && strcmp(conn, "USB") != 0) continue;
		plist_node_t *did = plist_dict_get(props, "DeviceID");
		if(did && did->kind == PLIST_UINT) {
			const char *sn = plist_dict_get_string(props, "SerialNumber");
			if(out_device_id) *out_device_id = (uint32_t)did->v.u;
			if(out_udid && sn) {
				strncpy(out_udid, sn, udid_sz - 1);
				out_udid[udid_sz - 1] = '\0';
			}
			found = true;
		}
	}
	plist_free(resp);
	if(!found && err) *err = "no USB-attached Apple device";
	return found;
}

int
usbmux_connect(uint32_t device_id, uint16_t port) {
	int fd = connect_to_usbmuxd();
	if(fd < 0) return -1;

	plist_node_t *req = make_base_msg("Connect");
	plist_dict_set(req, "DeviceID", plist_new_uint(device_id));
	/* PortNumber is the port reinterpreted in network byte order (htons),
	 * per libusbmuxd's send_connect_packet (plist path). */
	plist_dict_set(req, "PortNumber", plist_new_uint(ntohs(port)));
	bool ok = send_plist(fd, 2, req);
	plist_free(req);
	if(!ok) { close(fd); return -1; }

	plist_node_t *resp = recv_plist(fd, NULL);
	if(!resp) { close(fd); return -1; }

	/* success: { MessageType:"Result", Number:0 } (Apple's daemon uses
	 * "Number"; older/third-party daemons may use "ResultNumber"). Accept both. */
	plist_node_t *mt = plist_dict_get(resp, "MessageType");
	plist_node_t *rn = plist_dict_get(resp, "Number");
	if(!rn) rn = plist_dict_get(resp, "ResultNumber");
	bool success = (mt && mt->kind == PLIST_STRING && strcmp(mt->v.str, "Result") == 0 &&
	               rn && rn->kind == PLIST_UINT && rn->v.u == 0);
	plist_free(resp);
	if(!success) { close(fd); return -1; }

	/* fd is now the tunneled stream to device:port. */
	return fd;
}