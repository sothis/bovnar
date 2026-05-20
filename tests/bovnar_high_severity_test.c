#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_si_units.h"
#include "bvn_float.h"

static int g_tests    = 0;
static int g_failures = 0;

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "  FAIL %s:%d: " fmt "\n", \
            __FILE__, __LINE__, __VA_ARGS__); \
    g_failures++; \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    g_tests++; \
    if (!(cond)) FAIL("%s", (msg)); \
} while (0)

#define ASSERT_FALSE(cond, msg) \
    ASSERT_TRUE(!(cond), msg)

#define ASSERT_EQ_INT(a, b, msg) do { \
    g_tests++; \
    int _a = (int)(a), _b = (int)(b); \
    if (_a != _b) FAIL("%s: got %d expected %d", (msg), _a, _b); \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do { \
    g_tests++; \
    uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); \
    if (_a != _b) FAIL("%s: got %" PRIu64 " expected %" PRIu64, \
                       (msg), _a, _b); \
} while (0)

#define ASSERT_GE_UINT(a, b, msg) do { \
    g_tests++; \
    uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); \
    if (_a < _b) FAIL("%s: %" PRIu64 " < %" PRIu64, (msg), _a, _b); \
} while (0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    g_tests++; \
    double _a = (double)(a), _b = (double)(b), _e = (double)(eps); \
    if (fabs(_a - _b) > _e) \
        FAIL("%s: %.15g vs %.15g (eps %.2g)", (msg), _a, _b, _e); \
} while (0)

typedef struct {
    unsigned ev_data_count;
    unsigned array_row_start_count;
    unsigned array_row_end_count;
    unsigned octet_start_count;
    unsigned octet_end_count;
    unsigned struct_start_count;
    unsigned struct_end_count;
    unsigned stream_end_count;
    unsigned token_array_num_count;
    unsigned token_array_str_count;

    char     val[3][128];
    uint32_t val_len[3];
    error_code_t last_error;
} capture_t;

static bool capture_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    capture_t *c = ud;
    switch (ev) {
    case ev_data:
        if (c->ev_data_count < 3) {
            uint32_t n = d->length < 127u ? d->length : 127u;
            memcpy(c->val[c->ev_data_count], d->data, n);
            c->val[c->ev_data_count][n] = '\0';
            c->val_len[c->ev_data_count] = n;
        }
        c->ev_data_count++;
        if (d->type == token_is_array_number) c->token_array_num_count++;
        if (d->type == token_is_array_string) c->token_array_str_count++;
        break;
    case ev_array_row_start:   c->array_row_start_count++;  break;
    case ev_array_row_end:   c->array_row_end_count++;  break;
    case ev_octet_stream_start: c->octet_start_count++; break;
    case ev_octet_stream_end:   c->octet_end_count++;   break;
    case ev_struct_start:    c->struct_start_count++;   break;
    case ev_struct_end:      c->struct_end_count++;     break;
    case ev_stream_end:      c->stream_end_count++;     break;
    default: break;
    }
    return true;
}

static void capture_error(void *ud, error_code_t err,
                          uint64_t line, uint64_t col,
                          uint32_t byte, uint64_t off)
{
    capture_t *c = ud;
    c->last_error = err;
    (void)line; (void)col; (void)byte; (void)off;
}

static bool do_parse(const void *buf, uint64_t len,
                     bvnr_read_flags_t *flags,
                     bvnr_reader_t **out_r)
{
    bvnr_reader_t *r = bvnr_reader_create();
    if (!r) { FAIL("%s", "bvnr_reader_create returned NULL"); return false; }

    bool ok = bvnr_open_read_mem(r, buf, len, NULL, 0, flags)
           && bvnr_read(r);

    if (out_r) { *out_r = r; }
    else       { bvnr_reader_destroy(r); }
    return ok;
}

static bool do_parse_str(const char *s, bvnr_read_flags_t *flags,
                         bvnr_reader_t **out_r)
{
    return do_parse(s, (uint32_t)strlen(s), flags, out_r);
}

