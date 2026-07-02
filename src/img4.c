/* Copyright 2023 0x7ff
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
 * img4/im4p DER parsing, KBAG extraction and AES decryption via the
 * device GID0 key, plus LZSS decompression of the payload. Adapted
 * directly from gaster (0x7ff).
 */
#include "img4.h"
#include "checkm8.h"
#include "lzfse.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef HAVE_LIBUSB
#	include <openssl/evp.h>
#else
#	include <CommonCrypto/CommonCrypto.h>
#endif

#define DER_INT        (0x2U)
#define DER_SEQ        (0x30U)
#define LZSS_F         (18)
#define LZSS_N         (4096)
#define LZSS_THRESHOLD (2)
#define DER_IA5_STR    (0x16U)
#define DER_OCTET_STR  (0x4U)
#define DER_FLAG_OPTIONAL (1U << 0U)

static der_item_spec_t der_img4_item_specs[] = {
	{ 0, DER_IA5_STR, 0 },
	{ 1, DER_SEQ, 0 }
}, der_im4p_item_specs[] = {
	{ 0, DER_IA5_STR, 0 },
	{ 1, DER_IA5_STR, 0 },
	{ 2, DER_IA5_STR, 0 },
	{ 3, DER_OCTET_STR, 0 },
	{ 4, DER_OCTET_STR, DER_FLAG_OPTIONAL },
	{ 5, DER_SEQ, DER_FLAG_OPTIONAL }
};

static size_t
decompress_lzss(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len) {
	const uint8_t *src_end = src + src_len, *dst_start = dst, *dst_end = dst + dst_len;
	uint16_t i, r = LZSS_N - LZSS_F, flags = 0;
	uint8_t text_buf[LZSS_N + LZSS_F - 1], j;

	memset(text_buf, ' ', r);
	while(src != src_end && dst != dst_end) {
		if(((flags >>= 1U) & 0x100U) == 0) {
			flags = *src++ | 0xFF00U;
			if(src == src_end) {
				break;
			}
		}
		if((flags & 1U) != 0) {
			text_buf[r++] = *dst++ = *src++;
			r &= LZSS_N - 1U;
		} else {
			i = *src++;
			if(src == src_end) {
				break;
			}
			j = *src++;
			i |= (j & 0xF0U) << 4U;
			j = (j & 0xFU) + LZSS_THRESHOLD;
			do {
				*dst++ = text_buf[r++] = text_buf[i++ & (LZSS_N - 1U)];
				r &= LZSS_N - 1U;
			} while(j-- != 0 && dst != dst_end);
		}
	}
	return (size_t)(dst - dst_start);
}

static const uint8_t *
der_decode(const uint8_t *der, const uint8_t *der_end, size_t *out_len, uint8_t *tag) {
	size_t der_len;

	if(der_end - der > 2) {
		*tag = *der++;
		if(((der_len = *der++) & 0x80U) != 0) {
			*out_len = 0;
			if((der_len &= 0x7FU) <= sizeof(*out_len) && (size_t)(der_end - der) >= der_len) {
				while(der_len-- != 0) {
					*out_len = (*out_len << 8U) | *der++;
				}
			}
		} else {
			*out_len = der_len;
		}
		if(*out_len != 0 && (size_t)(der_end - der) >= *out_len) {
			return der;
		}
	}
	return NULL;
}

static const uint8_t *
der_decode_seq(const uint8_t *der, const uint8_t *der_end, const uint8_t **seq_end) {
	size_t der_len;
	uint8_t tag;

	if((der = der_decode(der, der_end, &der_len, &tag)) != NULL && tag == DER_SEQ) {
		*seq_end = der + der_len;
	}
	return der;
}

static const uint8_t *
der_decode_uint64(const uint8_t *der, const uint8_t *der_end, uint64_t *r) {
	size_t der_len;
	uint8_t tag;

	if((der = der_decode(der, der_end, &der_len, &tag)) != NULL && tag == DER_INT && (*der & 0x80U) == 0 && (der_len <= sizeof(*r) || (--der_len == sizeof(*r) && *der++ == 0))) {
		*r = 0;
		while(der_len-- != 0) {
			*r = (*r << 8U) | *der++;
		}
		return der;
	}
	return NULL;
}

