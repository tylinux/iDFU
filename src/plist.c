// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 iDFU authors
//
// Minimal plist implementation: XML encoder (for requests) and a tiny
// bplist00 + XML decoder (for responses). Only the node kinds needed by
// the EnterRecovery flow are supported.
#include "plist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* construction                                                       */
/* ------------------------------------------------------------------ */

plist_node_t *
plist_new_null(void) {
	plist_node_t *n = calloc(1, sizeof(*n));
	return n ? (n->kind = PLIST_NULL, n) : NULL;
}

plist_node_t *
plist_new_bool(bool b) {
	plist_node_t *n = calloc(1, sizeof(*n));
	return n ? (n->kind = PLIST_BOOL, n->v.b = b, n) : NULL;
}

plist_node_t *
plist_new_uint(uint64_t u) {
	plist_node_t *n = calloc(1, sizeof(*n));
	return n ? (n->kind = PLIST_UINT, n->v.u = u, n) : NULL;
}

plist_node_t *
plist_new_string(const char *s) {
	plist_node_t *n = calloc(1, sizeof(*n));
	if(!n) return NULL;
	n->kind = PLIST_STRING;
	n->v.str = strdup(s ? s : "");
	return n;
}

void
plist_dict_set(plist_node_t *dict, const char *key, plist_node_t *val) {
	if(!dict || dict->kind != PLIST_DICT) return;
	size_t k = dict->v.dict.count;
	char **nk = realloc(dict->v.dict.keys, (k + 1) * sizeof(char *));
	plist_node_t **nv = realloc(dict->v.dict.vals, (k + 1) * sizeof(plist_node_t *));
	if(!nk || !nv) return;
	dict->v.dict.keys = nk;
	dict->v.dict.vals = nv;
	dict->v.dict.keys[k] = strdup(key ? key : "");
	dict->v.dict.vals[k] = val;
	dict->v.dict.count = k + 1;
}

void
plist_array_append(plist_node_t *arr, plist_node_t *item) {
	if(!arr || arr->kind != PLIST_ARRAY) return;
	size_t k = arr->v.arr.count;
	plist_node_t **ni = realloc(arr->v.arr.items, (k + 1) * sizeof(plist_node_t *));
	if(!ni) return;
	arr->v.arr.items = ni;
	arr->v.arr.items[k] = item;
	arr->v.arr.count = k + 1;
}

plist_node_t *
plist_dict_get(const plist_node_t *dict, const char *key) {
	if(!dict || dict->kind != PLIST_DICT || !key) return NULL;
	for(size_t i = 0; i < dict->v.dict.count; ++i) {
		if(strcmp(dict->v.dict.keys[i], key) == 0) return dict->v.dict.vals[i];
	}
	return NULL;
}

const char *
plist_dict_get_string(const plist_node_t *dict, const char *key) {
	plist_node_t *n = plist_dict_get(dict, key);
	return (n && n->kind == PLIST_STRING) ? n->v.str : NULL;
}

void
plist_free(plist_node_t *node) {
	if(!node) return;
	switch(node->kind) {
	case PLIST_STRING: free(node->v.str); break;
	case PLIST_ARRAY:
		for(size_t i = 0; i < node->v.arr.count; ++i) plist_free(node->v.arr.items[i]);
		free(node->v.arr.items);
		break;
	case PLIST_DICT:
		for(size_t i = 0; i < node->v.dict.count; ++i) {
			free(node->v.dict.keys[i]);
			plist_free(node->v.dict.vals[i]);
		}
		free(node->v.dict.keys);
		free(node->v.dict.vals);
		break;
	default: break;
	}
	free(node);
}

/* ------------------------------------------------------------------ */
/* XML encoder                                                        */
/* ------------------------------------------------------------------ */

