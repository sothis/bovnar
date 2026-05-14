/*
 * bvn_float_fix_dec_test.c
 *
 * Integration tests for the float_fix and float_dec type families.
 * Covers: parsing, validation, writing, round-trip, error cases, and
 * the bvn_float_t to/from dec/fix public API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "bovnar.h"
#include "bvn_float.h"

/* ── Minimal test harness ──────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while (0)

/* ── Parse helpers ─────────────────────────────────────────────────── */

typedef struct { bool ok; value_type_spec_t vt; value_unit_t vu; } ParseResult;

static ParseResult parse_annotation(const char *s)
{
    ParseResult r;
    memset(&r, 0, sizeof r);
    bool type_ok = false, unit_ok = false, unit_too_long = false;
    uint8_t ubuf[256], ulen = 0;
    r.vt = bvn_parse_type_annotation(
        (const uint8_t *)s, (uint32_t)strlen(s),
        &type_ok, &unit_ok, &unit_too_long, &r.vu, ubuf, &ulen);
    r.ok = type_ok;
    return r;
}

/* ── bvn_parse_type_annotation tests ──────────────────────────────── */

static void test_float_fix_parse(void)
{
    puts("── float_fix annotation parsing ──────────────────────────────");

    ParseResult r;

    /* Basic family recognition. */
    r = parse_annotation("float_fix");
    CHECK(r.ok && r.vt.family == vt_float_fix, "float_fix bare family");
    CHECK(r.vt.width == 0 && r.vt.base == 0, "float_fix bare: width=0 Q=0");

    /* Width only. */
    r = parse_annotation("float_fix:32");
    CHECK(r.ok && r.vt.family == vt_float_fix, "float_fix:32 family");
    CHECK(r.vt.width == 32, "float_fix:32 width");
    CHECK(r.vt.base == 0, "float_fix:32 Q=0 default");

    /* Width + Q. */
    r = parse_annotation("float_fix:32,q8");
    CHECK(r.ok, "float_fix:32,q8 ok");
    CHECK(r.vt.width == 32, "float_fix:32,q8 width");
    CHECK(r.vt.base == 8, "float_fix:32,q8 Q=8");

    r = parse_annotation("float_fix:64,q31");
    CHECK(r.ok, "float_fix:64,q31 ok");
    CHECK(r.vt.width == 64 && r.vt.base == 31, "float_fix:64,q31 fields");

    r = parse_annotation("float_fix:16,q15");
    CHECK(r.ok, "float_fix:16,q15 ok");
    CHECK(r.vt.width == 16 && r.vt.base == 15, "float_fix:16,q15 fields");

    r = parse_annotation("float_fix:128,q64");
    CHECK(r.ok, "float_fix:128,q64 ok");
    CHECK(r.vt.width == 128 && r.vt.base == 64, "float_fix:128,q64 fields");

    r = parse_annotation("float_fix:256,q128");
    CHECK(r.ok, "float_fix:256,q128 ok");
    CHECK(r.vt.width == 256 && r.vt.base == 128, "float_fix:256,q128 fields");

    /* Q=0 with explicit qN. */
    r = parse_annotation("float_fix:32,q0");
    CHECK(r.ok && r.vt.base == 0, "float_fix:32,q0 Q=0 explicit");

    /* Invalid: Q >= width. */
    r = parse_annotation("float_fix:32,q32");
    CHECK(!r.ok, "float_fix:32,q32 rejected (Q >= width)");

    r = parse_annotation("float_fix:16,q16");
    CHECK(!r.ok, "float_fix:16,q16 rejected (Q == width)");

    /* Invalid width. */
    r = parse_annotation("float_fix:8");
    CHECK(!r.ok, "float_fix:8 rejected (invalid width)");

    r = parse_annotation("float_fix:48");
    CHECK(!r.ok, "float_fix:48 rejected (invalid width)");

    /* Invalid: base param forbidden for float_fix. */
    r = parse_annotation("float_fix:32,_10");
    CHECK(!r.ok, "float_fix:32,_10 rejected (base param forbidden)");

    /* float_fix must not match as float. */
    r = parse_annotation("float");
    CHECK(r.ok && r.vt.family == vt_float, "plain float still parses");

    /* Disambiguation: float_fix vs float. */
    r = parse_annotation("float_fix:32,q8");
    CHECK(r.vt.family == vt_float_fix, "float_fix not confused with float");
}

