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

/* Internal helpers shared by bvn_gregorian_date.c and bvn_datetime.c.
 * Not installed; do not include from public headers.  Strictly C99. */

#ifndef BVN_DATE_IMPL_H_
#define BVN_DATE_IMPL_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Overflow-checked int64_t arithmetic, C99 replacements for the C23
 * <stdckdint.h> ckd_* intrinsics: compute *r = a OP b and return true iff
 * the mathematical result does not fit in int64_t.  Exactly like ckd_*,
 * *r always receives the result wrapped to int64_t, so chained checks may
 * keep computing with it and only test the accumulated flag at the end.
 * Only C99-guaranteed behavior is used: unsigned arithmetic wraps, the
 * exact-width types are two's complement (so memcpy reinterpretation is
 * well defined), division truncates toward zero, and the one hazardous
 * division, INT64_MIN / -1, is excluded before dividing. */

static inline int64_t bvn_wrap_i64(uint64_t u)
{
	int64_t r;
	memcpy(&r, &u, sizeof r);
	return r;
}

static inline bool bvn_ckd_add(int64_t* r, int64_t a, int64_t b)
{
	*r = bvn_wrap_i64((uint64_t)a + (uint64_t)b);
	return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
}

static inline bool bvn_ckd_sub(int64_t* r, int64_t a, int64_t b)
{
	*r = bvn_wrap_i64((uint64_t)a - (uint64_t)b);
	return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
}

static inline bool bvn_ckd_mul(int64_t* r, int64_t a, int64_t b)
{
	*r = bvn_wrap_i64((uint64_t)a * (uint64_t)b);
	if (a == 0 || b == 0)
		return false;
	if (a == -1)		/* avoid INT64_MIN / -1 below */
		return b == INT64_MIN;
	if (b == -1)
		return a == INT64_MIN;
	if (a > 0) {
		if (b > 0)
			return a > INT64_MAX / b;
		return b < INT64_MIN / a;
	}
	if (b > 0)
		return a < INT64_MIN / b;
	return a < INT64_MAX / b;
}

/* Floor division/modulo for int64_t (branch-free; C's / and % truncate
 * toward zero, which is wrong for the negative operands that proleptic
 * dates and pre-epoch timestamps produce). */
static inline int64_t floor_div(int64_t a, int64_t b)
{
	int64_t q = a / b;
	int64_t r = a % b;
	int64_t needs_adj = (int64_t)((uint64_t)(r ^ b) >> 63) & -(int64_t)(r != 0);
	return q - needs_adj;
}

static inline int64_t floor_mod(int64_t a, int64_t b)
{
	int64_t r = a % b;
	int64_t needs_adj = -(int64_t)(((uint64_t)(r ^ b) >> 63)
						& (uint64_t)(r != 0));
	return r + (b & needs_adj);
}

/* n / d rounded to the nearest integer, halves away from zero.  Requires
 * d > 0.  Free of intermediate overflow for every n including INT64_MIN:
 * |r| < d, so 2*r fits, and the +/-1 adjustment only fires when |n| >= d/2,
 * keeping q strictly inside the int64_t range. */
static inline int64_t div_round_closest(int64_t n, int64_t d)
{
	int64_t q = n / d;
	int64_t r = n % d;
	if (r >= 0) {
		if (2*r >= d)
			q++;
	} else {
		if (-2*r >= d)
			q--;
	}
	return q;
}

#endif /* BVN_DATE_IMPL_H_ */
