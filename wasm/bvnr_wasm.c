/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * bvnr_wasm.c — WebAssembly entry points around the Bovnar C reference parser.
 *
 * This wraps the amalgamated single-file library (build/amalgamate/bovnar.{c,h})
 * with a handful of stable, string-in / JSON-string-out functions suitable for
 * calling across the JS<->WASM boundary. Three surfaces are exposed:
 *
 *   bvnr_wasm_validate(ptr,len) -> JSON  { ok, error, error_name, line, column,
 *                                          byte, offset, recovery_count,
 *                                          declared_version }
 *   bvnr_wasm_to_json (ptr,len) -> JSON  { ok, error, error_name, value }
 *                                          where `value` is the DOM projected to
 *                                          plain JSON (units dropped — see notes)
 *   bvnr_wasm_events  (ptr,len) -> JSON  { ok, error, error_name, events:[...] }
 *                                          the reference event stream (verified)
 *   bvnr_wasm_events_convert(ptr,len,unit,base,allow_nonterminating)
 *                               -> the same stream with the reader's lossless
 *                                  unit/base conversion armed, so events carry
 *                                  converted / converted_base / converted_unit
 *
 * The caller passes a pointer+length (NOT a NUL-terminated string), because a
 * .bvnr document may carry octet streams with embedded NULs. Every returning
 * function hands back a malloc'd, NUL-terminated UTF-8 JSON string that the JS
 * side must release with bvnr_wasm_free().
 *
 * Notes / current limitations (documented, not hidden):
 *   - to_json is a *projection* of the validated DOM to plain JSON values; it
 *     drops unit/type metadata (use events for that). Big integers are emitted
 *     as JSON number tokens with their exact decimal digits (lossless at the
 *     JSON-text level, but a naive JS JSON.parse will round them to double);
 *     integers wider than 64 bits are emitted as JSON strings to stay lossless.
 *     Non-finite floats (inf/-inf/nan) are emitted as JSON null (JSON has no
 *     native infinity/NaN). Octet streams are emitted as a lowercase hex
 *     string. Symbols and references are emitted as their text.
 */
#include "bovnar.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT
#endif

/* ------------------------------------------------------------------ */
/* growable byte buffer                                               */
/* ------------------------------------------------------------------ */
typedef struct {
	char  *p;
	size_t len;
	size_t cap;
	int    oom;
} sb_t;

static void sb_init(sb_t *b) { b->p = NULL; b->len = 0; b->cap = 0; b->oom = 0; }

static int sb_reserve(sb_t *b, size_t extra)
{
	if (b->oom) return 0;
	if (b->len + extra + 1 <= b->cap) return 1;
	size_t ncap = b->cap ? b->cap * 2 : 256;
	while (ncap < b->len + extra + 1) ncap *= 2;
	char *np = (char *)realloc(b->p, ncap);
	if (!np) { b->oom = 1; return 0; }
	b->p = np;
	b->cap = ncap;
	return 1;
}

static void sb_putc(sb_t *b, char c)
{
	if (!sb_reserve(b, 1)) return;
	b->p[b->len++] = c;
}

static void sb_put(sb_t *b, const char *s, size_t n)
{
	if (!sb_reserve(b, n)) return;
	memcpy(b->p + b->len, s, n);
	b->len += n;
}

static void sb_puts(sb_t *b, const char *s) { sb_put(b, s, strlen(s)); }

static void sb_printf(sb_t *b, const char *fmt, ...)
{
	char tmp[160];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if ((size_t)n < sizeof(tmp)) { sb_put(b, tmp, (size_t)n); return; }
	/* rare: needs a bigger scratch */
	if (!sb_reserve(b, (size_t)n)) return;
	va_start(ap, fmt);
	vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
	va_end(ap);
	b->len += (size_t)n;
}

/* Append `n` bytes of `s` as the *contents* of a JSON string (no surrounding
 * quotes), escaping per RFC 8259. Bytes are assumed UTF-8; raw control bytes
 * and the JSON metacharacters are escaped, everything else passes through. */
static void sb_json_str_body(sb_t *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"':  sb_puts(b, "\\\""); break;
		case '\\': sb_puts(b, "\\\\"); break;
		case '\b': sb_puts(b, "\\b");  break;
		case '\f': sb_puts(b, "\\f");  break;
		case '\n': sb_puts(b, "\\n");  break;
		case '\r': sb_puts(b, "\\r");  break;
		case '\t': sb_puts(b, "\\t");  break;
		default:
			if (c < 0x20) sb_printf(b, "\\u%04x", c);
			else          sb_putc(b, (char)c);
		}
	}
}