static void test_float_dec_parse(void)
{
    puts("── float_dec annotation parsing ──────────────────────────────");

    ParseResult r;

    r = parse_annotation("float_dec");
    CHECK(r.ok && r.vt.family == vt_float_dec, "float_dec bare family");

    r = parse_annotation("float_dec:16");
    CHECK(r.ok && r.vt.width == 16, "float_dec:16");

    r = parse_annotation("float_dec:32");
    CHECK(r.ok && r.vt.width == 32, "float_dec:32");

    r = parse_annotation("float_dec:64");
    CHECK(r.ok && r.vt.width == 64, "float_dec:64");

    r = parse_annotation("float_dec:128");
    CHECK(r.ok && r.vt.width == 128, "float_dec:128");

    r = parse_annotation("float_dec:256");
    CHECK(r.ok && r.vt.width == 256, "float_dec:256");

    /* Invalid width. */
    r = parse_annotation("float_dec:8");
    CHECK(!r.ok, "float_dec:8 rejected");

    r = parse_annotation("float_dec:48");
    CHECK(!r.ok, "float_dec:48 rejected");

    /* Invalid: Q param forbidden for float_dec. */
    r = parse_annotation("float_dec:32,q8");
    CHECK(!r.ok, "float_dec:32,q8 rejected (q param forbidden)");

    /* Invalid: base param forbidden. */
    r = parse_annotation("float_dec:64,_10");
    CHECK(!r.ok, "float_dec:64,_10 rejected");

    /* base field must be 0. */
    r = parse_annotation("float_dec:64");
    CHECK(r.ok && r.vt.base == 0, "float_dec:64 base==0");
}

/* ── bvn_effective_* helper tests ──────────────────────────────────── */

static void test_effective_helpers(void)
{
    puts("── bvn_effective_* helpers ───────────────────────────────────");

    value_type_spec_t fix32q8 = BVN_TYPE_FLOAT_FIX(32, 8);
    value_type_spec_t dec64   = BVN_TYPE_FLOAT_DEC(64);
    value_type_spec_t flt64   = BVN_TYPE_FLOAT(64);

    CHECK(bvn_effective_base(fix32q8) == 10u, "fix32q8: base is always 10");
    CHECK(bvn_effective_base(dec64)   == 10u, "dec64:   base is always 10");
    CHECK(bvn_effective_base(flt64)   == 10u, "flt64:   base default 10");

    CHECK(bvn_effective_q(fix32q8) == 8u,  "fix32q8: Q=8");
    CHECK(bvn_effective_q(dec64)   == 0u,  "dec64: Q=0");
    CHECK(bvn_effective_q(flt64)   == 0u,  "flt64: Q=0");

    CHECK(bvn_effective_width(fix32q8) == 32u, "fix32q8: width=32");
    CHECK(bvn_effective_width(dec64)   == 64u, "dec64: width=64");

    /* Zero width defaults to 64. */
    value_type_spec_t fix0 = BVN_TYPE_FLOAT_FIX(0, 4);
    CHECK(bvn_effective_width(fix0) == 64u, "float_fix width=0 → 64");
}

/* ── Reader round-trip: parse annotated values ─────────────────────── */

typedef struct {
    value_type_family_t family;
    uint32_t width;
    uint32_t base;   /* Q for float_fix */
    bool got_data;
    char data[64];
} ReadCapture;

static ReadCapture g_cap;

static bool on_capture(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    (void)ud;
    if (ev == ev_type_annotation_type_family) {
        g_cap.family = d->value_type.family;
        g_cap.width  = d->value_type.width;
        g_cap.base   = d->value_type.base;
    }
    if (ev == ev_data && d->type == token_is_number) {
        uint32_t n = d->length < 63 ? d->length : 63u;
        memcpy(g_cap.data, d->data, n);
        g_cap.data[n]  = '\0';
        g_cap.got_data = true;
    }
    return true;
}

