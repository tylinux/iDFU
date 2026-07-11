/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * Device mode probe + normal/recovery/DFU helpers using the limd stack
 * (libimobiledevice, libusbmuxd, libirecovery). Logic mirrors palera1n
 * dfuhelper/devhelper and irecovery -n for exit-to-normal.
 */
#include "dfu_enter.h"
#include "usb_probe.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libirecovery.h>
#include <usbmuxd.h>

static void
set_err(char *err, size_t err_sz, const char *msg) {
	if(err && err_sz)
		snprintf(err, err_sz, "%s", msg ? msg : "unknown error");
}

static bool
no_physical_home(uint32_t cpid, uint32_t bdid) {
	/* Same predicate as palera1n NO_PHYSICAL_HOME_BUTTON. */
	return cpid == 0x8015 ||
	       (cpid == 0x8010 &&
	        (bdid == 0x08 || bdid == 0x0a || bdid == 0x0c || bdid == 0x0e));
}

static bool
fill_from_irecv(idfu_device_t *out, irecv_client_t client, idfu_mode_t mode) {
	const struct irecv_device_info *info = irecv_get_device_info(client);
	if(!info)
		return false;
	out->mode = mode;
	out->ecid = info->ecid;
	out->cpid = info->cpid;
	out->bdid = info->bdid;
	out->has_ecid = true;
	out->no_home_button = no_physical_home(info->cpid, info->bdid);
	out->udid[0] = '\0';
	return true;
}

static irecv_error_t
recovery_set_autoboot_and_reboot(irecv_client_t client) {
	irecv_error_t e = irecv_setenv(client, "auto-boot", "true");
	if(e != IRECV_E_SUCCESS)
		return e;
	e = irecv_saveenv(client);
	if(e != IRECV_E_SUCCESS)
		return e;
	return irecv_reboot(client);
}

bool
idfu_dfu_present(void) {
	return usb_device_present(APPLE_VID, DFU_MODE_PID);
}

bool
idfu_wait_dfu(unsigned timeout_ms) {
	unsigned elapsed = 0;
	const unsigned step = 200;
	while(elapsed < timeout_ms) {
		if(idfu_dfu_present())
			return true;
		irecv_client_t client = NULL;
		if(irecv_open_with_ecid(&client, 0) == IRECV_E_SUCCESS) {
			int mode = 0;
			if(irecv_get_mode(client, &mode) == IRECV_E_SUCCESS &&
			   mode == IRECV_K_DFU_MODE) {
				irecv_close(client);
				return true;
			}
			irecv_close(client);
		}
		usleep(step * 1000);
		elapsed += step;
	}
	return false;
}

bool
idfu_probe_device(idfu_device_t *out, char *err, size_t err_sz) {
	if(!out) {
		set_err(err, err_sz, "null out");
		return false;
	}
	memset(out, 0, sizeof(*out));

	irecv_client_t client = NULL;
	if(irecv_open_with_ecid(&client, 0) == IRECV_E_SUCCESS) {
		int mode = 0;
		if(irecv_get_mode(client, &mode) == IRECV_E_SUCCESS) {
			if(mode == IRECV_K_DFU_MODE) {
				fill_from_irecv(out, client, IDFU_MODE_DFU);
				irecv_close(client);
				return true;
			}
			if(mode == IRECV_K_RECOVERY_MODE_1 || mode == IRECV_K_RECOVERY_MODE_2 ||
			   mode == IRECV_K_RECOVERY_MODE_3 || mode == IRECV_K_RECOVERY_MODE_4) {
				fill_from_irecv(out, client, IDFU_MODE_RECOVERY);
				irecv_close(client);
				return true;
			}
		}
		irecv_close(client);
	}

	if(usb_device_present(APPLE_VID, DFU_MODE_PID)) {
		out->mode = IDFU_MODE_DFU;
		return true;
	}
	if(usb_device_present(APPLE_VID, RECOVERY_MODE_PID)) {
		out->mode = IDFU_MODE_RECOVERY;
		return true;
	}

	usbmuxd_device_info_t *devs = NULL;
	int n = usbmuxd_get_device_list(&devs);
	if(n > 0 && devs) {
		for(int i = 0; i < n; i++) {
			if(devs[i].conn_type != CONNECTION_TYPE_USB)
				continue;
			out->mode = IDFU_MODE_NORMAL;
			snprintf(out->udid, sizeof(out->udid), "%s", devs[i].udid);
			usbmuxd_device_list_free(&devs);
			return true;
		}
		usbmuxd_device_list_free(&devs);
	}

	set_err(err, err_sz, "no Apple device found (normal/recovery/DFU)");
	return false;
}

