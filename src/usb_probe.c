/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * Non-blocking USB VID/PID presence probe (IOKit on macOS, libusb elsewhere).
 */
#include "usb_probe.h"

#ifdef HAVE_LIBUSB
#include <libusb-1.0/libusb.h>

bool
usb_device_present(uint16_t vid, uint16_t pid) {
	libusb_device **devs = NULL;
	ssize_t cnt;
	bool found = false;

	if(libusb_init(NULL) != LIBUSB_SUCCESS)
		return false;
	cnt = libusb_get_device_list(NULL, &devs);
	if(cnt >= 0) {
		for(ssize_t i = 0; i < cnt; ++i) {
			struct libusb_device_descriptor desc;
			if(libusb_get_device_descriptor(devs[i], &desc) == LIBUSB_SUCCESS &&
			   desc.idVendor == vid && desc.idProduct == pid) {
				found = true;
				break;
			}
		}
		libusb_free_device_list(devs, 1);
	}
	libusb_exit(NULL);
	return found;
}

#else /* macOS IOKit */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

static void
dict_set_int16(CFMutableDictionaryRef dict, CFStringRef key, int16_t value) {
	CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &value);
	if(num) {
		CFDictionarySetValue(dict, key, num);
		CFRelease(num);
	}
}

bool
usb_device_present(uint16_t vid, uint16_t pid) {
	CFMutableDictionaryRef matching;
	io_iterator_t iter = IO_OBJECT_NULL;
	io_service_t serv;
	bool found = false;

#if TARGET_OS_IPHONE
	matching = IOServiceMatching("IOUSBHostDevice");
#else
	matching = IOServiceMatching(kIOUSBDeviceClassName);
#endif
	if(!matching)
		return false;

	dict_set_int16(matching, CFSTR(kUSBVendorID), (int16_t)vid);
	dict_set_int16(matching, CFSTR(kUSBProductID), (int16_t)pid);

	if(IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != kIOReturnSuccess) {
		return false;
	}

	serv = IOIteratorNext(iter);
	if(serv != IO_OBJECT_NULL) {
		found = true;
		IOObjectRelease(serv);
	}
	IOObjectRelease(iter);
	return found;
}

#endif
