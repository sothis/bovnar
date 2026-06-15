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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_io_impl.h"
#include "bvn_val_impl.h"
#include "bovnar_currency.h"
#include "bvn_gregorian_date.h"
#include "bvn_datetime.h"
/*
 * ===========================================================================
 * Validator (semantic layer)
 * ===========================================================================
 *
 * The lexer produces lexically valid tokens; the validator turns them into
 * meaning and enforces the type system. It sits behind the lexer and is fed
 * raw tokens (bvn_val_receive) and structural events (bvn_val_receive_event).
 *
 * Dual emission — the central design choice. Each value can be observed at two
 * stages via two independent callbacks:
 *
 *   on_unverified : fired the moment the lexer hands a token over, before any
 *                   semantic check. Lets a consumer see the raw token stream.
 *   on_verified   : fired only after type/range/unit checks pass, carrying the
 *                   resolved value_type and unit.
 *
 * Tooling (the canonicaliser, the events demo) uses both to show the lexer and
 * validator views side by side; ordinary consumers usually set only
 * on_verified. The bvn_emit_unverified/_verified/_both helpers centralise the
 * "callback returned false => error_scanner_callback_failed" contract.
 *
 * Type inference — bovnar values may omit an explicit type annotation. When a
 * bare value arrives the validator infers a default family (uint/sint/float/
 * utf8/bool) from the literal's shape and *synthesises* the full annotation
 * event sequence (bvn_emit_default_type_annotation), so a downstream writer can
 * round-trip the value with an explicit type even though the input had none.
 *
 * Accumulator — bvn_acc_* incrementally fold a numeric literal into a uint64
 * while it is being parsed, tracking dot/exponent presence. This makes range
 * checking for <=64-bit types allocation-free; wider types fall back to the
 * arbitrary-precision validators in bovnar_utils.c.
 *
 * Caches — type annotations and inline units repeat heavily in real documents
 * (every element of a typed array, say). tcache/iucache are small FIFO caches
 * keyed on the raw annotation bytes so the comparatively expensive parse is
 * done once per distinct annotation rather than once per value.
 */

/*
 * Reader = lexer + validator bundled. Created zeroed; the actual buffers are
 * allocated later by bvnr_open_read_*, so a freshly created reader is cheap and
 * reusable across multiple open/read cycles.
 */
bvnr_reader_t* bvnr_reader_create(void)
{
	bvnr_reader_t* r = malloc(sizeof(*r));
	if (!r) return NULL;
	memset(r, 0, sizeof(*r));
	return r;
}
void bvnr_reader_destroy(bvnr_reader_t* r)
{
	if (!r) return;
	bvn_lex_destroy(&r->lex);
	free(r);
}
void bvn_val_init(bvnr_validator_t* v, bvnr_read_flags_t* opts)
{
	memset(v, 0, sizeof(*v));
	v->value_type             = BVN_TYPE_PLAIN;
	v->parsed_unit            = BVN_UNIT_NO_PREFIX(bu_none);
	v->inferred_default_vtype = BVN_TYPE_PLAIN;
	if (opts) {
		v->userdata      = opts->userdata;
		v->on_unverified = opts->on_unverified;
		v->on_verified   = opts->on_verified;
		v->on_error      = opts->on_error;
	}
}
/*
 * Callbacks receive a non-NULL data pointer even for empty payloads: consumers
 * can blindly read/printf the pointer without a NULL guard. This sentinel is
 * the shared "" they see in that case.
 */
static const uint8_t bvn_empty_sentinel[1] = { 0 };
static inline void bvn_normalize_data_ptr(bvnr_data_t* d)
{
	if (d && d->data == NULL)
		d->data = bvn_empty_sentinel;
}
static inline bool bvn_emit_unverified(bvnr_reader_t* r,
									   bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_unverified) return true;
	bvn_normalize_data_ptr(d);
	if (!v->on_unverified(v->userdata, ev, d)) {
		v->last_error = error_scanner_callback_failed;
		return false;
	}
	return true;
}
static inline bool bvn_emit_verified(bvnr_reader_t* r,
									 bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_verified) return true;
	bvn_normalize_data_ptr(d);
	if (!v->on_verified(v->userdata, ev, d)) {
		v->last_error = error_scanner_callback_failed;
		return false;
	}
	return true;
}
static inline bool bvn_emit_both(bvnr_reader_t* r,
								 bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_unverified && !v->on_verified) return true;
	if (!bvn_emit_unverified(r, ev, d)) return false;
	if (!bvn_emit_verified(r, ev, d))   return false;
	return true;
}
void bvn_acc_reset(bvnr_validator_t* v)
{
	v->acc_value     = 0;
	v->acc_overflow  = false;
	v->acc_has_dot   = false;
	v->acc_has_exp   = false;
	v->acc_exp_state = 0;
}
/*
 * Fold one more digit into the running integer accumulator, in arbitrary base.
 * Stops accumulating once a dot is seen (the integer value is then meaningless)
 * or once overflow is detected — the overflow flag is sticky and is what
 * bvn_check_acc_range turns into error_value_out_of_range for <=64-bit types.
 * The pre-multiply comparison avoids ever actually overflowing uint64.
 */
void bvn_acc_digit(bvnr_validator_t* v, uint32_t dv, uint32_t base)
{
	if (v->acc_overflow || v->acc_has_dot) return;
	if (base < 2u) return;
	uint64_t b = (uint64_t)base;
	uint64_t d = (uint64_t)dv;
	if (v->acc_value > (UINT64_MAX - d) / b)
		v->acc_overflow = true;
	else
		v->acc_value = v->acc_value * b + d;
}
static inline uint32_t bvn_effective_base_or_10(value_type_spec_t vt)
{
	/* float_fix/dec are always decimal; datetime's base field holds an epoch
	 * index (not a numeric base) and its carrier is decimal epoch-seconds. */
	if (vt.family == vt_float_fix || vt.family == vt_float_dec ||
	    vt.family == vt_datetime)
		return 10u;
	return vt.base ? vt.base : 10u;
}
/*
 * Infer and emit a default type annotation for an untyped value.
 *
 * The family is chosen from the literal's shape: strings -> utf8; numbers with
 * a dot/exponent -> float; numbers with a leading '-' -> sint, else uint;
 * nan/inf -> float; a number carrying a currency unit -> float_dec (money must
 * not be binary float); bools -> bool. The inferred 64-bit type then becomes
 * v->value_type so the value is validated as if it had been explicitly typed.
 *
 * inferred_default_vtype is an optimisation: consecutive values of the same
 * inferred type (e.g. a long array of plain ints) are extremely common, so if
 * the new inference matches the previous one we update state but skip
 * re-emitting the identical annotation event burst. The full event sequence
 * (start, family, width param, base param, unit param, end) is only emitted
 * when the inferred type actually changes.
 *
 * The comparison is by *effective* family/width/base (bvn_default_type_equiv),
 * not raw fields, and an explicit annotation primes inferred_default_vtype just
 * like a synthesised default. This keeps the canonical serialiser idempotent:
 * an explicit "<uint:64,_10,no_unit>" (base == 10) and the synthesised uint
 * default (base == 0, effective 10) are the same effective type, so a following
 * bare element is suppressed identically whether the run was opened by an
 * explicit annotation or an inferred one.
 */