static void
xml_escape_append(char **buf, size_t *cap, size_t *len, const char *s) {
	for(const char *p = s; *p; ++p) {
		const char *esc = NULL;
		switch(*p) {
		case '<': esc = "&lt;"; break;
		case '>': esc = "&gt;"; break;
		case '&': esc = "&amp;"; break;
		default: break;
		}
		size_t need = esc ? strlen(esc) : 1;
		if(*len + need + 1 > *cap) {
			*cap = (*cap + need + 64) * 2;
			*buf = realloc(*buf, *cap);
		}
		if(esc) {
			memcpy(*buf + *len, esc, need);
			*len += need;
		} else {
			(*buf)[(*len)++] = *p;
		}
	}
}

static void
buf_append(char **buf, size_t *cap, size_t *len, const char *s) {
	size_t n = strlen(s);
	if(*len + n + 1 > *cap) {
		*cap = (*cap + n + 64) * 2;
		*buf = realloc(*buf, *cap);
	}
	memcpy(*buf + *len, s, n);
	*len += n;
}

static void
xml_emit(const plist_node_t *node, char **buf, size_t *cap, size_t *len, int indent) {
	char pad[64];
	if(indent > 30) indent = 30;
	memset(pad, '\t', indent);
	pad[indent] = '\0';

	switch(node->kind) {
	case PLIST_NULL:
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "<null/>\n"); break;
	case PLIST_BOOL:
		buf_append(buf, cap, len, pad);
		buf_append(buf, cap, len, node->v.b ? "<true/>\n" : "<false/>\n"); break;
	case PLIST_UINT: {
		char tmp[32];
		snprintf(tmp, sizeof tmp, "<integer>%llu</integer>\n", (unsigned long long)node->v.u);
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, tmp);
		break;
	}
	case PLIST_STRING:
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "<string>");
		xml_escape_append(buf, cap, len, node->v.str);
		buf_append(buf, cap, len, "</string>\n");
		break;
	case PLIST_ARRAY:
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "<array>\n");
		for(size_t i = 0; i < node->v.arr.count; ++i)
			xml_emit(node->v.arr.items[i], buf, cap, len, indent + 1);
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "</array>\n");
		break;
	case PLIST_DICT:
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "<dict>\n");
		for(size_t i = 0; i < node->v.dict.count; ++i) {
			buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "\t<key>");
			xml_escape_append(buf, cap, len, node->v.dict.keys[i]);
			buf_append(buf, cap, len, "</key>\n");
			xml_emit(node->v.dict.vals[i], buf, cap, len, indent + 1);
		}
		buf_append(buf, cap, len, pad); buf_append(buf, cap, len, "</dict>\n");
		break;
	}
}

bool
plist_to_xml(const plist_node_t *node, char **out, size_t *out_len) {
	size_t cap = 512, len = 0;
	char *buf = malloc(cap);
	if(!buf) return false;
	buf_append(&buf, &cap, &len,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
		" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n");
	xml_emit(node, &buf, &cap, &len, 0);
	buf_append(&buf, &cap, &len, "</plist>\n");
	buf = realloc(buf, len + 1);
	buf[len] = '\0';
	*out = buf;
	if(out_len) *out_len = len;
	return true;
}

/* ================================================================== */
/* XML decoder (lenient: only the keys/values lockdownd/usbmuxd use)  */
/* ================================================================== */

typedef struct {
	const uint8_t *p;
	const uint8_t *end;
} xml_ctx;

static void
xml_skip_ws(xml_ctx *c) {
	while(c->p < c->end) {
		uint8_t ch = *c->p;
		if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') c->p++;
		else break;
	}
}

static void
xml_skip_ws_line(xml_ctx *c) {
	/* skip spaces and newlines only (used inside <data> blocks) */
	while(c->p < c->end && (c->p[0] == ' ' || c->p[0] == '\n' || c->p[0] == '\r' || c->p[0] == '\t')) c->p++;
}

static bool
xml_lit(xml_ctx *c, const char *lit) {
	size_t n = strlen(lit);
	if(c->p + n > c->end) return false;
	if(memcmp(c->p, lit, n) != 0) return false;
	c->p += n;
	return true;
}