static void sb_json_cstr(sb_t *b, const char *s)
{
	sb_putc(b, '"');
	if (s) sb_json_str_body(b, s, strlen(s));
	sb_putc(b, '"');
}

/* Return ownership of the buffer's NUL-terminated string to the caller. On OOM
 * frees what it has and returns a tiny static-ish JSON error (heap-allocated so
 * the caller's free() is always valid). */
static char *sb_finish(sb_t *b)
{
	if (b->oom) {
		free(b->p);
		static const char oom[] = "{\"ok\":false,\"error\":\"out_of_memory\"}";
		char *e = (char *)malloc(sizeof oom);           /* sizeof includes the NUL */
		if (e) memcpy(e, oom, sizeof oom);
		return e;
	}
	if (!b->p) { /* empty -> "" */
		char *e = (char *)malloc(1);
		if (e) e[0] = '\0';
		return e;
	}
	b->p[b->len] = '\0';
	return b->p;
}

/* ------------------------------------------------------------------ */
/* DOM -> JSON projection                                             */
/* ------------------------------------------------------------------ */
static void json_emit_node(sb_t *b, const bvn_dom_node_t *node);

static void json_emit_entries(sb_t *b, const bvn_dom_entry_t *entries, uint32_t n)
{
	sb_putc(b, '{');
	for (uint32_t i = 0; i < n; i++) {
		if (i) sb_putc(b, ',');
		sb_json_cstr(b, entries[i].key);
		sb_putc(b, ':');
		json_emit_node(b, entries[i].value);
	}
	sb_putc(b, '}');
}

static const char hexd[] = "0123456789abcdef";

static void json_emit_node(sb_t *b, const bvn_dom_node_t *node)
{
	if (!node) { sb_puts(b, "null"); return; }
	switch (bvn_dom_node_type(node)) {
	case BVN_DOM_NULL:
		sb_puts(b, "null");
		break;
	case BVN_DOM_BOOL: {
		bool v = false;
		bvn_dom_get_bool(node, &v);
		sb_puts(b, v ? "true" : "false");
		break;
	}
	case BVN_DOM_INT: {
		char *s = bvn_dom_int_to_str(node, 10);
		if (s) {
			/* Integers wider than 64 bits cannot survive JSON's double-backed
			 * number type, so emit them as a JSON string (matching the CLI's
			 * `convert`); narrow ints stay as number tokens. */
			if (bvn_dom_get_bigint(node)) {
				sb_putc(b, '"'); sb_puts(b, s); sb_putc(b, '"');
			} else {
				sb_puts(b, s);
			}
			bvn_dom_free_string(s);
		} else {
			sb_puts(b, "null");
		}
		break;
	}
	case BVN_DOM_FLOAT: {
		double v = 0.0;
		bvn_dom_get_float(node, &v);
		/* JSON has no infinity/NaN; emit null for non-finite, as `bovnar
		 * convert` does. */
		if (isnan(v) || isinf(v)) sb_puts(b, "null");
		else                      sb_printf(b, "%.17g", v);
		break;
	}
	case BVN_DOM_STRING: {
		const char *s = NULL; uint32_t n = 0;
		bvn_dom_get_string(node, &s, &n);
		sb_putc(b, '"');
		if (s) sb_json_str_body(b, s, n);
		sb_putc(b, '"');
		break;
	}
	case BVN_DOM_SYMBOL: {
		const char *s = NULL; uint32_t n = 0;
		bvn_dom_get_symbol(node, &s, &n);
		sb_putc(b, '"');
		if (s) sb_json_str_body(b, s, n);
		sb_putc(b, '"');
		break;
	}
	case BVN_DOM_REFERENCE: {
		const char *s = NULL; uint32_t n = 0;
		bvn_dom_get_reference(node, &s, &n);
		sb_putc(b, '"');
		if (s) sb_json_str_body(b, s, n);
		sb_putc(b, '"');
		break;
	}
	case BVN_DOM_OCTET_STREAM: {
		const uint8_t *s = NULL; uint32_t n = 0;
		bvn_dom_get_octets(node, &s, &n);
		sb_putc(b, '"');
		for (uint32_t i = 0; i < n; i++) {
			sb_putc(b, hexd[s[i] >> 4]);
			sb_putc(b, hexd[s[i] & 0xf]);
		}
		sb_putc(b, '"');
		break;
	}
	case BVN_DOM_STRUCT:
		json_emit_entries(b, bvn_dom_struct_entries(node),
		                  bvn_dom_struct_count(node));
		break;
	case BVN_DOM_ARRAY: {
		uint32_t cnt  = bvn_dom_array_count(node);
		uint32_t dims = bvn_dom_array_dims(node);
		if (dims > 1 && cnt % dims == 0) {
			/* Multi-row bovnar array -> nested JSON arrays (one inner array per
			 * row) so the dimensionality survives, matching `bovnar convert`. */
			uint32_t row_len = cnt / dims;
			sb_putc(b, '[');
			for (uint32_t r = 0; r < dims; r++) {
				if (r) sb_putc(b, ',');
				sb_putc(b, '[');
				for (uint32_t c = 0; c < row_len; c++) {
					if (c) sb_putc(b, ',');
					json_emit_node(b, bvn_dom_array_at(node, r * row_len + c));
				}
				sb_putc(b, ']');
			}
			sb_putc(b, ']');
		} else {
			sb_putc(b, '[');
			for (uint32_t i = 0; i < cnt; i++) {
				if (i) sb_putc(b, ',');
				json_emit_node(b, bvn_dom_array_at(node, i));
			}
			sb_putc(b, ']');
		}
		break;
	}
	default:
		sb_puts(b, "null");
		break;
	}
}

