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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bovnar.h"
#include "bovnar_currency.h"

static int failures = 0;
static int tests    = 0;

#define ASSERT_EQ_INT(a, b, msg) do {                                    \
    tests++;                                                              \
    int64_t _a = (int64_t)(a), _b = (int64_t)(b);                       \
    if (_a != _b) {                                                       \
        fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n", \
                __LINE__, (msg), (long long)_a, (long long)_b);          \
        failures++;                                                       \
    }                                                                     \
} while (0)

#define ASSERT_TRUE(cond, msg) do {                                       \
    tests++;                                                              \
    if (!(cond)) {                                                        \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));          \
        failures++;                                                       \
    }                                                                     \
} while (0)

#define ASSERT_STR(a, b, msg) do {                                        \
    tests++;                                                              \
    if (strcmp((a), (b)) != 0) {                                          \
        fprintf(stderr, "FAIL line %d: %s\n  got \"%s\", expected \"%s\"\n", \
                __LINE__, (msg), (a), (b));                               \
        failures++;                                                       \
    }                                                                     \
} while (0)

static int parse(const char *s)
{
    return bvn_parse_currency_str((const uint8_t *)s, (uint32_t)strlen(s));
}

static void test_enum_range_sentinels(void)
{
    printf("  enum range sentinels...\n");
    /* Block 90 of the base-unit id space: one contiguous run, and the bounds
     * come from the catalogue itself so they cannot claim more rows than exist. */
    ASSERT_EQ_INT(BVN_CURRENCY_FIRST, 900000, "currencies are block 90");
    ASSERT_EQ_INT(BVN_CURRENCY_COUNT, 216,    "216 currencies");
    ASSERT_EQ_INT(BVN_CURRENCY_LAST - BVN_CURRENCY_FIRST + 1,
                  BVN_CURRENCY_COUNT, "the run has no holes");
    ASSERT_TRUE(BVN_CURRENCY_COUNT <= BVN_UNIT_BLOCK_SIZE,
                "the catalogue fits inside its block");
    /* Nothing else may reach into the block — a currency id must not also name
     * a unit, which is what would happen if a block boundary were wrong. */
    ASSERT_TRUE(BVN_CURRENCY_FIRST > BVN_UNIT_NATIVE_LAST,
                "currencies sit above the native units");
    ASSERT_TRUE(BVN_CURRENCY_FIRST > BVN_PROFILE_UCUM_OPAQUE_LAST,
                "currencies sit above the profile blocks");
}

