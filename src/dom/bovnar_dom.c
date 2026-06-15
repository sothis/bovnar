/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar_dom.h"
#include "bvn_dom_impl.h"
#include "bovnar_si_units.h"
/*
 * ===========================================================================
 * DOM (in-memory document tree)
 * ===========================================================================
 *
 * The streaming reader is allocation-light but forces the caller to react to
 * events. The DOM is the convenience alternative: parse a whole document into a
 * tree of bvn_dom_node_t (built by the observer in bovnar_dom_builder.c) and
 * then navigate/query it randomly. A document is a bvn_dom_doc_t (a list of
 * top-level key/value entries plus any parse_error); every value is a node
 * tagged with its bvn_dom_type_t, carrying its value_type and unit, and using a
 * union for the payload (inline int/float, heap string/octets, child
 * struct/array). Integers wider than 64 bits are stored as a heap bvn_int_t
 * (val.bigint), narrower ones inline (val.int_val) — the value_type.width is
 * the discriminator everywhere in this file.
 *
 * This file holds construction helpers (used by the builder), the destructor,
 * navigation (lookup by path, struct/array access) and a family of typed
 * accessors that convert a node's value to a requested C type with range
 * checking.
 */

/*
 * Duplicate a length-counted slice into a fresh NUL-terminated heap string.
 * Used for keys and string payloads since DOM nodes own their text (the parse
 * buffers they came from are transient).
 */
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
static bool bvn_dom_stack_reserve(bvn_dom_node_t ***stack,
								  size_t *cap, size_t need)
{
	if (need <= *cap) return true;
	size_t nc = *cap ? *cap : 16u;
	while (nc < need) {
		if (nc > SIZE_MAX / (2u * sizeof(**stack))) return false;
		nc *= 2u;
	}
	bvn_dom_node_t **ns = realloc(*stack, nc * sizeof(**stack));
	if (!ns) return false;
	*stack = ns;
	*cap   = nc;
	return true;
}
/*
 * Free a node and its entire subtree. Deliberately ITERATIVE, using an explicit
 * heap-allocated work stack rather than recursion: a hostile or simply deep
 * document could nest thousands of levels, and a recursive free would blow the
 * C call stack. Children are pushed and processed in a loop; the node's own
 * payload (bigint/string/octets) is released as it is popped. If the work stack
 * can't grow, it falls back to freeing the current node and bailing — leaking
 * is preferable to crashing under memory pressure.
 */
