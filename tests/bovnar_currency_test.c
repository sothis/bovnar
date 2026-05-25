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
    ASSERT_EQ_INT(BVN_CURRENCY_FIAT_FIRST,    134, "FIAT_FIRST == 134");
    ASSERT_EQ_INT(BVN_CURRENCY_FIAT_LAST,     297, "FIAT_LAST  == 297");
    ASSERT_EQ_INT(BVN_CURRENCY_CRYPTO_FIRST,  298, "CRYPTO_FIRST == 298");
    ASSERT_EQ_INT(BVN_CURRENCY_CRYPTO_LAST,   347, "CRYPTO_LAST == 347");
    ASSERT_EQ_INT(BVN_VALUE_BASE_UNIT_COUNT_CURRENCY, 348, "TOTAL == 348");
    ASSERT_EQ_INT(BVN_CURRENCY_FIAT_LAST - BVN_CURRENCY_FIAT_FIRST + 1, 164, "164 fiat entries");
    ASSERT_EQ_INT(BVN_CURRENCY_CRYPTO_LAST - BVN_CURRENCY_CRYPTO_FIRST + 1, 50, "50 crypto entries");
    ASSERT_EQ_INT(BVN_CURRENCY_CRYPTO_FIRST, BVN_CURRENCY_FIAT_LAST + 1, "no gap fiat/crypto");
}