static bool
der_parse_seq(const uint8_t *der, size_t der_len, const der_item_spec_t *specs, size_t spec_cnt, der_item_t *out) {
	const uint8_t *der_end;
	size_t i, off;
	uint8_t tag;

	if((der = der_decode_seq(der, der + der_len, &der_end)) != NULL) {
		for(i = 0; i < spec_cnt; ++i) {
			if((der = der_decode(der, der_end, &der_len, &tag)) == NULL) {
				for(; i < spec_cnt; ++i) {
					if((specs[i].flags & DER_FLAG_OPTIONAL) == 0) {
						return false;
					}
				}
				return true;
			}
			if(specs[i].tag == tag) {
				off = specs[i].off;
				out[off].buf = der;
				out[off].len = der_len;
			} else if((specs[i].flags & DER_FLAG_OPTIONAL) == 0) {
				return false;
			}
			der += der_len;
		}
		return true;
	}
	return false;
}

bool
img4_get_kbag(img4_t img4, uint8_t *kbag) {
	const uint8_t *iv, *key, *der, *der_end;
	size_t iv_len, key_len;
	bool ret = false;
	uint8_t tag;
	uint64_t r;

	if(img4.im4p.kbag.buf != NULL && (der = der_decode_seq(img4.im4p.kbag.buf, img4.im4p.kbag.buf + img4.im4p.kbag.len, &der_end)) != NULL && (der = der_decode_seq(der, der_end, &der_end)) != NULL && (der = der_decode_uint64(der, der_end, &r)) != NULL && r == 1 && (iv = der_decode(der, der_end, &iv_len, &tag)) != NULL && tag == DER_OCTET_STR && iv_len == AES_BLOCK_SZ && (key = der_decode(iv + iv_len, der_end, &key_len, &tag)) != NULL && tag == DER_OCTET_STR && key_len == AES_KEY_SZ_BYTES_256) {
		memcpy(kbag, iv, iv_len);
		memcpy(kbag + iv_len, key, key_len);
		ret = true;
	}
	return ret;
}

bool
img4_init(const uint8_t *src, size_t src_len, img4_t *img4) {
	memset(img4, '\0', sizeof(*img4));
	return (der_parse_seq(src, src_len, der_img4_item_specs, sizeof(der_img4_item_specs) / sizeof(der_img4_item_specs[0]), &img4->magic) && img4->magic.len == 4 && memcmp(img4->magic.buf, "IMG4", img4->magic.len) == 0) || (der_parse_seq(src, src_len, der_im4p_item_specs, sizeof(der_im4p_item_specs) / sizeof(der_im4p_item_specs[0]), &img4->im4p.magic) && img4->im4p.magic.len == 4 && memcmp(img4->im4p.magic.buf, "IM4P", img4->im4p.magic.len) == 0);
}

static bool
aes_256_cbc_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *data_src, uint8_t *data_dst, size_t data_sz) {
#ifdef HAVE_LIBUSB
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	bool ret = false;
	int out_sz;

	if(ctx != NULL) {
		ret = EVP_DecryptInit(ctx, EVP_aes_256_cbc(), key, iv) == 1 && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 && EVP_DecryptUpdate(ctx, data_dst, &out_sz, data_src, (int)data_sz) == 1 && out_sz > 0 && (size_t)out_sz == data_sz && EVP_DecryptFinal(ctx, data_dst + out_sz, &out_sz) == 1 && out_sz == 0;
		EVP_CIPHER_CTX_free(ctx);
	}
	return ret;
#else
	size_t out_sz;

	return CCCrypt(kCCDecrypt, kCCAlgorithmAES, 0, key, AES_KEY_SZ_BYTES_256, iv, data_src, data_sz, data_dst, data_sz, &out_sz) == kCCSuccess && out_sz == data_sz;
#endif
}

