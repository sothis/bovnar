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

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bvn_port.h"
#include "bovnar.h"
#include "bovnar_dom.h"
#include "bovnar_si_units.h"
#include "bvn_dom_impl.h"
#include "bvn_float.h"
#ifndef BVN_DOM_FD_MAX_BYTES
#define BVN_DOM_FD_MAX_BYTES   (256u * 1024u * 1024u)
#endif
#ifndef BVN_DOM_OCT_MAX_BYTES
#define BVN_DOM_OCT_MAX_BYTES  (256u * 1024u * 1024u)
#endif
/*
 * ===========================================================================
 * DOM builder (event consumer that materialises the tree)
 * ===========================================================================
 *
 * This bridges the streaming reader and the DOM: it registers on_verified as
 * the reader's callback, and as events arrive it builds the bvn_dom_doc_t tree
 * defined in bovnar_dom.c. The only real state it needs is a scope stack — the
 * chain of containers currently open (document root, struct, or array) — so
 * that each value can be attached to the right parent. builder_attach inspects
 * the top scope to decide between doc_add / struct_add / array_append.
 *
 * Two subtleties:
 *  - Pending key: a struct/doc member arrives as an assignment-start (the key)
 *    followed later by its value event; the key is buffered until the value is
 *    built and attached.
 *  - Deferred array pop: an array row closes (ev_array_row_end) before we know
 *    whether a sibling dimension follows (ev_array_dim_start) — for
 *    multi-dimensional arrays the same array node must stay on the stack across
 *    rows. So the pop is deferred (pending_array_pop) and only applied when the
 *    next non-dimension event confirms the array is really finished.
 */
typedef enum bvn_scope_kind_e {
	BVN_SCOPE_DOC,
	BVN_SCOPE_STRUCT,
	BVN_SCOPE_ARRAY
} bvn_scope_kind_t;
typedef struct bvn_scope_s {
	bvn_scope_kind_t  kind;
	bvn_dom_node_t   *node;
} bvn_scope_t;
typedef struct bvn_builder_s {
	bvn_dom_doc_t  *doc;
	bvn_scope_t    *scopes;
	uint32_t        scope_count;
	uint32_t        scope_cap;
	char           *key;
	uint32_t        key_len;
	uint32_t        key_cap;
	bool            pending_array_pop;
	bool            array_dim_continue;
	uint8_t        *oct_buf;
	uint32_t        oct_len;
	uint32_t        oct_cap;
	bool            in_octet_stream;
	uint32_t        row_start_count;
	error_code_t    last_error;
} bvn_builder_t;
static bool builder_init(bvn_builder_t *b, bvn_dom_doc_t *doc)
{
	memset(b, 0, sizeof(*b));
	b->doc = doc;
	return true;
}
static void builder_cleanup(bvn_builder_t *b)
{
	free(b->scopes);
	free(b->key);
	free(b->oct_buf);
}
static bvn_scope_t *builder_top(bvn_builder_t *b)
{
	return b->scope_count ? &b->scopes[b->scope_count - 1u] : NULL;
}
static bool builder_push(bvn_builder_t *b, bvn_scope_kind_t kind,
						 bvn_dom_node_t *node)
{
	if (b->scope_count == b->scope_cap) {
		if (b->scope_cap > UINT32_MAX / 2u) return false;
		uint32_t nc = b->scope_cap ? b->scope_cap * 2u : 16u;
		bvn_scope_t *ns = realloc(b->scopes, nc * sizeof(*ns));
		if (!ns) return false;
		b->scopes   = ns;
		b->scope_cap = nc;
	}
	bvn_scope_t *s = &b->scopes[b->scope_count++];
	s->kind = kind;
	s->node = node;
	return true;
}
static void builder_pop(bvn_builder_t *b)
{
	if (b->scope_count > 0)
		b->scope_count--;
}
static bool builder_set_key(bvn_builder_t *b, const char *data,
							uint32_t len)
{
	if (len == UINT32_MAX) return false;   /* len + 1u would wrap to 0 */
	if (len + 1u > b->key_cap) {
		uint32_t nc = len + 1u;
		char *nk = realloc(b->key, nc);
		if (!nk) return false;
		b->key     = nk;
		b->key_cap = nc;
	}
	memcpy(b->key, data, len);
	b->key[len] = '\0';
	b->key_len  = len;
	return true;
}
static void builder_clear_key(bvn_builder_t *b)
{
	b->key_len = 0;
	if (b->key) b->key[0] = '\0';
}
/*
 * Attach a freshly built value to the currently open container, dispatching on
 * the top scope kind: a document/struct member keyed by the buffered key, or an
 * array element (no key). On failure the value is owned/destroyed by the add
 * helper, so callers must not free it again.
 */
