// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// lockdownd client implementation. Lockdownd frames each plist with a
// 4-byte big-endian length prefix; the body may be XML (our requests) or
// bplist00 (device replies). We send XML requests and parse replies with
// the unified plist module.
#include "lockdown.h"
#include "usbmux.h"
#include "plist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>

#define LOCKDOWN_PORT 62078
#define CLIENT_LABEL "idfu"

static bool
ld_write(int fd, plist_node_t *msg) {
	char *xml = NULL;
	size_t xml_len = 0;
	if(!plist_to_xml(msg, &xml, &xml_len)) return false;
	uint32_t nlen = htonl((uint32_t)xml_len);
	bool ok = (write(fd, &nlen, sizeof nlen) == sizeof nlen) &&
	          (write(fd, xml, xml_len) == (ssize_t)xml_len);
	free(xml);
	return ok;
}

static plist_node_t *
ld_read(int fd, const char **err) {
	uint32_t nlen = 0;
	if(read(fd, &nlen, sizeof nlen) != sizeof nlen) {
		if(err) *err = "lockdownd: short read (length)";
		return NULL;
	}
	uint32_t len = ntohl(nlen);
	if(len == 0 || len > (16u << 20)) { if(err) *err = "lockdownd: bad length"; return NULL; }
	uint8_t *buf = malloc(len);
	if(!buf) return NULL;
	uint32_t got = 0;
	while(got < len) {
		ssize_t r = read(fd, buf + got, len - got);
		if(r <= 0) { if(err) *err = "lockdownd: short read (body)"; free(buf); return NULL; }
		got += (uint32_t)r;
	}
	plist_node_t *node = plist_from_data(buf, len, err);
	free(buf);
	return node;
}

static plist_node_t *
ld_request(const char *request) {
	plist_node_t *d = plist_new_null();
	d->kind = PLIST_DICT;
	plist_dict_set(d, "Label", plist_new_string(CLIENT_LABEL));
	plist_dict_set(d, "Request", plist_new_string(request));
	return d;
}

/* Check a lockdownd response: success iff response echoes `request` and has
 * no Error key (Result absent counts as success on iOS 5+). */
static bool
ld_result_ok(const plist_node_t *resp, const char *request, const char **err) {
	if(!resp) return false;
	plist_node_t *rq = plist_dict_get(resp, "Request");
	if(!rq || rq->kind != PLIST_STRING || strcmp(rq->v.str, request) != 0) {
		if(err) *err = "lockdownd: response does not echo request";
		return false;
	}
	plist_node_t *errk = plist_dict_get(resp, "Error");
	if(errk && errk->kind == PLIST_STRING) {
		/* Surface the error string verbatim ("InvalidSession", etc.). */
		if(err) *err = errk->v.str;
		return false;
	}
	return true;
}

bool
lockdown_enter_recovery(const char **err) {
	uint32_t device_id = 0;
	char udid[128] = {0};
	if(!usbmux_find_first_usb_device(&device_id, udid, sizeof udid, err)) {
		return false;
	}
	printf("lockdownd: device UDID=%s (DeviceID=%u)\n", udid, device_id);

	int fd = usbmux_connect(device_id, LOCKDOWN_PORT);
	if(fd < 0) {
		if(err) *err = "cannot connect to device lockdownd (port 62078)";
		return false;
	}

	/* 1. QueryType handshake. */
	plist_node_t *qt = ld_request("QueryType");
	bool ok = ld_write(fd, qt);
	plist_free(qt);
	if(!ok) { if(err) *err = "lockdownd: write QueryType failed"; close(fd); return false; }

	plist_node_t *resp = ld_read(fd, err);
	if(!resp) { close(fd); return false; }
	const char *type = plist_dict_get_string(resp, "Type");
	if(!type || strcmp(type, "com.apple.mobile.lockdown") != 0) {
		if(err) *err = "lockdownd: unexpected QueryType response";
		plist_free(resp); close(fd); return false;
	}
	plist_free(resp);

	/* 2. EnterRecovery. */
	plist_node_t *er = ld_request("EnterRecovery");
	ok = ld_write(fd, er);
	plist_free(er);
	if(!ok) { if(err) *err = "lockdownd: write EnterRecovery failed"; close(fd); return false; }

	resp = ld_read(fd, err);
	bool success = ld_result_ok(resp, "EnterRecovery", err);
	plist_free(resp);
	close(fd);
	return success;
}