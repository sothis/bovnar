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

#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bvn_float.h"
#include "bovnar_si_units.h"
#include "bovnar_currency.h"
#include "bvn_unit_impl.h"
/*
 * ===========================================================================
 * Shared utilities: number parsing/formatting/validation and unit handling
 * ===========================================================================
 *
 * This is the cross-cutting toolbox used by the reader, writer and DOM. It has
 * three loosely related groups:
 *
 *  1. Digit/number primitives: convert characters to/from digit values in any
 *     base from 2 up to 62, plus the special base-64 and base-85 alphabets;
 *     validate numeric literals; and check that an integer literal fits a given
 *     bit width. The width check is the interesting part — bovnar supports
 *     integers up to BVN_MAX_INT_WIDTH bits, far past uint64, so for wide types
 *     the bound 2^w-1 cannot be held in a register. It is instead materialised
 *     as a decimal *string* (bvn_pow2m1_dec) and compared digit-string against
 *     the (base-converted) literal (bvn_cmp_dec). Cheap length-based prefilters
 *     avoid that expensive path for the common in-range/out-of-range cases.
 *
 *  2. Unit parsing/formatting: turn a textual unit like "kg.m/s^2" into a
 *     structured value_unit_t and back, including SI/IEC prefixes, exponents,
 *     and currency codes.
 *
 *  3. Identifier/symbol/reference validation and scalar parse/format helpers
 *     that wrap the bignum routines for the <=64-bit fast path.
 */

/*
 * Map an ASCII character to its digit value in `base`, or return `base` itself
 * to signal "not a valid digit". Bases 64 and 85 use their own fixed alphabets
 * (standard base64 and Ascii85). For bases up to 62 the ordering is
 * 0-9, a-z, A-Z; note that for base<=36 upper- and lower-case are the SAME
 * digit (case-insensitive hex etc.), while for base>36 they are distinct
 * digits — hence the branch on `base > 36u`.
 */
#define BVN_DEC_SCRATCH_SIZE 10000u
uint32_t bvn_char_to_digit(uint32_t c, uint32_t base)
{
	if (base == 64u) {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26u;
		if (c >= '0' && c <= '9') return c - '0' + 52u;
		if (c == '+')             return 62u;
		if (c == '/')             return 63u;
		return base;
	}
	if (base == 85u) {
		if (c >= '!' && c <= 'u') return c - '!';
		return base;
	}
	uint32_t d;
	if      (c >= '0' && c <= '9') d = c - '0';
	else if (c >= 'a' && c <= 'z') d = 10u + (c - 'a');
	else if (c >= 'A' && c <= 'Z') d = (base > 36u) ? 36u + (c - 'A')
													 : 10u + (c - 'A');
	else return base;
	return d < base ? d : base;
}
static uint32_t bvn_digit_to_char(uint32_t d, uint32_t base)
{
	if (base == 64u) {
		static const char b64[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
			"0123456789+/";
		return (d < 64u) ? (uint32_t)(uint8_t)b64[d] : 0u;
	}
	if (base == 85u)
		return (d < 85u) ? ('!' + d) : 0u;
	if (d <  10u) return '0' + d;
	if (d <  36u) return 'a' + (d - 10u);
	if (d <  62u) return 'A' + (d - 36u);
	return 0u;
}
bool bvn_is_special_number_string(const char* s)
{
	if (!s) return false;
	uint8_t c = (uint8_t)s[0];
	if (c != 'n' && c != 'i') return false;
	if (c == 'i')                                   /* inf  */
		return s[1] == 'n' && s[2] == 'f' && s[3] == '\0';
	if (s[1] == 'a')                                /* nan  */
		return s[2] == 'n' && s[3] == '\0';
	return s[1] == 'i' && s[2] == 'n' && s[3] == 'f' && s[4] == '\0';  /* ninf */
}
bool bvn_validate_digits_for_base(const char* s, uint32_t base)
{
	if (!s || !*s) return false;
	uint32_t i = 0;
	/* In bases 64 and 85 '+'/'-' are digit characters, not signs, so the bases
	 * are unsigned-only and no leading sign is consumed. Other bases accept one
	 * optional leading sign. */
	if (base != 64u && base != 85u && (s[0] == '-' || s[0] == '+')) i = 1;
	if (!s[i]) return false;
	for (; s[i]; i++) {
		if (bvn_char_to_digit((uint8_t)s[i], base) >= base)
			return false;
	}
	return true;
}
/*
 * Validate a full numeric literal (optional sign, mantissa with optional dot,
 * optional exponent) in the given base. The exponent marker is base-dependent:
 * decimal-style 'e'/'E' only when the base is small enough (<=14) that 'e'
 * isn't itself a digit, and hex-float-style 'p'/'P' for power-of-two bases up
 * to 16. The exponent itself is always decimal. Requires at least one mantissa
 * digit and, if an exponent marker appears, at least one exponent digit.
 */
bool bvn_validate_number_in_base(const char* s, uint32_t base)
{
	if (!s || !*s) return false;
	/* Bases 64 and 85 are unsigned integer bases with fixed alphabets (standard
	 * Base64, Ascii85) in which '+', '-', '.', and other punctuation are ordinary
	 * digit characters — not sign, decimal-point, or exponent markers. They never
	 * carry float syntax, so validate them as a pure unsigned digit string. This
	 * also keeps this function in lock-step with bvn_validate_digits_for_base,
	 * which the writer uses for the same values. */
	if (base == 64u || base == 85u)
		return bvn_validate_digits_for_base(s, base);
	uint32_t i = 0;
	if (s[0] == '-' || s[0] == '+') i = 1;
	if (!s[i]) return false;
	bool has_dot = false, has_exp = false;
	bool has_mant_digit = false, has_exp_digit = false;
	for (; s[i]; i++) {
		char c = s[i];
		if (c == '.') {
			if (has_dot || has_exp) return false;
			has_dot = true;
			continue;
		}
		if (((c == 'e' || c == 'E') && base <= 14u) ||
		    ((c == 'p' || c == 'P') && (base & (base - 1u)) == 0u && base <= 16u)) {
			if (has_exp || !has_mant_digit) return false;
			has_exp = true;
			if (s[i + 1] == '+' || s[i + 1] == '-') i++;
			continue;
		}
		if (c == '+' || c == '-') return false;
		if (has_exp) {
			if (bvn_char_to_digit((uint8_t)c, 10u) >= 10u)
				return false;
			has_exp_digit = true;
		} else {
			if (bvn_char_to_digit((uint8_t)c, base) >= base)
				return false;
			has_mant_digit = true;
		}
	}
	return has_mant_digit && (!has_exp || has_exp_digit);
}
/*
 * Compute 2^n - 1 as a decimal string — i.e. the largest unsigned value
 * representable in n bits. Used as the upper bound when range-checking integers
 * wider than 64 bits, where the bound itself can't fit in any machine integer.
 * Implemented as schoolbook doubling: maintain the number as an array of
 * little-endian decimal digits, double it n times, then subtract one. O(n^2)
 * in digit count, which is fine because n is a type width (thousands of bits at
 * most) and the result is cached/compared as a string.
 */
static int bvn_pow2m1_dec(uint32_t n, char* buf, size_t cap)
{
	if (!buf || cap < 2u) return -1;
	if (n == 0) {
		buf[0] = '0'; buf[1] = '\0';
		return 1;
	}
	uint8_t *dig = malloc(BVN_DEC_SCRATCH_SIZE);
	if (!dig) return -1;
	uint32_t nd = 1;
	dig[0] = 1;
	for (uint32_t i = 0; i < n; i++) {
		uint16_t carry = 0;
		for (uint32_t j = 0; j < nd; j++) {
			uint16_t v = (uint16_t)((uint16_t)dig[j] * 2u + carry);
			dig[j] = (uint8_t)(v % 10);
			carry  = v / 10;
		}
		while (carry && nd < BVN_DEC_SCRATCH_SIZE) {
			dig[nd++] = (uint8_t)(carry % 10);
			carry /= 10;
		}
	}
	int borrow = 1;
	for (uint32_t j = 0; j < nd && borrow; j++) {
		int v = (int)dig[j] - borrow;
		if (v < 0) { dig[j] = (uint8_t)(v + 10); borrow = 1; }
		else       { dig[j] = (uint8_t)v;         borrow = 0; }
	}
	while (nd > 1 && dig[nd - 1] == 0) nd--;
	uint32_t len = nd;
	if (len + 1 > cap) len = (uint32_t)cap - 1;
	for (uint32_t j = 0; j < len; j++)
		buf[j] = (char)('0' + dig[nd - 1 - j]);
	buf[len] = '\0';
	free(dig);
	return (int)len;
}
/*
 * Compare two non-negative decimal strings numerically. Leading zeros are
 * skipped first so the comparison reduces to "longer string wins, else
 * lexicographic" — valid only because both are pure decimal with no sign.
 */
static int bvn_cmp_dec(const char* a, const char* b)
{
	while (*a == '0' && a[1]) a++;
	while (*b == '0' && b[1]) b++;
	size_t la = strlen(a), lb = strlen(b);
	if (la < lb) return -1;
	if (la > lb) return  1;
	return strcmp(a, b);
}
/*
 * Convert an arbitrary-base digit string to a decimal string (Horner's method:
 * dec = dec*base + digit, with dec kept as little-endian decimal digits). Lets
 * the wide-integer range check normalise any base to decimal once and then do
 * all comparisons in base 10 against bvn_pow2m1_dec's output.
 */
static bool bvn_digits_to_dec(const char* src, uint32_t base,
	char* buf, size_t cap)
{
	uint8_t *dec = malloc(BVN_DEC_SCRATCH_SIZE);
	if (!dec) return false;
	uint32_t nd = 1;
	dec[0] = 0;
	for (; *src; src++) {
		uint32_t d = bvn_char_to_digit((uint8_t)*src, base);
		if (d >= base) { free(dec); return false; }
		uint32_t carry = d;
		for (uint32_t i = 0; i < nd; i++) {
			uint32_t v = (uint32_t)dec[i] * base + carry;
			dec[i] = (uint8_t)(v % 10u);
			carry  = v / 10u;
		}
		while (carry) {
			if (nd >= BVN_DEC_SCRATCH_SIZE) { free(dec); return false; }
			dec[nd++] = (uint8_t)(carry % 10u);
			carry /= 10u;
		}
	}
	if (nd + 1u > cap) { free(dec); return false; }
	for (uint32_t i = 0; i < nd; i++)
		buf[i] = (char)('0' + dec[nd - 1u - i]);
	buf[nd] = '\0';
	free(dec);
	return true;
}
/*
 * Approximate decimal digit count of a w-bit value: w * log10(2), rounded up.
 * Used only to size the cheap length-based prefilter, so a small over-estimate
 * is harmless.
 */
static uint32_t bvn_dec_digits_2pow(uint32_t w)
{
	if (w == 0u) return 1u;
	return (uint32_t)((double)w * 0.30102999566398119521) + 1u;
}
/*
 * Fast accept/reject by digit count before the costly exact comparison:
 * clearly-shorter numbers are in range (1), clearly-longer are out (0), and the
 * ambiguous boundary length returns -1 to mean "must compare exactly".
 */
static int bvn_dec_digit_prefilter(const char* d, uint32_t max_digits)
{
	while (d[0] == '0' && d[1]) d++;
	size_t n = strlen(d);
	if (n + 1u < (size_t)max_digits)     return 1;
	if (n > (size_t)max_digits + 1u)     return 0;
	return -1;
}
/*
 * Does the unsigned literal `s` (in `base`) fit in `w` bits?
 *
 * Two regimes: for w<=64 it parses to a uint64 (strtoull for base 10, manual
 * Horner with overflow guard otherwise) and compares against 2^w-1. For wider
 * types it normalises to decimal, tries the length prefilter, and only on an
 * ambiguous length materialises the exact bound 2^w-1 for a string compare.
 * w==0 means "width unspecified" and always passes; nan/inf pass through.
 * bvn_validate_sint_range mirrors this with the asymmetric signed bounds and a
 * 2^(w-1) magnitude limit.
 */
bool bvn_validate_uint_range(const char* s, uint32_t w, uint32_t base)
{
	if (!s) return true;
	if (w == 0) return true;
	/* In bases 64 and 85 '+'/'-' are digits, not signs; do not strip a leading
	 * '+' nor reject a leading '-' (it is digit 12 in Ascii85, an invalid digit
	 * in Base64 — either way the digit loop below decides). */
	const bool uses_sign = (base != 64u && base != 85u);
	uint8_t c0 = (uint8_t)s[0];
	if ((c0 < '0' || c0 > '9') && c0 != '+') {
		if (bvn_is_special_number_string(s)) return true;
		if (uses_sign && c0 == '-') return false;
	}
	const char* p = s;
	if (uses_sign && *p == '+') p++;
	if (w <= 64) {
		uint64_t maxv = (w >= 64) ? UINT64_MAX : (1ULL << w) - 1ULL;
		if (base == 10) {
			char* end;
			errno = 0;
			uint64_t v = strtoull(p, &end, 10);
			if (*end || errno == ERANGE) return false;
			return v <= maxv;
		}
		uint64_t v = 0;
		for (; *p; p++) {
			uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
			if (d >= base) return false;
			if (v > (UINT64_MAX - d) / base) return false;
			v = v * base + d;
		}
		return v <= maxv;
	}
	uint32_t max_digits = bvn_dec_digits_2pow(w);
	if (base == 10) {
		int pf = bvn_dec_digit_prefilter(p, max_digits);
		if (pf >= 0) return pf != 0;
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!maxs) return false;
		int r = bvn_pow2m1_dec(w, maxs, BVN_DEC_SCRATCH_SIZE);
		bool ok = (r >= 0) && (bvn_cmp_dec(p, maxs) <= 0);
		free(maxs);
		return ok;
	}
	{
		char *dec = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!dec) return false;
		if (!bvn_digits_to_dec(p, base, dec, BVN_DEC_SCRATCH_SIZE)) {
			free(dec);
			return false;
		}
		int pf = bvn_dec_digit_prefilter(dec, max_digits);
		if (pf >= 0) { free(dec); return pf != 0; }
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!maxs) { free(dec); return false; }
		int r = bvn_pow2m1_dec(w, maxs, BVN_DEC_SCRATCH_SIZE);
		bool ok = (r >= 0) && (bvn_cmp_dec(dec, maxs) <= 0);
		free(dec);
		free(maxs);
		return ok;
	}
}
static bool bvn_dec_increment(char *s)
{
	int len = (int)strlen(s);
	int carry = 1;
	for (int i = len - 1; i >= 0 && carry; i--) {
		int d = (s[i] - '0') + carry;
		s[i] = (char)('0' + (d % 10));
		carry = d / 10;
	}
	if (carry) {
		memmove(s + 1, s, (size_t)len + 1);
		s[0] = '1';
	}
	return true;
}
bool bvn_validate_sint_range(const char* s, uint32_t w, uint32_t base)
{
	if (!s) return true;
	if (w == 0) return true;
	uint8_t c0 = (uint8_t)s[0];
	if ((c0 < '0' || c0 > '9') && c0 != '-' && c0 != '+') {
		if (bvn_is_special_number_string(s)) return true;
	}
	bool neg = (s[0] == '-');
	const char* abs = s;
	if (neg || s[0] == '+') abs++;
	if (w <= 64) {
		if (base == 10) {
			char* end;
			errno = 0;
			int64_t v = strtoll(s, &end, 10);
			if (*end || errno == ERANGE) return false;
			int64_t lo = (w >= 64)
				? INT64_MIN
				: -(int64_t)(1ULL << (w - 1));
			int64_t hi = (w >= 64)
				? INT64_MAX
				: (int64_t)((1ULL << (w - 1)) - 1ULL);
			return v >= lo && v <= hi;
		}
		uint64_t v = 0;
		for (const char* p = abs; *p; p++) {
			uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
			if (d >= base) return false;
			if (v > (UINT64_MAX - d) / base) return false;
			v = v * base + d;
		}
		uint64_t mag = neg
			? ((w >= 64) ? (1ULL << 63) : (1ULL << (w - 1)))
			: ((w >= 64) ? (uint64_t)INT64_MAX : (1ULL << (w - 1)) - 1ULL);
		return v <= mag;
	}
	uint32_t max_digits = bvn_dec_digits_2pow(w - 1);
	if (base == 10) {
		int pf = bvn_dec_digit_prefilter(abs, max_digits);
		if (pf >= 0) return pf != 0;
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!maxs) return false;
		int r = bvn_pow2m1_dec(w - 1, maxs, BVN_DEC_SCRATCH_SIZE);
		if (r < 0) { free(maxs); return false; }
		if (!neg) {
			bool ok = bvn_cmp_dec(abs, maxs) <= 0;
			free(maxs);
			return ok;
		}
		char *tmp = malloc(BVN_DEC_SCRATCH_SIZE + 1u);
		if (!tmp) { free(maxs); return false; }
		strncpy(tmp, maxs, BVN_DEC_SCRATCH_SIZE - 1u);
		tmp[BVN_DEC_SCRATCH_SIZE - 1u] = '\0';
		bvn_dec_increment(tmp);
		bool ok = bvn_cmp_dec(abs, tmp) <= 0;
		free(maxs);
		free(tmp);
		return ok;
	}
	{
		char *dec  = malloc(BVN_DEC_SCRATCH_SIZE);
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!dec || !maxs) { free(dec); free(maxs); return false; }
		bool ok = false;
		if (!bvn_digits_to_dec(abs, base, dec, BVN_DEC_SCRATCH_SIZE))
			goto sint_bigint_done;
		{
			int pf = bvn_dec_digit_prefilter(dec, max_digits);
			if (pf >= 0) { ok = (pf != 0); goto sint_bigint_done; }
		}
		int r = bvn_pow2m1_dec(w - 1, maxs, BVN_DEC_SCRATCH_SIZE);
		if (r < 0)
			goto sint_bigint_done;
		if (!neg) {
			ok = bvn_cmp_dec(dec, maxs) <= 0;
			goto sint_bigint_done;
		}
		{
			char *tmp = malloc(BVN_DEC_SCRATCH_SIZE + 1u);
			if (!tmp) goto sint_bigint_done;
			strncpy(tmp, maxs, BVN_DEC_SCRATCH_SIZE - 1u);
			tmp[BVN_DEC_SCRATCH_SIZE - 1u] = '\0';
			bvn_dec_increment(tmp);
			ok = bvn_cmp_dec(dec, tmp) <= 0;
			free(tmp);
		}
