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

#include "bovnar_currency.h"
#include "bovnar.h"   /* prefix_system_t (prefix_iec) for the prefix-validity rule */
#include <stddef.h>
#include <string.h>
#define N_TOTAL  BVN_CURRENCY_COUNT
/*
 * ===========================================================================
 * Currency catalogue
 * ===========================================================================
 *
 * bovnar treats currencies as base units, so a money value carries its currency
 * the way a length carries metres. This table is the catalogue, and it is
 * POSITIONAL: a currency's value_base_unit_t id is BVN_CURRENCY_FIRST plus its
 * index here, so every lookup is a subtraction (bvn_currency_index below).
 *
 * One run, no sub-ranges. The catalogue used to be three — fiat, then crypto,
 * then an "extension" segment stranded outside the block for currencies added
 * after it was frozen — with the kind predicates reading the id's segment
 * rather than the row. That made the ORDER of this file part of the ABI twice
 * over: a new fiat currency could not be appended without landing among the
 * crypto ids, and the extension existed only because the block had no room
 * left. Both are gone: the block is 10000 wide (see the id-space note in
 * bovnar.h), and is_crypto is read from the row.
 *
 * Each entry records the ISO numeric code (0 for crypto), the minor-unit digit
 * count (e.g. 2 for cents, 0 for yen, 18 for ether), whether it is crypto, and
 * a human name.
 *
 * minor_unit is carried for APPLICATIONS to read via bvn_currency_minor_unit();
 * nothing inside the library consumes it. In particular the validator does not
 * check a value's decimal scale against it -- <float_dec:64,$JPY> 1.5555 is
 * accepted even though JPY has no minor units -- because the format stores what
 * the writer wrote and money precision is the application's policy.
 */
static const bvn_currency_info_t g_currency_table[N_TOTAL] = {
  /* Generated from src/gendata/currencies.bvnr by gen_currencies.py, in the
   * ascending .id order the positional index relies on. */
#include "bovnar_currency_table.gen.inc"
};
/* The bounds and the rows now come from one generator run, so they cannot
 * disagree about how many currencies there are — but the array above is sized
 * from the bound and filled from the rows, and a truncated or double-written
 * include would still leave a zeroed slot that reads back as a currency with an
 * empty code. The check costs nothing and says which of the two is short. */
/* Negative array length = hard error, the same C99-clean idiom the io impl uses
 * for its size guards (_Static_assert is C11 and this builds as -std=c99). */
typedef char bvn_currency_rows_check_[
	BVNR_CURRENCY_ROWS == N_TOTAL ? 1 : -1];
typedef char bvn_currency_span_check_[
	BVN_CURRENCY_LAST - BVN_CURRENCY_FIRST + 1 == N_TOTAL ? 1 : -1];
/*
 * Map a currency base-unit id to its g_currency_table index, or -1 if the id is
 * not a currency at all. Every other entry point here is built on it.
 */
static int bvn_currency_index(int base)
{
    if (base < BVN_CURRENCY_FIRST || base > BVN_CURRENCY_LAST)
        return -1;
    return base - BVN_CURRENCY_FIRST;
}
/*
 * "Is this base a currency" is a bounds check over the block — no table access.
 * Fiat vs crypto is not: it reads the catalogue row, so that the split is a
 * property of the currency rather than of where it happens to sit.
 */
bool bvn_unit_is_currency(int base)
{
    return bvn_currency_index(base) >= 0;
}
bool bvn_unit_is_fiat(int base)
{
    int idx = bvn_currency_index(base);
    return idx >= 0 && !g_currency_table[idx].is_crypto;
}
bool bvn_unit_is_crypto(int base)
{
    int idx = bvn_currency_index(base);
    return idx >= 0 && g_currency_table[idx].is_crypto;
}
uint8_t bvn_currency_minor_unit(int base, bool *ok)
{
    int idx = bvn_currency_index(base);
    if (idx < 0) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return g_currency_table[idx].minor_unit;
}
const bvn_currency_info_t *bvn_currency_info(int base)
{
    int idx = bvn_currency_index(base);
    if (idx < 0)
        return NULL;
    return &g_currency_table[idx];
}
/*
 * Resolve a 3-4 letter uppercase code (e.g. "USD", "USDT") to its base-unit id,
 * or 0 if unknown. Rejects non-uppercase input up front, then does a linear
 * scan of the catalogue; the result is the exact inverse of
 * bvn_currency_index(). 0 is a safe "not a currency" sentinel since it is
 * bu_none, which belongs to no block.
 */
int bvn_parse_currency_str(const uint8_t *s, uint32_t len)
{
    if (!s || len < 3 || len > 4)
        return 0;
    for (uint32_t i = 0; i < len; i++) {
        if (s[i] < 'A' || s[i] > 'Z')
            return 0;
    }
    for (int idx = 0; idx < N_TOTAL; idx++) {
        const char *code = g_currency_table[idx].code;
        if ((uint32_t)strlen(code) == len && memcmp(code, s, len) == 0)
            return BVN_CURRENCY_FIRST + idx;
    }
    return 0;
}
/*
 * Which prefix systems may attach to a currency. Binary IEC prefixes
 * (prefix_iec, e.g. "Ki") are nonsensical on money, so they are rejected; SI
 * prefixes (kEUR, etc.) are allowed. Non-currency bases are not this function's
 * concern and pass through.
 */
bool bvn_currency_prefix_valid(int base, int prefix_system)
{
    if (!bvn_unit_is_currency(base))
        return true;
    /* Test for the one system that IS allowed rather than against the one that
     * is not: `!= prefix_iec` waved through every out-of-range value, so
     * bvn_currency_prefix_valid(bu_usd, 2) said yes. Not reachable through the
     * parser (bvn_prefix_unit_valid range-checks first) but this is BVN_API and
     * a caller may pass anything. The enum has exactly two members, and an
     * unprefixed unit is carried as prefix_si with si_none, so prefix_si is the
     * whole of the accepted set. */
    return (prefix_system == prefix_si);
}