static void test_predicates(void)
{
    printf("  is_currency / is_fiat / is_crypto...\n");
    int usd = parse("USD"), btc = parse("BTC");
    ASSERT_TRUE( bvn_unit_is_currency(BVN_CURRENCY_FIRST),  "the first slot is a currency");
    ASSERT_TRUE( bvn_unit_is_currency(BVN_CURRENCY_LAST),   "the last slot is a currency");
    ASSERT_TRUE( bvn_unit_is_currency(usd),                 "USD is currency");
    ASSERT_TRUE( bvn_unit_is_currency(btc),                 "BTC is currency");
    ASSERT_TRUE(!bvn_unit_is_currency(0),       "bu_none not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(bu_meter),"bu_meter not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(bu_cup),  "bu_cup (volume) not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(BVN_CURRENCY_FIRST - 1), "below the block not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(BVN_CURRENCY_LAST + 1),  "the hole above the last row not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(-1),      "a negative id is not a currency");
    /* fiat vs crypto is read from the catalogue row, not from a sub-range, so
     * it holds wherever in the block a currency happens to sit. */
    ASSERT_TRUE( bvn_unit_is_fiat(usd),                    "USD is fiat");
    ASSERT_TRUE(!bvn_unit_is_fiat(btc),                    "BTC not fiat");
    ASSERT_TRUE(!bvn_unit_is_fiat(bu_meter),               "bu_meter not fiat");
    ASSERT_TRUE(!bvn_unit_is_fiat(BVN_CURRENCY_LAST + 1),  "a hole is not fiat");
    ASSERT_TRUE( bvn_unit_is_crypto(btc),                  "BTC is crypto");
    ASSERT_TRUE(!bvn_unit_is_crypto(usd),                  "USD not crypto");
    ASSERT_TRUE(!bvn_unit_is_crypto(bu_second),            "bu_second not crypto");
    ASSERT_TRUE(!bvn_unit_is_crypto(BVN_CURRENCY_LAST + 1),"a hole is not crypto");
    /* Every slot is exactly one of the two, and nothing outside the block is
     * either. That is the property the two predicates jointly promise. */
    for (int b = BVN_CURRENCY_FIRST; b <= BVN_CURRENCY_LAST; b++) {
        tests++;
        if (bvn_unit_is_fiat(b) == bvn_unit_is_crypto(b)) {
            fprintf(stderr, "FAIL: %d is neither or both fiat and crypto\n", b);
            failures++;
        }
    }
}

static void test_parse_fiat(void)
{
    printf("  bvn_parse_currency_str: fiat...\n");
    ASSERT_EQ_INT(parse("AED"), 900000, "AED==900000");
    ASSERT_EQ_INT(parse("ZWL"), 900165, "ZWL==900165");
    ASSERT_EQ_INT(parse("USD"), 900143, "USD==900143");
    ASSERT_EQ_INT(parse("EUR"), 900043, "EUR==900043");
    ASSERT_EQ_INT(parse("GBP"), 900046, "GBP==900046");
    ASSERT_EQ_INT(parse("JPY"), 900067, "JPY==900067");
    ASSERT_EQ_INT(parse("CHF"), 900027, "CHF==900027");
    ASSERT_EQ_INT(parse("KWD"), 900074, "KWD==900074");
    ASSERT_EQ_INT(parse("BHD"), 900014, "BHD==900014");
    ASSERT_EQ_INT(parse("BIF"), 900015, "BIF==900015");
    ASSERT_EQ_INT(parse("OMR"), 900103, "OMR==900103");
    ASSERT_EQ_INT(parse("JOD"), 900066, "JOD==900066");
    ASSERT_EQ_INT(parse("TND"), 900135, "TND==900135");
    ASSERT_EQ_INT(parse("CLF"), 900028, "CLF==900028");
    ASSERT_EQ_INT(parse("XAU"), 900152, "XAU==900152");
    ASSERT_EQ_INT(parse("XDR"), 900155, "XDR==900155");
    ASSERT_EQ_INT(parse("CUP"), 900033, "CUP (Cuban Peso)==900033");
    ASSERT_EQ_INT(parse("VND"), 900147, "VND==900147");
    ASSERT_EQ_INT(parse("CLP"), 900029, "CLP==900029");
    ASSERT_EQ_INT(parse("SLE"), 900123, "SLE==900123");
    ASSERT_EQ_INT(parse("SSP"), 900127, "SSP==900127");
}

static void test_parse_crypto(void)
{
    printf("  bvn_parse_currency_str: crypto...\n");
    ASSERT_EQ_INT(parse("BTC"), 900166, "BTC==900166");
    ASSERT_EQ_INT(parse("ETH"), 900167, "ETH==900167");
    ASSERT_EQ_INT(parse("SOL"), 900168, "SOL==900168");
    ASSERT_EQ_INT(parse("XRP"), 900169, "XRP==900169");
    ASSERT_EQ_INT(parse("DOT"), 900173, "DOT==900173");
    ASSERT_EQ_INT(parse("XMR"), 900174, "XMR==900174");
    ASSERT_EQ_INT(parse("XLM"), 900177, "XLM==900177");
    ASSERT_EQ_INT(parse("DOGE"), 900194, "DOGE==900194");
    ASSERT_EQ_INT(parse("LINK"), 900195, "LINK==900195");
    ASSERT_EQ_INT(parse("USDT"), 900196, "USDT==900196");
    ASSERT_EQ_INT(parse("USDC"), 900197, "USDC==900197");
    ASSERT_EQ_INT(parse("AVAX"), 900198, "AVAX==900198");
    ASSERT_EQ_INT(parse("ATOM"), 900199, "ATOM==900199");
    ASSERT_EQ_INT(parse("POL"), 900200, "POL==900200");
    ASSERT_EQ_INT(parse("NEAR"), 900201, "NEAR==900201");
    ASSERT_EQ_INT(parse("RUNE"), 900215, "RUNE==900215");
}

static void test_parse_rejects(void)
{
    printf("  bvn_parse_currency_str: rejects invalid tokens...\n");
    ASSERT_EQ_INT(parse(""),      0, "empty->0");
    ASSERT_EQ_INT(parse("AB"),    0, "2-char->0");
    ASSERT_EQ_INT(parse("ABCDE"),0, "5-char->0");
    ASSERT_EQ_INT(parse("usd"),  0, "lowercase->0");
    ASSERT_EQ_INT(parse("Usd"),  0, "mixed-case->0");
    ASSERT_EQ_INT(parse("XYZ"),  0, "unknown->0");
    ASSERT_EQ_INT(parse("123"),  0, "digits->0");
    ASSERT_EQ_INT(parse("LONG"), 0, "unknown 4-char->0");
}

static void test_minor_unit(void)
{
    printf("  bvn_currency_minor_unit...\n");
    bool ok;
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("USD"),&ok), 2, "USD minor=2"); ASSERT_TRUE(ok,"USD ok");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("JPY"),&ok), 0, "JPY minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("KRW"),&ok), 0, "KRW minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("VND"),&ok), 0, "VND minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("ISK"),&ok), 0, "ISK minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("DJF"),&ok), 0, "DJF minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("XAU"),&ok), 0, "XAU minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("XAF"),&ok), 0, "XAF minor=0");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("KWD"),&ok), 3, "KWD minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("BHD"),&ok), 3, "BHD minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("OMR"),&ok), 3, "OMR minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("JOD"),&ok), 3, "JOD minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("TND"),&ok), 3, "TND minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("IQD"),&ok), 3, "IQD minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("LYD"),&ok), 3, "LYD minor=3");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("CLF"),&ok), 4, "CLF minor=4");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("BTC"),&ok), 8, "BTC minor=8 (satoshi)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("ETH"),&ok),18, "ETH minor=18 (wei)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("SOL"),&ok), 9, "SOL minor=9 (lamport)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("XRP"),&ok), 6, "XRP minor=6 (drop)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("DOT"),&ok),10, "DOT minor=10 (planck)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("XMR"),&ok),12, "XMR minor=12 (piconero)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("XLM"),&ok), 7, "XLM minor=7 (stroop)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("EOS"),&ok), 4, "EOS minor=4");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("DOGE"),&ok),8, "DOGE minor=8 (koinu)");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("USDT"),&ok),6, "USDT minor=6");
    ASSERT_EQ_INT(bvn_currency_minor_unit((int)bu_meter,&ok),0, "bu_meter->0"); ASSERT_TRUE(!ok,"bu_meter !ok");
    ASSERT_EQ_INT(bvn_currency_minor_unit(0,&ok),            0, "bu_none->0");  ASSERT_TRUE(!ok,"bu_none !ok");
}