static bool builder_attach(bvn_builder_t *b, bvn_dom_node_t *val)
{
	bvn_scope_t *top = builder_top(b);
	if (!top) return false;
	if (top->kind == BVN_SCOPE_DOC) {
		return bvn_dom_doc_add(b->doc, b->key, b->key_len, val);
	}
	if (top->kind == BVN_SCOPE_STRUCT) {
		bool ok = bvn_dom_struct_add(top->node,
									 b->key, b->key_len, val);
		if (ok) builder_clear_key(b);
		return ok;
	}
	return bvn_dom_array_append(top->node, val);
}
static bool builder_oct_append(bvn_builder_t *b, const uint8_t *data,
							   uint32_t len)
{
	if (len > UINT32_MAX - b->oct_len) return false;
	uint32_t need = b->oct_len + len;
	if (need > BVN_DOM_OCT_MAX_BYTES) return false;
	if (need > b->oct_cap) {
		uint32_t nc = b->oct_cap ? b->oct_cap : 256u;
		while (nc < need) {
			if (nc > UINT32_MAX / 2u) { nc = need; break; }
			nc *= 2u;
		}
		if (nc > BVN_DOM_OCT_MAX_BYTES) nc = BVN_DOM_OCT_MAX_BYTES;
		if (nc < need) return false;
		uint8_t *nb = realloc(b->oct_buf, nc);
		if (!nb) return false;
		b->oct_buf = nb;
		b->oct_cap = nc;
	}
	memcpy(b->oct_buf + b->oct_len, data, len);
	b->oct_len = need;
	return true;
}
static bvn_dom_node_t *make_null(value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_NULL);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	return n;
}
/*
 * Build an INT node from the literal text. The effective width decides storage:
 * >64 bits parses into a heap bvn_int_t (arbitrary precision), otherwise into
 * the inline int64. Each make_* builder follows the same contract — take the
 * value text plus its resolved type/unit, allocate the typed node, and clean up
 * fully on any failure. The strtoll/strtoull fallback covers plain (untyped)
 * literals the typed parser declines; an untyped value above INT64_MAX is
 * promoted to uint64 so it isn't misread as negative.
 */
