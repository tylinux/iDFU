/* Copyright 2023 0x7ff
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef IDFU_IMG4_H
#	define IDFU_IMG4_H
#	include <stdint.h>
#	include <stddef.h>
#	include <stdbool.h>
#	include "usb.h"

typedef struct {
	const uint8_t *buf;
	size_t len;
} der_item_t;

typedef struct {
	uint8_t off, tag, flags;
} der_item_spec_t;

typedef struct {
	der_item_t magic, type, vers, data, kbag, comp;
} im4p_t;

typedef struct {
	der_item_t magic;
	im4p_t im4p;
} img4_t;

bool img4_init(const uint8_t *src, size_t src_len, img4_t *img4);
bool img4_get_kbag(img4_t img4, uint8_t *kbag);
bool img4_decrypt(img4_t img4, uint8_t *kbag, uint8_t **dec, size_t *dec_sz);
bool gaster_decrypt(usb_handle_t *handle, const uint8_t *src, size_t src_len, uint8_t **dst, size_t *dst_len);
bool gaster_decrypt_kbag(usb_handle_t *handle, const char *kbag_str);

#endif /* IDFU_IMG4_H */