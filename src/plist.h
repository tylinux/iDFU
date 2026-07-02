// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// Minimal in-memory plist for iDFU's usbmux/lockdownd client. Only the node
// kinds actually used by the EnterRecovery flow are modeled: null, boolean,
// integer (up to 64-bit), string, dict, array. The XML encoder produces XML
// plists (used for *requests* over usbmuxd and lockdownd). The bplist00
// decoder accepts only what the EnterRecovery responses use (dict/string/
// int/null/bool/array).
#ifndef IDFU_PLIST_H
#	define IDFU_PLIST_H
#	include <stdbool.h>
#	include <stddef.h>
#	include <stdint.h>

typedef enum {
	PLIST_NULL = 0,
	PLIST_BOOL,
	PLIST_UINT,
	PLIST_STRING,
	PLIST_ARRAY,
	PLIST_DICT
} plist_kind_t;

typedef struct plist_node plist_node_t;

struct plist_node {
	plist_kind_t kind;
	union {
		bool b;
		uint64_t u;
		char *str;          /* owned by node (NUL-terminated) */
		struct {
			plist_node_t **items;
			size_t count;
		} arr;
		struct {
			char **keys;        /* owned strings */
			plist_node_t **vals;
			size_t count;
		} dict;
	} v;
};

plist_node_t *plist_new_null(void);
plist_node_t *plist_new_bool(bool b);
plist_node_t *plist_new_uint(uint64_t u);
plist_node_t *plist_new_string(const char *s);

/* Strictly appends; does not dedupe. dict takes ownership of key copy. */
void     plist_dict_set(plist_node_t *dict, const char *key, plist_node_t *val);
void     plist_array_append(plist_node_t *arr, plist_node_t *item);

plist_node_t *plist_dict_get(const plist_node_t *dict, const char *key);
const char   *plist_dict_get_string(const plist_node_t *dict, const char *key);

/* Serialise to an XML plist. *out is malloc'd, NUL-terminated. */
bool     plist_to_xml(const plist_node_t *node, char **out, size_t *out_len);

/* Parse an XML or bplist00 (binary plist v1) blob. */
plist_node_t *plist_from_data(const uint8_t *data, size_t len, const char **err);

void     plist_free(plist_node_t *node);

#endif /* IDFU_PLIST_H */