/* Copyright 2023 0x7ff
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * iDFU USB abstraction: provides a single API over the macOS IOKit
 * backend and the libusb backend (selected via -DHAVE_LIBUSB).
 */
#ifndef IDFU_USB_H
#	define IDFU_USB_H

#	include <stdint.h>
#	include <stddef.h>
#	include <stdbool.h>

#	include "device_descriptor.h"

/* Apple DFU/Recovery USB identifiers. */
#	define APPLE_VID       (0x5AC)
#	define DFU_MODE_PID    (0x1227)
#	define RECOVERY_MODE_PID (0x1281)
#	define NORMAL_MODE_PID   (0x1298) /* trusted/normal boot (on iOS 9+) */

#	define EP0_MAX_PACKET_SZ      (0x40)
#	define DFU_MAX_TRANSFER_SZ    (0x800)
#	define MAX_BLOCK_SZ           (0x50)
#	define USB_MAX_STRING_DESCRIPTOR_IDX (10)

enum usb_transfer {
	USB_TRANSFER_OK,
	USB_TRANSFER_ERROR,
	USB_TRANSFER_STALL
};

typedef struct {
	enum usb_transfer ret;
	uint32_t sz;
} transfer_ret_t;

#ifdef HAVE_LIBUSB
#	include <libusb-1.0/libusb.h>
typedef struct {
	uint16_t vid, pid;
	struct libusb_device_handle *device;
} usb_handle_t;
#else
#	include <CoreFoundation/CoreFoundation.h>
#	include <IOKit/IOCFPlugIn.h>
#	include <IOKit/usb/IOUSBLib.h>
typedef struct {
	uint16_t vid, pid;
	io_service_t serv;
	IOUSBDeviceInterface320 **device;
	CFRunLoopSourceRef async_event_source;
} usb_handle_t;
#endif

typedef bool (*usb_check_cb_t)(usb_handle_t *, void *);

void     sleep_ms(unsigned ms);
void     init_usb_handle(usb_handle_t *handle, uint16_t vid, uint16_t pid);
bool     wait_usb_handle(usb_handle_t *handle, usb_check_cb_t usb_check_cb, void *arg);
void     reset_usb_handle(const usb_handle_t *handle);
void     close_usb_handle(usb_handle_t *handle);

bool     send_usb_control_request(const usb_handle_t *handle, uint8_t bm_request_type,
                                  uint8_t b_request, uint16_t w_value, uint16_t w_index,
                                  void *p_data, size_t w_len, transfer_ret_t *transfer_ret);
bool     send_usb_control_request_async(const usb_handle_t *handle, uint8_t bm_request_type,
                                        uint8_t b_request, uint16_t w_value, uint16_t w_index,
                                        void *p_data, size_t w_len, unsigned usb_abort_timeout,
                                        transfer_ret_t *transfer_ret);
bool     send_usb_control_request_no_data(const usb_handle_t *handle, uint8_t bm_request_type,
                                          uint8_t b_request, uint16_t w_value, uint16_t w_index,
                                          size_t w_len, transfer_ret_t *transfer_ret);
bool     send_usb_control_request_async_no_data(const usb_handle_t *handle, uint8_t bm_request_type,
                                                 uint8_t b_request, uint16_t w_value, uint16_t w_index,
                                                 size_t w_len, unsigned usb_abort_timeout,
                                                 transfer_ret_t *transfer_ret);

char    *get_usb_serial_number(usb_handle_t *handle);

/* Non-blocking (single-pass) check whether a USB device with the given
 * VID/PID is currently present. Used by the interactive DFU guide to
 * observe device insertion/removal transitions without blocking. */
bool     usb_device_present(uint16_t vid, uint16_t pid);

#endif /* IDFU_USB_H */