static bvn_dom_node_t *make_int(const char *str, uint32_t len,
								value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_INT);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	uint32_t effective_width = vt.width ? vt.width : 64u;
	uint32_t base            = bvn_effective_base(vt);
	if (effective_width > 64u) {
		bvn_int_t *bi = bvn_int_alloc();
		if (!bi) { bvn_dom_node_destroy(n); return NULL; }
		char *wbuf = malloc((size_t)len + 1u);
		if (!wbuf) {
			bvn_int_free(bi);
			bvn_dom_node_destroy(n);
			return NULL;
		}
		memcpy(wbuf, str, (size_t)len);
		wbuf[len] = '\0';
		bool ok = bvn_int_from_str(bi, wbuf, base);
		free(wbuf);
		if (!ok) {
			bvn_int_free(bi);
			bvn_dom_node_destroy(n);
			return NULL;
		}
		n->val.bigint = bi;
		return n;
	}
	char     stkbuf[256];
	char    *buf;
	bool     buf_on_heap = false;
	if ((size_t)len + 1u > sizeof(stkbuf)) {
		buf = malloc((size_t)len + 1u);
		if (!buf) { bvn_dom_node_destroy(n); return NULL; }
		buf_on_heap = true;
	} else {
		buf = stkbuf;
	}
	memcpy(buf, str, (size_t)len);
	buf[len] = '\0';
	bool parse_ok = false;
	if (vt.family == vt_sint || vt.family == vt_datetime ||
	    (vt.family == vt_plain && buf[0] == '-')) {
		int64_t v = 0;
		if (bvn_parse_int64(buf, vt, &v)) {
			n->val.int_val = v;
			parse_ok = true;
		} else {
			char *end = NULL;
			errno = 0;
			long long ll = strtoll(buf, &end, 10);
			/* Defensive: the validator already range-checks a width-typed
			 * literal before it reaches the builder, but never store a value
			 * that does not fit the declared signed width, so the node's tag
			 * and payload can never disagree. (width 0 / >= 64 always fit.) */
			bool fits = !(vt.width >= 1u && vt.width < 64u);
			if (!fits) {
				int64_t lim = (int64_t)1 << (vt.width - 1u);
				fits = ((int64_t)ll >= -lim && (int64_t)ll <= lim - 1);
			}
			if (errno == 0 && end && *end == '\0' && fits) {
				n->val.int_val = (int64_t)ll;
				parse_ok = true;
			}
		}
	} else {
		uint64_t v = 0;
		if (bvn_parse_uint64(buf, vt, &v)) {
			n->val.int_val = (int64_t)v;
			parse_ok = true;
		} else {
			char *end = NULL;
			errno = 0;
			unsigned long long ull = strtoull(buf, &end, 10);
			/* Same defensive width clamp as the signed path above. */
			bool fits = !(vt.width >= 1u && vt.width < 64u) ||
			            ((uint64_t)ull < ((uint64_t)1 << vt.width));
			if (errno == 0 && end && *end == '\0' && fits) {
				v = (uint64_t)ull;
				n->val.int_val = (int64_t)v;
				parse_ok = true;
			}
		}
		if (parse_ok && vt.family == vt_plain && v > (uint64_t)INT64_MAX) {
			n->value_type = BVN_TYPE_UINT_BASE(64u, 0u);
		}
	}
	if (buf_on_heap) free(buf);
	if (!parse_ok) { bvn_dom_node_destroy(n); return NULL; }
	return n;
}
static bvn_dom_node_t *make_float(const char *str, uint32_t len,
								  value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_FLOAT);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	char  stkbuf[256];
	char *buf;
	bool  buf_on_heap = false;
	if ((size_t)len + 1u > sizeof(stkbuf)) {
		buf = malloc((size_t)len + 1u);
		if (!buf) { bvn_dom_node_destroy(n); return NULL; }
		buf_on_heap = true;
	} else {
		buf = stkbuf;
	}
	memcpy(buf, str, (size_t)len);
	buf[len] = '\0';
	if (bvn_is_special_number_string(buf)) {
		if      (strcmp(buf, "nan")  == 0) n->val.float_val = NAN;
		else if (strcmp(buf, "inf")  == 0) n->val.float_val = INFINITY;
		else if (strcmp(buf, "ninf") == 0) n->val.float_val = -INFINITY;
	} else {
		uint32_t base = bvn_effective_base(vt);
		double v = 0.0;
		/*
		 * Convert through the canonical decimal/base parser. For base 10 this
		 * takes the exact-rational single-rounding path, which is correctly
		 * rounded across the whole binary64 range including subnormals. The
		 * former local route -- parse into a finite-precision bvn_float, then
		 * bvn_float_to_double -- rounded twice and was 1 ULP off for subnormal
		 * results (the stored value is a double regardless of the parse width,
		 * so nothing is lost by delegating). For non-decimal bases this still
		 * rejects a literal the parser cannot consume.
		 */
		if (!bvn_parse_double_in_base(buf, base, &v)) {
			if (buf_on_heap) free(buf);
			bvn_dom_node_destroy(n);
			return NULL;
		}
		n->val.float_val = v;
	}
	if (buf_on_heap) free(buf);
	return n;
}
static bvn_dom_node_t *make_string(const char *str, uint32_t len,
								   value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_STRING);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	n->val.str.data = bvn_dom_strdup(str, len);
	n->val.str.len  = len;
	if (!n->val.str.data) { bvn_dom_node_destroy(n); return NULL; }
	return n;
}
static bvn_dom_node_t *make_bool(const char *str, uint32_t len,
								 value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_BOOL);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	/* The validator collapses on/off to the canonical true/false text
	 * before emitting token_is_bool, so the only spellings that reach
	 * the DOM builder are "true" and "false". */
	n->val.int_val = (str && len == 4 && memcmp(str, "true", 4) == 0)
		? 1 : 0;
	return n;
}
static bvn_dom_node_t *make_symbol(const char *str, uint32_t len,
								   value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_SYMBOL);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	n->val.str.data = bvn_dom_strdup(str, len);
	n->val.str.len  = len;
	if (!n->val.str.data) { bvn_dom_node_destroy(n); return NULL; }
	return n;
}
static bvn_dom_node_t *make_reference(const char *str, uint32_t len,
									  value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_REFERENCE);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	n->val.str.data = bvn_dom_strdup(str, len);
	n->val.str.len  = len;
	if (!n->val.str.data) { bvn_dom_node_destroy(n); return NULL; }
	return n;
}
static bvn_dom_node_t *make_octets(const uint8_t *data, uint32_t len,
								   value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_OCTET_STREAM);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	if (len > 0) {
		n->val.octets.data = malloc(len);
		if (!n->val.octets.data) { bvn_dom_node_destroy(n); return NULL; }
		memcpy(n->val.octets.data, data, len);
	}
	n->val.octets.len = len;
	return n;
}
static bvn_dom_node_t *make_struct(value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_STRUCT);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	return n;
}
static bvn_dom_node_t *make_array(value_type_spec_t vt, value_unit_t vu)
{
	bvn_dom_node_t *n = bvn_dom_node_alloc(BVN_DOM_ARRAY);
	if (!n) return NULL;
	n->value_type = vt;
	n->value_unit = vu;
	n->arr.num_dims = 1;
	return n;
}
/*
 * Decide whether a numeric token should become a FLOAT or INT node. An explicit
 * float family forces float; an explicit int family forces int; otherwise
 * (untyped) it sniffs the text for a dot/exponent or a special value (nan/inf).
 * This is what lets a bare "3.14" land as a float and "42" as an int without an
 * annotation.
 */
