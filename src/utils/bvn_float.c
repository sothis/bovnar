#include <bvn_float.h>
#include <bvn_int.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <limits.h>
typedef struct {
	uint32_t *limbs;
	uint32_t  nlimbs;
	uint32_t  nused;
} bvnf_bn;
static bool bvnf_bn_grow(bvnf_bn *b, uint32_t need)
{
	if (need <= b->nlimbs) return true;
	uint32_t *p = realloc(b->limbs, (size_t)need * sizeof(uint32_t));
	if (!p) return false;
	memset(p + b->nlimbs, 0, (size_t)(need - b->nlimbs) * sizeof(uint32_t));
	b->limbs  = p;
	b->nlimbs = need;
	return true;
}
static bool bvnf_bn_init(bvnf_bn *b, uint32_t cap)
{
	if (!cap) cap = 8;
	b->limbs = calloc(cap, sizeof(uint32_t));
	if (!b->limbs) return false;
	b->nlimbs = cap;
	b->nused  = 0;
	return true;
}
static void bvnf_bn_free(bvnf_bn *b) { free(b->limbs); b->limbs = NULL; }
static void bvnf_bn_norm(bvnf_bn *b)
{
	uint32_t n       = b->nused <= b->nlimbs ? b->nused : b->nlimbs;
	uint32_t *lo     = b->limbs;
	uint32_t *hi     = lo + n;
	while (hi > lo && hi[-1] == 0) hi--;
	b->nused = (uint32_t)(hi - lo);
}
static bool bvnf_bn_is_zero(const bvnf_bn *b) { return b->nused == 0; }
static bool bvnf_bn_from_float_mant(bvnf_bn *b, const bvn_float_t *f)
{
	uint32_t nl = f->_nlimbs;
#if BVN_LIMB_BITS == 64
	if (nl > UINT32_MAX / 2u) return false;
	uint32_t w32 = nl * 2u;
	if (!bvnf_bn_grow(b, w32)) return false;
	for (uint32_t i = 0; i < nl; i++) {
		b->limbs[i * 2]     = (uint32_t)(f->_d[i] & 0xffffffffu);
		b->limbs[i * 2 + 1] = (uint32_t)(f->_d[i] >> 32);
	}
	b->nused = w32;
#else
	if (!bvnf_bn_grow(b, nl)) return false;
	for (uint32_t i = 0; i < nl; i++) b->limbs[i] = f->_d[i];
	b->nused = nl;
#endif
	bvnf_bn_norm(b);
	return true;
}
static bool bvnf_bn_mul_u32(bvnf_bn *b, uint32_t v)
{
	if (!v)         { b->nused = 0; return true; }
	if (v == 1)     return true;
	if (b->nused == 0) return true;
	if (!bvnf_bn_grow(b, b->nused + 1)) return false;
	b->limbs[b->nused] = 0;
	uint64_t carry = 0;
	for (uint32_t i = 0; i < b->nused; i++) {
		uint64_t r = (uint64_t)b->limbs[i] * v + carry;
		b->limbs[i] = (uint32_t)(r & 0xffffffffu);
		carry = r >> 32;
	}
	if (carry) { b->limbs[b->nused] = (uint32_t)carry; b->nused++; }
	return true;
}
static void bvnf_bn_shr(bvnf_bn *b, uint32_t bits)
{
	if (bvnf_bn_is_zero(b) || !bits) return;
	uint32_t wshift = bits / 32u, bshift = bits % 32u;
	if (wshift >= b->nused) { b->nused = 0; return; }
	if (bshift == 0) {
		memmove(b->limbs, b->limbs + wshift,
				(size_t)(b->nused - wshift) * sizeof(uint32_t));
		b->nused -= wshift;
	} else {
		uint32_t rbshift = 32u - bshift;
		for (uint32_t i = 0; i + wshift < b->nused; i++) {
			b->limbs[i] = b->limbs[i + wshift] >> bshift;
			if (i + wshift + 1 < b->nused)
				b->limbs[i] |= b->limbs[i + wshift + 1] << rbshift;
		}
		b->nused -= wshift;
	}
	bvnf_bn_norm(b);
}
static bool bvnf_bn_shl(bvnf_bn *b, uint32_t bits)
{
	if (bvnf_bn_is_zero(b) || !bits) return true;
	uint32_t wshift = bits / 32u, bshift = bits % 32u;
	uint32_t need = b->nused + wshift + (bshift ? 1u : 0u);
	if (!bvnf_bn_grow(b, need)) return false;
	for (uint32_t i = b->nused; i-- > 0;) {
		uint32_t v = b->limbs[i];
		b->limbs[i + wshift]     = bshift ? (v << bshift) : v;
		if (bshift && i + wshift + 1 < need)
			b->limbs[i + wshift + 1] |= v >> (32u - bshift);
		if (wshift) b->limbs[i] = 0;
	}
	b->nused = need;
	bvnf_bn_norm(b);
	return true;
}
static bool bvnf_bn_mul_pow5(bvnf_bn *b, uint32_t k)
{
	static const uint32_t p5_13 = 1220703125u;
	while (k >= 13) {
		if (!bvnf_bn_mul_u32(b, p5_13)) return false;
		k -= 13;
	}
	uint32_t p = 1;
	for (uint32_t i = 0; i < k; i++) p *= 5;
	return bvnf_bn_mul_u32(b, p);
}
static uint32_t bvnf_bn_div_u32(bvnf_bn *b, uint32_t d)
{
	uint64_t rem = 0;
	for (uint32_t i = b->nused; i-- > 0;) {
		uint64_t cur = (rem << 32) | b->limbs[i];
		b->limbs[i] = (uint32_t)(cur / d);
		rem         = cur % d;
	}
	bvnf_bn_norm(b);
	return (uint32_t)rem;
}
bvn_float_t *bvn_float_alloc(uint32_t prec)
{
	if (!prec || prec > BVN_FLOAT_MAX_PREC) return NULL;
	bvn_float_t *f = malloc(sizeof *f);
	if (!f) return NULL;
	uint32_t nl = BVN_FLOAT_NLIMBS(prec);
	bvn_limb_t *d = calloc(nl, sizeof(bvn_limb_t));
	if (!d) { free(f); return NULL; }
	f->_prec   = prec;
	f->_sign   = 1;
	f->_exp    = BVN_FLOAT_EXP_ZERO;
	f->_d      = d;
	f->_nlimbs = nl;
	f->_heap   = true;
	return f;
}
void bvn_float_free(bvn_float_t *f)
{
	if (!f) return;
	if (f->_heap) free(f->_d);
	free(f);
}
void bvn_float_init_buf(bvn_float_t *f, uint32_t prec,
						bvn_limb_t *buf, uint32_t nlimbs)
{
	f->_prec   = prec;
	f->_sign   = 1;
	f->_exp    = BVN_FLOAT_EXP_ZERO;
	f->_d      = buf;
	f->_nlimbs = nlimbs;
	f->_heap   = false;
	memset(buf, 0, (size_t)nlimbs * sizeof(bvn_limb_t));
}
void bvn_float_set_nan(bvn_float_t *f)
{
	f->_exp  = BVN_FLOAT_EXP_NAN;
	f->_sign = 1;
}
void bvn_float_set_inf(bvn_float_t *f, bool neg)
{
	f->_exp  = BVN_FLOAT_EXP_INF;
	f->_sign = neg ? -1 : 1;
}
void bvn_float_set_zero(bvn_float_t *f, bool neg)
{
	f->_exp  = BVN_FLOAT_EXP_ZERO;
	f->_sign = neg ? -1 : 1;
	memset(f->_d, 0, (size_t)f->_nlimbs * sizeof(bvn_limb_t));
}
bool bvn_float_copy(bvn_float_t *dst, const bvn_float_t *src)
{
	dst->_sign = src->_sign;
	dst->_exp  = src->_exp;
	if (!bvn_float_is_regular(src)) {
		memset(dst->_d, 0, (size_t)dst->_nlimbs * sizeof(bvn_limb_t));
		return true;
	}
	uint32_t dnl = dst->_nlimbs, snl = src->_nlimbs;
	if (dnl >= snl) {
		uint32_t pad = dnl - snl;
		memset(dst->_d, 0, (size_t)pad * sizeof(bvn_limb_t));
		memcpy(dst->_d + pad, src->_d, (size_t)snl * sizeof(bvn_limb_t));
	} else {
		uint32_t skip = snl - dnl;
		memcpy(dst->_d, src->_d + skip, (size_t)dnl * sizeof(bvn_limb_t));
	}
	return true;
}
static void bvnf_store_mant(bvn_float_t *f, const bvn_int_t *Q)
{
	uint32_t nl = f->_nlimbs;
	memset(f->_d, 0, (size_t)nl * sizeof(bvn_limb_t));
#if BVN_LIMB_BITS == 64
	for (uint32_t i = 0; i < nl; i++) {
		uint32_t lo_idx = i * 2;
		uint32_t hi_idx = i * 2 + 1;
		uint64_t lo = (lo_idx < Q->nused) ? Q->limbs[lo_idx] : 0u;
		uint64_t hi = (hi_idx < Q->nused) ? Q->limbs[hi_idx] : 0u;
		f->_d[i] = lo | (hi << 32);
	}
#else
	for (uint32_t i = 0; i < nl; i++)
		f->_d[i] = (i < Q->nused) ? Q->limbs[i] : 0u;
#endif
}
static bool bvnf_bitdiv(const bvn_int_t *num, int shift,
						 const bvn_int_t *den,
						 uint32_t *Q, uint32_t q_words,
						 int want, bool *sticky_out)
{
	int db = bvn_int_bitlen(den);
	int nb = bvn_int_bitlen(num);
	int total = nb + (shift > 0 ? shift : 0);
	if (total <= 0) { *sticky_out = false; return true; }
	uint32_t r_cap = (uint32_t)(db / 32) + 4u;
	uint32_t *r_limbs = calloc(r_cap, sizeof(uint32_t));
	if (!r_limbs) return false;
	bvn_int_t R = { r_limbs, r_cap, 0, false, true };
	memset(Q, 0, (size_t)q_words * sizeof(uint32_t));
	bool sticky = false;
	for (int i = total - 1; i >= 0 && !sticky; i--) {
		bvn_int_shl(&R, 1);
		int src = i - shift;
		if (src >= 0 && bvn_int_getbit(num, src))
			bvn_int_add_u32(&R, 1u);
		if (bvn_int_cmp(&R, den) >= 0) {
			bvn_int_sub_inplace(&R, den);
			if (i < want) {
				uint32_t wi = (uint32_t)i / 32u;
				if (wi < q_words) Q[wi] |= 1u << ((uint32_t)i % 32u);
			} else {
				sticky = true;
			}
		}
	}
	if (!sticky && !bvn_int_is_zero(&R)) sticky = true;
	free(r_limbs);
	*sticky_out = sticky;
	return true;
}
static bool bvnf_rational_to_float(bvn_float_t *f, bool neg,
									bvn_int_t *num, bvn_int_t *den)
{
	if (bvn_int_is_zero(num)) { bvn_float_set_zero(f, neg); return true; }
	int prec = (int)f->_prec;
	int nb   = bvn_int_bitlen(num);
	int db   = bvn_int_bitlen(den);
	int E = nb - db;
	{
		uint32_t _tb[BVN_INT_MAX_BITS / 32];
		bvn_int_t tmp = { _tb, BVN_INT_MAX_BITS/32, 0, false, false };
		memset(_tb, 0, sizeof _tb);
		if (E >= 0) {
			if (bvn_int_copy(&tmp, den) && bvn_int_shl(&tmp, E))
				if (bvn_int_cmp(&tmp, num) > 0) E--;
		} else {
			if (bvn_int_copy(&tmp, num) && bvn_int_shl(&tmp, -E))
				if (bvn_int_cmp(den, &tmp) > 0) E--;
		}
	}
	f->_sign = neg ? -1 : 1;
	f->_exp  = (int64_t)E + 1;
	int shift = prec + 1 - E;
	int want  = prec + 2;
	uint32_t q_words = (uint32_t)(want + 31) / 32u + 4u;
	uint32_t *_Qb = calloc(q_words, sizeof(uint32_t));
	if (!_Qb) return false;
#define RTF_RET(v) do { free(_Qb); return (v); } while (0)
	bool sticky = false, ok = true;
	if (shift >= 0 && nb + shift > (int)BVN_INT_MAX_BITS - 32) {
		ok = bvnf_bitdiv(num, shift, den, _Qb, q_words, want, &sticky);
	} else if (shift >= 0) {
		uint32_t _sb[BVN_INT_MAX_BITS / 32];
		bvn_int_t scaled = { _sb, BVN_INT_MAX_BITS/32, 0, false, false };
		memset(_sb, 0, sizeof _sb);
		uint32_t _Rb[BVN_INT_MAX_BITS / 32 + 4];
		bvn_int_t Q_int = { _Qb, q_words, 0, false, false };
		bvn_int_t R_int = { _Rb, BVN_INT_MAX_BITS/32 + 4, 0, false, false };
		memset(_Rb, 0, sizeof _Rb);
		ok = bvn_int_copy(&scaled, num) && bvn_int_shl(&scaled, shift)
			 && bvn_int_divrem(&Q_int, &R_int, &scaled, den);
		if (ok) sticky = !bvn_int_is_zero(&R_int);
	} else {
		uint32_t _ds[BVN_INT_MAX_BITS / 32];
		bvn_int_t dscaled = { _ds, BVN_INT_MAX_BITS/32, 0, false, false };
		memset(_ds, 0, sizeof _ds);
		uint32_t _Rb[BVN_INT_MAX_BITS / 32 + 4];
		bvn_int_t Q_int = { _Qb, q_words, 0, false, false };
		bvn_int_t R_int = { _Rb, BVN_INT_MAX_BITS/32 + 4, 0, false, false };
		memset(_Rb, 0, sizeof _Rb);
		ok = bvn_int_copy(&dscaled, den) && bvn_int_shl(&dscaled, -shift)
			 && bvn_int_divrem(&Q_int, &R_int, num, &dscaled);
		if (ok) sticky = !bvn_int_is_zero(&R_int);
	}
	if (!ok) RTF_RET(false);
	bvn_int_t Q_view = { _Qb, q_words, q_words, false, false };
	while (Q_view.nused > 0 && _Qb[Q_view.nused - 1] == 0) Q_view.nused--;
	bool rbit     = (bool)bvn_int_getbit(&Q_view, 0);
	bool gbit     = (bool)bvn_int_getbit(&Q_view, 1);
	bool lsbit    = (prec > 0) ? (bool)bvn_int_getbit(&Q_view, 2) : false;
	bool round_up = gbit && (rbit || sticky || lsbit);
	bvn_int_shr(&Q_view, 2);
	if (round_up) {
		if (!bvn_int_add_u32(&Q_view, 1u)) RTF_RET(false);
		if (bvn_int_bitlen(&Q_view) > prec) {
			bvn_int_shr(&Q_view, 1);
			f->_exp++;
		}
	}
	bvnf_store_mant(f, &Q_view);
#undef RTF_RET
	free(_Qb);
	return true;
}
static uint32_t bvnf_digit(uint8_t c, uint32_t base)
{
	uint32_t d;
	if      (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
	else if (c >= 'a' && c <= 'z') d = 10u + (uint32_t)(c - 'a');
	else if (c >= 'A' && c <= 'Z') d = 10u + (uint32_t)(c - 'A');
	else return base;
	return (d < base) ? d : base;
}
static bool bvnf_pow2_base(uint32_t base, uint32_t *log2base)
{
	if (!base || (base & (base - 1u))) return false;
	uint32_t k = 0;
	while ((1u << k) < base) k++;
	*log2base = k;
	return true;
}
bool bvn_float_from_str(bvn_float_t *f, const char *s, uint32_t base)
{
	if (!f || !s || base < 2 || base > 36) return false;
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
	bool neg = false;
	if      (*s == '+') s++;
	else if (*s == '-') { neg = true; s++; }
	{
		const char *p = s;
		uint8_t c0 = (uint8_t)(*p | 0x20);
		if (c0 == 'n' && ((uint8_t)(p[1]|0x20)) == 'a' && ((uint8_t)(p[2]|0x20)) == 'n')
			{ bvn_float_set_nan(f); return true; }
		if (c0 == 'i' && ((uint8_t)(p[1]|0x20)) == 'n' && ((uint8_t)(p[2]|0x20)) == 'f')
			{ bvn_float_set_inf(f, neg); return true; }
	}
	uint32_t _nb[BVN_INT_MAX_BITS / 32];
	uint32_t _db[BVN_INT_MAX_BITS / 32];
	uint32_t _tb[BVN_INT_MAX_BITS / 32];
	bvn_int_t num, den, tmp;
	num = (bvn_int_t){ _nb, BVN_INT_MAX_BITS/32, 0, false, false };
	den = (bvn_int_t){ _db, BVN_INT_MAX_BITS/32, 0, false, false };
	tmp = (bvn_int_t){ _tb, BVN_INT_MAX_BITS/32, 0, false, false };
	memset(_nb, 0, sizeof _nb);
	memset(_db, 0, sizeof _db);
	memset(_tb, 0, sizeof _tb);
	bvn_int_from_uint64(&den, 1u);
	uint32_t acc = 0, acc_d = 0;
	bool overflow = false;
	int frac_digits = 0;
	bool has_int  = false;
	bool has_frac = false;
#define FLUSH_CHUNK() \
	do { \
		if (acc_d > 0 && !overflow) { \
			  \
			uint32_t _mul = 1; \
			for (uint32_t _k = 0; _k < acc_d; _k++) _mul *= base; \
			if (!bvn_int_mul_u32(&num, _mul)) { overflow = true; break; } \
			if (!bvn_int_add_u32(&num, acc))  { overflow = true; break; } \
			acc = 0; acc_d = 0; \
		} \
	} while (0)
	while (!overflow) {
		uint32_t d = bvnf_digit((uint8_t)*s, base);
		if (d >= base) break;
		has_int = true;
		acc = acc * base + d; acc_d++;
		uint32_t cap = (base <= 10) ? 9u :
					   (base <= 16) ? 7u :
					   (base <= 32) ? 6u : 5u;
		if (acc_d >= cap) FLUSH_CHUNK();
		s++;
	}
	FLUSH_CHUNK();
	if (*s == '.') {
		s++;
		while (!overflow) {
			uint32_t d = bvnf_digit((uint8_t)*s, base);
			if (d >= base) break;
			has_frac = true;
			acc = acc * base + d; acc_d++;
			frac_digits++;
			uint32_t cap = (base <= 10) ? 9u :
						   (base <= 16) ? 7u :
						   (base <= 32) ? 6u : 5u;
			if (acc_d >= cap) FLUSH_CHUNK();
			s++;
		}
		FLUSH_CHUNK();
	}
	if (!has_int && !has_frac) return false;
	long exp_val = 0;
	bool use_bin_exp = false;
	bool eneg = false;
	bool exp_overflow = false;
	if (*s == 'e' || *s == 'E' || *s == 'p' || *s == 'P') {
		use_bin_exp = (*s == 'p' || *s == 'P');
		s++;
		if      (*s == '+') s++;
		else if (*s == '-') { eneg = true; s++; }
		long eabs = 0; bool has_e = false;
		while (*s >= '0' && *s <= '9') {
			if (!exp_overflow) {
				long d = (long)(*s - '0');
				if (eabs > (LONG_MAX - d) / 10L) exp_overflow = true;
				else eabs = eabs * 10L + d;
			}
			has_e = true; s++;
		}
		if (!has_e) return false;
		if (exp_overflow) {
			if (eneg) { bvn_float_set_zero(f, neg); return true; }
			else      { bvn_float_set_inf (f, neg); return true; }
		}
		exp_val = eneg ? -eabs : eabs;
	}
	if (overflow) {
		if (exp_val < 0) { bvn_float_set_zero(f, neg); return true; }
		bvn_float_set_inf(f, neg);
		return true;
	}
	if (base == 10) use_bin_exp = false;
	uint32_t log2b = 0;
	bool b_is_pow2 = bvnf_pow2_base(base, &log2b);
	if (bvn_int_is_zero(&num)) { bvn_float_set_zero(f, neg); return true; }
	if (use_bin_exp || b_is_pow2) {
		long net_shift;
		if (b_is_pow2) {
			net_shift = exp_val - (long)frac_digits * (long)log2b;
		} else {
			net_shift = exp_val;
			goto rational_path;
		}
		if (net_shift >= 0) {
			if (!bvn_int_shl(&num, (int)net_shift)) {
				bvn_float_set_inf(f, neg); return true;
			}
		} else {
			bvn_int_from_uint64(&den, 1u);
			if (!bvn_int_shl(&den, (int)(-net_shift))) {
				bvn_float_set_zero(f, neg); return true;
			}
		}
	} else {
rational_path:;
		long net_b_exp = exp_val - (long)frac_digits;
		if (!use_bin_exp) {
			if (net_b_exp >= 0) {
				for (long i = 0; i < net_b_exp; i++) {
					if (!bvn_int_mul_u32(&num, base)) {
						bvn_float_set_inf(f, neg); return true;
					}
				}
			} else {
				for (long i = 0; i < -net_b_exp; i++) {
					if (!bvn_int_mul_u32(&den, base)) {
						bvn_float_set_zero(f, neg); return true;
					}
				}
			}
		} else {
			for (int i = 0; i < frac_digits; i++) {
				if (!bvn_int_mul_u32(&den, base)) {
					bvn_float_set_zero(f, neg); return true;
				}
			}
			if (exp_val >= 0) {
				if (!bvn_int_shl(&num, (int)exp_val)) {
					bvn_float_set_inf(f, neg); return true;
				}
			} else {
				if (!bvn_int_shl(&den, (int)(-exp_val))) {
					bvn_float_set_zero(f, neg); return true;
				}
			}
		}
	}
	if (bvn_int_is_zero(&num)) { bvn_float_set_zero(f, neg); return true; }
	(void)tmp;
	return bvnf_rational_to_float(f, neg, &num, &den);
#undef FLUSH_CHUNK
}
size_t bvn_float_str_bufsize(uint32_t prec, uint32_t base)
{
	size_t digits;
	if (base == 10u) {
		digits = (size_t)prec * 302u / 1000u + 24u;
	} else {
		uint32_t log2b_floor = 1u;
		uint32_t b = base;
		while (b >= 2u) { log2b_floor++; b >>= 1u; }
		log2b_floor--;
		digits = (size_t)prec / log2b_floor + 24u;
	}
	return digits + 48u;
}
static int32_t bvnf_to_str_pow2(const bvn_float_t *f, char *buf,
								  size_t bufsize, uint32_t log2b)
{
	static const char DIGS[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	long prec = (long)f->_prec;
	long frac_bits = prec - 1L;
	long nfrac = (frac_bits + (long)log2b - 1L) / (long)log2b;
	char *fdigs = calloc((size_t)(nfrac + 1), 1u);
	if (!fdigs) return -1;
	for (long d = 0; d < nfrac; d++) {
		long bit_pos = prec - 2L - d * (long)log2b;
		uint32_t digit = 0;
		for (uint32_t b = 0; b < log2b; b++) {
			digit <<= 1;
			long bp = bit_pos - (long)b;
			if (bp >= 0) {
#if BVN_LIMB_BITS == 64
				uint32_t li = (uint32_t)bp / 64u;
				uint32_t lb = (uint32_t)bp % 64u;
				digit |= (li < f->_nlimbs)
				       ? (uint32_t)((f->_d[li] >> lb) & 1u) : 0u;
#else
				uint32_t li = (uint32_t)bp / 32u;
				uint32_t lb = (uint32_t)bp % 32u;
				digit |= (li < f->_nlimbs)
				       ? ((f->_d[li] >> lb) & 1u) : 0u;
#endif
			}
		}
		fdigs[d] = DIGS[digit < 37u ? digit : 0u];
	}
	long last_nz = nfrac - 1L;
	while (last_nz >= 0 && fdigs[last_nz] == '0') last_nz--;
	size_t pos = 0;
	int32_t ret = -1;
#define PUTC(c) do { if (pos + 1 >= bufsize) goto p2_done; buf[pos++] = (char)(c); } while (0)
	if (f->_sign < 0) PUTC('-');
	PUTC('1');
	if (last_nz >= 0) {
		PUTC('.');
		for (long d = 0; d <= last_nz; d++) PUTC(fdigs[d]);
	}
	{
		long bin_exp = f->_exp - 1L;
		PUTC('p');
		if (bin_exp >= 0) {
			PUTC('+');
		} else {
			PUTC('-');
			bin_exp = -bin_exp;
		}
		char ebuf[32]; int en = 0;
		if (bin_exp == 0) {
			ebuf[en++] = '0';
		} else {
			long ev = bin_exp;
			while (ev > 0) { ebuf[en++] = (char)('0' + ev % 10); ev /= 10; }
			for (int i = 0, j = en - 1; i < j; i++, j--) {
				char t = ebuf[i]; ebuf[i] = ebuf[j]; ebuf[j] = t;
			}
		}
		for (int i = 0; i < en; i++) PUTC(ebuf[i]);
	}
	buf[pos] = '\0';
	ret = (int32_t)pos;
p2_done:
#undef PUTC
	free(fdigs);
	return ret;
}
static int32_t bvnf_to_str_arb(const bvn_float_t *f, char *buf,
								 size_t bufsize, uint32_t base)
{
	static const char DIGS[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	size_t pos = 0;
#define PUTC_ARB(c) do { \
	if (pos + 1 >= bufsize) goto arb_overflow; \
	buf[pos++] = (char)(c); \
} while (0)
	long prec = f->_prec;
	long exp2 = f->_exp;
	long e2   = exp2 - prec;
	double log2b = log2((double)base);
	long ep = exp2 - 1L;
	long E_b;
	if (ep >= 0) E_b =  (long)ceil( (double)ep / log2b);
	else         E_b = -(long)floor((double)(-ep) / log2b);
	long ndigits = (long)ceil((double)prec / log2b) + 4L;
	long k = ndigits - 1L - E_b;
	if (f->_nlimbs > (UINT32_MAX - 8u) / 2u) return -1;
	bvnf_bn M = {0};
	if (!bvnf_bn_init(&M, f->_nlimbs * 2u + 8u)) return -1;
	if (!bvnf_bn_from_float_mant(&M, f)) { bvnf_bn_free(&M); return -1; }
	if (k >= 0 && e2 >= 0) {
		bvnf_bn_shl(&M, (uint32_t)e2);
		for (long i = 0; i < k; i++) bvnf_bn_mul_u32(&M, base);
	} else if (k >= 0 && e2 < 0) {
		for (long i = 0; i < k; i++) bvnf_bn_mul_u32(&M, base);
		bvnf_bn_shr(&M, (uint32_t)(-e2));
	} else if (k < 0 && e2 >= 0) {
		bvnf_bn_shl(&M, (uint32_t)e2);
		for (long i = 0; i < -k; i++) bvnf_bn_div_u32(&M, base);
	} else {
		bvnf_bn_shr(&M, (uint32_t)(-e2));
		for (long i = 0; i < -k; i++) bvnf_bn_div_u32(&M, base);
	}
	if (ndigits <= 0) { bvnf_bn_free(&M); return -1; }
	char *digs = calloc((size_t)(ndigits + 4), 1u);
	if (!digs) { bvnf_bn_free(&M); return -1; }
	for (long i = 0; i < ndigits; i++) {
		uint32_t rem = bvnf_bn_div_u32(&M, base);
		digs[i] = DIGS[rem < base ? rem : 0u];
	}
	bvnf_bn_free(&M);
	for (long i = 0, j = ndigits - 1; i < j; i++, j--) {
		char t = digs[i]; digs[i] = digs[j]; digs[j] = t;
	}
	if (ndigits > 1 && digs[0] == '0') {
		memmove(digs, digs + 1, (size_t)(ndigits - 1));
		digs[ndigits - 1] = '0';
		E_b--;
	}
	long last = ndigits - 1;
	while (last > 0 && digs[last] == '0') last--;
	ndigits = last + 1;
	int32_t ret = -1;
	if (f->_sign < 0) PUTC_ARB('-');
	PUTC_ARB(digs[0]);
	if (ndigits > 1) {
		PUTC_ARB('.');
		for (long i = 1; i < ndigits; i++) PUTC_ARB(digs[i]);
	}
	PUTC_ARB('e');
	if (E_b < 0) { PUTC_ARB('-'); E_b = -E_b; }
	else           PUTC_ARB('+');
	{
		char ebuf[32]; int en = 0;
		if (E_b == 0) { ebuf[en++] = '0'; }
		else { long ev = E_b; while (ev > 0) { ebuf[en++] = (char)('0' + ev%10); ev /= 10; } }
		for (int i = 0, j = en-1; i < j; i++, j--) { char t=ebuf[i]; ebuf[i]=ebuf[j]; ebuf[j]=t; }
		for (int i = 0; i < en; i++) PUTC_ARB(ebuf[i]);
	}
	buf[pos] = '\0';
	ret = (int32_t)pos;
arb_overflow:
#undef PUTC_ARB
	free(digs);
	return ret;
}
static int32_t bvnf_to_str_dec(const bvn_float_t *f, char *buf, size_t bufsize)
{
	long prec = f->_prec;
	long exp2 = f->_exp;
	long e2   = exp2 - prec;
	long E10;
	{
		long ep = exp2 - 1L;
		if (ep >= 0)  E10 = (ep * 302L + 999L) / 1000L;
		else          E10 = -( (-ep * 301L) / 1000L );
	}
	long ndigits = (prec * 302L + 999L) / 1000L + 4L;
	long k = ndigits - 1L - E10;
	if (f->_nlimbs > (UINT32_MAX - 4u) / 2u) return -1;
	bvnf_bn M = {0}; if (!bvnf_bn_init(&M, f->_nlimbs * 2u)) return -1;
	if (!bvnf_bn_from_float_mant(&M, f)) { bvnf_bn_free(&M); return -1; }
	long net2 = e2 + k;
	if (k >= 0) {
		bvnf_bn_mul_pow5(&M, (uint32_t)k);
		if      (net2 > 0) bvnf_bn_shl(&M, (uint32_t)net2);
		else if (net2 < 0) bvnf_bn_shr(&M, (uint32_t)(-net2));
	} else {
		long neg_k = -k;
		if      (net2 > 0) bvnf_bn_shl(&M, (uint32_t)net2);
		else if (net2 < 0) bvnf_bn_shr(&M, (uint32_t)(-net2));
		for (long i = 0; i < neg_k; i++)
			bvnf_bn_div_u32(&M, 5u);
	}
	if (ndigits <= 0) { bvnf_bn_free(&M); return -1; }
	char *digs = calloc((size_t)(ndigits + 4), 1u);
	if (!digs) { bvnf_bn_free(&M); return -1; }
	for (long i = 0; i < ndigits; i++) {
		uint32_t rem = bvnf_bn_div_u32(&M, 10u);
		digs[i] = (char)('0' + rem);
	}
	bvnf_bn_free(&M);
	for (long i = 0, j = ndigits - 1; i < j; i++, j--) {
		char t = digs[i]; digs[i] = digs[j]; digs[j] = t;
	}
	if (ndigits > 1 && digs[0] == '0') {
		memmove(digs, digs + 1, (size_t)(ndigits - 1));
		digs[ndigits - 1] = '0';
		E10--;
	}
	long last = ndigits - 1;
	while (last > 0 && digs[last] == '0') last--;
	ndigits = last + 1;
	size_t pos = 0;
	int32_t ret = -1;
#define PUTC(c) do { if (pos + 1 >= bufsize) goto overflow; \
					 buf[pos++] = (char)(c); } while(0)
	if (f->_sign < 0) PUTC('-');
	PUTC(digs[0]);
	if (ndigits > 1) {
		PUTC('.');
		for (long i = 1; i < ndigits; i++) PUTC(digs[i]);
	}
	PUTC('e');
	if (E10 >= 0) PUTC('+'); else { PUTC('-'); E10 = -E10; }
	{ char ebuf[32]; int en = 0;
	  if (E10 == 0) { ebuf[en++] = '0'; }
	  else {
		  long ev = E10;
		  while (ev > 0) { ebuf[en++] = (char)('0' + ev % 10); ev /= 10; }
		  for (int i = 0, j = en - 1; i < j; i++, j--) {
			  char t = ebuf[i]; ebuf[i] = ebuf[j]; ebuf[j] = t;
		  }
	  }
	  for (int i = 0; i < en; i++) PUTC(ebuf[i]);
	}
	buf[pos] = '\0';
	ret = (int32_t)pos;
overflow:
#undef PUTC
	free(digs);
	return ret;
}
int32_t bvn_float_to_str(const bvn_float_t *f, char *buf, size_t bufsize,
						  uint32_t base)
{
	if (!f || !buf || bufsize < 2 || base < 2 || base > 36) return -1;
	if (bvn_float_is_nan(f)) {
		if (bufsize < 4) return -1;
		memcpy(buf, "nan", 4);
		return 3;
	}
	if (bvn_float_is_inf(f)) {
		bool neg = f->_sign < 0;
		if (neg) { if (bufsize < 10) return -1; memcpy(buf, "-infinity", 10); return 9; }
		else     { if (bufsize < 9)  return -1; memcpy(buf, "infinity", 9);   return 8; }
	}
	if (bvn_float_is_zero(f)) {
		if (bufsize < 4) return -1;
		if (f->_sign < 0) { if (bufsize < 5) return -1; memcpy(buf, "-0.0", 5); return 4; }
		memcpy(buf, "0.0", 4);
		return 3;
	}
	uint32_t log2b = 0;
	if (bvnf_pow2_base(base, &log2b))
		return bvnf_to_str_pow2(f, buf, bufsize, log2b);
	if (base == 10u)
		return bvnf_to_str_dec(f, buf, bufsize);
	return bvnf_to_str_arb(f, buf, bufsize, base);
}
bool bvn_float_from_double(bvn_float_t *f, double v)
{
	if (!f) return false;
	if (isnan(v))  { bvn_float_set_nan(f);  return true; }
	if (isinf(v))  { bvn_float_set_inf(f, v < 0.0); return true; }
	if (v == 0.0) {
		bvn_float_set_zero(f, (1.0 / v) < 0.0);
		return true;
	}
	uint64_t bits;
	memcpy(&bits, &v, 8);
	bool neg = (bits >> 63) != 0;
	int  exp11 = (int)((bits >> 52) & 0x7ffu);
	uint64_t man = bits & ((1ull << 52) - 1ull);
	long exp2;
	uint64_t full_man;
	if (exp11 == 0) {
		full_man = man;
		exp2     = -1022L + 1L;
	} else {
		full_man = man | (1ull << 52);
		exp2     = (long)exp11 - 1023L + 1L;
	}
	int lz = 0;
	uint64_t tmp = full_man;
	while (tmp < (1ull << 52)) { tmp <<= 1; lz++; }
	full_man <<= lz;
	exp2 -= lz;
	f->_sign = neg ? -1 : 1;
	f->_exp  = exp2;
	memset(f->_d, 0, (size_t)f->_nlimbs * sizeof(bvn_limb_t));
	long prec = f->_prec;
	int shift = (int)prec - 53;
#if BVN_LIMB_BITS == 64
	if (shift >= 0) {
		uint32_t lo_limb = (uint32_t)shift / 64u;
		uint32_t lo_off  = (uint32_t)shift % 64u;
		if (lo_limb < f->_nlimbs)
			f->_d[lo_limb] = full_man << lo_off;
		if (lo_off > 0u && lo_limb + 1u < f->_nlimbs)
			f->_d[lo_limb + 1u] = full_man >> (64u - lo_off);
	} else {
		uint64_t scaled = full_man >> (unsigned)(-shift);
		if (f->_nlimbs > 0u) f->_d[0] = scaled;
	}
#else
	if (shift >= 0) {
		uint64_t scaled = full_man << (unsigned)(shift % 64);
		uint32_t lo = (uint32_t)(scaled & 0xffffffffu);
		uint32_t hi = (uint32_t)(scaled >> 32);
		uint32_t base_limb = (uint32_t)(prec - 1) / 32u;
		if (base_limb > 0 && (base_limb - 1) < f->_nlimbs) f->_d[base_limb - 1] = lo;
		if (base_limb < f->_nlimbs)                         f->_d[base_limb]     = hi;
	} else {
		uint64_t scaled = full_man >> (unsigned)(-shift);
		if (f->_nlimbs > 0) f->_d[0] = (uint32_t)(scaled & 0xffffffffu);
		if (f->_nlimbs > 1) f->_d[1] = (uint32_t)(scaled >> 32);
	}
#endif
	return true;
}
bool bvn_float_to_double(const bvn_float_t *f, double *out)
{
	if (!f || !out) return false;
	if (bvn_float_is_nan(f))  { *out = (f->_sign < 0) ? -NAN  : NAN;      return true; }
	if (bvn_float_is_inf(f))  { *out = (f->_sign < 0) ? -INFINITY : INFINITY; return true; }
	if (bvn_float_is_zero(f)) { *out = (f->_sign < 0) ? -0.0 : 0.0;       return true; }
	char buf[64];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) return false;
	char *end = NULL;
	double v = strtod(buf, &end);
	if (end == buf || end == NULL || *end != '\0') return false;
	*out = v;
	return true;
}
bool bvn_float_from_float(bvn_float_t *f, float v)
{
	return bvn_float_from_double(f, (double)v);
}
bool bvn_float_to_float(const bvn_float_t *f, float *out)
{
	if (!out) return false;
	double d;
	if (!bvn_float_to_double(f, &d)) return false;
	*out = (float)d;
	return true;
}
#include "bvn_float_impl.h"
void bvn_float_to_ieee_bin(const bvn_float_t *f,
							uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							uint32_t *bits, int bits32)
{
	for (int i = 0; i < bits32; i++) bits[i] = 0;
	if (bvn_float_is_nan(f)) {
		for (uint32_t i = 0; i < exp_bits; i++)
			bits[(man_bits + i) / 32] |= 1u << ((man_bits + i) % 32);
		if (man_bits > 0) bits[(man_bits - 1) / 32] |= 1u << ((man_bits - 1) % 32);
		if (f->_sign < 0) {
			uint32_t sp = man_bits + exp_bits;
			bits[sp / 32] |= 1u << (sp % 32);
		}
		return;
	}
	if (bvn_float_is_inf(f)) {
		for (uint32_t i = 0; i < exp_bits; i++)
			bits[(man_bits + i) / 32] |= 1u << ((man_bits + i) % 32);
		if (f->_sign < 0) {
			uint32_t sp = man_bits + exp_bits;
			bits[sp / 32] |= 1u << (sp % 32);
		}
		return;
	}
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) return;
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	bvn_float_parse(&ctx, buf, &p);
	BinFmt fmt = { (int)exp_bits, (int)man_bits, (int)bias };
	to_ieee_binary(&ctx, &p, &fmt, bits, bits32);
}
bool bvn_float_from_ieee_bin(bvn_float_t *f,
							  uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							  const uint32_t *bits, int bits32)
{
	if (!f || !bits) return false;
	int total     = 1 + (int)exp_bits + (int)man_bits;
	uint32_t eall = (1u << exp_bits) - 1u;
	uint32_t raw_exp = 0;
	for (uint32_t i = 0; i < exp_bits; i++) {
		uint32_t bit_idx = man_bits + i;
		if ((bits[bit_idx / 32] >> (bit_idx % 32)) & 1u)
			raw_exp |= 1u << i;
	}
	uint32_t sign_idx = (uint32_t)(total - 1);
	bool neg = ((bits[sign_idx / 32] >> (sign_idx % 32)) & 1u) != 0u;
	if (raw_exp == eall) {
		bool mant_zero = true;
		for (uint32_t i = 0; i < man_bits && mant_zero; i++) {
			if ((bits[i / 32] >> (i % 32)) & 1u) mant_zero = false;
		}
		if (mant_zero) bvn_float_set_inf(f, neg);
		else           bvn_float_set_nan(f);
		return true;
	}
	if (raw_exp == 0) {
		bool mant_zero = true;
		for (uint32_t i = 0; i < man_bits && mant_zero; i++) {
			if ((bits[i / 32] >> (i % 32)) & 1u) mant_zero = false;
		}
		if (mant_zero) { bvn_float_set_zero(f, neg); return true; }
	}
	uint32_t tmp_words[8] = {0};
	int w32 = (total + 31) / 32;
	if (w32 > 8) w32 = 8;
	for (int i = 0; i < w32 && i < bits32; i++) tmp_words[i] = bits[i];
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	{
		long unbiased = (long)raw_exp - (long)bias;
		bvn_int_zero(&p.coeff);
		if (raw_exp == 0) {
		} else {
			bvn_int_from_uint64(&p.coeff, 1u);
			bvn_int_shl(&p.coeff, (int)man_bits);
		}
		for (uint32_t i = 0; i < man_bits; i++) {
			if ((bits[i / 32] >> (i % 32)) & 1u)
				bvn_int_setbit(&p.coeff, (int)i);
		}
		p.neg = neg;
		p.dex = (int)(unbiased - (long)man_bits);
		p.nan = false; p.inf = false;
	}
	(void)tmp_words; (void)ctx; (void)p;
	f->_sign = neg ? -1 : 1;
	long unbiased = (long)raw_exp - (long)bias;
	uint32_t _mb[BVN_INT_MAX_BITS / 32];
	bvn_int_t mant = { _mb, BVN_INT_MAX_BITS/32, 0, false, false };
	memset(_mb, 0, sizeof _mb);
	if (raw_exp != 0) {
		bvn_int_from_uint64(&mant, 1u);
		if (!bvn_int_shl(&mant, (int)man_bits)) return false;
	}
	for (uint32_t i = 0; i < man_bits; i++) {
		if ((bits[i / 32] >> (i % 32)) & 1u)
			if (!bvn_int_setbit(&mant, (int)i)) return false;
	}
	if (bvn_int_is_zero(&mant)) { bvn_float_set_zero(f, neg); return true; }
	int leading = bvn_int_bitlen(&mant) - 1;
	long mpfr_exp = (raw_exp != 0) ?
					(unbiased + 1L) :
					(unbiased - (long)man_bits + (long)leading + 1L);
	int target_top = (int)f->_prec - 1;
	int cur_top    = leading;
	int shift      = target_top - cur_top;
	uint32_t _qb[BVN_INT_MAX_BITS / 32];
	bvn_int_t Q = { _qb, BVN_INT_MAX_BITS/32, 0, false, false };
	memset(_qb, 0, sizeof _qb);
	bvn_int_copy(&Q, &mant);
	if (shift > 0) bvn_int_shl(&Q, shift);
	else if (shift < 0) bvn_int_shr(&Q, -shift);
	f->_exp = mpfr_exp;
	bvnf_store_mant(f, &Q);
	return true;
}
void bvn_float_to_bin16(const bvn_float_t *f, uint16_t *out)
{
	uint32_t b[1] = {0};
	bvn_float_to_ieee_bin(f, 5, 10, 15, b, 1);
	*out = (uint16_t)b[0];
}
void bvn_float_to_bin32(const bvn_float_t *f, uint32_t *out)
{
	bvn_float_to_ieee_bin(f, 8, 23, 127, out, 1);
}
void bvn_float_to_bin64(const bvn_float_t *f, uint64_t *out)
{
	uint32_t b[2] = {0};
	bvn_float_to_ieee_bin(f, 11, 52, 1023, b, 2);
	*out = (uint64_t)b[0] | ((uint64_t)b[1] << 32);
}
void bvn_float_to_bin128(const bvn_float_t *f, uint32_t out[4])
{
	bvn_float_to_ieee_bin(f, 15, 112, 16383, out, 4);
}
void bvn_float_to_bin256(const bvn_float_t *f, uint32_t out[8])
{
	bvn_float_to_ieee_bin(f, 19, 236, 262143, out, 8);
}
bool bvn_float_from_bin16(bvn_float_t *f, uint16_t bits)
{
	uint32_t b = bits;
	return bvn_float_from_ieee_bin(f, 5, 10, 15, &b, 1);
}
bool bvn_float_from_bin32(bvn_float_t *f, uint32_t bits)
{
	return bvn_float_from_ieee_bin(f, 8, 23, 127, &bits, 1);
}
bool bvn_float_from_bin64(bvn_float_t *f, uint64_t bits)
{
	uint32_t b[2] = { (uint32_t)(bits & 0xffffffffu), (uint32_t)(bits >> 32) };
	return bvn_float_from_ieee_bin(f, 11, 52, 1023, b, 2);
}
bool bvn_float_from_bin128(bvn_float_t *f, const uint32_t bits[4])
{
	return bvn_float_from_ieee_bin(f, 15, 112, 16383, bits, 4);
}
bool bvn_float_from_bin256(bvn_float_t *f, const uint32_t bits[8])
{
	return bvn_float_from_ieee_bin(f, 19, 236, 262143, bits, 8);
}
static bool from_ieee_decimal(bvn_float_t *f,
							   int total_bits, int exp_bits, int coeff_bits,
							   int bias, const uint32_t *bits, int bits32)
{
	if (!f || !bits) return false;
	int sign_pos = total_bits - 1;
	bool neg = ((bits[sign_pos / 32] >> (sign_pos % 32)) & 1u) != 0u;
	uint32_t raw_exp = 0;
	for (int i = 0; i < exp_bits && i < 32; i++) {
		int pos = coeff_bits + i;
		if (pos >= bits32 * 32) break;
		if ((bits[pos / 32] >> (pos % 32)) & 1u)
			raw_exp |= (1u << i);
	}
	uint32_t exp_max = (exp_bits < 32) ? ((1u << exp_bits) - 1u) : 0xffffffffu;
	if (raw_exp == exp_max) {
		bool coeff_zero = true;
		for (int i = 0; i < coeff_bits && coeff_zero; i++) {
			if (i >= bits32 * 32) break;
			if ((bits[i / 32] >> (i % 32)) & 1u) coeff_zero = false;
		}
		if (coeff_zero) bvn_float_set_inf(f, neg);
		else            bvn_float_set_nan(f);
		return true;
	}
	int cb_words = (coeff_bits + 31) / 32;
	if (cb_words > 8) cb_words = 8;
	uint32_t cb[8] = {0};
	for (int i = 0; i < coeff_bits && i < 256; i++) {
		if (i >= bits32 * 32) break;
		if ((bits[i / 32] >> (i % 32)) & 1u)
			cb[i / 32] |= (1u << (i % 32));
	}
	bool coeff_zero = true;
	for (int i = 0; i < cb_words && coeff_zero; i++)
		if (cb[i]) coeff_zero = false;
	if (coeff_zero) { bvn_float_set_zero(f, neg); return true; }
	int64_t unbiased = (int64_t)raw_exp - (int64_t)bias;
	uint32_t tmp[8];
	for (int i = 0; i < cb_words; i++) tmp[i] = cb[i];
	bvn_int_t T;
	T.limbs    = tmp;
	T.nlimbs   = (uint32_t)cb_words;
	T.negative = false;
	T.heap     = false;
	T.nused    = 0;
	for (int i = cb_words - 1; i >= 0; i--) {
		if (tmp[i]) { T.nused = (uint32_t)(i + 1); break; }
	}
	char rev[256];
	int  rlen = 0;
	while (!bvn_int_is_zero(&T) && rlen < 255) {
		uint32_t rem = bvn_int_div_u32(&T, 10u);
		rev[rlen++]  = (char)('0' + rem);
	}
	char final_str[320];
	int  flen = 0;
	if (neg) final_str[flen++] = '-';
	for (int i = rlen - 1; i >= 0; i--)
		final_str[flen++] = rev[i];
	int n = snprintf(final_str + flen, sizeof(final_str) - (size_t)flen,
					 "e%" PRId64, unbiased);
	if (n < 0 || (size_t)n >= sizeof(final_str) - (size_t)flen) return false;
	flen += n;
	final_str[flen] = '\0';
	return bvn_float_from_str(f, final_str, 10);
}
void bvn_float_to_dec16(const bvn_float_t *f, uint16_t *out)
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { *out = 0; return; }
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	bvn_float_parse(&ctx, buf, &p);
	uint32_t b[1] = {0};
	DecFmt fmt = {16, 6, 9, 101, 2};
	to_ieee_decimal(&ctx, &p, &fmt, b, 1);
	*out = (uint16_t)b[0];
}
void bvn_float_to_dec32(const bvn_float_t *f, uint32_t *out)
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { *out = 0; return; }
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	bvn_float_parse(&ctx, buf, &p);
	DecFmt fmt = {32, 8, 23, 101, 7};
	to_ieee_decimal(&ctx, &p, &fmt, out, 1);
}
void bvn_float_to_dec64(const bvn_float_t *f, uint64_t *out)
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { *out = 0; return; }
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	bvn_float_parse(&ctx, buf, &p);
	uint32_t b[2] = {0, 0};
	DecFmt fmt = {64, 10, 53, 398, 16};
	to_ieee_decimal(&ctx, &p, &fmt, b, 2);
	*out = (uint64_t)b[0] | ((uint64_t)b[1] << 32);
}
void bvn_float_to_dec128(const bvn_float_t *f, uint32_t out[4])
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { memset(out, 0, 16); return; }
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	bvn_float_parse(&ctx, buf, &p);
	DecFmt fmt = {128, 14, 113, 6176, 34};
	to_ieee_decimal(&ctx, &p, &fmt, out, 4);
}
void bvn_float_to_dec256(const bvn_float_t *f, uint32_t out[8])
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { memset(out, 0, 32); return; }
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);
	PNum p; bvn_float_pnum_init(&p);
	bvn_float_parse(&ctx, buf, &p);
	DecFmt fmt = {256, 20, 235, 611867, 70};
	to_ieee_decimal(&ctx, &p, &fmt, out, 8);
}
bool bvn_float_from_dec16(bvn_float_t *f, uint16_t bits)
{
	uint32_t b = bits;
	return from_ieee_decimal(f, 16, 6, 9, 101, &b, 1);
}
bool bvn_float_from_dec32(bvn_float_t *f, uint32_t bits)
{
	return from_ieee_decimal(f, 32, 8, 23, 101, &bits, 1);
}
bool bvn_float_from_dec64(bvn_float_t *f, uint64_t bits)
{
	uint32_t b[2] = { (uint32_t)(bits & 0xffffffffu), (uint32_t)(bits >> 32) };
	return from_ieee_decimal(f, 64, 10, 53, 398, b, 2);
}
bool bvn_float_from_dec128(bvn_float_t *f, const uint32_t bits[4])
{
	return from_ieee_decimal(f, 128, 14, 113, 6176, bits, 4);
}
bool bvn_float_from_dec256(bvn_float_t *f, const uint32_t bits[8])
{
	return from_ieee_decimal(f, 256, 20, 235, 611867, bits, 8);
}
int16_t bvn_float_to_fix16(const bvn_float_t *f, uint32_t frac_bits)
{
	if (!f) return 0;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) return 0;
	return bvn_float_parse_fix16(buf, (int)frac_bits);
}
int32_t bvn_float_to_fix32(const bvn_float_t *f, uint32_t frac_bits)
{
	if (!f) return 0;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) return 0;
	return bvn_float_parse_fix32(buf, (int)frac_bits);
}
int64_t bvn_float_to_fix64(const bvn_float_t *f, uint32_t frac_bits)
{
	if (!f) return 0;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) return 0;
	return bvn_float_parse_fix64(buf, (int)frac_bits);
}
void bvn_float_to_fix128(const bvn_float_t *f, uint32_t frac_bits, uint32_t out[4])
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { memset(out, 0, 16); return; }
	bvn_float_parse_fix128(buf, (int)frac_bits, out);
}
void bvn_float_to_fix256(const bvn_float_t *f, uint32_t frac_bits, uint32_t out[8])
{
	if (!f || !out) return;
	char buf[bvn_float_str_bufsize((uint32_t)f->_prec, 10)];
	int n = bvn_float_to_str(f, buf, sizeof buf, 10);
	if (n <= 0) { memset(out, 0, 32); return; }
	bvn_float_parse_fix256(buf, (int)frac_bits, out);
}
static bool from_fix_common(bvn_float_t *f, bool neg,
							 const uint32_t *abs_words, int nwords,
							 uint32_t frac_bits)
{
	if (!f) return false;
	if (nwords < 1 || nwords > 8) return false;
	bool is_zero = true;
	for (int i = 0; i < nwords && is_zero; i++)
		if (abs_words[i]) is_zero = false;
	if (is_zero) { bvn_float_set_zero(f, neg); return true; }
	uint32_t _mb[8];
	for (int i = 0; i < 8; i++) _mb[i] = (i < nwords) ? abs_words[i] : 0u;
	bvn_int_t M;
	M.limbs    = _mb;
	M.nlimbs   = 8;
	M.negative = false;
	M.heap     = false;
	M.nused    = 0;
	for (int i = 7; i >= 0; i--) {
		if (_mb[i]) { M.nused = (uint32_t)(i + 1); break; }
	}
	int leading = bvn_int_bitlen(&M) - 1;
	long mpfr_exp = (long)leading + 1L - (long)frac_bits;
	uint32_t _qb[16];
	memset(_qb, 0, sizeof _qb);
	for (int i = 0; i < 8; i++) _qb[i] = _mb[i];
	bvn_int_t Q;
	Q.limbs    = _qb;
	Q.nlimbs   = 16;
	Q.nused    = M.nused;
	Q.negative = false;
	Q.heap     = false;
	int shift = (int)f->_prec - 1 - leading;
	if (shift > 0) { if (!bvn_int_shl(&Q, shift))  return false; }
	else if (shift < 0) { bvn_int_shr(&Q, -shift); }
	f->_sign = neg ? -1 : 1;
	f->_exp  = mpfr_exp;
	bvnf_store_mant(f, &Q);
	return true;
}
bool bvn_float_from_fix16(bvn_float_t *f, int16_t bits, uint32_t frac_bits)
{
	if (!f) return false;
	bool neg = (bits < 0);
	uint32_t abs_v = neg ? ((uint32_t)(-(int32_t)bits)) : (uint32_t)bits;
	return from_fix_common(f, neg, &abs_v, 1, frac_bits);
}
bool bvn_float_from_fix32(bvn_float_t *f, int32_t bits, uint32_t frac_bits)
{
	if (!f) return false;
	bool neg = (bits < 0);
	uint32_t abs_v = neg ? ((uint32_t)(-(int64_t)bits)) : (uint32_t)bits;
	return from_fix_common(f, neg, &abs_v, 1, frac_bits);
}
bool bvn_float_from_fix64(bvn_float_t *f, int64_t bits, uint32_t frac_bits)
{
	if (!f) return false;
	bool neg = (bits < 0);
	uint64_t abs_v;
	if (bits == INT64_MIN) abs_v = (uint64_t)INT64_MAX + 1ULL;
	else abs_v = (uint64_t)(neg ? -bits : bits);
	uint32_t w[2] = { (uint32_t)(abs_v & 0xffffffffu), (uint32_t)(abs_v >> 32) };
	return from_fix_common(f, neg, w, 2, frac_bits);
}
bool bvn_float_from_fix128(bvn_float_t *f, const uint32_t bits[4],
							uint32_t frac_bits)
{
	if (!f || !bits) return false;
	bool neg = ((bits[3] >> 31) & 1u) != 0u;
	uint32_t w[4];
	if (neg) {
		uint64_t carry = 1;
		for (int i = 0; i < 4; i++) {
			uint64_t v = (uint64_t)(~bits[i]) + carry;
			w[i] = (uint32_t)v; carry = v >> 32;
		}
	} else {
		for (int i = 0; i < 4; i++) w[i] = bits[i];
	}
	return from_fix_common(f, neg, w, 4, frac_bits);
}
bool bvn_float_from_fix256(bvn_float_t *f, const uint32_t bits[8],
							uint32_t frac_bits)
{
	if (!f || !bits) return false;
	bool neg = ((bits[7] >> 31) & 1u) != 0u;
	uint32_t w[8];
	if (neg) {
		uint64_t carry = 1;
		for (int i = 0; i < 8; i++) {
			uint64_t v = (uint64_t)(~bits[i]) + carry;
			w[i] = (uint32_t)v; carry = v >> 32;
		}
	} else {
		for (int i = 0; i < 8; i++) w[i] = bits[i];
	}
	return from_fix_common(f, neg, w, 8, frac_bits);
}
