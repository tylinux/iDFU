/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0.
 *
 * Device mode probe + normal/recovery -> DFU orchestration using official
 * limd stack (libimobiledevice, libusbmuxd, libirecovery, libplist).
 * Logic mirrors palera1n src/dfuhelper.c + src/devhelper.c.
 */
#include "dfu_enter.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libirecovery.h>
#include <usbmuxd.h>

#include "usb.h"

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
		/* Also accept libirecovery DFU open (covers edge re-enum cases). */
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

	/* Prefer DFU / Recovery via libirecovery. */
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

	/* Fallback PID probe (same numbers as gaster/checkra1n). */
	if(usb_device_present(APPLE_VID, DFU_MODE_PID)) {
		out->mode = IDFU_MODE_DFU;
		return true;
	}
	if(usb_device_present(APPLE_VID, RECOVERY_MODE_PID)) {
		out->mode = IDFU_MODE_RECOVERY;
		return true;
	}

	/* Normal mode via usbmuxd. */
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
	e = irecv_setenv(client, "auto-boot", "true");
	if(e != IRECV_E_SUCCESS)
		goto fail;
	e = irecv_saveenv(client);
	if(e != IRECV_E_SUCCESS)
		goto fail;
	e = irecv_reboot(client);
	if(e != IRECV_E_SUCCESS)
		goto fail;
	irecv_close(client);
	return true;
fail:
	set_err(err, err_sz, irecv_strerror(e));
	irecv_close(client);
	return false;
}