sint_bigint_done:
		free(dec);
		free(maxs);
		return ok;
	}
}
/*
 * Map an error code to a short stable identifier string for diagnostics/logs.
 * Returns a static string (never NULL), so it is safe to print unconditionally.
 */
const char* bvn_error_to_string(error_code_t code)
{
	switch (code) {
	case error_none:                      return "none";
	case error_unknown_token_type:        return "unknown_token_type";
	case error_array_row_size_mismatch:   return "array_row_size_mismatch";
	case error_identifier_too_long:       return "identifier_too_long";
	case error_empty_identifier:          return "empty_identifier";
	case error_struct_nesting_too_high:   return "struct_nesting_too_high";
	case error_array_nesting_too_high:    return "array_nesting_too_high";
	case error_illegal_struct_close:      return "illegal_struct_close";
	case error_string_too_long:           return "string_too_long";
	case error_illegal_escape_sequence:   return "illegal_escape_sequence";
	case error_number_too_long:           return "number_too_long";
	case error_symbol_too_long:           return "symbol_too_long";
	case error_reference_too_long:        return "reference_too_long";
	case error_read_complete_chunk_failed:return "read_complete_chunk_failed";
	case error_octet_stream_out_of_sync:  return "octet_stream_out_of_sync";
	case error_unexpected_input_byte:     return "unexpected_input_byte";
	case error_text_data_too_long:        return "text_data_too_long";
	case error_reading_from_source_fd:    return "reading_from_source_fd";
	case error_got_incomplete_bvnr_stream:return "incomplete_bvnr_stream";
	case error_invalid_utf8_byte:         return "invalid_utf8_byte";
	case error_invalid_byte_order_mark:   return "invalid_byte_order_mark";
	case error_type_too_long:             return "type_too_long";
	case error_unit_too_long:             return "unit_too_long";
	case error_expected_string_in_array:  return "expected_string_in_array";
	case error_expected_number_in_array:  return "expected_number_in_array";
	case error_illegal_value_type:        return "illegal_value_type";
	case error_scanner_callback_failed:   return "scanner_callback_failed";
	case error_file_too_long:             return "file_too_long";
	case error_invalid_argument:          return "invalid_argument";
	case error_too_many_array_items:      return "too_many_array_items";
	case error_writing_to_sink:           return "writing_to_sink";
	case error_sink_buffer_exhausted:     return "sink_buffer_exhausted";
	case error_unit_illegal:              return "unit_illegal";
	case error_base_requires_string_literal: return "base_requires_string_literal";
	case error_type_value_mismatch:       return "type_value_mismatch";
	case error_value_out_of_range:        return "value_out_of_range";
	case error_digit_not_in_base:         return "digit_not_in_base";
	case error_recovered:                 return "recovered";
	case error_unit_mismatch:             return "unit_mismatch";
	case error_array_element_type_mismatch: return "array_element_type_mismatch";
	case error_struct_shape_mismatch:     return "struct_shape_mismatch";
	case error_duplicate_struct_key:      return "duplicate_struct_key";
	default:                              return "unknown_error";
	}
}
typedef struct { const char* a; uint32_t len; value_base_unit_t u; } bu_entry_t;
typedef struct { const char* a; uint32_t len; si_prefix_id_t   p; } si_entry_t;
typedef struct { const char* a; uint32_t len; iec_prefix_id_t  p; } iec_entry_t;
#define BU_LEN_INDEX_SIZE 32u
/*
 * Length index over bu_table: bu_first_for_len[L] is the index of the first
 * table entry whose symbol is at most L bytes long, so the suffix matcher can
 * skip straight past the run of entries longer than the input. These values are
 * a pure function of bu_table and are PRECOMPUTED (rather than built lazily on
 * first use) so the lookup touches no mutable global state and is therefore
 * reentrant / thread-safe — concurrent bvn_parse_unit calls are safe without
 * the caller serialising them. bvn_bu_index_selfcheck() recomputes the index
 * from the table and is asserted equal to these literals by the unit tests, so
 * the constants cannot silently drift when the table changes.
 *
 * If the table grows past BU_LEN_INDEX_SIZE-1 (=31) byte symbols, widen the
 * index; the self-check test will flag it.
 */
static const uint16_t bu_first_for_len[BU_LEN_INDEX_SIZE] = {
	487, 462, 383, 341, 277, 205, 150,  57,
	 57,  57,  30,  11,  11,  11,  10,   8,
	  4,   1,   1,   1,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
};
static const uint32_t bu_max_len = 20u;
/*
 * Symbol -> base-unit lookup table. CRUCIAL INVARIANT: entries are ordered so
 * that, for any input, a longer symbol that is a suffix of that input is always
 * tested before a shorter one that is also a suffix (in practice: grouped by
 * DESCENDING symbol length). The parser takes the first suffix match and stops,
 * so this ordering is what makes it prefer the longest match — e.g. "mol" must
 * resolve to mole, not to prefix "mo" + "l" (liter). Reordering that lets a
 * shorter suffix precede a longer one is a CORRECTNESS bug, not just a slowdown.
 * When adding a unit, insert it in the correct length group and update
 * bu_first_for_len (the self-check test enforces both). The si_table/iec_table
 * below map the prefix symbols (k, M, Ki, ...) the same way.
 */
