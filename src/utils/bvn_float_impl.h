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

#ifndef BVN_FLOAT_IMPL_H_
#define BVN_FLOAT_IMPL_H_
#include "bvn_float.h"   /* public API (bvn_float_alloc/from_str/to_*): the
                          * binary and fixed-point string parsers below route
                          * through the arbitrary-precision heap engine, so they
                          * need its declarations regardless of include site. */
#include "bvn_int.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define BVN_FLOAT_INT_WORDS 64u
static inline bvn_int_t bvni_local_init(uint32_t *buf, uint32_t words)
{
	memset(buf, 0, (size_t)words * sizeof(uint32_t));
	bvn_int_t r;
	r.limbs    = buf;
	r.nlimbs   = words;
	r.nused    = 0;
	r.negative = false;
	r.heap     = false;
	r._reserved[0] = 0;   /* zero the reserved tail so the by-value return copies
	                       * no indeterminate bytes (matches bvn_int's other
	                       * initializers; keeps reserved fields 0 for future use) */
	r._reserved[1] = 0;
	return r;
}
#define BVN_INT_LOCAL(name)                                         \
	uint32_t       _lb_##name[BVN_FLOAT_INT_WORDS];                 \
	bvn_int_t name = bvni_local_init(_lb_##name, BVN_FLOAT_INT_WORDS)
typedef struct {
	bool overflow;
} bvn_float_ctx_t;
static inline void bvn_float_ctx_init   (bvn_float_ctx_t *ctx) { ctx->overflow = false; }
static inline void bvn_float_clear_overflow(bvn_float_ctx_t *ctx) { ctx->overflow = false; }
static inline bool bvn_float_has_overflow (const bvn_float_ctx_t *ctx) { return ctx->overflow; }
static inline void bvni_add_u32(bvn_float_ctx_t *ctx, bvn_int_t *b, uint32_t v)
{
	if (!bvn_int_add_u32(b, v)) ctx->overflow = true;
}
static inline void bvni_mul_u32(bvn_float_ctx_t *ctx, bvn_int_t *b, uint32_t v)
{
	if (!bvn_int_mul_u32(b, v)) ctx->overflow = true;
}
static inline void bvni_mul_pow10(bvn_float_ctx_t *ctx, bvn_int_t *b, int k)
{
	if (!bvn_int_mul_pow10(b, k)) ctx->overflow = true;
}
static inline void bvni_shl(bvn_float_ctx_t *ctx, bvn_int_t *b, int n)
{
	if (!bvn_int_shl(b, n)) ctx->overflow = true;
}
static inline void bvni_setbit(bvn_float_ctx_t *ctx, bvn_int_t *b, int i)
{
	if (!bvn_int_setbit(b, i)) ctx->overflow = true;
}
static inline void bvni_copy(bvn_float_ctx_t *ctx, bvn_int_t *dst, const bvn_int_t *src)
{
	if (!bvn_int_copy(dst, src)) ctx->overflow = true;
}
typedef struct {
	bool      neg;
	bool      inf;
	bool      nan;
	int       dex;
	uint32_t  _coeff_buf[BVN_FLOAT_INT_WORDS];
	bvn_int_t coeff;
} PNum;
static inline void bvn_float_pnum_init(PNum *p)
{
	p->neg = false; p->inf = false; p->nan = false;
	p->dex = 0;
	p->coeff = bvni_local_init(p->_coeff_buf, BVN_FLOAT_INT_WORDS);
}
static inline int bvni_count_decimal_digits(bvn_float_ctx_t *ctx, const bvn_int_t *b)
{
	if (bvn_int_is_zero(b)) return 0;
	BVN_INT_LOCAL(t);
	bvni_copy(ctx, &t, b);
	int digits = 0;
	while (!bvn_int_is_zero(&t)) {
		bvn_int_div_u32(&t, 10u);
		digits++;
	}
	return digits;
}
static inline bool bvn_float_parse(bvn_float_ctx_t *ctx, const char *s, PNum *r)
{
	r->neg = false; r->inf = false; r->nan = false;
	bvn_int_zero(&r->coeff);
	r->dex = 0;
	bvn_float_clear_overflow(ctx);
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
	if (*s == '+') s++;
	else if (*s == '-') { r->neg = true; s++; }
	{
		char c1 = *s | 0x20;
		if (c1 == 'i' && (s[1]|0x20)=='n' && (s[2]|0x20)=='f')
			{ r->inf = true; return true; }
		if (c1 == 'n' && (s[1]|0x20)=='a' && (s[2]|0x20)=='n')
			{ r->nan = true; return true; }
	}
	uint32_t acc = 0;
	int acc_digs = 0, int_digs = 0;
	while (*s >= '0' && *s <= '9') {
		if (bvn_float_has_overflow(ctx)) break;
		acc = acc * 10u + (uint32_t)(*s - '0');
		acc_digs++; int_digs++;
		if (acc_digs == 9) {
			bvni_mul_u32(ctx, &r->coeff, 1000000000u);
			bvni_add_u32(ctx, &r->coeff, acc);
			acc = 0; acc_digs = 0;
		}
		s++;
	}
	int frac_digs = 0;
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') {
			if (bvn_float_has_overflow(ctx)) break;
			acc = acc * 10u + (uint32_t)(*s - '0');
			acc_digs++; frac_digs++;
			if (acc_digs == 9) {
				bvni_mul_u32(ctx, &r->coeff, 1000000000u);
				bvni_add_u32(ctx, &r->coeff, acc);
				acc = 0; acc_digs = 0;
			}
			s++;
		}
	}
	if (acc_digs > 0) {
		if (!bvn_float_has_overflow(ctx)) {
			bvni_mul_pow10(ctx, &r->coeff, acc_digs);
			if (!bvn_float_has_overflow(ctx))
				bvni_add_u32(ctx, &r->coeff, acc);
		}
	}
	r->dex = -frac_digs;
	if (int_digs == 0 && frac_digs == 0) return false;
	if (*s == 'e' || *s == 'E') {
		s++;
		bool eneg = false;
		if      (*s == '+') s++;
		else if (*s == '-') { eneg = true; s++; }
		int eabs = 0; bool has_e = false; bool eabs_ovf = false;
		while (*s >= '0' && *s <= '9') {
			if (!eabs_ovf) {
				if (eabs > (INT_MAX - (*s - '0')) / 10) eabs_ovf = true;
				else eabs = eabs * 10 + (*s - '0');
			}
			has_e = true; s++;
		}
		if (!has_e) return false;
		if (eabs_ovf) {
			r->inf = !eneg;
			bvn_int_zero(&r->coeff);
			r->dex = 0;
			return true;
		}
		r->dex += eneg ? -eabs : eabs;
	}
	while (!bvn_int_is_zero(&r->coeff) && !bvn_float_has_overflow(ctx)) {
		uint32_t rem = bvn_int_div_u32(&r->coeff, 10u);
		if (rem != 0) {
			bvni_mul_u32(ctx, &r->coeff, 10u);
			bvni_add_u32(ctx, &r->coeff, rem);
			break;
		}
		r->dex++;
	}
	if (bvn_int_is_zero(&r->coeff)) r->dex = 0;
	if (bvn_float_has_overflow(ctx)) {
		r->inf = true;
		bvn_int_zero(&r->coeff);
		r->dex = 0;
	}
	return true;
}
#ifndef BVN_FLOAT_RO_DIGITS
#define BVN_FLOAT_RO_DIGITS 160
#endif
/*
 * Decimal-domain sibling of bvn_float_parse, used only by the DECIMAL
 * interchange encoders. It is identical to bvn_float_parse except that it
 * retains at most BVN_FLOAT_RO_DIGITS significant digits in the coefficient;
 * any further significant digits are folded into the decimal exponent and
 * collapsed into a single round-to-ODD on the retained coefficient (its last
 * decimal digit is forced odd iff the dropped tail was non-zero -- a decimal
 * integer is even exactly when its low bit is clear, and +1 on an even value
 * never carries out of the units digit, so the digit count cannot grow).
 *
 * to_ieee_decimal then performs its own round-to-nearest-even down to the
 * format width; decimal round-to-odd composes exactly with a later same-radix
 * round-to-nearest, so the final decimal result is correctly rounded. The cap
 * sits far above every decimal format's max_coeff_digs (so ordinary inputs are
 * untouched and round identically to before) yet far below the ~616-digit
 * fixed-buffer ceiling, which is what stops a long finite literal from
 * overflowing the coefficient and mis-saturating to Infinity.
 *
 * This must NOT back the binary or fixed-point encoders: decimal round-to-odd
 * does not compose with a binary round-to-nearest (an exact binary midpoint has
 * a many-digit decimal expansion that the cap would perturb, breaking
 * ties-to-even), so those paths round an exact rational via the heap engine
 * instead.
 */