static void test_info_records(void)
{
    printf("  bvn_currency_info records...\n");
    const bvn_currency_info_t *ci;

    ci = bvn_currency_info(parse("USD"));
    ASSERT_TRUE(ci != NULL, "USD non-null");
    ASSERT_STR(ci->code, "USD", "USD code");
    ASSERT_EQ_INT(ci->numeric_code, 840, "USD numeric 840");
    ASSERT_EQ_INT(ci->minor_unit,   2,   "USD minor 2");
    ASSERT_TRUE(!ci->is_crypto,          "USD not crypto");

    ci = bvn_currency_info(parse("JPY"));
    ASSERT_TRUE(ci != NULL, "JPY non-null");
    ASSERT_EQ_INT(ci->numeric_code, 392, "JPY numeric 392");
    ASSERT_EQ_INT(ci->minor_unit,   0,   "JPY minor 0");

    ci = bvn_currency_info(parse("KWD"));
    ASSERT_EQ_INT(ci->numeric_code, 414, "KWD numeric 414");
    ASSERT_EQ_INT(ci->minor_unit,   3,   "KWD minor 3");

    ci = bvn_currency_info(parse("CLF"));
    ASSERT_EQ_INT(ci->numeric_code, 990, "CLF numeric 990");
    ASSERT_EQ_INT(ci->minor_unit,   4,   "CLF minor 4");

    ci = bvn_currency_info(parse("CUP"));
    ASSERT_TRUE(ci != NULL,       "CUP non-null");
    ASSERT_STR(ci->code, "CUP",  "CUP wire code is CUP");
    ASSERT_EQ_INT(ci->numeric_code, 192, "CUP numeric 192");
    ASSERT_TRUE(!ci->is_crypto,  "CUP not crypto");

    ci = bvn_currency_info(parse("BTC"));
    ASSERT_TRUE(ci != NULL,      "BTC non-null");
    ASSERT_STR(ci->code, "BTC", "BTC code");
    ASSERT_EQ_INT(ci->numeric_code, 0, "BTC numeric 0");
    ASSERT_EQ_INT(ci->minor_unit,   8, "BTC minor 8");
    ASSERT_TRUE(ci->is_crypto,       "BTC is_crypto");

    ci = bvn_currency_info(parse("DOGE"));
    ASSERT_TRUE(ci != NULL,       "DOGE non-null");
    ASSERT_STR(ci->code, "DOGE", "DOGE 4-char code");
    ASSERT_TRUE(ci->is_crypto,    "DOGE is_crypto");

    ASSERT_EQ_INT((int)parse("RUNE"), BVN_CURRENCY_LAST, "RUNE is the last catalogue row");

    ASSERT_TRUE(bvn_currency_info((int)bu_meter) == NULL, "bu_meter info NULL");
    ASSERT_TRUE(bvn_currency_info(0)             == NULL, "bu_none info NULL");
    ASSERT_TRUE(bvn_currency_info(-1)            == NULL, "negative info NULL");
}