bool
img4_decrypt(img4_t img4, uint8_t *kbag, uint8_t **dec, size_t *dec_sz) {
	struct {
		uint32_t magic, type, adler32, uncomp_sz, comp_sz;
		uint8_t pad[COMP_HDR_PAD_SZ];
	} comp_hdr;
	const uint8_t *der, *der_end;
	bool ret = false;
	uint8_t *data;
	uint64_t r;

	if(img4.im4p.data.len > sizeof(comp_hdr) && (data = malloc(img4.im4p.data.len)) != NULL) {
		if(aes_256_cbc_decrypt(kbag + AES_BLOCK_SZ, kbag, img4.im4p.data.buf, data, img4.im4p.data.len)) {
			if(img4.im4p.comp.buf != NULL) {
				der = img4.im4p.comp.buf;
				der_end = der + img4.im4p.comp.len;
				if((der = der_decode_uint64(der, der_end, &r)) != NULL && r == 1 && der_decode_uint64(der, der_end, &r) != NULL && r != 0 && (*dec = malloc((size_t)r)) != NULL) {
					if(lzfse_decode_buffer(*dec, (size_t)r, data, img4.im4p.data.len, NULL) == r) {
						*dec_sz = (size_t)r;
						ret = true;
					} else {
						free(*dec);
					}
				}
			} else {
				memcpy(&comp_hdr, data, sizeof(comp_hdr));
				if(comp_hdr.magic == __builtin_bswap32(COMP_HDR_MAGIC) && comp_hdr.type == __builtin_bswap32(COMP_HDR_TYPE_LZSS) && (comp_hdr.comp_sz = __builtin_bswap32(comp_hdr.comp_sz)) <= img4.im4p.data.len - sizeof(comp_hdr) && (comp_hdr.uncomp_sz = __builtin_bswap32(comp_hdr.uncomp_sz)) != 0 && (*dec = malloc(comp_hdr.uncomp_sz)) != NULL) {
					if(decompress_lzss(data, comp_hdr.comp_sz, *dec, comp_hdr.uncomp_sz) == comp_hdr.uncomp_sz) {
						*dec_sz = comp_hdr.uncomp_sz;
						ret = true;
					} else {
						free(*dec);
					}
				} else if((*dec = malloc(img4.im4p.data.len)) != NULL) {
					memcpy(*dec, data, img4.im4p.data.len);
					*dec_sz = img4.im4p.data.len;
					ret = true;
				}
			}
		}
		free(data);
	}
	return ret;
}

bool
gaster_decrypt(usb_handle_t *handle, const uint8_t *src, size_t src_len, uint8_t **dst, size_t *dst_len) {
	uint8_t kbag[AES_BLOCK_SZ + AES_KEY_SZ_BYTES_256];
	img4_t img4;

	return img4_init(src, src_len, &img4) && img4_get_kbag(img4, kbag) && gaster_aes(handle, AES_CMD_CBC | AES_CMD_DEC, kbag, kbag, sizeof(kbag), AES_KEY_SZ_256 | AES_KEY_TYPE_GID0) && img4_decrypt(img4, kbag, dst, dst_len);
}

bool
gaster_decrypt_kbag(usb_handle_t *handle, const char *kbag_str) {
	uint8_t kbag[AES_BLOCK_SZ + AES_KEY_SZ_BYTES_256];
	bool ret = false;
	size_t i;

	if(strlen(kbag_str) == 2 * sizeof(kbag)) {
		for(i = 0; i < sizeof(kbag); ++i) {
			if(sscanf(&kbag_str[2 * i], "%02" SCNx8, &kbag[i]) != 1) {
				break;
			}
		}
		if(i == sizeof(kbag) && checkm8_exploit(handle) && gaster_aes(handle, AES_CMD_CBC | AES_CMD_DEC, kbag, kbag, sizeof(kbag), AES_KEY_SZ_256 | AES_KEY_TYPE_GID0)) {
			printf("IV: ");
			for(i = 0; i < AES_BLOCK_SZ; ++i) {
				printf("%02" PRIX8, kbag[i]);
			}
			printf(", key: ");
			for(i = 0; i < AES_KEY_SZ_BYTES_256; ++i) {
				printf("%02" PRIX8, kbag[AES_BLOCK_SZ + i]);
			}
			putchar('\n');
			ret = true;
		}
	}
	return ret;
}