static inline bool bvn_default_type_equiv(
	value_type_spec_t a, value_type_spec_t b)
{
	return a.family == b.family &&
	       bvn_effective_width(a) == bvn_effective_width(b) &&
	       bvn_effective_base(a)  == bvn_effective_base(b)  &&
	       bvn_effective_q(a)     == bvn_effective_q(b);
}
/*
 * ISO-8601 datetime literal support (spec 1.1).
 *
 * The lexer accumulates a literal such as 2026-06-15T12:00:00Z into the number
 * scratch buffer (see the dtlit_* states); these helpers recognise that shape,
 * strictly validate the calendar fields, and convert the UTC civil instant to a
 * signed integer count of seconds on the declared epoch's timescale. The result
 * is rendered as a decimal string and fed back through the ordinary
 * datetime-integer path, so range checking and emission are shared verbatim.
 */
static inline bool bvn_str_is_iso_datetime(const uint8_t* s, uint32_t n)
{
	/* A '-' that immediately follows a digit can only be the date separator
	 * of an ISO literal: a number never contains it (a negative sign is at
	 * index 0, an exponent sign follows 'e'/'E'). */
	for (uint32_t i = 1; i < n; i++)
		if (s[i] == '-' && s[i - 1] >= '0' && s[i - 1] <= '9')
			return true;
	return false;
}
/* A plain signed/unsigned decimal integer: optional '-' then all digits, with
 * no '.', exponent or other character. Used to decide whether a bare value in a
 * datetime array context can inherit the established datetime type. */