static void test_writer_error_accessors(void)
{
    printf("  test_writer_error_accessors...\n");

    uint8_t small[12];
    bvnr_writer_t *w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create");
    if (!w) return;

    ASSERT_TRUE(bvnr_open_write_mem(w, small, sizeof(small), false,
                                    NULL),
                "open_write_mem with 12-byte sink");

    bvnr_data_t hdr = {0};
    (void)bvnr_write_event(w, ev_stream_start, &hdr);

    bool any_failed = false;
    bvnr_data_t key = { .data = "port", .length = 4 };
    if (!bvnr_write_event(w, ev_assignment_start, &key)) { any_failed = true; }

    if (!any_failed) {
        value_type_spec_t vt = BVN_TYPE_UINT(16);
        value_unit_t      vu = BVN_UNIT_NONE;
        bvnr_data_t ann = { .value_type = vt, .value_unit = vu };
        if (!bvnr_write_event(w, ev_type_annotation_start,              &ann)) any_failed = true;
        if (!bvnr_write_event(w, ev_type_annotation_type_family,        &ann)) any_failed = true;
        if (!bvnr_write_event(w, ev_type_annotation_type_family_parameter, &ann)) any_failed = true;
        if (!bvnr_write_event(w, ev_type_annotation_end,                &ann)) any_failed = true;
        char vbuf[] = "8080";
        bvnr_data_t val = {
            .data       = vbuf,
            .length     = 4,
            .value_type = vt,
            .value_unit = vu,
            .type       = token_is_number,
        };
        if (!bvnr_write_event(w, ev_data, &val)) any_failed = true;
    }

    ASSERT_TRUE(any_failed, "at least one write must fail into a 12-byte sink");

    error_code_t ec = bvnr_writer_get_error(w);
    ASSERT_EQ_INT(ec, error_sink_buffer_exhausted,
                  "error code is error_sink_buffer_exhausted");

    uint64_t offset = bvnr_writer_get_error_offset(w);
    uint64_t bw     = bvnr_writer_bytes_written(w);

    ASSERT_TRUE(offset <= (uint64_t)sizeof(small),
                "error offset is within buffer size");
    ASSERT_TRUE(bw <= sizeof(small),
                "bytes_written is within buffer size");

    bvnr_data_t dummy = {0};
    ASSERT_FALSE(bvnr_write_event(w, ev_stream_start, &dummy),
                 "write after error_sink_buffer_exhausted returns false");
    ASSERT_EQ_INT(bvnr_writer_get_error(w), error_sink_buffer_exhausted,
                  "error code unchanged after redundant write");

    bvnr_writer_destroy(w);
}

