#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"

static char *make_cstr(const uint8_t *src, size_t srclen, size_t maxlen,
                       char *scratch, size_t scratch_cap)
{
    size_t n = srclen < maxlen ? srclen : maxlen;
    n = n < scratch_cap - 1 ? n : scratch_cap - 1;
    memcpy(scratch, src, n);
    scratch[n] = '\0';
    return scratch;
}

static value_type_spec_t make_type_spec(uint8_t family_byte, uint8_t param_byte)
{
    static const value_type_family_t families[] = {
        vt_plain, vt_utf8, vt_sint, vt_uint, vt_float
    };
    value_type_family_t fam = families[family_byte % 5u];

    static const uint32_t widths[] = { 0, 8, 16, 32, 64, 128, 255 };
    uint32_t width = widths[(param_byte & 0x07u) % 7u];

    static const uint32_t bases[] = { 0, 2, 8, 10, 16, 36, 62, 64, 85, 255 };
    uint32_t base = bases[((param_byte >> 3) & 0x07u) % 10u];

    return (value_type_spec_t){ .family = fam, .width = width, .base = base };
}

static void fuzz_validate_identifier(const uint8_t *p, size_t n)
{
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_identifier(buf);
}

static void fuzz_validate_symbol_value(const uint8_t *p, size_t n)
{
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_symbol(buf);
}

static void fuzz_validate_reference_value(const uint8_t *p, size_t n)
{
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_reference(buf);
}

static void fuzz_validate_number(const uint8_t *p, size_t n)
{
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_number(buf);
}

static void fuzz_validate_string(const uint8_t *p, size_t n)
{

    (void)bvn_validate_string(p, n);
}

static void fuzz_is_special_number_string(const uint8_t *p, size_t n)
{
    char buf[64];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_is_special_number_string(buf);
}

static void fuzz_validate_digits_for_base(const uint8_t *p, size_t n,
                                          uint8_t base_byte)
{

    static const uint32_t bases[] = { 0, 1, 2, 8, 10, 16, 36, 62, 64, 85, 100 };
    uint32_t base = bases[base_byte % 11u];

    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_digits_for_base(buf, base);
}

static void fuzz_validate_number_in_base(const uint8_t *p, size_t n,
                                         uint8_t base_byte)
{
    static const uint32_t bases[] = { 2, 8, 10, 16, 36, 62, 64, 85 };
    uint32_t base = bases[base_byte % 8u];

    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_number_in_base(buf, base);
}

static void fuzz_validate_uint_range(const uint8_t *p, size_t n,
                                     uint8_t w_byte, uint8_t b_byte)
{
    static const uint32_t widths[] = { 0, 8, 16, 32, 64, 128 };
    static const uint32_t bases[]  = { 0, 2, 10, 16, 36 };
    uint32_t w = widths[w_byte % 6u];
    uint32_t b = bases[b_byte % 5u];

    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_uint_range(buf, w, b);
}

static void fuzz_validate_sint_range(const uint8_t *p, size_t n,
                                     uint8_t w_byte, uint8_t b_byte)
{
    static const uint32_t widths[] = { 0, 8, 16, 32, 64, 128 };
    static const uint32_t bases[]  = { 0, 2, 10, 16 };
    uint32_t w = widths[w_byte % 6u];
    uint32_t b = bases[b_byte % 4u];

    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_validate_sint_range(buf, w, b);
}

static void fuzz_get_value_unit(const uint8_t *p, size_t n)
{

    size_t cap = n + 1 < 4096 ? n + 1 : 4096;
    uint8_t *buf = malloc(cap);
    if (!buf) return;
    size_t used = n < cap - 1 ? n : cap - 1;
    memcpy(buf, p, used);
    buf[used] = 0;

    bool ok = false;
    value_unit_t u = bvn_parse_unit(buf, &ok);

    if (ok) {

        if (u.num_components >= BVNR_MAX_UNIT_COMPONENTS)
            __builtin_trap();

        char strbuf[512];
        int32_t written = bvn_unit_to_string(u, strbuf, sizeof(strbuf));

        if (written >= 0 && (size_t)written >= sizeof(strbuf))
            __builtin_trap();

        double  factor  = bvn_unit_prefix_factor(u);
        double  pfactor = bvn_unit_prefix_factor(u);
        int32_t pexp    = bvn_unit_prefix_exponent(u);

        (void)factor; (void)pfactor; (void)pexp;
    }

    free(buf);
}