static inline bool bvn_str_is_plain_integer(const uint8_t* s, uint32_t n)
{
	uint32_t i = (n > 0 && s[0] == '-') ? 1u : 0u;
	if (i >= n)
		return false;
	for (; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;
	return true;
}
static bool bvn_iso_two_digit(const uint8_t* s, int* out)
{
	if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9')
		return false;
	*out = (s[0] - '0') * 10 + (s[1] - '0');
	return true;
}
static int bvn_iso_days_in_month(int64_t y, int m)
{
	static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
		return 29;
	return dim[m - 1];
}
/* Strict parse of YYYY-MM-DD, YYYY-MM-DDTHH:MM:SS or …Z into *dt (UTC civil).
 * Returns false on a wrong-width field, a misplaced separator or an
 * out-of-range component. */
/* Strict parse of an ISO-8601 literal into *dt (the civil time exactly as
 * written) and *offset_seconds (the tz offset in seconds; 0 for `Z` or no
 * zone). Accepted forms:
 *   YYYY-MM-DD
 *   YYYY-MM-DDTHH:MM:SS[.frac][Z | ±HH:MM]
 * A fractional part (`.` then 1+ digits) is accepted and dropped — the carrier
 * is whole seconds, so the value floors to the written second. Returns false on
 * a wrong-width field, a misplaced separator, an out-of-range component, a
 * malformed offset, or any trailing junk. */
static bool bvn_iso_parse_fields(const uint8_t* s, uint32_t n, bvn_datetime_t* dt,
	int64_t* offset_seconds)
{
	*offset_seconds = 0;
	if (n < 10)
		return false;
	for (int i = 0; i < 4; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;
	int64_t year = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
	int month, day, hour = 0, minute = 0, second = 0;
	if (s[4] != '-' || s[7] != '-')
		return false;
	if (!bvn_iso_two_digit(s + 5, &month) || !bvn_iso_two_digit(s + 8, &day))
		return false;
	if (month < 1 || month > 12)
		return false;
	if (day < 1 || day > bvn_iso_days_in_month(year, month))
		return false;

	uint32_t p = 10;
	if (p < n) {
		/* a time part must follow: 'T' HH:MM:SS */
		if (n < 19 || s[10] != 'T' || s[13] != ':' || s[16] != ':')
			return false;
		if (!bvn_iso_two_digit(s + 11, &hour) ||
		    !bvn_iso_two_digit(s + 14, &minute) ||
		    !bvn_iso_two_digit(s + 17, &second))
			return false;
		if (hour > 23 || minute > 59 || second > 59)
			return false;
		p = 19;
		/* optional fractional seconds: '.' then 1+ digits (dropped) */
		if (p < n && s[p] == '.') {
			uint32_t f0 = ++p;
			while (p < n && s[p] >= '0' && s[p] <= '9')
				p++;
			if (p == f0)
				return false;            /* '.' with no digit */
		}
		/* optional zone: 'Z', or ±HH:MM */
		if (p < n) {
			if (s[p] == 'Z') {
				p++;
			} else if (s[p] == '+' || s[p] == '-') {
				int sign = (s[p] == '-') ? -1 : 1;
				p++;
				int oh, om;
				if (n - p != 5u || s[p + 2] != ':')
					return false;        /* require exactly HH:MM */
				if (!bvn_iso_two_digit(s + p, &oh) ||
				    !bvn_iso_two_digit(s + p + 3, &om))
					return false;
				if (oh > 23 || om > 59)
					return false;
				*offset_seconds = sign *
					((int64_t)oh * 3600 + (int64_t)om * 60);
				p = n;
			} else {
				return false;
			}
		}
	}
	if (p != n)
		return false;                            /* trailing junk */

	memset(dt, 0, sizeof *dt);
	dt->date.year = year; dt->date.month = month; dt->date.day = day;
	dt->hour = hour; dt->minute = minute; dt->second = second;
	return true;
}
/* Convert an ISO literal to epoch seconds on vt's declared epoch. Civil epochs
 * (unix/mjd/ntp/y2000) use the uniform conversion; tai applies the leap-second
 * table; the atomic GNSS epochs are rejected (no round-trippable inverse). */
static bool bvn_iso_to_epoch_seconds(const uint8_t* s, uint32_t n,
	value_type_spec_t vt, int64_t* out, error_code_t* err)
{
	bvn_datetime_t dt;
	int64_t offset_seconds;
	if (!bvn_iso_parse_fields(s, n, &dt, &offset_seconds)) {
		*err = error_invalid_datetime_literal;
		return false;
	}
	/* Fold a tz offset to true UTC: the written civil time is at UTC+offset, so
	 * UTC = civil - offset. Done before the conversion so tai's leap-second
	 * lookup is evaluated at the true UTC instant; the conversion formula
	 * absorbs the now-out-of-range seconds (mktime-style). */
	dt.second -= offset_seconds;
	const char* ep = bvnr_datetime_epoch_name(vt);   /* vt.base = epoch index */
	int64_t secs;
	if (strcmp(ep, "tai") == 0) {
		secs = bvn_dt_utc_to_tai_seconds(&dt);
	} else if (strcmp(ep, "gps") == 0 || strcmp(ep, "galileo") == 0 ||
	           strcmp(ep, "glonass") == 0 || strcmp(ep, "beidou") == 0) {
		*err = error_datetime_literal_unsupported_epoch;
		return false;
	} else {
		/* civil epoch; the bvn_epoch_t enum value is the epoch's MJD. */
		secs = bvn_dt_datetime_to_epoch_seconds(&dt,
			(bvn_epoch_t)bvnr_datetime_epoch_mjd(vt));
	}
	if (secs == BVN_GDATE_OVF) {
		*err = error_value_out_of_range;
		return false;
	}
	*out = secs;
	return true;
}
/* Render an int64 as a decimal string (NUL-terminated); returns the length.
 * INT64_MIN-safe (magnitude computed in unsigned). */
static uint32_t bvn_i64_to_decimal(uint8_t* buf, int64_t value)
{
	uint8_t  tmp[24];
	uint32_t ti = 0;
	uint64_t mag = (value < 0) ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
	do { tmp[ti++] = (uint8_t)('0' + mag % 10u); mag /= 10u; } while (mag);
	uint32_t bi = 0;
	if (value < 0)
		buf[bi++] = '-';
	while (ti)
		buf[bi++] = tmp[--ti];
	buf[bi] = 0;
	return bi;
}
static bool bvn_emit_default_type_annotation(bvnr_reader_t* r,
	token_type_t tt, const uint8_t* str, uint32_t str_len,
	bool is_currency_unit,
	const uint8_t* inline_unit_str, uint32_t inline_unit_len,
	value_unit_t inline_unit_val)
{
	bvnr_validator_t* v = &r->val;
	value_type_spec_t default_type;
	const value_unit_t default_unit = BVN_UNIT_NO_PREFIX(bu_none);
	const char*       family_name;
	uint32_t          family_name_len;
	bool              emit_width = false;
	bool              emit_base  = false;
	bool              emit_unit  = false;
	static const char no_unit_str[] = "no_unit";
	if (tt == token_is_string || tt == token_is_array_string) {
		if (!bvn_type_is_plain(v->inferred_default_vtype) &&
		    bvn_default_type_equiv(BVN_TYPE_UTF8, v->inferred_default_vtype)) {
			v->value_type    = BVN_TYPE_UTF8;
			v->parsed_unit   = BVN_UNIT_NO_PREFIX(bu_none);
			v->unit_data_len = 0;
			return true;
		}
		default_type    = BVN_TYPE_UTF8;
		family_name     = "utf8";
		family_name_len = 4;
	} else if ((tt == token_is_number || tt == token_is_array_number) &&
	           bvn_str_is_iso_datetime(str, str_len)) {
		/* Bare ISO-8601 literal (spec 1.1) infers <datetime:64,unix>; the
		 * unix epoch is index 0 and stays implicit (no epoch param), so the
		 * synthesised annotation round-trips as <datetime:64>. */
		default_type.family = vt_datetime;
		default_type.width  = 64;
		default_type.base   = 0;
		family_name     = "datetime";
		family_name_len = 8;
		emit_width = true;
		emit_base  = false;
		emit_unit  = false;
	} else if ((tt == token_is_number || tt == token_is_array_number) &&
	           v->inferred_default_vtype.family == vt_datetime &&
	           bvn_str_is_plain_integer(str, str_len)) {
		/* A bare integer inside a datetime array (the established element
		 * type is datetime) inherits that datetime type — width and epoch —
		 * rather than inferring uint/sint. Without this the canonical form of
		 * a datetime array (annotation on the first element, bare integers
		 * after) would re-read as a datetime/uint mix and the DOM tier would
		 * reject it as non-homogeneous. inferred_default_vtype resets per
		 * assignment, so this only ever fires within one array. */
		default_type    = v->inferred_default_vtype;
		family_name     = "datetime";
		family_name_len = 8;
		emit_width = true;
		emit_base  = false;
		emit_unit  = false;
	} else if (tt == token_is_number || tt == token_is_array_number) {
		bool has_dot = v->acc_has_dot;
		bool has_exp = v->acc_has_exp;
		bool is_neg  = (str_len > 0 && str[0] == '-');
		if (!is_currency_unit && (has_dot || has_exp)) {
			value_type_spec_t cand = {.family=vt_float,.width=64,.base=0};
			if (!bvn_type_is_plain(v->inferred_default_vtype) &&
			    bvn_default_type_equiv(cand, v->inferred_default_vtype)) {
				v->value_type    = cand;
				v->parsed_unit   = BVN_UNIT_NO_PREFIX(bu_none);
				v->unit_data_len = 7u;
				return true;
			}
			default_type.family = vt_float;
			default_type.width  = 64;
			default_type.base   = 0;
			family_name     = "float";
			family_name_len = 5;
			emit_width = emit_base = emit_unit = true;
		} else if (!is_currency_unit
		           && str_len > (uint32_t)is_neg
		           && (uint8_t)(str[(uint32_t)is_neg] - '0') <= 9u) {
			value_type_spec_t cand;
			cand.family = is_neg ? vt_sint : vt_uint;
			cand.width  = 64;
			cand.base   = 0;
			if (!bvn_type_is_plain(v->inferred_default_vtype) &&
			    bvn_default_type_equiv(cand, v->inferred_default_vtype)) {
				v->value_type    = cand;
				v->parsed_unit   = BVN_UNIT_NO_PREFIX(bu_none);
				v->unit_data_len = 7u;
				return true;
			}
			default_type    = cand;
			family_name     = is_neg ? "sint" : "uint";
			family_name_len = 4;
			emit_width = emit_base = emit_unit = true;
		} else {
			bool is_special = bvn_is_special_number_string(
				(const char*)str);
			if (is_currency_unit) {
				default_type.family = vt_float_dec;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "float_dec";
				family_name_len = 9;
				/*
				 * float_dec has no numeric-base parameter; the
				 * parser rejects "<float_dec:...,_10>", so the
				 * canonical default must not emit a base param or
				 * its own output would fail to re-parse.
				 */
				emit_width = emit_unit = true;
				emit_base  = false;
			} else if (is_special) {
				default_type.family = vt_float;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "float";
				family_name_len = 5;
				emit_width = emit_base = emit_unit = true;
			} else if (is_neg) {
				default_type.family = vt_sint;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "sint";
				family_name_len = 4;
				emit_width = emit_base = emit_unit = true;
			} else {
				default_type.family = vt_uint;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "uint";
				family_name_len = 4;
				emit_width = emit_base = emit_unit = true;
			}
		}
	} else if (tt == token_is_bool) {
		default_type    = BVN_TYPE_BOOL;
		family_name     = "bool";
		family_name_len = 4;
	} else {
		return true;
	}
	if (!bvn_type_is_plain(v->inferred_default_vtype) &&
	    bvn_default_type_equiv(default_type, v->inferred_default_vtype)) {
		v->value_type    = default_type;
		v->parsed_unit   = default_unit;
		v->unit_data_len = emit_unit ? 7u : 0u;
		return true;
	}
	v->inferred_default_vtype = default_type;
	v->value_type    = default_type;
	/*
	 * Fold an inline unit on an otherwise un-annotated value into the
	 * synthesised annotation (e.g. "9.81 m/s" -> "<float:64,m/s> 9.81").
	 * The serialiser renders the unit from the annotation's unit param, not
	 * from the data event, so without this the inline unit would be dropped.
	 * Only numeric defaults carry a unit param (emit_unit); utf8 ignores
	 * units by spec, so a unit on a string is intentionally not folded.
	 */
	bool have_inline_unit =
		emit_unit && inline_unit_str != NULL && inline_unit_len > 0;
	v->parsed_unit   = have_inline_unit ? inline_unit_val : default_unit;
	v->unit_data_len = have_inline_unit ? (uint8_t)inline_unit_len
	                 : (emit_unit ? 7u : 0u);
	bvnr_data_t d;
	d.type       = token_is_type;
	d.value_type = BVN_TYPE_PLAIN;
	d.value_unit = BVN_UNIT_NO_PREFIX(bu_none);
	d.data       = family_name;
	d.length     = family_name_len;
	if (!bvn_emit_unverified(r, ev_type_annotation_start, &d))
		return false;
	d.value_type = v->value_type;
	d.value_unit = v->parsed_unit;
	if (!bvn_emit_verified(r, ev_type_annotation_start, &d))
		return false;
	if (!bvn_emit_both(r, ev_type_annotation_type_family, &d))
		return false;
	if (emit_width) {
		d.type   = token_is_type_width;
		d.data   = NULL;
		d.length = 0;
		if (!bvn_emit_both(r,
				ev_type_annotation_type_family_parameter,
				&d))
			return false;
	}
	if (emit_base) {
		d.type   = token_is_type_base;
		d.data   = NULL;
		d.length = 0;
		if (!bvn_emit_both(r,
				ev_type_annotation_type_family_parameter,
				&d))
			return false;
	}
	if (emit_unit) {
		d.type   = token_is_unit;
		if (have_inline_unit) {
			d.data   = inline_unit_str;
			d.length = inline_unit_len;
		} else {
			d.data   = no_unit_str;
			d.length = 7;
		}
		if (!bvn_emit_both(r,
				ev_type_annotation_type_family_parameter,
				&d))
			return false;
	}
	d.type   = token_is_type;
	d.data   = family_name;
	d.length = family_name_len;
	if (!bvn_emit_both(r, ev_type_annotation_end, &d))
		return false;
	return true;
}
/*
 * Re-scan a complete numeric literal to (a) populate the integer accumulator
 * for range checking and (b) set acc_has_dot / acc_has_exp. The lexer already
 * proved the literal is well-formed, so this is about extracting semantics, not
 * re-validating syntax — except for digit-in-base checking when the literal
 * came in as a string that must be interpreted in a non-decimal base.
 *
 * The base-10 path is split out and hand-tuned (inline overflow guard against
 * the UINT64 threshold 1.8e19) because decimal is by far the hot case; other
 * bases defer to bvn_char_to_digit + bvn_acc_digit. The acc_exp_state tracks
 * sign/digits of the exponent so exponent digits aren't folded into the value.
 */
static bool bvn_acc_parse_number(bvnr_validator_t* v,
	const uint8_t* str, uint32_t len, token_type_t tt)
{
	value_type_spec_t vt   = v->value_type;
	uint32_t          base = bvn_effective_base_or_10(vt);
	/* In bases 64 and 85 '+'/'-' are digit characters (those bases are
	 * unsigned-only), so a leading '-' is a digit to be accumulated, never a
	 * negative sign. */
	bool is_neg = (base != 64u && base != 85u && len > 0 && str[0] == '-');
	if (vt.family == vt_uint && is_neg) {
		v->last_error = error_value_out_of_range;
		return false;
	}
	if (tt == token_is_string || tt == token_is_array_string) {
		bool needs_parse =
			(vt.family == vt_uint || vt.family == vt_sint);
		if (!needs_parse) return true;
		uint32_t start = is_neg ? 1u : 0u;
		for (uint32_t i = start; i < len; i++) {
			uint8_t b = str[i];
			uint32_t dv = bvn_char_to_digit((uint32_t)b, base);
			if (dv >= base) {
				v->last_error = error_digit_not_in_base;
				return false;
			}
			bvn_acc_digit(v, dv, base);
		}
		return true;
	}
	if (tt != token_is_number && tt != token_is_array_number)
		return true;
	if (bvn_is_special_number_string((const char*)str))
		return true;
	uint32_t start = is_neg ? 1u : 0u;
	if (base == 10u) {
		for (uint32_t i = start; i < len; i++) {
			uint8_t b = str[i];
			if (b == '.') { v->acc_has_dot = true; continue; }
			if (b == 'e' || b == 'E') {
				v->acc_has_exp = true;
				v->acc_exp_state = 1;
				continue;
			}
			if (v->acc_exp_state > 0) {
				/* Inside the exponent: a sign or digit, never folded
				 * into the mantissa accumulator. */
				v->acc_exp_state = 2;
				continue;
			}
			uint32_t dv = (uint32_t)(b - (uint8_t)'0');
			if (dv > 9u) {
				v->last_error = error_digit_not_in_base;
				return false;
			}
			if (!v->acc_overflow && !v->acc_has_dot) {
				if (v->acc_value > (uint64_t)1844674407370955161ULL ||
				    (v->acc_value == (uint64_t)1844674407370955161ULL && dv > 5u))
					v->acc_overflow = true;
				else
					v->acc_value = v->acc_value * 10u + (uint64_t)dv;
			}
		}
		return true;
	}
	for (uint32_t i = start; i < len; i++) {
		uint8_t b = str[i];
		if (b == '.' ) { v->acc_has_dot = true; continue; }
		if ((b == 'e' || b == 'E') && base <= 14u) {
			v->acc_has_exp = true;
			v->acc_exp_state = 1;
			continue;
		}
		if (v->acc_exp_state > 0) {
			/* Inside the exponent: a sign or digit, never folded
			 * into the mantissa accumulator. */
			v->acc_exp_state = 2;
			continue;
		}
		uint32_t dv = bvn_char_to_digit((uint32_t)b, base);
		if (dv >= base) {
			v->last_error = error_digit_not_in_base;
			return false;
		}
		bvn_acc_digit(v, dv, base);
	}
	return true;
}
/*
 * Verify an integer literal fits the declared width/signedness.
 *
 * For widths > 64 bits the accumulator is useless, so it delegates to the
 * string-based arbitrary-precision range checks. For <=64 bits it computes the
 * exact bound from the width — note signed negatives allow one more magnitude
 * than positives (e.g. INT8 reaches -128 but only +127), handled by the is_neg
 * branch — and compares the accumulated magnitude. nan/inf pass through (they
 * are floats wearing a numeric token). A sticky overflow flag is an immediate
 * out-of-range.
 */
bool bvn_check_acc_range(bvnr_validator_t* v,
	const uint8_t* str, uint32_t str_len, token_type_t tt)
{
	(void)tt;
	value_type_spec_t vt   = v->value_type;
	/* datetime carries a decimal epoch-seconds value (base holds the epoch
	 * index, not a numeric base), validated as a signed integer. */
	uint32_t          base = (vt.family == vt_datetime)
		? 10u : bvn_effective_base_or_10(vt);
	/* Bases 64/85 are unsigned-only and use '+'/'-' as digits, so a leading '-'
	 * is never a negative sign there. */
	bool is_neg = (base != 64u && base != 85u && str_len > 0 && str[0] == '-');
	if (vt.family != vt_uint && vt.family != vt_sint && vt.family != vt_datetime)
		return true;
	if (str_len > 0) {
		uint8_t c0 = str[0];
		if ((c0 == 'n' || c0 == 'i') &&
		    bvn_is_special_number_string((const char*)str))
			return true;
	}
	uint32_t w = bvn_effective_width(vt);
	if (w > 64u) {
		bool in_range = (vt.family == vt_uint)
			? bvn_validate_uint_range((const char*)str, w, base)
			: bvn_validate_sint_range((const char*)str, w, base);
		if (!in_range)
			v->last_error = error_value_out_of_range;
		return in_range;
	}
	if (v->acc_overflow) {
		v->last_error = error_value_out_of_range;
		return false;
	}
	uint64_t acc = v->acc_value;
	if (vt.family == vt_uint) {
		uint64_t max_val = (w >= 64u) ? UINT64_MAX : (1ULL << w) - 1ULL;
		if (acc > max_val) {
			v->last_error = error_value_out_of_range;
			return false;
		}
	} else {
		uint64_t max_mag;
		if (is_neg)
			max_mag = (w >= 64u) ? (1ULL << 63u) : (1ULL << (w - 1));
		else
			max_mag = (w >= 64u) ? (uint64_t)INT64_MAX : (1ULL << (w - 1)) - 1ULL;
		if (acc > max_mag) {
			v->last_error = error_value_out_of_range;
			return false;
		}
	}
	return true;
}
/*
 * Gatekeeper: does the actual token kind agree with the declared type family?
 *
 * Enforces the core type rules — utf8 demands a string, bool demands a bool
 * token, numeric types demand a number (or a string that parses as a number in
 * the declared base), and dot/exponent are only allowed for the float
 * families. plain (untyped) and null always pass. When the checks pass for an
 * integer type it tail-calls the range check, so this one function is the
 * single "is this value legal for its type" decision point.
 */
bool bvn_validate_type_value_compat(bvnr_reader_t* r,
	token_type_t tt, const uint8_t* str, uint32_t str_len)
{
	bvnr_validator_t* v  = &r->val;
	value_type_spec_t vt = v->value_type;
	if (bvn_type_is_plain(vt) || tt == token_is_null_value)
		return true;
	if (vt.family == vt_utf8) {
		if (tt != token_is_string && tt != token_is_array_string) {
			v->last_error = error_type_value_mismatch;
			return false;
		}
		return true;
	}
	if (vt.family == vt_bool) {
		if (tt != token_is_bool) {
			v->last_error = error_type_value_mismatch;
			return false;
		}
		return true;
	}
	if (!bvn_type_is_numeric(vt) && vt.family != vt_datetime)
		return true;
	if (tt != token_is_number    && tt != token_is_array_number &&
		tt != token_is_string    && tt != token_is_array_string) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	/* A datetime is a bare decimal epoch-seconds integer; a string carrier
	 * (the base-in-a-string form, e.g. <uint:_16> "ff") does not apply. */
	if (vt.family == vt_datetime &&
	    (tt == token_is_string || tt == token_is_array_string)) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	if (tt == token_is_string || tt == token_is_array_string) {
		const char* s = (const char*)str;
		uint32_t base = bvn_effective_base(vt);
		if (!bvn_validate_number_in_base(s, base)) {
			v->last_error = error_digit_not_in_base;
			return false;
		}
	}
	if (v->acc_has_exp && vt.family != vt_float &&
		vt.family != vt_float_fix && vt.family != vt_float_dec) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	if (v->acc_has_dot && vt.family != vt_float &&
		vt.family != vt_float_fix && vt.family != vt_float_dec) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	/*
	 * Fixed-point range: a value outside the declared Q-format's signed range
	 * (e.g. 1000000 in float_fix:16,q8, whose range is [-128, 127.996]) is
	 * error_value_out_of_range, mirroring the uint/sint overflow check. Without
	 * this the literal would be accepted and then silently saturate/wrap when an
	 * application converts it to the fixed-point wire form. Special numbers
	 * (nan/inf/ninf) are range-exempt (§6.4) and skipped.
	 */
	if (vt.family == vt_float_fix && str_len > 0) {
		uint8_t c0 = str[0];
		bool special = (c0 == 'n' || c0 == 'i') &&
					   bvn_is_special_number_string((const char*)str);
		if (!special &&
			!bvn_float_str_fits_fix((const char*)str, 10u,
									bvn_effective_width(vt),
									bvn_effective_q(vt))) {
			v->last_error = error_value_out_of_range;
			return false;
		}
	}
	return bvn_check_acc_range(v, str, str_len, tt);
}
/*
 * The main token entry point — the validator's dispatch core.
 *
 * Handles each raw token kind in turn:
 *  - type annotation: parse (via cache) into value_type + unit, then replay the
 *    annotation as the structured event burst the writer expects; a unit bumps
 *    parsed_unit_serial so array frames can tell whether the unit changed.
 *  - symbol: recognise the reserved keywords null/true/false/on/off and rewrite
 *    them into null/bool tokens (bovnar has no separate keyword token kind).
 *  - inline unit: parse (via cache) the trailing unit and reconcile it with any
 *    annotation unit — a mismatch is error_unit_mismatch, the rule that "42 m"
 *    with a <,kg> annotation is illegal.
 *  - bare value: infer a default type, fold the accumulator, run the
 *    type/value/range checks, then emit the verified data event.
 * Every path ends by emitting the value (or its rewrite) through bvn_emit_both.
 */
bool bvn_val_receive(bvnr_reader_t* r, const bvnr_raw_token_t* raw)
{
	bvnr_validator_t* v = &r->val;
	token_type_t      tt = raw->type;
	if (tt == token_is_type) {
		bool type_ok = true, unit_ok = true, unit_too_long = false;
		value_unit_t parsed_unit = BVN_UNIT_NO_PREFIX(bu_none);
		uint8_t unit_buf[UINT8_MAX + 1];
		uint8_t ulen = 0;
		bvnr_data_t d;
		d.type       = token_is_type;
		d.value_type = BVN_TYPE_PLAIN;
		d.value_unit = BVN_UNIT_NO_PREFIX(bu_none);
		d.data       = raw->type_data;
		d.length     = raw->type_len;
		if (!bvn_emit_unverified(r, ev_type_annotation_start, &d))
			return false;
		uint32_t tcache_hit_idx = (uint32_t)-1;
		for (uint32_t i = 0; i < v->tcache_count; i++) {
			const bvn_type_cache_slot_t* sl = &v->tcache[i];
			if (sl->key_len == raw->type_len &&
			    memcmp(sl->key, raw->type_data, raw->type_len) == 0) {
				tcache_hit_idx = i;
				break;
			}
		}
		if (tcache_hit_idx != (uint32_t)-1) {
			const bvn_type_cache_slot_t* sl =
				&v->tcache[tcache_hit_idx];
			v->value_type = sl->vtype;
			parsed_unit   = sl->unit;
			ulen          = sl->ubuf_len;
			if (ulen)
				memcpy(unit_buf, sl->ubuf, ulen);
			type_ok       = sl->type_ok;
			unit_ok       = sl->unit_ok;
			unit_too_long = sl->unit_too_long;
		} else {
			v->value_type = bvn_parse_type_annotation(
				raw->type_data, raw->type_len,
				&type_ok, &unit_ok, &unit_too_long, &parsed_unit,
				unit_buf, &ulen);
			if (raw->type_len <= BVN_TYPE_CACHE_KEY_CAP &&
			    ulen <= BVN_TYPE_CACHE_UBUF_CAP) {
				uint32_t idx;
				if (v->tcache_count < BVN_TYPE_CACHE_SLOTS) {
					idx = v->tcache_count;
					v->tcache_count = (uint8_t)(idx + 1u);
				} else {
					idx = v->tcache_next;
					v->tcache_next = (uint8_t)((idx + 1u)
						% BVN_TYPE_CACHE_SLOTS);
				}
				bvn_type_cache_slot_t* sl = &v->tcache[idx];
				sl->key_len       = (uint16_t)raw->type_len;
				memcpy(sl->key, raw->type_data, raw->type_len);
				sl->vtype         = v->value_type;
				sl->unit          = parsed_unit;
				sl->ubuf_len      = ulen;
				if (ulen)
					memcpy(sl->ubuf, unit_buf, ulen);
				sl->type_ok       = type_ok;
				sl->unit_ok       = unit_ok;
				sl->unit_too_long = unit_too_long;
			}
		}
		if (!type_ok) {
			v->last_error = error_illegal_value_type;
			return false;
		}
		/* The datetime family is spec 1.1; in a 1.0/unversioned document it is
		 * not a recognised type (error_illegal_value_type), preserving the
		 * unversioned==1.0 contract. */
		if (v->value_type.family == vt_datetime &&
		    !bvn_lex_supports_1_1(&r->lex)) {
			v->last_error = error_illegal_value_type;
			return false;
		}
		/*
		 * Prime the default-annotation suppression state so a following
		 * bare array element of the same effective type is suppressed
		 * identically to a synthesised-default run — without this the
		 * canonical serialiser is not idempotent on heterogeneous arrays.
		 */
		v->inferred_default_vtype = v->value_type;
		v->parsed_unit   = parsed_unit;
		if (!BVN_UNIT_IS_NO_UNIT(parsed_unit))
			v->parsed_unit_serial++;
		v->unit_data_len = ulen;
		if (ulen > 0)
			v->has_annotation_unit = true;
		d.value_type = v->value_type;
		d.value_unit = v->parsed_unit;
		if (!bvn_emit_verified(r, ev_type_annotation_start, &d))
			return false;
		if (!bvn_emit_both(r, ev_type_annotation_type_family, &d))
			return false;
		if (v->value_type.width != 0) {
			d.type   = token_is_type_width;
			d.data   = NULL;
			d.length = 0;
			if (!bvn_emit_both(r,
					ev_type_annotation_type_family_parameter,
					&d))
				return false;
		}
		if (v->value_type.family == vt_datetime) {
			/* base holds the epoch index; emit it as a named parameter
			 * (e.g. "tai") rather than a numeric base. Index 0 (unix) is the
			 * default and is left implicit so <datetime> round-trips. */
			if (v->value_type.base != 0) {
				const char* ep =
					bvnr_datetime_epoch_name(v->value_type);
				d.type   = token_is_unit;
				d.data   = ep;
				d.length = (uint32_t)strlen(ep);
				if (!bvn_emit_both(r,
						ev_type_annotation_type_family_parameter,
						&d))
					return false;
			}
		} else if (v->value_type.base != 0) {
			d.type = (v->value_type.family == vt_float_fix)
				? token_is_type_q
				: token_is_type_base;
			d.data   = NULL;
			d.length = 0;
			if (!bvn_emit_both(r,
					ev_type_annotation_type_family_parameter,
					&d))
				return false;
		}
		if (ulen > 0) {
			if (v->value_type.family != vt_utf8 && !unit_ok) {
				v->last_error = unit_too_long
								? error_unit_too_long
								: error_unit_illegal;
				return false;
			}
			d.type   = token_is_unit;
			d.data   = unit_buf;
			d.length = ulen;
			if (!bvn_emit_both(r,
					ev_type_annotation_type_family_parameter,
					&d))
				return false;
		}
		d.type   = token_is_type;
		d.data   = raw->type_data;
		d.length = raw->type_len;
		if (!bvn_emit_both(r, ev_type_annotation_end, &d))
			return false;
		return true;
	}
	if (tt == token_is_symbol) {
		const uint8_t* s = raw->str_data;
		uint32_t       n = raw->str_len;
		bool           kw_bool = false, kw_true = false;
		if (n == 4 && memcmp(s, "null", 4) == 0) {
			tt = token_is_null_value;
		} else if ((n == 4 && memcmp(s, "true", 4) == 0) ||
		           (n == 2 && memcmp(s, "on", 2) == 0)) {
			kw_bool = true; kw_true = true;
		} else if ((n == 5 && memcmp(s, "false", 5) == 0) ||
		           (n == 3 && memcmp(s, "off", 3) == 0)) {
			kw_bool = true; kw_true = false;
		} else if ((n == 3 && (memcmp(s, "nan", 3) == 0 ||
		                       memcmp(s, "inf", 3) == 0)) ||
		           (n == 4 && memcmp(s, "ninf", 4) == 0)) {
			/* Special floats are bare reserved keywords (like null/
			 * true/false). Reclassify to a number token and fall
			 * through to the bare-value path, which synthesises the
			 * default <float:64> and applies special-number handling
			 * via bvn_is_special_number_string on raw->str_data.
			 * Array context is carried structurally by the array-row
			 * events, so (as for null/bool) a single token type
			 * suffices regardless of nesting. */
			tt = token_is_number;
		}
		if (kw_bool) {
			if (bvn_type_is_plain(v->value_type)) {
				if (!bvn_emit_default_type_annotation(r,
						token_is_bool, s, n, false,
						NULL, 0, BVN_UNIT_NO_PREFIX(bu_none)))
					return false;
			} else if (v->value_type.family != vt_bool) {
				v->last_error = error_type_value_mismatch;
				return false;
			}
			bvnr_data_t d;
			d.type       = token_is_bool;
			d.value_type = v->value_type;
			d.value_unit = v->parsed_unit;
			d.data       = kw_true ? "true" : "false";
			d.length     = kw_true ? 4u : 5u;
			return bvn_emit_both(r, ev_data, &d);
		}
	}
	if (tt == token_is_null_value) {
		bvnr_data_t d;
		d.type       = tt;
		d.value_type = v->value_type;
		d.value_unit = v->parsed_unit;
		d.data       = NULL;
		d.length     = 0;
		return bvn_emit_both(r, raw->event, &d);
	}
	bool         iu_have = false;
	bool         iu_ok   = true;
	value_unit_t iu_unit = BVN_UNIT_NO_PREFIX(bu_none);
	if (raw->inline_unit_len > 0) {
		uint32_t iu_hit_idx = (uint32_t)-1;
		for (uint32_t i = 0; i < v->iucache_count; i++) {
			const bvn_iu_cache_slot_t* sl = &v->iucache[i];
			if (sl->key_len == raw->inline_unit_len &&
			    memcmp(sl->key, raw->inline_unit_data,
			           raw->inline_unit_len) == 0) {
				iu_hit_idx = i;
				break;
			}
		}
		if (iu_hit_idx != (uint32_t)-1) {
			iu_unit = v->iucache[iu_hit_idx].unit;
			iu_ok   = v->iucache[iu_hit_idx].ok;
		} else {
			iu_unit = bvn_parse_unit(raw->inline_unit_data, &iu_ok);
			if (raw->inline_unit_len <= BVN_IU_CACHE_KEY_CAP) {
				uint32_t idx;
				if (v->iucache_count < BVN_IU_CACHE_SLOTS) {
					idx = v->iucache_count;
					v->iucache_count = (uint8_t)(idx + 1u);
				} else {
					idx = v->iucache_next;
					v->iucache_next = (uint8_t)((idx + 1u)
						% BVN_IU_CACHE_SLOTS);
				}
				bvn_iu_cache_slot_t* sl = &v->iucache[idx];
				sl->key_len = (uint8_t)raw->inline_unit_len;
				memcpy(sl->key, raw->inline_unit_data,
				       raw->inline_unit_len);
				sl->unit = iu_unit;
				sl->ok   = iu_ok;
			}
		}
		iu_have = true;
	}
	const uint8_t* val_str = raw->str_data;
	uint32_t       val_len = raw->str_len;
	uint8_t        dt_numbuf[24];
	bool           is_iso_dt =
		(tt == token_is_number || tt == token_is_array_number) &&
		bvn_str_is_iso_datetime(raw->str_data, raw->str_len);
	if (is_iso_dt) {
		/* spec 1.1: an ISO-8601 literal is not a recognised value in a
		 * 1.0/unversioned document — mirror the datetime-family gating. */
		if (!bvn_lex_supports_1_1(&r->lex)) {
			v->last_error = error_illegal_value_type;
			return false;
		}
		/* a timestamp carries no unit. */
		if (iu_have) {
			v->last_error = error_unit_illegal;
			return false;
		}
	}
	if (bvn_type_is_plain(v->value_type) &&
		(tt == token_is_number || tt == token_is_array_number ||
		 tt == token_is_string || tt == token_is_array_string)) {
		bool is_currency_unit = false;
		if (iu_have &&
		    (tt == token_is_number || tt == token_is_array_number)) {
			if (iu_ok && iu_unit.num_components == 1 &&
			    bvn_unit_is_currency((int)iu_unit.components[0].base))
				is_currency_unit = true;
		}
		const uint8_t* fold_unit_str = NULL;
		uint32_t       fold_unit_len = 0;
		value_unit_t   fold_unit_val = BVN_UNIT_NO_PREFIX(bu_none);
		if (iu_have && iu_ok &&
		    (tt == token_is_number || tt == token_is_array_number)) {
			fold_unit_str = raw->inline_unit_data;
			fold_unit_len = raw->inline_unit_len;
			fold_unit_val = iu_unit;
		}
		if (!bvn_emit_default_type_annotation(r, tt,
				raw->str_data, raw->str_len, is_currency_unit,
				fold_unit_str, fold_unit_len, fold_unit_val))
			return false;
	}
	if (is_iso_dt) {
		/* The annotation — explicit, or the <datetime:64,unix> just
		 * inferred above — must be datetime; convert the UTC civil literal
		 * to epoch seconds and substitute the decimal integer so the range
		 * check and emission below run the ordinary datetime-integer path. */
		if (v->value_type.family != vt_datetime) {
			v->last_error = error_type_value_mismatch;
			return false;
		}
		int64_t secs;
		if (!bvn_iso_to_epoch_seconds(raw->str_data, raw->str_len,
				v->value_type, &secs, &v->last_error))
			return false;
		val_len = bvn_i64_to_decimal(dt_numbuf, secs);
		val_str = dt_numbuf;
	}
	if (tt == token_is_number || tt == token_is_array_number ||
		tt == token_is_string || tt == token_is_array_string) {
		bvn_acc_reset(v);
		if (!bvn_acc_parse_number(v, val_str, val_len, tt))
			return false;
	}
	if (!bvn_validate_type_value_compat(r, tt, val_str, val_len))
		return false;
	if (iu_have) {
		if (!iu_ok) {
			v->last_error = error_unit_illegal;
			return false;
		}
		if (v->has_annotation_unit &&
			!bvn_unit_equal(v->parsed_unit, iu_unit)) {
			v->last_error = error_unit_mismatch;
			return false;
		}
		v->parsed_unit = iu_unit;
		if (!BVN_UNIT_IS_NO_UNIT(iu_unit))
			v->parsed_unit_serial++;
	}
	bvnr_data_t d;
	d.type       = tt;
	d.value_type = v->value_type;
	d.value_unit = v->parsed_unit;
	if (tt != token_is_null_value) {
		d.data   = val_str;
		d.length = val_len;
	} else {
		d.data   = NULL;
		d.length = 0;
	}
	return bvn_emit_both(r, raw->event, &d);
}
/*
 * Forward a payload-less structural event (struct/array open/close, stream
 * start/end, etc.). Short-circuits entirely when no callbacks are installed —
 * a validate-only run (no consumer) then does zero per-event work. The current
 * type/unit are attached so observers see the active context on every event.
 */
bool bvn_val_receive_event(bvnr_reader_t* r, bvnr_event_t ev)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_unverified && !v->on_verified) return true;
	bvnr_data_t d = {0};
	d.value_type = v->value_type;
	d.value_unit = v->parsed_unit;
	return bvn_emit_both(r, ev, &d);
}
bool bvn_val_receive_octet_chunk(
	bvnr_reader_t* r, const uint8_t* data, uint32_t len)
{
	bvnr_data_t d = {0};
	d.type       = token_is_octet_stream;
	d.value_type = BVN_TYPE_PLAIN;
	d.data       = data;
	d.length     = len;
	return bvn_emit_both(r, ev_data, &d);
}
/*
 * Reset per-value state at the start (and end) of each assignment so a type
 * annotation, unit, or accumulator left over from the previous value can never
 * leak into the next one. Both intro and outro reset identically; having both
 * makes the invariant hold regardless of which boundary the grammar hits first.
 */