static bool
xml_emit_text(const uint8_t *s, size_t n, char **out) {
	/* minimal unescape: &lt; &gt; &amp; */
	size_t cap = n + 16, len = 0;
	char *buf = malloc(cap);
	if(!buf) return false;
	for(size_t i = 0; i < n; ++i) {
		const char *ins = NULL;
		if(s[i] == '&') {
			if(i + 4 <= n && memcmp(s + i, "&lt;", 4) == 0) { ins = "<"; i += 3; }
			else if(i + 4 <= n && memcmp(s + i, "&gt;", 4) == 0) { ins = ">"; i += 3; }
			else if(i + 5 <= n && memcmp(s + i, "&amp;", 5) == 0) { ins = "&"; i += 4; }
		}
		if(len + 2 > cap) { cap *= 2; buf = realloc(buf, cap); }
		if(ins) buf[len++] = *ins; else buf[len++] = (char)s[i];
	}
	buf[len] = '\0';
	*out = buf;
	return true;
}

static plist_node_t *xml_parse_node(xml_ctx *c); /* fwd */

static plist_node_t *
xml_parse_dict(xml_ctx *c) {
	plist_node_t *dict = plist_new_null();
	dict->kind = PLIST_DICT;
	for(;;) {
		xml_skip_ws(c);
		if(xml_lit(c, "</dict>")) break;
		if(!xml_lit(c, "<key>")) { plist_free(dict); return NULL; }
		const uint8_t *ks = c->p;
		while(c->p < c->end && c->p[0] != '<') c->p++;
		char *key = NULL;
		xml_emit_text(ks, (size_t)(c->p - ks), &key);
		if(!xml_lit(c, "</key>")) { free(key); plist_free(dict); return NULL; }
		xml_skip_ws(c);
		plist_node_t *val = xml_parse_node(c);
		if(key && val) plist_dict_set(dict, key, val);
		else { free(key); plist_free(val); plist_free(dict); return NULL; }
		free(key);
	}
	return dict;
}

static plist_node_t *
xml_parse_array(xml_ctx *c) {
	plist_node_t *arr = plist_new_null();
	arr->kind = PLIST_ARRAY;
	for(;;) {
		xml_skip_ws(c);
		if(xml_lit(c, "</array>")) break;
		plist_node_t *item = xml_parse_node(c);
		if(!item) { plist_free(arr); return NULL; }
		plist_array_append(arr, item);
	}
	return arr;
}