bool
idfu_enter_recovery(const char *udid, char *err, size_t err_sz) {
	idevice_t device = NULL;
	lockdownd_client_t lockdown = NULL;
	lockdownd_error_t ldret;

	if(idevice_new(&device, udid && udid[0] ? udid : NULL) != IDEVICE_E_SUCCESS) {
		set_err(err, err_sz, "idevice_new failed (is usbmuxd running? device trusted?)");
		return false;
	}

	ldret = lockdownd_client_new(device, &lockdown, "idfu");
	if(ldret != LOCKDOWN_E_SUCCESS) {
		set_err(err, err_sz, lockdownd_strerror(ldret));
		idevice_free(device);
		return false;
	}

	ldret = lockdownd_enter_recovery(lockdown);
	if(ldret == LOCKDOWN_E_SESSION_INACTIVE) {
		lockdownd_client_free(lockdown);
		lockdown = NULL;
		ldret = lockdownd_client_new_with_handshake(device, &lockdown, "idfu");
		if(ldret != LOCKDOWN_E_SUCCESS) {
			set_err(err, err_sz, lockdownd_strerror(ldret));
			idevice_free(device);
			return false;
		}
		ldret = lockdownd_enter_recovery(lockdown);
	}

	if(ldret != LOCKDOWN_E_SUCCESS) {
		set_err(err, err_sz, lockdownd_strerror(ldret));
		lockdownd_client_free(lockdown);
		idevice_free(device);
		return false;
	}

	lockdownd_client_free(lockdown);
	idevice_free(device);
	return true;
}

bool
idfu_recovery_autoboot(uint64_t ecid, char *err, size_t err_sz) {
	irecv_client_t client = NULL;
	irecv_error_t e = irecv_open_with_ecid(&client, ecid);
	if(e != IRECV_E_SUCCESS) {
		set_err(err, err_sz, irecv_strerror(e));
		return false;
	}
	e = irecv_setenv(client, "auto-boot", "true");
	if(e != IRECV_E_SUCCESS)
		goto fail;
	e = irecv_saveenv(client);
	if(e != IRECV_E_SUCCESS)
		goto fail;
	irecv_close(client);
	return true;
fail:
	set_err(err, err_sz, irecv_strerror(e));
	irecv_close(client);
	return false;
}

bool
idfu_recovery_exitrecv(uint64_t ecid, char *err, size_t err_sz) {
	/* palera1n exitrecv_cmd: setenv + saveenv + reboot */
	irecv_client_t client = NULL;
	irecv_error_t e = irecv_open_with_ecid(&client, ecid);
	if(e != IRECV_E_SUCCESS) {
		set_err(err, err_sz, irecv_strerror(e));
		return false;
	}
	e = recovery_set_autoboot_and_reboot(client);
	if(e != IRECV_E_SUCCESS) {
		set_err(err, err_sz, irecv_strerror(e));
		irecv_close(client);
		return false;
	}
	irecv_close(client);
	return true;
}

bool
idfu_exit_to_normal(char *err, size_t err_sz) {
	idfu_device_t dev;
	char local_err[256];

	if(!idfu_probe_device(&dev, local_err, sizeof local_err)) {
		set_err(err, err_sz, local_err);
		return false;
	}

	if(dev.mode == IDFU_MODE_NORMAL) {
		set_err(err, err_sz, "device is already in normal mode");
		return false;
	}

	/*
	 * Recovery (and iBoot-style modes): same as `irecovery -n` —
	 * setenv auto-boot true + saveenv + reboot.
	 *
	 * Pure BootROM DFU (0x1227) has no NVRAM env; software cannot boot
	 * iOS from there without loading an image. We USB-reset and tell the
	 * caller to force-restart. If the guide already set auto-boot=true,
	 * a force restart lands in normal mode instead of Recovery.
	 */
	if(dev.mode == IDFU_MODE_RECOVERY) {
		uint64_t ecid = dev.has_ecid ? dev.ecid : 0;
		if(!idfu_recovery_exitrecv(ecid, err, err_sz))
			return false;
		return true;
	}

	if(dev.mode == IDFU_MODE_DFU) {
		irecv_client_t client = NULL;
		irecv_error_t open_e = irecv_open_with_ecid(&client, dev.has_ecid ? dev.ecid : 0);
		if(open_e == IRECV_E_SUCCESS) {
			irecv_error_t e = recovery_set_autoboot_and_reboot(client);
			if(e == IRECV_E_SUCCESS) {
				irecv_close(client);
				return true;
			}
			/* Fall back to USB reset so the port re-enumerates cleanly. */
			irecv_reset(client);
			irecv_close(client);
		}
		set_err(err, err_sz,
		        "BootROM DFU has no NVRAM; force-restart the device "
		        "(Side+Vol Down ~10s, then Side) to boot normal if auto-boot is true");
		/* Still report success with guidance after a best-effort reset. */
		return open_e == IRECV_E_SUCCESS;
	}

	set_err(err, err_sz, "unsupported mode for exit-to-normal");
	return false;
}