bool bvn_val_on_value_intro(bvnr_reader_t* r)
{
	bvnr_validator_t* v = &r->val;
	v->value_type             = BVN_TYPE_PLAIN;
	v->parsed_unit            = BVN_UNIT_NO_PREFIX(bu_none);
	v->inferred_default_vtype = BVN_TYPE_PLAIN;
	v->has_annotation_unit    = false;
	bvn_acc_reset(v);
	return true;
}
bool bvn_val_on_value_outro(bvnr_reader_t* r)
{
	bvnr_validator_t* v = &r->val;
	v->value_type             = BVN_TYPE_PLAIN;
	v->parsed_unit            = BVN_UNIT_NO_PREFIX(bu_none);
	v->inferred_default_vtype = BVN_TYPE_PLAIN;
	v->has_annotation_unit    = false;
	bvn_acc_reset(v);
	return true;
}
bool bvn_val_on_array_intro(bvnr_reader_t* r)
{
	bvnr_data_t d = {0};
	d.value_type = r->val.value_type;
	d.value_unit = r->val.parsed_unit;
	bvn_acc_reset(&r->val);
	r->val.has_annotation_unit = false;
	return bvn_emit_unverified(r, ev_array_row_start, &d) &&
		   bvn_emit_verified(r, ev_array_row_start, &d);
}
/*
 * Closing a row enforces rectangularity: the first row to close establishes the
 * expected width (*array_row_size); every later sibling row must match it or it
 * is error_array_row_size_mismatch. bvn_val_on_new_array_value applies the same
 * bound element-by-element so an over-long row is caught as early as possible.
 */
