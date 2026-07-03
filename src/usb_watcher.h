// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// Keep the macOS USB host port alive while the target device reboots
// through its BootROM. Without an active IOKit hot-plug subscription for
// the DFU/recovery PIDs the host controller can suspend the port as soon
// as the device disappears from recovery, and the device's BootROM then
// skips DFU (no D+ host-activity) and rebounds to recovery instead.
//
// On macOS we register IOServiceFirstMatch + IOServiceTerminate matching
// on a background CFRunLoop and keep it running until usb_watcher_stop().
// On libusb/Linux ports the watcher is a no-op (the same bus-state issue
// does not characteristically apply, and udev/usbmuxd already keep the
// bus exercised).
#ifndef IDFU_USB_WATCHER_H
#	define IDFU_USB_WATCHER_H
#	include <stdbool.h>

/* Returns true if the watcher is running. Safe to call on either backend. */
bool usb_watcher_start(void);
void usb_watcher_stop(void);

#endif /* IDFU_USB_WATCHER_H */