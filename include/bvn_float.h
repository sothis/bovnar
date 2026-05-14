#ifndef BVN_FLOAT_H_
#define BVN_FLOAT_H_
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
} bvn_float_t;
bvn_float_t *bvn_float_alloc(uint32_t prec);
void bvn_float_free(bvn_float_t *f);
void bvn_float_init_buf(bvn_float_t *f, uint32_t prec,
						bvn_limb_t *buf, uint32_t nlimbs);
static inline bool bvn_float_is_nan(const bvn_float_t *f)
	{ return f->_exp == BVN_FLOAT_EXP_NAN; }
static inline bool bvn_float_is_inf(const bvn_float_t *f)
	{ return f->_exp == BVN_FLOAT_EXP_INF; }
static inline bool bvn_float_is_zero(const bvn_float_t *f)
	{ return f->_exp == BVN_FLOAT_EXP_ZERO; }
static inline bool bvn_float_is_neg(const bvn_float_t *f)
	{ return f->_sign < 0; }
static inline bool bvn_float_is_regular(const bvn_float_t *f)
{
	return f->_exp != BVN_FLOAT_EXP_NAN
		&& f->_exp != BVN_FLOAT_EXP_INF
		&& f->_exp != BVN_FLOAT_EXP_ZERO;
}
void bvn_float_set_nan (bvn_float_t *f);
void bvn_float_set_inf (bvn_float_t *f, bool neg);
void bvn_float_set_zero(bvn_float_t *f, bool neg);
bool bvn_float_copy(bvn_float_t *dst, const bvn_float_t *src);
bool bvn_float_from_str(bvn_float_t *f, const char *s, uint32_t base);
int32_t bvn_float_to_str(const bvn_float_t *f, char *buf, size_t bufsize,
						  uint32_t base);
size_t bvn_float_str_bufsize(uint32_t prec, uint32_t base);
bool bvn_float_from_double(bvn_float_t *f, double v);
bool bvn_float_to_double  (const bvn_float_t *f, double *out);
bool bvn_float_from_float (bvn_float_t *f, float v);
bool bvn_float_to_float   (const bvn_float_t *f, float *out);
void bvn_float_to_ieee_bin(const bvn_float_t *f,
							uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							uint32_t *bits, int bits32);
bool bvn_float_from_ieee_bin(bvn_float_t *f,
							  uint32_t exp_bits, uint32_t man_bits, int32_t bias,
							  const uint32_t *bits, int bits32);
void bvn_float_to_bin16 (const bvn_float_t *f, uint16_t *out);
void bvn_float_to_bin32 (const bvn_float_t *f, uint32_t *out);
void bvn_float_to_bin64 (const bvn_float_t *f, uint64_t *out);
void bvn_float_to_bin128(const bvn_float_t *f, uint32_t out[4]);
void bvn_float_to_bin256(const bvn_float_t *f, uint32_t out[8]);
bool bvn_float_from_bin16 (bvn_float_t *f, uint16_t bits);
bool bvn_float_from_bin32 (bvn_float_t *f, uint32_t bits);
bool bvn_float_from_bin64 (bvn_float_t *f, uint64_t bits);
bool bvn_float_from_bin128(bvn_float_t *f, const uint32_t bits[4]);
bool bvn_float_from_bin256(bvn_float_t *f, const uint32_t bits[8]);
void bvn_float_to_dec16 (const bvn_float_t *f, uint16_t *out);
void bvn_float_to_dec32 (const bvn_float_t *f, uint32_t *out);
void bvn_float_to_dec64 (const bvn_float_t *f, uint64_t *out);
void bvn_float_to_dec128(const bvn_float_t *f, uint32_t out[4]);
void bvn_float_to_dec256(const bvn_float_t *f, uint32_t out[8]);
bool bvn_float_from_dec16 (bvn_float_t *f, uint16_t bits);
bool bvn_float_from_dec32 (bvn_float_t *f, uint32_t bits);
bool bvn_float_from_dec64 (bvn_float_t *f, uint64_t bits);
bool bvn_float_from_dec128(bvn_float_t *f, const uint32_t bits[4]);
bool bvn_float_from_dec256(bvn_float_t *f, const uint32_t bits[8]);
int16_t bvn_float_to_fix16 (const bvn_float_t *f, uint32_t frac_bits);
int32_t bvn_float_to_fix32 (const bvn_float_t *f, uint32_t frac_bits);
int64_t bvn_float_to_fix64 (const bvn_float_t *f, uint32_t frac_bits);
void    bvn_float_to_fix128(const bvn_float_t *f, uint32_t frac_bits,
							 uint32_t out[4]);
void    bvn_float_to_fix256(const bvn_float_t *f, uint32_t frac_bits,
							 uint32_t out[8]);
bool bvn_float_from_fix16 (bvn_float_t *f, int16_t  bits, uint32_t frac_bits);
bool bvn_float_from_fix32 (bvn_float_t *f, int32_t  bits, uint32_t frac_bits);
bool bvn_float_from_fix64 (bvn_float_t *f, int64_t  bits, uint32_t frac_bits);
bool bvn_float_from_fix128(bvn_float_t *f, const uint32_t bits[4],
							uint32_t frac_bits);
bool bvn_float_from_fix256(bvn_float_t *f, const uint32_t bits[8],
							uint32_t frac_bits);
#ifdef __cplusplus
}
#endif
#endif
