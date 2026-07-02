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
 * Interactive DFU entry guide. Walks the user through the Recovery ->
 * DFU button timing sequence, polling the USB layer for the device's
 * Recovery-mode (PID 0x1281) and DFU-mode (PID 0x1227) enumerations to
 * decide when to advance to the next step.
 */
#include "dfu_guide.h"
#include "usb.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define POLL_INTERVAL_MS     250
#define RECOVERY_TIMEOUT_MS  60000
#define DFU_TIMEOUT_MS       60000

static void
print_step(const char *msg) {
	printf("\n\033[1;33m==>\033[0m %s\n", msg);
	fflush(stdout);
}

static void
print_ok(const char *msg) {
	printf("\033[1;32m[+]\033[0m %s\n", msg);
	fflush(stdout);
}

static void
print_warn(const char *msg) {
	printf("\033[1;31m[!]\033[0m %s\n", msg);
	fflush(stdout);
}

/* Poll up to timeout_ms for usb_device_present(vid,pid) to match `want`. */
static bool
poll_present(uint16_t vid, uint16_t pid, bool want, unsigned timeout_ms) {
	unsigned elapsed = 0;
	while(elapsed < timeout_ms) {
		if(usb_device_present(vid, pid) == want) {
			return true;
		}
		usleep(POLL_INTERVAL_MS * 1000);
		elapsed += POLL_INTERVAL_MS;
	}
	return false;
}

/* Wait for either Apple Recovery or DFU to appear. Returns the PID that
 * appeared, or 0 on timeout. */
static uint16_t
wait_for_apple_device(unsigned timeout_ms) {
	unsigned elapsed = 0;
	while(elapsed < timeout_ms) {
		if(usb_device_present(APPLE_VID, DFU_MODE_PID)) {
			return DFU_MODE_PID;
		}
		if(usb_device_present(APPLE_VID, RECOVERY_MODE_PID)) {
			return RECOVERY_MODE_PID;
		}
		usleep(POLL_INTERVAL_MS * 1000);
		elapsed += POLL_INTERVAL_MS;
	}
	return 0;
}

static void
print_button_instructions(void) {
	puts("");
	puts("DFU button sequence (works for most devices):");
	puts("  1. Press and HOLD the Side/Top button (the power button).");
	puts("  2. After ~8 seconds, KEEP holding it and ALSO press and hold");
	puts("     the Volume Down button (iPhone X+) or Home button (older).");
	puts("  3. After ~10 more seconds, RELEASE the Side/Top button but");
	puts("     KEEP holding Volume Down/Home for another ~5 seconds.");
	puts("");
}

bool
dfu_guide_run(void) {
	puts("iDFU interactive DFU guide");
	puts("--------------------------");
	puts("This will guide you into DFU mode. Have your Lightning/USB cable");
	puts("ready. Connect the device to this Mac BEFORE starting the steps.");

	/* Step 1: connect device. */
	print_step("Step 1 / 4: Connect your device via USB.");
	puts("Plug it in now. If it's already plugged in, unplug it first so");
	puts("the guide can detect a clean insertion.");
	if(!poll_present(APPLE_VID, RECOVERY_MODE_PID, false, 10000)) {
		print_warn("Could not confirm the device was unplugged; continuing anyway.");
	}
	printf("Waiting for the device to enumerate... ");
	fflush(stdout);
	uint16_t first = wait_for_apple_device(120000);
	if(first == 0) {
		print_warn("No Apple USB device detected within 120s. Aborting.");
		return false;
	}
	if(first == DFU_MODE_PID) {
		print_ok("Device is already in DFU mode!");
		return true;
	}
	print_ok("Device detected in Recovery mode.");

	/* Step 2: hold power button 8s. */
	print_step("Step 2 / 4: Press and HOLD the Side/Top (power) button (8 seconds).");
	puts("Keep holding it. The guide will continue automatically when the");
	puts("device is ready to advance.");

	/* Step 3: hold Home/Volume Down 10s as well -> Device transitions. */
	print_step("Step 3 / 4: Continue holding Side/Top + press Volume Down (or Home)");
	puts("for 10 seconds. The screen should stay black. The guide will detect");
	puts("when the device leaves Recovery.");
	printf("Waiting for Recovery mode to disappear... ");
	fflush(stdout);
	if(!poll_present(APPLE_VID, RECOVERY_MODE_PID, false, RECOVERY_TIMEOUT_MS)) {
		print_warn("Recovery mode did not disappear. The button timing may be wrong.");
		print_button_instructions();
		return false;
	}
	print_ok("Recovery mode disappeared (good).");

	/* Step 4: Release power button, keep VolDown for 5s -> DFU appears. */
	print_step("Step 4 / 4: RELEASE the Side/Top button. KEEP holding Volume Down");
	puts("(or Home) for another ~5 seconds until DFU is detected.");
	printf("Waiting for DFU mode... ");
	fflush(stdout);
	if(!poll_present(APPLE_VID, DFU_MODE_PID, true, DFU_TIMEOUT_MS)) {
		print_warn("DFU mode did not appear within 60s. The button timing may be wrong.");
		print_button_instructions();
		return false;
	}
	print_ok("DFU mode detected!");
	return true;
}