static inline bool bvn_float_parse_ro(bvn_float_ctx_t *ctx, const char *s, PNum *r)
{
	r->neg = false; r->inf = false; r->nan = false;
	bvn_int_zero(&r->coeff);
	r->dex = 0;
	bvn_float_clear_overflow(ctx);
	if (!s) return false;
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
	if (*s == '+') s++;
	else if (*s == '-') { r->neg = true; s++; }
	{
		char c1 = *s | 0x20;
		if (c1 == 'i' && (s[1]|0x20)=='n' && (s[2]|0x20)=='f')
			{ r->inf = true; return true; }
		if (c1 == 'n' && (s[1]|0x20)=='a' && (s[2]|0x20)=='n')
			{ r->nan = true; return true; }
	}
	uint32_t acc = 0;
	int acc_digs = 0, int_digs = 0, frac_digs = 0;
	int  sig = 0;          /* significant digits retained in the coefficient */
	int  extra_sig = 0;    /* significant digits dropped past the cap        */
	bool started = false;  /* first non-zero significant digit seen          */
	bool sticky = false;   /* a dropped significant digit was non-zero       */
	while (*s >= '0' && *s <= '9') {
		int d = *s - '0';
		int_digs++;
		if (started || d != 0) {
			started = true;
			if (sig < BVN_FLOAT_RO_DIGITS) {
				acc = acc * 10u + (uint32_t)d;
				acc_digs++; sig++;
				if (acc_digs == 9) {
					bvni_mul_u32(ctx, &r->coeff, 1000000000u);
					bvni_add_u32(ctx, &r->coeff, acc);
					acc = 0; acc_digs = 0;
				}
			} else {
				extra_sig++;
				if (d != 0) sticky = true;
			}
		}
		s++;
	}
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') {
			int d = *s - '0';
			frac_digs++;
			if (started || d != 0) {
				started = true;
				if (sig < BVN_FLOAT_RO_DIGITS) {
					acc = acc * 10u + (uint32_t)d;
					acc_digs++; sig++;
					if (acc_digs == 9) {
						bvni_mul_u32(ctx, &r->coeff, 1000000000u);
						bvni_add_u32(ctx, &r->coeff, acc);
						acc = 0; acc_digs = 0;
					}
				} else {
					extra_sig++;
					if (d != 0) sticky = true;
				}
			}
			s++;
		}
	}
	if (acc_digs > 0) {
		bvni_mul_pow10(ctx, &r->coeff, acc_digs);
		bvni_add_u32(ctx, &r->coeff, acc);
	}
	/* dropped significant digits raise the exponent by their count; dropped
	 * fractional digits below the cap contribute only to the sticky bit */
	r->dex = -frac_digs + extra_sig;
	if (int_digs == 0 && frac_digs == 0) return false;
	if (*s == 'e' || *s == 'E') {
		s++;
		bool eneg = false;
		if      (*s == '+') s++;
		else if (*s == '-') { eneg = true; s++; }
		int eabs = 0; bool has_e = false; bool eabs_ovf = false;
		while (*s >= '0' && *s <= '9') {
			if (!eabs_ovf) {
				if (eabs > (INT_MAX - (*s - '0')) / 10) eabs_ovf = true;
				else eabs = eabs * 10 + (*s - '0');
			}
			has_e = true; s++;
		}
		if (!has_e) return false;
		if (eabs_ovf) {
			r->inf = !eneg;
			bvn_int_zero(&r->coeff);
			r->dex = 0;
			return true;
		}
		r->dex += eneg ? -eabs : eabs;
	}
	if (sticky && bvn_int_getbit(&r->coeff, 0) == 0)
		bvni_add_u32(ctx, &r->coeff, 1u);
	while (!bvn_int_is_zero(&r->coeff) && !bvn_float_has_overflow(ctx)) {
		uint32_t rem = bvn_int_div_u32(&r->coeff, 10u);
		if (rem != 0) {
			bvni_mul_u32(ctx, &r->coeff, 10u);
			bvni_add_u32(ctx, &r->coeff, rem);
			break;
		}
		r->dex++;
	}
	if (bvn_int_is_zero(&r->coeff)) r->dex = 0;
	if (bvn_float_has_overflow(ctx)) {
		r->inf = true;
		bvn_int_zero(&r->coeff);
		r->dex = 0;
	}
	return true;
}
typedef struct {
	int exp_bits;
	int man_bits;
	int bias;
} BinFmt;
static inline void to_ieee_binary(bvn_float_ctx_t *ctx, const PNum *p, const BinFmt *f,
								   uint32_t *bits, int bits32)
{
	int pbits = f->man_bits;
	int ebits = f->exp_bits;
	int bias  = f->bias;
	int eall  = (1 << ebits) - 1;
	int total = 1 + ebits + pbits;
	for (int i = 0; i < bits32; i++) bits[i] = 0;
	bvn_float_clear_overflow(ctx);
	if (p->neg)
		bits[(total - 1) / 32] |= (1u << ((total - 1) % 32));
	if (p->nan) {
		for (int i = 0; i < ebits; i++)
			bits[(pbits + i) / 32] |= (1u << ((pbits + i) % 32));
		if (pbits > 0) {
			int wi = (pbits - 1) / 32, bi2 = (pbits - 1) % 32;
			bits[wi] |= (1u << bi2);
		}
		return;
	}
	if (p->inf) {
		for (int i = 0; i < ebits; i++)
			bits[(pbits + i) / 32] |= (1u << ((pbits + i) % 32));
		return;
	}
	if (bvn_int_is_zero(&p->coeff)) return;
	BVN_INT_LOCAL(num);
	BVN_INT_LOCAL(den);
	bvni_copy(ctx, &num, &p->coeff);
	bvn_int_from_uint64(&den, 1u);
	if (p->dex >= 0) bvni_mul_pow10(ctx, &num,  p->dex);
	else             bvni_mul_pow10(ctx, &den, -p->dex);
	if (bvn_float_has_overflow(ctx)) {
		for (int i = 0; i < ebits; i++)
			bits[(pbits + i) / 32] |= (1u << ((pbits + i) % 32));
		return;
	}
	int E;
	{
		int nb = bvn_int_bitlen(&num), db = bvn_int_bitlen(&den);
		E = nb - db;
		if (E >= 0) {
			BVN_INT_LOCAL(t);
			bvni_copy(ctx, &t, &den); bvni_shl(ctx, &t, E);
			if (bvn_int_cmp(&t, &num) > 0) E--;
		} else {
			BVN_INT_LOCAL(t);
			bvni_copy(ctx, &t, &num); bvni_shl(ctx, &t, -E);
			if (bvn_int_cmp(&den, &t) > 0) E--;
		}
	}
	if (E > eall - bias - 1) {
		for (int i = 0; i < ebits; i++)
			bits[(pbits + i) / 32] |= (1u << ((pbits + i) % 32));
		return;
	}
	int be = E + bias;
	int shift = pbits + 2 - E;
	bool fits;
	if (shift >= 0)
		fits = (bvn_int_bitlen(&num) + shift <= (int)(BVN_FLOAT_INT_WORDS * 32u));
	else
		fits = (bvn_int_bitlen(&den) + (-shift) <= (int)(BVN_FLOAT_INT_WORDS * 32u));
	if (fits && be > 0) {
		BVN_INT_LOCAL(Q);
		BVN_INT_LOCAL(R);
		if (shift >= 0) {
			BVN_INT_LOCAL(tmp);
			bvni_copy(ctx, &tmp, &num); bvni_shl(ctx, &tmp, shift);
			bvn_int_divrem(&Q, &R, &tmp, &den);
		} else {
			BVN_INT_LOCAL(D);
			bvni_copy(ctx, &D, &den); bvni_shl(ctx, &D, -shift);
			bvn_int_divrem(&Q, &R, &num, &D);
		}
		bool sticky  = !bvn_int_is_zero(&R);
		bool rbit    = (bool)bvn_int_getbit(&Q, 0);
		bool gbit    = (bool)bvn_int_getbit(&Q, 1);
		bool lsbit   = (pbits > 0) ? (bool)bvn_int_getbit(&Q, 2) : false;
		bool round_up = gbit && (rbit || sticky || lsbit);
		bvn_int_shr(&Q, 2);
		if (round_up) {
			bvni_add_u32(ctx, &Q, 1u);
			if (bvn_int_bitlen(&Q) > pbits + 1) {
				bvn_int_shr(&Q, 1);
				be++;
				if (be >= eall) {
					for (int i = 0; i < ebits; i++)
						bits[(pbits + i) / 32] |= (1u << ((pbits + i) % 32));
					return;
				}
			}
		}
		{
			int wi = pbits / 32, b2 = pbits % 32;
			if ((uint32_t)wi < Q.nlimbs) Q.limbs[wi] &= ~(1u << b2);
			bvn_int_norm(&Q);
		}
		{
			BVN_INT_LOCAL(eb);
			bvn_int_from_uint64(&eb, (uint64_t)be);
			bvni_shl(ctx, &eb, pbits);
			for (uint32_t i = 0; i < eb.nused && (int)i < bits32; i++)
				bits[i] |= eb.limbs[i];
		}
		for (uint32_t i = 0; i < Q.nused && (int)i < bits32; i++)
			bits[i] |= Q.limbs[i];
		return;
	}
	BVN_INT_LOCAL(rem);
	BVN_INT_LOCAL(scale);
	if (E >= 0) {
		bvni_copy(ctx, &rem, &num);
		bvni_copy(ctx, &scale, &den); bvni_shl(ctx, &scale, E);
		if (!bvn_int_sub_inplace(&rem, &scale)) { ctx->overflow = true; return; }
	} else {
		bvni_copy(ctx, &rem, &num); bvni_shl(ctx, &rem, -E);
		if (!bvn_int_sub_inplace(&rem, &den))   { ctx->overflow = true; return; }
		bvni_copy(ctx, &scale, &den);
	}
	const int mb_total = pbits + 2;
	uint8_t *mb = (uint8_t *)malloc((size_t)mb_total);
	if (!mb) { ctx->overflow = true; return; }
	memset(mb, 0, (size_t)mb_total);
	int mb_count = mb_total;
	for (int i = 0; i < mb_count; i++) {
		bvni_shl(ctx, &rem, 1);
		if (bvn_int_cmp(&rem, &scale) >= 0) {
			mb[i] = 1;
			if (!bvn_int_sub_inplace(&rem, &scale)) {
				ctx->overflow = true; free(mb); return;
			}
		} else {
			mb[i] = 0;
		}
	}
	bool sticky = !bvn_int_is_zero(&rem);
	if (be <= 0) {
		int sub_shift = 1 - be;
		be = 0;
		uint8_t *mb2 = (uint8_t *)malloc((size_t)mb_total);
		if (!mb2) { ctx->overflow = true; free(mb); return; }
		memset(mb2, 0, (size_t)mb_total);
		if (sub_shift - 1 < mb_total) mb2[sub_shift - 1] = 1;
		bool new_sticky = sticky;
		for (int i = 0; i < mb_count; i++) {
			if (i + sub_shift < mb_total)
				mb2[i + sub_shift] = mb[i];
			else if (mb[i])
				new_sticky = true;
		}
		memcpy(mb, mb2, (size_t)mb_total);
		free(mb2);
		mb_count = mb_total;
		sticky = new_sticky;
	}
	bool guard    = (pbits     < mb_count) ? (bool)mb[pbits]     : false;
	bool rbit2    = (pbits + 1 < mb_count) ? (bool)mb[pbits + 1] : false;
	bool lsbit2   = (pbits > 0)            ? (bool)mb[pbits - 1] : false;
	bool round_up2 = guard && (rbit2 || sticky || lsbit2);
	BVN_INT_LOCAL(mant);
	for (int i = 0; i < pbits; i++)
		if (mb[i]) bvni_setbit(ctx, &mant, pbits - 1 - i);
	free(mb);
	mb = NULL;
	if (round_up2) {
		bvni_add_u32(ctx, &mant, 1u);
		if (bvn_int_bitlen(&mant) > pbits) {
			bvn_int_zero(&mant); be++;
			if (be >= eall) {
				for (int i = 0; i < ebits; i++)
					bits[(pbits + i) / 32] |= (1u << ((pbits + i) % 32));
				return;
			}
		}
	}
	if (be > 0) {
		BVN_INT_LOCAL(eb);
		bvn_int_from_uint64(&eb, (uint64_t)be);
		bvni_shl(ctx, &eb, pbits);
		for (uint32_t i = 0; i < eb.nused && (int)i < bits32; i++)
			bits[i] |= eb.limbs[i];
	}
	for (uint32_t i = 0; i < mant.nused && (int)i < bits32; i++)
		bits[i] |= mant.limbs[i];
}
typedef struct {
	int total_bits;
	int exp_bits;
	int coeff_bits;
	int bias;
	int max_coeff_digs;
} DecFmt;
/*
 * Classify a decimal width as "BID-standard" vs bovnar's in-house layout. The
 * test is purely the IEEE relation bias == 3*2^(exp_bits-3) + p - 2 (Emax =
 * 3*2^(exp_bits-3), bias = Emax + p - 2). This holds for the genuine IEEE-754
 * interchange formats decimal32/64/128 and, by construction of its parameters
 * (exp_bits=6, p=2, bias=24), for bovnar's decimal16 as well — so decimal16 is
 * encoded exactly like the IEEE widths. Only decimal256 fails the relation and
 * uses the in-house layout. BID-standard formats use the combination field —
 * the "11" coefficient prefix and the 1111x Infinity/NaN encoding — and cap the
 * biased exponent at 3*2^(exp_bits-2); the in-house width uses the full exponent
 * field with an all-ones special encoding.
 *
 * NOTE: the standard/in-house split is *inferred* from the bias here. That makes
 * the encoding silently dependent on the exact (exp_bits, p, bias) tuple: if
 * decimal16's bias were ever changed it could flip layout. The tuples are fixed
 * by the on-disk format (doc/03_bovnar_spec.md §5.2) and must not be altered.
 */