void bvn_dom_node_destroy(bvn_dom_node_t *n)
{
	if (!n) return;
	bvn_dom_node_t **stack = NULL;
	size_t sc  = 0;
	size_t cap = 0;
	if (!bvn_dom_stack_reserve(&stack, &cap, 1u)) {
		free(n);
		return;
	}
	stack[sc++] = n;
	while (sc > 0u) {
		bvn_dom_node_t *cur = stack[--sc];
		if (!cur) continue;
		switch (cur->type) {
		case BVN_DOM_INT:
			if (cur->value_type.width > 64u && cur->val.bigint)
				bvn_int_free(cur->val.bigint);
			break;
		case BVN_DOM_STRING:
		case BVN_DOM_SYMBOL:
		case BVN_DOM_REFERENCE:
			free(cur->val.str.data);
			break;
		case BVN_DOM_OCTET_STREAM:
			free(cur->val.octets.data);
			break;
		default:
			break;
		}
		if (cur->members.entries) {
			uint32_t mc = cur->members.count;
			if (!bvn_dom_stack_reserve(&stack, &cap, sc + mc)) {
				free(cur);
				free(stack);
				return;
			}
			for (uint32_t i = 0; i < mc; i++) {
				free(cur->members.entries[i].key);
				stack[sc++] = cur->members.entries[i].value;
			}
			free(cur->members.entries);
		}
		if (cur->arr.items) {
			uint32_t ac = cur->arr.count;
			if (!bvn_dom_stack_reserve(&stack, &cap, sc + ac)) {
				free(cur);
				free(stack);
				return;
			}
			for (uint32_t i = 0; i < ac; i++)
				stack[sc++] = cur->arr.items[i];
			free(cur->arr.items);
		}
		free(cur);
	}
	free(stack);
}
bool bvn_dom_struct_add(bvn_dom_node_t *s,
						const char *key, uint32_t klen,
						bvn_dom_node_t *val)
{
	if (s->members.count == s->members.cap) {
		if (s->members.cap > UINT32_MAX / 2u) {
			bvn_dom_node_destroy(val); return false;
		}
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
		if (d->cap > UINT32_MAX / 2u) {
			bvn_dom_node_destroy(val); return false;
		}
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
		if (a->arr.cap > UINT32_MAX / 2u) {
			bvn_dom_node_destroy(elem); return false;
		}
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
/*
 * Descend an array node by a run of [N] indices (spec 1.1). A flat /-row matrix
 * stores its cells row-major with arr.num_dims rows of arr.rows_per_dim[0]
 * columns, so it consumes TWO indices ([row][col] -> items[row*cols + col]); a
 * 1-D array (num_dims == 1) consumes ONE ([i] -> items[i]). Genuine nested
 * arrays (the [[..],[..]] form) are 1-D at each level, so they descend one index
 * per level. A partial index of a matrix (only [row]) has no node to return and
 * yields NULL, as does any out-of-range index. Returns the addressed node, or
 * NULL; *cur is advanced as indices are consumed.
 */
static bvn_dom_node_t *bvn_dom_apply_indices(
	bvn_dom_node_t *cur, const uint32_t *idx, uint32_t nidx)
{
	uint32_t k = 0;
	while (k < nidx) {
		if (!cur || cur->type != BVN_DOM_ARRAY) return NULL;
		uint32_t nd = cur->arr.num_dims;
		if (nd >= 2u) {
			if (k + 2u > nidx) return NULL;       /* matrix needs [row][col] */
			uint32_t cols = cur->arr.rows_per_dim[0];
			uint32_t r = idx[k], c = idx[k + 1u];
			if (cols == 0u || r >= nd || c >= cols) return NULL;
			uint32_t off = r * cols + c;
			if (off >= cur->arr.count) return NULL;
			cur = cur->arr.items[off];
			k += 2u;
		} else {
			if (idx[k] >= cur->arr.count) return NULL;
			cur = cur->arr.items[idx[k]];
			k += 1u;
		}
	}
	return cur;
}
/*
 * Resolve a path like ".a.b.c" — or ".matrix[0][1]" / ".rows[2].name" with array
 * indexing (spec 1.1) — to a node, walking struct members and array elements
 * from the document root. A leading dot is optional. Descending into a struct
 * member requires a struct; a [N] run requires an array (see
 * bvn_dom_apply_indices). Any missing key, non-array index, out-of-range index,
 * or malformed [N] returns NULL. This is also what the CLI `query` command and
 * application-driven reference resolution use, so indexing works there too.
 */
bvn_dom_node_t *bvn_dom_lookup(const bvn_dom_doc_t *doc,
							   const char *path)
{
	if (!doc || !path) return NULL;
	const char *p = path;
	if (*p == '.') p++;
	if (!*p || *p == '[') return NULL;     /* must start with a key */
	bvn_dom_node_t *cur = NULL;
	bool first = true;
	while (*p) {
		/* a key segment runs up to the next '.', '[', or end */
		const char *start = p;
		while (*p && *p != '.' && *p != '[') p++;
		uint32_t seg_len = (uint32_t)(p - start);
		if (!seg_len) return NULL;
		char segment[256];
		if (seg_len >= sizeof(segment)) return NULL;
		memcpy(segment, start, seg_len);
		segment[seg_len] = '\0';
		if (first) {
			cur = NULL;
			for (uint32_t i = 0; i < doc->count; i++) {
				if (strcmp(doc->entries[i].key, segment) == 0) {
					cur = doc->entries[i].value;
					break;
				}
			}
			first = false;
		} else {
			if (cur->type != BVN_DOM_STRUCT) return NULL;
			cur = bvn_dom_struct_get(cur, segment);
		}
		if (!cur) return NULL;
		/* a run of [N] indices addressing the segment's array value */
		if (*p == '[') {
			uint32_t idx[32];
			uint32_t nidx = 0;
			while (*p == '[') {
				p++;
				if (*p < '0' || *p > '9') return NULL;
				uint64_t v = 0;
				while (*p >= '0' && *p <= '9') {
					v = v * 10u + (uint64_t)(*p - '0');
					if (v > UINT32_MAX) return NULL;
					p++;
				}
				if (*p != ']') return NULL;
				p++;
				if (nidx >= 32u) return NULL;
				idx[nidx++] = (uint32_t)v;
			}
			cur = bvn_dom_apply_indices(cur, idx, nidx);
			if (!cur) return NULL;
		}
		if (*p == '.') { p++; if (!*p) return NULL; continue; }
		if (*p == '\0') break;
		return NULL;                       /* unexpected trailing byte */
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
bool bvn_dom_get_bool(const bvn_dom_node_t *node, bool *out)
{
	if (!node || !out || node->type != BVN_DOM_BOOL) return false;
	*out = node->val.int_val != 0;
	return true;
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
/*
 * Convert a numeric node to its value expressed in SI base units, applying the
 * unit's scale factor (and offset for affine units such as °C/°F, where the
 * conversion is value*factor + offset rather than a pure scaling). This is the
 * payoff of carrying units in the type system: e.g. a node "5 km" returns 5000.
 * Non-numeric nodes or units with no SI mapping return 0.0.
 */
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
/*
 * Render an integer node to a freshly allocated string in any base, hiding the
 * narrow-vs-bignum split: wide values format via bvn_int_to_str, narrow ones
 * via the int/uint formatters chosen by signedness. The caller owns the result
 * (bvn_dom_free_string). This is the only way to read a >64-bit DOM integer as
 * a value, since it can't fit any C integer type.
 */
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
	if (node->value_type.family == vt_sint)
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
void bvn_dom_free_string(char *s)
{
	free(s);
}
/*
 * The two raw integer extractors behind every bvn_dom_get_iN / get_uN
 * accessor. They
 * normalise the narrow/bignum storage split into a single int64/uint64, failing
 * if a wide value doesn't fit 64 bits or a negative value is requested as
 * unsigned. The public fixed-width getters (get_i32, get_u8, ...) layer a range
 * check on top, so each returns false rather than silently truncating when the
 * value is out of range for the requested C type. make_narrow_int and the
 * bvn_dom_node_from_* constructors are the symmetric builders.
 */
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