/* ------------------------------------------------------------------ */
/* shared: emit the {ok,error,error_name,...} preamble fields         */
/* ------------------------------------------------------------------ */
static void emit_status(sb_t *b, error_code_t err)
{
	sb_printf(b, "\"ok\":%s,", err == error_none ? "true" : "false");
	sb_printf(b, "\"error\":%d,", (int)err);
	sb_puts(b, "\"error_name\":");
	sb_json_cstr(b, bvn_error_to_string(err));
}

/* ================================================================== */
/* exported entry points                                              */
/* ================================================================== */

WASM_EXPORT
char *bvnr_wasm_to_json(const char *buf, int len)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;
	bvn_dom_doc_t *doc = bvn_dom_parse(buf, (uint32_t)len);
	error_code_t err = doc ? bvn_dom_doc_get_parse_error(doc) : error_invalid_argument;

	sb_putc(&b, '{');
	emit_status(&b, err);
	sb_puts(&b, ",\"value\":");
	if (doc && err == error_none)
		json_emit_entries(&b, bvn_dom_doc_entries(doc), bvn_dom_doc_count(doc));
	else
		sb_puts(&b, "null");
	sb_putc(&b, '}');

	if (doc) bvn_dom_doc_destroy(doc);
	return sb_finish(&b);
}

WASM_EXPORT
char *bvnr_wasm_validate(const char *buf, int len)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	bvnr_reader_t *r = bvnr_reader_create();
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.continue_on_error = false;

	bool opened = r && bvnr_open_read_mem(r, buf, (uint64_t)len, NULL, 0, &flags);
	if (opened) bvnr_read(r);

	error_code_t err = r ? bvnr_reader_get_error(r)
	                     : error_invalid_argument;
	if (!opened && err == error_none) err = error_invalid_argument;

	sb_putc(&b, '{');
	emit_status(&b, err);
	if (r) {
		sb_printf(&b, ",\"line\":%llu",
		          (unsigned long long)bvnr_reader_get_error_line(r));
		sb_printf(&b, ",\"column\":%llu",
		          (unsigned long long)bvnr_reader_get_error_column(r));
		sb_printf(&b, ",\"byte\":%u", bvnr_reader_get_error_byte(r));
		sb_printf(&b, ",\"offset\":%llu",
		          (unsigned long long)bvnr_reader_get_error_offset(r));
		sb_printf(&b, ",\"recovery_count\":%llu",
		          (unsigned long long)bvnr_reader_get_recovery_count(r));
		uint16_t maj = 0, min = 0;
		if (bvnr_reader_get_declared_version(r, &maj, &min))
			sb_printf(&b, ",\"declared_version\":\"%u.%u\"", maj, min);
		else
			sb_puts(&b, ",\"declared_version\":null");
	}
	sb_putc(&b, '}');

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