static const bu_entry_t bu_table[] = {
	{"atmosphere_technical", 20, bu_atmosphere_technical},
	{"metric_horsepower",    17, bu_metric_horsepower},
	{"preussischer_fuss",    17, bu_prussian_fuss},
	{"prussian_scheffel",    17, bu_scheffel},
	{"standard_gravity",     16, bu_standard_gravity},
	{"prussian_klafter",     16, bu_klafter},
	{"preussische_rute",     16, bu_prussian_rute},
	{"preussische_elle",     16, bu_prussian_elle},
	{"fluid_ounces_uk", 15, bu_fluid_ounce_uk},
	{"prussian_morgen",      15, bu_morgen},
	{"nautical_miles",  14, bu_nautical_mile},
	{"survey_foot",     11, bu_survey_foot},
	{"foot_pounds",     11, bu_foot_pound},    {"fluid_ounce_uk",  14, bu_fluid_ounce_uk},
	{"german_mile",     11, bu_german_mile},
	{"nautical_mile",   13, bu_nautical_mile},
	{"kilogram_force",  14, bu_kilogram_force},
	{"deutsche_meile",  14, bu_german_mile},
	{"electronvolts",   13, bu_electronvolt},
	{"doppelzentner",   13, bu_doppelzentner},
	{"prussian_zoll",   13, bu_prussian_zoll},
	{"prussian_rute",   13, bu_prussian_rute},
	{"prussian_line",   13, bu_prussian_line},
	{"prussian_fuss",   13, bu_prussian_fuss},
	{"prussian_elle",   13, bu_prussian_elle},
	{"pennyweights",    12, bu_pennyweight},   {"fluid_ounces",    12, bu_fluid_ounce},
	{"electronvolt",    12, bu_electronvolt},
	{"volt_amperes",    12, bu_volt_ampere},
	{"inch_mercury",    12, bu_inch_hg},
	{"foot_pound",      10, bu_foot_pound},    {"volt_ampere",     11, bu_volt_ampere},
	{"atmospheres",     11, bu_atmosphere},    {"tablespoons",    11, bu_tablespoon},
	{"troy_ounces",     11, bu_troy_ounce},    {"fluid_ounce",    11, bu_fluid_ounce},
	{"pound_force",     11, bu_pound_force},   {"light_years",    11, bu_light_year},
	{"pennyweight",     11, bu_pennyweight},     {"newton_temperature", 18, bu_newton_temp},
	{"steradians",      10, bu_steradian},     {"becquerels",     10, bu_becquerel},
	{"short_tons",      10, bu_short_ton},     {"gallons_uk",     10, bu_gallon_uk},
	{"tablespoon",      10, bu_tablespoon},    {"horsepower",     10, bu_horsepower},
	{"fahrenheit",      10, bu_fahrenheit},    {"atmosphere",     10, bu_atmosphere},
	{"troy_ounce",      10, bu_troy_ounce},    {"light_year",     10, bu_light_year},
	{"arcminutes",      10, bu_arcminute},     {"arcseconds",     10, bu_arcsecond},
	{"revolutions",     11, bu_revolution},    {"fluid_drams",    11, bu_fluid_dram},
	{"fortnights",      10, bu_fortnight},     {"revolution",     10, bu_revolution},
	{"fluid_dram",      10, bu_fluid_dram},
	{"inch_hg",          7, bu_inch_hg},       {"teaspoons",       9, bu_teaspoon},
	{"short_ton",        9, bu_short_ton},     {"gallon_uk",       9, bu_gallon_uk},
	{"angstroms",        9, bu_angstrom},      {"arcminute",       9, bu_arcminute},
	{"arcsecond",        9, bu_arcsecond},     {"long_tons",       9, bu_long_ton},
	{"roentgens",        9, bu_roentgen},      {"steradian",       9, bu_steradian},
	{"becquerel",        9, bu_becquerel},     {"quarts_uk",       9, bu_quart_uk},
	{"fortnight",        9, bu_fortnight},     {"degreesRa",       9, bu_rankine},
	{"degreesDe",        9, bu_delisle},       {"degreesRe",       9, bu_reaumur},
	{"degreesRo",        9, bu_romer},
	{"gill_uk",          7, bu_gill_uk},       {"gills_uk",        8, bu_gill_uk},
	{"quintals",         8, bu_quintal},        {"scruples",        8, bu_scruple},
	{"decibels",         8, bu_decibel},       {"teaspoon",        8, bu_teaspoon},
	{"angstrom",         8, bu_angstrom},      {"furlongs",        8, bu_furlong},
	{"maxwells",         8, bu_maxwell},       {"oersteds",        8, bu_oersted},
	{"galileos",         8, bu_galileo},       {"long_ton",        8, bu_long_ton},
	{"roentgen",         8, bu_roentgen},      {"calories",        8, bu_calorie},
	{"gradians",         8, bu_grad},          {"hectares",        8, bu_hectare},
	{"candelas",         8, bu_candela},       {"coulombs",        8, bu_coulomb},
	{"sieverts",         8, bu_sievert},       {"pints_uk",        8, bu_pint_uk},
	{"quart_uk",         8, bu_quart_uk},      {"fl_oz_uk",        8, bu_fluid_ounce_uk},
	{"fl_drams",         8, bu_fluid_dram},    {"scheffel",        8, bu_scheffel},
	{"degreesF",         8, bu_fahrenheit},    {"degreesC",        8, bu_celsius},
	{"degreeRa",         8, bu_rankine},       {"degreeDe",        8, bu_delisle},
	{"degreeRe",         8, bu_reaumur},       {"degreeRo",        8, bu_romer},
	{"degreesN",         8, bu_newton_temp},
	{"per_cent_mille",  14, bu_per_cent_mille},
	{"per_myriad",      10, bu_per_myriad},     {"per_mille",       9, bu_per_mille},
	{"percent",          7, bu_percent},
	{"leagues",          7, bu_league},        {"quintal",         7, bu_quintal},
	{"scruple",          7, bu_scruple},
	{"bushels",          7, bu_bushel},        {"deniers",         7, bu_denier},
	{"zentner",          7, bu_zentner},       {"klafter",         7, bu_klafter},
	{"rankine",          7, bu_rankine},       {"decibel",         7, bu_decibel},
	{"delisle",          7, bu_delisle},       {"reaumur",         7, bu_reaumur},
	{"degreeN",          7, bu_newton_temp},
	{"gallons",          7, bu_gallon},        {"parsecs",         7, bu_parsec},
	{"furlong",          7, bu_furlong},       {"maxwell",         7, bu_maxwell},
	{"oersted",          7, bu_oersted},       {"galileo",         7, bu_galileo},
	{"calorie",          7, bu_calorie},       {"fathoms",         7, bu_fathom},
	{"barrels",          7, bu_barrel},        {"gradian",         7, bu_grad},
	{"radians",          7, bu_radian},        {"daltons",         7, bu_dalton},
	{"seconds",          7, bu_second},        {"amperes",         7, bu_ampere},
	{"kelvins",          7, bu_kelvin},        {"candela",         7, bu_candela},
	{"newtons",          7, bu_newton},        {"pascals",         7, bu_pascal},
	{"coulomb",          7, bu_coulomb},       {"siemens",         7, bu_siemens},
	{"henries",          7, bu_henry},         {"celsius",         7, bu_celsius},
	{"degreeF",          7, bu_fahrenheit},    {"degreeC",         7, bu_celsius},
	{"minutes",          7, bu_minute},        {"hectare",         7, bu_hectare},
	{"sievert",          7, bu_sievert},       {"degrees",         7, bu_degree},
	{"pint_uk",          7, bu_pint_uk},       {"bushel",          6, bu_bushel},
	{"cables",           6, bu_cable},         {"league",          6, bu_league},
	{"denier",           6, bu_denier},        {"months",          6, bu_month},
	{"pfunds",           6, bu_pfund},         {"schffl",          6, bu_scheffel},
	{"morgen",           6, bu_morgen},
	{"minims",           6, bu_minim},         {"chains",          6, bu_chain},
	{"nepers",           6, bu_neper},         {"barrel",          6, bu_barrel},
	{"arcmin",           6, bu_arcminute},     {"arcsec",          6, bu_arcsecond},
	{"parsec",           6, bu_parsec},        {"gallon",          6, bu_gallon},
	{"fathom",           6, bu_fathom},        {"pounds",          6, bu_pound},
	{"ounces",           6, bu_ounce},         {"grains",          6, bu_grain},
	{"stones",           6, bu_stone},         {"carats",          6, bu_carat},
	{"therms",           6, bu_therm},         {"quarts",          6, bu_quart},
	{"stilbs",           6, bu_stilb},         {"curies",          6, bu_curie},
	{"poises",           6, bu_poise},         {"stokes",          6, bu_stokes},
	{"gal_uk",           6, bu_gallon_uk},     {"radian",          6, bu_radian},
	{"tonnes",           6, bu_tonne},         {"dalton",          6, bu_dalton},
	{"second",           6, bu_second},        {"metres",          6, bu_meter},
	{"meters",           6, bu_meter},         {"ampere",          6, bu_ampere},
	{"kelvin",           6, bu_kelvin},        {"newton",          6, bu_newton},
	{"pascal",           6, bu_pascal},        {"joules",          6, bu_joule},
	{"farads",           6, bu_farad},         {"webers",          6, bu_weber},
	{"teslas",           6, bu_tesla},         {"henrys",          6, bu_henry},
	{"lumens",           6, bu_lumen},         {"katals",          6, bu_katal},
	{"litres",           6, bu_liter},         {"liters",          6, bu_liter},
	{"minute",           6, bu_minute},        {"degree",          6, bu_degree},
	{"degrRa",           6, bu_rankine},       {"degrDe",          6, bu_delisle},
	{"degrRe",           6, bu_reaumur},       {"degrRo",          6, bu_romer},
	{"inches",           6, bu_inch},          {"gi_uk",           5, bu_gill_uk},
	{"bauds",            5, bu_baud},           {"cable",           5, bu_cable},
	{"hands",            5, bu_hand},
	{"pecks",            5, bu_peck},          {"turns",           5, bu_revolution},
	{"minim",            5, bu_minim},         {"month",           5, bu_month},
	{"fl_dr",            5, bu_fluid_dram},
	{"linie",            5, bu_prussian_line}, {"pfund",           5, bu_pfund},
	{"dt_mi",            5, bu_german_mile},
	{"drams",            5, bu_dram},          {"slugs",           5, bu_slug},
	{"chain",            5, bu_chain},         {"gills",           5, bu_gill},
	{"neper",            5, bu_neper},         {"gauss",           5, bu_gauss},
	{"poise",            5, bu_poise},         {"stilb",           5, bu_stilb},
	{"curie",            5, bu_curie},         {"stoke",           5, bu_stokes},
	{"therm",            5, bu_therm},         {"quart",           5, bu_quart},
	{"pound",            5, bu_pound},         {"ounce",           5, bu_ounce},
	{"grain",            5, bu_grain},         {"stone",           5, bu_stone},
	{"carat",            5, bu_carat},         {"yards",           5, bu_yard},
	{"miles",            5, bu_mile},          {"acres",           5, bu_acre},
	{"barns",            5, bu_barn},          {"dynes",           5, bu_dyne},
	{"phots",            5, bu_phot},          {"knots",           5, bu_knot},
	{"pints",            5, bu_pint},          {"degrF",           5, bu_fahrenheit},
	{"degRa",            5, bu_rankine},       {"degrN",           5, bu_newton_temp},
	{"degDe",            5, bu_delisle},       {"degRe",           5, bu_reaumur},
	{"degRo",            5, bu_romer},         {"romer",           5, bu_romer},
	{"tonne",            5, bu_tonne},         {"metre",           5, bu_meter},
	{"meter",            5, bu_meter},         {"grams",           5, bu_gram},
	{"joule",            5, bu_joule},         {"farad",           5, bu_farad},
	{"weber",            5, bu_weber},         {"tesla",           5, bu_tesla},
	{"henry",            5, bu_henry},         {"lumen",           5, bu_lumen},
	{"katal",            5, bu_katal},         {"litre",           5, bu_liter},
	{"liter",            5, bu_liter},         {"weeks",           5, bu_week},
	{"years",            5, bu_year},          {"moles",           5, bu_mol},
	{"hertz",            5, bu_hertz},         {"watts",           5, bu_watt},
	{"volts",            5, bu_volt},          {"grays",           5, bu_gray},
	{"hours",            5, bu_hour},          {"Bytes",           5, bu_byte},
	{"bytes",            5, bu_byte},          {"degrC",           5, bu_celsius},
	{"tn_sh",            5, bu_short_ton},     {"fl_oz",           5, bu_fluid_ounce},
	{"pt_uk",            5, bu_pint_uk},       {"qt_uk",           5, bu_quart_uk},
	{"ftUS",             4, bu_survey_foot},   {"qntl",            4, bu_quintal},
	{"baud",             4, bu_baud},          {"hand",            4, bu_hand},
	{"rods",             4, bu_rod},           {"slug",            4, bu_slug},
	{"gill",             4, bu_gill},          {"cups",            4, bu_cup},
	{"kips",             4, bu_kip},
	{"prln",             4, bu_prussian_line}, {"lots",            4, bu_lot},
	{"rute",             4, bu_prussian_rute}, {"elle",            4, bu_prussian_elle},
	{"zoll",             4, bu_prussian_zoll},
	{"knot",             4, bu_knot},          {"rems",            4, bu_rem},
	{"pint",             4, bu_pint},          {"barn",            4, bu_barn},
	{"acre",             4, bu_acre},          {"dyne",            4, bu_dyne},
	{"phot",             4, bu_phot},          {"tbsp",            4, bu_tablespoon},
	{"tn_l",             4, bu_long_ton},      {"degF",            4, bu_fahrenheit},
	{"Torr",             4, bu_torr},          {"torr",            4, bu_torr},
	{"grad",             4, bu_grad},          {"mmHg",            4, bu_mmhg},
	{"oz_t",             4, bu_troy_ounce},    {"bars",            4, bu_bar},
	{"week",             4, bu_week},          {"year",            4, bu_year},
	{"gram",             4, bu_gram},          {"amps",            4, bu_ampere},
	{"mole",             4, bu_mol},           {"watt",            4, bu_watt},
	{"volt",             4, bu_volt},          {"ohms",            4, bu_ohm},
	{"days",             4, bu_day},           {"hour",            4, bu_hour},
	{"Byte",             4, bu_byte},          {"byte",            4, bu_byte},
	{"thou",             4, bu_thou},          {"mils",            4, bu_thou},
	{"bits",             4, bu_bit},
	{"gray",             4, bu_gray},          {"degC",            4, bu_celsius},
	{"degN",             4, bu_newton_temp},
	{"\xc2\xb0""Ra",     4, bu_rankine},       {"\xc2\xb0""De",    4, bu_delisle},
	{"\xc2\xb0""Re",     4, bu_reaumur},       {"\xc2\xb0""Ro",    4, bu_romer},
	{"degr",             4, bu_degree},
	{"inch",             4, bu_inch},
	{"foot",             4, bu_foot},          {"feet",            4, bu_foot},
	{"yard",             4, bu_yard},          {"mile",            4, bu_mile},
	{"fath",             4, bu_fathom},        {"ergs",            4, bu_erg},
	{"vars",             4, bu_var},           {"dram",            4, bu_dram},
	{"peck",             4, bu_peck},          {"turn",            4, bu_revolution},
	{"pcm",              3, bu_per_cent_mille},{"ppm",             3, bu_ppm},
	{"ppb",              3, bu_ppb},
	{"\xe2\x80\xb0",     3, bu_per_mille},     {"\xe2\x80\xb1",    3, bu_per_myriad},
	{"lea",              3, bu_league},        {"cbl",             3, bu_cable},
	{"dwt",              3, bu_pennyweight},   {"rpm",             3, bu_rpm},
	{"kgf",              3, bu_kilogram_force},{"var",             3, bu_var},
	{"gal",              3, bu_gallon},        {"Gal",             3, bu_galileo},
	{"Btu",              3, bu_btu},           {"BTU",             3, bu_btu},
	{"btu",              3, bu_btu},           {"lbf",             3, bu_pound_force},
	{"lbs",              3, bu_pound},         {"psi",             3, bu_psi},
	{"atm",              3, bu_atmosphere},    {"cal",             3, bu_calorie},
	{"erg",              3, bu_erg},           {"thm",             3, bu_therm},
	{"kip",              3, bu_kip},           {"rem",             3, bu_rem},
	{"cup",              3, bu_cup},           {"gon",             3, bu_grad},
	{"bbl",              3, bu_barrel},        {"fur",             3, bu_furlong},
	{"dyn",              3, bu_dyne},          {"nmi",             3, bu_nautical_mile},
	{"mil",              3, bu_thou},          {"rod",             3, bu_rod},
	{"tex",              3, bu_tex},           {"den",             3, bu_denier},
	{"bsh",              3, bu_bushel},        {"rev",             3, bu_revolution},
	{"Pfd",              3, bu_pfund},         {"prz",             3, bu_prussian_zoll},
	{"prf",              3, bu_prussian_fuss}, {"Ztr",             3, bu_zentner},
	{"lot",              3, bu_lot},
	{"ch",               2, bu_chain},         {"rd",              2, bu_rod},
	{"gi",               2, bu_gill},          {"dr",              2, bu_dram},
	{"\xc2\xb0""N",     3, bu_newton_temp},
	{"\xc2\xb0""C",     3, bu_celsius},
	{"\xe2\x84\xab",    3, bu_angstrom},
	{"\xe2\x84\xa6",    3, bu_ohm},
	{"deg",              3, bu_degree},        {"bar",             3, bu_bar},
	{"sec",              3, bu_second},        {"amp",             3, bu_ampere},
	{"ohm",              3, bu_ohm},           {"Ohm",             3, bu_ohm},
	{"lux",              3, bu_lux},
	{"day",              3, bu_day},           {"bit",             3, bu_bit},
	{"kat",              3, bu_katal},         {"min",             3, bu_minute},
	{"mol",              3, bu_mol},           {"rad",             3, bu_radian},
	{"tsp",              3, bu_teaspoon},
	{"ft_lb",            5, bu_foot_pound},    {"inHg",            4, bu_inch_hg},
	{"\xc2\xb0""F",     3, bu_fahrenheit},
	{"Bd",               2, bu_baud},           {"sc",              2, bu_scruple},
	{"Ra",               2, bu_rankine},
	{"VA",               2, bu_volt_ampere},
	{"dB",               2, bu_decibel},
	{"Oe",               2, bu_oersted},       {"Mx",              2, bu_maxwell},
	{"Ci",               2, bu_curie},         {"Np",              2, bu_neper},
	{"sb",               2, bu_stilb},         {"ph",              2, bu_phot},
	{"St",               2, bu_stokes},        {"ly",              2, bu_light_year},
	{"pc",               2, bu_parsec},        {"ac",              2, bu_acre},
	{"kn",               2, bu_knot},          {"qt",              2, bu_quart},
	{"pt",               2, bu_pint},          {"ft",              2, bu_foot},
	{"yd",               2, bu_yard},          {"mi",              2, bu_mile},
	{"lb",               2, bu_pound},         {"oz",              2, bu_ounce},
	{"gr",               2, bu_grain},         {"st",              2, bu_stone},
	{"ct",               2, bu_carat},         {"hp",              2, bu_horsepower},
	{"in",               2, bu_inch},
	{"Bq",               2, bu_becquerel},     {"Gy",              2, bu_gray},
	{"Hz",               2, bu_hertz},         {"Pa",              2, bu_pascal},
	{"Sv",               2, bu_sievert},       {"Wb",              2, bu_weber},
	{"cd",               2, bu_candela},       {"lm",              2, bu_lumen},
	{"lx",               2, bu_lux},           {"sr",              2, bu_steradian},
	{"ha",               2, bu_hectare},       {"wk",              2, bu_week},
	{"yr",               2, bu_year},          {"au",              2, bu_astronomical_unit},
	{"Da",               2, bu_dalton},        {"eV",              2, bu_electronvolt},
	{"gn",               2, bu_standard_gravity},
	{"PS",               2, bu_metric_horsepower},
	{"CV",               2, bu_metric_horsepower},
	{"mo",               2, bu_month},
	{"fn",               2, bu_fortnight},
	{"pk",               2, bu_peck},
	{"at",               2, bu_atmosphere_technical},
	{"dz",               2, bu_doppelzentner},
	{"\xc3\x85",         2, bu_angstrom},
	{"\xc2\xb0",         2, bu_degree},
	{"%",                1, bu_percent},
	{"G",                1, bu_gauss},         {"P",               1, bu_poise},
	{"R",                1, bu_roentgen},
	{"B",                1, bu_byte},          {"C",               1, bu_coulomb},
	{"F",                1, bu_farad},         {"H",               1, bu_henry},
	{"J",                1, bu_joule},         {"K",               1, bu_kelvin},
	{"L",                1, bu_liter},         {"N",               1, bu_newton},
	{"l",                1, bu_liter},
	{"S",                1, bu_siemens},       {"T",               1, bu_tesla},
	{"V",                1, bu_volt},          {"W",               1, bu_watt},
	{"A",                1, bu_ampere},        {"b",               1, bu_bit},
	{"d",                1, bu_day},           {"g",               1, bu_gram},
	{"h",                1, bu_hour},          {"m",               1, bu_meter},
	{"s",                1, bu_second},        {"t",               1, bu_tonne},
	{NULL, 0, bu_none}
};
static const si_entry_t si_table[] = {
	{"Q",  1, si_quetta}, {"R",  1, si_ronna},  {"Y",  1, si_yotta},
	{"Z",  1, si_zetta},  {"E",  1, si_exa},    {"P",  1, si_peta},
	{"T",  1, si_tera},   {"G",  1, si_giga},   {"M",  1, si_mega},
	{"k",  1, si_kilo},   {"h",  1, si_hecto},  {"da", 2, si_deca},
	{"d",  1, si_deci},   {"c",  1, si_centi},  {"m",  1, si_milli},
	{"\xc2\xb5", 2, si_micro},
	{"n",  1, si_nano},   {"p",  1, si_pico},   {"f",  1, si_femto},
	{"a",  1, si_atto},   {"z",  1, si_zepto},  {"y",  1, si_yocto},
	{"r",  1, si_ronto},  {"q",  1, si_quecto},
	{NULL, 0, si_none}
};
static const iec_entry_t iec_table[] = {
	{"Qi", 2, iec_quebi}, {"Ri", 2, iec_robi},
	{"Yi", 2, iec_yobi},  {"Zi", 2, iec_zebi},  {"Ei", 2, iec_exbi},
	{"Pi", 2, iec_pebi},  {"Ti", 2, iec_tebi},  {"Gi", 2, iec_gibi},
	{"Mi", 2, iec_mebi},  {"Ki", 2, iec_kibi},
	{NULL, 0, iec_none}
};
/*
 * Recompute the length index from bu_table and verify it equals the precomputed
 * bu_first_for_len / bu_max_len literals above. This is the guard that keeps the
 * baked-in constants honest: it is not used on the parse path (which reads the
 * literals directly, with no init), only by the unit tests. Returns true iff the
 * literals are consistent with the current table; a false return after editing
 * bu_table means the literals must be regenerated.
 */