static void fuzz_parse_int64(const uint8_t *p, size_t n,
                             uint8_t fam, uint8_t param)
{
    value_type_spec_t vts = make_type_spec(fam, param);
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));

    int64_t out = 0;
    (void)bvn_parse_int64(buf, vts, &out);
}

static void fuzz_parse_uint64(const uint8_t *p, size_t n,
                              uint8_t fam, uint8_t param)
{
    value_type_spec_t vts = make_type_spec(fam, param);
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));

    uint64_t out = 0;
    (void)bvn_parse_uint64(buf, vts, &out);
}

static void fuzz_parse_double(const uint8_t *p, size_t n,
                              uint8_t fam, uint8_t param)
{
    value_type_spec_t vts = make_type_spec(fam, param);
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));

    double out = 0.0;
    (void)bvn_parse_double(buf, vts, &out);
}

static void fuzz_parse_double_in_base(const uint8_t *p, size_t n,
                                      uint8_t base_byte)
{
    static const uint32_t bases[] = { 2, 10, 16, 36, 64, 85 };
    uint32_t base = bases[base_byte % 6u];

    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));

    double out = 0.0;
    (void)bvn_parse_double_in_base(buf, base, &out);
}

static void fuzz_looks_like_double(const uint8_t *p, size_t n)
{
    char buf[512];
    make_cstr(p, n, sizeof(buf) - 1, buf, sizeof(buf));
    (void)bvn_looks_like_double(buf);
}

static void fuzz_format_uint64(const uint8_t *p, size_t n,
                               uint8_t base_byte, uint8_t mindig_byte)
{
    uint64_t value = 0;
    memcpy(&value, p, n < sizeof(value) ? n : sizeof(value));

    static const uint32_t bases[]  = { 2, 8, 10, 16, 36, 62, 64, 85 };
    static const uint32_t mindig[] = { 0, 1, 4, 8, 16, 64, 255 };
    uint32_t base = bases[base_byte % 8u];
    uint32_t md   = mindig[mindig_byte % 7u];

    char buf[1024];
    int32_t r = bvn_format_uint64(buf, sizeof(buf), value, base, md);
    if (r < 0) return;
    if ((size_t)r >= sizeof(buf)) __builtin_trap();

    if (r > 0) (void)buf[r - 1];
}

static void fuzz_format_int64(const uint8_t *p, size_t n,
                              uint8_t base_byte, uint8_t mindig_byte)
{
    int64_t value = 0;
    memcpy(&value, p, n < sizeof(value) ? n : sizeof(value));

    static const uint32_t bases[]  = { 2, 8, 10, 16 };
    static const uint32_t mindig[] = { 0, 1, 4, 8 };
    uint32_t base = bases[base_byte % 4u];
    uint32_t md   = mindig[mindig_byte % 4u];

    char buf[256];
    int32_t r = bvn_format_int64(buf, sizeof(buf), value, base, md);
    if (r < 0) return;
    if ((size_t)r >= sizeof(buf)) __builtin_trap();
    if (r > 0) (void)buf[r - 1];
}

static void fuzz_format_double(const uint8_t *p, size_t n,
                               uint8_t fam, uint8_t param)
{
    double value = 0.0;
    memcpy(&value, p, n < sizeof(value) ? n : sizeof(value));

    value_type_spec_t vts = make_type_spec(fam, param);

    char buf[256];
    int32_t r = bvn_format_double(buf, sizeof(buf), value, vts);
    if (r < 0) return;
    if ((size_t)r >= sizeof(buf)) __builtin_trap();
    if (r > 0) (void)buf[r - 1];
}