static void test_reader_max_identifier_length(void)
{
    printf("  test_reader_max_identifier_length...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_identifier_length = 3,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".abcd = 1;\n", &f, &r),
                 "4-char identifier with limit=3 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_identifier_too_long,
                  "error is error_identifier_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".abc = 1;\n", &f, &r),
                "3-char identifier with limit=3 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_string_length(void)
{
    printf("  test_reader_max_string_length...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_string_length = 4,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = \"hello\";\n", &f, &r),
                 "5-byte string with limit=4 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_string_too_long,
                  "error is error_string_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".x = \"abcd\";\n", &f, &r),
                "4-byte string with limit=4 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_number_length(void)
{
    printf("  test_reader_max_number_length...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_number_length = 3,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = 1234;\n", &f, &r),
                 "4-digit number with limit=3 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_number_too_long,
                  "error is error_number_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".x = 123;\n", &f, &r),
                "3-digit number with limit=3 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_symbol_length(void)
{
    printf("  test_reader_max_symbol_length...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_symbol_length = 4,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = abcde;\n", &f, &r),
                 "5-char symbol with limit=4 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_symbol_too_long,
                  "error is error_symbol_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".x = abcd;\n", &f, &r),
                "4-char symbol with limit=4 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_reference_length(void)
{
    printf("  test_reader_max_reference_length...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_reference_length = 5,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = &.longer;\n", &f, &r),
                 "7-char reference with limit=5 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_reference_too_long,
                  "error is error_reference_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".x = &.long;\n", &f, &r),
                "5-char reference with limit=5 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_array_items(void)
{
    printf("  test_reader_max_array_items...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_array_items = 2,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".a = [1,2,3];\n", &f, &r),
                 "3-item array with limit=2 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_too_many_array_items,
                  "error is error_too_many_array_items");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".a = [1,2];\n", &f, &r),
                "2-item array with limit=2 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_text_bytes(void)
{
    printf("  test_reader_max_text_bytes...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_text_bytes = 7,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = 1;\n", &f, &r),
                 "8-byte stream with max_text_bytes=7 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_text_data_too_long,
                  "error is error_text_data_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    f.max_text_bytes = 8;
    ASSERT_TRUE(do_parse_str(".x = 1;\n", &f, &r),
                "8-byte stream with max_text_bytes=8 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_file_size(void)
{
    printf("  test_reader_max_file_size...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_file_size = 7,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = 1;\n", &f, &r),
                 "8-byte stream with max_file_size=7 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_file_too_long,
                  "error is error_file_too_long");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    f.max_file_size = 8;
    ASSERT_TRUE(do_parse_str(".x = 1;\n", &f, &r),
                "8-byte stream with max_file_size=8 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_struct_nesting(void)
{
    printf("  test_reader_max_struct_nesting...\n");

    const char *depth3 = ".a = { .b = { .c = { .d = 1; }; }; };\n";
    const char *depth2 = ".a = { .b = { .c = 1; }; };\n";

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_struct_nesting = 2,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(depth3, &f, &r),
                 "depth-3 struct with limit=2 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_struct_nesting_too_high,
                  "error is error_struct_nesting_too_high");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(depth2, &f, &r),
                "depth-2 struct with limit=2 succeeds");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    f.max_struct_nesting = 3;
    ASSERT_TRUE(do_parse_str(depth3, &f, &r),
                "depth-3 struct with limit=3 succeeds");
    bvnr_reader_destroy(r);
}

static void test_reader_max_array_nesting(void)
{
    printf("  test_reader_max_array_nesting...\n");

    const char *nested = ".a = [[1,2],[3,4]];\n";
    const char *flat   = ".a = [1,2,3];\n";

    capture_t ctx = {0};
    bvnr_read_flags_t f = {
        .max_array_nesting = 1,
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(nested, &f, &r),
                 "depth-2 array with limit=1 fails");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_array_nesting_too_high,
                  "error is error_array_nesting_too_high");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(flat, &f, &r),
                "depth-1 array with limit=1 succeeds");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    f.max_array_nesting = 2;
    ASSERT_TRUE(do_parse_str(nested, &f, &r),
                "depth-2 array with limit=2 succeeds");
    bvnr_reader_destroy(r);
}

static void test_recovery_count(void)
{
    printf("  test_recovery_count...\n");

    const char *src =
        ".a = <float:8> 1.0;\n"
        ".b = <float:8> 2.0;\n"
        ".c = <float:8> 3.0;\n"
        ".d = 42;\n";

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified       = capture_verified,
        .on_error          = capture_error,
        .userdata          = &ctx,
        .continue_on_error = true,
    };

    bvnr_reader_t *r = NULL;
    do_parse_str(src, &flags, &r);

    uint64_t rc = bvnr_reader_get_recovery_count(r);
    ASSERT_GE_UINT(rc, 3,
                   "recovery_count >= 3 after three illegal-type assignments");

    ASSERT_GE_UINT((uint64_t)ctx.ev_data_count, 1,
                   "at least one ev_data delivered despite earlier errors");

    bvnr_reader_destroy(r);
}

static bool cb_abort_immediately(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    (void)ud; (void)ev; (void)d;
    return false;
}

static void test_callback_abort_verified(void)
{
    printf("  test_callback_abort_verified...\n");

    bvnr_read_flags_t flags = { .on_verified = cb_abort_immediately };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = 1;\n", &flags, &r),
                 "parse must fail when on_verified returns false");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_scanner_callback_failed,
                  "error is error_scanner_callback_failed (verified path)");
    bvnr_reader_destroy(r);
}

static void test_callback_abort_unverified(void)
{
    printf("  test_callback_abort_unverified...\n");

    bvnr_read_flags_t flags = {
        .on_unverified = cb_abort_immediately,

    };
    bvnr_reader_t *r = NULL;

    ASSERT_FALSE(do_parse_str(".x = 1;\n", &flags, &r),
                 "parse must fail when on_unverified returns false");
    ASSERT_EQ_INT(bvnr_reader_get_error(r), error_scanner_callback_failed,
                  "error is error_scanner_callback_failed (unverified path)");
    bvnr_reader_destroy(r);
}

static void test_si_units_compatible(void)
{
    printf("  test_si_units_compatible...\n");

    value_unit_t m   = BVN_UNIT_SI(bu_meter,  si_none);
    value_unit_t km  = BVN_UNIT_SI(bu_meter,  si_kilo);
    value_unit_t mm  = BVN_UNIT_SI(bu_meter,  si_milli);
    value_unit_t s   = BVN_UNIT_SI(bu_second, si_none);
    value_unit_t kg  = BVN_UNIT_SI(bu_gram,   si_kilo);

    ASSERT_TRUE (bvn_units_compatible(m,  km), "m and km: compatible");
    ASSERT_TRUE (bvn_units_compatible(km, m),  "km and m: symmetric");
    ASSERT_TRUE (bvn_units_compatible(m,  mm), "m and mm: compatible");
    ASSERT_FALSE(bvn_units_compatible(m,  s),  "m and s: incompatible");
    ASSERT_FALSE(bvn_units_compatible(m,  kg), "m and kg: incompatible");
    ASSERT_FALSE(bvn_units_compatible(s,  kg), "s and kg: incompatible");

    value_unit_t none = BVN_UNIT_NONE;
    ASSERT_FALSE(bvn_units_compatible(m, none), "m and none: incompatible");
}

static void test_si_unit_convert_factor(void)
{
    printf("  test_si_unit_convert_factor...\n");

    value_unit_t m  = BVN_UNIT_SI(bu_meter, si_none);
    value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo);
    value_unit_t mm = BVN_UNIT_SI(bu_meter, si_milli);
    value_unit_t s  = BVN_UNIT_SI(bu_second, si_none);

    bool ok = false, req_affine = false;
    double f;

    f = bvn_unit_convert_factor(km, m, &ok, &req_affine);
    ASSERT_TRUE (ok,        "km→m: ok");
    ASSERT_FALSE(req_affine,"km→m: not affine");
    ASSERT_NEAR (f, 1000.0, 1e-9, "km→m factor = 1000");

    f = bvn_unit_convert_factor(m, km, &ok, &req_affine);
    ASSERT_TRUE (ok,        "m→km: ok");
    ASSERT_NEAR (f, 0.001,  1e-12, "m→km factor = 0.001");

    f = bvn_unit_convert_factor(mm, m, &ok, &req_affine);
    ASSERT_TRUE (ok,         "mm→m: ok");
    ASSERT_NEAR (f, 0.001,   1e-12, "mm→m factor = 0.001");

    (void)bvn_unit_convert_factor(m, s, &ok, &req_affine);
    ASSERT_FALSE(ok, "m→s: incompatible, ok=false");
}

