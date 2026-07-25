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

#ifndef BVN_INTERNAL_DIMS_H_
#define BVN_INTERNAL_DIMS_H_
#include "bovnar.h"
#define BVN_EVENT_COUNT             15
#define BVN_ERROR_COUNT             42
#define BVN_PREFIX_SYSTEM_COUNT      2
#define BVN_SI_PREFIX_COUNT         25
#define BVN_IEC_PREFIX_COUNT        11
#define BVN_VALUE_BASE_UNIT_COUNT  383
typedef char bvn_internal_dims_event_check[
	(ev_stream_end + 1 == BVN_EVENT_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_error_check[
	(error_duplicate_struct_key + 1 == BVN_ERROR_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_prefix_system_check[
	(prefix_iec + 1 == BVN_PREFIX_SYSTEM_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_si_prefix_check[
	(si_quetta + 1 == BVN_SI_PREFIX_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_iec_prefix_check[
	(iec_quebi + 1 == BVN_IEC_PREFIX_COUNT) ? 1 : -1];
/* The highest enumerator, whichever block it lives in: physical units run
 * 1..133, 348..377 and 380.., the currency gap is 134..347, and the appended
 * currencies bu_zwg/bu_xcg sit at 378..379 between the two physical runs. Adding
 * a unit or a currency therefore has to move this — the tables sized by
 * BVN_VALUE_BASE_UNIT_COUNT are indexed BY the enum value, so an undersized
 * count is an out-of-bounds read, not a cosmetic mismatch. */
typedef char bvn_internal_dims_value_base_unit_check[
	(bu_kilometer_per_hour + 1 == BVN_VALUE_BASE_UNIT_COUNT) ? 1 : -1];
#endif
