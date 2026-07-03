// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// IOKit USB hot-plug subscription kept alive on a background CFRunLoop so
// the macOS USB host port stays powered and D+ active across the device's
// reboot cycle, allowing the device's BootROM to enter DFU.
#include "usb_watcher.h"
#include "usb.h"

#ifdef HAVE_LIBUSB

/* No-op on the libusb backend. */
bool usb_watcher_start(void) { return true; }
void usb_watcher_stop(void) {}

#else

#	include <pthread.h>
#	include <unistd.h>
#	include <CoreFoundation/CoreFoundation.h>
#	include <IOKit/IOKitLib.h>
#	include <IOKit/usb/IOUSBLib.h>

static pthread_t         g_thread;
static bool              g_running = false;
static CFRunLoopRef      g_loop    = NULL;
static IONotificationPortRef g_ports[4];
static int                g_port_count = 0;

/* Match callback: drains the iterator (arming future notifications) and
 * immediately releases the matched service object; we only need the
 * subscription to exist, not the service. */
static void
noop_match_cb(void *refcon, io_iterator_t iter) {
	(void)refcon;
	io_object_t s;
	while((s = IOIteratorNext(iter)) != 0) {
		IOObjectRelease(s);
	}
}

static void
dict_set_sint16(CFMutableDictionaryRef dict, const void *key, int16_t v) {
	CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &v);
	if(n) { CFDictionarySetValue(dict, key, n); CFRelease(n); }
}

/* Register FirstMatch + Terminate notifications for one (VID,PID) pair
 * against the given run loop. Adds its notification port to g_ports so it
 * can be released on stop(). */
static void
prime_pid_once(uint16_t pid, const char *notify_type) {
	IONotificationPortRef port = IONotificationPortCreate(kIOMasterPortDefault);
	if(!port) return;

	CFRunLoopSourceRef src = IONotificationPortGetRunLoopSource(port);
	if(src) CFRunLoopAddSource(g_loop, src, kCFRunLoopDefaultMode);

	/* A fresh matching dict per call: IOServiceAddMatchingNotification
	 * takes ownership (retains) the dict, so each notification needs its
	 * own. Reusing a single dict across FirstMatch + Terminate trips an
	 * over-release / SIGTRAP on modern macOS. */
	CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
	if(matching) {
		dict_set_sint16(matching, CFSTR(kUSBVendorID),  (int16_t)APPLE_VID);
		dict_set_sint16(matching, CFSTR(kUSBProductID), (int16_t)pid);

		io_iterator_t iter;
		kern_return_t k = IOServiceAddMatchingNotification(port,
			notify_type, matching, noop_match_cb, NULL, &iter);
		if(k == kIOReturnSuccess && iter) {
			noop_match_cb(NULL, iter); /* arm + drain immediate matches */
		}
	}

	if(g_port_count < (int)(sizeof g_ports / sizeof g_ports[0])) {
		g_ports[g_port_count++] = port;
	}
}

static void
prime_pid(uint16_t pid) {
	prime_pid_once(pid, "IOServiceFirstMatch");
	prime_pid_once(pid, "IOServiceTerminate");
}

static void *
watcher_thread_main(void *arg) {
	(void)arg;
	CFRunLoopRef loop = CFRunLoopGetCurrent();
	g_loop = loop;

	/* Keep subscriptions alive for both the DFU and Recovery PIDs: the
	 * device transitions through both, and we need the port alive for the
	 * whole reboot cycle. */
	prime_pid(DFU_MODE_PID);
	prime_pid(RECOVERY_MODE_PID);

	/* Spin the run loop; stopped via CFRunLoopStop(g_loop) in
	 * usb_watcher_stop(). Running the loop is what keeps the match-source
	 * active and the USB host port exercised. */
	CFRunLoopRun();
	return NULL;
}

bool
usb_watcher_start(void) {
	if(g_running) return true;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	int rc = pthread_create(&g_thread, &attr, watcher_thread_main, NULL);
	pthread_attr_destroy(&attr);
	if(rc != 0) return false;
	/* Give the worker a brief window to set up its run loop + subscriptions. */
	usleep(50 * 1000);
	g_running = true;
	return true;
}

void
usb_watcher_stop(void) {
	if(!g_running) return;
	if(g_loop) CFRunLoopStop(g_loop);
	pthread_join(g_thread, NULL);
	g_running = false;

	/* Release notification ports. */
	for(int i = 0; i < g_port_count; ++i) {
		IONotificationPortDestroy(g_ports[i]);
	}
	g_port_count = 0;
	g_loop = NULL;
}

#endif /* HAVE_LIBUSB */