bool bvn_bu_index_selfcheck(void)
{
	uint32_t count = 0;
	uint32_t maxlen = 0;
	for (const bu_entry_t* e = bu_table; e->a; e++) {
		count++;
		if (e->len > maxlen) maxlen = e->len;
	}
	if (maxlen >= BU_LEN_INDEX_SIZE) maxlen = BU_LEN_INDEX_SIZE - 1u;
	if (bu_max_len != maxlen)
		return false;
	uint32_t idx = 0;
	for (int32_t L = (int32_t)BU_LEN_INDEX_SIZE - 1; L >= 0; L--) {
		while (idx < count && bu_table[idx].len > (uint32_t)L) idx++;
		if (bu_first_for_len[L] != (uint16_t)idx)
			return false;
	}
	return true;
}
/*
 * Strip and decode an exponent suffix from the end of a unit component,
 * returning the number of bytes consumed (0 if none). bovnar accepts three
 * spellings, all handled here: ASCII caret form ("^2", "^-3"), Unicode
 * superscript digits (e.g. "²", "³", and the U+2070 block for 4-9), and a
 * Unicode superscript minus for negative exponents. Defaulting to exp_linear
 * when absent means "m" and "m^1" parse identically.
 */
static uint32_t parse_unit_exponent_suffix(
	const char* s, uint32_t len, unit_exponent_t* exp)
{
	if (len >= 2 && s[len - 2] == '^') {
		char d = s[len - 1];
		if (d >= '1' && d <= '9') {
			static const unit_exponent_t pos_tab[10] = {
				exp_invalid,  exp_linear,  exp_square,  exp_cubic,
				exp_quartic,  exp_quintic, exp_sextic,  exp_septic,
				exp_octic,    exp_nonic
			};
			*exp = pos_tab[(uint8_t)(d - '0')];
			return 2;
		}
	}
	if (len >= 3 && s[len - 3] == '^' &&
		(s[len - 2] == '+' || s[len - 2] == '-')) {
		char sign = s[len - 2];
		char d    = s[len - 1];
		if (d >= '1' && d <= '9') {
			static const unit_exponent_t pos_tab[10] = {
				exp_invalid,  exp_linear,  exp_square,  exp_cubic,
				exp_quartic,  exp_quintic, exp_sextic,  exp_septic,
				exp_octic,    exp_nonic
			};
			static const unit_exponent_t neg_tab[10] = {
				exp_invalid,  exp_neg_linear, exp_neg_square,
				exp_neg_cubic, exp_neg_quartic, exp_neg_quintic,
				exp_neg_sextic, exp_neg_septic, exp_neg_octic,
				exp_neg_nonic
			};
			uint8_t idx = (uint8_t)(d - '0');
			*exp = (sign == '-') ? neg_tab[idx] : pos_tab[idx];
			return 3;
		}
	}
	unit_exponent_t base_exp = exp_linear;
	uint32_t        dig_len  = 0;
	if (len >= 3 &&
		(uint8_t)s[len - 3] == 0xE2 &&
		(uint8_t)s[len - 2] == 0x81) {
		switch ((uint8_t)s[len - 1]) {
		case 0xB4: base_exp = exp_quartic;  dig_len = 3; break;
		case 0xB5: base_exp = exp_quintic;  dig_len = 3; break;
		case 0xB6: base_exp = exp_sextic;   dig_len = 3; break;
		case 0xB7: base_exp = exp_septic;   dig_len = 3; break;
		case 0xB8: base_exp = exp_octic;    dig_len = 3; break;
		case 0xB9: base_exp = exp_nonic;    dig_len = 3; break;
		default:   break;
		}
	}
	if (!dig_len && len >= 2 && (uint8_t)s[len - 2] == 0xC2) {
		switch ((uint8_t)s[len - 1]) {
		case 0xB9: base_exp = exp_linear; dig_len = 2; break;
		case 0xB2: base_exp = exp_square; dig_len = 2; break;
		case 0xB3: base_exp = exp_cubic;  dig_len = 2; break;
		default:   break;
		}
	}
	if (!dig_len)
		return 0;
	uint32_t pos      = len - dig_len;
	bool     negative = false;
	uint32_t sign_len = 0;
	if (pos >= 3 &&
		(uint8_t)s[pos - 3] == 0xE2 &&
		(uint8_t)s[pos - 2] == 0x81) {
		uint8_t b = (uint8_t)s[pos - 1];
		if      (b == 0xBB) { negative = true;  sign_len = 3; }
		else if (b == 0xBA) { negative = false; sign_len = 3; }
	}
	if (negative) {
		switch (base_exp) {
		case exp_linear:  base_exp = exp_neg_linear;  break;
		case exp_square:  base_exp = exp_neg_square;  break;
		case exp_cubic:   base_exp = exp_neg_cubic;   break;
		case exp_quartic: base_exp = exp_neg_quartic; break;
		case exp_quintic: base_exp = exp_neg_quintic; break;
		case exp_sextic:  base_exp = exp_neg_sextic;  break;
		case exp_septic:  base_exp = exp_neg_septic;  break;
		case exp_octic:   base_exp = exp_neg_octic;   break;
		case exp_nonic:   base_exp = exp_neg_nonic;   break;
		default:          break;
		}
	}
	*exp = base_exp;
	return dig_len + sign_len;
}
static unit_exponent_t bvn_negate_exponent(unit_exponent_t e)
{
	switch (e) {
	case exp_linear:      return exp_neg_linear;
	case exp_square:      return exp_neg_square;
	case exp_cubic:       return exp_neg_cubic;
	case exp_quartic:     return exp_neg_quartic;
	case exp_quintic:     return exp_neg_quintic;
	case exp_sextic:      return exp_neg_sextic;
	case exp_septic:      return exp_neg_septic;
	case exp_octic:       return exp_neg_octic;
	case exp_nonic:       return exp_neg_nonic;
	case exp_neg_linear:  return exp_linear;
	case exp_neg_square:  return exp_square;
	case exp_neg_cubic:   return exp_cubic;
	case exp_neg_quartic: return exp_quartic;
	case exp_neg_quintic: return exp_quintic;
	case exp_neg_sextic:  return exp_sextic;
	case exp_neg_septic:  return exp_septic;
	case exp_neg_octic:   return exp_octic;
	case exp_neg_nonic:   return exp_nonic;
	default:              return e;
	}
}
/*
 * Parse one unit component "[prefix~]base[^exp]" into its structured form.
 *
 * Order of operations matters: the exponent suffix is removed first, then the
 * remainder is resolved as base ± prefix. Currencies are checked before the SI
 * table because a currency code may itself look like a prefixed unit, and they
 * use an explicit '~' to separate an (SI/IEC) prefix from the currency. For
 * physical units the base symbol is matched as the longest suffix (via the
 * length index) and whatever precedes it must be a known prefix followed by
 * '~'. bvn_prefix_unit_valid rejects nonsensical prefix/unit pairings (e.g. a
 * binary IEC prefix on a non-information unit). *ok reports validity.
 */