static void fuzz_char_ops(const uint8_t *p, size_t n,
                          uint8_t fam, uint8_t param)
{
    if (n == 0) return;

    static const uint32_t bases[] = { 2, 8, 10, 16, 36, 62, 85 };
    for (size_t i = 0; i < n && i < 16u; i++) {
        for (size_t b = 0; b < 7u; b++)
            (void)bvn_char_to_digit((uint32_t)p[i], bases[b]);
    }

    value_type_spec_t vts = make_type_spec(fam, param);
    (void)bvn_min_digits_for_type(vts);
}

static void fuzz_parse_type_annotation(const uint8_t *p, size_t n,
                                       uint8_t _unused)
{
    (void)_unused;
    if (n == 0) return;

    bool type_ok = false, unit_ok = false, unit_too_long = false;
    value_unit_t out_unit;
    memset(&out_unit, 0, sizeof(out_unit));
    uint8_t unit_buf[256];
    uint8_t unit_buf_len = 0;

    value_type_spec_t vts = bvn_parse_type_annotation(
        p, (uint32_t)(n < UINT32_MAX ? n : UINT32_MAX),
        &type_ok, &unit_ok, &unit_too_long,
        &out_unit, unit_buf, &unit_buf_len);

    if (type_ok) {

        if ((unsigned)vts.family >= (unsigned)vt_illegal + 1u)
            __builtin_trap();
    }
    if (unit_ok) {
        if (out_unit.num_components > BVNR_MAX_UNIT_COMPONENTS + 1u)
            __builtin_trap();
    }
}

#define N_SELECTORS 21u

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

    if (size < 3)
        return 0;

    uint8_t selector = data[0] % N_SELECTORS;
    uint8_t aux_a    = data[1];
    uint8_t aux_b    = data[2];

    const uint8_t *payload = data + 3;
    size_t         plen    = size - 3;

    switch (selector) {
    case  0: fuzz_validate_identifier(payload, plen); break;
    case  1: fuzz_validate_symbol_value(payload, plen); break;
    case  2: fuzz_validate_reference_value(payload, plen); break;
    case  3: fuzz_validate_number(payload, plen); break;
    case  4: fuzz_validate_string(payload, plen); break;
    case  5: fuzz_is_special_number_string(payload, plen); break;
    case  6: fuzz_validate_digits_for_base(payload, plen, aux_a); break;
    case  7: fuzz_validate_number_in_base(payload, plen, aux_a); break;
    case  8: fuzz_validate_uint_range(payload, plen, aux_a, aux_b); break;
    case  9: fuzz_validate_sint_range(payload, plen, aux_a, aux_b); break;
    case 10: fuzz_get_value_unit(payload, plen); break;
    case 11: fuzz_parse_int64(payload, plen, aux_a, aux_b); break;
    case 12: fuzz_parse_uint64(payload, plen, aux_a, aux_b); break;
    case 13: fuzz_parse_double(payload, plen, aux_a, aux_b); break;
    case 14: fuzz_parse_double_in_base(payload, plen, aux_a); break;
    case 15: fuzz_looks_like_double(payload, plen); break;
    case 16: fuzz_format_uint64(payload, plen, aux_a, aux_b); break;
    case 17: fuzz_format_int64(payload, plen, aux_a, aux_b); break;
    case 18: fuzz_format_double(payload, plen, aux_a, aux_b); break;
    case 19: fuzz_char_ops(payload, plen, aux_a, aux_b); break;
    case 20: fuzz_parse_type_annotation(payload, plen, aux_a); break;
    default: break;
    }

    return 0;
}

#ifdef __AFL_FUZZ_TESTCASE_LEN

__AFL_FUZZ_INIT();

int main(void)
{
    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(100000)) {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
        LLVMFuzzerTestOneInput(buf, len);
    }
    return 0;
}

#elif defined(FUZZ_STANDALONE)

static int run_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return 0; }

    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 1; }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return 1;
    }
    fclose(f);
    LLVMFuzzerTestOneInput(buf, (size_t)sz);
    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <corpus_file> [...]\n", argv[0]);
        return 1;
    }
    int ret = 0;
    for (int i = 1; i < argc; i++)
        ret |= run_file(argv[i]);
    return ret;
}

#endif
