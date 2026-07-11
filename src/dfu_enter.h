/* Copyright 2026 iDFU authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * DFU entry helpers built on libimobiledevice / libirecovery / libusbmuxd,
 * following the same recovery->DFU orchestration as palera1n's dfuhelper.
 */
#ifndef IDFU_DFU_ENTER_H
#define IDFU_DFU_ENTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	IDFU_MODE_NONE = 0,
	IDFU_MODE_NORMAL,
	IDFU_MODE_RECOVERY,
	IDFU_MODE_DFU,
} idfu_mode_t;

typedef struct {
	idfu_mode_t mode;
	char        udid[64];
	uint64_t    ecid;
	uint32_t    cpid;
	uint32_t    bdid;
	bool        has_ecid;
	bool        no_home_button; /* A11-style: side + volume down */
} idfu_device_t;

/* Detect first connected Apple device across normal / recovery / DFU. */
bool idfu_probe_device(idfu_device_t *out, char *err, size_t err_sz);

/* Normal mode -> Recovery via lockdownd (libimobiledevice). */
bool idfu_enter_recovery(const char *udid, char *err, size_t err_sz);

/*
 * Recovery helpers (libirecovery), same semantics as palera1n:
 *   autoboot: setenv auto-boot true + saveenv
 *   exitrecv: autoboot + irecv_reboot  (soft reboot; ~2s black screen)
 */
bool idfu_recovery_autoboot(uint64_t ecid, char *err, size_t err_sz);
bool idfu_recovery_exitrecv(uint64_t ecid, char *err, size_t err_sz);

/* Poll until DFU (0x1227) appears, or timeout_ms elapses. */
bool idfu_wait_dfu(unsigned timeout_ms);

/* True if a DFU device is currently present. */
bool idfu_dfu_present(void);

#ifdef __cplusplus
}
#endif

#endif /* IDFU_DFU_ENTER_H */