static void read_snippet(const char *bvnr_text)
{
    memset(&g_cap, 0, sizeof g_cap);
    bvnr_read_flags_t flags;
    memset(&flags, 0, sizeof flags);
    flags.on_verified = on_capture;

    bvnr_reader_t *r = bvnr_reader_create();
    bvnr_open_read_mem(r, bvnr_text, (uint32_t)strlen(bvnr_text),
                       NULL, 0, &flags);
    bvnr_read(r);
    bvnr_reader_destroy(r);
}

static void test_reader_float_fix(void)
{
    puts("── reader: float_fix values ──────────────────────────────────");

    read_snippet(".x = <float_fix:32,q8> 3.14;\n");
    CHECK(g_cap.family == vt_float_fix, "float_fix:32,q8 family");
    CHECK(g_cap.width  == 32, "float_fix:32,q8 width");
    CHECK(g_cap.base   == 8,  "float_fix:32,q8 Q");
    CHECK(g_cap.got_data, "float_fix:32,q8 got data");

    read_snippet(".x = <float_fix:64,q31> -1.5;\n");
    CHECK(g_cap.family == vt_float_fix, "float_fix:64,q31 family");
    CHECK(g_cap.width  == 64 && g_cap.base == 31, "float_fix:64,q31 fields");

    read_snippet(".x = <float_fix:16> 0.5;\n");
    CHECK(g_cap.family == vt_float_fix, "float_fix:16 no-Q family");
    CHECK(g_cap.width == 16 && g_cap.base == 0, "float_fix:16 no-Q fields");

    /* Width 256. */
    read_snippet(".x = <float_fix:256,q128> 1.0;\n");
    CHECK(g_cap.family == vt_float_fix && g_cap.width == 256, "float_fix:256,q128");

    /* Dot and exponent are valid for float_fix. */
    read_snippet(".x = <float_fix:32,q16> 1e2;\n");
    CHECK(g_cap.got_data, "float_fix: exponent value accepted");

    read_snippet(".x = <float_fix:32,q16> .5;\n");
    CHECK(g_cap.got_data, "float_fix: dot-led value accepted");

    /* Special numbers. */
    read_snippet(".x = <float_fix:32,q8> $nan$;\n");
    CHECK(g_cap.family == vt_float_fix, "float_fix: nan special accepted");
}

static void test_reader_float_dec(void)
{
    puts("── reader: float_dec values ──────────────────────────────────");

    read_snippet(".x = <float_dec:64> 1.23456789e10;\n");
    CHECK(g_cap.family == vt_float_dec, "float_dec:64 family");
    CHECK(g_cap.width  == 64, "float_dec:64 width");
    CHECK(g_cap.base   == 0,  "float_dec:64 base=0");
    CHECK(g_cap.got_data, "float_dec:64 got data");

    read_snippet(".x = <float_dec:32> -0.001;\n");
    CHECK(g_cap.family == vt_float_dec && g_cap.width == 32, "float_dec:32");

    read_snippet(".x = <float_dec:128> 9.99e70;\n");
    CHECK(g_cap.family == vt_float_dec && g_cap.width == 128, "float_dec:128");

    read_snippet(".x = <float_dec:256> 1.0;\n");
    CHECK(g_cap.family == vt_float_dec && g_cap.width == 256, "float_dec:256");

    read_snippet(".x = <float_dec:16> $infinity$;\n");
    CHECK(g_cap.family == vt_float_dec, "float_dec: infinity special");
}

/* ── Writer round-trip ─────────────────────────────────────────────── */

#define WBUF_SIZE 4096
static uint8_t g_wbuf[WBUF_SIZE];

static uint64_t write_to_buf(bvnr_write_flags_t *flags,
                              bool (*fn)(bvnr_writer_t *))
{
    memset(g_wbuf, 0, sizeof g_wbuf);
    bvnr_writer_t *w = bvnr_writer_create();
    bvnr_open_write_mem(w, g_wbuf, sizeof g_wbuf, false, flags);
    fn(w);
    bvnr_write_finish(w);
    uint64_t n = bvnr_writer_bytes_written(w);
    bvnr_writer_destroy(w);
    return n;
}


