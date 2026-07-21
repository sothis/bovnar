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
#define N_FIAT   (BVN_CURRENCY_FIAT_LAST  - BVN_CURRENCY_FIAT_FIRST  + 1)
#define N_CRYPTO (BVN_CURRENCY_CRYPTO_LAST - BVN_CURRENCY_CRYPTO_FIRST + 1)
#define N_MAIN   (N_FIAT + N_CRYPTO)
#define N_EXT    (BVN_CURRENCY_EXT_LAST    - BVN_CURRENCY_EXT_FIRST   + 1)
#define N_TOTAL  (N_MAIN + N_EXT)
/*
 * ===========================================================================
 * Currency catalogue
 * ===========================================================================
 *
 * bovnar treats currencies as base units, so a money value carries its currency
 * the way a length carries metres. This table is the catalogue: ISO 4217 fiat
 * currencies first, then a list of crypto assets, then an appended "extension"
 * segment for currencies added after the main block was frozen. CRITICAL
 * INVARIANT: for the first N_MAIN entries a currency's value_base_unit_t enum
 * value equals BVN_CURRENCY_FIAT_FIRST + its index here, so a lookup is a direct
 * O(1) index (base - BVN_CURRENCY_FIAT_FIRST). The trailing N_EXT entries break
 * that 1:1 relation (their enum values are BVN_CURRENCY_EXT_FIRST..,placed past
 * the unit block for ABI stability), so all index math goes through
 * bvn_currency_index() below. The ordering of this array must stay in lockstep
 * with the enum within each segment. Each entry records the ISO numeric code (0
 * for crypto), the minor-unit digit count (e.g. 2 for cents, 0 for yen, 18 for
 * ether), whether it is crypto, and a human name.
 *
 * minor_unit is carried for APPLICATIONS to read via bvn_currency_minor_unit();
 * nothing inside the library consumes it. In particular the validator does not
 * check a value's decimal scale against it -- <float_dec:64,$JPY> 1.5555 is
 * accepted even though JPY has no minor units -- because the format stores what
 * the writer wrote and money precision is the application's policy.
 */
static const bvn_currency_info_t g_currency_table[N_TOTAL] = {
  /*
   * ISO 4217 fiat, then crypto assets, then the appended extension segment
   * — generated from src/gendata/currencies.bvnr by gen_currencies.py.
   * Positional: a main-block currency's enum value is FIAT_FIRST + its index
   * here; the extension rows follow, reached via bvn_currency_index().
   */
#include "bovnar_currency_table.gen.inc"
};
/* The table is generated; the ranges are hand-written in the header. Nothing
 * connected the two, so widening a range without adding a row compiled cleanly
 * and exposed the extra slot as a currency with an empty code and name (caught
 * only at test time). The generator now emits its actual row counts. */
/* Negative array length = hard error, the same C99-clean idiom the io impl uses
 * for its size guards (_Static_assert is C11 and this builds as -std=c99). */
typedef char bvn_currency_rows_fiat_check_[
	BVNR_CURRENCY_ROWS_FIAT   == N_FIAT   ? 1 : -1];
typedef char bvn_currency_rows_crypto_check_[
	BVNR_CURRENCY_ROWS_CRYPTO == N_CRYPTO ? 1 : -1];
typedef char bvn_currency_rows_ext_check_[
	BVNR_CURRENCY_ROWS_EXT    == N_EXT    ? 1 : -1];
/*
 * Range predicates over the currency enum block. Fiat and crypto occupy
 * contiguous enum ranges, and the extension segment (EXT_FIRST..EXT_LAST, all
 * fiat) is a second small range past the unit block; "is this base a currency /
 * fiat / crypto" is therefore a bounds check — no table access needed.
 */
bool bvn_unit_is_currency(int base)
{
    return (base >= BVN_CURRENCY_FIAT_FIRST && base <= BVN_CURRENCY_CRYPTO_LAST) ||
           (base >= BVN_CURRENCY_EXT_FIRST  && base <= BVN_CURRENCY_EXT_LAST);
}
bool bvn_unit_is_fiat(int base)
{
    return (base >= BVN_CURRENCY_FIAT_FIRST && base <= BVN_CURRENCY_FIAT_LAST) ||
           (base >= BVN_CURRENCY_EXT_FIRST  && base <= BVN_CURRENCY_EXT_LAST);
}
bool bvn_unit_is_crypto(int base)
{
    return (base >= BVN_CURRENCY_CRYPTO_FIRST && base <= BVN_CURRENCY_CRYPTO_LAST);
}
/*
 * Map a currency base-unit enum value to its g_currency_table index. The main
 * block (134-347) indexes directly; the extension segment maps onto the table
 * slots that follow it. Returns -1 if base is not a currency.
 */
static int bvn_currency_index(int base)
{
    if (base >= BVN_CURRENCY_FIAT_FIRST && base <= BVN_CURRENCY_CRYPTO_LAST)
        return base - BVN_CURRENCY_FIAT_FIRST;
    if (base >= BVN_CURRENCY_EXT_FIRST && base <= BVN_CURRENCY_EXT_LAST)
        return N_MAIN + (base - BVN_CURRENCY_EXT_FIRST);
    return -1;
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
 * Resolve a 3-4 letter uppercase code (e.g. "USD", "USDT") to its base-unit
 * enum value, or 0 if unknown. Rejects non-uppercase input up front, then does
 * a linear scan of the catalogue. Returns the enum value, the inverse of the
 * indexing used by the info lookups above: main-block slots map back to
 * index + BVN_CURRENCY_FIAT_FIRST, extension slots to BVN_CURRENCY_EXT_FIRST +
 * their offset. 0 is a safe "not a currency" sentinel since it is bu_none.
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
        if ((uint32_t)strlen(code) == len && memcmp(code, s, len) == 0) {
            if (idx < N_MAIN)
                return idx + BVN_CURRENCY_FIAT_FIRST;
            return BVN_CURRENCY_EXT_FIRST + (idx - N_MAIN);
        }
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