static bool number_is_float(const char *s, uint32_t len,
							value_type_spec_t vt)
{
	if (vt.family == vt_float ||
		vt.family == vt_float_fix ||
		vt.family == vt_float_dec) return true;
	if (len <= 9) {
		char tmp[10];
		memcpy(tmp, s, len);
		tmp[len] = '\0';
		if (bvn_is_special_number_string(tmp))
			return true;
	}
	if (vt.family == vt_uint || vt.family == vt_sint) return false;
	for (uint32_t i = 0; i < len; i++) {
		if (s[i] == '.' || s[i] == 'e' || s[i] == 'E')
			return true;
	}
	return false;
}
static void builder_do_deferred_pop(bvn_builder_t *b)
{
	if (!b->pending_array_pop) return;
	b->pending_array_pop = false;
	bvn_scope_t *top = b->scope_count > 0 ? builder_top(b) : NULL;
	if (top && top->kind == BVN_SCOPE_ARRAY) {
		builder_pop(b);
	}
}
/*
 * The reader's verified-event callback: the state machine that turns the event
 * stream into tree mutations. assignment_start buffers the key; struct/array
 * starts allocate a container, attach it, and push a scope; the matching ends
 * pop; ev_data builds the right leaf node via the make_* helpers and attaches
 * it; octet-stream events accumulate binary chunks into oct_buf and emit one
 * octet node at the end. Array dimension/row events drive the deferred-pop and
 * per-dimension row-size bookkeeping described at the top of the file. Returning
 * false aborts the parse — the reader surfaces it as
 * error_scanner_callback_failed, which bvn_dom_parse maps back to b->last_error.
 */
