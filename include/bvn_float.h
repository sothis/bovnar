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

#ifndef BVN_FLOAT_H_
#define BVN_FLOAT_H_
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bvn_int.h"
#ifndef BVN_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define BVN_API  /* DLL symbols exported via CMake WINDOWS_EXPORT_ALL_SYMBOLS */
#  elif defined(__GNUC__) || defined(__clang__)
#    define BVN_API __attribute__((visibility("default")))
#  else
#    define BVN_API
#  endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define BVN_FLOAT_MAX_PREC  32768u
#if UINTPTR_MAX > 0xffffffffu
	typedef uint64_t bvn_limb_t;
	#define BVN_LIMB_BITS 64u
#else
	typedef uint32_t bvn_limb_t;
	#define BVN_LIMB_BITS 32u
#endif
#define BVN_FLOAT_EXP_MIN  (INT64_MIN)
#define BVN_FLOAT_EXP_ZERO (INT64_MIN + 1)
#define BVN_FLOAT_EXP_NAN  (INT64_MIN + 2)
#define BVN_FLOAT_EXP_INF  (INT64_MIN + 3)
#define BVN_FLOAT_NLIMBS(prec) \
	(((uint32_t)(prec) + BVN_LIMB_BITS - 1u) / BVN_LIMB_BITS)
typedef struct bvn_float_s {
	uint32_t    _prec;
	int32_t     _sign;
	int64_t     _exp;
	bvn_limb_t *_d;
	uint32_t    _nlimbs;
	bool        _heap;
	uint64_t    _reserved[4];
} bvn_float_t;
BVN_API bvn_float_t *bvn_float_alloc(uint32_t prec);
BVN_API void bvn_float_free(bvn_float_t *f);
BVN_API void bvn_float_init_buf(bvn_float_t *f, uint32_t prec,
						bvn_limb_t *buf, uint32_t nlimbs);
BVN_API bool bvn_float_is_nan(const bvn_float_t *f);
BVN_API bool bvn_float_is_inf(const bvn_float_t *f);
BVN_API bool bvn_float_is_zero(const bvn_float_t *f);
BVN_API bool bvn_float_is_neg(const bvn_float_t *f);
BVN_API bool bvn_float_is_regular(const bvn_float_t *f);
BVN_API void bvn_float_set_nan (bvn_float_t *f);
BVN_API void bvn_float_set_inf (bvn_float_t *f, bool neg);
BVN_API void bvn_float_set_zero(bvn_float_t *f, bool neg);
BVN_API bool bvn_float_copy(bvn_float_t *dst, const bvn_float_t *src);
/*
 * Parse a number in the given base into f (rounded to f's precision). Returns
 * false on malformed input. Note a capacity limit: a finite decimal whose exact
 * value would need more than BVN_INT_MAX_BITS of intermediate precision -- i.e.
 * an extreme decimal exponent (on this build, magnitude beyond ~1e9865 -- the
 * exact bound shifts slightly with the number of coefficient digits), such as
 * "1e10000" or "1e-10000" -- saturates to +/-inf or +/-0 and still returns
 * true, rather than failing. The 64-bit stored exponent is not the binding
 * limit here; the exact rational is. Callers parsing untrusted extreme
 * exponents can distinguish saturation from a literal inf/0 via
 * bvn_float_is_inf / bvn_float_is_zero if that matters.
 */
BVN_API bool bvn_float_from_str(bvn_float_t *f, const char *s, uint32_t base);
/* Parse a base-10 or base-16 numeric literal into an EXACT rational value =
 * num/den (sign carried in num; den > 0). Returns true only for a finite value;
 * false for nan/inf, a malformed literal, or an unsupported base. num and den
 * must be caller-allocated. Underpins lossless read-time unit conversion. */
BVN_API bool bvn_float_parse_rational(const char *s, uint32_t base,
                                      bvn_int_t *num, bvn_int_t *den);
BVN_API int32_t bvn_float_to_str(const bvn_float_t *f, char *buf, size_t bufsize,
						  uint32_t base);
