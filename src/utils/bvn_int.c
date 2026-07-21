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

#include "bvn_int.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
/*
 * ===========================================================================
 * Arbitrary-precision integers (bignum)
 * ===========================================================================
 *
 * bovnar integers can be far wider than 64 bits (uint:1024, etc.), so values
 * that don't fit a machine register are held here as a magnitude-plus-sign
 * bignum: `limbs` is a little-endian array of 32-bit limbs (limbs[0] least
 * significant), `nused` is the count of significant limbs, and `negative` is a
 * separate sign flag (sign-magnitude, not two's complement — chosen because the
 * format's integers are conceptually signed/unsigned decimal text, and
 * sign-magnitude makes base conversion and printing trivial). Zero is the
 * canonical nused==0 with negative==false; bvn_int_norm restores that form.
 *
 * Storage can be heap-backed (grown on demand by bigint_ensure_cap, `heap`
 * true) or wrap a caller-provided fixed buffer (`heap` false, never grows).
 * 32-bit limbs are used so every limb*limb product and limb/word division fits
 * in a uint64 intermediate, keeping the arithmetic portable C with no need for
 * compiler 128-bit support.
 *
 * The heavy routine is bvn_int_divrem (full bignum/bignum division); everything
 * else is schoolbook add/mul/shift over the limb array.
 */