static void test_prefix_valid(void)
{
    printf("  bvn_currency_prefix_valid...\n");
    int usd  = parse("USD");
    int btc  = parse("BTC");
    int doge = parse("DOGE");
    ASSERT_TRUE( bvn_currency_prefix_valid(usd,  0), "SI prefix USD valid");
    ASSERT_TRUE( bvn_currency_prefix_valid(btc,  0), "SI prefix BTC valid");
    ASSERT_TRUE( bvn_currency_prefix_valid(doge, 0), "SI prefix DOGE valid");
    ASSERT_TRUE(!bvn_currency_prefix_valid(usd,  1), "IEC prefix USD invalid");
    ASSERT_TRUE(!bvn_currency_prefix_valid(btc,  1), "IEC prefix BTC invalid");
    ASSERT_TRUE(!bvn_currency_prefix_valid(doge, 1), "IEC prefix DOGE invalid");
    ASSERT_TRUE(bvn_currency_prefix_valid((int)bu_meter, 1), "IEC on bu_meter not blocked");
    ASSERT_TRUE(bvn_currency_prefix_valid(0, 1),              "IEC on bu_none not blocked");
}

static void test_contiguity(void)
{
    printf("  contiguity: every slot has a valid info record...\n");
    for (int b = BVN_CURRENCY_FIRST; b <= BVN_CURRENCY_LAST; b++) {
        const bvn_currency_info_t *ci = bvn_currency_info(b);
        tests++;
        if (!ci) { fprintf(stderr,"FAIL: info(%d)==NULL\n",b); failures++; continue; }
        tests++;
        if (ci->code[0] == '\0') { fprintf(stderr,"FAIL: %d has an empty code\n",b); failures++; }
        /* An ISO numeric code identifies a fiat currency and no crypto asset
         * has one, so the column and the numeric field must agree. */
        tests++;
        if (ci->is_crypto ? ci->numeric_code != 0 : ci->numeric_code == 0) {
            fprintf(stderr,"FAIL: %d (%s) numeric_code=%u disagrees with is_crypto=%d\n",
                    b, ci->code, ci->numeric_code, (int)ci->is_crypto);
            failures++;
        }
        /* And the id round-trips through the code, which is the only handle a
         * document ever has on a currency. */
        tests++;
        if (parse(ci->code) != b) {
            fprintf(stderr,"FAIL: %s parses to %d, not %d\n", ci->code, parse(ci->code), b);
            failures++;
        }
    }
}