static void test_si_unit_dimension_vector(void)
{
    printf("  test_si_unit_dimension_vector...\n");

    value_unit_t ms = BVN_UNIT_COMPOUND2(
        bu_meter,  si_none, exp_linear,
        bu_second, si_none, exp_neg_linear);

    int32_t dims[7] = {0};
    ASSERT_TRUE(bvn_unit_dimension_vector(ms, dims),
                "dimension_vector for m/s succeeds");
    ASSERT_EQ_INT(dims[bvn_si_dim_meter],    1,  "m/s: meter exp = +1");
    ASSERT_EQ_INT(dims[bvn_si_dim_second],  -1,  "m/s: second exp = -1");
    ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 0,  "m/s: mass exp = 0");
    ASSERT_EQ_INT(dims[bvn_si_dim_ampere],   0,  "m/s: current exp = 0");

    value_unit_t m = BVN_UNIT_SI(bu_meter, si_none);
    memset(dims, 0, sizeof(dims));
    ASSERT_TRUE(bvn_unit_dimension_vector(m, dims),
                "dimension_vector for m succeeds");
    ASSERT_EQ_INT(dims[bvn_si_dim_meter],  1, "m: meter exp = +1");
    ASSERT_EQ_INT(dims[bvn_si_dim_second], 0, "m: second exp = 0");

    value_unit_t none = BVN_UNIT_NONE;
    memset(dims, 0, sizeof(dims));
    (void)bvn_unit_dimension_vector(none, dims);
    for (int i = 0; i < 7; i++)
        ASSERT_EQ_INT(dims[i], 0, "dimensionless: all exponents = 0");
}

static void test_si_unit_reduce(void)
{
    printf("  test_si_unit_reduce...\n");

    value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo);
    double scale = 1.0;
    bool overflow = false;
    value_unit_t reduced = bvn_unit_reduce(km, &scale, &overflow);

    ASSERT_NEAR(scale, 1000.0, 1e-9, "km: reduce scale = 1000");

    value_unit_t m = BVN_UNIT_SI(bu_meter, si_none);
    ASSERT_TRUE(bvn_units_compatible(reduced, m),
                "reduced km is dimensionally compatible with m");

    value_unit_t ms = BVN_UNIT_SI(bu_second, si_milli);
    scale = 1.0;
    (void)bvn_unit_reduce(ms, &scale, NULL);
    ASSERT_NEAR(scale, 0.001, 1e-12, "ms: reduce scale = 0.001");

    value_unit_t gib = BVN_UNIT_IEC(bu_byte, iec_gibi);
    scale = 1.0;
    (void)bvn_unit_reduce(gib, &scale, NULL);
    ASSERT_NEAR(scale, 1073741824.0, 1.0, "GiB: reduce scale = 2^30");
}