static void test_predicates(void)
{
    printf("  is_currency / is_fiat / is_crypto...\n");
    ASSERT_TRUE( bvn_unit_is_currency(BVN_CURRENCY_FIAT_FIRST),   "FIAT_FIRST is currency");
    ASSERT_TRUE( bvn_unit_is_currency(BVN_CURRENCY_FIAT_LAST),    "FIAT_LAST is currency");
    ASSERT_TRUE( bvn_unit_is_currency(BVN_CURRENCY_CRYPTO_FIRST), "CRYPTO_FIRST is currency");
    ASSERT_TRUE( bvn_unit_is_currency(BVN_CURRENCY_CRYPTO_LAST),  "CRYPTO_LAST is currency");
    ASSERT_TRUE(!bvn_unit_is_currency(0),       "bu_none not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(bu_meter),"bu_meter not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(bu_cup),  "bu_cup (volume) not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(BVN_CURRENCY_FIAT_FIRST - 1), "below range not currency");
    ASSERT_TRUE(!bvn_unit_is_currency(BVN_VALUE_BASE_UNIT_COUNT_CURRENCY), "sentinel not currency");
    ASSERT_TRUE( bvn_unit_is_fiat(BVN_CURRENCY_FIAT_FIRST),    "FIAT_FIRST is fiat");
    ASSERT_TRUE( bvn_unit_is_fiat(BVN_CURRENCY_FIAT_LAST),     "FIAT_LAST is fiat");
    ASSERT_TRUE(!bvn_unit_is_fiat(BVN_CURRENCY_CRYPTO_FIRST),  "CRYPTO_FIRST not fiat");
    ASSERT_TRUE(!bvn_unit_is_fiat(bu_meter),                    "bu_meter not fiat");
    ASSERT_TRUE( bvn_unit_is_crypto(BVN_CURRENCY_CRYPTO_FIRST),"CRYPTO_FIRST is crypto");
    ASSERT_TRUE( bvn_unit_is_crypto(BVN_CURRENCY_CRYPTO_LAST), "CRYPTO_LAST is crypto");
    ASSERT_TRUE(!bvn_unit_is_crypto(BVN_CURRENCY_FIAT_LAST),   "FIAT_LAST not crypto");
    ASSERT_TRUE(!bvn_unit_is_crypto(bu_second),                 "bu_second not crypto");
}

static void test_parse_fiat(void)
{
    printf("  bvn_parse_currency_str: fiat...\n");
    ASSERT_EQ_INT(parse("AED"), 134, "AED==134");
    ASSERT_EQ_INT(parse("ZWL"), 297, "ZWL==297");
    ASSERT_EQ_INT(parse("USD"), 277, "USD==277");
    ASSERT_EQ_INT(parse("EUR"), 177, "EUR==177");
    ASSERT_EQ_INT(parse("GBP"), 180, "GBP==180");
    ASSERT_EQ_INT(parse("JPY"), 201, "JPY==201");
    ASSERT_EQ_INT(parse("CHF"), 161, "CHF==161");
    ASSERT_EQ_INT(parse("KWD"), 208, "KWD==208");
    ASSERT_EQ_INT(parse("BHD"), 148, "BHD==148");
    ASSERT_EQ_INT(parse("BIF"), 149, "BIF==149");
    ASSERT_EQ_INT(parse("OMR"), 237, "OMR==237");
    ASSERT_EQ_INT(parse("JOD"), 200, "JOD==200");
    ASSERT_EQ_INT(parse("TND"), 269, "TND==269");
    ASSERT_EQ_INT(parse("CLF"), 162, "CLF==162");
    ASSERT_EQ_INT(parse("XAU"), 286, "XAU==286");
    ASSERT_EQ_INT(parse("XDR"), 288, "XDR==288");
    ASSERT_EQ_INT(parse("CUP"), 167, "CUP (Cuban Peso)==167");
    ASSERT_EQ_INT(parse("VND"), 281, "VND==281");
    ASSERT_EQ_INT(parse("CLP"), 163, "CLP==163");
    ASSERT_EQ_INT(parse("SLE"), 257, "SLE==257");
    ASSERT_EQ_INT(parse("SSP"), 260, "SSP==260");
}

static void test_parse_crypto(void)
{
    printf("  bvn_parse_currency_str: crypto...\n");
    ASSERT_EQ_INT(parse("BTC"),  298, "BTC==298");
    ASSERT_EQ_INT(parse("ETH"),  299, "ETH==299");
    ASSERT_EQ_INT(parse("SOL"),  300, "SOL==300");
    ASSERT_EQ_INT(parse("XRP"),  301, "XRP==301");
    ASSERT_EQ_INT(parse("DOT"),  305, "DOT==305");
    ASSERT_EQ_INT(parse("XMR"),  306, "XMR==306");
    ASSERT_EQ_INT(parse("XLM"),  309, "XLM==309");
    ASSERT_EQ_INT(parse("DOGE"), 326, "DOGE==326");
    ASSERT_EQ_INT(parse("LINK"), 327, "LINK==327");
    ASSERT_EQ_INT(parse("USDT"), 328, "USDT==328");
    ASSERT_EQ_INT(parse("USDC"), 329, "USDC==329");
    ASSERT_EQ_INT(parse("AVAX"), 330, "AVAX==330");
    ASSERT_EQ_INT(parse("ATOM"), 331, "ATOM==331");
    ASSERT_EQ_INT(parse("POL"),  332, "POL==332");
    ASSERT_EQ_INT(parse("NEAR"), 333, "NEAR==333");
    ASSERT_EQ_INT(parse("RUNE"), 347, "RUNE==347");
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

    ASSERT_EQ_INT((int)parse("RUNE"), BVN_CURRENCY_CRYPTO_LAST, "RUNE==CRYPTO_LAST");

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
    printf("  contiguity: every fiat/crypto slot has a valid info record...\n");
    for (int b = BVN_CURRENCY_FIAT_FIRST; b <= BVN_CURRENCY_FIAT_LAST; b++) {
        const bvn_currency_info_t *ci = bvn_currency_info(b);
        tests++;
        if (!ci) { fprintf(stderr,"FAIL: info(%d)==NULL\n",b); failures++; continue; }
        tests++;
        if (ci->is_crypto)      { fprintf(stderr,"FAIL: fiat %d is_crypto\n",b); failures++; }
        tests++;
        if (ci->numeric_code==0){ fprintf(stderr,"FAIL: fiat %d numeric_code=0 (%s)\n",b,ci->code); failures++; }
    }
    for (int b = BVN_CURRENCY_CRYPTO_FIRST; b <= BVN_CURRENCY_CRYPTO_LAST; b++) {
        const bvn_currency_info_t *ci = bvn_currency_info(b);
        tests++;
        if (!ci) { fprintf(stderr,"FAIL: info(%d)==NULL\n",b); failures++; continue; }
        tests++;
        if (!ci->is_crypto)      { fprintf(stderr,"FAIL: crypto %d not is_crypto (%s)\n",b,ci->code); failures++; }
        tests++;
        if (ci->numeric_code!=0) { fprintf(stderr,"FAIL: crypto %d numeric_code=%u (%s)\n",b,ci->numeric_code,ci->code); failures++; }
    }
}

static void test_cup_collision(void)
{
    printf("  CUP collision guard: physical bu_cup != Cuban Peso...\n");
    ASSERT_TRUE((int)bu_cup == 81,        "bu_cup (volume) == 81");
    ASSERT_TRUE(!bvn_unit_is_currency((int)bu_cup), "bu_cup (volume) not currency");
    ASSERT_EQ_INT(parse("CUP"), 167,      "CUP string -> Cuban Peso (167)");
    ASSERT_TRUE(parse("CUP") != (int)bu_cup, "CUP currency != bu_cup physical");
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
    printf("\n--------------------------------------\n");
    printf("  Results: %d tests, %d failures\n", tests, failures);
    printf("--------------------------------------\n");
    return failures ? 1 : 0;
}
