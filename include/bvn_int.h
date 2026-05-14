#ifndef BVN_INT_H_
#define BVN_INT_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BVN_INT_MAX_BITS  32768u
#define BVN_INT_MAX_LIMBS (BVN_INT_MAX_BITS / 32u)
typedef struct bvn_int_s {
	uint32_t *limbs;
	uint32_t  nlimbs;
	uint32_t  nused;
	bool      negative;
	bool      heap;
} bvn_int_t;
bvn_int_t *bvn_int_alloc(void);
void       bvn_int_free(bvn_int_t *n);
bool    bvn_int_from_str(bvn_int_t *n, const char *s, uint32_t base);
int32_t bvn_int_to_str(const bvn_int_t *n,
						char *buf, size_t bufsize,
						uint32_t base);
size_t  bvn_int_str_bufsize(uint32_t bits, uint32_t base);
bool bvn_int_is_zero(const bvn_int_t *n);
bool bvn_int_from_int64 (bvn_int_t *n, int64_t  v);
bool bvn_int_from_uint64(bvn_int_t *n, uint64_t v);
bool bvn_int_from_int32 (bvn_int_t *n, int32_t  v);
bool bvn_int_from_uint32(bvn_int_t *n, uint32_t v);
bool bvn_int_from_int16 (bvn_int_t *n, int16_t  v);
bool bvn_int_from_uint16(bvn_int_t *n, uint16_t v);
bool bvn_int_from_int8  (bvn_int_t *n, int8_t   v);
bool bvn_int_from_uint8 (bvn_int_t *n, uint8_t  v);
bool bvn_int_to_int64 (const bvn_int_t *n, int64_t  *out);
bool bvn_int_to_uint64(const bvn_int_t *n, uint64_t *out);
bool bvn_int_to_int32 (const bvn_int_t *n, int32_t  *out);
bool bvn_int_to_uint32(const bvn_int_t *n, uint32_t *out);
bool bvn_int_to_int16 (const bvn_int_t *n, int16_t  *out);
bool bvn_int_to_uint16(const bvn_int_t *n, uint16_t *out);
bool bvn_int_to_int8  (const bvn_int_t *n, int8_t   *out);
bool bvn_int_to_uint8 (const bvn_int_t *n, uint8_t  *out);
void     bvn_int_zero(bvn_int_t *n);
void     bvn_int_norm(bvn_int_t *n);
bool     bvn_int_copy(bvn_int_t *dst, const bvn_int_t *src);
bool     bvn_int_add_u32(bvn_int_t *n, uint32_t v);
bool     bvn_int_mul_u32(bvn_int_t *n, uint32_t v);
bool     bvn_int_mul_pow10(bvn_int_t *n, int k);
uint32_t bvn_int_div_u32(bvn_int_t *n, uint32_t v);
bool bvn_int_shl(bvn_int_t *n, int bits);
void bvn_int_shr(bvn_int_t *n, int bits);
int  bvn_int_bitlen(const bvn_int_t *n);
int  bvn_int_getbit(const bvn_int_t *n, int i);
bool bvn_int_setbit(bvn_int_t *n, int i);
int  bvn_int_cmp(const bvn_int_t *a, const bvn_int_t *b);
bool bvn_int_sub_inplace(bvn_int_t *a, const bvn_int_t *b);
bool bvn_int_divrem(bvn_int_t *q, bvn_int_t *r,
					const bvn_int_t *a, const bvn_int_t *b);
#ifdef __cplusplus
}
#endif
#endif
