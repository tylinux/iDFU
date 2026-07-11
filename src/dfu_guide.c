/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * Interactive DFU entry guide (palera1n dfuhelper-style):
 *   normal  -> lockdownd EnterRecovery
 *   recovery -> setenv auto-boot true + saveenv + irecv_reboot
 *            + timed Side/Volume-Down (or Home) button sequence
 *   wait for DFU PID 0x1227
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
		if(idfu_probe_device(dev, err, err_sz) &&
		   (dev->mode == IDFU_MODE_RECOVERY || dev->mode == IDFU_MODE_DFU))
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

	print_step(both);
	step_countdown(2, both, false);

	print_step("Sending irecv_reboot (soft reboot — expect ~2s black screen)");
	if(!idfu_recovery_exitrecv(dev->ecid, err, sizeof err)) {
		print_warn(err);
		print_warn("Soft reboot failed; try the manual button sequence while the device is in Recovery.");
	} else {
		print_ok("irecv_reboot issued.");
	}

	if(step_countdown(2, both, true)) {
		print_ok("DFU mode detected!");
		return true;
	}
	if(step_countdown(10, one, true)) {
		print_ok("DFU mode detected!");
		return true;
	}

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
	puts("Uses libimobiledevice + libirecovery.");
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

bool
dfu_exit_normal_run(void) {
	char err[256];
	idfu_device_t dev;

	puts("iDFU exit to normal mode");
	puts("------------------------");

	if(!idfu_probe_device(&dev, err, sizeof err)) {
		print_warn(err);
		return false;
	}

	switch(dev.mode) {
	case IDFU_MODE_NORMAL:
		print_ok("Device is already in normal mode.");
		if(dev.udid[0])
			printf("  UDID=%s\n", dev.udid);
		return true;
	case IDFU_MODE_RECOVERY:
		print_ok("Device in Recovery.");
		if(dev.has_ecid)
			printf("  ECID=0x%llx\n", (unsigned long long)dev.ecid);
		print_step("setenv auto-boot true + saveenv + irecv_reboot (irecovery -n)");
		if(!idfu_exit_to_normal(err, sizeof err)) {
			print_warn(err);
			return false;
		}
		print_ok("Reboot to normal issued.");
		return true;
	case IDFU_MODE_DFU:
		print_ok("Device in DFU.");
		if(dev.has_ecid)
			printf("  ECID=0x%llx  CPID=0x%x\n",
			       (unsigned long long)dev.ecid, dev.cpid);
		print_step("Attempting exit from DFU");
		if(idfu_exit_to_normal(err, sizeof err)) {
			/* Soft path worked, or reset + guidance. */
			if(strstr(err, "BootROM DFU") != NULL) {
				print_warn(err);
				puts("");
				puts("Manual force-restart (A11 example):");
				puts("  1. Hold Side + Volume Down ~10s until Apple logo would appear,");
				puts("     then release both and briefly press Side to power on.");
				puts("  2. If auto-boot was set true before DFU entry, iOS should boot.");
				puts("  3. If you land in Recovery instead, run: idfu exit");
				return true;
			}
			print_ok("Exit command issued.");
			return true;
		}
		print_warn(err);
		return false;
	default:
		print_warn("Unknown mode.");
		return false;
	}
}
