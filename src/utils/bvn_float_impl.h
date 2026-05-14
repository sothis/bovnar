#ifndef BVN_FLOAT_IMPL_H_
#define BVN_FLOAT_IMPL_H_
#include "bvn_int.h"
#include <stdbool.h>
#include <stdint.h>
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
		int eabs = 0; bool has_e = false;
		while (*s >= '0' && *s <= '9') {
			if (eabs < 1000000) eabs = eabs * 10 + (*s - '0');
			has_e = true; s++;
		}
		if (!has_e) return false;
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
	uint8_t mb[260];
	int mb_count = pbits + 2;
	if (mb_count > 260) mb_count = 260;
	for (int i = 0; i < mb_count; i++) {
		bvni_shl(ctx, &rem, 1);
		if (bvn_int_cmp(&rem, &scale) >= 0) {
			mb[i] = 1;
			if (!bvn_int_sub_inplace(&rem, &scale)) { ctx->overflow = true; return; }
		} else {
			mb[i] = 0;
		}
	}
	bool sticky = !bvn_int_is_zero(&rem);
	if (be <= 0) {
		int sub_shift = 1 - be;
		be = 0;
		uint8_t mb2[260]; memset(mb2, 0, sizeof(mb2));
		if (sub_shift - 1 < 260) mb2[sub_shift - 1] = 1;
		for (int i = 0; i < mb_count && i + sub_shift < 260; i++)
			mb2[i + sub_shift] = mb[i];
		bool new_sticky = sticky;
		for (int i = 0; i < mb_count; i++)
			if (i + sub_shift >= 260 && mb[i]) new_sticky = true;
		memcpy(mb, mb2, sizeof(mb));
		mb_count = 260;
		for (int i = pbits + 2; i < mb_count; i++)
			if (mb[i]) new_sticky = true;
		sticky = new_sticky;
	}
	bool guard    = (pbits     < mb_count) ? (bool)mb[pbits]     : false;
	bool rbit2    = (pbits + 1 < mb_count) ? (bool)mb[pbits + 1] : false;
	bool lsbit2   = (pbits > 0)            ? (bool)mb[pbits - 1] : false;
	bool round_up2 = guard && (rbit2 || sticky || lsbit2);
	BVN_INT_LOCAL(mant);
	for (int i = 0; i < pbits; i++)
		if (mb[i]) bvni_setbit(ctx, &mant, pbits - 1 - i);
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
static inline void to_ieee_decimal(bvn_float_ctx_t *ctx, const PNum *p, const DecFmt *f,
									uint32_t *bits, int bits32)
{
	int cbits = f->coeff_bits;
	int ebits = f->exp_bits;
	int bias  = f->bias;
	int emax  = (1 << ebits) - 1;
	int total = f->total_bits;
	for (int i = 0; i < bits32; i++) bits[i] = 0;
	bvn_float_clear_overflow(ctx);
	if (p->nan || p->inf) {
		for (int i = 0; i < ebits; i++) {
			int pos = total - 2 - i;
			if (pos >= 0)
				bits[pos / 32] |= (1u << (pos % 32));
		}
		if (p->nan) {
			if (cbits > 0) {
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
		int dig = bvni_count_decimal_digits(ctx, &C);
		while (dig > f->max_coeff_digs) {
			uint32_t rem = bvn_int_div_u32(&C, 10u);
			E++;
			if (rem >= 5u) {
				bvni_add_u32(ctx, &C, 1u);
				if (bvni_count_decimal_digits(ctx, &C) > f->max_coeff_digs) {
					bvn_int_div_u32(&C, 10u);
					E++;
				}
			}
			dig = bvni_count_decimal_digits(ctx, &C);
		}
	}
	int be = E + bias;
	if (be < 0) {
		int shift = -be;
		for (int i = 0; i < shift && !bvn_int_is_zero(&C); i++) {
			bvn_int_div_u32(&C, 10u);
			E++;
		}
		be = 0;
		if (bvn_int_is_zero(&C)) {
			if (p->neg) bits[(total-1)/32] |= (1u << ((total-1)%32));
			return;
		}
	}
	if (be >= emax) {
		for (int i = 0; i < ebits; i++) {
			int pos = total - 2 - i;
			if (pos >= 0)
				bits[pos / 32] |= (1u << (pos % 32));
		}
		if (p->neg) bits[(total-1)/32] |= (1u << ((total-1)%32));
		return;
	}
	(void)emax;
	while (bvn_int_bitlen(&C) > cbits)
		bvn_int_shr(&C, 1);
	BVN_INT_LOCAL(ebig);
	bvn_int_from_uint64(&ebig, (uint64_t)be);
	bvni_shl(ctx, &ebig, cbits);
	int rn = ((int)ebig.nused > (int)C.nused) ? (int)ebig.nused : (int)C.nused;
	for (int i = 0; i < rn && i < bits32; i++) {
		uint32_t ev = ((uint32_t)i < ebig.nused) ? ebig.limbs[i] : 0u;
		uint32_t cv = ((uint32_t)i < C.nused)    ? C.limbs[i]    : 0u;
		bits[i] = ev | cv;
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
static inline uint16_t bvn_float_parse_bin16(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	BinFmt f = {5, 10, 15};
	uint32_t b[1] = {0};
	to_ieee_binary(&ctx, &p, &f, b, 1);
	return (uint16_t)b[0];
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline _Float16 bvn_float_parse_f16(const char *s)
{
	union { uint16_t u; _Float16 f; } c;
	c.u = bvn_float_parse_bin16(s); return c.f;
}
#pragma GCC diagnostic pop
static inline uint32_t bvn_float_parse_bin32(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	BinFmt f = {8, 23, 127};
	uint32_t b[1] = {0};
	to_ieee_binary(&ctx, &p, &f, b, 1);
	return b[0];
}
static inline float bvn_float_parse_f32(const char *s)
{
	union { uint32_t u; float f; } c;
	c.u = bvn_float_parse_bin32(s); return c.f;
}
static inline uint64_t bvn_float_parse_bin64(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	BinFmt f = {11, 52, 1023};
	uint32_t b[2] = {0,0};
	to_ieee_binary(&ctx, &p, &f, b, 2);
	return (uint64_t)b[0] | ((uint64_t)b[1] << 32);
}
static inline double bvn_float_parse_f64(const char *s)
{
	union { uint64_t u; double f; } c;
	c.u = bvn_float_parse_bin64(s); return c.f;
}
static inline void bvn_float_parse_bin128(const char *s, uint32_t out[4])
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	BinFmt f = {15, 112, 16383};
	to_ieee_binary(&ctx, &p, &f, out, 4);
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
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	BinFmt f = {19, 236, 262143};
	to_ieee_binary(&ctx, &p, &f, out, 8);
}
static inline uint16_t bvn_float_parse_dec16(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	DecFmt f = {16, 6, 9, 101, 2};
	uint32_t b[1] = {0};
	to_ieee_decimal(&ctx, &p, &f, b, 1);
	return (uint16_t)b[0];
}
static inline uint32_t bvn_float_parse_dec32(const char *s)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
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
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
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
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
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
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	DecFmt f = {256, 20, 235, 611867, 70};
	to_ieee_decimal(&ctx, &p, &f, out, 8);
}
static inline int16_t bvn_float_parse_fix16(const char *s, int frac)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	uint32_t b[1] = {0};
	to_fixed_point(&ctx, &p, 16, frac, b, 1);
	return (int16_t)b[0];
}
static inline int32_t bvn_float_parse_fix32(const char *s, int frac)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	uint32_t b[1] = {0};
	to_fixed_point(&ctx, &p, 32, frac, b, 1);
	return (int32_t)b[0];
}
static inline int64_t bvn_float_parse_fix64(const char *s, int frac)
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	uint32_t b[2] = {0,0};
	to_fixed_point(&ctx, &p, 64, frac, b, 2);
	return (int64_t)((uint64_t)b[0] | ((uint64_t)b[1] << 32));
}
static inline void bvn_float_parse_fix128(const char *s, int frac, uint32_t out[4])
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	to_fixed_point(&ctx, &p, 128, frac, out, 4);
}
static inline void bvn_float_parse_fix256(const char *s, int frac, uint32_t out[8])
{
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p); bvn_float_parse(&ctx, s, &p);
	to_fixed_point(&ctx, &p, 256, frac, out, 8);
}
#endif