static bool on_verified(void *userdata, bvnr_event_t ev, bvnr_data_t *d)
{
	bvn_builder_t *b = (bvn_builder_t *)userdata;
	switch (ev) {
	case ev_stream_start:
		break;
	case ev_stream_end:
		break;
	case ev_assignment_start: {
		builder_do_deferred_pop(b);
		if (d->data && d->length) {
			if (!builder_set_key(b, (const char *)d->data, d->length))
				return false;
		}
		break;
	}
	case ev_type_annotation_start:
	case ev_type_annotation_end:
	case ev_type_annotation_type_family:
	case ev_type_annotation_type_family_parameter:
		break;
	case ev_struct_start: {
		builder_do_deferred_pop(b);
		bvn_dom_node_t *s = make_struct(d->value_type, d->value_unit);
		if (!s) return false;
		if (!builder_attach(b, s))
			return false;
		if (!builder_push(b, BVN_SCOPE_STRUCT, s)) return false;
		builder_clear_key(b);
		break;
	}
	case ev_struct_end:
		builder_do_deferred_pop(b);
		builder_pop(b);
		break;
	case ev_array_row_start: {
		if (b->array_dim_continue) {
			b->array_dim_continue = false;
			bvn_scope_t *top = builder_top(b);
			if (top && top->node)
				b->row_start_count = top->node->arr.count;
			else
				b->row_start_count = 0;
			break;
		}
		builder_do_deferred_pop(b);
		bvn_dom_node_t *a = make_array(d->value_type, d->value_unit);
		if (!a) return false;
		if (!builder_attach(b, a))
			return false;
		if (!builder_push(b, BVN_SCOPE_ARRAY, a)) return false;
		builder_clear_key(b);
		b->row_start_count = 0;
		break;
	}
	case ev_array_row_end: {
		/*
		 * A nested array element (the [[...],[...]] form) closes its own
		 * row before the parent's row closes, producing back-to-back
		 * row_end events. The inner array's pop is deferred (pending), so
		 * apply it now — otherwise this parent row_end would mis-operate
		 * on the still-open child scope, leaving the parent array un-popped
		 * and silently absorbing every following sibling. A pending pop is
		 * only ever cancelled by ev_array_dim_start (which clears the flag
		 * to continue the same array across '/' dimensions), so if it is
		 * still set here the child array is genuinely finished.
		 */
		builder_do_deferred_pop(b);
		bvn_scope_t *top = builder_top(b);
		if (top && top->kind == BVN_SCOPE_ARRAY) {
			uint32_t row_size = top->node->arr.count - b->row_start_count;
			uint32_t dim = top->node->arr.num_dims - 1u;
			if (dim < 8)
				top->node->arr.rows_per_dim[dim] = row_size;
		}
		b->pending_array_pop = true;
		break;
	}
	case ev_array_dim_start: {
		b->pending_array_pop = false;
		b->array_dim_continue = true;
		bvn_scope_t *top = builder_top(b);
		if (top && top->kind == BVN_SCOPE_ARRAY) {
			/* num_dims is the array's row count (one per '/' separator) and is
			 * read back by bvn_dom_array_dims to reconstruct the matrix shape
			 * (JSON convert, numpy bridge). It must track the true row count, so
			 * it is NOT clamped to the rows_per_dim[8] bound; saturate only to
			 * avoid a wrap to 0 (which would divide-by-zero in the shape emitter)
			 * on a pathologically large number of rows. */
			if (top->node->arr.num_dims < UINT32_MAX)
				top->node->arr.num_dims++;
		}
		break;
	}
	case ev_octet_stream_start:
		builder_do_deferred_pop(b);
		b->in_octet_stream = true;
		b->oct_len = 0;
		break;
	case ev_octet_stream_end: {
		bvn_dom_node_t *o = make_octets(b->oct_buf, b->oct_len,
										d->value_type, d->value_unit);
		if (!o) return false;
		if (!builder_attach(b, o))
			return false;
		b->in_octet_stream = false;
		b->oct_len = 0;
		break;
	}
	case ev_data: {
		if (b->in_octet_stream) {
			if (d->type == token_is_octet_stream && d->data && d->length) {
				if (!builder_oct_append(b,
						(const uint8_t *)d->data, d->length))
					return false;
			}
			break;
		}
		builder_do_deferred_pop(b);
		const char *str = (const char *)d->data;
		uint32_t   len = d->length;
		value_type_spec_t vt = d->value_type;
		value_unit_t      vu = d->value_unit;
		bvn_dom_node_t *nd = NULL;
		switch (d->type) {
		case token_is_null_value:
			nd = make_null(vt, vu);
			break;
		case token_is_number:
		case token_is_array_number:
			if (number_is_float(str, len, vt))
				nd = make_float(str, len, vt, vu);
			else
				nd = make_int(str, len, vt, vu);
			/* spec 1.1 — keep a datetime literal's verbatim sub-second
			 * digits so DOM consumers can read them and the value can
			 * round-trip as an ISO literal. */
			if (nd && vt.family == vt_datetime &&
			    d->frac_data && d->frac_length) {
				nd->dt_frac = bvn_dom_strdup(
					(const char *)d->frac_data, d->frac_length);
				if (!nd->dt_frac) {
					bvn_dom_node_destroy(nd);
					return false;
				}
				nd->dt_frac_len = d->frac_length;
			}
			if (!nd && b->last_error == error_none)
				b->last_error = error_value_out_of_range;
			break;
		case token_is_string:
		case token_is_array_string:
			nd = make_string(str, len, vt, vu);
			break;
		case token_is_bool:
			nd = make_bool(str, len, vt, vu);
			break;
		case token_is_symbol:
			nd = make_symbol(str, len, vt, vu);
			break;
		case token_is_reference:
			nd = make_reference(str, len, vt, vu);
			break;
		case token_is_octet_stream:
			break;
		default:
			nd = make_null(vt, vu);
			break;
		}
		if (!nd) return false;
		if (!builder_attach(b, nd))
			return false;
		break;
	}
	default:
		break;
	}
	return true;
}
/*
 * Public one-shot parse: build a complete DOM from an in-memory buffer. It
 * wires a builder (with the document scope pushed) as the reader's on_verified
 * consumer, runs the reader to completion, and returns the document. On a parse
 * failure the document is still returned but carries parse_error — callers
 * check bvn_dom_doc_get_parse_error rather than relying on a NULL return, so
 * partial structure can be inspected. bvn_dom_parse_fd[_ex] slurp a file
 * descriptor into a bounded buffer first, then delegate here.
 */
/*
 * Array element homogeneity (spec 1.0). Heterogeneous arrays are a parse error:
 * every non-null element of an array must share the same kind and physical
 * dimension. The check is recursive and runs over the materialised DOM (the
 * streaming reader validates per-value types/units; cross-element structure is
 * a whole-array property, so it is enforced where the structure exists).
 *
 * The rules, decided for 1.0:
 *   - scalars: same physical dimension; the numeric encodings (int/float) may
 *     mix freely, bool/utf8/symbol/reference/octet may not mix with numbers or
 *     each other;
 *   - sub-arrays: same length and recursively-matching element shape;
 *   - structs: same keys (same order) with recursively-matching field shapes;
 *   - null / empty: a hole that matches any element and never establishes or
 *     breaks the shape (so sparse arrays like [1,,3] stay valid).
 */
/*
 * "Same physical dimension" for homogeneity. bvn_units_compatible covers the SI
 * physical units, but it returns false for any unit containing a currency
 * (currencies deliberately have no SI dimension). For homogeneity each currency
 * is its own dimension, so when the SI check fails we fall back to comparing the
 * base components and exponents directly (ignoring prefix magnitude): "$USD" and
 * "$USD" match, "$USD" and "$EUR" do not, "k~$USD" and "$USD" match, and
 * "$USD/oz_t" matches another "$USD/oz_t". Pure-physical units never reach the
 * fallback because bvn_units_compatible already decided them.
 */