static value_unit_component_t bvn_parse_single_unit_component(
	const char* s, uint32_t len, bool* ok)
{
	value_unit_component_t r = {
		.base = bu_none,
		.exponent = exp_linear,
		.prefix.system = prefix_si,
		.prefix.id.si  = si_none
	};
	*ok = true;
	if (!s || !len) { *ok = false; return r; }
	uint32_t suf = parse_unit_exponent_suffix(s, len, &r.exponent);
	len -= suf;
	if (len == 0) { *ok = false; return r; }
	{
		/* Currencies carry a mandatory '$' sigil (spec 1.0): standalone "$USD",
		 * or prefixed "<prefix>~$EUR".  A bare code is no longer a currency, so
		 * the CUP/cup namespace collision can no longer arise. */
		if (s[0] == '$') {
			int cid = bvn_parse_currency_str((const uint8_t*)s + 1, len - 1);
			if (cid > 0) {
				r.base = (value_base_unit_t)cid;
				return r;
			}
			*ok = false;   /* '$' introduces a currency and nothing else */
			return r;
		}
		for (uint32_t ti = 1; ti + 1 < len; ti++) {
			if (s[ti] != '~') continue;
			uint32_t pfx_len  = ti;
			uint32_t curr_off = ti + 1;
			if (curr_off >= len || s[curr_off] != '$')
				break;     /* no sigil after '~' -> not a currency component */
			uint32_t code_off = curr_off + 1;
			uint32_t code_len = len - code_off;
			int cid = bvn_parse_currency_str(
				(const uint8_t*)s + code_off, code_len);
			if (cid > 0) {
				r.base = (value_base_unit_t)cid;
				for (const iec_entry_t* e = iec_table; e->a; e++) {
					if (e->len == pfx_len && memcmp(s, e->a, pfx_len) == 0) {
						r.prefix.system = prefix_iec;
						r.prefix.id.iec = e->p;
						if (!bvn_prefix_unit_valid(r.prefix, r.base))
							*ok = false;
						return r;
					}
				}
				for (const si_entry_t* e = si_table; e->a; e++) {
					if (e->len == pfx_len && memcmp(s, e->a, pfx_len) == 0) {
						r.prefix.system = prefix_si;
						r.prefix.id.si  = e->p;
						if (!bvn_prefix_unit_valid(r.prefix, r.base))
							*ok = false;
						return r;
					}
				}
				*ok = false;
				return r;
			}
			*ok = false;   /* '$' after '~' but unknown currency code */
			return r;
		}
	}
	uint32_t bu_skip_for_input;
	{
		uint32_t lkup = len > bu_max_len ? bu_max_len : len;
		bu_skip_for_input = bu_first_for_len[lkup];
	}
	const bu_entry_t* best = NULL;
	for (const bu_entry_t* e = &bu_table[bu_skip_for_input]; e->a; e++) {
		if (e->len > len) continue;
		if (memcmp(s + len - e->len, e->a, e->len) == 0) {
			best = e;
			break;
		}
	}
	if (!best) { *ok = false; return r; }
	r.base = best->u;
	uint32_t plen = len - best->len;
	if (plen == 0) {
		r.prefix.system = prefix_si;
		r.prefix.id.si  = si_none;
		return r;
	}
	if (plen < 2 || s[plen - 1] != '~') {
		*ok = false;
		return r;
	}
	uint32_t plen2 = plen - 1;
	for (const iec_entry_t* e = iec_table; e->a; e++) {
		if (e->len == plen2 &&
			memcmp(s, e->a, plen2) == 0) {
			r.prefix.system = prefix_iec;
			r.prefix.id.iec = e->p;
			if (!bvn_prefix_unit_valid(r.prefix, r.base)) {
				*ok = false;
			}
			return r;
		}
	}
	for (const si_entry_t* e = si_table; e->a; e++) {
		if (e->len == plen2 &&
			memcmp(s, e->a, plen2) == 0) {
			r.prefix.system = prefix_si;
			r.prefix.id.si  = e->p;
			if (!bvn_prefix_unit_valid(r.prefix, r.base)) {
				*ok = false;
			}
			return r;
		}
	}
	*ok = false;
	return r;
}
/*
 * Parse a complete unit expression into a value_unit_t (up to
 * BVNR_MAX_UNIT_COMPONENTS components). The literal "no_unit" maps to the empty
 * unit. A component-separated expression uses '*' / '·' for multiplication and
 * '/' to switch into the denominator, where component exponents are negated so
 * "m/s^2" becomes [m^1, s^-2]. With no separator the whole string is a single
 * component, handled by the fast path. bvn_parse_unit is the convenience
 * NUL-terminated wrapper around this length-counted form.
 */
/*
 * Parenthesised grouping in unit expressions (spec 1.0). A "(...)" group is a
 * sub-expression parsed independently; like any factor it obeys the sticky
 * denominator, so a "/" before a group negates the group's net component
 * exponents. This makes the readable forms work and compose correctly:
 *   k~g/(m·s²)   -> kg·m⁻¹·s⁻²     (pressure)
 *   (k~g/m)·s²   -> kg·m⁻¹·s²
 *   a/(b/c)      -> a·c·b⁻¹
 * Parenless expressions are unaffected (identical to the pre-1.0 flat parser).
 * An explicit separator is required between a factor and a group ("m·(s)", not
 * "m(s)"); a group is not yet followed by its own exponent. Nesting is bounded.
 */
#define BVN_UNIT_GROUP_MAX_DEPTH 16u
static value_unit_t bvn_parse_unit_expr(
	const char* s, uint32_t slen, uint32_t depth, bool* ok)
{
	value_unit_t result = { .num_components = 0 };
	*ok = true;
	if (!s || !slen)                    { *ok = false; return result; }
	if (depth > BVN_UNIT_GROUP_MAX_DEPTH) { *ok = false; return result; }
	if (slen == 7 && memcmp(s, "no_unit", 7) == 0)
		return result;                  /* the empty (dimensionless) unit */

	bool     in_denominator = false;
	uint32_t i              = 0;
	while (i < slen) {
		if (s[i] == '(') {
			/* Factor is a parenthesised group: find the matching ')'. */
			uint32_t pd = 1, j = i + 1;
			for (; j < slen && pd; j++) {
				if (s[j] == '(')      pd++;
				else if (s[j] == ')') pd--;
				if (pd == 0) break;
			}
			if (pd != 0) { *ok = false; return result; }  /* unmatched '(' */
			bool gok;
			value_unit_t grp = bvn_parse_unit_expr(
				s + i + 1, j - (i + 1), depth + 1, &gok);
			if (!gok) { *ok = false; return result; }
			uint32_t gn = grp.num_components < BVNR_MAX_UNIT_COMPONENTS
			            ? grp.num_components : BVNR_MAX_UNIT_COMPONENTS;
			for (uint32_t k = 0; k < gn; k++) {
				if (result.num_components >= BVNR_MAX_UNIT_COMPONENTS) {
					*ok = false; return result;
				}
				value_unit_component_t c = grp.components[k];
				if (in_denominator)
					c.exponent = bvn_negate_exponent(c.exponent);
				result.components[result.num_components++] = c;
			}
			i = j + 1;                  /* advance past ')' */
		} else {
			/* Factor is a plain component: scan to the next top-level
			 * separator or '('. */
			uint32_t start = i;
			while (i < slen) {
				uint8_t b = (uint8_t)s[i];
				if (b == '*' || b == '/' || b == '(')
					break;
				if (b == 0xC2 && i + 1 < slen &&
					(uint8_t)s[i + 1] == 0xB7)
					break;
				i++;
			}
			if (i == start) { *ok = false; return result; } /* empty */
			bool comp_ok;
			value_unit_component_t comp = bvn_parse_single_unit_component(
				s + start, i - start, &comp_ok);
			if (!comp_ok) { *ok = false; return result; }
			if (in_denominator)
				comp.exponent = bvn_negate_exponent(comp.exponent);
			if (result.num_components >= BVNR_MAX_UNIT_COMPONENTS) {
				*ok = false; return result;
			}
			result.components[result.num_components++] = comp;
		}
		/* Consume the separator between factors (or stop at end). */
		if (i < slen) {
			uint8_t b = (uint8_t)s[i];
			if (b == '/')      { in_denominator = true; i++; }
			else if (b == '*') { i++; }
			else if (b == 0xC2 && i + 1 < slen &&
					 (uint8_t)s[i + 1] == 0xB7) { i += 2; }
			else { *ok = false; return result; } /* '(' (implicit mult) or
			                                       * an exponent after ')' */
			if (i >= slen) { *ok = false; return result; } /* trailing sep */
		}
	}
	if (result.num_components == 0)
		*ok = false;
	return result;
}
value_unit_t bvn_parse_unit_n(const uint8_t* unit, uint32_t len, bool* ok)
{
	*ok = true;
	if (!unit || !len) {
		*ok = false;
		return (value_unit_t){ .num_components = 0 };
	}
	return bvn_parse_unit_expr((const char*)unit, len, 0, ok);
}
value_unit_t bvn_parse_unit(const uint8_t* unit, bool* ok)
{
	if (!unit) { *ok = false; return (value_unit_t){ .num_components = 0 }; }
	return bvn_parse_unit_n(unit, (uint32_t)strlen((const char*)unit), ok);
}
static const char* si_prefix_str(si_prefix_id_t p)
{
	switch (p) {
	case si_quecto: return "q";  case si_ronto: return "r";
	case si_yocto:  return "y";  case si_zepto: return "z";
	case si_atto:   return "a";  case si_femto: return "f";
	case si_pico:   return "p";  case si_nano:  return "n";
	case si_micro:  return "\xc2\xb5";
	case si_milli:  return "m";  case si_centi: return "c";
	case si_deci:   return "d";  case si_deca:  return "da";
	case si_hecto:  return "h";  case si_kilo:  return "k";
	case si_mega:   return "M";  case si_giga:  return "G";
	case si_tera:   return "T";  case si_peta:  return "P";
	case si_exa:    return "E";  case si_zetta: return "Z";
	case si_yotta:  return "Y";  case si_ronna: return "R";
	case si_quetta: return "Q";  default:       return "";
	}
}
static const char* iec_prefix_str(iec_prefix_id_t p)
{
	switch (p) {
	case iec_kibi: return "Ki"; case iec_mebi: return "Mi";
	case iec_gibi: return "Gi"; case iec_tebi: return "Ti";
	case iec_pebi: return "Pi"; case iec_exbi: return "Ei";
	case iec_zebi: return "Zi"; case iec_yobi: return "Yi";
	case iec_robi: return "Ri"; case iec_quebi: return "Qi";
	default:       return "";
	}
}
static const char* base_unit_str(value_base_unit_t b)
{
	switch (b) {
	case bu_bit:       return "b";
	case bu_byte:      return "B";
	case bu_second:    return "s";
	case bu_meter:     return "m";
	case bu_gram:      return "g";
	case bu_ampere:    return "A";
	case bu_kelvin:    return "K";
	case bu_mol:       return "mol";
	case bu_candela:   return "cd";
	case bu_hertz:     return "Hz";
	case bu_newton:    return "N";
	case bu_pascal:    return "Pa";
	case bu_joule:     return "J";
	case bu_watt:      return "W";
	case bu_volt:      return "V";
	case bu_ohm:       return "\xe2\x84\xa6";
	case bu_farad:     return "F";
	case bu_coulomb:   return "C";
	case bu_siemens:   return "S";
	case bu_weber:     return "Wb";
	case bu_tesla:     return "T";
	case bu_henry:     return "H";
	case bu_lumen:     return "lm";
	case bu_lux:       return "lx";
	case bu_becquerel: return "Bq";
	case bu_gray:      return "Gy";
	case bu_sievert:   return "Sv";
	case bu_katal:     return "kat";
	case bu_liter:     return "L";
	case bu_minute:    return "min";
	case bu_hour:      return "h";
	case bu_day:       return "d";
	case bu_degree:    return "\xc2\xb0";
	case bu_celsius:   return "\xc2\xb0""C";
	case bu_radian:            return "rad";
	case bu_steradian:         return "sr";
	case bu_tonne:             return "t";
	case bu_bar:               return "bar";
	case bu_electronvolt:      return "eV";
	case bu_dalton:            return "Da";
	case bu_astronomical_unit: return "au";
	case bu_hectare:           return "ha";
	case bu_week:              return "wk";
	case bu_year:              return "yr";
	case bu_inch:              return "in";
	case bu_foot:              return "ft";
	case bu_yard:              return "yd";
	case bu_mile:              return "mi";
	case bu_nautical_mile:     return "nmi";
	case bu_angstrom:          return "\xe2\x84\xab";
	case bu_light_year:        return "ly";
	case bu_parsec:            return "pc";
	case bu_furlong:           return "fur";
	case bu_fathom:            return "fath";
	case bu_pound:             return "lb";
	case bu_ounce:             return "oz";
	case bu_grain:             return "gr";
	case bu_stone:             return "st";
	case bu_short_ton:         return "tn_sh";
	case bu_long_ton:          return "tn_l";
	case bu_troy_ounce:        return "oz_t";
	case bu_carat:             return "ct";
	case bu_fahrenheit:        return "\xc2\xb0""F";
	case bu_atmosphere:        return "atm";
	case bu_mmhg:              return "mmHg";
	case bu_torr:              return "Torr";
	case bu_psi:               return "psi";
	case bu_calorie:           return "cal";
	case bu_btu:               return "Btu";
	case bu_erg:               return "erg";
	case bu_therm:             return "thm";
	case bu_horsepower:        return "hp";
	case bu_pound_force:       return "lbf";
	case bu_dyne:              return "dyn";
	case bu_kip:               return "kip";
	case bu_knot:              return "kn";
	case bu_gallon:            return "gal";
	case bu_gallon_uk:         return "gal_uk";
	case bu_quart:             return "qt";
	case bu_pint:              return "pt";
	case bu_cup:               return "cup";
	case bu_fluid_ounce:       return "fl_oz";
	case bu_tablespoon:        return "tbsp";
	case bu_teaspoon:          return "tsp";
	case bu_barrel:            return "bbl";
	case bu_acre:              return "ac";
	case bu_barn:              return "barn";
	case bu_arcminute:         return "arcmin";
	case bu_arcsecond:         return "arcsec";
	case bu_grad:              return "grad";
	case bu_poise:             return "P";
	case bu_stokes:            return "St";
	case bu_gauss:             return "G";
	case bu_maxwell:           return "Mx";
	case bu_oersted:           return "Oe";
	case bu_stilb:             return "sb";
	case bu_phot:              return "ph";
	case bu_galileo:           return "Gal";
	case bu_curie:             return "Ci";
	case bu_roentgen:          return "R";
	case bu_rem:               return "rem";
	case bu_neper:             return "Np";
	case bu_decibel:           return "dB";
	case bu_rankine:           return "\xc2\xb0Ra";
	case bu_delisle:           return "\xc2\xb0""De";
	case bu_newton_temp:       return "\xc2\xb0""N";
	case bu_reaumur:           return "\xc2\xb0""Re";
	case bu_romer:             return "\xc2\xb0""Ro";
	case bu_percent:           return "%";
	case bu_per_mille:         return "\xe2\x80\xb0";
	case bu_per_myriad:        return "\xe2\x80\xb1";
	case bu_per_cent_mille:    return "pcm";
	case bu_ppm:               return "ppm";
	case bu_ppb:               return "ppb";
	case bu_slug:              return "slug";
	case bu_thou:              return "thou";
	case bu_pint_uk:           return "pt_uk";
	case bu_fluid_ounce_uk:    return "fl_oz_uk";
	case bu_quart_uk:          return "qt_uk";
	case bu_var:               return "var";
	case bu_volt_ampere:       return "VA";
	case bu_kilogram_force:    return "kgf";
	case bu_inch_hg:           return "inHg";
	case bu_rpm:               return "rpm";
	case bu_foot_pound:        return "ft_lb";
	case bu_dram:              return "dr";
	case bu_pennyweight:       return "dwt";
	case bu_chain:             return "ch";
	case bu_rod:               return "rd";
	case bu_gill:              return "gi";
	case bu_gill_uk:           return "gi_uk";
	case bu_standard_gravity:  return "gn";
	case bu_metric_horsepower: return "PS";
	case bu_revolution:        return "rev";
	case bu_month:             return "mo";
	case bu_fortnight:         return "fn";
	case bu_atmosphere_technical: return "at";
	case bu_tex:               return "tex";
	case bu_denier:            return "den";
	case bu_fluid_dram:        return "fl_dr";
	case bu_minim:             return "minim";
	case bu_peck:              return "pk";
	case bu_bushel:            return "bsh";
	case bu_pfund:             return "Pfd";
	case bu_zentner:           return "Ztr";
	case bu_doppelzentner:     return "dz";
	case bu_lot:               return "lot";
	case bu_prussian_line:     return "prln";
	case bu_prussian_zoll:     return "prz";
	case bu_prussian_fuss:     return "prf";
	case bu_prussian_elle:     return "elle";
	case bu_prussian_rute:     return "rute";
	case bu_klafter:           return "klafter";
	case bu_german_mile:       return "dt_mi";
	case bu_morgen:            return "morgen";
	case bu_scheffel:          return "schffl";
	case bu_survey_foot:       return "ftUS";
	case bu_league:            return "lea";
	case bu_cable:             return "cbl";
	case bu_hand:              return "hand";
	case bu_quintal:           return "qntl";
	case bu_scruple:           return "sc";
	case bu_baud:              return "Bd";
	default:
		if (bvn_unit_is_currency((int)b)) {
			const bvn_currency_info_t *info = bvn_currency_info((int)b);
			if (info) return info->code;
		}
		return "";
	}
}
/*
 * Render an exponent as a Unicode superscript suffix (e.g. ² ³ ⁻²). This is the
 * default, pretty form; bvn_write_exponent_suffix_ascii produces the plain
 * "^2" / "^-2" form selected by BVN_UNIT_ASCII_EXP for ASCII-only consumers.
 * exp_linear writes nothing (the implied exponent 1 is never spelled out).
 */