static uint32_t bigint_char_to_digit(uint32_t c, uint32_t base)
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
	return (d < base) ? d : base;
}
static uint32_t bigint_digit_to_char(uint32_t d, uint32_t base)
{
	if (base == 64u) {
		static const char b64[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
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
/*
 * Guarantee at least `need` limbs of capacity. Heap-backed numbers grow
 * geometrically (doubling) to keep repeated appends amortised O(1); fixed-buffer
 * numbers (heap==false) cannot grow and fail instead. A hard ceiling of
 * BVN_INT_MAX_LIMBS bounds memory use against a pathological input. Newly added
 * limbs are zeroed so callers may treat capacity beyond nused as clean.
 */
static bool bigint_ensure_cap(bvn_int_t *n, uint32_t need)
{
	if (need > BVN_INT_MAX_LIMBS) return false;
	if (need <= n->nlimbs)        return true;
	if (!n->heap) return false;
	uint32_t nc = n->nlimbs ? n->nlimbs * 2u : 8u;
	if (nc < need) nc = need;
	if (nc > BVN_INT_MAX_LIMBS) nc = BVN_INT_MAX_LIMBS;
	uint32_t *p = realloc(n->limbs, (size_t)nc * sizeof(uint32_t));
	if (!p) return false;
	memset(p + n->nlimbs, 0, (size_t)(nc - n->nlimbs) * sizeof(uint32_t));
	n->limbs  = p;
	n->nlimbs = nc;
	return true;
}
/*
 * n = n*mul + add, in one pass with carry propagation. This is the workhorse
 * of base conversion (bvn_int_from_str): each input digit does n = n*base +
 * digit. The uint64 product can't overflow because both operands are 32-bit.
 */
static bool bigint_mul_add_word(bvn_int_t *n, uint32_t mul, uint32_t add)
{
	uint64_t carry = (uint64_t)add;
	for (uint32_t i = 0; i < n->nused; i++) {
		uint64_t prod  = (uint64_t)n->limbs[i] * (uint64_t)mul + carry;
		n->limbs[i]    = (uint32_t)prod;
		carry          = prod >> 32;
	}
	if (carry) {
		if (!bigint_ensure_cap(n, n->nused + 1u)) return false;
		n->limbs[n->nused++] = (uint32_t)carry;
	}
	return true;
}
bvn_int_t *bvn_int_alloc(void)
{
	bvn_int_t *n = calloc(1, sizeof(bvn_int_t));
	if (n) n->heap = true;
	return n;
}
void bvn_int_free(bvn_int_t *n)
{
	if (!n) return;
	if (n->heap) free(n->limbs);
	free(n);
}
bool bvn_int_is_zero(const bvn_int_t *n)
{
	return !n || n->nused == 0;
}
/*
 * Parse a signed integer string in the given base (2-62, 64, 85) into a bignum
 * via repeated multiply-accumulate. A lone sign or empty string is rejected;
 * the sign is only applied to a non-zero result so "-0" normalises to +0.
 */
bool bvn_int_from_str(bvn_int_t *n, const char *s, uint32_t base)
{
	if (!n || !s) return false;
	const bool valid_base =
		(base >= 2u && base <= 62u) || base == 64u || base == 85u;
	if (!valid_base) return false;
	bool negative = false;
	/* '+'/'-' are signs only in sign-bearing bases. In bases 64 and 85 they are
	 * digit characters (Base64 '+' = 62; Ascii85 '+' = 10, '-' = 12), so those
	 * bases are unsigned-only and every character is parsed as a digit. */
	if (base != 64u && base != 85u) {
		if      (*s == '-') { negative = true;  ++s; }
		else if (*s == '+') {                   ++s; }
	}
	if (!*s) return false;
	n->nused    = 0;
	n->negative = false;
	for (; *s; ++s) {
		uint32_t d = bigint_char_to_digit((uint8_t)*s, base);
		if (d >= base) return false;
		if (!bigint_mul_add_word(n, base, d)) return false;
	}
	n->negative = negative && (n->nused != 0);
	return true;
}
size_t bvn_int_str_bufsize(uint32_t bits, uint32_t base)
{
	if (bits == 0 || base < 2u) return 4u;
	uint32_t bpd;
	if      (base >= 64u) bpd = 6u;
	else if (base >= 32u) bpd = 5u;
	else if (base >= 16u) bpd = 4u;
	else if (base >= 8u)  bpd = 3u;
	else if (base >= 4u)  bpd = 2u;
	else                  bpd = 1u;
	return (size_t)((bits + bpd - 1u) / bpd) + 2u;
}
/*
 * Convert a bignum to a string in `base` by repeated division: divide the whole
 * magnitude by the base, the remainder is the next least-significant digit,
 * repeat until zero. Works on a private copy of the limbs so the source is
 * untouched. Digits come out reversed and are flipped into the caller buffer.
 * Returns -1 (rather than truncating) if the buffer is too small — size it with
 * bvn_int_str_bufsize, which upper-bounds the digit count from the bit width.
 */
int32_t bvn_int_to_str(const bvn_int_t *n,
						char *buf, size_t bufsize,
						uint32_t base)
{
	if (!n || !buf || bufsize < 2u) return -1;
	const bool valid_base =
		(base >= 2u && base <= 62u) || base == 64u || base == 85u;
	if (!valid_base) return -1;
	/* Bases 64 and 85 use '+'/'-' as digits, so they have no sign character and
	 * are unsigned-only: a negative value has no representation. */
	if (n->negative && (base == 64u || base == 85u)) return -1;
	if (bvn_int_is_zero(n)) {
		uint32_t zch = bigint_digit_to_char(0u, base);
		if (!zch) return -1;
		buf[0] = (char)zch;
		buf[1] = '\0';
		return 1;
	}
	const size_t max_digits = (size_t)n->nused * 32u + 2u;
	char        *tmp = malloc(max_digits);
	if (!tmp) return -1;
	uint32_t *limbs = malloc((size_t)n->nused * sizeof(uint32_t));
	if (!limbs) { free(tmp); return -1; }
	memcpy(limbs, n->limbs, (size_t)n->nused * sizeof(uint32_t));
	uint32_t nused = n->nused;
	size_t ndigits = 0;
	while (nused > 0) {
		uint64_t rem = 0;
		for (int32_t i = (int32_t)nused - 1; i >= 0; i--) {
			uint64_t cur = (rem << 32) | (uint64_t)limbs[i];
			limbs[i] = (uint32_t)(cur / base);
			rem      = cur % base;
		}
		/* nused starts at n->nused and only decreases here, and limbs holds
		 * n->nused elements, so limbs[nused - 1] is in bounds for every
		 * reachable state. GCC 16's -fanalyzer cannot model that invariant
		 * (n->nused is unconstrained to it) and reports a phantom over-read. */
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wanalyzer-out-of-bounds"
#endif
		while (nused > 0 && limbs[nused - 1] == 0u) nused--;
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif
		uint32_t ch = bigint_digit_to_char((uint32_t)rem, base);
		if (!ch) { free(limbs); free(tmp); return -1; }
		tmp[ndigits++] = (char)ch;
	}
	free(limbs);
	const size_t sign_len = n->negative ? 1u : 0u;
	const size_t total    = sign_len + ndigits;
	if (total + 1u > bufsize) { free(tmp); return -1; }
	char *out = buf;
	if (n->negative) *out++ = '-';
	for (size_t i = 0; i < ndigits; i++)
		out[i] = tmp[ndigits - 1u - i];
	buf[total] = '\0';
	free(tmp);
	return (int32_t)total;
}
bool bvn_int_from_uint64(bvn_int_t *n, uint64_t v)
{
	if (!n) return false;
	n->nused    = 0;
	n->negative = false;
	if (v == 0) return true;
	if (!bigint_ensure_cap(n, 2u)) return false;
	n->limbs[0] = (uint32_t)v;
	n->limbs[1] = (uint32_t)(v >> 32);
	n->nused    = n->limbs[1] ? 2u : 1u;
	return true;
}
bool bvn_int_from_int64(bvn_int_t *n, int64_t v)
{
	if (!n) return false;
	const bool neg = (v < 0);
	const uint64_t mag = neg
		? ((v == INT64_MIN) ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)(-v))
		: (uint64_t)v;
	if (!bvn_int_from_uint64(n, mag)) return false;
	n->negative = neg && (n->nused != 0);
	return true;
}
bool bvn_int_from_uint32(bvn_int_t *n, uint32_t v)
{ return bvn_int_from_uint64(n, (uint64_t)v); }
bool bvn_int_from_int32(bvn_int_t *n, int32_t v)
{ return bvn_int_from_int64(n, (int64_t)v); }
bool bvn_int_from_uint16(bvn_int_t *n, uint16_t v)
{ return bvn_int_from_uint64(n, (uint64_t)v); }
bool bvn_int_from_int16(bvn_int_t *n, int16_t v)
{ return bvn_int_from_int64(n, (int64_t)v); }
bool bvn_int_from_uint8(bvn_int_t *n, uint8_t v)
{ return bvn_int_from_uint64(n, (uint64_t)v); }
bool bvn_int_from_int8(bvn_int_t *n, int8_t v)
{ return bvn_int_from_int64(n, (int64_t)v); }
bool bvn_int_to_uint64(const bvn_int_t *n, uint64_t *out)
{
	if (!n || !out) return false;
	if (n->negative)  return false;
	if (n->nused == 0) { *out = 0; return true; }
	if (n->nused >  2) return false;
	*out = (n->nused == 2)
		 ? ((uint64_t)n->limbs[1] << 32) | (uint64_t)n->limbs[0]
		 :  (uint64_t)n->limbs[0];
	return true;
}
bool bvn_int_to_int64(const bvn_int_t *n, int64_t *out)
{
	if (!n || !out) return false;
	if (n->nused == 0) { *out = 0; return true; }
	if (n->nused >  2) return false;
	const uint64_t mag = (n->nused == 2)
					   ? ((uint64_t)n->limbs[1] << 32) | (uint64_t)n->limbs[0]
					   :  (uint64_t)n->limbs[0];
	if (n->negative) {
		if (mag > (uint64_t)INT64_MAX + 1u) return false;
		*out = (mag == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)mag;
	} else {
		if (mag > (uint64_t)INT64_MAX) return false;
		*out = (int64_t)mag;
	}
	return true;
}
bool bvn_int_to_uint32(const bvn_int_t *n, uint32_t *out)
{
	uint64_t v;
	if (!bvn_int_to_uint64(n, &v)) return false;
	if (v > (uint64_t)UINT32_MAX)  return false;
	*out = (uint32_t)v;
	return true;
}
bool bvn_int_to_int32(const bvn_int_t *n, int32_t *out)
{
	int64_t v;
	if (!bvn_int_to_int64(n, &v)) return false;
	if (v < (int64_t)INT32_MIN || v > (int64_t)INT32_MAX) return false;
	*out = (int32_t)v;
	return true;
}
bool bvn_int_to_uint16(const bvn_int_t *n, uint16_t *out)
{
	uint64_t v;
	if (!bvn_int_to_uint64(n, &v)) return false;
	if (v > (uint64_t)UINT16_MAX)  return false;
	*out = (uint16_t)v;
	return true;
}
bool bvn_int_to_int16(const bvn_int_t *n, int16_t *out)
{
	int64_t v;
	if (!bvn_int_to_int64(n, &v)) return false;
	if (v < (int64_t)INT16_MIN || v > (int64_t)INT16_MAX) return false;
	*out = (int16_t)v;
	return true;
}
bool bvn_int_to_uint8(const bvn_int_t *n, uint8_t *out)
{
	uint64_t v;
	if (!bvn_int_to_uint64(n, &v)) return false;
	if (v > (uint64_t)UINT8_MAX)   return false;
	*out = (uint8_t)v;
	return true;
}
bool bvn_int_to_int8(const bvn_int_t *n, int8_t *out)
{
	int64_t v;
	if (!bvn_int_to_int64(n, &v)) return false;
	if (v < (int64_t)INT8_MIN || v > (int64_t)INT8_MAX) return false;
	*out = (int8_t)v;
	return true;
}
void bvn_int_zero(bvn_int_t *n)
{
	n->nused    = 0;
	n->negative = false;
}
/*
 * Restore canonical form by dropping leading zero limbs. Every routine that may
 * produce a high zero limb (subtraction, division, shifts) calls this so that
 * nused always reflects the true magnitude and comparisons stay correct.
 */
void bvn_int_norm(bvn_int_t *n)
{
	while (n->nused > 0 && n->limbs[n->nused - 1] == 0u)
		n->nused--;
}
bool bvn_int_copy(bvn_int_t *dst, const bvn_int_t *src)
{
	if (dst == src) return true;
	if (!bigint_ensure_cap(dst, src->nused)) return false;
	/* A zero bignum has nused == 0 and may still have limbs == NULL; memcpy is
	 * undefined with a null pointer even for a zero length. */
	if (src->nused)
		memcpy(dst->limbs, src->limbs, (size_t)src->nused * sizeof(uint32_t));
	dst->nused    = src->nused;
	dst->negative = src->negative;
	return true;
}
bool bvn_int_add_u32(bvn_int_t *n, uint32_t v)
{
	uint64_t carry = v;
	for (uint32_t i = 0; i < n->nused && carry; i++) {
		uint64_t s = (uint64_t)n->limbs[i] + carry;
		n->limbs[i] = (uint32_t)s;
		carry       = s >> 32;
	}
	if (carry) {
		if (!bigint_ensure_cap(n, n->nused + 1u)) return false;
		n->limbs[n->nused++] = (uint32_t)carry;
	}
	return true;
}
bool bvn_int_mul_u32(bvn_int_t *n, uint32_t v)
{
	if (v == 0) { bvn_int_zero(n); return true; }
	uint64_t carry = 0;
	for (uint32_t i = 0; i < n->nused; i++) {
		uint64_t p  = (uint64_t)n->limbs[i] * v + carry;
		n->limbs[i] = (uint32_t)p;
		carry       = p >> 32;
	}
	if (carry) {
		if (!bigint_ensure_cap(n, n->nused + 1u)) return false;
		n->limbs[n->nused++] = (uint32_t)carry;
	}
	return true;
}
bool bvn_int_mul_pow10(bvn_int_t *n, int k)
{
	static const uint32_t p10[] =
		{ 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
		  1000000000 };
	while (k >= 9) {
		if (!bvn_int_mul_u32(n, 1000000000u)) return false;
		k -= 9;
	}
	if (k > 0) return bvn_int_mul_u32(n, p10[k]);
	return true;
}
uint32_t bvn_int_div_u32(bvn_int_t *n, uint32_t v)
{
	if (!n || v == 0) return UINT32_MAX;
	uint64_t rem = 0;
	for (int32_t i = (int32_t)n->nused - 1; i >= 0; i--) {
		uint64_t cur = (rem << 32) | (uint64_t)n->limbs[i];
		n->limbs[i]  = (uint32_t)(cur / v);
		rem          = cur % v;
	}
	bvn_int_norm(n);
	return (uint32_t)rem;
}
bool bvn_int_shl(bvn_int_t *n, int bits)
{
	if (bvn_int_is_zero(n) || bits == 0) return true;
	if (bits < 0) return true;
	int ws = bits / 32;
	int bs = bits % 32;
	if (bs) {
		uint32_t carry = 0;
		for (uint32_t i = 0; i < n->nused; i++) {
			uint32_t nc = n->limbs[i] >> (32 - bs);
			n->limbs[i] = (n->limbs[i] << bs) | carry;
			carry       = nc;
		}
		if (carry) {
			if (!bigint_ensure_cap(n, n->nused + 1u)) return false;
			n->limbs[n->nused++] = carry;
		}
	}
	if (ws) {
		uint32_t new_nused = n->nused + (uint32_t)ws;
		if (!bigint_ensure_cap(n, new_nused)) return false;
		for (int32_t i = (int32_t)new_nused - 1; i >= 0; i--) {
			int32_t src = i - ws;
			n->limbs[i] = (src >= 0 && (uint32_t)src < n->nused)
						  ? n->limbs[src] : 0u;
		}
		n->nused = new_nused;
	}
	bvn_int_norm(n);
	return true;
}
void bvn_int_shr(bvn_int_t *n, int bits)
{
	if (bits < 0 || bvn_int_is_zero(n) || bits == 0) return;
	int ws = bits / 32;
	int bs = bits % 32;
	if (ws >= (int)n->nused) { bvn_int_zero(n); return; }
	if (ws) {
		uint32_t nn = n->nused - (uint32_t)ws;
		for (uint32_t i = 0; i < nn; i++)
			n->limbs[i] = n->limbs[i + (uint32_t)ws];
		n->nused = nn;
	}
	if (bs) {
		for (uint32_t i = 0; i < n->nused; i++) {
			n->limbs[i] >>= bs;
			if (i + 1u < n->nused)
				n->limbs[i] |= n->limbs[i + 1u] << (32 - bs);
		}
	}
	bvn_int_norm(n);
}
int bvn_int_bitlen(const bvn_int_t *n)
{
	if (bvn_int_is_zero(n)) return 0;
	int bits = (int)(n->nused - 1u) * 32;
	uint32_t top = n->limbs[n->nused - 1u];
	while (top) { bits++; top >>= 1; }
	return bits;
}
int bvn_int_getbit(const bvn_int_t *n, int i)
{
	if (i < 0) return 0;               /* no negative bit positions */
	uint32_t wi = (uint32_t)i / 32u;
	int      bi = i % 32;
	if (wi >= n->nused) return 0;
	return (int)((n->limbs[wi] >> bi) & 1u);
}
bool bvn_int_setbit(bvn_int_t *n, int i)
{
	if (i < 0) return false;           /* no negative bit positions */
	uint32_t wi = (uint32_t)i / 32u;
	int      bi = i % 32;
	if (!bigint_ensure_cap(n, wi + 1u)) return false;
	if (wi >= n->nused) {
		for (uint32_t j = n->nused; j <= wi; j++) n->limbs[j] = 0u;
		n->nused = wi + 1u;
	}
	n->limbs[wi] |= (1u << bi);
	return true;
}
/*
 * Three-way compare (-1/0/+1) respecting sign. Differing signs decide
 * immediately (with a +0/-0 equality guard); same sign compares magnitudes by
 * limb count then most-significant limb downward, and the result is flipped for
 * negatives so e.g. -5 < -3.
 */
int bvn_int_cmp(const bvn_int_t *a, const bvn_int_t *b)
{
	if (a->negative != b->negative) {
		if (bvn_int_is_zero(a) && bvn_int_is_zero(b)) return 0;
		return a->negative ? -1 : 1;
	}
	int mag;
	if (a->nused != b->nused) {
		mag = (a->nused > b->nused) ? 1 : -1;
	} else {
		mag = 0;
		for (int32_t i = (int32_t)a->nused - 1; i >= 0; i--) {
			if (a->limbs[i] != b->limbs[i]) {
				mag = (a->limbs[i] > b->limbs[i]) ? 1 : -1;
				break;
			}
		}
	}
	return a->negative ? -mag : mag;
}
bool bvn_int_sub_inplace(bvn_int_t *a, const bvn_int_t *b)
{
	int64_t  borrow = 0;
	uint32_t max_n  = (a->nused > b->nused) ? a->nused : b->nused;
	for (uint32_t i = 0; i < max_n; i++) {
		int64_t av = (i < a->nused) ? (int64_t)a->limbs[i] : 0;
		int64_t bv = (i < b->nused) ? (int64_t)b->limbs[i] : 0;
		int64_t d  = av - borrow - bv;
		if (d < 0) { d += (int64_t)1 << 32; borrow = 1; }
		else        borrow = 0;
		if (i < a->nused) {
			a->limbs[i] = (uint32_t)d;
		} else if (i < a->nlimbs) {
			a->limbs[i] = (uint32_t)d;
			a->nused     = i + 1u;
		} else {
			return false;
		}
	}
	bvn_int_norm(a);
	return (borrow == 0);
}
/*
 * Full bignum division: a = q*b + r (truncating toward zero; r takes a's sign).
 *
 * This is Knuth's Algorithm D (TAOCP Vol. 2, 4.3.1) for multi-limb division,
 * with cheaper special cases peeled off first: zero dividend, dividend shorter
 * than divisor (quotient 0), equal-length quick magnitude compare, and a
 * single-limb divisor (the simple bvn_int_div_u32 long-division). The general
 * path normalises by left-shifting both operands so the divisor's top limb has
 * its high bit set (s), which makes the per-digit quotient estimate q_hat
 * accurate to within 1; the estimate is then refined and a multiply-subtract
 * with occasional add-back computes each quotient limb. The remainder is
 * de-normalised (right-shifted by s) at the end. u/v are temporary normalised
 * buffers freed before return.
 */
bool bvn_int_divrem(bvn_int_t *q, bvn_int_t *r,
					const bvn_int_t *a, const bvn_int_t *b)
{
	bvn_int_zero(q);
	bvn_int_zero(r);
	if (bvn_int_is_zero(b)) return false;
	uint32_t na = a->nused;
	uint32_t nb = b->nused;
	while (na > 0u && a->limbs[na - 1u] == 0u) na--;
	while (nb > 0u && b->limbs[nb - 1u] == 0u) nb--;
	if (na == 0u) return true;
	if (na < nb) {
		if (!bvn_int_copy(r, a)) return false;
		return true;
	}
	if (na == nb) {
		int mag = 0;
		for (int32_t i = (int32_t)na - 1; i >= 0 && mag == 0; i--) {
			if      (a->limbs[(uint32_t)i] > b->limbs[(uint32_t)i]) mag =  1;
			else if (a->limbs[(uint32_t)i] < b->limbs[(uint32_t)i]) mag = -1;
		}
		if (mag < 0) {
			if (!bvn_int_copy(r, a)) return false;
			return true;
		}
		if (mag == 0) {
			if (!bigint_ensure_cap(q, 1u)) return false;
			q->limbs[0u] = 1u;
			q->nused     = 1u;
			q->negative  = a->negative != b->negative;
			return true;
		}
	}
	if (nb == 1u) {
		if (!bvn_int_copy(q, a)) return false;
		q->negative = false;
		uint32_t rem = bvn_int_div_u32(q, b->limbs[0u]);
		q->negative = a->negative != b->negative;
		if (rem != 0u) {
			if (!bigint_ensure_cap(r, 1u)) return false;
			r->limbs[0u] = rem;
			r->nused     = 1u;
			r->negative  = a->negative;
		}
		return true;
	}
	uint32_t n = nb;
	uint32_t m = na - nb;
	uint32_t *u = calloc((size_t)(m + n + 1u), sizeof(uint32_t));
	uint32_t *v = calloc((size_t)n, sizeof(uint32_t));
	if (!u || !v) { free(u); free(v); return false; }
	uint32_t s = 0u;
	{ uint32_t t = b->limbs[n - 1u]; while (t < 0x80000000u) { s++; t <<= 1u; } }
	{
		uint64_t carry = 0u;
		for (uint32_t i = 0u; i < na; i++) {
			uint64_t x = ((uint64_t)a->limbs[i] << s) | carry;
			u[i]       = (uint32_t)x;
			carry      = x >> 32u;
		}
		u[na] = (uint32_t)carry;
	}
	{
		uint64_t carry = 0u;
		for (uint32_t i = 0u; i < n; i++) {
			uint64_t x = ((uint64_t)b->limbs[i] << s) | carry;
			v[i]       = (uint32_t)x;
			carry      = x >> 32u;
		}
	}
	if (!bigint_ensure_cap(q, m + 1u)) { free(u); free(v); return false; }
	memset(q->limbs, 0, (size_t)(m + 1u) * sizeof(uint32_t));
	const uint64_t B = (uint64_t)1u << 32u;
	const uint32_t vn1 = v[n - 1u];
	const uint32_t vn2 = (n >= 2u) ? v[n - 2u] : 0u;
	for (int32_t j = (int32_t)m; j >= 0; j--) {
		const uint32_t uj = (uint32_t)j;
		uint64_t u_hi  = (uint64_t)u[uj + n];
		uint64_t u_lo  = (uint64_t)u[uj + n - 1u];
		uint64_t u_lo2 = (n >= 2u) ? (uint64_t)u[uj + n - 2u] : 0u;
		uint64_t q_hat, r_hat;
		if (u_hi >= (uint64_t)vn1) {
			q_hat = B - 1u;
			r_hat = u_lo + (uint64_t)vn1;
		} else {
			const uint64_t numer = (u_hi << 32u) | u_lo;
			q_hat = numer / (uint64_t)vn1;
			r_hat = numer % (uint64_t)vn1;
		}
		while (r_hat < B &&
			   q_hat * (uint64_t)vn2 > ((r_hat << 32u) | u_lo2)) {
			q_hat--;
			r_hat += (uint64_t)vn1;
		}
		uint64_t mc = 0u;
		uint64_t sb = 0u;
		for (uint32_t i = 0u; i <= n; i++) {
			uint64_t p;
			if (i < n) {
				const uint64_t prod = q_hat * (uint64_t)v[i] + mc;
				mc = prod >> 32u;
				p  = prod & 0xFFFFFFFFull;
			} else {
				p  = mc;
				mc = 0u;
			}
			p += sb;
			const uint64_t ui = (uint64_t)u[uj + i];
			if (ui >= p) {
				u[uj + i] = (uint32_t)(ui - p);
				sb = 0u;
			} else {
				u[uj + i] = (uint32_t)(ui + B - p);
				sb = 1u;
			}
		}
		q->limbs[uj] = (uint32_t)q_hat;
		if (sb) {
			q->limbs[uj]--;
			uint64_t carry = 0u;
			for (uint32_t i = 0u; i <= n; i++) {
				const uint64_t s_val = (uint64_t)u[uj + i]
									 + (i < n ? (uint64_t)v[i] : 0u)
									 + carry;
				u[uj + i] = (uint32_t)s_val;
				carry      = s_val >> 32u;
			}
		}
	}
	q->nused    = m + 1u;
	q->negative = a->negative != b->negative;
	bvn_int_norm(q);
	if (!bigint_ensure_cap(r, n)) { free(u); free(v); return false; }
	if (s == 0u) {
		for (uint32_t i = 0u; i < n; i++) r->limbs[i] = u[i];
	} else {
		for (uint32_t i = 0u; i < n; i++) {
			r->limbs[i] = (u[i] >> s)
						| (i + 1u < n ? u[i + 1u] << (32u - s) : 0u);
		}
	}
	r->nused    = n;
	r->negative = a->negative;
	bvn_int_norm(r);
	free(u);
	free(v);
	return true;
}
/* ── arbitrary-precision multiply / add / gcd ─────────────────────────────
 *
 * These three complete the arithmetic needed for exact-rational unit
 * conversion: multiply and add operate on the signed value, gcd on the
 * magnitude (for reducing a rational to lowest terms). Each writes into `dst`,
 * which is allowed to alias the inputs — results are built in a private temp and
 * copied out, so aliasing never corrupts an operand mid-computation.
 */
static int bigint_cmpmag(const bvn_int_t *a, const bvn_int_t *b)
{
	uint32_t na = a->nused, nb = b->nused;
	while (na && a->limbs[na - 1u] == 0u) na--;
	while (nb && b->limbs[nb - 1u] == 0u) nb--;
	if (na != nb) return na > nb ? 1 : -1;
	for (int32_t i = (int32_t)na - 1; i >= 0; i--)
		if (a->limbs[(uint32_t)i] != b->limbs[(uint32_t)i])
			return a->limbs[(uint32_t)i] > b->limbs[(uint32_t)i] ? 1 : -1;
	return 0;
}
/* acc.magnitude += b.magnitude (signs ignored). */
static bool bigint_addmag_into(bvn_int_t *acc, const bvn_int_t *b)
{
	uint32_t maxn = (acc->nused > b->nused) ? acc->nused : b->nused;
	if (!bigint_ensure_cap(acc, maxn + 1u)) return false;
	uint64_t carry = 0u;
	for (uint32_t i = 0; i < maxn; i++) {
		uint64_t s = (uint64_t)(i < acc->nused ? acc->limbs[i] : 0u)
			   + (uint64_t)(i < b->nused   ? b->limbs[i]   : 0u) + carry;
		acc->limbs[i] = (uint32_t)s;
		carry = s >> 32;
	}
	acc->limbs[maxn] = (uint32_t)carry;
	acc->nused = maxn + 1u;
	bvn_int_norm(acc);
	return true;
}
bool bvn_int_mul(bvn_int_t *dst, const bvn_int_t *a, const bvn_int_t *b)
{
	if (!dst || !a || !b) return false;
	if (bvn_int_is_zero(a) || bvn_int_is_zero(b)) { bvn_int_zero(dst); return true; }
	uint32_t na = a->nused, nb = b->nused;
	bvn_int_t *tmp = bvn_int_alloc();
	if (!tmp) return false;
	if (!bigint_ensure_cap(tmp, na + nb)) { bvn_int_free(tmp); return false; }
	memset(tmp->limbs, 0, (size_t)(na + nb) * sizeof(uint32_t));
	for (uint32_t i = 0; i < na; i++) {
		uint64_t ai = a->limbs[i], carry = 0u;
		for (uint32_t j = 0; j < nb; j++) {
			uint64_t cur = (uint64_t)tmp->limbs[i + j]
				     + ai * (uint64_t)b->limbs[j] + carry;
			tmp->limbs[i + j] = (uint32_t)cur;
			carry = cur >> 32;
		}
		uint32_t k = i + nb;
		while (carry) {
			uint64_t cur = (uint64_t)tmp->limbs[k] + carry;
			tmp->limbs[k] = (uint32_t)cur;
			carry = cur >> 32;
			k++;
		}
	}
	tmp->nused    = na + nb;
	tmp->negative = (a->negative != b->negative);
	bvn_int_norm(tmp);
	bool ok = bvn_int_copy(dst, tmp);
	bvn_int_free(tmp);
	return ok;
}
bool bvn_int_add(bvn_int_t *dst, const bvn_int_t *a, const bvn_int_t *b)
{
	if (!dst || !a || !b) return false;
	bvn_int_t *tmp = bvn_int_alloc();
	if (!tmp) return false;
	bool ok = true;
	if (a->negative == b->negative) {
		/* same sign: magnitudes add, sign preserved */
		ok = bvn_int_copy(tmp, a);
		if (ok) { tmp->negative = false; ok = bigint_addmag_into(tmp, b); }
		if (ok) tmp->negative = a->negative && (tmp->nused != 0u);
	} else {
		/* differing signs: larger magnitude minus smaller, sign of larger */
		int c = bigint_cmpmag(a, b);
		if (c == 0) {
			bvn_int_zero(tmp);
		} else if (c > 0) {
			ok = bvn_int_copy(tmp, a);
			if (ok) { tmp->negative = false; ok = bvn_int_sub_inplace(tmp, b); }
			if (ok) tmp->negative = a->negative && (tmp->nused != 0u);
		} else {
			ok = bvn_int_copy(tmp, b);
			if (ok) { tmp->negative = false; ok = bvn_int_sub_inplace(tmp, a); }
			if (ok) tmp->negative = b->negative && (tmp->nused != 0u);
		}
	}
	if (ok) ok = bvn_int_copy(dst, tmp);
	bvn_int_free(tmp);
	return ok;
}
bool bvn_int_gcd(bvn_int_t *dst, const bvn_int_t *a, const bvn_int_t *b)
{
	if (!dst || !a || !b) return false;
	bvn_int_t *x = bvn_int_alloc(), *y = bvn_int_alloc();
	bvn_int_t *q = bvn_int_alloc(), *r = bvn_int_alloc();
	bool ok = x && y && q && r;
	if (ok) ok = bvn_int_copy(x, a) && bvn_int_copy(y, b);
	if (ok) { x->negative = false; y->negative = false; }
	/* Euclid: gcd(x,y) = gcd(y, x mod y) until y == 0 */
	while (ok && !bvn_int_is_zero(y)) {
		if (!bvn_int_divrem(q, r, x, y)) { ok = false; break; }
		r->negative = false;
		if (!bvn_int_copy(x, y)) { ok = false; break; }
		bvn_int_t *t = y; y = r; r = t;   /* y = r; reuse old y as scratch */
	}
	if (ok) { x->negative = false; ok = bvn_int_copy(dst, x); }
	bvn_int_free(x); bvn_int_free(y); bvn_int_free(q); bvn_int_free(r);
	return ok;
}
