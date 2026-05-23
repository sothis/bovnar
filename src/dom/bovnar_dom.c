#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar_dom.h"
#include "bvn_dom_impl.h"
#include "bovnar_si_units.h"
char *bvn_dom_strdup(const char *s, uint32_t len)
{
	char *r = malloc(len + 1u);
	if (!r) return NULL;
	memcpy(r, s, len);
	r[len] = '\0';
	return r;
}
bvn_dom_node_t *bvn_dom_node_alloc(bvn_dom_type_t t)
{
	bvn_dom_node_t *n = malloc(sizeof(*n));
	if (!n) return NULL;
	memset(n, 0, sizeof(*n));
	n->type       = t;
	n->value_type = BVN_TYPE_PLAIN;
	n->value_unit = BVN_UNIT_NO_PREFIX(bu_none);
	return n;
}
void bvn_dom_node_destroy(bvn_dom_node_t *n)
{
	if (!n) return;
	switch (n->type) {
	case BVN_DOM_INT:
		if (n->value_type.width > 64u && n->val.bigint)
			bvn_int_free(n->val.bigint);
		break;
	case BVN_DOM_STRING:
	case BVN_DOM_SYMBOL:
	case BVN_DOM_REFERENCE:
		free(n->val.str.data);
		break;
	case BVN_DOM_OCTET_STREAM:
		free(n->val.octets.data);
		break;
	default:
		break;
	}
	if (n->members.entries) {
		for (uint32_t i = 0; i < n->members.count; i++) {
			free(n->members.entries[i].key);
			bvn_dom_node_destroy(n->members.entries[i].value);
		}
		free(n->members.entries);
	}
	if (n->arr.items) {
		for (uint32_t i = 0; i < n->arr.count; i++)
			bvn_dom_node_destroy(n->arr.items[i]);
		free(n->arr.items);
	}
	free(n);
}
bool bvn_dom_struct_add(bvn_dom_node_t *s,
						const char *key, uint32_t klen,
						bvn_dom_node_t *val)
{
	if (s->members.count == s->members.cap) {
		uint32_t nc = s->members.cap ? s->members.cap * 2u : 8u;
		bvn_dom_entry_t *ne = realloc(s->members.entries,
									  nc * sizeof(*ne));
		if (!ne) { bvn_dom_node_destroy(val); return false; }
		s->members.entries = ne;
		s->members.cap     = nc;
	}
	char *ks = bvn_dom_strdup(key, klen);
	if (!ks) { bvn_dom_node_destroy(val); return false; }
	s->members.entries[s->members.count].key   = ks;
	s->members.entries[s->members.count].value = val;
	s->members.count++;
	return true;
}
bool bvn_dom_doc_add(bvn_dom_doc_t *d,
					 const char *key, uint32_t klen,
					 bvn_dom_node_t *val)
{
	if (d->count == d->cap) {
		uint32_t nc = d->cap ? d->cap * 2u : 8u;
		bvn_dom_entry_t *ne = realloc(d->entries, nc * sizeof(*ne));
		if (!ne) { bvn_dom_node_destroy(val); return false; }
		d->entries = ne;
		d->cap     = nc;
	}
	char *ks = bvn_dom_strdup(key, klen);
	if (!ks) { bvn_dom_node_destroy(val); return false; }
	d->entries[d->count].key   = ks;
	d->entries[d->count].value = val;
	d->count++;
	return true;
}
bool bvn_dom_array_append(bvn_dom_node_t *a, bvn_dom_node_t *elem)
{
	if (a->arr.count == a->arr.cap) {
		uint32_t nc = a->arr.cap ? a->arr.cap * 2u : 8u;
		bvn_dom_node_t **ni = realloc(a->arr.items,
									  nc * sizeof(*ni));
		if (!ni) { bvn_dom_node_destroy(elem); return false; }
		a->arr.items = ni;
		a->arr.cap   = nc;
	}
	a->arr.items[a->arr.count++] = elem;
	return true;
}
bvn_dom_doc_t *bvn_dom_doc_create(void)
{
	bvn_dom_doc_t *d = malloc(sizeof(*d));
	if (!d) return NULL;
	memset(d, 0, sizeof(*d));
	return d;
}
void bvn_dom_doc_destroy(bvn_dom_doc_t *doc)
{
	if (!doc) return;
	if (doc->entries) {
		for (uint32_t i = 0; i < doc->count; i++) {
			free(doc->entries[i].key);
			bvn_dom_node_destroy(doc->entries[i].value);
		}
		free(doc->entries);
	}
	free(doc);
}
bvn_dom_node_t *bvn_dom_struct_get(const bvn_dom_node_t *node,
								   const char *key)
{
	if (!node || !key) return NULL;
	if (node->type != BVN_DOM_STRUCT) return NULL;
	for (uint32_t i = 0; i < node->members.count; i++) {
		if (strcmp(node->members.entries[i].key, key) == 0)
			return node->members.entries[i].value;
	}
	return NULL;
}
bvn_dom_node_t *bvn_dom_lookup(const bvn_dom_doc_t *doc,
							   const char *path)
{
	if (!doc || !path) return NULL;
	const char *p = path;
	if (*p == '.') p++;
	if (!*p) return NULL;
	bvn_dom_node_t *cur = NULL;
	for (;;) {
		const char *dot = strchr(p, '.');
		uint32_t seg_len = dot ? (uint32_t)(dot - p) : (uint32_t)strlen(p);
		if (!seg_len) return NULL;
		char segment[256];
		if (seg_len >= sizeof(segment)) return NULL;
		memcpy(segment, p, seg_len);
		segment[seg_len] = '\0';
		if (!cur) {
			for (uint32_t i = 0; i < doc->count; i++) {
				if (strcmp(doc->entries[i].key, segment) == 0) {
					cur = doc->entries[i].value;
					break;
				}
			}
		} else {
			cur = bvn_dom_struct_get(cur, segment);
		}
		if (!cur) return NULL;
		if (!dot) break;
		p = dot + 1;
		if (cur->type != BVN_DOM_STRUCT) return NULL;
	}
	return cur;
}
bvn_dom_node_t *bvn_dom_array_at(const bvn_dom_node_t *node,
								 uint32_t index)
{
	if (!node || node->type != BVN_DOM_ARRAY) return NULL;
	if (index >= node->arr.count) return NULL;
	return node->arr.items[index];
}
bvn_dom_type_t bvn_dom_node_type(const bvn_dom_node_t *node)
{
	return node ? node->type : BVN_DOM_NULL;
}
bool bvn_dom_is_null(const bvn_dom_node_t *node)
{
	return node && node->type == BVN_DOM_NULL;
}
bool bvn_dom_get_float(const bvn_dom_node_t *node, double *out)
{
	if (!node || !out) return false;
	if (node->type == BVN_DOM_FLOAT) {
		*out = node->val.float_val;
		return true;
	}
	if (node->type == BVN_DOM_INT) {
		if (node->value_type.width > 64u) {
			int64_t v;
			if (!node->val.bigint || !bvn_int_to_int64(node->val.bigint, &v))
				return false;
			*out = (double)v;
			return true;
		}
		*out = (double)node->val.int_val;
		return true;
	}
	return false;
}
bool bvn_dom_get_string(const bvn_dom_node_t *node,
						const char **out, uint32_t *len)
{
	if (!node || node->type != BVN_DOM_STRING || !out) return false;
	*out = node->val.str.data;
	if (len) *len = node->val.str.len;
	return true;
}
bool bvn_dom_get_symbol(const bvn_dom_node_t *node,
						const char **out, uint32_t *len)
{
	if (!node || node->type != BVN_DOM_SYMBOL || !out) return false;
	*out = node->val.str.data;
	if (len) *len = node->val.str.len;
	return true;
}
bool bvn_dom_get_reference(const bvn_dom_node_t *node,
						   const char **out, uint32_t *len)
{
	if (!node || node->type != BVN_DOM_REFERENCE || !out) return false;
	*out = node->val.str.data;
	if (len) *len = node->val.str.len;
	return true;
}
bool bvn_dom_get_octets(const bvn_dom_node_t *node,
						const uint8_t **out, uint32_t *len)
{
	if (!node || node->type != BVN_DOM_OCTET_STREAM || !out) return false;
	*out = node->val.octets.data;
	if (len) *len = node->val.octets.len;
	return true;
}
value_type_spec_t bvn_dom_get_value_type(const bvn_dom_node_t *node)
{
	return node ? node->value_type : BVN_TYPE_PLAIN;
}
value_unit_t bvn_dom_get_unit(const bvn_dom_node_t *node)
{
	return node ? node->value_unit : BVN_UNIT_NO_PREFIX(bu_none);
}
int32_t bvn_dom_get_unit_string(const bvn_dom_node_t *node,
								char *buf, size_t bufsize)
{
	if (!node) return -1;
	return bvn_unit_to_string(node->value_unit, buf, bufsize);
}
double bvn_dom_get_value_in_base_units(const bvn_dom_node_t *node)
{
	if (!node) return 0.0;
	double raw;
	if (node->type == BVN_DOM_FLOAT) {
		raw = node->val.float_val;
	} else if (node->type == BVN_DOM_INT) {
		if (node->value_type.width > 64u) {
			int64_t v;
			if (!node->val.bigint || !bvn_int_to_int64(node->val.bigint, &v))
				return 0.0;
			raw = (double)v;
		} else {
			raw = (double)node->val.int_val;
		}
	} else {
		return 0.0;
	}
	bool is_affine      = false;
	double affine_offset = 0.0;
	bool si_ok           = true;
	double factor = bvn_unit_to_si_factor(node->value_unit,
										   &is_affine,
										   &affine_offset,
										   &si_ok);
	if (!si_ok)
		return 0.0;
	if (is_affine)
		return raw * factor + affine_offset;
	return raw * factor;
}
uint32_t bvn_dom_struct_count(const bvn_dom_node_t *node)
{
	return (node && node->type == BVN_DOM_STRUCT)
		   ? node->members.count : 0;
}
uint32_t bvn_dom_array_count(const bvn_dom_node_t *node)
{
	return (node && node->type == BVN_DOM_ARRAY)
		   ? node->arr.count : 0;
}
uint32_t bvn_dom_array_dims(const bvn_dom_node_t *node)
{
	return (node && node->type == BVN_DOM_ARRAY)
		   ? node->arr.num_dims : 0;
}
const bvn_dom_entry_t *bvn_dom_struct_entries(const bvn_dom_node_t *node)
{
	return (node && node->type == BVN_DOM_STRUCT)
		   ? node->members.entries : NULL;
}
const bvn_dom_entry_t *bvn_dom_doc_entries(const bvn_dom_doc_t *doc)
{
	return doc ? doc->entries : NULL;
}
uint32_t bvn_dom_doc_count(const bvn_dom_doc_t *doc)
{
	return doc ? doc->count : 0;
}
error_code_t bvn_dom_doc_get_parse_error(const bvn_dom_doc_t *doc)
{
	return doc ? doc->parse_error : error_none;
}
const bvn_int_t *bvn_dom_get_bigint(const bvn_dom_node_t *node)
{
	if (!node || node->type != BVN_DOM_INT) return NULL;
	if (node->value_type.width <= 64u)      return NULL;
	return node->val.bigint;
}
char *bvn_dom_int_to_str(const bvn_dom_node_t *node, uint32_t base)
{
	if (!node || node->type != BVN_DOM_INT) return NULL;
	if (node->value_type.width > 64u) {
		if (!node->val.bigint) return NULL;
		size_t bufsz = bvn_int_str_bufsize(node->value_type.width, base);
		char  *buf   = malloc(bufsz);
		if (!buf) return NULL;
		if (bvn_int_to_str(node->val.bigint, buf, bufsz, base) < 0) {
			free(buf);
			return NULL;
		}
		return buf;
	}
	char    tmp[128];
	int32_t slen;
	if (node->value_type.family == vt_sint || node->val.int_val < 0)
		slen = bvn_format_int64(tmp, sizeof(tmp),
								node->val.int_val, base, 0u);
	else
		slen = bvn_format_uint64(tmp, sizeof(tmp),
								 (uint64_t)node->val.int_val, base, 0u);
	if (slen < 0) return NULL;
	char *out = malloc((size_t)slen + 1u);
	if (!out) return NULL;
	memcpy(out, tmp, (size_t)slen + 1u);
	return out;
}
static bool dom_raw_i64(const bvn_dom_node_t *node, int64_t *out)
{
	if (!node || node->type != BVN_DOM_INT || !out) return false;
	if (node->value_type.width > 64u)
		return node->val.bigint && bvn_int_to_int64(node->val.bigint, out);
	*out = node->val.int_val;
	return true;
}
static bool dom_raw_u64(const bvn_dom_node_t *node, uint64_t *out)
{
	if (!node || node->type != BVN_DOM_INT || !out) return false;
	if (node->value_type.width > 64u)
		return node->val.bigint && bvn_int_to_uint64(node->val.bigint, out);
	if (node->value_type.family == vt_sint && node->val.int_val < 0)
		return false;
	*out = (uint64_t)node->val.int_val;
	return true;
}
bool bvn_dom_get_i64(const bvn_dom_node_t *node, int64_t *out)
{ return dom_raw_i64(node, out); }
bool bvn_dom_get_u64(const bvn_dom_node_t *node, uint64_t *out)
{ return dom_raw_u64(node, out); }
bool bvn_dom_get_i32(const bvn_dom_node_t *node, int32_t *out)
{
	int64_t v;
	if (!dom_raw_i64(node, &v)) return false;
	if (v < (int64_t)INT32_MIN || v > (int64_t)INT32_MAX) return false;
	*out = (int32_t)v;
	return true;
}
bool bvn_dom_get_u32(const bvn_dom_node_t *node, uint32_t *out)
{
	uint64_t v;
	if (!dom_raw_u64(node, &v)) return false;
	if (v > (uint64_t)UINT32_MAX) return false;
	*out = (uint32_t)v;
	return true;
}
bool bvn_dom_get_i16(const bvn_dom_node_t *node, int16_t *out)
{
	int64_t v;
	if (!dom_raw_i64(node, &v)) return false;
	if (v < (int64_t)INT16_MIN || v > (int64_t)INT16_MAX) return false;
	*out = (int16_t)v;
	return true;
}
bool bvn_dom_get_u16(const bvn_dom_node_t *node, uint16_t *out)
{
	uint64_t v;
	if (!dom_raw_u64(node, &v)) return false;
	if (v > (uint64_t)UINT16_MAX) return false;
	*out = (uint16_t)v;
	return true;
}
bool bvn_dom_get_i8(const bvn_dom_node_t *node, int8_t *out)
{
	int64_t v;
	if (!dom_raw_i64(node, &v)) return false;
	if (v < (int64_t)INT8_MIN || v > (int64_t)INT8_MAX) return false;
	*out = (int8_t)v;
	return true;
}
bool bvn_dom_get_u8(const bvn_dom_node_t *node, uint8_t *out)
{
	uint64_t v;
	if (!dom_raw_u64(node, &v)) return false;
	if (v > (uint64_t)UINT8_MAX) return false;
	*out = (uint8_t)v;
	return true;
}
static bvn_dom_node_t *make_narrow_int(int64_t            raw,
										value_type_family_t fam,
										uint32_t            width)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_INT);
	if (!n) return NULL;
	n->value_type = (value_type_spec_t){ .family = fam,
										 .width  = width,
										 .base   = 0u };
	n->value_unit = BVN_UNIT_NONE;
	n->val.int_val    = raw;
	return n;
}
bvn_dom_node_t *bvn_dom_node_from_i64(int64_t v)
{ return make_narrow_int(v,                     vt_sint, 64u); }
bvn_dom_node_t *bvn_dom_node_from_u64(uint64_t v)
{ return make_narrow_int((int64_t)v,            vt_uint, 64u); }
bvn_dom_node_t *bvn_dom_node_from_i32(int32_t v)
{ return make_narrow_int((int64_t)v,            vt_sint, 32u); }
bvn_dom_node_t *bvn_dom_node_from_u32(uint32_t v)
{ return make_narrow_int((int64_t)(uint64_t)v,  vt_uint, 32u); }
bvn_dom_node_t *bvn_dom_node_from_i16(int16_t v)
{ return make_narrow_int((int64_t)v,            vt_sint, 16u); }
bvn_dom_node_t *bvn_dom_node_from_u16(uint16_t v)
{ return make_narrow_int((int64_t)(uint64_t)v,  vt_uint, 16u); }
bvn_dom_node_t *bvn_dom_node_from_i8(int8_t v)
{ return make_narrow_int((int64_t)v,            vt_sint,  8u); }
bvn_dom_node_t *bvn_dom_node_from_u8(uint8_t v)
{ return make_narrow_int((int64_t)(uint64_t)v,  vt_uint,  8u); }
bvn_dom_node_t *bvn_dom_node_from_bigint(bvn_int_t     *bigint,
										  value_type_spec_t vt,
										  value_unit_t      vu)
{
	if (!bigint) return NULL;
	if (vt.width <= 64u) return NULL;
	if (vt.family != vt_sint && vt.family != vt_uint) return NULL;
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_INT);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	n->val.bigint     = bigint;
	return n;
}