static int32_t bvn_write_exponent_suffix(
	char* buf, size_t bufsize, unit_exponent_t e)
{
	int32_t pos = 0;
	switch (e) {
	case exp_linear:
		break;
	case exp_square:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb2;
		break;
	case exp_cubic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb3;
		break;
	case exp_quartic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb4;
		break;
	case exp_quintic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb5;
		break;
	case exp_sextic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb6;
		break;
	case exp_septic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb7;
		break;
	case exp_octic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb8;
		break;
	case exp_nonic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb9;
		break;
	case exp_neg_linear:
		if (pos + 5 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb9;
		break;
	case exp_neg_square:
		if (pos + 5 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb2;
		break;
	case exp_neg_cubic:
		if (pos + 5 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb3;
		break;
	case exp_neg_quartic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb4;
		break;
	case exp_neg_quintic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb5;
		break;
	case exp_neg_sextic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb6;
		break;
	case exp_neg_septic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb7;
		break;
	case exp_neg_octic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb8;
		break;
	case exp_neg_nonic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb9;
		break;
	case exp_invalid:
		return -1;
	}
	buf[pos] = '\0';
	return pos;
}
static int32_t bvn_write_exponent_suffix_ascii(
	char* buf, size_t bufsize, unit_exponent_t e)
{
	int32_t pos = 0;
	switch (e) {
	case exp_linear:
		break;
	case exp_square:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '2';
		break;
	case exp_cubic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '3';
		break;
	case exp_quartic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '4';
		break;
	case exp_quintic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '5';
		break;
	case exp_sextic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '6';
		break;
	case exp_septic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '7';
		break;
	case exp_octic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '8';
		break;
	case exp_nonic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '9';
		break;
	case exp_neg_linear:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '1';
		break;
	case exp_neg_square:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '2';
		break;
	case exp_neg_cubic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '3';
		break;
	case exp_neg_quartic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '4';
		break;
	case exp_neg_quintic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '5';
		break;
	case exp_neg_sextic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '6';
		break;
	case exp_neg_septic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '7';
		break;
	case exp_neg_octic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '8';
		break;
	case exp_neg_nonic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '9';
		break;
	case exp_invalid:
		return -1;
	}
	buf[pos] = '\0';
	return pos;
}
/*
 * Format one component as "[prefix~]base[exp]" — the inverse of
 * bvn_parse_single_unit_component. _ex is the same but routes the exponent
 * through the ASCII or Unicode writer per flags. Every write is bounds-checked
 * against bufsize and returns -1 on overflow so the caller can surface
 * error_unit_too_long rather than truncate.
 */
static int32_t bvn_write_unit_component(
	char* buf, size_t bufsize, const value_unit_component_t* c)
{
	int32_t pos = 0;
	bool has_prefix = false;
	if (c->prefix.system == prefix_iec && c->prefix.id.iec != iec_none) {
		const char* p = iec_prefix_str(c->prefix.id.iec);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	} else if (c->prefix.system == prefix_si && c->prefix.id.si != si_none) {
		const char* p = si_prefix_str(c->prefix.id.si);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	}
	if (has_prefix) {
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '~';
	}
	if (bvn_unit_is_currency((int)c->base)) {
		/* Mandatory currency sigil (spec 1.0) so output round-trips: "$USD",
		 * and "<prefix>~$EUR" once the prefix/'~' above is already written. */
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '$';
	}
	{
		const char* bu = base_unit_str(c->base);
		size_t bulen = strlen(bu);
		if (pos + (int32_t)bulen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, bu, bulen);
		pos += (int32_t)bulen;
	}
	int32_t w = bvn_write_exponent_suffix(
		buf + pos, bufsize - (size_t)pos,
		c->exponent);
	if (w < 0) return -1;
	pos += w;
	return pos;
}
static int32_t bvn_write_unit_component_ex(
	char* buf, size_t bufsize,
	const value_unit_component_t* c,
	bvn_unit_flags_t flags)
{
	int32_t pos = 0;
	bool has_prefix = false;
	if (c->prefix.system == prefix_iec && c->prefix.id.iec != iec_none) {
		const char* p = iec_prefix_str(c->prefix.id.iec);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	} else if (c->prefix.system == prefix_si && c->prefix.id.si != si_none) {
		const char* p = si_prefix_str(c->prefix.id.si);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	}
	if (has_prefix) {
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '~';
	}
	if (bvn_unit_is_currency((int)c->base)) {
		/* Mandatory currency sigil (spec 1.0) so output round-trips: "$USD",
		 * and "<prefix>~$EUR" once the prefix/'~' above is already written. */
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '$';
	}
	{
		const char* bu = base_unit_str(c->base);
		size_t bulen = strlen(bu);
		if (pos + (int32_t)bulen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, bu, bulen);
		pos += (int32_t)bulen;
	}
	int32_t w;
	if (flags & BVN_UNIT_ASCII_EXP)
		w = bvn_write_exponent_suffix_ascii(buf + pos,
		                                    bufsize - (size_t)pos,
		                                    c->exponent);
	else
		w = bvn_write_exponent_suffix(buf + pos,
		                               bufsize - (size_t)pos,
		                               c->exponent);
	if (w < 0) return -1;
	pos += w;
	return pos;
}
/*
 * Exact identity of a single unit component (base, exponent, prefix). Helper
 * for the order-insensitive bvn_unit_equal below.
 */
static bool bvn_unit_component_identical(const value_unit_component_t *ca,
                                         const value_unit_component_t *cb)
{
	if (ca->base     != cb->base)     return false;
	if (ca->exponent != cb->exponent) return false;
	if (ca->prefix.system != cb->prefix.system) return false;
	if (ca->prefix.system == prefix_si)
		return ca->prefix.id.si == cb->prefix.id.si;
	return ca->prefix.id.iec == cb->prefix.id.iec;
}
/*
 * Structural equality of two units. This is exact component identity (base,
 * exponent, prefix) — not dimensional equivalence — but it is ORDER-INSENSITIVE:
 * unit multiplication is commutative, so "s³·m⁻⁵" and "m⁻⁵·s³" denote the same
 * unit and must compare equal. The parser preserves source order in the
 * value_unit_t component array, so a positional comparison would wrongly reject
 * two spellings of the same unit (e.g. an annotation unit vs an inline unit in a
 * different order), violating the spec rule that logically equivalent notations
 * compare as equal. We therefore match components as multisets: every component
 * of a must pair with a distinct, identical component of b.
 */
bool bvn_unit_equal(value_unit_t a, value_unit_t b)
{
	if (a.num_components != b.num_components)
		return false;
	uint32_t n = a.num_components < BVNR_MAX_UNIT_COMPONENTS
	           ? a.num_components : BVNR_MAX_UNIT_COMPONENTS;
	bool matched[BVNR_MAX_UNIT_COMPONENTS] = { false };
	for (uint32_t i = 0; i < n; i++) {
		bool found = false;
		for (uint32_t j = 0; j < n; j++) {
			if (matched[j])
				continue;
			if (bvn_unit_component_identical(&a.components[i],
			                                 &b.components[j])) {
				matched[j] = true;
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}
bool bvn_unit_valid(value_unit_t u)
{
	if (u.num_components > BVNR_MAX_UNIT_COMPONENTS)
		return false;
	for (uint32_t i = 0; i < u.num_components; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->exponent == exp_invalid)
			return false;
		if ((uint32_t)c->base >= BVN_VALUE_BASE_UNIT_COUNT)
			return false;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			return false;
	}
	return true;
}
static const value_base_unit_t bvni_si_named_derived[] = {
	bu_newton, bu_pascal, bu_joule, bu_watt,
	bu_volt, bu_ohm, bu_farad, bu_coulomb, bu_siemens,
	bu_weber, bu_tesla, bu_henry, bu_lux, bu_lumen,
	bu_hertz, bu_becquerel, bu_gray, bu_sievert, bu_katal,
};
#define BVNI_SI_NAMED_DERIVED_COUNT \
	((uint32_t)(sizeof(bvni_si_named_derived)/sizeof(bvni_si_named_derived[0])))
static int32_t bvni_pexp_to_si_prefix_id(int32_t pexp)
{
	for (uint32_t i = 0; i < BVN_SI_PREFIX_COUNT; i++) {
		if (bvni_si_pfx_table[i].exp == pexp)
			return (int32_t)i;
	}
	return -1;
}
/*
 * Try to collapse a reduced unit back into a single named SI derived unit with
 * the right prefix (e.g. kg·m/s² -> N, or 1000 of those -> kN). It computes the
 * unit's SI dimension vector and net scale factor, finds a named derived unit
 * (bvni_si_named_derived) with the same dimensions, and — if the leftover scale
 * is an exact power of ten that maps to an SI prefix — emits that prefixed
 * named unit. Only invoked under BVN_UNIT_REDUCE; affine units (those with an
 * offset, like °C) are left alone because they can't be rescaled by a factor.
 */
static value_unit_t bvni_reduce_to_named_si(value_unit_t u, double scale)
{
	bool aff, ok;
	double off;
	double base_si = bvn_unit_to_si_factor(u, &aff, &off, &ok);
	if (!ok || aff)
		return u;
	double net_si = scale * base_si;
	if (net_si <= 0.0 || !isfinite(net_si))
		return u;
	int32_t dim_r[bvn_si_dim_count];
	if (!bvn_unit_dimension_vector(u, dim_r))
		return u;
	for (uint32_t n = 0; n < BVNI_SI_NAMED_DERIVED_COUNT; n++) {
		value_base_unit_t nd = bvni_si_named_derived[n];
		value_unit_t probe;
		memset(&probe, 0, sizeof(probe));
		probe.num_components = 1;
		probe.components[0].base      = nd;
		probe.components[0].exponent  = exp_linear;
		probe.components[0].prefix.system   = prefix_si;
		probe.components[0].prefix.id.si    = si_none;
		int32_t dim_nd[bvn_si_dim_count];
		if (!bvn_unit_dimension_vector(probe, dim_nd))
			continue;
		bool dim_match = true;
		for (int d = 0; d < bvn_si_dim_count; d++) {
			if (dim_r[d] != dim_nd[d]) {
				dim_match = false;
				break;
			}
		}
		if (!dim_match)
			continue;
		double log_ratio = log10(net_si);
		int32_t pexp = (int32_t)round(log_ratio);
		if (fabs(log_ratio - (double)pexp) > 1e-6)
			continue;
		int32_t pfx_id = bvni_pexp_to_si_prefix_id(pexp);
		if (pfx_id < 0)
			continue;
		value_unit_t result;
		memset(&result, 0, sizeof(result));
		result.num_components = 1;
		result.components[0].base           = nd;
		result.components[0].exponent       = exp_linear;
		result.components[0].prefix.system  = prefix_si;
		result.components[0].prefix.id.si   = (si_prefix_id_t)pfx_id;
		return result;
	}
	return u;
}
/*
 * Format a whole unit to text. The empty unit prints as "no_unit". A
 * multi-component unit is grouped into numerator (positive exponents) and
 * denominator (negative exponents): if both are present it prints
 * "a·b/c·d" with denominator exponents negated back to positive; otherwise it
 * prints a flat "·"-joined product. The separator is '·' (U+00B7) normally or
 * '*' under BVN_UNIT_ASCII_EXP. With BVN_UNIT_REDUCE it first reduces to SI base
 * units and attempts the named-SI collapse above. bvn_unit_to_string is the
 * plain wrapper with default (Unicode, non-reducing) flags.
 */
int32_t bvn_unit_to_string_ex(value_unit_t u, char* buf, size_t bufsize,
                               bvn_unit_flags_t flags)
{
	if (!buf || bufsize < 1)
		return -1;
	if (flags & BVN_UNIT_REDUCE) {
		double   scale;
		bool     overflow;
		if (!bvn_unit_valid(u))
			return -1;
		u = bvn_unit_reduce(u, &scale, &overflow);
		if (!overflow)
			u = bvni_reduce_to_named_si(u, scale);
	} else {
		if (!bvn_unit_valid(u))
			return -1;
	}
	uint32_t nc = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	            ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	if (nc == 0) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	if (nc == 1
		&& u.components[0].base == bu_none
		&& u.components[0].prefix.system == prefix_si
		&& u.components[0].prefix.id.si == si_none
		&& u.components[0].exponent == exp_linear) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	bool ascii = (flags & BVN_UNIT_ASCII_EXP) != 0;
	if (nc == 1) {
		return bvn_write_unit_component_ex(
			buf, bufsize, &u.components[0], flags);
	}
	uint32_t num_num = 0, num_den = 0;
	uint32_t num_idx[BVNR_MAX_UNIT_COMPONENTS];
	uint32_t den_idx[BVNR_MAX_UNIT_COMPONENTS];
	for (uint32_t i = 0; i < nc; i++) {
		if (bvni_is_neg_exp(u.components[i].exponent))
			den_idx[num_den++] = i;
		else
			num_idx[num_num++] = i;
	}
	int32_t pos = 0;
	if (num_num > 0 && num_den > 0) {
		for (uint32_t n = 0; n < num_num; n++) {
			if (n > 0) {
				if (ascii) {
					if (pos + 1 >= (int32_t)bufsize) return -1;
					buf[pos++] = '*';
				} else {
					if (pos + 2 >= (int32_t)bufsize) return -1;
					buf[pos++] = (char)0xc2;
					buf[pos++] = (char)0xb7;
				}
			}
			int32_t w = bvn_write_unit_component_ex(
				buf + pos, bufsize - (size_t)pos,
				&u.components[num_idx[n]], flags);
			if (w < 0) return -1;
			pos += w;
		}
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '/';
		for (uint32_t d = 0; d < num_den; d++) {
			if (d > 0) {
				if (ascii) {
					if (pos + 1 >= (int32_t)bufsize) return -1;
					buf[pos++] = '*';
				} else {
					if (pos + 2 >= (int32_t)bufsize) return -1;
					buf[pos++] = (char)0xc2;
					buf[pos++] = (char)0xb7;
				}
			}
			value_unit_component_t tmp = u.components[den_idx[d]];
			tmp.exponent = bvn_negate_exponent(tmp.exponent);
			int32_t w = bvn_write_unit_component_ex(
				buf + pos, bufsize - (size_t)pos,
				&tmp, flags);
			if (w < 0) return -1;
			pos += w;
		}
	} else {
		for (uint32_t i = 0; i < nc; i++) {
			if (i > 0) {
				if (ascii) {
					if (pos + 1 >= (int32_t)bufsize) return -1;
					buf[pos++] = '*';
				} else {
					if (pos + 2 >= (int32_t)bufsize) return -1;
					buf[pos++] = (char)0xc2;
					buf[pos++] = (char)0xb7;
				}
			}
			int32_t w = bvn_write_unit_component_ex(
				buf + pos, bufsize - (size_t)pos,
				&u.components[i], flags);
			if (w < 0) return -1;
			pos += w;
		}
	}
	buf[pos] = '\0';
	return pos;
}
int32_t bvn_unit_to_string(value_unit_t u, char* buf, size_t bufsize)
{
	if (!buf || bufsize < 1)
		return -1;
	if (!bvn_unit_valid(u))
		return -1;
	uint32_t nc = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	            ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	if (nc == 0) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	if (nc == 1
		&& u.components[0].base == bu_none
		&& u.components[0].prefix.system == prefix_si
		&& u.components[0].prefix.id.si == si_none
		&& u.components[0].exponent == exp_linear) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	if (nc == 1) {
		return bvn_write_unit_component(
			buf, bufsize, &u.components[0]);
	}
	uint32_t num_num = 0, num_den = 0;
	uint32_t num_idx[BVNR_MAX_UNIT_COMPONENTS];
	uint32_t den_idx[BVNR_MAX_UNIT_COMPONENTS];
	for (uint32_t i = 0; i < nc; i++) {
		if (bvni_is_neg_exp(u.components[i].exponent)) {
			den_idx[num_den++] = i;
		} else {
			num_idx[num_num++] = i;
		}
	}
	int32_t pos = 0;
	if (num_num > 0 && num_den > 0) {
		for (uint32_t n = 0; n < num_num; n++) {
			if (n > 0) {
				if (pos + 2 >= (int32_t)bufsize) return -1;
				buf[pos++] = (char)0xc2;
				buf[pos++] = (char)0xb7;
			}
			int32_t w = bvn_write_unit_component(
				buf + pos, bufsize - (size_t)pos,
				&u.components[num_idx[n]]);
			if (w < 0) return -1;
			pos += w;
		}
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '/';
		for (uint32_t d = 0; d < num_den; d++) {
			if (d > 0) {
				if (pos + 2 >= (int32_t)bufsize) return -1;
				buf[pos++] = (char)0xc2;
				buf[pos++] = (char)0xb7;
			}
			value_unit_component_t tmp = u.components[den_idx[d]];
			tmp.exponent = bvn_negate_exponent(tmp.exponent);
			int32_t w = bvn_write_unit_component(
				buf + pos, bufsize - (size_t)pos,
				&tmp);
			if (w < 0) return -1;
			pos += w;
		}
	} else {
		for (uint32_t i = 0; i < nc; i++) {
			if (i > 0) {
				if (pos + 2 >= (int32_t)bufsize) return -1;
				buf[pos++] = (char)0xc2;
				buf[pos++] = (char)0xb7;
			}
			int32_t w = bvn_write_unit_component(
				buf + pos, bufsize - (size_t)pos,
				&u.components[i]);
			if (w < 0) return -1;
			pos += w;
		}
	}
	buf[pos] = '\0';
	return pos;
}
static double bvn_single_component_factor(value_unit_component_t c)
{
	double pf      = bvni_prefix_factor(c);
	int32_t abs_exp = bvni_exp_abs(c.exponent);
	double result  = pow(pf, (double)abs_exp);
	if (bvni_is_neg_exp(c.exponent))
		result = 1.0 / result;
	return result;
}
/*
 * The multiplicative scale a unit's prefixes contribute (e.g. "km" -> 1000),
 * folding each component's prefix factor raised to its exponent. Lets a
 * consumer convert a stored value to base-unit magnitude.
 * bvn_unit_prefix_exponent is the base-10 log of the same, for callers that
 * prefer an integer power of ten over a floating factor.
 */
double bvn_unit_prefix_factor(value_unit_t u)
{
	double f = 1.0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		if (u.components[i].exponent == exp_invalid)
			continue;
		f *= bvn_single_component_factor(u.components[i]);
	}
	return f;
}
int32_t bvn_unit_prefix_exponent(value_unit_t u)
{
	int32_t total = 0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		if (u.components[i].exponent == exp_invalid)
			continue;
		total += bvni_prefix_exp_int(u.components[i]);
	}
	return total;
}
/*
 * Parse the body of a type annotation (the text inside `<...>`, e.g.
 * "uint:32,_16,kg") into a value_type_spec_t plus its unit.
 *
 * Grammar: a family keyword, then optional `:`-introduced, `,`-separated
 * parameters in any order — a bare number is the bit width, `_N` is the numeric
 * base (rejected for the decimal float families), `qN` is the fixed-point
 * fraction-bit count, and anything else is the unit string. The four out-flags
 * separate the kinds of failure the caller must distinguish: an unparseable
 * type (type_ok) is fatal, whereas a bad/oversized unit (unit_ok /
 * unit_too_long) may be tolerated for utf8 where a trailing token isn't a unit.
 * Returning these separately is what lets the validator produce precise
 * error_unit_illegal vs error_unit_too_long vs error_illegal_value_type codes.
 */
value_type_spec_t bvn_parse_type_annotation(
	const uint8_t* str, uint32_t len,
	bool* type_ok, bool* unit_ok, bool* unit_too_long,
	value_unit_t* out_unit,
	uint8_t* unit_buf, uint8_t* unit_buf_len)
{
	value_type_spec_t r = BVN_TYPE_PLAIN;
	*type_ok      = true;
	*unit_ok      = true;
	*unit_too_long = false;
	*unit_buf_len = 0;
	*out_unit     = BVN_UNIT_NONE;
	if (!str || !len) { *type_ok = false; return r; }
	uint32_t pos = 0;
	if (pos + 9 <= len && memcmp(str + pos, "float_fix", 9) == 0) {
		r.family = vt_float_fix; pos += 9;
	} else if (pos + 9 <= len && memcmp(str + pos, "float_dec", 9) == 0) {
		r.family = vt_float_dec; pos += 9;
	} else if (pos + 5 <= len && memcmp(str + pos, "float", 5) == 0) {
		r.family = vt_float; pos += 5;
	} else if (pos + 4 <= len && memcmp(str + pos, "uint", 4) == 0) {
		r.family = vt_uint; pos += 4;
	} else if (pos + 4 <= len && memcmp(str + pos, "utf8", 4) == 0) {
		r.family = vt_utf8; pos += 4;
	} else if (pos + 4 <= len && memcmp(str + pos, "bool", 4) == 0) {
		r.family = vt_bool; pos += 4;
	} else if (pos + 4 <= len && memcmp(str + pos, "sint", 4) == 0) {
		r.family = vt_sint; pos += 4;
	} else {
		*type_ok = false;
		return r;
	}
	if (pos < len) {
		if (str[pos] != ':') {
			*type_ok = false;
			return r;
		}
		pos++;
		while (pos < len && str[pos] == ' ') pos++;
		/* A ":" must introduce at least one parameter; "<uint:>" with an
		 * empty list is rejected (param-type = family [":" type-param-list],
		 * the list being mandatory once the colon is present). */
		if (pos >= len) {
			*type_ok = false;
			return r;
		}
	}
	/* Each parameter class (width, base, q-fraction, unit) may appear at most
	 * once; a repeated class is error_illegal_value_type. Without these guards
	 * a later occurrence silently overwrites the earlier one (last-wins), which
	 * makes acceptance order-dependent and can mask a real violation — e.g.
	 * "<uint:8,16> 300" would otherwise be accepted with width 16, hiding that
	 * 300 does not fit uint:8. */
	bool have_width = false, have_base = false, have_q = false, have_unit = false;
	bool need_comma = false;
	while (pos < len) {
		if (need_comma) {
			if (str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			pos++;
			while (pos < len && str[pos] == ' ') pos++;
		}
		need_comma = true;
		/* A comma must be followed by a real parameter. Empty or
		 * trailing components (<uint:8,>, <uint:8,,>, <uint:,_16>) are
		 * rejected to match the strict type-param-list grammar
		 * (type-param-list = type-param , {ws "," ws type-param}); since
		 * parameters are class-identified and order-independent, empty
		 * positional slots are never required. */
		if (pos >= len || str[pos] == ',') {
			*type_ok = false;
			return r;
		}
		uint8_t c = str[pos];
		if (c >= '0' && c <= '9') {
			if (have_width) { *type_ok = false; return r; }
			have_width = true;
			uint64_t w = 0;
			while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
				uint32_t d = (uint32_t)(str[pos] - '0');
				if (w > ((uint64_t)UINT32_MAX - d) / 10u) {
					*type_ok = false;
					return r;
				}
				w = w * 10u + d;
				pos++;
			}
			if (pos < len && str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			r.width = (uint32_t)w;
		} else if (c == '_') {
			if (have_base) { *type_ok = false; return r; }
			have_base = true;
			pos++;
			if (pos >= len || str[pos] < '0' || str[pos] > '9') {
				*type_ok = false;
				return r;
			}
			uint32_t b = 0;
			while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
				b = b * 10u + (uint32_t)(str[pos] - '0');
				if (b > 10000u) {
					*type_ok = false;
					return r;
				}
				pos++;
			}
			if (pos < len && str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			if (r.family == vt_float_fix || r.family == vt_float_dec) {
				*type_ok = false;
				return r;
			}
			if (!((b >= 2u && b <= 62u) || b == 64u || b == 85u)) {
				*type_ok = false;
				return r;
			}
			r.base = b;
		} else if (c == 'q' && pos + 1 < len &&
				   str[pos + 1] >= '0' && str[pos + 1] <= '9') {
			if (r.family != vt_float_fix) {
				*type_ok = false;
				return r;
			}
			if (have_q) { *type_ok = false; return r; }
			have_q = true;
			pos++;
			uint32_t q = 0;
			while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
				q = q * 10u + (uint32_t)(str[pos] - '0');
				if (q > 256u) {
					*type_ok = false;
					return r;
				}
				pos++;
			}
			if (pos < len && str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			r.base = q;
		} else {
			if (have_unit) { *type_ok = false; return r; }
			have_unit = true;
			uint32_t ustart = pos;
			while (pos < len && str[pos] != ',')
				pos++;
			uint32_t ulen = pos - ustart;
			if (ulen > UINT8_MAX) {
				*unit_ok      = false;
				*unit_too_long = true;
				return r;
			}
			memcpy(unit_buf, str + ustart, ulen);
			unit_buf[ulen] = '\0';
			*unit_buf_len  = (uint8_t)ulen;
			if (ulen == 7u && memcmp(unit_buf, "no_unit", 7) == 0) {
				*out_unit = BVN_UNIT_NONE;
			} else {
				bool uok = true;
				*out_unit = bvn_parse_unit_n(unit_buf, ulen, &uok);
				if (!uok)
					*unit_ok = false;
			}
		}
	}
	if (r.family == vt_sint || r.family == vt_uint) {
		if (r.width > BVN_MAX_INT_WIDTH) {
			*type_ok = false;
			return r;
		}
		/* Bases 64 and 85 use '+'/'-' as digit characters, leaving no sign
		 * character available, so they are unsigned-only: signed integers in
		 * those bases are illegal. */
		if (r.family == vt_sint && (r.base == 64u || r.base == 85u)) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_float) {
		if (r.base != 0u && r.base != 10u && r.base != 16u) {
			*type_ok = false;
			return r;
		}
		if (r.width != 0u && r.width != 16u &&
			(r.width % 32u != 0u || r.width > BVN_FLOAT_MAX_PREC)) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_float_fix || r.family == vt_float_dec) {
		if (r.width != 0u  && r.width != 16u  && r.width != 32u &&
			r.width != 64u && r.width != 128u && r.width != 256u) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_float_fix) {
		uint32_t eff_w = r.width ? r.width : 64u;
		if (r.base >= eff_w) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_bool || r.family == vt_utf8) {
		/* bool and utf8 are parameterless families: a width, base, q, or
		 * unit parameter is error_illegal_value_type. (q is already
		 * rejected above for any non-float_fix family.) */
		if (r.width != 0u || r.base != 0u || *unit_buf_len != 0u) {
			*type_ok = false;
			return r;
		}
	}
	return r;
}
/*
 * Identifier well-formedness: must start with a letter, '_' or a UTF-8
 * multibyte lead, and may not contain whitespace, controls, or any of the
 * characters the grammar reserves for punctuation (the explicit reject set).
 * The trailing bvn_validate_string call also guarantees the whole thing is
 * valid UTF-8. bvn_validate_symbol reuses the same rules; bvn_validate_reference
 * applies them to each dot-separated segment of a `.a.b.c` path.
 */
bool bvn_validate_identifier(const char* id)
{
	if (!id || !*id) return false;
	if (!((*id >= 'a' && *id <= 'z') || (*id >= 'A' && *id <= 'Z') ||
		  *id == '_' ||
		  ((uint8_t)*id >= 0xc3 && (uint8_t)*id <= 0xf4)))
		return false;
	for (const char* p = id + 1; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c <= 0x20 || c == 0x7f || c == 0xc2) return false;
		if (c == '"' || c == '#' || c == ',' || c == '.' ||
			c == '/' || c == ';' || c == '<' || c == '=' ||
			c == '>' || c == '[' || c == ']' || c == '{' ||
			c == '}' || c == '!' || c == '$' || c == '%' ||
			c == '&' || c == '\''|| c == '(' || c == ')' ||
			c == '*' || c == ':' || c == '?' || c == '@' ||
			c == '\\'|| c == '^' || c == '`' || c == '|' ||
			c == '~')
			return false;
	}
	return bvn_validate_string((const uint8_t*)id, strlen(id));
}
bool bvn_validate_symbol(const char* surr)
{
	if (!surr || !*surr) return false;
	return bvn_validate_identifier(surr);
}
bool bvn_validate_reference(const char* link)
{
	if (!link || !*link) return false;
	const char* p = link;
	if (*p != '.') return false;
	do {
		p++;
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			  *p == '_' ||
			  ((uint8_t)*p >= 0xc3 && (uint8_t)*p <= 0xf4)))
			return false;
		p++;
		while (*p && *p != '.') {
			unsigned char c = (unsigned char)*p;
			if (c <= 0x20 || c == 0x7f || c == 0xc2) return false;
			if (c == '"' || c == '#' || c == ',' || c == '/' ||
				c == ';' || c == '<' || c == '=' || c == '>' ||
				c == '[' || c == ']' || c == '{' || c == '}' ||
				c == '!' || c == '$' || c == '%' || c == '&' ||
				c == '\''|| c == '(' || c == ')' || c == '*' ||
				c == ':' || c == '?' || c == '@' || c == '\\' ||
				c == '^' || c == '`' || c == '|' || c == '~')
				return false;
			p++;
		}
	} while (*p == '.');
	return true;
}
bool bvn_validate_number(const char* s)
{
	if (!s || !*s) return false;
	const char* p = s;
	if (*p == '-') p++;
	if (!*p) return false;
	bool has_dot = false, has_exp = false;
	bool has_mant_dig = false, has_exp_dig = false;
	for (; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			if (has_exp) has_exp_dig = true;
			else         has_mant_dig = true;
			continue;
		}
		if (*p == '.') { if (has_dot || has_exp) return false; has_dot = true; continue; }
		if (*p == 'e' || *p == 'E') {
			if (has_exp || !has_mant_dig) return false;
			has_exp = true;
			if (p[1] == '+' || p[1] == '-') p++;
			continue;
		}
		return false;
	}
	return has_mant_dig && (!has_exp || has_exp_dig);
}
/*
 * Format an unsigned integer in any supported base, left-padded with leading
 * zeros to min_digits. Digits are produced least-significant-first into a
 * stack scratch buffer and then copied out in order — the standard trick that
 * avoids needing to know the length up front. Returns -1 if the base is
 * unsupported or the output buffer is too small (never truncates silently).
 * bvn_format_int64 prepends '-' and formats the magnitude, taking care with
 * INT64_MIN whose magnitude doesn't fit in int64.
 */
