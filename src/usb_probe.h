/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Minimal USB VID/PID presence probe for Apple Recovery/DFU PIDs.
 * Full transfer stack (checkm8 etc.) is intentionally not included.
 */
#ifndef IDFU_USB_PROBE_H
#define IDFU_USB_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#define APPLE_VID          (0x5AC)
#define DFU_MODE_PID       (0x1227)
#define RECOVERY_MODE_PID  (0x1281)

/* Single-pass check: true if a device with the given VID/PID is present. */
bool usb_device_present(uint16_t vid, uint16_t pid);

#endif /* IDFU_USB_PROBE_H */