static inline bool bvnf_dec_is_standard(int exp_bits, int max_coeff_digs, int bias)
{
	return bias == 3 * (1 << (exp_bits - 3)) + max_coeff_digs - 2;
}
static inline void to_ieee_decimal(bvn_float_ctx_t *ctx, const PNum *p, const DecFmt *f,
									uint32_t *bits, int bits32)
{
	int cbits = f->coeff_bits;
	int ebits = f->exp_bits;
	int bias  = f->bias;
	bool is_std = bvnf_dec_is_standard(ebits, f->max_coeff_digs, bias);
	/* Standard formats reserve the "11" combination prefix, so their biased
	 * exponent tops out at 3*2^(exp_bits-2); the in-house width uses the full
	 * field, reserving only the all-ones code for Infinity/NaN. */
	int be_max = is_std ? (3 << (ebits - 2)) : ((1 << ebits) - 1);
	int total = f->total_bits;
	for (int i = 0; i < bits32; i++) bits[i] = 0;
	bvn_float_clear_overflow(ctx);
	if (p->nan || p->inf) {
		/*
		 * Special values. Standard formats use the IEEE combination field: the
		 * four bits below the sign are 1111, and the fifth bit selects Infinity
		 * (0) or NaN (1) — matching the BID encoding emitted by libbid/hardware.
		 * The in-house width keeps bovnar's own convention: every exponent-field
		 * bit set, with NaN flagged by the top coefficient bit.
		 */
		if (is_std) {
			for (int i = 0; i < 4; i++) {
				int pos = total - 2 - i;
				if (pos >= 0) bits[pos / 32] |= (1u << (pos % 32));
			}
			if (p->nan) {
				int pos = total - 6;
				if (pos >= 0) bits[pos / 32] |= (1u << (pos % 32));
			}
		} else {
			for (int i = 0; i < ebits; i++) {
				int pos = total - 2 - i;
				if (pos >= 0)
					bits[pos / 32] |= (1u << (pos % 32));
			}
			if (p->nan && cbits > 0) {
				int wi = (cbits - 1) / 32, bi2 = (cbits - 1) % 32;
				bits[wi] |= (1u << bi2);
			}
		}
		if (p->neg) bits[(total-1)/32] |= (1u << ((total-1)%32));
		return;
	}
	if (bvn_int_is_zero(&p->coeff)) {
		if (p->neg) bits[(total-1)/32] |= (1u << ((total-1)%32));
		return;
	}
	BVN_INT_LOCAL(C);
	bvni_copy(ctx, &C, &p->coeff);
	int E = p->dex;
	{
		/*
		 * Reduce the coefficient in a SINGLE round-to-nearest, ties-to-even step
		 * that satisfies both width constraints at once: at most max_coeff_digs
		 * significant digits AND a biased exponent of at least 0 (the subnormal
		 * floor). Each dropped low-order digit raises E by one, so the number of
		 * digits to drop is the larger of the two requirements — the digit-count
		 * surplus (dig - max_coeff_digs) and the subnormal shift (-(E + bias)).
		 * Rounding only once, at that combined resolution, is what keeps a
		 * many-digit value that lands in the subnormal range from being rounded
		 * twice (first to max_coeff_digs, then again down to the subnormal width),
		 * which could be off by a unit in the last place. The guard is the most
		 * significant dropped digit; every lower dropped digit folds into sticky;
		 * a carry that regrows the count past the limit (…999 -> 1000) drops one
		 * further (exact, trailing-zero) digit.
		 *
		 * For values whose exponent is already in range (be >= 0) the subnormal
		 * term is non-positive and this reduces exactly to the old digit-count
		 * trim; for in-range-digit subnormals (dig <= max_coeff_digs) it reduces
		 * exactly to the old subnormal trim. Only the genuine many-digit-subnormal
		 * overlap — previously double-rounded — changes behaviour.
		 */
		int dig       = bvni_count_decimal_digits(ctx, &C);
		int drop_digs = dig - f->max_coeff_digs;
		int drop_exp  = -(E + bias);
		int drop      = drop_digs > drop_exp ? drop_digs : drop_exp;
		if (drop > 0) {
			bool     sticky = false;
			uint32_t guard  = 0u;
			for (int i = 0; i < drop; i++) {
				if (bvn_int_is_zero(&C)) { guard = 0u; break; } /* rest, incl. guard, are 0 */
				uint32_t d2 = bvn_int_div_u32(&C, 10u);
				if (i == drop - 1)      guard  = d2;
				else if (d2 != 0u)      sticky = true;
			}
			E += drop;
			if ((guard > 5u) ||
				(guard == 5u && (sticky || bvn_int_getbit(&C, 0) == 1))) {
				bvni_add_u32(ctx, &C, 1u);
				if (bvni_count_decimal_digits(ctx, &C) > f->max_coeff_digs) {
					bvn_int_div_u32(&C, 10u);
					E++;
				}
			}
			if (bvn_int_is_zero(&C)) {   /* rounded away (subnormal underflow to 0) */
				if (p->neg) bits[(total-1)/32] |= (1u << ((total-1)%32));
				return;
			}
		}
	}
	/*
	 * Canonicalise the cohort. A decimal value has many encodings that differ
	 * only in their quantum (coefficient C with trailing zeros vs C/10 with the
	 * exponent raised); they denote the same number. The encoder must pick one
	 * deterministically, otherwise the same value serialises to different bytes
	 * depending on provenance: the parser already strips trailing zeros, but the
	 * rounding step above can reintroduce them (e.g. 7.07560058344503e27 rounds
	 * to coefficient 7075600583445030), so a freshly-rounded value and the same
	 * value decoded-then-re-encoded would land on different cohort members.
	 *
	 * We canonicalise to the shortest coefficient: strip trailing zero digits,
	 * raising E by one each time. Stripping raises the biased exponent, so it is
	 * bounded so be never reaches be_max; the clamp loop below re-adds exactly
	 * the trailing zeros that values near Emax require, and that clamped form is
	 * then the canonical representative for those (it is the unique shortest
	 * coefficient whose exponent still fits). The result is a function of the
	 * value alone, independent of how the coefficient was produced.
	 */
	while (E + bias < be_max - 1) {
		uint32_t r = bvn_int_div_u32(&C, 10u);
		if (r != 0u) {                 /* not a trailing zero: undo and stop */
			bvni_mul_u32(ctx, &C, 10u);
			bvni_add_u32(ctx, &C, r);
			break;
		}
		E++;
	}
	int be = E + bias;
	if (be < 0) be = 0;   /* unreachable: the combined drop already lifts be >= 0 */
	/*
	 * Clamp to the maximum encodable exponent. The magnitude is in range but
	 * the shortest coefficient (trailing zeros stripped above) leaves the
	 * biased exponent above be_max. IEEE 754 requires
	 * lengthening the coefficient with trailing zeros and lowering the
	 * exponent until it fits, rather than rounding to Infinity. Only when the
	 * coefficient already uses the full max_coeff_digs digits and be is still
	 * out of range is the value a genuine overflow.
	 */
	while (be >= be_max &&
		   bvni_count_decimal_digits(ctx, &C) < f->max_coeff_digs) {
		bvni_mul_u32(ctx, &C, 10u);
		E--; be--;
	}
	if (be >= be_max) {
		/*
		 * Finite magnitude too large for this format's exponent range rounds to
		 * Infinity. Emit the same special pattern the p->inf path uses: the 1111x
		 * combination (fifth bit 0) for standard formats, all exponent bits for
		 * the in-house width. (Emitting all-ones unconditionally would decode as
		 * NaN under the standard 1111x scheme.)
		 */
		if (is_std) {
			for (int i = 0; i < 4; i++) {
				int pos = total - 2 - i;
				if (pos >= 0) bits[pos / 32] |= (1u << (pos % 32));
			}
		} else {
			for (int i = 0; i < ebits; i++) {
				int pos = total - 2 - i;
				if (pos >= 0)
					bits[pos / 32] |= (1u << (pos % 32));
			}
		}
		if (p->neg) bits[(total-1)/32] |= (1u << ((total-1)%32));
		return;
	}
	/*
	 * Pack the biased exponent and coefficient. CASE A — the coefficient fits in
	 * coeff_bits — places it directly in the trailing field with the exponent
	 * above it; this is the only case for the in-house width and for decimal128.
	 * CASE B — the coefficient needs one more bit, reachable for decimal32/64
	 * whose 10^p exceeds 2^coeff_bits — uses the BID combination field: a "11"
	 * prefix, then the exponent, then the low tL = total-3-exp_bits coefficient
	 * bits with the implicit 2^coeff_bits top bit cleared.
	 */
	if (bvn_int_bitlen(&C) <= cbits) {
		BVN_INT_LOCAL(ebig);
		bvn_int_from_uint64(&ebig, (uint64_t)be);
		bvni_shl(ctx, &ebig, cbits);
		for (uint32_t i = 0; i < ebig.nused && (int)i < bits32; i++)
			bits[i] |= ebig.limbs[i];
		for (uint32_t i = 0; i < C.nused && (int)i < bits32; i++)
			bits[i] |= C.limbs[i];
	} else {
		int tL = total - 3 - ebits;
		{
			int wi = cbits / 32, b2 = cbits % 32;
			if ((uint32_t)wi < C.nlimbs) C.limbs[wi] &= ~(1u << b2);
			bvn_int_norm(&C);
		}
		BVN_INT_LOCAL(ebig);
		bvn_int_from_uint64(&ebig, (uint64_t)be);
		bvni_shl(ctx, &ebig, tL);
		BVN_INT_LOCAL(combo);
		bvn_int_from_uint64(&combo, 3u);
		bvni_shl(ctx, &combo, total - 3);
		for (uint32_t i = 0; i < combo.nused && (int)i < bits32; i++)
			bits[i] |= combo.limbs[i];
		for (uint32_t i = 0; i < ebig.nused && (int)i < bits32; i++)
			bits[i] |= ebig.limbs[i];
		for (uint32_t i = 0; i < C.nused && (int)i < bits32; i++)
			bits[i] |= C.limbs[i];
	}
	if (p->neg) {
		int sign_pos = total - 1;
		bits[sign_pos / 32] |= (1u << (sign_pos % 32));
	}
}
static inline void to_fixed_point(bvn_float_ctx_t *ctx,
								   const PNum *p, int total_bits, int frac_bits,
								   uint32_t *bits, int bits32)
{
	for (int i = 0; i < bits32; i++) bits[i] = 0;
	bvn_float_clear_overflow(ctx);
	if (p->nan || p->inf) return;
	BVN_INT_LOCAL(num);
	BVN_INT_LOCAL(den);
	bvni_copy(ctx, &num, &p->coeff);
	bvn_int_from_uint64(&den, 1u);
	if (p->dex >= 0) bvni_mul_pow10(ctx, &num,  p->dex);
	else             bvni_mul_pow10(ctx, &den, -p->dex);
	if (bvn_float_has_overflow(ctx)) return;
	bvni_shl(ctx, &num, frac_bits);
	/*
	 * The shift can itself overflow the fixed BVN_INT_LOCAL budget (it did not
	 * before mul_pow10 cleared, but a large frac_bits or a wide coefficient can
	 * trip it here). Re-check before dividing: on a fixed buffer bvni_shl leaves
	 * num partially shifted, so divrem would otherwise run on a truncated
	 * numerator and emit a silently wrong (though in-bounds) result. The public
	 * bvnf_to_fix_direct guards its shift the same way.
	 */
	if (bvn_float_has_overflow(ctx)) return;
	BVN_INT_LOCAL(Q);
	BVN_INT_LOCAL(R);
	bvn_int_divrem(&Q, &R, &num, &den);
	BVN_INT_LOCAL(twoR);
	bvni_copy(ctx, &twoR, &R); bvni_shl(ctx, &twoR, 1);
	int cmp = bvn_int_cmp(&twoR, &den);
	if (cmp > 0 || (cmp == 0 && bvn_int_getbit(&Q, 0)))
		bvni_add_u32(ctx, &Q, 1u);
	if (p->neg) {
		int w = (total_bits + 31) / 32;
		if (w > (int)BVN_FLOAT_INT_WORDS) w = (int)BVN_FLOAT_INT_WORDS;
		for (int i = (int)Q.nused; i < w; i++) Q.limbs[i] = 0u;
		Q.nused = (uint32_t)w;
		for (int i = 0; i < w; i++) Q.limbs[i] = ~Q.limbs[i];
		uint64_t carry = 1;
		for (int i = 0; i < w && carry; i++) {
			uint64_t s = (uint64_t)Q.limbs[i] + carry;
			Q.limbs[i] = (uint32_t)s;
			carry       = s >> 32;
		}
	}
	int w = (total_bits + 31) / 32;
	for (int i = 0; i < (int)Q.nused && i < w && i < bits32; i++)
		bits[i] = Q.limbs[i];
	if (total_bits % 32 != 0 && w <= bits32 && w > 0) {
		uint32_t mask = ((uint32_t)1 << (total_bits % 32)) - 1u;
		if (p->neg && (bits[w-1] & ((uint32_t)1 << ((total_bits%32)-1))))
			bits[w-1] |= ~mask;
		else
			bits[w-1] &= mask;
	}
}
/*
 * True when the numeric literal is negative, mirroring the sign handling in
 * bvn_float_parse. Used only to re-apply the sign bit of a NaN result: the heap
 * engine yields a canonical (positive) NaN, but bovnar's encoders and tests
 * treat "-nan" as sign-set, so the rerouted binary parsers restore it.
 */