/* ------- event stream ------- */
static const char *evt_name(bvnr_event_t e)
{
	switch (e) {
	case ev_stream_start:                          return "stream_start";
	case ev_assignment_start:                      return "assign_start";
	case ev_octet_stream_start:                    return "octet_start";
	case ev_octet_stream_end:                      return "octet_end";
	case ev_struct_start:                          return "struct_open";
	case ev_struct_end:                            return "struct_close";
	case ev_array_row_start:                       return "array_row_start";
	case ev_array_row_end:                         return "array_row_end";
	case ev_array_dim_start:                       return "array_dim_start";
	case ev_data:                                  return "data";
	case ev_type_annotation_start:                 return "type_start";
	case ev_type_annotation_end:                   return "type_end";
	case ev_type_annotation_type_family:           return "type_family";
	case ev_type_annotation_type_family_parameter: return "type_param";
	case ev_stream_end:                            return "stream_end";
	}
	return "?";
}

static const char *evt_tok(token_type_t t)
{
	switch (t) {
	case token_is_identifier:   return "identifier";
	case token_is_string:       return "string";
	case token_is_number:       return "number";
	case token_is_symbol:       return "symbol";
	case token_is_reference:    return "reference";
	case token_is_array_number: return "array_number";
	case token_is_array_string: return "array_string";
	case token_is_type:         return "type";
	case token_is_octet_stream: return "octet_stream";
	case token_is_null_value:   return "null";
	case token_is_structure:    return "structure";
	case token_is_unit:         return "unit";
	case token_is_type_width:   return "type_width";
	case token_is_type_base:    return "type_base";
	case token_is_type_q:       return "type_q";
	case token_is_bool:         return "bool";
	case token_is_unknown:      return "unknown";
	}
	return "?";
}

static const char *evt_family(value_type_family_t f)
{
	switch (f) {
	case vt_plain:     return "plain";
	case vt_utf8:      return "utf8";
	case vt_sint:      return "sint";
	case vt_uint:      return "uint";
	case vt_float:     return "float";
	case vt_float_fix: return "float_fix";
	case vt_float_dec: return "float_dec";
	case vt_bool:      return "bool";
	case vt_datetime:  return "datetime";
	case vt_illegal:   return "illegal";
	}
	return "?";
}

typedef struct {
	sb_t    *b;
	uint64_t seq;
	int      count;
} evt_ctx_t;

/*
 * on_verified and want_unit share the single `userdata` slot, so they share one
 * context: `ev` for the event serialiser, `req` for the conversion target. A
 * NULL `req.unit` means "keep each value's own unit", which together with a base
 * makes the request a pure base conversion.
 */
typedef struct {
	evt_ctx_t           ev;
	const value_unit_t *unit;
	uint32_t            base;
} events_ud_t;


