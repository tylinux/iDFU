// SPDX-License-Identifier: Apache-2.0
//
// Copyright 2026 iDFU authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Standard USB device descriptor storage, shared across TUs. Defined in usb.c.
#ifndef IDFU_DEVICE_DESCRIPTOR_H
#	define IDFU_DEVICE_DESCRIPTOR_H
#	include <stdint.h>

/* Representation of the standard USB device descriptor for the connected
 * Apple device. Storage lives in usb.c. */
typedef struct {
	uint8_t b_len, b_descriptor_type;
	uint16_t bcd_usb;
	uint8_t b_device_class, b_device_sub_class, b_device_protocol, b_max_packet_sz;
	uint16_t id_vendor, id_product, bcd_device;
	uint8_t i_manufacturer, i_product, i_serial_number, b_num_configurations;
} device_descriptor_t;

extern device_descriptor_t device_descriptor;

#endif /* IDFU_DEVICE_DESCRIPTOR_H */