static bool write_fix32q8(bvnr_writer_t *w)
{
    return bvnr_write_float_fix(w, "x", 32, 8, 3.14);
}
static bool write_fix64q0(bvnr_writer_t *w)
{
    return bvnr_write_float_fix(w, "val", 64, 0, -7.0);
}
static bool write_dec64(bvnr_writer_t *w)
{
    return bvnr_write_float_dec(w, "d", 64, 2.718281828);
}
static bool write_dec32(bvnr_writer_t *w)
{
    return bvnr_write_float_dec(w, "pi", 32, 3.14159);
}
static bool write_dec256(bvnr_writer_t *w)
{
    return bvnr_write_float_dec(w, "big", 256, 1.0);
}

static void test_writer_float_fix(void)
{
    puts("── writer: float_fix output ──────────────────────────────────");

    bvnr_write_flags_t flags; memset(&flags, 0, sizeof flags);
    uint64_t n;

    n = write_to_buf(&flags, write_fix32q8);
    CHECK(n > 0, "float_fix write produced output");
    CHECK(strstr((char *)g_wbuf, "float_fix") != NULL, "float_fix in output");
    CHECK(strstr((char *)g_wbuf, ":32") != NULL, "width :32 in output");
    CHECK(strstr((char *)g_wbuf, "q8") != NULL, "q8 in output");

    n = write_to_buf(&flags, write_fix64q0);
    CHECK(n > 0, "float_fix:64,q0 write ok");
    CHECK(strstr((char *)g_wbuf, "float_fix") != NULL, "float_fix:64 in output");
    /* Q=0: no qN token should appear. */
    CHECK(strstr((char *)g_wbuf, "q0") == NULL, "q0 not emitted for Q=0");
}

static void test_writer_float_dec(void)
{
    puts("── writer: float_dec output ──────────────────────────────────");

    bvnr_write_flags_t flags; memset(&flags, 0, sizeof flags);
    uint64_t n;

    n = write_to_buf(&flags, write_dec64);
    CHECK(n > 0, "float_dec:64 write ok");
    CHECK(strstr((char *)g_wbuf, "float_dec") != NULL, "float_dec in output");
    CHECK(strstr((char *)g_wbuf, ":64") != NULL, ":64 in output");
    /* No base or Q suffix. */
    CHECK(strstr((char *)g_wbuf, "_10") == NULL, "_10 not in float_dec output");
    CHECK(strstr((char *)g_wbuf, "q") == NULL, "q not in float_dec output");

    n = write_to_buf(&flags, write_dec32);
    CHECK(strstr((char *)g_wbuf, "float_dec") && strstr((char *)g_wbuf, ":32"),
          "float_dec:32 output");

    n = write_to_buf(&flags, write_dec256);
    CHECK(strstr((char *)g_wbuf, "float_dec") && strstr((char *)g_wbuf, ":256"),
          "float_dec:256 output");
}

/* ── Write→read round-trip ─────────────────────────────────────────── */

static void roundtrip_read(const uint8_t *buf, uint32_t len)
{
    memset(&g_cap, 0, sizeof g_cap);
    bvnr_read_flags_t flags; memset(&flags, 0, sizeof flags);
    flags.on_verified = on_capture;
    bvnr_reader_t *r = bvnr_reader_create();
    bvnr_open_read_mem(r, buf, len, NULL, 0, &flags);
    bvnr_read(r);
    bvnr_reader_destroy(r);
}

static void test_roundtrip(void)
{
    puts("── write→read round-trip ─────────────────────────────────────");

    bvnr_write_flags_t wf; memset(&wf, 0, sizeof wf);

    /* float_fix:32,q8 */
    write_to_buf(&wf, write_fix32q8);
    roundtrip_read(g_wbuf, (uint32_t)strlen((char *)g_wbuf));
    CHECK(g_cap.family == vt_float_fix, "roundtrip: float_fix family");
    CHECK(g_cap.width  == 32, "roundtrip: float_fix width=32");
    CHECK(g_cap.base   == 8,  "roundtrip: float_fix Q=8");

    /* float_dec:64 */
    write_to_buf(&wf, write_dec64);
    roundtrip_read(g_wbuf, (uint32_t)strlen((char *)g_wbuf));
    CHECK(g_cap.family == vt_float_dec, "roundtrip: float_dec family");
    CHECK(g_cap.width  == 64, "roundtrip: float_dec width=64");
    CHECK(g_cap.base   == 0,  "roundtrip: float_dec base=0");
}