int32_t bvn_format_uint64(char* buf, size_t bufsize, uint64_t value,
						  uint32_t base, uint32_t min_digits)
{
	bool valid_base = (base >= 2u && base <= 62u) ||
					  base == 64u || base == 85u;
	if (!valid_base || !buf || bufsize < 2) return -1;
	if (min_digits > 64u) min_digits = 64u;
	char     tmp[65];
	int      pos      = 64;
	uint32_t zero_ch  = bvn_digit_to_char(0u, base);
	tmp[pos] = '\0';
	if (value == 0) {
		tmp[--pos] = (char)zero_ch;
	} else {
		while (value) {
			uint32_t d  = (uint32_t)(value % base);
			uint32_t ch = bvn_digit_to_char(d, base);
			if (!ch) return -1;
			tmp[--pos] = (char)ch;
			value /= base;
		}
	}
	while ((64 - pos) < (int)min_digits) tmp[--pos] = (char)zero_ch;
	int len = 64 - pos;
	if ((size_t)(len + 1) > bufsize) return -1;
	memcpy(buf, tmp + pos, (size_t)len + 1);
	return len;
}
int32_t bvn_format_int64(char* buf, size_t bufsize, int64_t value,
						 uint32_t base, uint32_t min_digits)
{
	bool neg = (value < 0);
	/* Bases 64 and 85 use '+'/'-' as digits and so have no sign character; a
	 * negative value cannot be represented (callers gate this via uint-only
	 * type validation, but guard here too rather than emit a misreadable '-'). */
	if (neg && (base == 64u || base == 85u)) return -1;
	if (neg) {
		if (bufsize < 2) return -1;
		buf[0] = '-';
		uint64_t abs_val = (uint64_t)0 - (uint64_t)value;
		int32_t r = bvn_format_uint64(buf + 1, bufsize - 1,
									  abs_val, base, min_digits);
		return r < 0 ? r : r + 1;
	}
	return bvn_format_uint64(buf, bufsize, (uint64_t)value, base, min_digits);
}
uint32_t bvn_min_digits_for_type(value_type_spec_t vt)
{
	if (!bvn_type_is_numeric(vt) || !vt.width) return 0;
	uint32_t w = vt.width;
	uint32_t base = bvn_effective_base(vt);
	uint32_t digits = 1;
	if (w > 64u) return 0u;
	uint64_t maxv = (vt.family == vt_sint)
		? ((uint64_t)1u << (w - 1u)) - 1u
		: (w >= 64u) ? UINT64_MAX : ((uint64_t)1u << w) - 1u;
	uint64_t v = maxv;
	while (v >= base) { v /= base; digits++; }
	return digits;
}
/*
 * Parse a signed integer in the type's effective base into int64. Bases 2-36
 * defer to the libc strtoll (fast, locale-free here); larger bases (up to 62,
 * plus 64/85) are parsed by hand via bvn_char_to_digit because strtoll doesn't
 * know those alphabets. Overflow is checked before it happens, and INT64_MIN is
 * handled specially since its magnitude is one past INT64_MAX. The uint64 and
 * double parsers follow the same base-split structure.
 */
