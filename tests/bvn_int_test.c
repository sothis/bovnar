#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include "bvn_int.h"

#ifdef WITH_GMP
#  include <gmp.h>
#endif

static int g_fails = 0;
static int g_pass  = 0;

#define CHECK(cond, msg) do {                                    \
    if (!(cond)) {                                               \
        fprintf(stderr, "FAIL  line %d: %s\n", __LINE__, (msg));\
        g_fails++;                                               \
    } else {                                                     \
        g_pass++;                                                \
    }                                                            \
} while (0)

#define CHECKSTR(a, b) do {                                               \
    if (strcmp((a),(b)) != 0) {                                           \
        fprintf(stderr, "FAIL  line %d: got \"%s\", want \"%s\"\n",       \
                __LINE__, (a), (b));                                       \
        g_fails++;                                                         \
    } else {                                                               \
        g_pass++;                                                          \
    }                                                                      \
} while(0)

static char *round_trip(const char *s, uint32_t base)
{
    bvn_int_t *n = bvn_int_alloc();
    if (!n) return NULL;

    if (!bvn_int_from_str(n, s, base)) {
        bvn_int_free(n);
        return NULL;
    }

    size_t bufsz = bvn_int_str_bufsize(n->nused * 32u, base);
    char  *buf   = malloc(bufsz);
    if (!buf) { bvn_int_free(n); return NULL; }

    int32_t len = bvn_int_to_str(n, buf, bufsz, base);
    bvn_int_free(n);

    if (len < 0) { free(buf); return NULL; }
    return buf;
}

static void test_zero(void)
{
    puts("── zero ──────────────────────────────────────────────────────");

    char *r;

    r = round_trip("0", 10); CHECK(r && strcmp(r,"0")==0, "0 base10"); free(r);
    r = round_trip("+0", 10); CHECK(r && strcmp(r,"0")==0, "+0 base10"); free(r);
    r = round_trip("-0", 10); CHECK(r && strcmp(r,"0")==0, "-0 base10"); free(r);
    r = round_trip("0", 2);  CHECK(r && strcmp(r,"0")==0, "0 base2"); free(r);
    r = round_trip("0", 16); CHECK(r && strcmp(r,"0")==0, "0 base16"); free(r);

    r = round_trip("A", 64); CHECK(r && strcmp(r,"A")==0, "A base64 (digit 0 = value 0)"); free(r);
    r = round_trip("0", 64); CHECK(r && strcmp(r,"0")==0, "0 base64 (digit 52 roundtrip)"); free(r);

    bvn_int_t *n = bvn_int_alloc();
    CHECK(bvn_int_is_zero(n), "fresh alloc is zero");
    bvn_int_free(n);
}

static void test_small_values(void)
{
    puts("── small values ──────────────────────────────────────────────");

    char *r;

    r = round_trip("1", 10);    CHECK(r && strcmp(r,"1")==0,   "1 dec");    free(r);
    r = round_trip("255", 10);  CHECK(r && strcmp(r,"255")==0, "255 dec");  free(r);
    r = round_trip("ff", 16);   CHECK(r && strcmp(r,"ff")==0,  "ff hex");   free(r);
    r = round_trip("FF", 16);   CHECK(r && strcmp(r,"ff")==0,  "FF→ff hex");free(r);
    r = round_trip("11111111", 2); CHECK(r && strcmp(r,"11111111")==0, "0xff base2"); free(r);
    r = round_trip("-1", 10);   CHECK(r && strcmp(r,"-1")==0,  "-1 dec");   free(r);
    r = round_trip("-ff", 16);  CHECK(r && strcmp(r,"-ff")==0, "-ff hex");  free(r);

    r = round_trip("18446744073709551615", 10);
    CHECK(r && strcmp(r,"18446744073709551615")==0, "UINT64_MAX dec"); free(r);

    r = round_trip("-9223372036854775808", 10);
    CHECK(r && strcmp(r,"-9223372036854775808")==0, "INT64_MIN dec"); free(r);
}

