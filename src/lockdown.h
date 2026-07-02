// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// Minimal lockdownd client: drives a device sitting in normal/trusted
// mode into Recovery mode via the EnterRecovery request. Self-contained:
// enumerates via usbmux, connects to lockdownd, queries type, sends the
// EnterRecovery plist. No pairing/StartSession (trusted devices accept
// EnterRecovery with just a QueryType).
#ifndef IDFU_LOCKDOWN_H
#	define IDFU_LOCKDOWN_H
#	include <stdbool.h>

/* Bring the attached (normal-mode) device into Recovery mode.
 * Uses the local usbmuxd + lockdownd on port 62078.
 * Returns true on success. On failure, *err (if non-NULL) is set to a
 * static diagnostic string. */
bool lockdown_enter_recovery(const char **err);

#endif /* IDFU_LOCKDOWN_H */