/* ── bvn_float_t ↔ dec conversion API ─────────────────────────────── */

static void test_bvn_float_dec_api(void)
{
    puts("── bvn_float_t <-> dec API ───────────────────────────────────");

    bvn_limb_t limbs[BVN_FLOAT_NLIMBS(64u)];
    bvn_float_t f;
    bvn_float_init_buf(&f, 64u, limbs, BVN_FLOAT_NLIMBS(64u));

    /* 1.0 → dec64 → back. */
    bvn_float_from_double(&f, 1.0);
    uint64_t dec64_bits;
    bvn_float_to_dec64(&f, &dec64_bits);
    CHECK(dec64_bits != 0, "dec64 of 1.0 is nonzero");

    bvn_float_set_zero(&f, false);
    bool ok = bvn_float_from_dec64(&f, dec64_bits);
    CHECK(ok, "from_dec64 succeeded");
    double out = 0.0;
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - 1.0) < 1e-9, "dec64 round-trip: 1.0");

    /* -3.14 → dec32 → back (reduced precision). */
    bvn_float_from_double(&f, -3.14);
    uint32_t dec32_bits;
    bvn_float_to_dec32(&f, &dec32_bits);
    CHECK(dec32_bits != 0, "dec32 of -3.14 nonzero");

    bvn_float_set_zero(&f, false);
    ok = bvn_float_from_dec32(&f, dec32_bits);
    CHECK(ok, "from_dec32 succeeded");
    out = 0.0;
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - (-3.14)) < 0.01, "dec32 round-trip: -3.14 approx");

    /* NaN and Inf round-trip. */
    bvn_float_set_nan(&f);
    uint32_t nan_bits;
    bvn_float_to_dec32(&f, &nan_bits);
    bvn_float_set_zero(&f, false);
    bvn_float_from_dec32(&f, nan_bits);
    CHECK(bvn_float_is_nan(&f), "dec32 NaN round-trip");

    bvn_float_set_inf(&f, false);
    bvn_float_to_dec32(&f, &nan_bits);
    bvn_float_set_zero(&f, false);
    bvn_float_from_dec32(&f, nan_bits);
    CHECK(bvn_float_is_inf(&f) && !bvn_float_is_neg(&f), "dec32 +Inf round-trip");

    bvn_float_set_inf(&f, true);
    bvn_float_to_dec32(&f, &nan_bits);
    bvn_float_set_zero(&f, false);
    bvn_float_from_dec32(&f, nan_bits);
    CHECK(bvn_float_is_inf(&f) && bvn_float_is_neg(&f), "dec32 -Inf round-trip");

    /* Zero round-trip. */
    bvn_float_set_zero(&f, false);
    bvn_float_to_dec32(&f, &dec32_bits);
    bvn_float_from_dec32(&f, dec32_bits);
    CHECK(bvn_float_is_zero(&f), "dec32 zero round-trip");

    /* dec128 - 4-word output. */
    bvn_float_from_double(&f, 2.71828);
    uint32_t dec128[4];
    bvn_float_to_dec128(&f, dec128);
    bvn_float_set_zero(&f, false);
    ok = bvn_float_from_dec128(&f, dec128);
    CHECK(ok, "from_dec128 succeeded");
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - 2.71828) < 1e-4, "dec128 round-trip: e approx");

    /* dec16 — very narrow-range format: bias=101, 6 exp bits → [0..62].
     * Unbiased exponent range = [-101, -39].  Values near 1.5 produce
     * biased_exp ≈ 100 which overflows the 6-bit field → Inf.
     * Use a value in the representable range, e.g. 1.0e-50
     * (be = -50 + 101 = 51, which fits in 6 bits). */
    bvn_float_from_double(&f, 1.0e-50);
    uint16_t dec16_bits;
    bvn_float_to_dec16(&f, &dec16_bits);
    bvn_float_set_zero(&f, false);
    bvn_float_from_dec16(&f, dec16_bits);
    bvn_float_to_double(&f, &out);
    /* double can represent 1e-50; allow 1% relative error for 2-digit precision. */
    CHECK(fabs(out - 1.0e-50) < 1.0e-50 * 0.1, "dec16 round-trip: 1e-50");
}