static void test_cup_collision(void)
{
    printf("  CUP collision guard: physical bu_cup != Cuban Peso...\n");
    ASSERT_TRUE((int)bu_cup == 100080,    "bu_cup (volume) == 100080");
    ASSERT_TRUE(!bvn_unit_is_currency((int)bu_cup), "bu_cup (volume) not currency");
    ASSERT_EQ_INT(parse("CUP"), 900033, "CUP string -> Cuban Peso (900033)");
    ASSERT_TRUE(parse("CUP") != (int)bu_cup, "CUP currency != bu_cup physical");
    /* The two live in different blocks, which is what makes the collision
     * structurally impossible rather than merely absent today. */
    ASSERT_TRUE(parse("CUP") - (int)bu_cup > BVN_UNIT_BLOCK_SIZE,
                "the volume and the peso are blocks apart");
}

static void test_late_currencies(void)
{
    printf("  currencies added after the first release (ZWG/XCG)...\n");
    /* These two used to be an "extension segment" stranded outside the
     * currency range, because that range had been frozen with no room to grow.
     * The block has 10000 ids, so they are ordinary rows now — the point of
     * this test is that nothing about them is special any more. */
    ASSERT_EQ_INT(parse("ZWG"), 900164, "ZWG is an ordinary catalogue row");
    ASSERT_EQ_INT(parse("XCG"), 900154, "XCG is an ordinary catalogue row");
    ASSERT_TRUE( bvn_unit_is_currency(parse("ZWG")), "ZWG is currency");
    ASSERT_TRUE( bvn_unit_is_currency(parse("XCG")), "XCG is currency");
    ASSERT_TRUE( bvn_unit_is_fiat(parse("ZWG")),     "ZWG is fiat");
    ASSERT_TRUE( bvn_unit_is_fiat(parse("XCG")),     "XCG is fiat");
    ASSERT_TRUE(!bvn_unit_is_crypto(parse("ZWG")),   "ZWG not crypto");
    ASSERT_TRUE(!bvn_unit_is_crypto(parse("XCG")),   "XCG not crypto");
    /* No unit is ever currency space, whichever block it is in. */
    ASSERT_TRUE(!bvn_unit_is_currency((int)bu_pfund), "bu_pfund not currency");
    ASSERT_TRUE(!bvn_unit_is_currency((int)bu_ppb),   "bu_ppb not currency");
    ASSERT_TRUE(!bvn_unit_is_currency((int)bu_ucum_iu), "bu_ucum_iu not currency");

    bool ok;
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("ZWG"),&ok), 2, "ZWG minor=2"); ASSERT_TRUE(ok,"ZWG ok");
    ASSERT_EQ_INT(bvn_currency_minor_unit(parse("XCG"),&ok), 2, "XCG minor=2"); ASSERT_TRUE(ok,"XCG ok");

    const bvn_currency_info_t *ci = bvn_currency_info(parse("ZWG"));
    ASSERT_TRUE(ci != NULL,                "ZWG non-null");
    ASSERT_STR(ci->code, "ZWG",            "ZWG code");
    ASSERT_EQ_INT(ci->numeric_code, 924,   "ZWG numeric 924");
    ASSERT_TRUE(!ci->is_crypto,            "ZWG not crypto");

    ci = bvn_currency_info(parse("XCG"));
    ASSERT_TRUE(ci != NULL,                "XCG non-null");
    ASSERT_STR(ci->code, "XCG",            "XCG code");
    ASSERT_EQ_INT(ci->numeric_code, 532,   "XCG numeric 532 (inherited from ANG)");
    ASSERT_TRUE(!ci->is_crypto,            "XCG not crypto");

    /* SI prefixes allowed on the new fiat; IEC prefixes rejected like all money. */
    ASSERT_TRUE( bvn_currency_prefix_valid(parse("ZWG"), 0), "SI prefix ZWG valid");
    ASSERT_TRUE(!bvn_currency_prefix_valid(parse("XCG"), 1), "IEC prefix XCG invalid");
}

int main(void)
{
    printf("bovnar_currency_test\n");
    printf("--------------------------------------\n");
    test_enum_range_sentinels();
    test_predicates();
    test_parse_fiat();
    test_parse_crypto();
    test_parse_rejects();
    test_minor_unit();
    test_info_records();
    test_prefix_valid();
    test_contiguity();
    test_cup_collision();
    test_late_currencies();
    printf("\n--------------------------------------\n");
    printf("  Results: %d tests, %d failures\n", tests, failures);
    printf("--------------------------------------\n");
    return failures ? 1 : 0;
}