BVN_API size_t bvn_float_str_bufsize(uint32_t prec, uint32_t base);
BVN_API bool bvn_float_from_double(bvn_float_t *f, double v);
/*
 * Narrowing a bvn_float to a fixed-width binary format (bvn_float_to_double /
 * to_float / to_bin*) rounds the *stored* value once, correctly. It cannot undo
 * a rounding that already happened when the bvn_float was built: if a decimal
 * string was parsed into a fixed-width bvn_float and then narrowed, the parse
 * and the narrowing round in sequence (double rounding) and the result can be 1
 * ULP off for inputs near a rounding midpoint. This is NOT confined to
 * subnormals -- normal results are affected too, and NO fixed intermediate
 * width removes it (widening only shrinks the set of inputs that trip it, never
 * to zero; the "double rounding is innocuous when the intermediate is >= 2t+2"
 * result governs the exact products/sums of t-bit operands, not the rounding of
 * an arbitrary-length decimal string). To convert a STRING correctly, do not
 * build a bvn_float and narrow at all: use the exact single-rounding path
 * bvn_float_strtoieee_bin / bvn_float_strtod (used by bvn_parse_double_in_base
 * for base 10 and 16 and by the bvn_float_parse_bin* helpers), which rounds the
 * exact rational once and is correctly rounded at any input length. Narrowing a
 * bvn_float that already holds the value you want (e.g. the result of arithmetic
 * at full precision) is fine; it is only the parse-then-narrow sequence on a
 * decimal/hex string that double-rounds.
 */
BVN_API bool bvn_float_to_double  (const bvn_float_t *f, double *out);
/*
 * Correctly-rounded base-10 decimal string -> double in a single rounding step,
 * across the whole range (normals, subnormals, and overflow to inf). NULL
 * returns 0.0. This is the supported public form of the conversion the value
 * layer uses; prefer it over building a low-precision bvn_float and narrowing.
 */
BVN_API double bvn_float_strtod(const char *s);
BVN_API bool bvn_float_from_float (bvn_float_t *f, float v);
BVN_API bool bvn_float_to_float   (const bvn_float_t *f, float *out);
BVN_API void bvn_float_to_ieee_bin(const bvn_float_t *f,
							uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							uint32_t *bits, int bits32);
/*
 * Correctly-rounded base-10 or base-16 (hex-float) string -> IEEE-754 binary
 * interchange value, in a SINGLE rounding step across the whole range (normals,
 * subnormals, overflow to inf, underflow to zero). The literal is parsed as an
 * exact rational num/den and rounded once to the requested (exp_bits, man_bits,
 * bias) format; it does NOT build an intermediate fixed-width bvn_float and then
 * narrow, so it cannot double-round near a rounding midpoint at any input
 * length. The bit layout of *bits matches bvn_float_to_ieee_bin. This is the
 * primitive behind bvn_float_strtod and the bvn_float_parse_bin* helpers; the
 * value layer uses it for base-10 and base-16 float literals. NULL string or an
 * unsupported base leaves *bits all-zero (i.e. +0.0); a syntactically invalid
 * literal likewise yields +0.0 (there is no error channel in this signature).
 *
 * "Whole range" means the whole range OF THE TARGET FORMAT, and carries the same
 * capacity caveat as bvn_float_from_str: the literal is held as an exact
 * rational bounded by BVN_INT_MAX_BITS, so a decimal exponent beyond roughly
 * +/-1e9865 saturates to inf / 0 no matter what the format could represent.
 * Every standard format up to binary128 has a narrower range than that, so the
 * limit is invisible there -- binary128's own overflow point is ~1e4932. It
 * binds only for binary256 (max ~1e78913) and any wider caller-defined geometry,
 * where "1e50000" yields +Infinity rather than the finite normal the format has
 * room for. Lifting it needs a different algorithm -- 10^50000 alone is ~166000
 * bits -- not a larger constant.
 */
BVN_API void bvn_float_strtoieee_bin(const char *s, uint32_t base,
							uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							uint32_t *bits, int bits32);
BVN_API bool bvn_float_from_ieee_bin(bvn_float_t *f,
							  uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							  const uint32_t *bits, int bits32);