bool bvn_val_on_array_outro(bvnr_reader_t* r,
	uint64_t curr_row_size, uint64_t* array_row_size)
{
	bvnr_validator_t* v = &r->val;
	bvnr_data_t       d = {0};
	/* UINT64_MAX is the "row width not yet established" sentinel: a width of 0
	 * is a real, valid width (an empty row, "[]"), so it must be distinguishable
	 * from "unset". The first row to close fixes the width; siblings must match. */
	if (*array_row_size == UINT64_MAX)
		*array_row_size = curr_row_size;
	else if (*array_row_size != curr_row_size) {
		v->last_error = error_array_row_size_mismatch;
		return false;
	}
	return bvn_emit_both(r, ev_array_row_end, &d);
}
bool bvn_val_on_new_array_value(bvnr_reader_t* r,
	uint64_t curr_row_size, uint64_t array_row_size)
{
	bvnr_validator_t* v = &r->val;
	if (array_row_size != UINT64_MAX && curr_row_size >= array_row_size) {
		v->last_error = error_array_row_size_mismatch;
		return false;
	}
	bvn_acc_reset(&r->val);
	v->has_annotation_unit = false;
	return true;
}
bool bvnr_read(bvnr_reader_t* r)
{
	return bvn_lex_run(r);
}
error_code_t bvnr_reader_get_error(const bvnr_reader_t* r)
{
	return r ? r->val.last_error : error_none;
}
uint64_t bvnr_reader_get_error_line(const bvnr_reader_t* r)
{
	return r ? r->val.error_line : 0;
}
uint64_t bvnr_reader_get_error_column(const bvnr_reader_t* r)
{
	return r ? r->val.error_column : 0;
}
uint32_t bvnr_reader_get_error_byte(const bvnr_reader_t* r)
{
	return r ? r->val.error_byte : 0;
}
uint64_t bvnr_reader_get_error_offset(const bvnr_reader_t* r)
{
	return r ? r->val.error_offset : 0;
}
uint64_t bvnr_reader_get_recovery_count(const bvnr_reader_t* r)
{
	return r ? r->lex.recovery_count : 0;
}
bool bvnr_reader_get_declared_version(
	const bvnr_reader_t* r, uint16_t* major, uint16_t* minor)
{
	if (!r || !r->lex.has_declared_version)
		return false;
	if (major) *major = r->lex.declared_major;
	if (minor) *minor = r->lex.declared_minor;
	return true;
}
/*
 * Bind a reader to a source and arm it for bvnr_read. Tears down any previous
 * lexer state first so a single reader can be reused for many documents.
 * Requires a source with a real pull function (a zeroed/uninitialised source is
 * rejected rather than crashing). The optional dbg_sink tees raw input for the
 * canonicaliser. bvnr_open_read_mem is a thin convenience wrapper that builds a
 * memory source (and optional memory debug sink) and forwards here.
 */
bool bvnr_open_read_source(
	bvnr_reader_t* r, const bvnr_source_t* src,
	const bvnr_sink_t* dbg_sink, bvnr_read_flags_t* options)
{
	if (!r)
		return false;
	bvn_lex_destroy(&r->lex);
	memset(&r->lex, 0, sizeof(r->lex));
	if (!src || !bvn_source_impl_c(src)->pull)
		return false;
	if (!bvn_lex_init(&r->lex, src, dbg_sink, options))
		return false;
	bvn_val_init(&r->val, options);
	return true;
}
bool bvnr_open_read_mem(
	bvnr_reader_t* r, const void* buf, uint64_t len,
	void* dbg_buf, uint64_t dbg_cap,
	bvnr_read_flags_t* options)
{
	bvnr_source_t src;
	bvnr_sink_t   dbg;
	bvnr_source_from_mem(&src, buf, len);
	if (dbg_buf && dbg_cap)
		bvnr_sink_to_mem(&dbg, dbg_buf, dbg_cap);
	return bvnr_open_read_source(r, &src,
		(dbg_buf && dbg_cap) ? &dbg : NULL, options);
}
