/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Interactive DFU entry guide.
 *
 * Flow (aligned with palera1n dfuhelper):
 *   normal  -> lockdownd EnterRecovery (libimobiledevice)
 *   recovery -> setenv auto-boot true + saveenv + irecv_reboot (libirecovery)
 *            + timed Side/Volume-Down (or Home) button sequence
 *   wait for DFU PID 0x1227
 *
 * The ~2s black screen after reboot is from irecv_reboot(), not a hard reset.
 */
#include "dfu_guide.h"
#include "dfu_enter.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

/* Countdown helper: print `text (remaining)` each second.
 * If expect_dfu is true, stop early when DFU appears. */
static bool
step_countdown(int seconds, const char *text, bool expect_dfu) {
	for(int left = seconds; left > 0; left--) {
		if(expect_dfu && idfu_dfu_present()) {
			printf("\r\033[K");
			fflush(stdout);
			return true;
		}
		printf("\r\033[K  %s (%d) ", text, left);
		fflush(stdout);
		sleep(1);
	}
	printf("\r\033[K  %s (0)\n", text);
	fflush(stdout);
	return expect_dfu && idfu_dfu_present();
}

static bool
wait_recovery_device(idfu_device_t *dev, unsigned timeout_s, char *err, size_t err_sz) {
	for(unsigned i = 0; i < timeout_s * 5; i++) {
		if(idfu_probe_device(dev, err, err_sz) && dev->mode == IDFU_MODE_RECOVERY)
			return true;
		if(idfu_probe_device(dev, err, err_sz) && dev->mode == IDFU_MODE_DFU)
			return true;
		usleep(200 * 1000);
	}
	if(err && err_sz)
		snprintf(err, err_sz, "timed out waiting for Recovery/DFU");
	return false;
}

static bool
recovery_to_dfu(idfu_device_t *dev) {
	char err[256];

	if(dev->mode == IDFU_MODE_DFU) {
		print_ok("Already in DFU mode.");
		return true;
	}
	if(dev->mode != IDFU_MODE_RECOVERY) {
		print_warn("Not in Recovery mode.");
		return false;
	}

	/* Ensure we have ECID for irecv_open_with_ecid. */
	if(!dev->has_ecid) {
		idfu_device_t again;
		if(idfu_probe_device(&again, err, sizeof err) && again.has_ecid)
			*dev = again;
	}
	if(!dev->has_ecid) {
		print_warn("Could not read device ECID from Recovery; cannot soft-reboot via irecv.");
		return false;
	}

	printf("  ECID=0x%llx  CPID=0x%x  BDID=0x%x  %s\n",
	       (unsigned long long)dev->ecid, dev->cpid, dev->bdid,
	       dev->no_home_button ? "(no Home button: Side + Volume Down)"
	                           : "(Home + Power)");

	print_step("Setting auto-boot=true (libirecovery)");
	if(!idfu_recovery_autoboot(dev->ecid, err, sizeof err)) {
		print_warn(err);
		/* continue anyway; exitrecv will set it again */
	} else {
		print_ok("auto-boot saved.");
	}

	puts("");
	puts("Get ready. Fingers on the buttons now.");
	puts("When the device reboots (screen goes black ~2s after reboot command),");
	puts("you must already be holding the combination below.");
	puts("");
	printf("Press ENTER when ready... ");
	fflush(stdout);
	int c;
	while((c = getchar()) != '\n' && c != EOF) {
	}

	const char *both = dev->no_home_button
	                       ? "Hold Volume Down + Side button"
	                       : "Hold Home + Power button";
	const char *one = dev->no_home_button
	                      ? "Hold Volume Down button"
	                      : "Hold Home button";

	/* palera1n: step(4,2,...) then exitrecv then step(2,0)+step(10,0) */
	print_step(both);
	step_countdown(2, both, false);

	print_step("Sending irecv_reboot (soft reboot — expect ~2s black screen)");
	if(!idfu_recovery_exitrecv(dev->ecid, err, sizeof err)) {
		print_warn(err);
		print_warn("Soft reboot failed; try the manual button sequence while the device is in Recovery.");
	} else {
		print_ok("irecv_reboot issued.");
	}

	/* Cover SecureROM window: keep both buttons, then only volume/home. */
	if(step_countdown(2, both, true)) {
		print_ok("DFU mode detected!");
		return true;
	}
	if(step_countdown(10, one, true)) {
		print_ok("DFU mode detected!");
		return true;
	}

	/* Grace poll */
	print_step("Waiting for DFU re-enumeration...");
	if(idfu_wait_dfu(15000)) {
		print_ok("DFU mode detected!");
		return true;
	}

	print_warn("DFU not seen. Device may have returned to Recovery or Normal.");
	puts("  Tips:");
	puts("  - Align button hold with the soft reboot (do not wait for a long hard reset).");
	puts("  - Keep holding Volume Down/Home through the black screen.");
	puts("  - Prefer a direct USB port / known-good cable.");
	return false;
}

bool
dfu_guide_run(void) {
	char err[256];
	idfu_device_t dev;

	puts("iDFU interactive DFU guide (limd / palera1n-style)");
	puts("-------------------------------------------------");
	puts("Uses libimobiledevice + libirecovery (static when available).");
	puts("Connect the device over USB. Unlock + Trust this computer if asked.");
	puts("");

	print_step("Probing for device (normal / recovery / DFU)");
	if(!idfu_probe_device(&dev, err, sizeof err)) {
		print_warn(err);
		puts("Waiting up to 120s for a device...");
		bool found = false;
		for(int i = 0; i < 120 * 5; i++) {
			if(idfu_probe_device(&dev, err, sizeof err)) {
				found = true;
				break;
			}
			usleep(200 * 1000);
		}
		if(!found) {
			print_warn("No device found. Aborting.");
			return false;
		}
	}

	if(dev.mode == IDFU_MODE_DFU) {
		print_ok("Device is already in DFU mode.");
		return true;
	}

	if(dev.mode == IDFU_MODE_NORMAL) {
		print_ok("Device in NORMAL mode.");
		if(dev.udid[0])
			printf("  UDID=%s\n", dev.udid);
		print_step("Requesting lockdownd EnterRecovery");
		if(!idfu_enter_recovery(dev.udid[0] ? dev.udid : NULL, err, sizeof err)) {
			print_warn(err);
			puts("Unlock the device, tap Trust, ensure usbmuxd is running, then retry.");
			puts("Or put the device into Recovery manually and re-run `idfu guide`.");
			return false;
		}
		print_ok("EnterRecovery request sent.");
		print_step("Waiting for Recovery mode");
		if(!wait_recovery_device(&dev, 60, err, sizeof err)) {
			print_warn(err);
			return false;
		}
		if(dev.mode == IDFU_MODE_DFU) {
			print_ok("Device landed in DFU unexpectedly — success.");
			return true;
		}
		print_ok("Recovery mode reached.");
	} else if(dev.mode == IDFU_MODE_RECOVERY) {
		print_ok("Device already in Recovery mode.");
	} else {
		print_warn("Unknown mode.");
		return false;
	}

	return recovery_to_dfu(&dev);
}
