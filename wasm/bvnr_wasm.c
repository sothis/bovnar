/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * bvnr_wasm.c — WebAssembly entry points around the Bovnar C reference parser.
 *
 * This wraps the amalgamated single-file library (build/amalgamate/bovnar.{c,h})
 * with stable, string-in / JSON-string-out functions suitable for calling
 * across the JS<->WASM boundary. These surfaces are exposed:
 *
 *   bvnr_wasm_validate(ptr,len) -> JSON  { ok, error, error_name, tier, line,
 *                                          column, byte, offset, recovery_count,
 *                                          skipped_bytes, declared_version }
 *   bvnr_wasm_to_json (ptr,len) -> JSON  { ok, error, error_name, value }
 *                                          where `value` is the DOM projected to
 *                                          plain JSON (units dropped — see notes)
 *   bvnr_wasm_events  (ptr,len) -> JSON  { ok, error, error_name, line, column,
 *                                          byte, offset, recovery_count,
 *                                          skipped_bytes, declared_version,
 *                                          events:[...] }
 *                                          the reference event stream (verified)
 *   bvnr_wasm_events_convert(ptr,len,unit,base,allow_nonterminating)
 *                               -> the same stream with the reader's lossless
 *                                  unit/base conversion armed, so events carry
 *                                  converted / converted_base / converted_unit,
 *                                  and converted_num / converted_den when the
 *                                  exact result has no terminating expansion
 *   bvnr_wasm_parse   (ptr,len) -> JSON  the union of events and errors from ONE
 *                                          reader pass; what a caller wanting
 *                                          both should use
 *   bvnr_wasm_errors  (ptr,len) -> JSON  { ok, error, error_name, <position>,
 *                                          recovery_count, skipped_bytes,
 *                                          declared_version, errors:[...] }
 *                                          every error, in resync mode
 *   bvnr_wasm_doc     (ptr,len) -> JSON  { ok, error, error_name, tree:[...] }
 *                                          the document tree with type fields,
 *                                          the written unit spelling and a
 *                                          datetime's sub-second digits
 *   bvnr_wasm_version()         -> the library version string
 *
 * Each parsing surface above also has an `_ex` twin taking two extra words:
 *   <name>_ex(ptr, len, opts, policy)      and, for events_convert,
 *   bvnr_wasm_events_convert_ex(ptr, len, unit, base, allow, opts, policy).
 * `opts` is 0 or a bvnr_wasm_opts_t the JS wrapper fills in (the reader flags
 * that had no way in: strict_version, text_only, the byte/count caps and the
 * conversion work limit); `policy` is 0 or a handle from
 * bvnr_wasm_policy_create() (bvnr_reader_set_unit_policy / bvn_dom_parse_policy
 * as data). The old names stay for a wrapper paired with an older core, and
 * the JS layer feature-tests the _ex symbols rather than silently dropping an
 * option it cannot pass. toJSON and doc take a policy only, in the THREE-word
 * form (ptr, len, policy): their DOM entry points take no read flags.
 *
 * Small, stateless helpers mirror the rest of the reader-side C API:
 * bvnr_wasm_version_number / bvnr_wasm_spec_version / bvnr_wasm_peek_version,
 * bvnr_wasm_error_name, bvnr_wasm_unit (parse + canonical spelling + profile
 * flag), bvnr_wasm_units_equal, bvnr_wasm_units_convertible,
 * bvnr_wasm_convert_unit, bvnr_wasm_unit_to_profile and
 * bvnr_wasm_unit_profiles.
 *
 * The caller passes a pointer+length (NOT a NUL-terminated string), because a
 * .bvnr document may carry octet streams with embedded NULs. Every returning
 * function hands back a malloc'd, NUL-terminated UTF-8 JSON string that the JS
 * side must release with bvnr_wasm_free(). (The few that take a unit string —
 * unit, units_equal, convert_unit, the policy adds — take it NUL-terminated;
 * a unit and a key path cannot contain NUL.)
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
 *   - A want_unit conversion that cannot terminate in the output base carries
 *     its exact value as converted_num/converted_den decimal STRINGS (the
 *     rational can exceed 2^53); when it can terminate, `converted` is that
 *     exact text and the rational fields are omitted.
 *   - doc(), unlike to_json, keeps unitSource (the document's own spelling)
 *     and a datetime's `frac` (its sub-second digits). A value a unit policy
 *     converted has no unitSource, by the DOM's own contract.
 */
#include "bovnar.h"
/* The DOM API (bvn_dom_parse, the typed accessors) and the exact-rational
 * helpers. The amalgamated bovnar.h carries all public headers already and
 * their guards make these no-ops there; they make this file compile against
 * include/ directly, which is how the native shape test builds it. */
#include "bovnar_dom.h"
#include "bovnar_si_units.h"
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
 * frees what it has and returns a small JSON error (heap-allocated so the
 * caller's free() is always valid).
 *
 * That error has the SAME shape as every other result: ok, a numeric error and
 * its error_name. It used to be {"ok":false,"error":"out_of_memory"} -- `error`
 * a string where every other payload has a number, and no error_name at all, so
 * `r.error_name` was undefined on the one path a caller is least likely to have
 * tested. The library has no code of its own for exhaustion and reports it as
 * error_invalid_argument (see bvnr_read_flags_t.want_unit); `detail` says which
 * it was. */
static char *sb_finish(sb_t *b)
{
	if (b->oom) {
		free(b->p);
		b->p = NULL; b->len = 0; b->cap = 0;
		char oom[160];
		int n = snprintf(oom, sizeof oom,
			"{\"ok\":false,\"error\":%d,\"error_name\":\"%s\","
			"\"detail\":\"out of memory\"}",
			(int)error_invalid_argument,
			bvn_error_to_string(error_invalid_argument));
		if (n < 0 || (size_t)n >= sizeof oom) return NULL;
		char *e = (char *)malloc((size_t)n + 1u);
		if (e) memcpy(e, oom, (size_t)n + 1u);
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

/* Splice a finished sub-buffer (the events or errors array body) into `into`,
 * and release it.
 *
 * NOT by way of sb_finish: on exhaustion that returns the JSON error OBJECT,
 * and the callers used to paste whatever it returned between "events":[ and ]
 * -- a truncated listing became {"ok":true,...,"events":[{"ok":false,...}]},
 * a well-formed success wrapped around the report of its own failure. An
 * exhausted part makes the whole result the out-of-memory result. */
static void sb_splice(sb_t *into, sb_t *part)
{
	if (part->oom) into->oom = 1;
	else if (part->p) sb_put(into, part->p, part->len);
	free(part->p);
	part->p = NULL; part->len = 0; part->cap = 0;
}

/* ------------------------------------------------------------------ */
/* read options: bvnr_read_flags_t, as a flat block the JS wrapper     */
/* writes into the module's heap                                      */
/* ------------------------------------------------------------------ */
/*
 * The C API's reader options (strict_version, text_only, the byte/count caps,
 * max_conversion_length, the length limits, the nesting limits) had no way in
 * from JavaScript at all: every export opened its reader with a zeroed flags
 * struct and two overridden nesting limits. These are assertions a consumer
 * makes about the input ("this channel is text only", "a document that
 * declares a future version is an error"), and the C API's own comment on
 * text_only says why silently ignoring one is worse than not offering it.
 *
 * The block is fixed-width and version-stamped rather than a packed bitfield so
 * a stale wrapper cannot be read against a newer core: a layout the core does
 * not know is refused with error_invalid_argument, never half-applied. 0 in any
 * limit means the same thing it means in bvnr_read_flags_t (0 = the library's
 * own default, or, for max_file_size, unlimited). The two nesting fields differ
 * from the raw C struct on purpose: 0 there selects the reader's default of 64,
 * while every surviving WASM surface has always run at the format's hard limit
 * of 255, so 0 here means "keep 255" and an explicit value is range-checked.
 */
#define BVNR_WASM_OPTS_VERSION	1u
#define BVNR_WASM_OPT_STRICT_VERSION	0x1u
#define BVNR_WASM_OPT_TEXT_ONLY		0x2u
/* Deliver an octet chunk's payload as a hex string in its data event. The
 * reader's chunk events carry the bytes; JSON cannot, so without this a JS
 * consumer of a binary document saw only lengths (and toJSON's whole-stream
 * hex string, which loses the chunk boundaries). Off by default: hex doubles
 * the payload, and the playground parses documents it never needs bytes for.
 * A core from before this bit refuses it as an unknown flag rather than
 * ignoring the request. */
#define BVNR_WASM_OPT_OCTETS_HEX	0x4u
#define BVNR_WASM_OPTS_KNOWN		(BVNR_WASM_OPT_STRICT_VERSION | \
					 BVNR_WASM_OPT_TEXT_ONLY | \
					 BVNR_WASM_OPT_OCTETS_HEX)
typedef struct {
	uint32_t	version;
	uint32_t	flags;
	uint64_t	max_file_size;
	uint64_t	max_text_bytes;
	uint64_t	max_array_items;
	uint32_t	max_conversion_length;
	uint32_t	max_identifier_length;
	uint32_t	max_string_length;
	uint32_t	max_number_length;
	uint32_t	max_symbol_length;
	uint32_t	max_reference_length;
	uint32_t	max_struct_nesting;
	uint32_t	max_array_nesting;
	uint32_t	_reserved[2];
} bvnr_wasm_opts_t;

/* The JS wrapper writes this block field by field through a DataView with the
 * offsets hard-coded (wasm/index.mjs, OPTS_SIZE). A compiler that lays it out
 * differently would make the wrapper write the wrong fields -- so the layout is
 * pinned here, where the C compiler is the authority. C11 only; the native test
 * builds this file as C99. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(bvnr_wasm_opts_t) == 72,
	       "options layout: keep wasm/index.mjs OPTS_SIZE in step");
_Static_assert(offsetof(bvnr_wasm_opts_t, max_file_size) == 8, "options layout");
_Static_assert(offsetof(bvnr_wasm_opts_t, max_conversion_length) == 32,
	       "options layout");
_Static_assert(offsetof(bvnr_wasm_opts_t, max_struct_nesting) == 56,
	       "options layout");
_Static_assert(offsetof(bvnr_wasm_opts_t, max_array_nesting) == 60,
	       "options layout");
#endif

/* Copy `o` into a reader flags struct. False means the block is not one this
 * build understands; the caller reports error_invalid_argument rather than
 * reading fields it cannot trust. */
static bool wasm_read_flags(bvnr_read_flags_t *f, const bvnr_wasm_opts_t *o)
{
	memset(f, 0, sizeof *f);
	f->max_struct_nesting = 255;   /* the WASM surfaces' long-standing limit */
	f->max_array_nesting  = 255;
	if (!o) return true;
	if (o->version != BVNR_WASM_OPTS_VERSION) return false;
	if (o->flags & ~(uint32_t)BVNR_WASM_OPTS_KNOWN) return false;
	if (o->max_identifier_length > 0xffffu ||
	    o->max_string_length     > 0xffffu ||
	    o->max_number_length     > 0xffffu ||
	    o->max_symbol_length     > 0xffffu ||
	    o->max_reference_length  > 0xffffu ||
	    o->max_struct_nesting    > 255u    ||
	    o->max_array_nesting     > 255u)
		return false;
	f->strict_version         = (o->flags & BVNR_WASM_OPT_STRICT_VERSION) != 0u;
	f->text_only              = (o->flags & BVNR_WASM_OPT_TEXT_ONLY) != 0u;
	f->max_file_size          = o->max_file_size;
	f->max_text_bytes         = o->max_text_bytes;
	f->max_array_items        = o->max_array_items;
	f->max_conversion_length  = o->max_conversion_length;
	f->max_identifier_length  = (uint16_t)o->max_identifier_length;
	f->max_string_length      = (uint16_t)o->max_string_length;
	f->max_number_length      = (uint16_t)o->max_number_length;
	f->max_symbol_length      = (uint16_t)o->max_symbol_length;
	f->max_reference_length   = (uint16_t)o->max_reference_length;
	if (o->max_struct_nesting) f->max_struct_nesting = (uint8_t)o->max_struct_nesting;
	if (o->max_array_nesting)  f->max_array_nesting  = (uint8_t)o->max_array_nesting;
	return true;
}

/* ------------------------------------------------------------------ */
/* unit policy handle (bvnr_reader_set_unit_policy / bvn_dom_parse_policy) */
/* ------------------------------------------------------------------ */
/*
 * The policy is data in the C API -- "what the document must contain, and what
 * unit the consumer wants values delivered in" -- and it parses every unit
 * string when it is INSTALLED, so a malformed unit or rule path is reported
 * before the parse begins. Neither half was reachable from JavaScript.
 *
 * A handle keeps the strings and the two struct arrays alive between calls; the
 * JS builder adds an entry at a time and each add is checked by installing the
 * whole candidate policy on a throwaway reader -- the library's own parser is
 * the only oracle for what it will accept, exactly as the Python binding's
 * build_unit_policy uses it. A refused add leaves the handle as it was.
 */
typedef struct {
	char			**strings;
	uint32_t		  nstrings, cstrings;
	bvnr_unit_rule_t	 *rules;
	uint32_t		  nrules, crules;
	bvnr_unit_target_t	 *targets;
	uint32_t		  ntargets, ctargets;
	const char		**reqdim;
	uint32_t		  nreqdim, creqdim;
	uint32_t		  base;
	int			  normalise;
	int			  on_inexact;
	bool			  require_unit;
} wasm_policy_t;

static bool wasm_grow(void **arr, uint32_t *cap, uint32_t need, size_t elem)
{
	if (need <= *cap) return true;
	uint32_t ncap = *cap ? *cap * 2u : 4u;
	while (ncap < need) ncap *= 2u;
	void *np = realloc(*arr, (size_t)ncap * elem);
	if (!np) return false;
	*arr = np;
	*cap = ncap;
	return true;
}

static char *wasm_strdup(const char *s)
{
	size_t n = strlen(s) + 1u;
	char *p = (char *)malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static void policy_view(const wasm_policy_t *p, bvnr_unit_policy_t *out)
{
	memset(out, 0, sizeof *out);
	out->rules                    = p->nrules ? p->rules : NULL;
	out->num_rules                = p->nrules;
	out->targets                  = p->ntargets ? p->targets : NULL;
	out->num_targets              = p->ntargets;
	out->base                     = p->base;
	out->normalise                = p->normalise ? bvnr_normalise_si
						     : bvnr_normalise_none;
	out->on_inexact               = p->on_inexact ? bvnr_inexact_leave
						     : bvnr_inexact_error;
	out->require_unit             = p->require_unit;
	out->require_dimension_of     = p->nreqdim ? p->reqdim : NULL;
	out->num_require_dimension_of = p->nreqdim;
}

/* Install the policy on a throwaway reader: the library's own acceptance test,
 * shared with bvnr_reader_set_unit_policy, so the wrapper cannot accept a
 * policy the reader would refuse later. */
static bool policy_try(const wasm_policy_t *p)
{
	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) return false;
	bvnr_unit_policy_t v;
	policy_view(p, &v);
	bool ok = bvnr_reader_set_unit_policy(r, &v);
	bvnr_reader_destroy(r);
	return ok;
}

/* Truncate a policy back to a remembered state after a refused add. Strings are
 * appended in the same order as the entry that owns them, so the tail is
 * exactly the candidate's. */
static void policy_truncate(wasm_policy_t *p, uint32_t nstrings,
			    uint32_t nrules, uint32_t ntargets, uint32_t nreqdim)
{
	for (uint32_t i = nstrings; i < p->nstrings; i++)
		free(p->strings[i]);
	p->nstrings = nstrings;
	p->nrules   = nrules;
	p->ntargets = ntargets;
	p->nreqdim  = nreqdim;
}

static bool policy_install(const wasm_policy_t *policy, bvnr_reader_t *r)
{
	if (!policy) return true;
	bvnr_unit_policy_t v;
	policy_view(policy, &v);
	return bvnr_reader_set_unit_policy(r, &v);
}

WASM_EXPORT
void *bvnr_wasm_policy_create(void)
{
	/* malloc + memset rather than calloc: the native test replaces this file's
	 * allocator to fail each allocation in turn, and only malloc/realloc are
	 * intercepted there. (A zeroed handle is the empty policy, which the
	 * library accepts and treats as inactive.) */
	wasm_policy_t *p = (wasm_policy_t *)malloc(sizeof *p);
	if (p) memset(p, 0, sizeof *p);
	return p;
}

WASM_EXPORT
void bvnr_wasm_policy_destroy(void *handle)
{
	wasm_policy_t *p = (wasm_policy_t *)handle;
	if (!p) return;
	for (uint32_t i = 0; i < p->nstrings; i++) free(p->strings[i]);
	free(p->strings);
	free(p->rules);
	free(p->targets);
	free(p->reqdim);
	free(p);
}

WASM_EXPORT
int bvnr_wasm_policy_require_unit(void *handle, int on)
{
	wasm_policy_t *p = (wasm_policy_t *)handle;
	if (!p) return 0;
	bool old = p->require_unit;
	p->require_unit = on != 0;
	if (!policy_try(p)) { p->require_unit = old; return 0; }
	return 1;
}

WASM_EXPORT
int bvnr_wasm_policy_target(void *handle, const char *unit, uint32_t base)
{
	wasm_policy_t *p = (wasm_policy_t *)handle;
	if (!p || !unit) return 0;
	uint32_t ns = p->nstrings, nt = p->ntargets;
	if (!wasm_grow((void **)&p->targets, &p->ctargets, nt + 1u, sizeof *p->targets))
		return 0;
	if (!wasm_grow((void **)&p->strings, &p->cstrings, ns + 1u, sizeof *p->strings))
		return 0;
	char *u = wasm_strdup(unit);
	if (!u) return 0;
	p->strings[p->nstrings++] = u;
	p->targets[nt].unit = u;
	p->targets[nt].base = base;
	p->ntargets = nt + 1u;
	if (!policy_try(p)) { policy_truncate(p, ns, p->nrules, nt, p->nreqdim); return 0; }
	return 1;
}

WASM_EXPORT
int bvnr_wasm_policy_require_dimension(void *handle, const char *unit)
{
	wasm_policy_t *p = (wasm_policy_t *)handle;
	if (!p || !unit) return 0;
	uint32_t ns = p->nstrings, nr = p->nreqdim;
	if (!wasm_grow((void **)&p->reqdim, &p->creqdim, nr + 1u, sizeof *p->reqdim))
		return 0;
	if (!wasm_grow((void **)&p->strings, &p->cstrings, ns + 1u, sizeof *p->strings))
		return 0;
	char *u = wasm_strdup(unit);
	if (!u) return 0;
	p->strings[p->nstrings++] = u;
	p->reqdim[nr] = u;
	p->nreqdim = nr + 1u;
	if (!policy_try(p)) { policy_truncate(p, ns, p->nrules, p->ntargets, nr); return 0; }
	return 1;
}

WASM_EXPORT
int bvnr_wasm_policy_rule(void *handle, const char *path, const char *unit,
			  uint32_t base, int mode)
{
	wasm_policy_t *p = (wasm_policy_t *)handle;
	if (!p || !path || !unit) return 0;
	if (mode != (int)bvnr_rule_convert && mode != (int)bvnr_rule_require) return 0;
	uint32_t ns = p->nstrings, nr = p->nrules;
	if (!wasm_grow((void **)&p->rules, &p->crules, nr + 1u, sizeof *p->rules))
		return 0;
	if (!wasm_grow((void **)&p->strings, &p->cstrings, ns + 2u, sizeof *p->strings))
		return 0;
	char *ps = wasm_strdup(path);
	if (!ps) return 0;
	p->strings[p->nstrings++] = ps;
	char *us = wasm_strdup(unit);
	if (!us) { policy_truncate(p, ns, nr, p->ntargets, p->nreqdim); return 0; }
	p->strings[p->nstrings++] = us;
	p->rules[nr].path = ps;
	p->rules[nr].unit = us;
	p->rules[nr].base = base;
	p->rules[nr].mode = (bvnr_unit_rule_mode_t)mode;
	p->nrules = nr + 1u;
	if (!policy_try(p)) { policy_truncate(p, ns, nr, p->ntargets, p->nreqdim); return 0; }
	return 1;
}

WASM_EXPORT
int bvnr_wasm_policy_normalise(void *handle, int normalise, uint32_t base,
			       int on_inexact)
{
	wasm_policy_t *p = (wasm_policy_t *)handle;
	if (!p) return 0;
	if (normalise != (int)bvnr_normalise_none &&
	    normalise != (int)bvnr_normalise_si) return 0;
	if (on_inexact != (int)bvnr_inexact_error &&
	    on_inexact != (int)bvnr_inexact_leave) return 0;
	if (base != 0u && !bvn_rational_base_valid(base)) return 0;
	int     on = p->normalise, oi = p->on_inexact;
	uint32_t ob = p->base;
	p->normalise = normalise;
	p->on_inexact = on_inexact;
	p->base = base;
	if (!policy_try(p)) {
		p->normalise = on; p->on_inexact = oi; p->base = ob;
		return 0;
	}
	return 1;
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
		/* NULL here is an allocation failure: an INT node always renders. The
		 * old fallback wrote `null` with ok:true, which reported a valid
		 * integer as JSON's null; make it the out-of-memory result instead. */
		if (!s) { b->oom = 1; break; }
		/* Integers wider than 64 bits cannot survive JSON's double-backed
		 * number type, so emit them as a JSON string (matching the CLI's
		 * `convert`); narrow ints stay as number tokens. */
		if (bvn_dom_get_bigint(node)) {
			sb_putc(b, '"'); sb_puts(b, s); sb_putc(b, '"');
		} else {
			sb_puts(b, s);
		}
		bvn_dom_free_string(s);
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

/* Where the (first) error is, in the shape validate() has always reported.
 * `byte` is the offending input BYTE's value, `offset` its 0-based index, and
 * `column` counts CHARACTERS, not bytes (bovnar.h spells out all three). */
static void emit_position(sb_t *b, uint64_t line, uint64_t column,
			  uint32_t byte, uint64_t offset)
{
	sb_printf(b, ",\"line\":%llu", (unsigned long long)line);
	sb_printf(b, ",\"column\":%llu", (unsigned long long)column);
	sb_printf(b, ",\"byte\":%u", byte);
	sb_printf(b, ",\"offset\":%llu", (unsigned long long)offset);
}

/* What resync cost, and what the document said it was. An on_error-only
 * consumer has no other way to learn that values went missing: the skipped
 * bytes were never parsed, so no callback ever mentions them
 * (bvnr_reader_get_skipped_bytes says exactly this). */
static void emit_reader_stats(sb_t *b, const bvnr_reader_t *r)
{
	if (r) {
		sb_printf(b, ",\"recovery_count\":%llu",
			  (unsigned long long)bvnr_reader_get_recovery_count(r));
		sb_printf(b, ",\"skipped_bytes\":%llu",
			  (unsigned long long)bvnr_reader_get_skipped_bytes(r));
	} else {
		sb_puts(b, ",\"recovery_count\":0,\"skipped_bytes\":0");
	}
}

static void emit_declared_version(sb_t *b, const bvnr_reader_t *r)
{
	uint16_t maj = 0, min = 0;
	if (r && bvnr_reader_get_declared_version(r, &maj, &min))
		sb_printf(b, ",\"declared_version\":\"%u.%u\"", maj, min);
	else
		sb_puts(b, ",\"declared_version\":null");
}

/* The same field for the surfaces that never hold a reader: the directive is
 * scanned directly (bvnr_peek_version) over the leading bytes, so toJSON() and
 * doc() report the version they were given for the same reason every reader
 * surface does. A malformed directive is null here; the parse error itself is
 * the DOM's to report. */
static void emit_peeked_version(sb_t *b, const char *buf, int len)
{
	uint16_t maj = 0, min = 0;
	if (bvnr_peek_version(buf, (uint64_t)(len > 0 ? len : 0), &maj, &min))
		sb_printf(b, ",\"declared_version\":\"%u.%u\"", maj, min);
	else
		sb_puts(b, ",\"declared_version\":null");
}

/* The common result for a call that could not even start: a unit policy or an
 * options block the wrapper handed over in a shape this build refuses, or a
 * reader that could not be created. The array fields say which of the payload
 * arrays the surface carries (parse() carries both), so the result keeps that
 * surface's shape -- a caller must not have to know that a refusal took a
 * different path to have r.events. */
static char *wasm_refusal(error_code_t err, const char *detail,
			  const char *array_field,
			  const char *array_field2, bool tier)
{
	sb_t b; sb_init(&b);
	sb_putc(&b, '{');
	emit_status(&b, err);
	if (detail) {
		sb_puts(&b, ",\"detail\":");
		sb_json_cstr(&b, detail);
	}
	if (tier) sb_puts(&b, ",\"tier\":null");
	emit_position(&b, 0, 0, 0, 0);
	sb_puts(&b, ",\"recovery_count\":0,\"skipped_bytes\":0,"
		    "\"declared_version\":null");
	const char *fields[2] = { array_field, array_field2 };
	for (int i = 0; i < 2; i++) {
		if (!fields[i]) continue;
		sb_puts(&b, ",\"");
		sb_puts(&b, fields[i]);
		sb_puts(&b, "\":[]");
	}
	sb_putc(&b, '}');
	return sb_finish(&b);
}


/* ================================================================== */
/* exported entry points                                              */
/* ================================================================== */

/*
 * The document tier's verdict on a buffer the streaming reader has accepted.
 *
 * Validity has two tiers (spec 12.4/12.5). The reader checks every value as it
 * passes; that an array's elements agree with one another, that the records of
 * an array share one shape and that no scope repeats a key are properties of
 * the whole document, and are checked over the materialised tree. `bovnar
 * validate` runs both. validate() and errors() here ran the first only -- so
 * `.a = 1; .a = 2;` and `.a = [1, "x"];` came back {"ok":true} from the module
 * whose README says it reports what the CLI reports, while toJSON() and doc()
 * in the SAME module refused them.
 */
static error_code_t wasm_document_tier(const char *buf, int len, bool *internal)
{
	if (internal) *internal = false;
	bvn_dom_doc_t *doc = bvn_dom_parse(buf, (uint32_t)len);
	if (!doc) {
		/* Not a finding about the document: the DOM could not be built. The
		 * caller must not label this a document-tier error. */
		if (internal) *internal = true;
		return error_invalid_argument;
	}
	error_code_t err = bvn_dom_doc_get_parse_error(doc);
	bvn_dom_doc_destroy(doc);
	return err;
}

/* The DOM under an optional unit policy. bvn_dom_parse_policy is the only DOM
 * entry point that takes one; with no policy the plain form is exactly the same
 * parse, and its error handling stays in one place. A refused policy comes back
 * as the doc's parse error (error_invalid_argument), which is what the header
 * promises. */
static bvn_dom_doc_t *dom_parse(const char *buf, int len,
				const wasm_policy_t *policy)
{
	if (!policy) return bvn_dom_parse(buf, (uint32_t)len);
	bvnr_unit_policy_t v;
	policy_view(policy, &v);
	return bvn_dom_parse_policy(buf, (uint32_t)len, &v);
}

/* The first node in the subtree that is a datetime written with sub-second
 * digits, or NULL. Mirrors find_fractional_datetime in src/bovnar.c. */
static const bvn_dom_node_t *json_fractional_datetime(const bvn_dom_node_t *node)
{
	if (!node) return NULL;
	switch (bvn_dom_node_type(node)) {
	case BVN_DOM_INT:
		return bvn_dom_get_datetime_fraction(node, NULL) ? node : NULL;
	case BVN_DOM_STRUCT: {
		uint32_t cnt = bvn_dom_struct_count(node);
		const bvn_dom_entry_t *e = bvn_dom_struct_entries(node);
		for (uint32_t i = 0; e && i < cnt; i++) {
			const bvn_dom_node_t *hit = json_fractional_datetime(e[i].value);
			if (hit) return hit;
		}
		return NULL;
	}
	case BVN_DOM_ARRAY: {
		uint32_t cnt = bvn_dom_array_count(node);
		for (uint32_t i = 0; i < cnt; i++) {
			const bvn_dom_node_t *hit =
				json_fractional_datetime(bvn_dom_array_at(node, i));
			if (hit) return hit;
		}
		return NULL;
	}
	default:
		return NULL;
	}
}

static char *to_json_impl(const char *buf, int len,
			  const wasm_policy_t *policy)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;
	bvn_dom_doc_t *doc = dom_parse(buf, len, policy);
	error_code_t err = doc ? bvn_dom_doc_get_parse_error(doc) : error_invalid_argument;

	/* A datetime written with a fractional second has only its whole-second
	 * carrier to offer JSON, so projecting it CHANGES the value: half past
	 * midnight came out as midnight, with ok:true. `bovnar convert` refuses
	 * that document rather than truncate it, and the README says this
	 * projection matches convert; it refuses too. There is no error code for
	 * "valid, but not expressible here", so it is value_out_of_range with a
	 * `detail` that says what and where. events() still delivers the digits
	 * (`frac`). */
	const char *frac_key = NULL;
	if (doc && err == error_none) {
		const bvn_dom_entry_t *e = bvn_dom_doc_entries(doc);
		uint32_t cnt = bvn_dom_doc_count(doc);
		for (uint32_t i = 0; e && i < cnt && !frac_key; i++)
			if (json_fractional_datetime(e[i].value))
				frac_key = e[i].key ? e[i].key : "";
		if (frac_key) err = error_value_out_of_range;
	}

	sb_putc(&b, '{');
	emit_status(&b, err);
	if (frac_key) {
		sb_puts(&b, ",\"detail\":\"a datetime with sub-second digits cannot "
		            "be represented in JSON without dropping the fraction "
		            "(under top-level key '");
		sb_json_str_body(&b, frac_key, strlen(frac_key));
		sb_puts(&b, "')\"");
	}
	emit_peeked_version(&b, buf, len);
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
char *bvnr_wasm_to_json(const char *buf, int len)
{
	return to_json_impl(buf, len, NULL);
}

/* The DOM projected under a unit policy -- the shape `bovnar convert` takes
 * when it is given --unit/--field/--si. Read flags do not cross here: the DOM
 * entry points take a policy, not a bvnr_read_flags_t, so the wrapper refuses
 * strictVersion/textOnly/caps on these two surfaces rather than accepting them
 * and doing nothing.
 *
 * THREE words, not the reader _ex twins' four: (buf, len, policy). There is no
 * opts word to pass, and inventing an ignored one would invite exactly the
 * arity mistake this comment exists to prevent. */
WASM_EXPORT
char *bvnr_wasm_to_json_ex(const char *buf, int len, uintptr_t policy)
{
	return to_json_impl(buf, len, (const wasm_policy_t *)policy);
}

static char *validate_impl(const char *buf, int len,
			   const bvnr_wasm_opts_t *opts,
			   const wasm_policy_t *policy)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	bvnr_read_flags_t flags;
	if (!wasm_read_flags(&flags, opts))
		return wasm_refusal(error_invalid_argument,
				    "unrecognized reader options", NULL, NULL, true);
	flags.continue_on_error = false;

	bvnr_reader_t *r = bvnr_reader_create();
	if (!r)
		/* Not a document error and not a position: report it as the
		 * internal failure it is, with tier null rather than the "stream"
		 * a caller might place a caret at. */
		return wasm_refusal(error_invalid_argument, "out of memory", NULL, NULL, true);
	if (!policy_install(policy, r)) {
		bvnr_reader_destroy(r);
		return wasm_refusal(error_invalid_argument,
				    "the unit policy was refused", NULL, NULL, true);
	}
	/* Match every other front end: the CLI (src/bovnar.c), the Python bindings
	 * and the DOM builder all raise BOTH of these to 255. Left at the library
	 * default of 64, the browser rejected a document the same bvnr_read()
	 * accepts natively -- and this very binary's DOM path already allows 255, so
	 * the playground disagreed with itself as well as with the CLI. The page
	 * states that the playground runs the reference reader and delivers the same
	 * events; that has to hold at the limits too.
	 *
	 * The array half of that was missed when the struct half was fixed, and it
	 * behaved exactly as described above: 65 nested arrays parsed natively and
	 * raised error_array_nesting_too_high in the browser, while doc()/toJSON()
	 * -- which reach the reader through bvn_dom_parse, where the DOM builder
	 * sets its own 255 -- accepted the same document in the same module.
	 * (Both limits now come from wasm_read_flags, whose default is 255.) */

	bool opened = r && bvnr_open_read_mem(r, buf, (uint64_t)len, NULL, 0, &flags);
	if (opened) bvnr_read(r);

	error_code_t err = r ? bvnr_reader_get_error(r)
	                     : error_invalid_argument;
	if (!opened && err == error_none) err = error_invalid_argument;

	/* The second tier, exactly where cmd_validate runs it: only once the
	 * streaming pass is clean. Its errors are properties of the whole document
	 * and have no position -- line, column, byte and offset stay 0 -- so `tier`
	 * says which kind this is; a caller placing a caret must not put one at
	 * 0:0. (See wasm_document_tier.) It reads the document as WRITTEN, without
	 * the policy, as the CLI's second pass does: array homogeneity and duplicate
	 * keys are properties of the document, not of the unit the consumer asked
	 * its values to be delivered in. */
	const char *tier = (err == error_none) ? NULL : "stream";
	const char *detail = NULL;
	if (opened && err == error_none) {
		bool internal = false;
		err = wasm_document_tier(buf, len, &internal);
		if (err != error_none) {
			if (internal) detail = "out of memory";
			else          tier = "document";
		}
	}

	sb_putc(&b, '{');
	emit_status(&b, err);
	if (detail) {
		sb_puts(&b, ",\"detail\":");
		sb_json_cstr(&b, detail);
	}
	sb_puts(&b, ",\"tier\":");
	if (tier) sb_json_cstr(&b, tier);
	else      sb_puts(&b, "null");
	if (r) {
		emit_position(&b, bvnr_reader_get_error_line(r),
			      bvnr_reader_get_error_column(r),
			      bvnr_reader_get_error_byte(r),
			      bvnr_reader_get_error_offset(r));
	} else {
		emit_position(&b, 0, 0, 0, 0);
	}
	emit_reader_stats(&b, r);
	emit_declared_version(&b, r);
	sb_putc(&b, '}');

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

WASM_EXPORT
char *bvnr_wasm_validate(const char *buf, int len)
{
	return validate_impl(buf, len, NULL, NULL);
}

/* Reader options + unit policy. A separate export rather than a changed
 * signature: the wrapper and the core are separate URLs with separate cache
 * entries, so a current wrapper can be paired with a stale core. The wrapper
 * feature-tests this symbol and refuses options when it is absent, instead of
 * accepting them and doing nothing. */
WASM_EXPORT
char *bvnr_wasm_validate_ex(const char *buf, int len, uintptr_t opts,
			    uintptr_t policy)
{
	return validate_impl(buf, len, (const bvnr_wasm_opts_t *)opts,
			     (const wasm_policy_t *)policy);
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
	/* BVNR_WASM_OPT_OCTETS_HEX: carry each octet chunk's payload as `hex`. */
	bool     octet_hex;
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
	/* Recovered errors. events_impl runs in resync mode, and a document that
	 * resyncs and then reaches EOF cleanly has the reader's final code CLEARED
	 * -- so deriving `ok` from that code alone reported success for a document
	 * whose failing assignment had been skipped. bvnr_wasm_parse hit exactly
	 * this and computes `ok` from an error COUNT instead; the comment there
	 * spells out why. This is the same fix for the events path, which is where
	 * it bites hardest: eventsConvert('.v = <float:64,k~m/h> 100.0;', 'm/s')
	 * reported {"ok":true} with the data event simply ABSENT, because 250/9
	 * does not terminate and the value was dropped. A well-formed stream
	 * carrying a key and no value, and nothing saying so. */
	uint32_t            err_count;
	error_code_t        first_err;
	/* Position of the first error, captured in the callback: after a clean EOF
	 * the reader's own position getters are cleared along with its code, so the
	 * surface cannot ask for it afterwards. */
	uint64_t            first_line, first_column, first_offset;
	uint32_t            first_byte;
} events_ud_t;

static void events_err_cb(void *ud, error_code_t err,
                          uint64_t line, uint64_t column,
                          uint32_t byte, uint64_t offset)
{
	events_ud_t *u = (events_ud_t *)ud;
	if (u->err_count == 0u) {
		u->first_err    = err;
		u->first_line   = line;
		u->first_column = column;
		u->first_byte   = byte;
		u->first_offset = offset;
	}
	u->err_count++;
}

/* A bvn_int_t as a JSON decimal string. The rational can exceed 2^53 by a wide
 * margin, so a number token would lose digits at JSON.parse; a string keeps
 * every one, as the Python binding's converted_rational() does. */
static void sb_json_bigint(sb_t *b, const char *name,
			   const struct bvn_int_s *n)
{
	int bits = bvn_int_bitlen(n);
	if (bits < 0) bits = 0;
	size_t cap = bvn_int_str_bufsize((uint32_t)bits, 10);
	/* realloc(NULL, ...) rather than malloc: the native exhaustion sweep
	 * replaces this file's realloc to fail each buffer growth in turn, and
	 * this scratch is a buffer growth like any other. */
	char *tmp = (char *)realloc(NULL, cap);
	if (!tmp) { b->oom = 1; return; }
	int32_t len = bvn_int_to_str(n, tmp, cap, 10);
	if (len < 0) { free(tmp); b->oom = 1; return; }
	sb_puts(b, ",\"");
	sb_puts(b, name);
	sb_puts(b, "\":\"");
	sb_put(b, tmp, (size_t)len);
	sb_putc(b, '"');
	free(tmp);
}


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

	/* textual payload (skip raw octet bytes; report their length instead).
	 * `length > 0` is not enough on its own: an EMPTY STRING is a value with a
	 * zero-byte payload, and omitting `text` for it made the event for
	 * `.s = ""` indistinguishable from a token that carries no text at all (a
	 * null literal, whose data is the library's shared empty sentinel). The
	 * string types are therefore emitted even at zero length; every other
	 * textless event (stream boundaries, width/base/q parameters, a null) has
	 * no text field. */
	if (d->type == token_is_octet_stream) {
		sb_printf(b, ",\"bytes\":%u", d->length);
		if (ctx->octet_hex && d->data && d->length) {
			const uint8_t *p = (const uint8_t *)d->data;
			sb_puts(b, ",\"hex\":\"");
			for (uint32_t k = 0; k < d->length; k++) {
				sb_putc(b, hexd[p[k] >> 4]);
				sb_putc(b, hexd[p[k] & 0xf]);
			}
			sb_putc(b, '"');
		}
	} else if (d->length ||
		   d->type == token_is_string ||
		   d->type == token_is_array_string) {
		sb_puts(b, ",\"text\":\"");
		sb_json_str_body(b, (const char *)d->data, d->length);
		sb_putc(b, '"');
	}

	/* type/unit metadata, where meaningful. A parameter token is excluded:
	 * its job is the ONE parameter it carries (width, base, q, a unit), and
	 * stamping the whole resolved type beside it made a float_fix's q read as
	 * "base" and a datetime's epoch parameter look like the value's base. */
	if (d->value_type.family != vt_plain &&
	    d->type != token_is_type_width &&
	    d->type != token_is_type_base &&
	    d->type != token_is_type_q &&
	    d->type != token_is_unit) {
		sb_puts(b, ",\"family\":");
		sb_json_cstr(b, evt_family(d->value_type.family));
		sb_printf(b, ",\"width\":%u", d->value_type.width);
		sb_printf(b, ",\"base\":%u", d->value_type.base);
		char unit_buf[BVNR_UNIT_STRING_MAX];
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
	/* numeric value of a type-annotation parameter (carried in value_type, not
	 * in d->data) so the shim can rebuild the annotation. q is float_fix's
	 * fixed-point scaling, kept in the same slot as base and emitted under its
	 * own name: `q8` reported as "base 8" is a radix the value does not have. */
	if (d->type == token_is_type_width)
		sb_printf(b, ",\"width\":%u", d->value_type.width);
	else if (d->type == token_is_type_base)
		sb_printf(b, ",\"base\":%u", d->value_type.base);
	else if (d->type == token_is_type_q)
		sb_printf(b, ",\"q\":%u", d->value_type.base);
	if (d->frac_data && d->frac_length) {
		sb_puts(b, ",\"frac\":\"");
		sb_json_str_body(b, (const char *)d->frac_data, d->frac_length);
		sb_putc(b, '"');
	}
	/* library 2.0 — the unit as the document spelled it, beside the canonical
	 * "unit" above: "ucum:mL{total}" where "unit" says "m~L". */
	if (d->unit_source && d->unit_source_length) {
		sb_puts(b, ",\"unit_source\":\"");
		sb_json_str_body(b, (const char *)d->unit_source,
				 d->unit_source_length);
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
			/* The exact result has no terminating expansion in the output
			 * base (want_unit_allow_nonterminating). The rational is the only
			 * carrier of the value: without it the event said
			 * "converted:null" and the caller had nothing to use, which is
			 * exactly the dead end the Python binding closed. */
			sb_puts(b, ",\"converted\":null");
			if (d->conv.num && d->conv.den) {
				sb_json_bigint(b, "converted_num", d->conv.num);
				sb_json_bigint(b, "converted_den", d->conv.den);
			}
		}
		sb_printf(b, ",\"converted_base\":%u", d->conv.base);
		char ub[BVNR_UNIT_STRING_MAX];
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
			 bool convert, bool allow_nonterminating,
			 const bvnr_wasm_opts_t *opts,
			 const wasm_policy_t *policy)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	sb_t evbuf; sb_init(&evbuf);
	events_ud_t ud;
	memset(&ud, 0, sizeof ud);
	ud.ev.b    = &evbuf;
	ud.ev.seq  = 0;
	ud.ev.count = 0;
	ud.ev.octet_hex = opts && (opts->flags & BVNR_WASM_OPT_OCTETS_HEX);
	ud.unit    = target;
	ud.base    = base;
	ud.first_err = error_none;

	bvnr_read_flags_t flags;
	if (!wasm_read_flags(&flags, opts))
		return wasm_refusal(error_invalid_argument,
				    "unrecognized reader options", "events", NULL, false);

	bvnr_reader_t *r = bvnr_reader_create();
	if (r && !policy_install(policy, r)) {
		bvnr_reader_destroy(r);
		return wasm_refusal(error_invalid_argument,
				    "the unit policy was refused", "events", NULL, false);
	}
	flags.userdata = &ud;
	flags.on_verified = evt_cb;
	flags.on_error = events_err_cb;
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

	/* `ok` from the error COUNT, not the final code -- see events_ud_t. The
	 * reported code is the FIRST error whenever on_error saw one, so a caller
	 * learns what went wrong first rather than reading "none" beside a missing
	 * value.
	 *
	 * Whenever, not only "when the final code has been cleared": that was the
	 * condition, and it reported the LAST error for any document whose final
	 * one was fatal. A string holding a bad byte and never closed came back as
	 * incomplete_bvnr_stream here while errors()[0] and validate() both said
	 * invalid_utf8_byte -- three surfaces documented to name the same, first,
	 * error. */
	if (ud.err_count > 0u)
		err = ud.first_err;
	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s,",
	          (err == error_none && ud.err_count == 0u) ? "true" : "false");
	sb_printf(&b, "\"error\":%d,", (int)err);
	sb_puts(&b, "\"error_name\":");
	sb_json_cstr(&b, bvn_error_to_string(err));
	/* The first error's position: the errors array carries every position for
	 * errors()/parse(), but events() has no such array, so without these a
	 * caller that only reads the stream could not point at the failure at all
	 * while validate() on the same document could. */
	emit_position(&b, ud.first_line, ud.first_column,
		      ud.first_byte, ud.first_offset);
	emit_reader_stats(&b, r);
	emit_declared_version(&b, r);
	sb_puts(&b, ",\"events\":[");
	sb_splice(&b, &evbuf);
	sb_puts(&b, "]}");

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

WASM_EXPORT
char *bvnr_wasm_events(const char *buf, int len)
{
	return events_impl(buf, len, NULL, 0u, false, false, NULL, NULL);
}

WASM_EXPORT
char *bvnr_wasm_events_ex(const char *buf, int len, uintptr_t opts,
			  uintptr_t policy)
{
	return events_impl(buf, len, NULL, 0u, false, false,
			   (const bvnr_wasm_opts_t *)opts,
			   (const wasm_policy_t *)policy);
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
 * failing the parse — and then carries the exact rational as
 * `converted_num`/`converted_den` decimal strings, the only carriers the value
 * has in that mode.
 *
 * An unparseable `unit` is reported as error_unit_illegal rather than silently
 * falling back to no conversion; an unusable `base` (anything but 0 or a base
 * bvnr can write) is error_invalid_argument BEFORE the document is read, so a
 * document with no numeric value cannot pass a base the reader would have
 * refused on the first one.
 *
 * Note the interaction with this surface's resync policy: because
 * continue_on_error is on, a value the conversion rejects — dimensionally
 * incompatible with `unit`, or inexact — is skipped along with its assignment
 * rather than aborting, so it does not appear in `events`. The status DOES say
 * so: `ok` is false and `error`/`error_name` carry the rejection
 * (unit_mismatch, unit_inexact), because events_impl counts recovered errors
 * rather than reading the reader's cleared final code. Reporting `ok` over a
 * value it had dropped is what that count replaced.
 */
static char *events_convert_impl(const char *buf, int len, const char *unit,
				 int base, int allow_nonterminating,
				 const bvnr_wasm_opts_t *opts,
				 const wasm_policy_t *policy)
{
	value_unit_t        target;
	const value_unit_t *tp = NULL;

	/* 0 keeps each value's own base; a negative one is no base at all, and
	 * used to be read as 0 -- a silent "no conversion" reported as ok. The
	 * rest of the domain is bvnr's own: 2..62, 64 and 85. */
	if (base < 0 || (base != 0 && !bvn_rational_base_valid((uint32_t)base)))
		return wasm_refusal(error_invalid_argument,
				    "unusable output base", "events", NULL, false);
	if (unit && unit[0]) {
		bool ok = false;
		target = bvn_parse_unit((const uint8_t *)unit, &ok);
		if (!ok)
			return wasm_refusal(error_unit_illegal,
					    "the conversion target is not a unit",
					    "events", NULL, false);
		tp = &target;
	}
	return events_impl(buf, len, tp, (uint32_t)base,
			   true, allow_nonterminating != 0, opts, policy);
}

WASM_EXPORT
char *bvnr_wasm_events_convert(const char *buf, int len, const char *unit,
			       int base, int allow_nonterminating)
{
	return events_convert_impl(buf, len, unit, base, allow_nonterminating,
				   NULL, NULL);
}

WASM_EXPORT
char *bvnr_wasm_events_convert_ex(const char *buf, int len, const char *unit,
				  int base, int allow_nonterminating,
				  uintptr_t opts, uintptr_t policy)
{
	return events_convert_impl(buf, len, unit, base, allow_nonterminating,
				   (const bvnr_wasm_opts_t *)opts,
				   (const wasm_policy_t *)policy);
}

/* ------- all errors (resync mode) ------- */
typedef struct {
	sb_t        *b;
	int          count;
	/* First recovered code, for the same reason events_ud_t carries one: under
	 * continue_on_error a document that resyncs and then reaches EOF cleanly
	 * has the reader's final code CLEARED, so a surface reporting that code
	 * alone says "none" beside its own non-empty errors array. */
	error_code_t first;
	/* The first error's position, for the top-level fields. errors[0] already
	 * carries it, but every other surface reports it beside the code. */
	uint64_t     first_line, first_column, first_offset;
	uint32_t     first_byte;
} err_ctx_t;

static void err_collect(void *ud, error_code_t err,
                        uint64_t line, uint64_t column,
                        uint32_t byte, uint64_t offset)
{
	err_ctx_t *ctx = (err_ctx_t *)ud;
	sb_t *b = ctx->b;
	if (ctx->count == 0) {
		ctx->first        = err;
		ctx->first_line   = line;
		ctx->first_column = column;
		ctx->first_byte   = byte;
		ctx->first_offset = offset;
	}
	if (ctx->count++) sb_putc(b, ',');
	sb_putc(b, '{');
	sb_printf(b, "\"error\":%d,", (int)err);
	sb_puts(b, "\"error_name\":");
	sb_json_cstr(b, bvn_error_to_string(err));
	sb_printf(b, ",\"line\":%llu", (unsigned long long)line);
	sb_printf(b, ",\"column\":%llu", (unsigned long long)column);
	sb_printf(b, ",\"byte\":%u", byte);
	sb_printf(b, ",\"offset\":%llu", (unsigned long long)offset);
	sb_puts(b, ",\"tier\":\"stream\"");
	sb_putc(b, '}');
}

/* Validate in resync mode, collecting EVERY error (not just the first) the way
 * the playground's on_error panel expects. Returns
 * { ok, error, error_name, line, column, byte, offset, recovery_count,
 *   skipped_bytes, declared_version,
 *   errors:[{error,error_name,line,column,byte,offset,tier}] }.
 * `error`/`error_name` name the FIRST error, as on every other surface; this
 * one used to carry no top-level code at all, so a caller doing
 * `if (!r.ok) show(r.error_name)` read undefined. `tier` is "stream" for an
 * error the reader raised at a position, "document" for a whole-document rule
 * (no position; see below). */
static char *errors_impl(const char *buf, int len,
			 const bvnr_wasm_opts_t *opts,
			 const wasm_policy_t *policy)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	sb_t ebuf; sb_init(&ebuf);
	err_ctx_t ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.b = &ebuf;
	ctx.first = error_none;

	bvnr_read_flags_t flags;
	if (!wasm_read_flags(&flags, opts))
		return wasm_refusal(error_invalid_argument,
				    "unrecognized reader options", "errors", NULL, false);

	bvnr_reader_t *r = bvnr_reader_create();
	if (r && !policy_install(policy, r)) {
		bvnr_reader_destroy(r);
		return wasm_refusal(error_invalid_argument,
				    "the unit policy was refused", "errors", NULL, false);
	}
	flags.userdata          = &ctx;
	flags.on_error          = err_collect;
	flags.continue_on_error = true;   /* resync: report all errors, keep going */

	bool opened = r && bvnr_open_read_mem(r, buf, (uint64_t)len, NULL, 0, &flags);
	if (opened) bvnr_read(r);

	/* "EVERY error" includes the one the streaming pass cannot see. When that
	 * pass found nothing, ask the document tier, and report what it says as
	 * one more entry -- positionless, and marked as such. (Only then: the DOM
	 * does not resync, so over a document with stream errors it could only
	 * repeat the first of them.) Without this, errors() said {"ok":true,
	 * "errors":[]} for a duplicate key. */
	error_code_t internal_err = error_none;
	if (opened && ctx.count == 0) {
		bool internal = false;
		error_code_t derr = wasm_document_tier(buf, len, &internal);
		if (internal) {
			/* The DOM could not be built: an internal failure, not an entry
			 * about the document. The result is ok:false with an empty list. */
			internal_err = derr;
		} else if (derr != error_none) {
			ctx.first = derr;
			sb_printf(&ebuf, "{\"error\":%d,\"error_name\":", (int)derr);
			sb_json_cstr(&ebuf, bvn_error_to_string(derr));
			sb_puts(&ebuf, ",\"line\":0,\"column\":0,\"byte\":0,"
			               "\"offset\":0,\"tier\":\"document\"}");
			ctx.count++;
		}
	}

	error_code_t err = ctx.count > 0 ? ctx.first
	                                 : (internal_err != error_none ? internal_err
	                                    : (r ? bvnr_reader_get_error(r)
	                                         : error_invalid_argument));
	if (!opened && err == error_none) err = error_invalid_argument;

	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s,",
		  (err == error_none && ctx.count == 0) ? "true" : "false");
	sb_printf(&b, "\"error\":%d,", (int)err);
	sb_puts(&b, "\"error_name\":");
	sb_json_cstr(&b, bvn_error_to_string(err));
	emit_position(&b, ctx.first_line, ctx.first_column,
		      ctx.first_byte, ctx.first_offset);
	emit_reader_stats(&b, r);
	emit_declared_version(&b, r);
	sb_puts(&b, ",\"errors\":[");
	sb_splice(&b, &ebuf);
	sb_puts(&b, "]}");

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

WASM_EXPORT
char *bvnr_wasm_errors(const char *buf, int len)
{
	return errors_impl(buf, len, NULL, NULL);
}

WASM_EXPORT
char *bvnr_wasm_errors_ex(const char *buf, int len, uintptr_t opts,
			  uintptr_t policy)
{
	return errors_impl(buf, len, (const bvnr_wasm_opts_t *)opts,
			   (const wasm_policy_t *)policy);
}

/* ------- combined single-pass parse (events + errors) ------- */
/*
 * The playground needs BOTH the verified event stream and the full error list
 * for the same document. Doing that with bvnr_wasm_events + bvnr_wasm_errors
 * meant constructing two readers, walking the document twice, and serialising
 * then re-parsing two JSON payloads in JS -- for a 1000-assignment document the
 * event payload alone is hundreds of KB, on every debounce tick.
 *
 * Both of those surfaces already run with continue_on_error = true, so a single
 * reader carrying both callbacks produces exactly the same two streams in one
 * pass. The only thing in the way was bvnr_read_flags_t::userdata being a single
 * pointer; the shim struct below lets both callbacks keep their own context.
 *
 * Returns { ok, error, error_name, events:[...], errors:[...], declared_version }
 * -- the union of what the two separate exports return. They are kept for
 * compatibility (and because errors alone is genuinely cheaper when the caller
 * only validates).
 */
typedef struct {
	events_ud_t ev;
	err_ctx_t   er;
} both_ud_t;

static bool both_evt_cb(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	return evt_cb(&((both_ud_t *)ud)->ev, e, d);
}

static void both_err_cb(void *ud, error_code_t err,
                        uint64_t line, uint64_t column,
                        uint32_t byte, uint64_t offset)
{
	err_collect(&((both_ud_t *)ud)->er, err, line, column, byte, offset);
}

static char *parse_impl(const char *buf, int len,
			const bvnr_wasm_opts_t *opts,
			const wasm_policy_t *policy)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;

	sb_t evbuf; sb_init(&evbuf);
	sb_t ebuf;  sb_init(&ebuf);
	both_ud_t ud;
	memset(&ud, 0, sizeof(ud));
	ud.ev.ev.b = &evbuf;
	ud.ev.ev.octet_hex = opts && (opts->flags & BVNR_WASM_OPT_OCTETS_HEX);
	ud.ev.unit = NULL;
	ud.ev.base = 0u;
	ud.er.b     = &ebuf;
	ud.er.count = 0;
	ud.er.first = error_none;

	bvnr_read_flags_t flags;
	if (!wasm_read_flags(&flags, opts))
		return wasm_refusal(error_invalid_argument,
				    "unrecognized reader options", "events", "errors", false);

	bvnr_reader_t *r = bvnr_reader_create();
	if (r && !policy_install(policy, r)) {
		bvnr_reader_destroy(r);
		return wasm_refusal(error_invalid_argument,
				    "the unit policy was refused", "events", "errors", false);
	}
	flags.userdata          = &ud;
	flags.on_verified       = both_evt_cb;
	flags.on_error          = both_err_cb;
	flags.continue_on_error = true;   /* resync: all events AND all errors */

	bool opened = r && bvnr_open_read_mem(r, buf, (uint64_t)len, NULL, 0, &flags);
	if (opened) bvnr_read(r);

	error_code_t err = r ? bvnr_reader_get_error(r) : error_invalid_argument;
	if (!opened && err == error_none) err = error_invalid_argument;

	/* `ok` here means "nothing went wrong", which for a payload that also
	 * carries an errors array must account for RECOVERED errors. emit_status
	 * derives ok from the reader's final error code, and under
	 * continue_on_error a document that resyncs and then reaches EOF cleanly
	 * has that code cleared (bovnar_lexer.c clears last_error on the clean-EOF
	 * break; tests/bovnar_reader_test.c asserts it). Using emit_status verbatim
	 * therefore produced {"ok":true, ..., "errors":[...3 entries...]} -- self-
	 * contradictory, and disagreeing with bvnr_wasm_errors, which computes ok
	 * from its error COUNT. Match that instead.
	 *
	 * "error"/"error_name" need the same treatment, and getting only the `ok`
	 * half left this export saying {"ok":false,"error":0,"error_name":"none"}
	 * -- a caller doing `if (!r.ok) show(r.error_name)` was told "none" went
	 * wrong. events_impl reports the FIRST recovered code when the final one
	 * has been cleared; this is the export index.mjs tells callers to PREFER
	 * over events()+errors(), and the one the playground shim actually uses, so
	 * it disagreed with the surface it replaced on the same document: events()
	 * said unexpected_input_byte, parse() said none. Same fallback here. */
	/* ...and, as there, the first error whenever there was one -- not only
	 * when the final code happens to have been cleared. */
	if (ud.er.count > 0)
		err = ud.er.first;
	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s,",
	          (err == error_none && ud.er.count == 0) ? "true" : "false");
	sb_printf(&b, "\"error\":%d,", (int)err);
	sb_puts(&b, "\"error_name\":");
	sb_json_cstr(&b, bvn_error_to_string(err));
	emit_position(&b, ud.er.first_line, ud.er.first_column,
		      ud.er.first_byte, ud.er.first_offset);
	emit_reader_stats(&b, r);
	emit_declared_version(&b, r);
	sb_puts(&b, ",\"events\":[");
	sb_splice(&b, &evbuf);
	sb_puts(&b, "],\"errors\":[");
	sb_splice(&b, &ebuf);
	sb_putc(&b, ']');
	sb_putc(&b, '}');

	if (r) bvnr_reader_destroy(r);
	return sb_finish(&b);
}

WASM_EXPORT
char *bvnr_wasm_parse(const char *buf, int len)
{
	return parse_impl(buf, len, NULL, NULL);
}

/* Events + errors under read options and/or a unit policy. The wrapper prefers
 * this export whenever it has one of the two to pass. */
WASM_EXPORT
char *bvnr_wasm_parse_ex(const char *buf, int len, uintptr_t opts,
			 uintptr_t policy)
{
	return parse_impl(buf, len, (const bvnr_wasm_opts_t *)opts,
			  (const wasm_policy_t *)policy);
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
	char ub[BVNR_UNIT_STRING_MAX];
	int32_t ul = bvn_dom_get_unit_string(node, ub, sizeof ub);
	if (ul > 0 && strcmp(ub, "no_unit") != 0) {
		sb_puts(b, ",\"unit\":");
		sb_json_cstr(b, ub);
	} else {
		sb_puts(b, ",\"unit\":null");
	}
	/* The spelling the document used, where it says something the canonical
	 * form does not: a unit profile ("ucum:mm[Hg]"), a UCUM annotation
	 * ("ucum:mL{total}"), or any non-canonical spelling. bvn_dom_get_unit_source
	 * returns NULL exactly when the canonical text IS the spelling, and for a
	 * value a unit policy converted (the document's spelling describes a unit
	 * the node no longer has). */
	uint32_t usrc_len = 0;
	const char *usrc = bvn_dom_get_unit_source(node, &usrc_len);
	if (usrc) {
		sb_puts(b, ",\"unitSource\":\"");
		sb_json_str_body(b, usrc, usrc_len);
		sb_putc(b, '"');
	} else {
		sb_puts(b, ",\"unitSource\":null");
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
		/* NULL is an allocation failure; the old fallback said "0" with a null
		 * value, a wrong number rather than an honest exhaustion. */
		if (!s) { b->oom = 1; break; }
		bool big = bvn_dom_get_bigint(node) != NULL;
		value_type_spec_t vt = bvn_dom_get_value_type(node);
		sb_puts(b, "{\"type\":\"scalar\",\"kind\":");
		sb_json_cstr(b, vt.family == vt_datetime ? "datetime" : "number");
		sb_puts(b, ",\"text\":");
		sb_json_cstr(b, s);
		sb_puts(b, ",\"value\":");
		if (big) { sb_putc(b, '"'); sb_puts(b, s); sb_putc(b, '"'); }
		else     sb_puts(b, s);
		bvn_dom_free_string(s);
		/* A datetime written with sub-second digits keeps them here: the node's
		 * integer is the whole-second epoch count, and the fraction has nowhere
		 * else to live. toJSON() refuses such a document rather than truncate
		 * it; doc() and events() deliver the digits. */
		uint32_t flen = 0;
		const char *fr = bvn_dom_get_datetime_fraction(node, &flen);
		sb_puts(b, ",\"frac\":");
		if (fr) {
			sb_putc(b, '"');
			sb_json_str_body(b, fr, flen);
			sb_putc(b, '"');
		} else {
			sb_puts(b, "null");
		}
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
 * parseBovnar's event stream. Returns
 * { ok, error, error_name, tree:[{key,value}...] }. */
static char *doc_impl(const char *buf, int len, const wasm_policy_t *policy)
{
	sb_t b; sb_init(&b);
	if (len < 0) len = 0;
	bvn_dom_doc_t *doc = dom_parse(buf, len, policy);
	error_code_t err = doc ? bvn_dom_doc_get_parse_error(doc)
	                       : error_invalid_argument;
	/* The same {ok, error, error_name} preamble as every other result. It was
	 * a bare "ok": a caller told false had no way to learn why, short of
	 * parsing the document again through another entry point. */
	sb_putc(&b, '{');
	emit_status(&b, err);
	emit_peeked_version(&b, buf, len);
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
char *bvnr_wasm_doc(const char *buf, int len)
{
	return doc_impl(buf, len, NULL);
}

/* Three words, as bvnr_wasm_to_json_ex: the DOM takes a policy only. */
WASM_EXPORT
char *bvnr_wasm_doc_ex(const char *buf, int len, uintptr_t policy)
{
	return doc_impl(buf, len, (const wasm_policy_t *)policy);
}

WASM_EXPORT
const char *bvnr_wasm_version(void)
{
	return bvnr_version_string();
}

/* The other two version questions the C API answers and the module did not:
 * the numeric library version (bvnr_version, monotonic and safe to compare) and
 * the highest SPEC version this build understands (bvnr_spec_version). The
 * string above is the library's; a document's own declared version is separate
 * again (declared_version / peekVersion). */
WASM_EXPORT
uint32_t bvnr_wasm_version_number(void)
{
	return bvnr_version();
}

/* Packed (major << 16) | minor: both parts are uint16_t, so the whole answer
 * fits a JS number exactly and no out-parameter allocation is needed. */
WASM_EXPORT
uint32_t bvnr_wasm_spec_version(void)
{
	uint16_t maj = 0, min = 0;
	bvnr_spec_version(&maj, &min);
	return ((uint32_t)maj << 16) | (uint32_t)min;
}

/* A document's leading "#!bovnar <major>.<minor>" directive without a full
 * parse. Same packing as bvnr_wasm_spec_version; *found (may be NULL) says
 * whether there was a well-formed directive. The out-parameter exists rather
 * than an in-band sentinel because the packed form can hold every value a
 * directive can declare -- 65535.65535 is 0xFFFFFFFF, and a sentinel would
 * make that document indistinguishable from one with no directive. */
WASM_EXPORT
uint32_t bvnr_wasm_peek_version(const char *buf, int len, int *found)
{
	if (found) *found = 0;
	if (len < 0) len = 0;
	uint16_t maj = 0, min = 0;
	if (!bvnr_peek_version(buf, (uint64_t)len, &maj, &min))
		return 0;
	if (found) *found = 1;
	return ((uint32_t)maj << 16) | (uint32_t)min;
}

/* bvn_error_to_string, so a caller holding the NUMERIC code in a result can
 * name it. The table is the library's; an unknown code answers "unknown_error",
 * exactly as the C function does. */
WASM_EXPORT
const char *bvnr_wasm_error_name(int code)
{
	return bvn_error_to_string((error_code_t)code);
}

/* Parse a unit string: { ok, unit, error, error_name, profile_only }.
 *
 * `unit` is the CANONICAL spelling (bvn_unit_to_string) and is null when the
 * string parses but has no text -- an opaque profile unit beside another
 * component, say: a real unit that cannot be serialised (bvn_unit_to_string
 * documents the three ways). `profile_only` is bvn_unit_is_profile_only: the
 * unit has no native spelling. The error is bvn_unit_error_code's distinction
 * between illegal input, an unknown profile and an unsupported profile code. */
WASM_EXPORT
char *bvnr_wasm_unit(const char *text)
{
	sb_t b; sb_init(&b);
	bool ok = false;
	value_unit_t u = bvn_parse_unit((const uint8_t *)(text ? text : ""), &ok);
	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s", ok ? "true" : "false");
	if (!ok) {
		error_code_t e = bvn_unit_error_code(
			(const uint8_t *)(text ? text : ""),
			(uint32_t)(text ? strlen(text) : 0));
		sb_printf(&b, ",\"error\":%d,", (int)e);
		sb_puts(&b, "\"error_name\":");
		sb_json_cstr(&b, bvn_error_to_string(e));
		sb_puts(&b, ",\"unit\":null,\"profile_only\":false}");
		return sb_finish(&b);
	}
	char ub[BVNR_UNIT_STRING_MAX];
	int32_t ul = bvn_unit_to_string(u, ub, sizeof ub);
	sb_puts(&b, ",\"error\":0,\"error_name\":\"none\",\"unit\":");
	if (ul >= 0) sb_json_cstr(&b, ub);
	else         sb_puts(&b, "null");
	sb_printf(&b, ",\"profile_only\":%s}",
		  bvn_unit_is_profile_only(u) ? "true" : "false");
	return sb_finish(&b);
}

/* 1 equal, 0 not equal, -1 one (or both) is not a unit. The caller decides
 * whether that -1 is a program error or data; the wrapper makes it a throw. */
WASM_EXPORT
int bvnr_wasm_units_equal(const char *a, const char *b)
{
	bool oka = false, okb = false;
	value_unit_t ua = bvn_parse_unit((const uint8_t *)(a ? a : ""), &oka);
	value_unit_t ub = bvn_parse_unit((const uint8_t *)(b ? b : ""), &okb);
	if (!oka || !okb) return -1;
	return bvn_unit_equal(ua, ub) ? 1 : 0;
}

/* bvn_units_convertible: the predicate the C API says to SCREEN a conversion
 * target with. 1 yes, 0 no, -1 one of them is not a unit. It is a screen, not a
 * guarantee (s/°C passes it and the conversion still declines); the reader's
 * own policy handles a declined conversion as "this target does not apply". */
WASM_EXPORT
int bvnr_wasm_units_convertible(const char *a, const char *b)
{
	bool oka = false, okb = false;
	value_unit_t ua = bvn_parse_unit((const uint8_t *)(a ? a : ""), &oka);
	value_unit_t ub = bvn_parse_unit((const uint8_t *)(b ? b : ""), &okb);
	if (!oka || !okb) return -1;
	return bvn_units_convertible(ua, ub) ? 1 : 0;
}

/* bvn_unit_convert_value: one number from one unit to another. The DOUBLE path
 * -- it is the C API's own convenience converter, exact only as far as a double
 * is (affine scales such as °C/°F included). The exact arbitrary-precision path
 * is the reader's want_unit hook, reached through eventsConvert(), and that is
 * where a consumer that must not round should go. Returns
 * { ok, value } or { ok:false, error, error_name, value:null }. */
WASM_EXPORT
char *bvnr_wasm_convert_unit(double value, const char *from, const char *to)
{
	sb_t b; sb_init(&b);
	bool okf = false, okt = false;
	value_unit_t uf = bvn_parse_unit((const uint8_t *)(from ? from : ""), &okf);
	value_unit_t ut = bvn_parse_unit((const uint8_t *)(to ? to : ""), &okt);
	error_code_t err = error_none;
	double out = 0.0;
	if (!okf)
		err = bvn_unit_error_code((const uint8_t *)(from ? from : ""),
					  (uint32_t)(from ? strlen(from) : 0));
	else if (!okt)
		err = bvn_unit_error_code((const uint8_t *)(to ? to : ""),
					  (uint32_t)(to ? strlen(to) : 0));
	else if (!bvn_unit_convert_value(value, uf, ut, &out))
		err = error_unit_mismatch;
	/* JSON has no infinity/NaN, and a conversion that produced one is not the
	 * value to hand back as null with ok:true. */
	if (err == error_none && !isfinite(out)) err = error_value_out_of_range;
	sb_putc(&b, '{');
	emit_status(&b, err);
	sb_puts(&b, ",\"value\":");
	if (err == error_none) sb_printf(&b, "%.17g", out);
	else sb_puts(&b, "null");
	sb_putc(&b, '}');
	return sb_finish(&b);
}

/* bvn_unit_to_profile / bvn_unit_to_ucum: a unit written as a code in a
 * profile's vocabulary (the "ucum" case is the one every FHIR-shaped consumer
 * wants). Returns { ok, error, error_name, unit }, where `unit` is the code
 * WITHOUT the "ns:" prefix, or null when the unit is valid but that vocabulary
 * has no code for it -- the two are partial by construction (a currency, an
 * opaque unit of another profile, a compound in a flat vocabulary). An
 * unparseable input is a refusal with its own code, as in bvnr_wasm_unit. */
WASM_EXPORT
char *bvnr_wasm_unit_to_profile(const char *text, const char *ns)
{
	sb_t b; sb_init(&b);
	bool ok = false;
	value_unit_t u = bvn_parse_unit((const uint8_t *)(text ? text : ""), &ok);
	error_code_t err = error_none;
	char ub[BVNR_UNIT_STRING_MAX];
	int32_t n = -1;
	if (!ok) {
		err = bvn_unit_error_code((const uint8_t *)(text ? text : ""),
					  (uint32_t)(text ? strlen(text) : 0));
	} else if (ns && ns[0]) {
		n = bvn_unit_to_profile(ns, u, ub, sizeof ub);
	} else {
		n = bvn_unit_to_ucum(u, ub, sizeof ub);
	}
	sb_putc(&b, '{');
	emit_status(&b, err);
	sb_puts(&b, ",\"unit\":");
	if (err == error_none && n >= 0) sb_json_cstr(&b, ub);
	else                              sb_puts(&b, "null");
	sb_putc(&b, '}');
	return sb_finish(&b);
}

/* The compiled-in unit profiles, as { ok, profiles: ["ucum", ...] } -- the same
 * status-plus-payload shape every other result uses, so the exhaustion sweep
 * and the JS layer treat it like one. */
WASM_EXPORT
char *bvnr_wasm_unit_profiles(void)
{
	sb_t b; sb_init(&b);
	sb_puts(&b, "{\"ok\":true,\"error\":0,\"error_name\":\"none\",\"profiles\":[");
	uint32_t n = bvn_unit_profile_count();
	for (uint32_t i = 0; i < n; i++) {
		if (i) sb_putc(&b, ',');
		sb_json_cstr(&b, bvn_unit_profile_name(i));
	}
	sb_puts(&b, "]}");
	return sb_finish(&b);
}

/* bvn_parse_type_annotation: the body of a type annotation ("uint:32,_16,k~g")
 * parsed as the reader parses one, without a document around it. The three
 * flags keep the failure kinds apart exactly as the C API does: type_ok false
 * is an annotation that does not parse (an unknown family, an unwritable base
 * for that family), unit_ok false a unit parameter that is not a unit, and
 * unit_too_long the one unit failure that is about length (over 255 bytes).
 *
 * This is the reader's own acceptance test: <float:64,_8> and <uint:64,_0> are
 * type_ok false here, as they are error_illegal_value_type in a document. It
 * does not check a VALUE (that is what parsing one does), so <uint:8> against
 * a string still passes here.
 *
 * The type fields are the parsed spec's own, not the reader's resolved view:
 * width 0 and base 0 mean "not written" (the reader resolves them to 64 and
 * 10), float_fix keeps its q in base as the event stream does, and a datetime
 * keeps its epoch index there beside the epoch's name. `unit` is the canonical
 * spelling (null when the unit parses but has no text, as bvnr_wasm_unit says);
 * `unit_source` is the parameter exactly as written. */
WASM_EXPORT
char *bvnr_wasm_parse_type_annotation(const char *text)
{
	sb_t b; sb_init(&b);
	bool type_ok = false, unit_ok = false, unit_too_long = false;
	value_unit_t unit;
	uint8_t unit_text[UINT8_MAX + 1u];
	uint8_t unit_len = 0;
	size_t len = text ? strlen(text) : 0;

	memset(unit_text, 0, sizeof unit_text);
	memset(&unit, 0, sizeof unit);
	value_type_spec_t vt = bvn_parse_type_annotation(
		(const uint8_t *)(text ? text : ""), (uint32_t)len,
		&type_ok, &unit_ok, &unit_too_long, &unit, unit_text, &unit_len);

	sb_putc(&b, '{');
	sb_printf(&b, "\"ok\":%s",
		  (type_ok && unit_ok && !unit_too_long) ? "true" : "false");
	sb_printf(&b, ",\"type_ok\":%s,\"unit_ok\":%s,\"unit_too_long\":%s",
		  type_ok ? "true" : "false", unit_ok ? "true" : "false",
		  unit_too_long ? "true" : "false");
	sb_puts(&b, ",\"family\":");
	if (type_ok) sb_json_cstr(&b, evt_family(vt.family));
	else         sb_puts(&b, "null");
	if (type_ok) {
		sb_printf(&b, ",\"width\":%u,\"base\":%u", vt.width, vt.base);
		if (vt.family == vt_datetime) {
			const char *ep = bvnr_datetime_epoch_name(vt);
			sb_puts(&b, ",\"epoch\":");
			if (ep) sb_json_cstr(&b, ep);
			else    sb_puts(&b, "null");
		} else {
			sb_puts(&b, ",\"epoch\":null");
		}
	} else {
		sb_puts(&b, ",\"width\":null,\"base\":null,\"epoch\":null");
	}
	sb_puts(&b, ",\"unit\":");
	bool have_unit = false;
	if (unit_ok) {
		char ub[BVNR_UNIT_STRING_MAX];
		if (bvn_unit_to_string(unit, ub, sizeof ub) >= 0) {
			sb_json_cstr(&b, ub);
			have_unit = true;
		}
	}
	if (!have_unit) sb_puts(&b, "null");
	sb_puts(&b, ",\"unit_source\":");
	if (!unit_too_long && unit_len > 0u) {
		sb_putc(&b, '"');
		sb_json_str_body(&b, (const char *)unit_text, unit_len);
		sb_putc(&b, '"');
	} else {
		sb_puts(&b, "null");
	}
	sb_printf(&b, ",\"profile_only\":%s",
		  (unit_ok && bvn_unit_is_profile_only(unit)) ? "true" : "false");
	sb_putc(&b, '}');
	return sb_finish(&b);
}

WASM_EXPORT
void bvnr_wasm_free(char *p)
{
	free(p);
}