static void test_si_unit_to_si_factor(void)
{
    printf("  test_si_unit_to_si_factor...\n");

    bool is_affine = false, ok = false;
    double offset = 0.0, factor;

    value_unit_t kms = BVN_UNIT_COMPOUND2(
        bu_meter,  si_kilo, exp_linear,
        bu_second, si_none, exp_neg_linear);

    factor = bvn_unit_to_si_factor(kms, &is_affine, &offset, &ok);
    ASSERT_TRUE (ok,         "unit_to_si_factor km/s: ok");
    ASSERT_FALSE(is_affine,  "km/s: not affine");
    ASSERT_NEAR (factor, 1000.0, 1e-9, "km/s SI factor = 1000");

    value_unit_t K = BVN_UNIT_SI(bu_kelvin, si_none);
    factor = bvn_unit_to_si_factor(K, &is_affine, &offset, &ok);
    ASSERT_TRUE (ok,        "unit_to_si_factor K: ok");
    ASSERT_FALSE(is_affine, "K: not affine");
    ASSERT_NEAR (factor, 1.0, 1e-12, "K SI factor = 1");

    value_unit_t mK = BVN_UNIT_SI(bu_kelvin, si_milli);
    factor = bvn_unit_to_si_factor(mK, &is_affine, &offset, &ok);
    ASSERT_TRUE(ok,           "unit_to_si_factor mK: ok");
    ASSERT_NEAR(factor, 0.001, 1e-12, "mK SI factor = 0.001");

    value_unit_t C = BVN_UNIT_SI(bu_celsius, si_none);
    is_affine = false; offset = 0.0;
    factor = bvn_unit_to_si_factor(C, &is_affine, &offset, &ok);
    ASSERT_TRUE(ok,       "unit_to_si_factor °C: ok");
    ASSERT_TRUE(is_affine,"°C: is_affine = true");
    ASSERT_NEAR(factor, 1.0,    1e-9,  "°C SI factor = 1");
    ASSERT_NEAR(offset, 273.15, 1e-3,  "°C affine_offset ≈ 273.15 K");
}

static void test_array_reader_events(void)
{
    printf("  test_array_reader_events...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_TRUE(do_parse_str(".a = [1, 2, 3];\n", &flags, &r),
                "1-D numeric array parses");
    ASSERT_EQ_INT((int)ctx.token_array_num_count, 3,
                  "[1,2,3]: 3 token_is_array_number events");
    ASSERT_GE_UINT((uint64_t)ctx.array_row_start_count, 1,
                   "[1,2,3]: at least 1 ev_array_row_start");
    ASSERT_GE_UINT((uint64_t)ctx.array_row_end_count, 1,
                   "[1,2,3]: at least 1 ev_array_row_end");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".a = [1,2]/[3,4];\n", &flags, &r),
                "multi-row array parses");
    ASSERT_EQ_INT((int)ctx.token_array_num_count, 4,
                  "[1,2]/[3,4]: 4 token_is_array_number events");

    ASSERT_GE_UINT((uint64_t)ctx.array_row_start_count, 2,
                   "[1,2]/[3,4]: at least 2 ev_array_row_start");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".a = [\"x\", \"y\", \"z\"];\n", &flags, &r),
                "string array parses");
    ASSERT_EQ_INT((int)ctx.token_array_str_count, 3,
                  "string array: 3 token_is_array_string events");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".a = [,1,,2,];\n", &flags, &r),
                "sparse array parses");

    ASSERT_GE_UINT((uint64_t)ctx.ev_data_count, 2,
                   "sparse array: at least 2 ev_data events");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".a = [<sint:16> 1, <sint:16> 2];\n", &flags, &r),
                "typed array parses");
    ASSERT_EQ_INT((int)ctx.token_array_num_count, 2,
                  "typed array: 2 token_is_array_number events");
    bvnr_reader_destroy(r);
}

static void test_octet_stream_reader_events(void)
{
    printf("  test_octet_stream_reader_events...\n");

    static const uint8_t src[] = {

        '.', 'b', ' ', '=', ' ',

        '\x00',

        '\x01', '\x05', '\x00', 'h', 'e', 'l', 'l', 'o',

        '\x00',

        ';', '\n'
    };

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_TRUE(do_parse(src, sizeof(src), &flags, &r),
                "octet-stream document parses without error");
    ASSERT_EQ_INT((int)ctx.octet_start_count, 1,
                  "ev_octet_stream_start received exactly once");
    ASSERT_EQ_INT((int)ctx.octet_end_count, 1,
                  "ev_octet_stream_end received exactly once");
    bvnr_reader_destroy(r);

    static const uint8_t empty_oct[] = {
        '.', 'x', ' ', '=', ' ',
        '\x00', '\x00',
        ';', '\n'
    };
    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse(empty_oct, sizeof(empty_oct), &flags, &r),
                "empty octet-stream parses without error");
    ASSERT_EQ_INT((int)ctx.octet_start_count, 1, "empty octet: start event");
    ASSERT_EQ_INT((int)ctx.octet_end_count,   1, "empty octet: end event");
    bvnr_reader_destroy(r);
}