static void test_native_getters(void)
{
    puts("── native getters ────────────────────────────────────────────");

    bvn_int_t *n = bvn_int_alloc();

    CHECK(bvn_int_from_str(n, "255", 10), "from_str 255");
    uint8_t u8 = 0;
    CHECK(bvn_int_to_uint8(n, &u8) && u8 == 255, "to_uint8 255");
    CHECK(bvn_int_from_str(n, "256", 10), "from_str 256");
    CHECK(!bvn_int_to_uint8(n, &u8), "to_uint8 256 must fail");

    CHECK(bvn_int_from_str(n, "-128", 10), "from_str -128");
    int8_t i8 = 0;
    CHECK(bvn_int_to_int8(n, &i8) && i8 == -128, "to_int8 INT8_MIN");
    CHECK(bvn_int_from_str(n, "-129", 10), "from_str -129");
    CHECK(!bvn_int_to_int8(n, &i8), "to_int8 -129 must fail");

    CHECK(bvn_int_from_str(n, "65535", 10), "from_str 65535");
    uint16_t u16 = 0;
    CHECK(bvn_int_to_uint16(n, &u16) && u16 == 65535u, "to_uint16 UINT16_MAX");

    CHECK(bvn_int_from_str(n, "-2147483648", 10), "from_str INT32_MIN");
    int32_t i32 = 0;
    CHECK(bvn_int_to_int32(n, &i32) && i32 == INT32_MIN, "to_int32 INT32_MIN");

    CHECK(bvn_int_from_str(n, "18446744073709551615", 10), "from_str UINT64_MAX");
    uint64_t u64 = 0;
    CHECK(bvn_int_to_uint64(n, &u64) && u64 == UINT64_MAX, "to_uint64 UINT64_MAX");

    CHECK(bvn_int_from_str(n, "-1", 10), "from_str -1");
    CHECK(!bvn_int_to_uint64(n, &u64), "to_uint64(-1) must fail");

    CHECK(bvn_int_from_str(n, "-9223372036854775808", 10), "from_str INT64_MIN");
    int64_t i64 = 0;
    CHECK(bvn_int_to_int64(n, &i64) && i64 == INT64_MIN, "to_int64 INT64_MIN");

    bvn_int_free(n);
}

static void test_native_setters(void)
{
    puts("── native setters ────────────────────────────────────────────");

    bvn_int_t *n = bvn_int_alloc();
    char buf[128];

    CHECK(bvn_int_from_int8(n, -1), "from_int8 -1");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, "-1") == 0, "setter int8 -1");

    CHECK(bvn_int_from_uint8(n, 255), "from_uint8 255");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, "255") == 0, "setter uint8 255");

    CHECK(bvn_int_from_int64(n, INT64_MIN), "from_int64 INT64_MIN");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, "-9223372036854775808") == 0, "setter int64 INT64_MIN");

    CHECK(bvn_int_from_uint64(n, UINT64_MAX), "from_uint64 UINT64_MAX");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, "18446744073709551615") == 0, "setter uint64 UINT64_MAX");

    bvn_int_free(n);
}

static void test_bases(void)
{
    puts("── multi-base round-trip for 0xDEADBEEF ──────────────────────");

    const char *dec = "3735928559";
    char *r;

    r = round_trip("deadbeef", 16);
    CHECK(r && strcmp(r,"deadbeef")==0, "deadbeef hex rt"); free(r);

    bvn_int_t *n = bvn_int_alloc();
    CHECK(bvn_int_from_str(n, "deadbeef", 16), "from hex");

    char buf[64];
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, dec) == 0, "deadbeef hex→dec");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 2) > 0
          && strcmp(buf, "11011110101011011011111011101111") == 0, "deadbeef→bin");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 8) > 0
          && strcmp(buf, "33653337357") == 0, "deadbeef→oct");

    bvn_int_free(n);

    r = round_trip("Z9", 62); CHECK(r != NULL, "base62 round-trip non-null"); free(r);
}

static void test_wide(void)
{
    puts("── wide integers (> 64 bits) ─────────────────────────────────");

    const char *u128max = "340282366920938463463374607431768211455";

    bvn_int_t *n = bvn_int_alloc();
    CHECK(bvn_int_from_str(n, u128max, 10), "parse 2^128-1");

    char buf[200];
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, u128max) == 0, "2^128-1 rt dec");

    const char *u128hex = "ffffffffffffffffffffffffffffffff";
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 16) > 0
          && strcmp(buf, u128hex) == 0, "2^128-1 hex");

    char neg[200];
    neg[0] = '-';
    memcpy(neg + 1, u128max, strlen(u128max) + 1);
    CHECK(bvn_int_from_str(n, neg, 10), "parse -(2^128-1)");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 10) > 0
          && strcmp(buf, neg) == 0, "-(2^128-1) rt");

    const char *p256 =
        "10000000000000000000000000000000"
        "00000000000000000000000000000000";
    CHECK(bvn_int_from_str(n, p256, 16), "parse 2^256 hex");
    CHECK(bvn_int_to_str(n, buf, sizeof(buf), 16) > 0
          && strcmp(buf, p256) == 0, "2^256 hex rt");

    bvn_int_free(n);
}

static void test_invalid_input(void)
{
    puts("── invalid input rejection ───────────────────────────────────");

    bvn_int_t *n = bvn_int_alloc();

    CHECK(!bvn_int_from_str(n, "",    10), "empty string");
    CHECK(!bvn_int_from_str(n, "-",   10), "bare minus");
    CHECK(!bvn_int_from_str(n, "+",   10), "bare plus");
    CHECK(!bvn_int_from_str(n, "1g",  16), "invalid hex digit g");
    CHECK(!bvn_int_from_str(n, "2",    2), "digit 2 in base 2");
    CHECK(!bvn_int_from_str(n, NULL,  10), "NULL string");
    CHECK(!bvn_int_from_str(n, "0",    1), "base 1 invalid");
    CHECK(!bvn_int_from_str(n, "0",   63), "base 63 invalid");
    CHECK(!bvn_int_from_str(n, "0",   66), "base 66 invalid");

    bvn_int_free(n);
}

