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
#include "bovnar_currency.h"   /* BVN_CURRENCY_FIRST, for the block check below */
#define BVN_EVENT_COUNT             15
#define BVN_ERROR_COUNT             52
#define BVN_PREFIX_SYSTEM_COUNT      2
#define BVN_SI_PREFIX_COUNT         25
#define BVN_IEC_PREFIX_COUNT        11
typedef char bvn_internal_dims_event_check[
	(ev_stream_end + 1 == BVN_EVENT_COUNT) ? 1 : -1];
/* Anchored to the LAST enumerator, like the event and unit checks below — not to
 * a named one in the middle. Pinned to error_duplicate_struct_key it stayed green
 * across seven new codes (42..48), and BVN_ERROR_COUNT is what the fuzz harnesses
 * use as their "is this a real error code" bound: they __builtin_trap() above it,
 * so a stale count turns a legitimate spec-1.1 error into a fuzz crash. */
typedef char bvn_internal_dims_error_check[
	(error_octet_stream_forbidden + 1 == BVN_ERROR_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_prefix_system_check[
	(prefix_iec + 1 == BVN_PREFIX_SYSTEM_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_si_prefix_check[
	(si_quetta + 1 == BVN_SI_PREFIX_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_iec_prefix_check[
	(iec_quebi + 1 == BVN_IEC_PREFIX_COUNT) ? 1 : -1];
/*
 * The dense unit tables (si_conv_table, bu_prefix_policy) have one row per
 * DEFINED base unit, not one per id — the id space is blocked and sparse, so
 * indexing those tables by an id would size them at nearly a million rows and
 * fill them with holes that read back as valid units. BVN_UNIT_SLOT_COUNT is
 * that row count and bvni_unit_slot() is the id -> row map.
 *
 * gen_units.py and gen_profiles.py each emit half of the layout from their own
 * read of the data files. This recomputes the total from both halves, so the
 * two cannot drift: bu_none, then every native unit, then every opaque unit.
 */
typedef char bvn_internal_dims_unit_slot_check[
	(1 + BVN_UNIT_NATIVE_COUNT + BVN_PROFILE_OPAQUE_COUNT
	 == BVN_UNIT_SLOT_COUNT) ? 1 : -1];
/* Every block starts at or above the first block's floor, so an id below it is
 * bu_none or nothing at all — which is what lets bvni_unit_slot() reject the
 * whole of the space below the blocks in one comparison. */
typedef char bvn_internal_dims_unit_block_check[
	(BVN_UNIT_NATIVE_FIRST == BVN_UNIT_ID_FIRST) ? 1 : -1];
/* Currencies must not overlap a unit block: bvn_unit_is_currency() is a bounds
 * check, and an overlap would make one id answer to two vocabularies. */
typedef char bvn_internal_dims_currency_block_check[
	(BVN_CURRENCY_FIRST > BVN_PROFILE_UDUNITS_OPAQUE_FIRST) ? 1 : -1];
/*
 * The row of `base` in a dense unit table, or -1 if `base` has no row.
 *
 * -1 covers three different things and every caller must treat all three alike:
 * an id that names no unit at all, a currency (which has a catalogue row, not a
 * conversion row), and any negative or absurd value a caller may have put in a
 * value_unit_component_t by hand. This is the ONLY way to index those tables —
 * a base unit id is not an array index, and has not been since the id space was
 * blocked.
 *
 * bu_none is slot 0, which is a real row: it is the identity for conversion and
 * carries no prefix policy, so the tables want an entry for it.
 */
static inline int32_t bvni_unit_slot(value_base_unit_t base)
{
	int32_t v = (int32_t)base;
	if (v == (int32_t)bu_none)
		return 0;
	/* One comparison discards everything below the first block, which is the
	 * whole of the old dense id space plus every negative. */
	if (v < BVN_UNIT_ID_FIRST)
		return -1;
	if (v <= BVN_UNIT_NATIVE_LAST)
		return BVN_SLOT_NATIVE(v);
	return BVN_PROFILE_OPAQUE_SLOT(v);
}
/* Whether `base` names a base unit this build defines — a native unit, a
 * profile's opaque unit, a currency, or bu_none. The blocked id space is sparse,
 * so this is NOT a range check and must not be written as one. */
static inline bool bvni_base_defined(value_base_unit_t base)
{
	return bvni_unit_slot(base) >= 0 || bvn_unit_is_currency((int)base);
}
#endif