BVN_API void bvn_float_to_bin16 (const bvn_float_t *f, uint16_t *out);
BVN_API void bvn_float_to_bin32 (const bvn_float_t *f, uint32_t *out);
BVN_API void bvn_float_to_bin64 (const bvn_float_t *f, uint64_t *out);
BVN_API void bvn_float_to_bin128(const bvn_float_t *f, uint32_t out[4]);
BVN_API void bvn_float_to_bin256(const bvn_float_t *f, uint32_t out[8]);
BVN_API bool bvn_float_from_bin16 (bvn_float_t *f, uint16_t bits);
BVN_API bool bvn_float_from_bin32 (bvn_float_t *f, uint32_t bits);
BVN_API bool bvn_float_from_bin64 (bvn_float_t *f, uint64_t bits);
BVN_API bool bvn_float_from_bin128(bvn_float_t *f, const uint32_t bits[4]);
BVN_API bool bvn_float_from_bin256(bvn_float_t *f, const uint32_t bits[8]);
/*
 * IEEE-style decimal interchange formats (custom binary storage layout, see
 * doc/1_bovnar_spec.md S5.2). to_dec* rounds the stored value to the format's
 * digit width with round-to-nearest-even and emits the CANONICAL cohort member:
 * the shortest coefficient (trailing zeros stripped), with the exponent clamped
 * only as the IEEE exponent range near Emax requires. The encoding is therefore
 * a deterministic function of the value alone -- the same number always
 * serialises to the same bytes regardless of how it was produced, and
 * to_dec*(from_dec*(bits)) reproduces bits for any value to_dec* can emit.
 * from_dec* decodes any valid cohort member; a non-canonical significand (more
 * digits than the format allows, possible only in malformed input -- e.g.
 * decimal16's 9-bit coefficient field versus its 2-digit maximum) is read as
 * zero, per IEEE 754 S3.5.2.
 */
BVN_API void bvn_float_to_dec16 (const bvn_float_t *f, uint16_t *out);
BVN_API void bvn_float_to_dec32 (const bvn_float_t *f, uint32_t *out);
BVN_API void bvn_float_to_dec64 (const bvn_float_t *f, uint64_t *out);
BVN_API void bvn_float_to_dec128(const bvn_float_t *f, uint32_t out[4]);
BVN_API void bvn_float_to_dec256(const bvn_float_t *f, uint32_t out[8]);
BVN_API bool bvn_float_from_dec16 (bvn_float_t *f, uint16_t bits);
BVN_API bool bvn_float_from_dec32 (bvn_float_t *f, uint32_t bits);
BVN_API bool bvn_float_from_dec64 (bvn_float_t *f, uint64_t bits);
BVN_API bool bvn_float_from_dec128(bvn_float_t *f, const uint32_t bits[4]);
BVN_API bool bvn_float_from_dec256(bvn_float_t *f, const uint32_t bits[8]);
BVN_API int16_t bvn_float_to_fix16 (const bvn_float_t *f, uint32_t frac_bits);
BVN_API int32_t bvn_float_to_fix32 (const bvn_float_t *f, uint32_t frac_bits);
BVN_API int64_t bvn_float_to_fix64 (const bvn_float_t *f, uint32_t frac_bits);
BVN_API void    bvn_float_to_fix128(const bvn_float_t *f, uint32_t frac_bits,
							 uint32_t out[4]);
BVN_API void    bvn_float_to_fix256(const bvn_float_t *f, uint32_t frac_bits,
							 uint32_t out[8]);
BVN_API bool bvn_float_from_fix16 (bvn_float_t *f, int16_t  bits, uint32_t frac_bits);
BVN_API bool bvn_float_from_fix32 (bvn_float_t *f, int32_t  bits, uint32_t frac_bits);
BVN_API bool bvn_float_from_fix64 (bvn_float_t *f, int64_t  bits, uint32_t frac_bits);
BVN_API bool bvn_float_from_fix128(bvn_float_t *f, const uint32_t bits[4],
							uint32_t frac_bits);
BVN_API bool bvn_float_from_fix256(bvn_float_t *f, const uint32_t bits[8],
							uint32_t frac_bits);
/*
 * Range checks for fixed-point (Q-format). Return true iff value * 2^frac_bits
 * rounds to a datum representable in a signed total_bits-bit field (total_bits
 * one of 16/32/64/128/256). The bvn_float_to_fixNN encoders saturate to the
 * representable extreme on overflow rather than wrapping.
 */
BVN_API bool bvn_float_fix_in_range(const bvn_float_t *f,
							uint32_t total_bits, uint32_t frac_bits);
BVN_API bool bvn_float_str_fits_fix(const char *s, uint32_t base,
							uint32_t total_bits, uint32_t frac_bits);
#ifdef __cplusplus
}
#endif
#endif