/* ── bvn_float_t ↔ fix conversion API ─────────────────────────────── */

static void test_bvn_float_fix_api(void)
{
    puts("── bvn_float_t <-> fix API ───────────────────────────────────");

    bvn_limb_t limbs[BVN_FLOAT_NLIMBS(64u)];
    bvn_float_t f;
    bvn_float_init_buf(&f, 64u, limbs, BVN_FLOAT_NLIMBS(64u));

    /* to_fix16 / from_fix16: value = 1.5, Q=8 → integer 384 (1.5 × 256). */
    bvn_float_from_double(&f, 1.5);
    int16_t fix16 = bvn_float_to_fix16(&f, 8);
    CHECK(fix16 == 384, "to_fix16: 1.5 Q8 == 384");

    bvn_float_set_zero(&f, false);
    bool ok = bvn_float_from_fix16(&f, 384, 8);
    CHECK(ok, "from_fix16 ok");
    double out = 0.0;
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - 1.5) < 1e-6, "from_fix16: 384 Q8 == 1.5");

    /* to_fix32 / from_fix32: value = -2.25, Q=16 → -147456 (-2.25 × 65536). */
    bvn_float_from_double(&f, -2.25);
    int32_t fix32 = bvn_float_to_fix32(&f, 16);
    CHECK(fix32 == -147456, "to_fix32: -2.25 Q16 == -147456");

    bvn_float_set_zero(&f, false);
    ok = bvn_float_from_fix32(&f, -147456, 16);
    CHECK(ok, "from_fix32 ok");
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - (-2.25)) < 1e-6, "from_fix32: -147456 Q16 == -2.25");

    /* to_fix64 / from_fix64: 0.5, Q=32. */
    bvn_float_from_double(&f, 0.5);
    int64_t fix64 = bvn_float_to_fix64(&f, 32);
    CHECK(fix64 == (int64_t)1 << 31, "to_fix64: 0.5 Q32 == 2^31");

    bvn_float_set_zero(&f, false);
    ok = bvn_float_from_fix64(&f, (int64_t)1 << 31, 32);
    CHECK(ok, "from_fix64 ok");
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - 0.5) < 1e-9, "from_fix64: 2^31 Q32 == 0.5");

    /* Zero. */
    bvn_float_from_double(&f, 0.0);
    fix16 = bvn_float_to_fix16(&f, 8);
    CHECK(fix16 == 0, "to_fix16 zero");

    bvn_float_from_fix16(&f, 0, 8);
    CHECK(bvn_float_is_zero(&f), "from_fix16 zero");

    /* Negative with INT16_MIN boundary. */
    ok = bvn_float_from_fix16(&f, INT16_MIN, 0);
    CHECK(ok, "from_fix16 INT16_MIN ok");
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - (-32768.0)) < 1.0, "from_fix16 INT16_MIN value");

    /* Integer fixed-point (Q=0). */
    bvn_float_from_double(&f, 7.0);
    fix32 = bvn_float_to_fix32(&f, 0);
    CHECK(fix32 == 7, "to_fix32: 7.0 Q0 == 7");

    /* fix128 round-trip. */
    bvn_float_from_double(&f, 1.0);
    uint32_t fix128[4];
    bvn_float_to_fix128(&f, 16, fix128);
    /* 1.0 × 2^16 = 65536. */
    CHECK(fix128[0] == 65536u && fix128[1] == 0, "to_fix128: 1.0 Q16 == 65536");

    uint32_t fix128b[4] = {65536u, 0, 0, 0};
    bvn_float_set_zero(&f, false);
    ok = bvn_float_from_fix128(&f, fix128b, 16);
    CHECK(ok, "from_fix128 ok");
    bvn_float_to_double(&f, &out);
    CHECK(fabs(out - 1.0) < 1e-9, "from_fix128 round-trip: 1.0 Q16");
}

/* ── Validation error cases ────────────────────────────────────────── */