static plist_node_t *
xml_parse_node(xml_ctx *c) {
	xml_skip_ws(c);
	if(c->p >= c->end) return NULL;
	if(xml_lit(c, "<dict>")) return xml_parse_dict(c);
	if(xml_lit(c, "<array>")) return xml_parse_array(c);
	if(xml_lit(c, "<true/>")) return plist_new_bool(true);
	if(xml_lit(c, "<false/>")) return plist_new_bool(false);
	if(xml_lit(c, "<null/>")) return plist_new_null();
	if(xml_lit(c, "<data>")) {
		/* skip until </data>; we don't use data payloads (NetworkAddress etc.) */
		size_t depth = 1;
		while(c->p < c->end && depth > 0) {
			if(c->p + 5 < c->end && memcmp(c->p, "</data", 5) == 0) { while(c->p < c->end && c->p[0] != '>') c->p++; if(c->p < c->end) c->p++; depth--; }
			else c->p++;
			xml_skip_ws_line(c);
		}
		return plist_new_null();
	}
	if(c->p + 3 < c->end && memcmp(c->p, "<date", 5) == 0) {
		/* <date>..</date>: emit null */
		while(c->p < c->end && c->p[0] != '>') c->p++;
		if(c->p < c->end) c->p++;
		while(c->p + 6 < c->end && memcmp(c->p, "</date", 6) != 0) c->p++;
		while(c->p < c->end && c->p[0] != '>') c->p++;
		if(c->p < c->end) c->p++;
		return plist_new_null();
	}
	if(xml_lit(c, "<string>")) {
		const uint8_t *s = c->p;
		while(c->p < c->end && c->p[0] != '<') c->p++;
		char *txt = NULL;
		xml_emit_text(s, (size_t)(c->p - s), &txt);
		if(!xml_lit(c, "</string>")) { free(txt); return NULL; }
		plist_node_t *n = plist_new_string(txt ? txt : "");
		free(txt);
		return n;
	}
	if(c->p + 9 < c->end && memcmp(c->p, "<integer>", 9) == 0) {
		c->p += 9;
		uint64_t v = 0;
		while(c->p < c->end && c->p[0] >= '0' && c->p[0] <= '9') {
			v = v * 10 + (c->p[0] - '0'); c->p++;
		}
		if(!xml_lit(c, "</integer>")) return NULL;
		return plist_new_uint(v);
	}
	/* Unknown leaf tag: skip to its matching close so dict parsing can continue. */
	const uint8_t *tagstart = c->p;
	if(tagstart < c->end && tagstart[0] == '<') {
		c->p++; /* skip '<' */
		const uint8_t *tn = c->p;
		while(c->p < c->end && c->p[0] != ' ' && c->p[0] != '>' && c->p[0] != '/') c->p++;
		size_t tlen = (size_t)(c->p - tn);
		while(c->p < c->end && c->p[0] != '>') c->p++;
		if(c->p < c->end) c->p++;
		/* find </tag> */
		if(tlen && tlen < 32) {
			char close[34] = "</"; memcpy(close + 2, tn, tlen); close[2 + tlen] = '>'; close[3 + tlen] = '\0';
			size_t clen = strlen(close);
			while(c->p + clen <= c->end && memcmp(c->p, close, clen) != 0) c->p++;
			if(c->p + clen <= c->end) c->p += clen;
		}
		return plist_new_null();
	}
	return NULL;
}

static plist_node_t *
plist_from_xml(const uint8_t *data, size_t len) {
	xml_ctx c = { data, data + len };
	xml_skip_ws(&c);
	/* skip <?xml ...?> and DOCTYPE if present */
	while(c.p < c.end) {
		if(c.p[0] == '<' && c.p + 1 < c.end && c.p[1] == '?') {
			while(c.p + 1 < c.end && !(c.p[0] == '?' && c.p[1] == '>')) c.p++;
			c.p += 2;
		} else if(c.p + 2 < c.end && memcmp(c.p, "<!", 2) == 0) {
			while(c.p < c.end && c.p[0] != '>') c.p++;
			if(c.p < c.end) c.p++;
		} else {
			break;
		}
		xml_skip_ws(&c);
	}
	if(!xml_lit(&c, "<plist")) return NULL;
	/* skip <plist ...> tag */
	while(c.p < c.end && c.p[0] != '>') c.p++;
	if(c.p < c.end) c.p++;
	plist_node_t *root = xml_parse_node(&c);
	return root;
}

/* ================================================================== */
/* bplist00 decoder (only dict/string/null/bool/uint/array/UID)       */
/* ================================================================== */

typedef struct {
	const uint8_t *data, *end;
	uint8_t obj_ref_size;
	uint8_t offset_size;
	uint64_t num_objects;
	const uint8_t *offset_table;
} bp_ctx;

static uint64_t
bp_read_int(const uint8_t *p, size_t n) {
	uint64_t v = 0;
	for(size_t i = 0; i < n; ++i) v = (v << 8) | p[i];
	return v;
}

static plist_node_t *bp_parse_obj(bp_ctx *c, uint64_t idx);