static void test_bufsize(void)
{
    puts("── bvn_int_str_bufsize ────────────────────────────────────");

    bvn_int_t *n  = bvn_int_alloc();
    const char *u128 = "340282366920938463463374607431768211455";

    CHECK(bvn_int_from_str(n, u128, 10), "parse u128 for bufsize test");

    size_t sz = bvn_int_str_bufsize(128u, 10u);
    char  *buf = malloc(sz);
    CHECK(buf != NULL, "malloc for bufsize test");
    int32_t r = bvn_int_to_str(n, buf, sz, 10);
    CHECK(r > 0 && strcmp(buf, u128) == 0, "bufsize fits u128 base10");
    free(buf);

    sz  = bvn_int_str_bufsize(128u, 2u);
    buf = malloc(sz);
    CHECK(buf != NULL, "malloc for bufsize base2 test");
    r   = bvn_int_to_str(n, buf, sz, 2);
    CHECK(r > 0, "bufsize fits u128 base2");
    free(buf);

    bvn_int_free(n);
}

#ifdef WITH_GMP
static void test_gmp_roundtrip(void)
{
    puts("── GMP round-trip compatibility ──────────────────────────────");

    static const struct { const char *s; int base; } cases[] = {
        { "0",                    10 },
        { "-1",                   10 },
        { "18446744073709551615", 10 },
        { "-9223372036854775808", 10 },
        { "340282366920938463463374607431768211455", 10 },
        { "ffffffffffffffffffffffffffffffff", 16 },
        { "deadbeef", 16 },
        { "-deadbeef", 16 },
        { "z", 36 },
        { "zzzzzzzzzzzzzzzzzzzzzzzz", 36 },
    };

    bvn_int_t *n = bvn_int_alloc();
    mpz_t gmp;
    mpz_init(gmp);

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        const char *s    = cases[i].s;
        int         base = cases[i].base;

        CHECK(bvn_int_from_str(n, s, (uint32_t)base), "gmp_rt: bvn parse");
        size_t bsz = bvn_int_str_bufsize(n->nused * 32u, (uint32_t)base);
        char  *bvn_str = malloc(bsz);
        CHECK(bvn_str != NULL, "gmp_rt: malloc");
        int32_t blen = bvn_int_to_str(n, bvn_str, bsz, (uint32_t)base);
        CHECK(blen >= 0, "gmp_rt: bvn to_str");

        CHECK(mpz_set_str(gmp, bvn_str, base) == 0, "gmp_rt: mpz_set_str");
        char *gmp_str = mpz_get_str(NULL, base, gmp);
        CHECKSTR(bvn_str, gmp_str);

        free(bvn_str);
        free(gmp_str);
    }

    mpz_clear(gmp);
    bvn_int_free(n);
}
#endif

static void test_32768_bit(void)
{
    puts("── 32768-bit round-trip in base 16 ──────────────────────────");

    const size_t hex_digits = BVN_INT_MAX_BITS / 4u;
    char *input = malloc(hex_digits + 1u);
    CHECK(input != NULL, "32768-bit alloc input");
    if (!input) return;
    memset(input, 'f', hex_digits);
    input[hex_digits] = '\0';

    bvn_int_t *n = bvn_int_alloc();
    CHECK(bvn_int_from_str(n, input, 16u), "32768-bit from_str");

    CHECK(n->nused == BVN_INT_MAX_LIMBS, "32768-bit nused == 1024");

    size_t bufsz = bvn_int_str_bufsize(BVN_INT_MAX_BITS, 16u);
    char  *out   = malloc(bufsz);
    CHECK(out != NULL, "32768-bit alloc output");

    if (out) {
        int32_t r = bvn_int_to_str(n, out, bufsz, 16u);
        CHECK(r == (int32_t)hex_digits, "32768-bit to_str length");
        CHECK(memcmp(out, input, hex_digits) == 0, "32768-bit round-trip");
        free(out);
    }

    bvn_int_free(n);
    free(input);
}

int main(void)
{
    test_zero();
    test_small_values();
    test_native_getters();
    test_native_setters();
    test_bases();
    test_wide();
    test_invalid_input();
    test_bufsize();
    test_32768_bit();

#ifdef WITH_GMP
    test_gmp_roundtrip();
#endif

    printf("\n%s  %d passed, %d failed\n",
           g_fails ? "FAIL" : "PASS", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
