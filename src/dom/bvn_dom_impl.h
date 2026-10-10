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

#ifndef BVN_DOM_IMPL_H_
#define BVN_DOM_IMPL_H_
#include "bovnar.h"
#include "bovnar_dom.h"
#include "bvn_int.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/*
 * A node's unit, held ONCE for every node that carries it.
 *
 * A value_unit_t is 516 bytes -- 32 components of 16 -- and the node used to
 * hold one inline. Every element of `<float:64,m/s²> [ ... ]` therefore carried
 * its own copy of the same annotation, and so did every bare `1` in an array
 * with no unit at all: 656 bytes for a scalar whose payload is eight, about 340
 * for each byte of a document made of small integers. bvn_dom_parse_fd reads up
 * to 256 MiB.
 *
 * `src` is the unit as the document spelled it, kept only where that differs
 * from bvn_unit_to_string(unit) -- a profile spelling, a UCUM annotation, a
 * compact prefix -- and NULL otherwise. It lives here because it is a property
 * of the same annotation and repeats with it.
 *
 * The two static blocks below are never counted and never freed. Blocks are shared only between nodes built by one parse, and
 * a tree is not safe to mutate or destroy from two threads at once -- it never
 * was -- so the count is not atomic.
 */
typedef struct bvn_dom_unit_s {
	uint32_t     refs;
	uint32_t     src_len;
	char        *src;
	value_unit_t unit;
} bvn_dom_unit_t;
/* BVN_UNIT_NO_PREFIX(bu_none), what bvn_dom_node_alloc gives a node, and
 * BVN_UNIT_NONE, what a value with no unit at all has. */
extern bvn_dom_unit_t bvn_dom_unit_default;
extern bvn_dom_unit_t bvn_dom_unit_none;
static inline bool bvn_dom_unit_static(const bvn_dom_unit_t *u)
{
	return u == &bvn_dom_unit_default || u == &bvn_dom_unit_none;
}
/* A block for `vu` with one reference, or one of the static blocks when `vu` is
 * one of theirs and there is no spelling to keep. NULL on allocation failure. */
bvn_dom_unit_t *bvn_dom_unit_get(const value_unit_t *vu,
				 const char *src, uint32_t src_len);
bvn_dom_unit_t *bvn_dom_unit_ref(bvn_dom_unit_t *u);     /* NULL: count is full */
void            bvn_dom_unit_release(bvn_dom_unit_t *u);
bool            bvn_dom_unit_is(const bvn_dom_unit_t *u, const value_unit_t *vu,
				const char *src, uint32_t src_len);
/* Give the node `u`, taking over the caller's reference. */
void bvn_dom_node_take_unit(bvn_dom_node_t *n, bvn_dom_unit_t *u);
/* Give the node a block of its own for `vu`. False on allocation failure, the
 * node unchanged. */
bool bvn_dom_node_set_unit(bvn_dom_node_t *n, const value_unit_t *vu);
/*
 * The shape of a `/`-row matrix: its row count and the widths of the first
 * eight rows. Only a matrix has one. On an array node, `ext.dims` is
 *   NULL                an array built by hand: no rows recorded (0 dims)
 *   &bvn_dom_dims_1d    a parsed one-row array, whose only row is the array
 *   anything else       allocated when the second row opened
 */
typedef struct bvn_dom_dims_s {
	uint32_t num_dims;
	uint32_t rows_per_dim[8];
} bvn_dom_dims_t;
extern bvn_dom_dims_t bvn_dom_dims_1d;
struct bvn_dom_node_s {
	bvn_dom_type_t       type;
	value_type_spec_t    value_type;
	bvn_dom_unit_t      *unit;          /* never NULL */
	/* ONE of these, chosen by `type`. The struct and array payloads used to
	 * sit beside the union, 72 bytes on every scalar that was neither. */
	union {
		int64_t       int_val;
		bvn_int_t *bigint;
		double        float_val;
		struct { char    *data; uint32_t len; } str;
		struct { uint8_t *data; uint32_t len; } octets;
		struct {
			bvn_dom_entry_t *entries;
			uint32_t         count;
			uint32_t         cap;
		} members;
		struct {
			bvn_dom_node_t **items;
			uint32_t         count;
			uint32_t         cap;
		} arr;
	} val;
	/* One more pointer, chosen by `type` again, and freed the same way for
	 * either: an array's matrix shape, or (spec 1.1) the verbatim ISO
	 * sub-second digits of a datetime -- the digits only, no leading '.',
	 * NUL-terminated, NULL when the literal had no fraction. val.int_val still
	 * holds the whole epoch seconds. Never read `dt_frac` on an array. */
	union {
		char           *dt_frac;
		bvn_dom_dims_t *dims;
	} ext;
};
/* Pinned, because nothing else would notice it growing back: the size of this
 * struct is the memory a document costs per value. */
typedef char bvn_dom_node_size_check[(sizeof(struct bvn_dom_node_s) <= 48u) ? 1 : -1];
static inline uint32_t bvn_dom_arr_ndims(const bvn_dom_node_t *n)
{
	return n->ext.dims ? n->ext.dims->num_dims : 0u;
}
/* Width of row `i` (i < 8) as recorded. */
static inline uint32_t bvn_dom_arr_row(const bvn_dom_node_t *n, uint32_t i)
{
	if (!n->ext.dims)
		return 0u;
	if (n->ext.dims == &bvn_dom_dims_1d)
		return i == 0u ? n->val.arr.count : 0u;
	return n->ext.dims->rows_per_dim[i];
}
struct bvn_dom_doc_s {
	bvn_dom_entry_t *entries;
	uint32_t         count;
	uint32_t         cap;
	error_code_t     parse_error;
};
/*
 * The attach functions without the public ones' argument checks -- no test that
 * the container is of the right kind, and no duplicate-key scan. For the DOM
 * builder only, which knows the kind from its own scope stack and has to let a
 * duplicate key INTO the tree so the document-tier pass can report it; see
 * bvn_dom_struct_add in bovnar_dom.c. Same ownership rule: the value is
 * destroyed on every failure path.
 */
bool bvn_dom_struct_add_nocheck(bvn_dom_node_t *s,
				const char *key, uint32_t klen,
				bvn_dom_node_t *val);
bool bvn_dom_doc_add_nocheck(bvn_dom_doc_t *d,
			     const char *key, uint32_t klen,
			     bvn_dom_node_t *val);
bool bvn_dom_array_append_nocheck(bvn_dom_node_t *a, bvn_dom_node_t *elem);
#endif