static void test_string_vt_vf_escape_write(void)
{
    printf("  test_string_vt_vf_escape_write...\n");

    uint8_t outbuf[256];
    bvnr_sink_t sink;
    bvnr_writer_t *w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create");
    if (!w) return;
    bvnr_sink_to_mem(&sink, outbuf, sizeof(outbuf));
    ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL),
                "open_write_sink for \\v/\\f test");

    bvnr_data_t ev = {0};
    ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &ev), "stream_start");

    ev.type = token_is_identifier;
    ev.data = "vt_key"; ev.length = 6;
    ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &ev),
                "assignment_start for \\v key");

    static const uint8_t vt_payload[] = { 'a', '\v', 'b' };
    ev.type   = token_is_string;
    ev.data   = vt_payload;
    ev.length = sizeof(vt_payload);
    ASSERT_TRUE(bvnr_write_event(w, ev_data, &ev),
                "ev_data with \\v byte must succeed after Bug 1 fix");
    ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none,
                  "no error after writing \\v in string");

    ev.type = token_is_identifier;
    ev.data = "ff_key"; ev.length = 6;
    ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &ev),
                "assignment_start for \\f key");

    static const uint8_t ff_payload[] = { 'x', '\f', 'y' };
    ev.type   = token_is_string;
    ev.data   = ff_payload;
    ev.length = sizeof(ff_payload);
    ASSERT_TRUE(bvnr_write_event(w, ev_data, &ev),
                "ev_data with \\f byte must succeed after Bug 1 fix");
    ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none,
                  "no error after writing \\f in string");

    ASSERT_TRUE(bvnr_write_finish(w), "finish");

    uint64_t n = bvnr_writer_bytes_written(w);
    bvnr_writer_destroy(w);
    ASSERT_TRUE(n > 0, "bytes written > 0");

    bvnr_reader_t *r = NULL;
    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    ASSERT_TRUE(do_parse(outbuf, (uint32_t)n, &flags, &r),
                "written \\v/\\f document roundtrips cleanly");
    ASSERT_EQ_INT((int)ctx.ev_data_count, 2, "two string values roundtripped");
    bvnr_reader_destroy(r);
}

static void test_octet_zero_length_chunk_rejected(void)
{
    printf("  test_octet_zero_length_chunk_rejected...\n");

    uint8_t outbuf[128];
    bvnr_sink_t sink;
    bvnr_writer_t *w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create");
    if (!w) return;
    bvnr_sink_to_mem(&sink, outbuf, sizeof(outbuf));
    ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL),
                "open_write_sink");

    bvnr_data_t ev = {0};
    ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &ev), "stream_start");

    ev.type = token_is_identifier;
    ev.data = "b"; ev.length = 1;
    ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &ev),
                "assignment_start");
    ASSERT_TRUE(bvnr_write_event(w, ev_octet_stream_start, &ev),
                "octet_stream_start");

    ev.type   = token_is_octet_stream;
    ev.data   = outbuf;
    ev.length = 0;
    ASSERT_FALSE(bvnr_write_event(w, ev_data, &ev),
                 "zero-length octet chunk must be rejected");
    ASSERT_EQ_INT(bvnr_writer_get_error(w), error_invalid_argument,
                  "error must be error_invalid_argument for zero-length chunk");

    bvnr_writer_destroy(w);
}

static void test_special_float_write(void)
{
    printf("  test_special_float_write...\n");

    uint8_t outbuf[512];

    bvnr_writer_t *w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create");
    if (!w) return;

    ASSERT_TRUE(bvnr_open_write_mem(w, outbuf, sizeof(outbuf), false, NULL),
                "open_write_mem for special floats");

    bvnr_data_t hdr = {0};
    ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &hdr), "stream_start");
    ASSERT_TRUE(bvnr_write_float(w, "nan_val",  64,  (double)NAN),       "write NaN");
    ASSERT_TRUE(bvnr_write_float(w, "pos_inf",  64,  (double)INFINITY),  "write +Inf");
    ASSERT_TRUE(bvnr_write_float(w, "neg_inf",  64, -(double)INFINITY),  "write -Inf");
    ASSERT_TRUE(bvnr_write_finish(w), "finish");

    uint64_t n = bvnr_writer_bytes_written(w);
    bvnr_writer_destroy(w);
    ASSERT_TRUE(n > 0 && n < sizeof(outbuf), "writer produced bytes");

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;
    ASSERT_TRUE(do_parse(outbuf, n, &flags, &r),
                "special-float document parses cleanly");
    ASSERT_EQ_INT((int)ctx.ev_data_count, 3,
                  "three ev_data events (nan, +inf, -inf)");
    bvnr_reader_destroy(r);

    if (ctx.ev_data_count < 3) return;

    ASSERT_TRUE(strstr(ctx.val[0], "nan") != NULL ||
                strstr(ctx.val[0], "NaN") != NULL,
                "NaN: value token contains 'nan'");

    ASSERT_TRUE(strstr(ctx.val[1], "infinity") != NULL ||
                strstr(ctx.val[1], "inf")      != NULL ||
                strstr(ctx.val[1], "Inf")      != NULL,
                "+Inf: value token contains 'inf'");

    ASSERT_FALSE(ctx.val[1][0] == '-',
                 "+Inf: value token is not negative");

    ASSERT_TRUE(strstr(ctx.val[2], "infinity") != NULL ||
                strstr(ctx.val[2], "inf")      != NULL ||
                strstr(ctx.val[2], "Inf")      != NULL,
                "-Inf: value token contains 'inf'");

    ASSERT_TRUE(ctx.val[2][0] == '-' ||
                strstr(ctx.val[2], "-") != NULL,
                "-Inf: value token has a minus sign");
}