static plist_node_t *
bp_parse_obj_at(bp_ctx *c, const uint8_t *obj) {
	uint8_t token = obj[0] & 0xF0;
	uint8_t n = obj[0] & 0x0F;

	switch(token) {
	case 0x00: /* null/false/true */
		if(n == 0) return plist_new_null();
		if(n == 8) return plist_new_bool(false);
		if(n == 9) return plist_new_bool(true);
		return NULL;
	case 0x10: { /* int: 2^n bytes, big-endian */
		size_t len = 1u << n;
		return plist_new_uint(bp_read_int(obj + 1, len));
	}
	case 0x50: { /* UID (0x08 follows): we model as uint */
		size_t len = n + 1;
		return plist_new_uint(bp_read_int(obj + 1, len));
	}
	case 0x60: { /* string ASCII */
		size_t len = n;
		if(n == 0xF) {
			/* length is an int object following */
			/* (rare for our payloads) */
			return NULL;
		}
		char *s = malloc(len + 1);
		memcpy(s, obj + 1, len);
		s[len] = '\0';
		plist_node_t *node = plist_new_string(s);
		free(s);
		return node;
	}
	case 0xA0: { /* array */
		size_t len = n;
		plist_node_t *arr = plist_new_null();
		arr->kind = PLIST_ARRAY;
		for(size_t i = 0; i < len; ++i) {
			uint64_t ref = bp_read_int(obj + 1 + i * c->obj_ref_size, c->obj_ref_size);
			plist_node_t *item = bp_parse_obj(c, ref);
			if(item) plist_array_append(arr, item);
		}
		return arr;
	}
	case 0xD0: { /* dict */
		size_t len = n;
		plist_node_t *dict = plist_new_null();
		dict->kind = PLIST_DICT;
		for(size_t i = 0; i < len; ++i) {
			uint64_t kref = bp_read_int(obj + 1 + i * c->obj_ref_size, c->obj_ref_size);
			uint64_t vref = bp_read_int(obj + 1 + (len + i) * c->obj_ref_size, c->obj_ref_size);
			plist_node_t *knode = bp_parse_obj(c, kref);
			plist_node_t *vnode = bp_parse_obj(c, vref);
			if(knode && knode->kind == PLIST_STRING && vnode) {
				plist_dict_set(dict, knode->v.str, vnode);
			}
			plist_free(knode);
		}
		return dict;
	}
	default:
		return NULL;
	}
}

static plist_node_t *
bp_parse_obj(bp_ctx *c, uint64_t idx) {
	if(idx >= c->num_objects) return NULL;
	uint64_t off = bp_read_int(c->offset_table + idx * c->offset_size, c->offset_size);
	if(c->data + off >= c->end) return NULL;
	return bp_parse_obj_at(c, c->data + off);
}

static plist_node_t *
plist_from_bin(const uint8_t *data, size_t len, const char **err) {
	if(len < 40) { if(err) *err = "bplist too short"; return NULL; }
	if(memcmp(data, "bplist00", 8) != 0) { if(err) *err = "bad bplist magic"; return NULL; }
	const uint8_t *trailer = data + len - 32;
	bp_ctx c;
	c.offset_size = trailer[6];
	c.obj_ref_size = trailer[7];
	c.num_objects = bp_read_int(trailer + 8, 8);
	uint64_t root_idx = bp_read_int(trailer + 16, 8);
	c.data = data;
	c.end = data + len;
	uint64_t off_table_off = bp_read_int(trailer + 24, 8);
	c.offset_table = data + off_table_off;
	if(c.offset_table + c.num_objects * c.offset_size > c.end) {
		if(err) *err = "bplist offsettable out of range"; return NULL;
	}
	return bp_parse_obj(&c, root_idx);
}

plist_node_t *
plist_from_data(const uint8_t *data, size_t len, const char **err) {
	if(err) *err = NULL;
	if(len >= 8 && memcmp(data, "bplist00", 8) == 0) return plist_from_bin(data, len, err);
	if(len >= 5 && memcmp(data, "<?xml", 5) == 0) return plist_from_xml(data, len);
	if(err) *err = "unknown plist format";
	return NULL;
}