static bool evt_cb(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	evt_ctx_t *ctx = &((events_ud_t *)ud)->ev;
	sb_t *b = ctx->b;
	if (ctx->count++) sb_putc(b, ',');
	sb_putc(b, '{');
	sb_printf(b, "\"seq\":%llu", (unsigned long long)ctx->seq++);
	sb_puts(b, ",\"ev\":");
	sb_json_cstr(b, evt_name(e));
	sb_puts(b, ",\"tok\":");
	sb_json_cstr(b, evt_tok(d->type));

	/* textual payload (skip raw octet bytes; report their length instead) */
	if (d->type == token_is_octet_stream) {
		sb_printf(b, ",\"bytes\":%u", d->length);
	} else if (d->data && d->length) {
		sb_puts(b, ",\"text\":\"");
		sb_json_str_body(b, (const char *)d->data, d->length);
		sb_putc(b, '"');
	}

	/* type/unit metadata, where meaningful */
	if (d->value_type.family != vt_plain &&
	    d->type != token_is_type_width &&
	    d->type != token_is_type_base &&
	    d->type != token_is_unit) {
		sb_puts(b, ",\"family\":");
		sb_json_cstr(b, evt_family(d->value_type.family));
		sb_printf(b, ",\"width\":%u", d->value_type.width);
		sb_printf(b, ",\"base\":%u", d->value_type.base);
		char unit_buf[160];
		if (bvn_unit_to_string(d->value_unit, unit_buf, sizeof(unit_buf)) >= 0 &&
		    unit_buf[0] && strcmp(unit_buf, "no_unit") != 0) {
			sb_puts(b, ",\"unit\":");
			sb_json_cstr(b, unit_buf);
		}
		if (d->value_type.family == vt_datetime) {
			const char *ep = bvnr_datetime_epoch_name(d->value_type);
			if (ep) { sb_puts(b, ",\"epoch\":"); sb_json_cstr(b, ep); }
		}
	}
	/* numeric value of a type-annotation width/base parameter (carried in
	 * value_type, not in d->data) so the shim can rebuild the annotation. */
	if (d->type == token_is_type_width)
		sb_printf(b, ",\"width\":%u", d->value_type.width);
	else if (d->type == token_is_type_base)
		sb_printf(b, ",\"base\":%u", d->value_type.base);
	if (d->frac_data && d->frac_length) {
		sb_puts(b, ",\"frac\":\"");
		sb_json_str_body(b, (const char *)d->frac_data, d->frac_length);
		sb_putc(b, '"');
	}
	/* lossless read-time unit/base conversion (bvnr_read_flags_t.want_unit).
	 * conv.text is NULL when the exact result has no terminating expansion in
	 * the output base (want_unit_allow_nonterminating): report the conversion
	 * with a null value rather than dropping the event's conversion fields. */
	if (d->converted) {
		if (d->conv.text) {
			sb_puts(b, ",\"converted\":\"");
			sb_json_str_body(b, d->conv.text, d->conv.length);
			sb_putc(b, '"');
		} else {
			sb_puts(b, ",\"converted\":null");
		}
		sb_printf(b, ",\"converted_base\":%u", d->conv.base);
		char ub[160];
		if (bvn_unit_to_string(d->conv.unit, ub, sizeof ub) >= 0 && ub[0]) {
			sb_puts(b, ",\"converted_unit\":");
			sb_json_cstr(b, ub);
		}
	}
	sb_putc(b, '}');
	return true;
}

static bool conv_want_unit(void *ud, const bvnr_data_t *d,
			   value_unit_t *want, uint32_t *want_base)
{
	const events_ud_t *u = (const events_ud_t *)ud;
	*want      = u->unit ? *u->unit : d->value_unit;
	*want_base = u->base;
	return true;
}

static char *events_impl(const char *buf, int len,
			 const value_unit_t *target, uint32_t base,
			 bool convert, bool allow_nonterminating)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	sb_t evbuf; sb_init(&evbuf);
	events_ud_t ud = { { &evbuf, 0, 0 }, target, base };

	bvnr_reader_t *r = bvnr_reader_create();
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata = &ud;
	flags.on_verified = evt_cb;
	/* resync: emit verified events for every well-formed assignment, skipping
	 * (not aborting at) a broken one — so the playground tree and the demos'
	 * resync mode show all recoverable structure past an error. */
	flags.continue_on_error = true;
	if (convert) {
		flags.want_unit = conv_want_unit;
		flags.want_unit_allow_nonterminating = allow_nonterminating;
	}

	bool opened = r && bvnr_open_read_mem(r, buf, (uint64_t)len, NULL, 0, &flags);
	if (opened) bvnr_read(r);

	error_code_t err = r ? bvnr_reader_get_error(r) : error_invalid_argument;
	if (!opened && err == error_none) err = error_invalid_argument;

	sb_putc(&b, '{');
	emit_status(&b, err);
	sb_puts(&b, ",\"events\":[");
	char *evs = sb_finish(&evbuf);
	if (evs) { sb_puts(&b, evs); free(evs); }
	sb_puts(&b, "]}");

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

WASM_EXPORT
char *bvnr_wasm_events(const char *buf, int len)
{
	return events_impl(buf, len, NULL, 0u, false, false);
}

/*
 * Same event stream, but with the reader's lossless read-time unit/base
 * conversion armed — the only way to reach the `converted` / `converted_base` /
 * `converted_unit` fields evt_cb emits.
 *
 * `unit` is a NUL-terminated unit string ("m", "k~m/s", ""/NULL to keep each
 * value's own unit — which with a `base` is a pure base conversion). `base` is
 * the output base (0 keeps the value's own; otherwise 2..62, 64 or 85).
 * `allow_nonterminating` non-zero delivers a result that is exact as a rational
 * but has no finite expansion in `base` with `"converted":null` instead of
 * failing the parse.
 *
 * An unparseable `unit` is reported as error_unit_illegal rather than silently
 * falling back to no conversion.
 *
 * Note the interaction with this surface's resync policy: because
 * continue_on_error is on, a value the conversion rejects — dimensionally
 * incompatible with `unit`, or inexact — is skipped along with its assignment
 * rather than aborting, so it simply does not appear in `events` and the
 * top-level status stays ok. Ask for a unit compatible with what you expect, or
 * compare the event count against an unconverted run.
 */