static bool bvn_dom_same_dimension(value_unit_t a, value_unit_t b)
{
	if (bvn_units_compatible(a, b))
		return true;
	if (a.num_components != b.num_components)
		return false;
	uint32_t n = a.num_components < BVNR_MAX_UNIT_COMPONENTS
	           ? a.num_components : BVNR_MAX_UNIT_COMPONENTS;
	for (uint32_t i = 0; i < n; i++) {
		if (a.components[i].base     != b.components[i].base)     return false;
		if (a.components[i].exponent != b.components[i].exponent) return false;
	}
	return true;
}
static const bvn_dom_node_t *bvn_dom_first_nonnull_elem(
	const bvn_dom_node_t *arr)
{
	uint32_t n = bvn_dom_array_count(arr);
	for (uint32_t i = 0; i < n; i++) {
		const bvn_dom_node_t *e = bvn_dom_array_at(arr, i);
		if (e && bvn_dom_node_type(e) != BVN_DOM_NULL)
			return e;
	}
	return NULL;
}
/*
 * Structural type-shape equality of two array siblings. Returns error_none when
 * a and b are mutually homogeneous, otherwise the specific mismatch code. null
 * on either side is a hole and always matches.
 *
 * check_dim distinguishes the two scopes of the spec-1.0 rule ("shape uniform,
 * fields free"). In a bare array / matrix context (check_dim = true) elements
 * must share the same physical dimension AND sub-arrays must be rectangular
 * (equal length). Once the path descends into a struct, its fields need only
 * match in KIND and nesting: a scalar field may carry a different unit in each
 * record (e.g. a multi-currency ledger) and a list field may have a different
 * length in each record (e.g. per-record argument lists). So struct-field
 * recursion passes check_dim = false, freeing both dimension and array length.
 */
/*
 * Hard recursion-depth guard for the post-parse structural validators below.
 * The reader caps array and struct nesting at 255 *each*, so a materialised tree
 * reaching these checks is at most ~510 levels deep; this ceiling sits well above
 * that yet stops any pathological tree (or a future caller that bypasses the
 * reader caps) from exhausting the C stack. The destructor is iterative for the
 * same reason (see bovnar_dom.c); these checks fail closed instead.
 */
#define BVN_DOM_CHECK_MAX_DEPTH 4096u
static error_code_t bvn_dom_shape_equal(
	const bvn_dom_node_t *a, const bvn_dom_node_t *b, bool check_dim,
	unsigned depth)
{
	if (depth > BVN_DOM_CHECK_MAX_DEPTH)
		return error_array_nesting_too_high;
	bvn_dom_type_t ta = bvn_dom_node_type(a);
	bvn_dom_type_t tb = bvn_dom_node_type(b);
	if (ta == BVN_DOM_NULL || tb == BVN_DOM_NULL)
		return error_none;
	bool a_num = (ta == BVN_DOM_INT || ta == BVN_DOM_FLOAT);
	bool b_num = (tb == BVN_DOM_INT || tb == BVN_DOM_FLOAT);
	if (a_num && b_num) {
		/* Both numeric: kinds match (encodings may mix). Dimension only when
		 * comparing array/matrix elements, not struct fields. */
		/* datetime (spec 1.1) is its own kind: a timestamp does not mix with a
		 * plain count/measurement (spec §7.4 lists the mixable encodings; datetime
		 * is not among them), and — like a currency — its epoch is a dimension, so
		 * two datetimes on different epochs (time scales) may not share an array. */
		bool a_dt = (a->value_type.family == vt_datetime);
		bool b_dt = (b->value_type.family == vt_datetime);
		if (a_dt != b_dt)
			return error_array_element_type_mismatch;
		if (a_dt && b_dt && check_dim &&
			a->value_type.base != b->value_type.base)
			return error_array_element_type_mismatch;
		if (check_dim &&
			!bvn_dom_same_dimension(bvn_dom_get_unit(a), bvn_dom_get_unit(b)))
			return error_array_element_type_mismatch;
		return error_none;
	}
	if (ta != tb)
		return error_array_element_type_mismatch;
	switch (ta) {
	case BVN_DOM_ARRAY: {
		/* rectangular only in the bare array / matrix context; list fields of
		 * records may vary in length (check_dim == false). */
		if (check_dim) {
			if (bvn_dom_array_count(a) != bvn_dom_array_count(b))
				return error_array_row_size_mismatch;
			/* A '/'-row matrix stores its cells flat, with the row geometry in
			 * num_dims + rows_per_dim[]. Equal total cell count is NOT enough to
			 * mean equal shape: a 2x3 block and a 3x2 block (or a flat 4-element
			 * array and a 2x2 matrix) both hold the same number of cells but are
			 * different shapes, so the row geometry must match too. */
			if (a->arr.num_dims != b->arr.num_dims)
				return error_array_row_size_mismatch;
			uint32_t nd = a->arr.num_dims;
			if (nd > 8u) nd = 8u;   /* rows_per_dim records only the first 8 rows */
			for (uint32_t i = 0; i < nd; i++)
				if (a->arr.rows_per_dim[i] != b->arr.rows_per_dim[i])
					return error_array_row_size_mismatch;
		}
		const bvn_dom_node_t *ra = bvn_dom_first_nonnull_elem(a);
		const bvn_dom_node_t *rb = bvn_dom_first_nonnull_elem(b);
		if (ra && rb)
			return bvn_dom_shape_equal(ra, rb, check_dim,
				depth + 1u);   /* matrices stay uniform */
		return error_none;
	}
	case BVN_DOM_STRUCT: {
		uint32_t na = bvn_dom_struct_count(a);
		if (na != bvn_dom_struct_count(b))
			return error_struct_shape_mismatch;
		const bvn_dom_entry_t *ea = bvn_dom_struct_entries(a);
		const bvn_dom_entry_t *eb = bvn_dom_struct_entries(b);
		for (uint32_t i = 0; i < na; i++) {
			if (!ea[i].key || !eb[i].key ||
				strcmp(ea[i].key, eb[i].key) != 0)
				return error_struct_shape_mismatch;
			/* fields free: compare kind/shape, not dimension. */
			error_code_t e =
				bvn_dom_shape_equal(ea[i].value, eb[i].value, false,
					depth + 1u);
			if (e != error_none)
				return e;
		}
		return error_none;
	}
	default:
		/* string / symbol / bool / reference / octet: same kind suffices. */
		return error_none;
	}
}
/*
 * Recursively verify that every array in the subtree is homogeneous. Descends
 * into struct fields and array elements so a violation at any depth is found.
 */
