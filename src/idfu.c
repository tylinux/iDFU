/* Copyright 2026 iDFU authors
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
 * iDFU entry point and subcommand dispatch.
 */
#include "usb.h"
#include "checkm8.h"
#include "img4.h"
#include "dfu_guide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDFU_VERSION "0.1.0"

extern unsigned usb_timeout, usb_abort_timeout_min;

static void
usage(const char *prog) {
	printf("Usage: env [VARS] %s <command> [args]\n", prog);
	puts("");
	puts("Commands:");
	puts("  guide            Walk through entering DFU mode interactively.");
	puts("  pwn              Exploit checkm8 to reach PWNED DFU.");
	puts("  boot <image>     pwn (if needed) then upload + boot an unsigned");
	puts("                   raw image such as PongoOS.bin.");
	puts("  reset            Clear DFU state and reset the device.");
	puts("  decrypt <src> <dst>  Decrypt an img4/im4p using GID0 AES key.");
	puts("  decrypt_kbag <kbag>  Decrypt a KBAG string using GID0 AES key.");
	puts("  version          Print version and exit.");
	puts("");
	puts("Environment variables:");
	puts("  USB_TIMEOUT            USB timeout in ms (default 5).");
	puts("  USB_ABORT_TIMEOUT_MIN  Minimum USB abort timeout in ms (default 0).");
}

static bool
read_binary_file(const char *filename, uint8_t **buf, size_t *len) {
	FILE *fp = fopen(filename, "rb");
	bool ret = false;

	if(fp != NULL) {
		if(fseek(fp, 0, SEEK_END) == 0 && (*len = (size_t)ftell(fp)) != 0 && (*buf = malloc(*len)) != NULL) {
			rewind(fp);
			ret = fread(*buf, 1, *len, fp) == *len;
		}
		fclose(fp);
	}
	if(!ret) {
		printf("Cannot read file \"%s\".\n", filename);
	}
	return ret;
}

static void
parse_env_timeouts(void) {
	char *env_usb_timeout = getenv("USB_TIMEOUT");
	char *env_usb_abort_timeout_min = getenv("USB_ABORT_TIMEOUT_MIN");

	if(env_usb_timeout == NULL || sscanf(env_usb_timeout, "%u", &usb_timeout) != 1 || usb_timeout < 1) {
		usb_timeout = 5;
	}
	printf("usb_timeout: %u\n", usb_timeout);
	if(env_usb_abort_timeout_min == NULL || sscanf(env_usb_abort_timeout_min, "%u", &usb_abort_timeout_min) != 1 || usb_abort_timeout_min > usb_timeout) {
		usb_abort_timeout_min = 0;
	}
	printf("usb_abort_timeout_min: %u\n", usb_abort_timeout_min);
}

static int
cmd_pwn(void) {
	usb_handle_t handle;
	int ret = EXIT_FAILURE;

	if(checkm8_exploit(&handle)) {
		ret = EXIT_SUCCESS;
	}
	return ret;
}

static int
cmd_reset(void) {
	usb_handle_t handle;
	if(gaster_reset_command(&handle)) {
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

static int
cmd_guide(void) {
	if(dfu_guide_run()) {
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

static int
cmd_decrypt(const char *src_filename, const char *dst_filename) {
	usb_handle_t handle;
	uint8_t *buf, *dec;
	size_t len, dec_sz;
	bool ret = false;
	FILE *dst_fp;

	if(read_binary_file(src_filename, &buf, &len)) {
		if(checkm8_exploit(&handle) && gaster_decrypt(&handle, buf, len, &dec, &dec_sz)) {
			if((dst_fp = fopen(dst_filename, "wb")) != NULL) {
				ret = fwrite(dec, 1, dec_sz, dst_fp) == dec_sz;
				fclose(dst_fp);
			}
			free(dec);
		}
		free(buf);
	}
	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int
cmd_decrypt_kbag(const char *kbag_str) {
	usb_handle_t handle;
	return gaster_decrypt_kbag(&handle, kbag_str) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int
cmd_boot(const char *image_path) {
	usb_handle_t handle;
	uint8_t *buf = NULL;
	size_t len = 0;
	bool pwned = false;

	/* Detect whether the device is already PWNED before re-running the exploit. */
	init_usb_handle(&handle, APPLE_VID, DFU_MODE_PID);
	if(wait_usb_handle(&handle, checkm8_check_usb_device, &pwned)) {
		close_usb_handle(&handle);
	} else {
		puts("No DFU device found. Run `idfu guide` first to enter DFU mode.");
		return EXIT_FAILURE;
	}

	if(!pwned) {
		puts("Device not yet PWNED; running checkm8...");
		if(!checkm8_exploit(&handle)) {
			puts("checkm8 exploit failed.");
			return EXIT_FAILURE;
		}
	} else {
		puts("Device already PWNED.");
	}

	if(!read_binary_file(image_path, &buf, &len)) {
		return EXIT_FAILURE;
	}

	printf("Uploading %zu bytes of %s...\n", len, image_path);
	if(wait_usb_handle(&handle, NULL, NULL)) {
		bool ret = dfu_send_data(&handle, buf, len);
		if(ret) {
			puts("Image uploaded; resetting device to boot it.");
			reset_usb_handle(&handle);
			close_usb_handle(&handle);
		} else {
			puts("Failed to upload image.");
			close_usb_handle(&handle);
		}
		free(buf);
		return ret ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	free(buf);
	return EXIT_FAILURE;
}

int
main(int argc, char **argv) {
	parse_env_timeouts();

	if(argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	if(strcmp(argv[1], "guide") == 0) {
		return cmd_guide();
	}
	if(strcmp(argv[1], "pwn") == 0) {
		return cmd_pwn();
	}
	if(strcmp(argv[1], "reset") == 0) {
		return cmd_reset();
	}
	if(strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
		printf("iDFU %s (checkm8 core from gaster / 0x7ff, Apache-2.0)\n", IDFU_VERSION);
		return EXIT_SUCCESS;
	}
	if(argc == 4 && strcmp(argv[1], "decrypt") == 0) {
		return cmd_decrypt(argv[2], argv[3]);
	}
	if(argc == 3 && strcmp(argv[1], "decrypt_kbag") == 0) {
		return cmd_decrypt_kbag(argv[2]);
	}
	if(argc == 3 && strcmp(argv[1], "boot") == 0) {
		return cmd_boot(argv[2]);
	}

	usage(argv[0]);
	return EXIT_FAILURE;
}