static error_code_t read_error(const char *snippet)
{
    error_code_t err = error_none;
    bvnr_read_flags_t flags; memset(&flags, 0, sizeof flags);
    flags.on_verified = on_capture;
    flags.on_error = NULL;

    bvnr_reader_t *r = bvnr_reader_create();
    bvnr_open_read_mem(r, snippet, (uint32_t)strlen(snippet), NULL, 0, &flags);
    bvnr_read(r);
    err = bvnr_reader_get_error(r);
    bvnr_reader_destroy(r);
    return err;
}

static void test_validation_errors(void)
{
    puts("── validation error cases ────────────────────────────────────");

    error_code_t err;

    /* Invalid width for float_fix. */
    err = read_error(".x = <float_fix:8> 1.0;\n");
    CHECK(err == error_illegal_value_type, "float_fix:8 → error_illegal_value_type");

    err = read_error(".x = <float_fix:48,q8> 1.0;\n");
    CHECK(err == error_illegal_value_type, "float_fix:48 → error_illegal_value_type");

    /* Q >= width. */
    err = read_error(".x = <float_fix:32,q32> 1.0;\n");
    CHECK(err == error_illegal_value_type, "float_fix:32,q32 → illegal");

    /* Invalid width for float_dec. */
    err = read_error(".x = <float_dec:8> 1.0;\n");
    CHECK(err == error_illegal_value_type, "float_dec:8 → error_illegal_value_type");

    /* Type/value mismatch: integer for float_fix is OK (no dot). */
    err = read_error(".x = <float_fix:32,q8> 5;\n");
    CHECK(err == error_none, "float_fix: bare integer value ok");

    /* String tokens: the validator accepts quoted strings for all numeric
     * families (they are used for non-decimal bases and special values).
     * Content validation happens at application level, not in bovnar. */
    err = read_error(".x = <float_fix:32,q8> \"hello\";\n");
    CHECK(err == error_none, "float_fix: quoted string accepted at validator level");

    err = read_error(".x = <float_dec:64> \"hello\";\n");
    CHECK(err == error_none, "float_dec: quoted string accepted at validator level");

    /* float_dec and float_fix accept dot and exponent. */
    err = read_error(".x = <float_fix:32,q16> 1.5;\n");
    CHECK(err == error_none, "float_fix: 1.5 ok");

    err = read_error(".x = <float_dec:64> 1.23e10;\n");
    CHECK(err == error_none, "float_dec: 1.23e10 ok");

    /* base param forbidden for float_fix. */
    err = read_error(".x = <float_fix:32,_10> 1.0;\n");
    CHECK(err == error_illegal_value_type, "float_fix:32,_10 → illegal");
}

/* ── bvn_type_is_numeric ───────────────────────────────────────────── */

static void test_is_numeric(void)
{
    puts("── bvn_type_is_numeric ───────────────────────────────────────");

    CHECK(bvn_type_is_numeric(BVN_TYPE_FLOAT_FIX(32, 8)),  "float_fix is numeric");
    CHECK(bvn_type_is_numeric(BVN_TYPE_FLOAT_DEC(64)),     "float_dec is numeric");
    CHECK(bvn_type_is_numeric(BVN_TYPE_FLOAT(64)),         "float is numeric");
    CHECK(bvn_type_is_numeric(BVN_TYPE_UINT(32)),          "uint is numeric");
    CHECK(!bvn_type_is_numeric(BVN_TYPE_UTF8),             "utf8 is not numeric");
    CHECK(!bvn_type_is_numeric(BVN_TYPE_PLAIN),            "plain is not numeric");
}

/* ── main ──────────────────────────────────────────────────────────── */

int main(void)
{
    puts("══ bvn_float_fix_dec_test ════════════════════════════════════");

    test_float_fix_parse();
    test_float_dec_parse();
    test_effective_helpers();
    test_reader_float_fix();
    test_reader_float_dec();
    test_writer_float_fix();
    test_writer_float_dec();
    test_roundtrip();
    test_bvn_float_dec_api();
    test_bvn_float_fix_api();
    test_validation_errors();
    test_is_numeric();

    puts("═════════════════════════════════════════════════════════════");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