static inline bool bvnf_leading_minus(const char *s)
{
	if (!s) return false;
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
	return *s == '-';
}
/*
 * Intermediate precision for the fixed-point string parsers. Rounding
 * value*2^frac to a Q-format integer needs the exact rational: a finite decimal
 * can fall arbitrarily close to a fixed-point half-ulp, so a binary
 * intermediate must carry enough bits that it lands on the correct side of
 * every tie. total_bits covers the result, frac_bits the fractional binary
 * positions, and ~log2(10) < 4 bits per input digit bound the decimal exponent;
 * +64 is slack. Clamped to the engine's precision ceiling (an input long enough
 * to need more than that is far beyond any real fixed-point literal).
 */
static inline uint32_t bvnf_fix_prec(int total_bits, int frac_bits, const char *s)
{
	size_t n = s ? strlen(s) : (size_t)0;
	unsigned long long p = (unsigned long long)total_bits
	    + (frac_bits > 0 ? (unsigned long long)frac_bits : 0ull)
	    + 4ull * (unsigned long long)n + 64ull;
	if (p < 64ull)            p = 64ull;
	if (p > BVN_FLOAT_MAX_PREC) p = BVN_FLOAT_MAX_PREC;
	return (uint32_t)p;
}
/*
 * The binary and fixed-point string parsers below route through the
 * arbitrary-precision heap engine (bvn_float_from_str at a precision wide enough
 * to be double-rounding-free, then the validated bvn_float_to_* encoder) rather
 * than the fixed 2048-bit PNum scratch. The scratch path formed coeff / 10^|exp|
 * in a single fixed buffer and saturated to +/-Infinity (binary) or 0 (fixed)
 * whenever 10^|exp| overflowed it, so a long finite literal -- e.g. a value near
 * 1.0 written with hundreds of digits, or a representable subnormal whose exact
 * rational needs more than 2048 bits -- was converted incorrectly. The heap
 * engine (to 32768 bits) has no such ceiling. For each binary width the
 * intermediate precision is >= 2p+2 (p = stored mantissa bits + 1), which makes
 * the final narrowing in bvn_float_to_binNN provably free of double rounding,
 * including subnormals. The DECIMAL parsers do NOT route this way (see
 * bvn_float_parse_ro): a binary intermediate cannot represent decimal ties.
 */