WASM_EXPORT
char *bvnr_wasm_events_convert(const char *buf, int len, const char *unit,
			       int base, int allow_nonterminating)
{
	value_unit_t        target;
	const value_unit_t *tp = NULL;

	if (unit && unit[0]) {
		bool ok = false;
		target = bvn_parse_unit((const uint8_t *)unit, &ok);
		if (!ok) {
			sb_t b; sb_init(&b);
			sb_putc(&b, '{');
			emit_status(&b, error_unit_illegal);
			sb_puts(&b, ",\"events\":[]}");
			return sb_finish(&b);
		}
		tp = &target;
	}
	return events_impl(buf, len, tp, (uint32_t)(base > 0 ? base : 0),
			   true, allow_nonterminating != 0);
}

/* ------- all errors (resync mode) ------- */
typedef struct {
	sb_t *b;
	int   count;
} err_ctx_t;

static void err_collect(void *ud, error_code_t err,
                        uint64_t line, uint64_t column,
                        uint32_t byte, uint64_t offset)
{
	err_ctx_t *ctx = (err_ctx_t *)ud;
	sb_t *b = ctx->b;
	if (ctx->count++) sb_putc(b, ',');
	sb_putc(b, '{');
	sb_printf(b, "\"error\":%d,", (int)err);
	sb_puts(b, "\"error_name\":");
	sb_json_cstr(b, bvn_error_to_string(err));
	sb_printf(b, ",\"line\":%llu", (unsigned long long)line);
	sb_printf(b, ",\"column\":%llu", (unsigned long long)column);
	sb_printf(b, ",\"byte\":%u", byte);
	sb_printf(b, ",\"offset\":%llu", (unsigned long long)offset);
	sb_putc(b, '}');
}

/* Validate in resync mode, collecting EVERY error (not just the first) the way
 * the playground's on_error panel expects. Returns
 * { ok, errors:[{error,error_name,line,column,byte,offset}], declared_version }. */
WASM_EXPORT
char *bvnr_wasm_errors(const char *buf, int len)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	sb_t ebuf; sb_init(&ebuf);
	err_ctx_t ctx = { &ebuf, 0 };

	bvnr_reader_t *r = bvnr_reader_create();
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata          = &ctx;
	flags.on_error          = err_collect;
	flags.continue_on_error = true;   /* resync: report all errors, keep going */

	bool opened = r && bvnr_open_read_mem(r, buf, (uint64_t)len, NULL, 0, &flags);
	if (opened) bvnr_read(r);

	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s", ctx.count == 0 ? "true" : "false");
	sb_puts(&b, ",\"errors\":[");
	char *es = sb_finish(&ebuf);
	if (es) { sb_puts(&b, es); free(es); }
	sb_puts(&b, "]");
	if (r) {
		uint16_t maj = 0, min = 0;
		if (bvnr_reader_get_declared_version(r, &maj, &min))
			sb_printf(&b, ",\"declared_version\":\"%u.%u\"", maj, min);
		else
			sb_puts(&b, ",\"declared_version\":null");
	}
	sb_putc(&b, '}');

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

/* ================================================================== */
/* rich document tree (drives the playground's WASM-backed parser shim)*/
/* ================================================================== */
/* Shortest round-tripping %g representation of a finite double (so the tree
 * shows "9.81", not "9.8100000000000005"). */
static void wdoc_float_text(sb_t *b, double v)
{
	char tmp[64];
	for (int p = 1; p <= 17; p++) {
		snprintf(tmp, sizeof tmp, "%.*g", p, v);
		if (strtod(tmp, NULL) == v) break;
	}
	sb_json_str_body(b, tmp, strlen(tmp));
}
static void wdoc_float_number(sb_t *b, double v)
{
	char tmp[64];
	for (int p = 1; p <= 17; p++) {
		snprintf(tmp, sizeof tmp, "%.*g", p, v);
		if (strtod(tmp, NULL) == v) break;
	}
	sb_puts(b, tmp);
}

/* Resolved type fields shared by ann + valueType on the JS side:
 * familyName, width, base (numeric only), unit (string|null), epoch (dt|null). */