static void test_open_write_mem_and_bytes_written(void)
{
    printf("  test_open_write_mem_and_bytes_written...\n");

    uint8_t buf[512];
    memset(buf, 0xAA, sizeof(buf));

    bvnr_writer_t *w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create");
    if (!w) return;

    ASSERT_TRUE(bvnr_open_write_mem(w, buf, sizeof(buf), false, NULL),
                "bvnr_open_write_mem succeeds");

    bvnr_data_t hdr = {0};
    ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &hdr), "stream_start");
    ASSERT_TRUE(bvnr_write_string(w, "host",    "localhost"),    "write host");
    ASSERT_TRUE(bvnr_write_uint  (w, "port",  16, 8080u),        "write port");
    ASSERT_TRUE(bvnr_write_bool  (w, "tls",   true),             "write tls");
    ASSERT_TRUE(bvnr_write_float (w, "ratio", 64, 1.5),          "write ratio");
    ASSERT_TRUE(bvnr_write_finish(w), "finish");

    uint64_t written = bvnr_writer_bytes_written(w);
    bvnr_writer_destroy(w);

    ASSERT_TRUE(written > 0, "bytes_written > 0");
    ASSERT_TRUE(written < sizeof(buf), "bytes_written < buffer capacity");

    ASSERT_EQ_INT(buf[written], 0xAA,
                  "sentinel byte past written region is untouched");

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;
    ASSERT_TRUE(do_parse(buf, written, &flags, &r),
                "bytes written via open_write_mem are parseable");

    ASSERT_EQ_INT((int)ctx.ev_data_count, 4,
                  "4 ev_data events in the round-tripped document");
    bvnr_reader_destroy(r);

    memset(buf, 0xAA, sizeof(buf));
    w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create (pretty)");
    if (!w) return;
    ASSERT_TRUE(bvnr_open_write_mem(w, buf, sizeof(buf), true, NULL),
                "open_write_mem pretty=true");
    hdr = (bvnr_data_t){0};
    (void)bvnr_write_event(w, ev_stream_start, &hdr);
    (void)bvnr_write_uint(w, "n", 32, 99u);
    (void)bvnr_write_finish(w);
    uint64_t written_pretty = bvnr_writer_bytes_written(w);
    bvnr_writer_destroy(w);

    ASSERT_TRUE(written_pretty > 0, "pretty mode: bytes_written > 0");
    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse(buf, written_pretty, &flags, &r),
                "pretty-mode output is parseable");
    ASSERT_EQ_INT((int)ctx.ev_data_count, 1,
                  "pretty-mode: 1 ev_data event");
    bvnr_reader_destroy(r);
}