static inline uint16_t bvn_float_parse_bin16(const char *s)
{
	uint32_t b[1] = { 0u };
	bvn_float_strtoieee_bin(s, 10u, 5u, 10u, 15, b, 1);
	return (uint16_t)b[0];
}
/*
 * _Float16 is a compiler/target extension (and only since C23 a standard
 * optional type), exactly like _Float128 and the _Decimal* helpers below, so it
 * must be feature-tested rather than merely have its pedantic warning silenced.
 * Without the guard, every translation unit that includes this header fails to
 * COMPILE on any target lacking _Float16 (e.g. x86-64 built with -mno-sse, or
 * embedded cores without a half-precision type) -- a hard error, not a warning.
 * The raw-bits packer bvn_float_parse_bin16 above stays unconditional; only the
 * _Float16-typed convenience wrapper is gated, mirroring bvn_float_parse_f128.
 */
#if defined(__FLT16_MAX__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline _Float16 bvn_float_parse_f16(const char *s)
{
	union { uint16_t u; _Float16 f; } c;
	c.u = bvn_float_parse_bin16(s); return c.f;
}
#pragma GCC diagnostic pop
#endif
static inline uint32_t bvn_float_parse_bin32(const char *s)
{
	uint32_t b[1] = { 0u };
	bvn_float_strtoieee_bin(s, 10u, 8u, 23u, 127, b, 1);
	return b[0];
}
static inline float bvn_float_parse_f32(const char *s)
{
	union { uint32_t u; float f; } c;
	c.u = bvn_float_parse_bin32(s); return c.f;
}
static inline uint64_t bvn_float_parse_bin64(const char *s)
{
	uint32_t b[2] = { 0u, 0u };
	bvn_float_strtoieee_bin(s, 10u, 11u, 52u, 1023, b, 2);
	return (uint64_t)b[0] | ((uint64_t)b[1] << 32);
}
static inline double bvn_float_parse_f64(const char *s)
{
	union { uint64_t u; double f; } c;
	c.u = bvn_float_parse_bin64(s); return c.f;
}
static inline void bvn_float_parse_bin128(const char *s, uint32_t out[4])
{
	bvn_float_strtoieee_bin(s, 10u, 15u, 112u, 16383, out, 4);
}
#if defined(__SIZEOF_FLOAT128__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline _Float128 bvn_float_parse_f128(const char *s)
{
	union { uint32_t u[4]; _Float128 f; } c;
	bvn_float_parse_bin128(s, c.u); return c.f;
}
#pragma GCC diagnostic pop
#endif
static inline void bvn_float_parse_bin256(const char *s, uint32_t out[8])
{
	bvn_float_strtoieee_bin(s, 10u, 19u, 236u, 262143, out, 8);
}
static inline uint16_t bvn_float_parse_dec16(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse_ro(&ctx, s, &p);
	DecFmt f = {16, 6, 9, 24, 2};
	uint32_t b[1] = {0};
	to_ieee_decimal(&ctx, &p, &f, b, 1);
	return (uint16_t)b[0];
}
static inline uint32_t bvn_float_parse_dec32(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse_ro(&ctx, s, &p);
	DecFmt f = {32, 8, 23, 101, 7};
	uint32_t b[1] = {0};
	to_ieee_decimal(&ctx, &p, &f, b, 1);
	return b[0];
}
#ifdef __DEC32_MAX__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline _Decimal32 bvn_float_parse_d32(const char *s)
{
	union { uint32_t u; _Decimal32 d; } c;
	c.u = bvn_float_parse_dec32(s); return c.d;
}
#endif
static inline uint64_t bvn_float_parse_dec64(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse_ro(&ctx, s, &p);
	DecFmt f = {64, 10, 53, 398, 16};
	uint32_t b[2] = {0,0};
	to_ieee_decimal(&ctx, &p, &f, b, 2);
	return (uint64_t)b[0] | ((uint64_t)b[1] << 32);
}
#ifdef __DEC32_MAX__
static inline _Decimal64 bvn_float_parse_d64(const char *s)
{
	union { uint64_t u; _Decimal64 d; } c;
	c.u = bvn_float_parse_dec64(s); return c.d;
}
#endif
static inline void bvn_float_parse_dec128(const char *s, uint32_t out[4])
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse_ro(&ctx, s, &p);
	DecFmt f = {128, 14, 113, 6176, 34};
	to_ieee_decimal(&ctx, &p, &f, out, 4);
}
#ifdef __DEC32_MAX__
static inline _Decimal128 bvn_float_parse_d128(const char *s)
{
	union { uint32_t u[4]; _Decimal128 d; } c;
	bvn_float_parse_dec128(s, c.u); return c.d;
}
#pragma GCC diagnostic pop
#endif
static inline void bvn_float_parse_dec256(const char *s, uint32_t out[8])
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse_ro(&ctx, s, &p);
	DecFmt f = {256, 20, 235, 611867, 70};
	to_ieee_decimal(&ctx, &p, &f, out, 8);
}
static inline int16_t bvn_float_parse_fix16(const char *s, int frac)
{
	int16_t out = 0;
	bvn_float_t *f = bvn_float_alloc(bvnf_fix_prec(16, frac, s));
	if (!f) return 0;
	if (bvn_float_from_str(f, s, 10u))
		out = bvn_float_to_fix16(f, (uint32_t)frac);
	bvn_float_free(f);
	return out;
}
static inline int32_t bvn_float_parse_fix32(const char *s, int frac)
{
	int32_t out = 0;
	bvn_float_t *f = bvn_float_alloc(bvnf_fix_prec(32, frac, s));
	if (!f) return 0;
	if (bvn_float_from_str(f, s, 10u))
		out = bvn_float_to_fix32(f, (uint32_t)frac);
	bvn_float_free(f);
	return out;
}
static inline int64_t bvn_float_parse_fix64(const char *s, int frac)
{
	int64_t out = 0;
	bvn_float_t *f = bvn_float_alloc(bvnf_fix_prec(64, frac, s));
	if (!f) return 0;
	if (bvn_float_from_str(f, s, 10u))
		out = bvn_float_to_fix64(f, (uint32_t)frac);
	bvn_float_free(f);
	return out;
}
static inline void bvn_float_parse_fix128(const char *s, int frac, uint32_t out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0u;
	bvn_float_t *f = bvn_float_alloc(bvnf_fix_prec(128, frac, s));
	if (!f) return;
	if (bvn_float_from_str(f, s, 10u))
		bvn_float_to_fix128(f, (uint32_t)frac, out);
	bvn_float_free(f);
}
static inline void bvn_float_parse_fix256(const char *s, int frac, uint32_t out[8])
{
	for (int i = 0; i < 8; i++) out[i] = 0u;
	bvn_float_t *f = bvn_float_alloc(bvnf_fix_prec(256, frac, s));
	if (!f) return;
	if (bvn_float_from_str(f, s, 10u))
		bvn_float_to_fix256(f, (uint32_t)frac, out);
	bvn_float_free(f);
}
#endif