static void wdoc_type_fields(sb_t *b, const bvn_dom_node_t *node)
{
	value_type_spec_t vt = bvn_dom_get_value_type(node);
	sb_puts(b, ",\"familyName\":");
	sb_json_cstr(b, evt_family(vt.family));
	if (bvn_type_is_numeric(vt)) {
		sb_printf(b, ",\"width\":%u", bvn_effective_width(vt));
		sb_printf(b, ",\"base\":%u", bvn_effective_base(vt));
	} else {
		sb_puts(b, ",\"width\":null,\"base\":null");
	}
	char ub[160];
	int32_t ul = bvn_dom_get_unit_string(node, ub, sizeof ub);
	if (ul > 0 && strcmp(ub, "no_unit") != 0) {
		sb_puts(b, ",\"unit\":");
		sb_json_cstr(b, ub);
	} else {
		sb_puts(b, ",\"unit\":null");
	}
	if (vt.family == vt_datetime) {
		const char *ep = bvnr_datetime_epoch_name(vt);
		if (ep) { sb_puts(b, ",\"epoch\":"); sb_json_cstr(b, ep); }
		else      sb_puts(b, ",\"epoch\":null");
	} else {
		sb_puts(b, ",\"epoch\":null");
	}
}

static void wdoc_emit_value(sb_t *b, const bvn_dom_node_t *node);

static void wdoc_emit_entry(sb_t *b, const char *key, const bvn_dom_node_t *node)
{
	sb_puts(b, "{\"key\":");
	sb_json_cstr(b, key);
	sb_puts(b, ",\"value\":");
	wdoc_emit_value(b, node);
	sb_putc(b, '}');
}

