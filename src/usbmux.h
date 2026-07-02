// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// Minimal usbmuxd v1 (plist) client: connect to the local daemon, list
// attached USB devices, and tunnel a TCP connection to a device port
// (used for lockdownd on 62078). Exactly enough for iDFU's normal-mode
// EnterRecovery flow; no Listen/Subscribe path.
#ifndef IDFU_USBMUX_H
#	define IDFU_USBMUX_H
#	include <stdint.h>
#	include <stddef.h>
#	include <stdbool.h>

/* On success returns a connected TCP-style fd to device:port; -1 on error. */
int  usbmux_connect(uint32_t device_id, uint16_t port);

/* Enumerate attached USB devices. Returns first USB-attached device:
 * writes its DeviceID into *out_device_id and a NUL-terminated UDID into
 * out_udid (size udid_sz). Returns true on success. */
bool usbmux_find_first_usb_device(uint32_t *out_device_id, char *out_udid, size_t udid_sz,
                                  const char **err);

#endif /* IDFU_USBMUX_H */