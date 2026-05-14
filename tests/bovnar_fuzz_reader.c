#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"

static void assert_valid_error(error_code_t e)
{

    if ((unsigned)e >= (unsigned)BVN_ERROR_COUNT)
        __builtin_trap();
}

typedef struct {
    uint32_t     unverified_events;
    uint32_t     verified_events;
    uint32_t     stream_begin_count;
    bool         first_event_seen;

    uint32_t     abort_after;
    bool         aborted;
} fuzz_ctx_t;

static bool on_unverified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    fuzz_ctx_t *ctx = ud;
    ctx->unverified_events++;

    if (!ctx->first_event_seen) {
        if (ev != ev_stream_start)
            __builtin_trap();
        ctx->first_event_seen = true;
    }
    if (ev == ev_stream_start)
        ctx->stream_begin_count++;

    if (ctx->stream_begin_count > 1)
        __builtin_trap();

    (void)d;
    return true;
}

static bool on_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    fuzz_ctx_t *ctx = ud;
    ctx->verified_events++;

    if (d && (unsigned)d->type >= (unsigned)token_is_unknown + 1u) {

    }

    if (ctx->abort_after && ctx->verified_events >= ctx->abort_after) {
        ctx->aborted = true;
        return false;
    }

    (void)ev;
    return true;
}

static void on_error(void *ud, error_code_t err,
                     uint64_t line, uint64_t col,
                     uint32_t byte_val, uint64_t offset)
{
    assert_valid_error(err);
    (void)ud; (void)line; (void)col; (void)byte_val; (void)offset;
}

typedef enum {
    LIMITS_DEFAULT = 0,
    LIMITS_TIGHT   = 1,
    LIMITS_ZERO    = 2,
    LIMITS_MIXED   = 3,
} limit_profile_t;

static void apply_limits(bvnr_read_flags_t *f, limit_profile_t p,
                         bool file_cap, uint32_t payload_len)
{
    switch (p) {
    case LIMITS_DEFAULT:
        f->max_identifier_length = 255;
        f->max_string_length     = 65535;
        f->max_number_length     = 255;
        f->max_symbol_length     = 255;
        f->max_reference_length  = 65535;
        f->max_array_items       = 0;
        f->max_text_bytes        = 0;
        f->max_file_size         = 0;
        f->max_struct_nesting    = 255;
        f->max_array_nesting     = 255;
        break;
    case LIMITS_TIGHT:
        f->max_identifier_length = 4;
        f->max_string_length     = 8;
        f->max_number_length     = 4;
        f->max_symbol_length     = 4;
        f->max_reference_length  = 16;
        f->max_array_items       = 3;
        f->max_text_bytes        = 32;
        f->max_file_size         = 64;
        f->max_struct_nesting    = 4;
        f->max_array_nesting     = 4;
        break;
    case LIMITS_ZERO:

        memset(f, 0, sizeof(*f));

        break;
    case LIMITS_MIXED:
        f->max_identifier_length = 255;
        f->max_string_length     = 16;
        f->max_number_length     = 255;
        f->max_symbol_length     = 255;
        f->max_reference_length  = 16;
        f->max_array_items       = 256;
        f->max_text_bytes        = 256;
        f->max_file_size         = 0;
        f->max_struct_nesting    = 8;
        f->max_array_nesting     = 8;
        break;
    }

    if (file_cap && payload_len > 1)
        f->max_file_size = payload_len / 2 + 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    uint8_t config     = data[0];
    uint8_t abort_seed = data[1];
    const uint8_t *payload = data + 2;
    uint32_t plen = (uint32_t)(size - 2);

    limit_profile_t profile  = (limit_profile_t)(config & 0x03u);
    bool continue_on_error   = (config >> 2) & 1u;
    bool abort_mode          = (config >> 3) & 1u;
    bool file_cap            = (config >> 4) & 1u;
    bool no_unverified_cb    = (config >> 5) & 1u;
    bool no_verified_cb      = (config >> 6) & 1u;

    fuzz_ctx_t ctx = {
        .abort_after = abort_mode ? (uint32_t)(abort_seed + 1) : 0u,
    };

    bvnr_read_flags_t flags;
    memset(&flags, 0, sizeof(flags));
    apply_limits(&flags, profile, file_cap, plen);

    flags.userdata         = &ctx;
    flags.on_unverified    = no_unverified_cb ? NULL : on_unverified;
    flags.on_verified      = no_verified_cb   ? NULL : on_verified;
    flags.continue_on_error = continue_on_error;
    flags.on_error         = on_error;

    bvnr_reader_t *r = bvnr_reader_create();
    if (!r) return 0;

    bool opened = bvnr_open_read_mem(r, payload, plen, NULL, 0, &flags);
    if (opened) {
        (void)bvnr_read(r);
    }

    error_code_t err = bvnr_reader_get_error(r);
    assert_valid_error(err);

    uint64_t line   = bvnr_reader_get_error_line(r);
    uint64_t col    = bvnr_reader_get_error_column(r);
    uint64_t offset = bvnr_reader_get_error_offset(r);

    if (err != error_none && line > 0) {
        if (offset + 1 < line - 1)
            __builtin_trap();
    }
    (void)col; (void)offset;

    uint64_t recovery_count = bvnr_reader_get_recovery_count(r);
    if (continue_on_error && !ctx.aborted) {

        (void)recovery_count;
    }

    if (err == error_none && !ctx.aborted &&
        !no_unverified_cb && !no_verified_cb) {
        if (ctx.unverified_events != ctx.verified_events)
            __builtin_trap();
    }

    bvnr_reader_destroy(r);
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