static void wdoc_emit_value(sb_t *b, const bvn_dom_node_t *node)
{
	if (!node) { sb_puts(b, "{\"type\":\"null\",\"text\":\"\"}"); return; }
	switch (bvn_dom_node_type(node)) {
	case BVN_DOM_NULL:
		sb_puts(b, "{\"type\":\"null\",\"text\":\"\"");
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	case BVN_DOM_BOOL: {
		bool v = false; bvn_dom_get_bool(node, &v);
		sb_puts(b, "{\"type\":\"scalar\",\"kind\":\"bool\",\"text\":");
		sb_json_cstr(b, v ? "true" : "false");
		sb_printf(b, ",\"value\":%s", v ? "true" : "false");
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	case BVN_DOM_INT: {
		char *s = bvn_dom_int_to_str(node, 10);
		bool big = bvn_dom_get_bigint(node) != NULL;
		value_type_spec_t vt = bvn_dom_get_value_type(node);
		sb_puts(b, "{\"type\":\"scalar\",\"kind\":");
		sb_json_cstr(b, vt.family == vt_datetime ? "datetime" : "number");
		sb_puts(b, ",\"text\":");
		sb_json_cstr(b, s ? s : "0");
		sb_puts(b, ",\"value\":");
		if (!s)        sb_puts(b, "null");
		else if (big) { sb_putc(b, '"'); sb_puts(b, s); sb_putc(b, '"'); }
		else           sb_puts(b, s);
		if (s) bvn_dom_free_string(s);
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	case BVN_DOM_FLOAT: {
		double v = 0.0; bvn_dom_get_float(node, &v);
		value_type_spec_t vt = bvn_dom_get_value_type(node);
		bool fin = !(isnan(v) || isinf(v));
		sb_puts(b, "{\"type\":\"scalar\",\"kind\":");
		sb_json_cstr(b, vt.family == vt_datetime ? "datetime"
		                : (fin ? "number" : "special"));
		sb_puts(b, ",\"text\":\"");
		if (isnan(v))      sb_puts(b, "nan");
		else if (isinf(v)) sb_puts(b, v < 0 ? "-inf" : "inf");
		else               wdoc_float_text(b, v);
		sb_puts(b, "\",\"value\":");
		if (fin) wdoc_float_number(b, v);
		else     sb_puts(b, "null");
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	case BVN_DOM_STRING: {
		const char *s = NULL; uint32_t n = 0; bvn_dom_get_string(node, &s, &n);
		sb_puts(b, "{\"type\":\"scalar\",\"kind\":\"string\",\"text\":\"");
		if (s) sb_json_str_body(b, s, n);
		sb_puts(b, "\",\"value\":\"");
		if (s) sb_json_str_body(b, s, n);
		sb_putc(b, '"');
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	case BVN_DOM_SYMBOL: {
		const char *s = NULL; uint32_t n = 0; bvn_dom_get_symbol(node, &s, &n);
		sb_puts(b, "{\"type\":\"scalar\",\"kind\":\"symbol\",\"text\":\"");
		if (s) sb_json_str_body(b, s, n);
		sb_puts(b, "\",\"value\":\"");
		if (s) sb_json_str_body(b, s, n);
		sb_putc(b, '"');
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	case BVN_DOM_REFERENCE: {
		const char *s = NULL; uint32_t n = 0; bvn_dom_get_reference(node, &s, &n);
		sb_puts(b, "{\"type\":\"reference\",\"kind\":\"reference\",\"text\":\"&");
		if (s) sb_json_str_body(b, s, n);
		sb_puts(b, "\",\"value\":\"");
		if (s) sb_json_str_body(b, s, n);
		sb_putc(b, '"');
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	case BVN_DOM_OCTET_STREAM: {
		const uint8_t *s = NULL; uint32_t n = 0; bvn_dom_get_octets(node, &s, &n);
		sb_printf(b, "{\"type\":\"octet\",\"total\":%u,\"chunks\":[{\"length\":%u}]}",
		          n, n);
		break;
	}
	case BVN_DOM_STRUCT: {
		sb_puts(b, "{\"type\":\"struct\",\"children\":[");
		const bvn_dom_entry_t *e = bvn_dom_struct_entries(node);
		uint32_t cnt = bvn_dom_struct_count(node);
		for (uint32_t i = 0; i < cnt; i++) {
			if (i) sb_putc(b, ',');
			wdoc_emit_entry(b, e[i].key, e[i].value);
		}
		sb_puts(b, "]}");
		break;
	}
	case BVN_DOM_ARRAY: {
		uint32_t cnt = bvn_dom_array_count(node);
		uint32_t dims = bvn_dom_array_dims(node);
		sb_printf(b, "{\"type\":\"array\",\"dims\":%u,\"rows\":[", dims ? dims : 1);
		if (dims > 1 && cnt % dims == 0) {
			uint32_t rl = cnt / dims;
			for (uint32_t r = 0; r < dims; r++) {
				if (r) sb_putc(b, ',');
				sb_puts(b, "{\"elements\":[");
				for (uint32_t c = 0; c < rl; c++) {
					if (c) sb_putc(b, ',');
					wdoc_emit_value(b, bvn_dom_array_at(node, r * rl + c));
				}
				sb_puts(b, "]}");
			}
		} else {
			sb_puts(b, "{\"elements\":[");
			for (uint32_t i = 0; i < cnt; i++) {
				if (i) sb_putc(b, ',');
				wdoc_emit_value(b, bvn_dom_array_at(node, i));
			}
			sb_puts(b, "]}");
		}
		sb_puts(b, "]");
		wdoc_type_fields(b, node);
		sb_putc(b, '}');
		break;
	}
	default:
		sb_puts(b, "{\"type\":\"null\",\"text\":\"\"}");
		break;
	}
}

/* Full document tree in document order, each value carrying its resolved type
 * fields. The shim turns this into parseFaithful's tree and replays it as
 * parseBovnar's event stream. Returns { ok, tree:[{key,value}...] }. */
WASM_EXPORT
char *bvnr_wasm_doc(const char *buf, int len)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;
	bvn_dom_doc_t *doc = bvn_dom_parse(buf, (uint32_t)len);
	error_code_t err = doc ? bvn_dom_doc_get_parse_error(doc)
	                       : error_invalid_argument;
	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s", err == error_none ? "true" : "false");
	sb_puts(&b, ",\"tree\":[");
	if (doc) {
		const bvn_dom_entry_t *e = bvn_dom_doc_entries(doc);
		uint32_t cnt = bvn_dom_doc_count(doc);
		for (uint32_t i = 0; i < cnt; i++) {
			if (i) sb_putc(&b, ',');
			wdoc_emit_entry(&b, e[i].key, e[i].value);
		}
	}
	sb_puts(&b, "]}");
	if (doc) bvn_dom_doc_destroy(doc);
	return sb_finish(&b);
}

WASM_EXPORT
const char *bvnr_wasm_version(void)
{
	return bvnr_version_string();
}

WASM_EXPORT
void bvnr_wasm_free(char *p)
{
	free(p);
}
