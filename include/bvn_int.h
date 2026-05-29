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

#ifndef BVN_INT_H_
#define BVN_INT_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef BVN_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define BVN_API __declspec(dllexport)
#  elif defined(__GNUC__) || defined(__clang__)
#    define BVN_API __attribute__((visibility("default")))
#  else
#    define BVN_API
#  endif
#endif
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
	uint64_t  _reserved[2];
} bvn_int_t;
BVN_API bvn_int_t *bvn_int_alloc(void);
BVN_API void       bvn_int_free(bvn_int_t *n);
BVN_API bool    bvn_int_from_str(bvn_int_t *n, const char *s, uint32_t base);
BVN_API int32_t bvn_int_to_str(const bvn_int_t *n,
						char *buf, size_t bufsize,
						uint32_t base);
BVN_API size_t  bvn_int_str_bufsize(uint32_t bits, uint32_t base);
BVN_API bool bvn_int_is_zero(const bvn_int_t *n);
BVN_API bool bvn_int_from_int64 (bvn_int_t *n, int64_t  v);
BVN_API bool bvn_int_from_uint64(bvn_int_t *n, uint64_t v);
BVN_API bool bvn_int_from_int32 (bvn_int_t *n, int32_t  v);
BVN_API bool bvn_int_from_uint32(bvn_int_t *n, uint32_t v);
BVN_API bool bvn_int_from_int16 (bvn_int_t *n, int16_t  v);
BVN_API bool bvn_int_from_uint16(bvn_int_t *n, uint16_t v);
BVN_API bool bvn_int_from_int8  (bvn_int_t *n, int8_t   v);
BVN_API bool bvn_int_from_uint8 (bvn_int_t *n, uint8_t  v);
BVN_API bool bvn_int_to_int64 (const bvn_int_t *n, int64_t  *out);
BVN_API bool bvn_int_to_uint64(const bvn_int_t *n, uint64_t *out);
BVN_API bool bvn_int_to_int32 (const bvn_int_t *n, int32_t  *out);
BVN_API bool bvn_int_to_uint32(const bvn_int_t *n, uint32_t *out);
BVN_API bool bvn_int_to_int16 (const bvn_int_t *n, int16_t  *out);
BVN_API bool bvn_int_to_uint16(const bvn_int_t *n, uint16_t *out);
BVN_API bool bvn_int_to_int8  (const bvn_int_t *n, int8_t   *out);
BVN_API bool bvn_int_to_uint8 (const bvn_int_t *n, uint8_t  *out);
BVN_API void     bvn_int_zero(bvn_int_t *n);
BVN_API void     bvn_int_norm(bvn_int_t *n);
BVN_API bool     bvn_int_copy(bvn_int_t *dst, const bvn_int_t *src);
BVN_API bool     bvn_int_add_u32(bvn_int_t *n, uint32_t v);
BVN_API bool     bvn_int_mul_u32(bvn_int_t *n, uint32_t v);
BVN_API bool     bvn_int_mul_pow10(bvn_int_t *n, int k);
BVN_API uint32_t bvn_int_div_u32(bvn_int_t *n, uint32_t v);
BVN_API bool bvn_int_shl(bvn_int_t *n, int bits);
BVN_API void bvn_int_shr(bvn_int_t *n, int bits);
BVN_API int  bvn_int_bitlen(const bvn_int_t *n);
BVN_API int  bvn_int_getbit(const bvn_int_t *n, int i);
BVN_API bool bvn_int_setbit(bvn_int_t *n, int i);
BVN_API int  bvn_int_cmp(const bvn_int_t *a, const bvn_int_t *b);
BVN_API bool bvn_int_sub_inplace(bvn_int_t *a, const bvn_int_t *b);
BVN_API bool bvn_int_divrem(bvn_int_t *q, bvn_int_t *r,
					const bvn_int_t *a, const bvn_int_t *b);
#ifdef __cplusplus
}
#endif
#endif
