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

#ifndef BOVNAR_CURRENCY_H
#define BOVNAR_CURRENCY_H
#include <stdbool.h>
#include <stdint.h>
#ifndef BVN_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define BVN_API  /* DLL symbols exported via CMake WINDOWS_EXPORT_ALL_SYMBOLS */
#  elif defined(__GNUC__) || defined(__clang__)
#    define BVN_API __attribute__((visibility("default")))
#  else
#    define BVN_API
#  endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
/*
 * The currency block of the base-unit id space (block 90; see the layout note
 * in bovnar.h). BVN_CURRENCY_FIRST/LAST/COUNT are generated from the catalogue
 * itself, so a bound can no longer be widened without a row behind it.
 *
 * The whole block is ONE ascending run, and a currency's catalogue index is its
 * id minus BVN_CURRENCY_FIRST. Which currencies are crypto is a column of the
 * catalogue rather than a sub-range of the ids: bvn_unit_is_fiat and
 * bvn_unit_is_crypto read that column. So a new currency of either kind appends
 * at the end of currencies.bvnr and renumbers nothing.
 */
#include "bovnar_currency.gen.h"
typedef struct {
    char     code[5];
    uint16_t numeric_code;
    uint8_t  minor_unit;
    bool     is_crypto;
    char     name[48];
} bvn_currency_info_t;
BVN_API bool bvn_unit_is_currency(int base);
BVN_API bool bvn_unit_is_fiat(int base);
BVN_API bool bvn_unit_is_crypto(int base);
BVN_API uint8_t bvn_currency_minor_unit(int base, bool *ok);
BVN_API const bvn_currency_info_t *bvn_currency_info(int base);
BVN_API int bvn_parse_currency_str(const uint8_t *s, uint32_t len);
BVN_API bool bvn_currency_prefix_valid(int base, int prefix_system);
#define BVN_UNIT_FIAT(e)   BVN_UNIT_NO_PREFIX(e)
#define BVN_UNIT_CRYPTO(e) BVN_UNIT_NO_PREFIX(e)
#ifdef __cplusplus
}
#endif
#endif