/*
 * Verify no key is repeated within one scope — a struct, or the top-level
 * document (spec 1.0). Unique keys keep lookup, references and iteration in
 * agreement on the value of a key. Sorting a copy of the key pointers keeps
 * this O(n log n), so a scope with very many members cannot turn quadratic.
 */
static int bvn_dom_keycmp(const void *a, const void *b)
{
	const char *ka = *(const char *const *)a;
	const char *kb = *(const char *const *)b;
	return strcmp(ka ? ka : "", kb ? kb : "");
}
static error_code_t bvn_dom_keys_unique(
	const bvn_dom_entry_t *ent, uint32_t n)
{
	if (n < 2u)
		return error_none;
	const char **keys = malloc((size_t)n * sizeof(*keys));
	if (!keys) {
		/* OOM: fall back to an in-place O(n^2) compare rather than skipping
		 * the check, which would let a document with duplicate keys pass
		 * validation under memory pressure. Slow for a huge scope, but a
		 * scope that large is unlikely to coexist with a failed small malloc,
		 * and correctness must not depend on available memory. */
		for (uint32_t i = 0; i < n; i++) {
			if (!(ent[i].key && ent[i].key[0]))
				continue;
			for (uint32_t j = i + 1u; j < n; j++)
				if (ent[j].key && ent[j].key[0] &&
				    strcmp(ent[i].key, ent[j].key) == 0)
					return error_duplicate_struct_key;
		}
		return error_none;
	}
	uint32_t m = 0;
	for (uint32_t i = 0; i < n; i++)
		if (ent[i].key && ent[i].key[0])   /* only real, non-empty keys */
			keys[m++] = ent[i].key;
	qsort(keys, (size_t)m, sizeof(*keys), bvn_dom_keycmp);
	error_code_t r = error_none;
	for (uint32_t i = 1; i < m; i++) {
		if (strcmp(keys[i], keys[i - 1]) == 0) {
			r = error_duplicate_struct_key;
			break;
		}
	}
	free(keys);
	return r;
}
static error_code_t bvn_dom_check_homogeneous(
	const bvn_dom_node_t *node, unsigned depth)
{
	if (!node)
		return error_none;
	if (depth > BVN_DOM_CHECK_MAX_DEPTH)
		return bvn_dom_node_type(node) == BVN_DOM_STRUCT
			? error_struct_nesting_too_high
			: error_array_nesting_too_high;
	switch (bvn_dom_node_type(node)) {
	case BVN_DOM_ARRAY: {
		uint32_t n = bvn_dom_array_count(node);
		const bvn_dom_node_t *ref = NULL;
		for (uint32_t i = 0; i < n; i++) {
			const bvn_dom_node_t *e = bvn_dom_array_at(node, i);
			error_code_t sub = bvn_dom_check_homogeneous(e, depth + 1u);
			if (sub != error_none)
				return sub;
			if (!e || bvn_dom_node_type(e) == BVN_DOM_NULL)
				continue;             /* hole */
			if (!ref)
				ref = e;              /* first non-null sets the shape */
			else {
				/* array elements: dimension is enforced (bare arrays and
				 * matrices are uniform). */
				error_code_t cmp = bvn_dom_shape_equal(ref, e, true, depth + 1u);
				if (cmp != error_none)
					return cmp;
			}
		}
		return error_none;
	}
	case BVN_DOM_STRUCT: {
		uint32_t n = bvn_dom_struct_count(node);
		const bvn_dom_entry_t *ent = bvn_dom_struct_entries(node);
		error_code_t dup = bvn_dom_keys_unique(ent, n);
		if (dup != error_none)
			return dup;
		for (uint32_t i = 0; i < n; i++) {
			error_code_t sub =
				bvn_dom_check_homogeneous(ent[i].value, depth + 1u);
			if (sub != error_none)
				return sub;
		}
		return error_none;
	}
	default:
		return error_none;
	}
}
bvn_dom_doc_t *bvn_dom_parse(const void *data, uint32_t len)
{
	bvn_dom_doc_t *doc = bvn_dom_doc_create();
	if (!doc) return NULL;
	bvn_builder_t b;
	if (!builder_init(&b, doc)) {
		bvn_dom_doc_destroy(doc);
		return NULL;
	}
	if (!builder_push(&b, BVN_SCOPE_DOC, NULL)) {
		builder_cleanup(&b);
		bvn_dom_doc_destroy(doc);
		return NULL;
	}
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.max_file_size = UINT32_MAX;
	flags.max_array_nesting = 255;
	flags.max_struct_nesting = 255;
	flags.userdata      = &b;
	flags.on_verified   = on_verified;
	bvnr_reader_t *rd = bvnr_reader_create();
	if (!rd) {
		builder_cleanup(&b);
		bvn_dom_doc_destroy(doc);
		return NULL;
	}
	if (!bvnr_open_read_mem(rd, data, len, NULL, 0, &flags)) {
		bvnr_reader_destroy(rd);
		builder_cleanup(&b);
		bvn_dom_doc_destroy(doc);
		return NULL;
	}
	bool ok = bvnr_read(rd);
	if (!ok) {
		error_code_t reader_err = bvnr_reader_get_error(rd);
		if (reader_err == error_scanner_callback_failed
			&& b.last_error != error_none)
			doc->parse_error = b.last_error;
		else
			doc->parse_error = reader_err;
	} else {
		/* Per-value syntax/type/unit all passed; now enforce the cross-element
		 * structural rules over the materialised tree (spec 1.0): unique keys
		 * within every scope (including the top-level document) and array
		 * element homogeneity. */
		error_code_t herr = bvn_dom_keys_unique(doc->entries, doc->count);
		for (uint32_t i = 0; herr == error_none && i < doc->count; i++)
			herr = bvn_dom_check_homogeneous(doc->entries[i].value, 0u);
		if (herr != error_none)
			doc->parse_error = herr;
	}
	bvnr_reader_destroy(rd);
	builder_do_deferred_pop(&b);
	builder_cleanup(&b);
	return doc;
}
/*
 * Read an entire fd into a growable buffer (capped at max_bytes to bound memory
 * against an endless stream) and parse it. EINTR is retried; the buffer doubles
 * until the cap. Because the DOM needs the whole document in memory anyway,
 * slurping first is simpler than driving the reader off the fd directly.
 */