static void test_bvnf_base16_special_roundtrip(void)
{
    printf("  test_bvnf_base16_special_roundtrip...\n");

    uint8_t outbuf[2048];
    bvnr_writer_t *w = bvnr_writer_create();
    ASSERT_TRUE(w != NULL, "writer create");
    if (!w) return;
    ASSERT_TRUE(bvnr_open_write_mem(w, outbuf, sizeof(outbuf), false, NULL),
                "open_write_mem");

    bvnr_data_t hdr = {0};
    ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &hdr), "stream_start");

    bvn_float_t *f = bvn_float_alloc(256u);
    ASSERT_TRUE(f != NULL, "bvn_float_alloc");
    if (!f) { bvnr_writer_destroy(w); return; }

    bvn_float_set_nan(f);
    ASSERT_TRUE(bvnr_write_bvnf_base(w, "nan_val", f, 256u, 16u),
                "write NaN base16");

    bvn_float_set_inf(f, false);
    ASSERT_TRUE(bvnr_write_bvnf_base(w, "pos_inf", f, 256u, 16u),
                "write +Inf base16");

    bvn_float_set_inf(f, true);
    ASSERT_TRUE(bvnr_write_bvnf_base(w, "neg_inf", f, 256u, 16u),
                "write -Inf base16");

    bvn_float_from_double(f, 1.5);
    ASSERT_TRUE(bvnr_write_bvnf_base(w, "normal", f, 256u, 16u),
                "write 1.5 base16");

    ASSERT_TRUE(bvnr_write_finish(w), "finish");
    uint64_t n = bvnr_writer_bytes_written(w);
    bvn_float_free(f);
    bvnr_writer_destroy(w);

    ASSERT_TRUE(n > 0u, "bytes written > 0");

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;
    ASSERT_TRUE(do_parse(outbuf, n, &flags, &r),
                "base16 bvnf with NaN/+Inf/-Inf/normal must roundtrip");
    ASSERT_EQ_INT((int)ctx.ev_data_count, 4,
                  "four ev_data events in roundtripped document");

    if (ctx.ev_data_count >= 1)
        ASSERT_TRUE(strstr(ctx.val[0], "nan") != NULL ||
                    strstr(ctx.val[0], "NaN") != NULL,
                    "NaN: value token contains 'nan'");

    if (ctx.ev_data_count >= 2)
        ASSERT_TRUE(strstr(ctx.val[1], "inf") != NULL ||
                    strstr(ctx.val[1], "Inf") != NULL,
                    "+Inf: value token contains 'inf'");

    bvnr_reader_destroy(r);
}

static void test_stream_end_event(void)
{
    printf("  test_stream_end_event...\n");

    capture_t ctx = {0};
    bvnr_read_flags_t flags = {
        .on_verified = capture_verified,
        .on_error    = capture_error,
        .userdata    = &ctx,
    };
    bvnr_reader_t *r = NULL;

    ASSERT_TRUE(do_parse_str(".a = 1;\n", &flags, &r),
                "simple parse succeeds");
    ASSERT_EQ_INT((int)ctx.stream_end_count, 1,
                  "ev_stream_end fires exactly once on clean parse");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_TRUE(do_parse_str(".x = \"hello\";\n.y = 42;\n", &flags, &r),
                "multi-statement parse succeeds");
    ASSERT_EQ_INT((int)ctx.stream_end_count, 1,
                  "ev_stream_end fires exactly once for multi-statement stream");
    bvnr_reader_destroy(r);

    ctx = (capture_t){0};
    ASSERT_FALSE(do_parse_str(".a = !!invalid;\n", &flags, &r),
                 "invalid input fails");
    ASSERT_EQ_INT((int)ctx.stream_end_count, 0,
                  "ev_stream_end not fired on parse error");
    bvnr_reader_destroy(r);
}

int main(void)
{
    printf("Running bovnar_high_severity_test suite...\n\n");

    printf("Gaps 1+2: writer error accessors\n");
    test_writer_error_accessors();

    printf("\nGaps 3+4: reader max_* limits\n");
    test_reader_max_identifier_length();
    test_reader_max_string_length();
    test_reader_max_number_length();
    test_reader_max_symbol_length();
    test_reader_max_reference_length();
    test_reader_max_array_items();
    test_reader_max_text_bytes();
    test_reader_max_file_size();
    test_reader_max_struct_nesting();
    test_reader_max_array_nesting();

    printf("\nGap 5: recovery count\n");
    test_recovery_count();

    printf("\nGap 6: callback abort\n");
    test_callback_abort_verified();
    test_callback_abort_unverified();

    printf("\nGap 7: SI unit conversion APIs\n");
    test_si_units_compatible();
    test_si_unit_convert_factor();
    test_si_unit_dimension_vector();
    test_si_unit_reduce();

    printf("\nGap 8: unit_to_si_factor (affine)\n");
    test_si_unit_to_si_factor();

    printf("\nGap 9: array reader events\n");
    test_array_reader_events();

    printf("\nGap 10: octet-stream events\n");
    test_octet_stream_reader_events();
    test_octet_zero_length_chunk_rejected();

    printf("\nGap 11: string escape write roundtrip\n");
    test_string_vt_vf_escape_write();

    printf("\nGap 12: special float write\n");
    test_special_float_write();

    printf("\nGap 12: open_write_mem / bytes_written\n");
    test_open_write_mem_and_bytes_written();

    printf("\nGap 13: bvnf base-16 special float roundtrip\n");
    test_bvnf_base16_special_roundtrip();

    printf("\nFix: ev_stream_end event\n");
    test_stream_end_event();

    printf("\n");
    if (g_failures == 0) {
        printf("PASSED %d tests\n", g_tests);
        return 0;
    }
    fprintf(stderr, "FAILED %d of %d tests\n", g_failures, g_tests);
    return 1;
}