bool bvn_parse_int64(const char* s, value_type_spec_t vt, int64_t* out)
{
	if (!s || !out) return false;
	uint32_t base = bvn_effective_base(vt);
	if (base >= 2u && base <= 36u) {
		char* end;
		errno = 0;
		long long v = strtoll(s, &end, (int)base);
		if (*end || errno == ERANGE) return false;
		*out = (int64_t)v;
		return true;
	}
	bool neg = (s[0] == '-');
	const char* p = s + (neg ? 1u : 0u);
	if (!*p) return false;
	uint64_t acc = 0;
	while (*p) {
		uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
		if (d >= base) return false;
		if (acc > (UINT64_MAX - d) / base) return false;
		acc = acc * base + d;
		p++;
	}
	if (neg) {
		if (acc > (uint64_t)INT64_MAX + 1u) return false;
		*out = (acc == (uint64_t)INT64_MAX + 1u)
		       ? INT64_MIN
		       : -(int64_t)acc;
	} else {
		if (acc > (uint64_t)INT64_MAX) return false;
		*out = (int64_t)acc;
	}
	return true;
}
bool bvn_parse_uint64(const char* s, value_type_spec_t vt, uint64_t* out)
{
	if (!s || !out) return false;
	uint32_t base = bvn_effective_base(vt);
	if (base >= 2u && base <= 36u) {
		char* end;
		errno = 0;
		unsigned long long v = strtoull(s, &end, (int)base);
		if (*end || errno == ERANGE) return false;
		*out = (uint64_t)v;
		return true;
	}
	const char* p = s;
	if (*p == '+') p++;
	if (!*p) return false;
	uint64_t acc = 0;
	while (*p) {
		uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
		if (d >= base) return false;
		if (acc > (UINT64_MAX - d) / base) return false;
		acc = acc * base + d;
		p++;
	}
	*out = acc;
	return true;
}
/*
 * Parse a floating literal in an arbitrary base into a double.
 *
 * Base 10 and base 16 both go through bvn_float_strtoieee_bin, which forms the
 * literal as an exact rational num/den and rounds it ONCE to binary64 (correct
 * rounding across the whole range: normals, subnormals, overflow to inf,
 * underflow to 0, at any input length). It does NOT build an intermediate
 * fixed-width bvn_float and then narrow -- that sequence rounds twice and is off
 * by 1 ULP for inputs near a rounding midpoint, and no fixed intermediate width
 * removes it (the previous base-10 route landed 2.2250738585072011e-308 on the
 * smallest normal instead of the largest subnormal; the previous base-16 route
 * mis-rounded hex-floats with more than 64 significant bits the same way).
 * bvn_float_strtoieee_bin understands both decimal mantissas and hex-float
 * p-exponents, so a single exact path now serves both bases.
 *
 * bvn_format_double is the inverse, going double -> bvn_float -> text so the
 * rendered precision matches the declared width.
 */
bool bvn_parse_double_in_base(const char* s, uint32_t base, double* out)
{
	if (!s || !out) return false;
	if (base == 10u || base == 16u) {
		uint32_t b[2] = { 0u, 0u };
		bvn_float_strtoieee_bin(s, base, 11u, 52u, 1023, b, 2);
		uint64_t u = (uint64_t)b[0] | ((uint64_t)b[1] << 32);
		memcpy(out, &u, sizeof *out);
		return true;
	}
	/* No other base denotes a floating literal in this format. */
	return false;
}
bool bvn_looks_like_double(const char* s)
{
	if (!s) return false;
	for (; *s; s++)
		if (*s == '.' || *s == 'e' || *s == 'E' || *s == 'p' || *s == 'P')
			return true;
	return false;
}
int32_t bvn_format_double(char* buf, size_t bufsize, double value,
						  value_type_spec_t vt)
{
	if (!buf || bufsize < 2) return -1;
	bvn_limb_t _dlimbs[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	uint32_t prec = bvn_effective_width(vt);
	if (!prec || prec > 64u) prec = 64u;
	/* `value` is a C double, which carries at most 53 significand bits. Loading
	 * it into a wider float pads the mantissa with zero bits the value never
	 * actually had; the shortest-round-trip search in bvn_float_to_str then has
	 * to pin down that wider float and emits ~20 noise digits
	 * (0.1 -> 1.0000000000000000555e-1). Capping the format precision at the
	 * double's true 53 bits yields the canonical shortest decimal (0.1) that
	 * still round-trips the double exactly, while a narrower declared width
	 * (float:16/float:32) is preserved so its rounding intent is honoured. */
	if (prec > 53u) prec = 53u;
	bvn_float_init_buf(&f, prec, _dlimbs, BVN_FLOAT_NLIMBS(64u));
	if (!bvn_float_from_double(&f, value)) return -1;
	uint32_t base = bvn_effective_base(vt);
	if (!base) base = 10u;
	if (base > 36u) return -1;
	return bvn_float_to_str(&f, buf, bufsize, base);
}
bool bvn_parse_double(const char* s, value_type_spec_t vt, double* out)
{
	return bvn_parse_double_in_base(s, bvn_effective_base(vt), out);
}
/*
 * Expose the escape-replacement table (escape letter -> literal byte) so other
 * components decode string escapes identically to the lexer. A non-zero entry
 * is a recognised escape; zero means "not a valid escape character".
 */
const uint8_t* bvn_get_escape_repl_table(void)
{
	static const uint8_t table[256] = {
		['t']  = '\t',  ['n']  = '\n', ['v']  = '\v',
		['f']  = '\f',  ['r']  = '\r', ['"']  = '"',
		['\\'] = '\\',
	};
	return table;
}