bvn_dom_doc_t *bvn_dom_parse_fd_ex(int fd, uint64_t max_bytes)
{
	if (!max_bytes || max_bytes > (uint64_t)BVN_DOM_FD_MAX_BYTES)
		max_bytes = BVN_DOM_FD_MAX_BYTES;
	size_t cap = 65536;
	size_t len = 0;
	uint8_t *buf = malloc(cap);
	if (!buf) return NULL;
	for (;;) {
		ssize_t n;
		do {
			n = read(fd, buf + len, cap - len);
		} while (n < 0 && errno == EINTR);
		if (n < 0) {
			free(buf);
			return NULL;
		}
		if (n == 0) break;
		len += (size_t)n;
		if ((uint64_t)len > max_bytes) {
			free(buf);
			return NULL;
		}
		if (len == cap) {
			size_t ncap;
			if (cap > SIZE_MAX / 2u) ncap = SIZE_MAX;
			else                     ncap = cap * 2u;
			if ((uint64_t)ncap > max_bytes) ncap = (size_t)max_bytes + 1u;
			if (ncap <= cap) { free(buf); return NULL; }
			uint8_t *nb = realloc(buf, ncap);
			if (!nb) { free(buf); return NULL; }
			buf = nb;
			cap = ncap;
		}
	}
	if (len > (size_t)UINT32_MAX) { free(buf); return NULL; }
	bvn_dom_doc_t *doc = bvn_dom_parse(buf, (uint32_t)len);
	free(buf);
	return doc;
}
bvn_dom_doc_t *bvn_dom_parse_fd(int fd)
{
	return bvn_dom_parse_fd_ex(fd, BVN_DOM_FD_MAX_BYTES);
}
