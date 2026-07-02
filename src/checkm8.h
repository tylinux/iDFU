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
 * checkm8 vulnerability primitive definitions for iDFU.
 * State machine and payload layout adapted from gaster (0x7ff).
 */
#ifndef IDFU_CHECKM8_H
#	define IDFU_CHECKM8_H

#	include <stdint.h>
#	include <stdbool.h>
#	include "usb.h"

/* DFU USB request constants. */
#	define DFU_DNLOAD                    (1)
#	define DFU_GET_STATUS                (3)
#	define DFU_CLR_STATUS                (4)

#	define DFU_STATUS_OK                 (0)
#	define DFU_STATE_MANIFEST_SYNC       (6)
#	define DFU_STATE_MANIFEST            (7)
#	define DFU_STATE_MANIFEST_WAIT_RESET (8)

#	define AES_CMD_DEC                   (1U)
#	define AES_CMD_CBC                   (16U)
#	define AES_BLOCK_SZ                  (16)
#	define AES_KEY_SZ_BYTES_256          (32)
#	define AES_KEY_TYPE_GID0             (0x200U)
#	define AES_KEY_SZ_256                (0x20000000U)

#	define DFU_FILE_SUFFIX_LEN           (16)
#	define COMP_HDR_PAD_SZ               (0x16C)
#	define COMP_HDR_MAGIC                (0x636F6D70U)
#	define COMP_HDR_TYPE_LZSS            (0x6C7A7373U)

#	define ARM_16K_TT_L2_SZ              (0x2000000U)
#	define DONE_MAGIC                    (0x646F6E65646F6E65ULL)
#	define EXEC_MAGIC                    (0x6578656365786563ULL)
#	define MEMC_MAGIC                    (0x6D656D636D656D63ULL)

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct {
	uint64_t func, arg;
} callback_t;

typedef struct {
	uint32_t endpoint, pad_0;
	uint64_t io_buffer;
	uint32_t status, io_len, ret_cnt, pad_1;
	uint64_t callback, next;
} dfu_callback_t;

typedef struct {
	uint32_t endpoint, io_buffer, status, io_len, ret_cnt, callback, next;
} dfu_callback_armv7_t;

typedef struct {
	dfu_callback_t callback;
} checkm8_overwrite_t;

typedef struct {
	dfu_callback_armv7_t callback;
} checkm8_overwrite_armv7_t;

bool checkm8_check_usb_device(usb_handle_t *handle, void *pwned);
bool checkm8_exploit(usb_handle_t *handle);
bool dfu_send_data(const usb_handle_t *handle, uint8_t *data, size_t len);
bool gaster_reset_command(usb_handle_t *handle);
bool dfu_check_status(const usb_handle_t *handle, uint8_t status, uint8_t state);
bool gaster_aes(usb_handle_t *handle, uint32_t cmd, const uint8_t *src, uint8_t *dst, size_t len, uint32_t options);
bool gaster_command(usb_handle_t *handle, void *request_data, size_t request_len, uint8_t **response, size_t response_len);

extern uint16_t cpid;

#endif /* IDFU_CHECKM8_H */