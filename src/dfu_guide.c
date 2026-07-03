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
#include "lockdown.h"
#include "usbmux.h"
#include "usb_watcher.h"
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

/* Wait for any Apple device to appear (normal/recovery/DFU). Returns
 * the PID that appeared, or 0 on timeout.
 *
 * Recovery/DFU are matched on their fixed PIDs. A "normal" (lockdownd-
 * visible, pairing-capable) device is detected via usbmuxd's ListDevices,
 * because these devices cycle through several PIDs depending on trusted
 * vs untrusted state (0x12a8 / 0x12a9 / 0x1298 / ...) and we do not want
 * to enumerate every variant. The returned sentinel is APPLE_VID. */
#define APPLE_NORMAL_SENTINEL (APPLE_VID)

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
		/* usbmuxd-visible USB device => normal mode (lockdownd reachable). */
		uint32_t did;
		char udid[128];
		if(usbmux_find_first_usb_device(&did, udid, sizeof udid, NULL)) {
			return APPLE_NORMAL_SENTINEL;
		}
		usleep(POLL_INTERVAL_MS * 1000);
		elapsed += POLL_INTERVAL_MS;
	}
	return 0;
}

/* Drive a device in normal/trusted mode into Recovery mode by issuing the
 * EnterRecovery request through usbmux -> lockdownd (no external tools).
 * On failure, falls back to providing the manual button sequence. Polls
 * for the Recovery-mode PID (0x1281) to appear. */
static bool
enter_recovery_via_lockdownd(unsigned timeout_ms) {
	const char *err = NULL;
	printf("Asking lockdownd to enter Recovery mode... ");
	fflush(stdout);
	if(lockdown_enter_recovery(&err)) {
		print_ok("request sent.");
		printf("Waiting for Recovery mode (PID 0x1281)... ");
		fflush(stdout);
		if(!poll_present(APPLE_VID, RECOVERY_MODE_PID, true, timeout_ms)) {
			print_warn("Recovery mode did not appear within timeout.");
			return false;
		}
		print_ok("Recovery mode reached.");
		return true;
	}
	printf("FAILED\n");
	if(err) printf("  %s\n", err);
	print_warn("Automatic recovery failed (locked / not trusted / daemon down).");
	puts("    On Linux ensure `usbmuxd` is installed and running;");
	puts("    on macOS it ships with the system. If the device is passcode");
	puts("    locked, unlock it and tap 'Trust This Computer' first.");
	puts("");
	puts("Falling back to the manual button sequence:");
	puts("  1. Press and quickly release Volume Up.");
	puts("  2. Press and quickly release Volume Down.");
	puts("  3. Press and HOLD the Side (power) button until the screen");
	puts("     goes black, keep holding, until the 'connect to computer'");
	puts("     Recovery screen appears.");
	puts("    (Home-button devices: press & hold Side+Home together.)");
	printf("Waiting for Recovery mode... ");
	fflush(stdout);
	if(!poll_present(APPLE_VID, RECOVERY_MODE_PID, true, timeout_ms)) {
		print_warn("Recovery mode did not appear within timeout.");
		return false;
	}
	print_ok("Recovery mode reached.");
	return true;
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
	{
		uint32_t did; char udid[128];
		bool already = usb_device_present(APPLE_VID, DFU_MODE_PID) ||
		               usb_device_present(APPLE_VID, RECOVERY_MODE_PID) ||
		               usbmux_find_first_usb_device(&did, udid, sizeof udid, NULL);
		if(already) {
			puts("An Apple USB device is already connected; using it.");
		} else {
			puts("Plug it in now. If it's already plugged in, unplug it first so");
			puts("the guide can detect a clean insertion.");
			if(!poll_present(APPLE_VID, RECOVERY_MODE_PID, false, 10000)) {
				print_warn("Could not confirm the device was unplugged; continuing anyway.");
			}
		}
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
	if(first == APPLE_NORMAL_SENTINEL) {
		print_ok("Device detected in NORMAL mode (trusted/pair-capable).");
		puts("Will enter Recovery mode automatically via usbmux/lockdownd.");
		puts("Make sure the device is unlocked and trusted by this computer.");
		if(!enter_recovery_via_lockdownd(60000)) {
			return false;
		}
		/* fall through to the Recovery -> DFU steps below */
	} else {
		print_ok("Device detected in Recovery mode.");
	}

	/* Recovery -> DFU: use the host to trigger a device-side detach+reset
	 * into DFU (the iBoot recovery USB stack honours a DFU_DETACH class
	 * request), so you do NOT need the 6s long-press / manual timing.
	 * We keep the IOKit hot-plug watcher alive across the reboot so the
	 * USB host port stays powered, and we then poll for the DFU PID.
	 * HOLD Volume Down (iPhone X+) / Home (older) the whole time.
	 *
	 * If the auto-reseat fails, we fall back to the manual button sequence. */
	print_step("HOLD Volume Down (or Home) now. The device will reset shortly.");
	fflush(stdout);
	if(!usb_trigger_recovery_reset()) {
		print_warn("Auto reset request failed; falling back to manual button sequence.");
	} else {
		print_ok("User-request has been sent.");
		puts("    Keep holding Volume Down (or Home). The guide waits for DFU...");
	}

	/* Begin the USB-host-port watcher now so the port stays alive for the
	 * reboot into BootROM, then poll straight for the DFU PID. */
	bool result;
	if(usb_watcher_start()) {
		puts("(USB host port is now held active for the reboot cycle.)");
	} else {
		print_warn("usb_watcher could not be started; the host USB port may go");
		puts("    to sleep during the reboot. Continuing anyway.");
	}

	puts("Waiting for the device to re-appear in DFU mode (PID 0x1227)...");
	printf("Waiting for DFU mode... ");
	fflush(stdout);

	/* The device typically re-enumerates in DFU within ~10 s. */
	if(poll_present(APPLE_VID, DFU_MODE_PID, true, 30000)) {
		print_ok("DFU mode detected!");
		result = true;
		goto done;
	}

	/* Auto reset did not land in DFU: hand the user the standard button
	 * sequence to finish it manually. */
	print_warn("Auto reset did not land in DFU within 30s.");
	puts("");
	print_button_instructions();
	puts("Try the manual button sequence now (these buttons, NOT the Side");
	puts("button alone): press and HOLD the Side/Top button ~8s, then WITH");
	puts("Volume Down (or Home) held another ~10s, then release the Side/");
	puts("Top button but keep holding Volume Down/Home ~5 more seconds.");
	result = false;

done:
	usb_watcher_stop();
	return result;
}