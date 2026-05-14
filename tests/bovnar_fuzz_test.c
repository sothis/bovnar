#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_dom.h"

#define MAX_BUF      8192u
#define MAX_SEED     4096u
#define WALK_DEPTH     32u
#define N_UTIL_FUNCS   21u

static uint8_t      g_crash_buf[MAX_BUF + 2];
static size_t       g_crash_len;
static unsigned     g_crash_iter;
static const char  *g_crash_harness = "?";

static void sig_puts(int fd, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    write(fd, s, n);
}

static void sig_putu(int fd, unsigned long v)
{
    char buf[24];
    int  i = (int)sizeof(buf) - 1;
    buf[i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = (char)('0' + v % 10); v /= 10; } }
    sig_puts(fd, buf + i);
}

static void sig_puthex(int fd, uint8_t b)
{
    static const char h[] = "0123456789abcdef";
    char buf[3] = { h[b >> 4], h[b & 0xF], ' ' };
    write(fd, buf, 3);
}

static void crash_handler(int sig)
{
    int err = STDERR_FILENO;
    sig_puts(err, "\n[bvnr_fuzz CRASH] signal ");
    sig_putu(err, (unsigned)sig);
    sig_puts(err, " in harness '");
    sig_puts(err, g_crash_harness);
    sig_puts(err, "' at iteration ");
    sig_putu(err, g_crash_iter);
    sig_puts(err, "\nInput length: ");
    sig_putu(err, (unsigned long)g_crash_len);
    sig_puts(err, " bytes\n");

    size_t limit = g_crash_len < 256u ? g_crash_len : 256u;
    for (size_t i = 0; i < limit; i++) {
        sig_puthex(err, g_crash_buf[i]);
        if ((i & 15u) == 15u) write(err, "\n", 1);
    }
    write(err, "\n", 1);

    int fd = open("bvnr_crash_input.bin",
                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, g_crash_buf, g_crash_len);
        close(fd);
        sig_puts(err, "Reproducer saved to: bvnr_crash_input.bin\n");
    }
    _exit(1);
}

static void install_crash_handler(void)
{
    static const int sigs[] = { SIGSEGV, SIGABRT, SIGILL, SIGBUS, SIGFPE };
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    for (size_t i = 0; i < sizeof(sigs)/sizeof(sigs[0]); i++)
        sigaction(sigs[i], &sa, NULL);
}

static void set_crash_context(const char *harness, unsigned iter,
                              const uint8_t *buf, size_t len)
{
    g_crash_harness = harness;
    g_crash_iter    = iter;
    g_crash_len     = len < MAX_BUF ? len : MAX_BUF;
    memcpy(g_crash_buf, buf, g_crash_len);
}

typedef struct { uint64_t s[4]; } prng_t;

static inline uint64_t rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t prng_next(prng_t *p)
{
    uint64_t r = rotl64(p->s[1] * 5, 7) * 9;
    uint64_t t = p->s[1] << 17;
    p->s[2] ^= p->s[0];
    p->s[3] ^= p->s[1];
    p->s[1] ^= p->s[2];
    p->s[0] ^= p->s[3];
    p->s[2] ^= t;
    p->s[3]  = rotl64(p->s[3], 45);
    return r;
}

static void prng_seed(prng_t *p, uint64_t seed)
{
    for (int i = 0; i < 4; i++) {
        seed += UINT64_C(0x9e3779b97f4a7c15);
        uint64_t z = seed;
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        p->s[i] = z ^ (z >> 31);
    }
}

static uint64_t prng_range(prng_t *p, uint64_t lo, uint64_t hi)
{

    if (lo >= hi) return lo;
    return lo + prng_next(p) % (hi - lo + 1);
}

static const uint8_t INTERESTING_BYTES[] = {
    0x00, 0x01, 0x02, 0x09, 0x0a, 0x0d,
    0x1f, 0x20, 0x22, 0x24, 0x26, 0x2f,
    0x3c, 0x3e, 0x40, 0x5c, 0x7e, 0x7f,
    0x80, 0xbf, 0xc0, 0xc2, 0xc3, 0xef,
    0xf4, 0xf5, 0xfe, 0xff,

    0xbb, 0xb5, 0xb7, 0xb2, 0xb3,
};

static const uint16_t INTERESTING_WORDS[] = {
    0x0000, 0x0001, 0x007f, 0x0080, 0x00ff,
    0x7fff, 0x8000, 0xfffe, 0xffff,

    0xbbef, 0xbfbb,
};

typedef enum {
    MUT_BIT_FLIP_1 = 0,
    MUT_BIT_FLIP_2,
    MUT_BIT_FLIP_4,
    MUT_BYTE_FLIP,
    MUT_BYTE_SET_INTERESTING,
    MUT_WORD_SET_INTERESTING,
    MUT_ARITH_BYTE,
    MUT_INSERT_BYTE,
    MUT_DELETE_BYTE,
    MUT_CLONE_CHUNK,
    MUT_ZERO_CHUNK,
    MUT_SPLICE_SEED,
    MUT__COUNT
} mut_type_t;

typedef struct {
    uint8_t data[MAX_BUF + 2];
    size_t  len;
} mbuf_t;

static void mbuf_from_seed(mbuf_t *m, const uint8_t *seed, size_t seedlen)
{
    m->len = seedlen < MAX_SEED ? seedlen : MAX_SEED;
    if (m->len == 0) m->len = 1;
    memcpy(m->data, seed, m->len);
    m->data[m->len] = '\0';
}

static void mutate(mbuf_t *m, prng_t *rng,
                   const uint8_t *const *seeds, size_t *seed_lens, size_t nseed)
{
    if (m->len == 0) m->len = 1;

    mut_type_t mt = (mut_type_t)(prng_next(rng) % MUT__COUNT);
    size_t pos;

    switch (mt) {
    case MUT_BIT_FLIP_1: {
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        m->data[pos] ^= (uint8_t)(1u << (prng_next(rng) % 8));
        break;
    }
    case MUT_BIT_FLIP_2: {
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        uint8_t mask = (uint8_t)(3u << (prng_next(rng) % 7));
        m->data[pos] ^= mask;
        break;
    }
    case MUT_BIT_FLIP_4: {
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        m->data[pos] ^= (uint8_t)(0x0Fu << (prng_next(rng) % 5));
        break;
    }
    case MUT_BYTE_FLIP: {
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        m->data[pos] ^= 0xFF;
        break;
    }
    case MUT_BYTE_SET_INTERESTING: {
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        size_t idx = (size_t)(prng_next(rng) % (sizeof(INTERESTING_BYTES) / sizeof(INTERESTING_BYTES[0])));
        m->data[pos] = INTERESTING_BYTES[idx];
        break;
    }
    case MUT_WORD_SET_INTERESTING: {
        if (m->len < 2) {
            m->data[0] = INTERESTING_BYTES[prng_next(rng) % (sizeof(INTERESTING_BYTES) / sizeof(INTERESTING_BYTES[0]))];
            break;
        }
        pos = (size_t)prng_range(rng, 0, m->len - 2);
        size_t idx = (size_t)(prng_next(rng) % (sizeof(INTERESTING_WORDS) / sizeof(INTERESTING_WORDS[0])));
        uint16_t w = INTERESTING_WORDS[idx];
        m->data[pos]     = (uint8_t)(w & 0xFF);
        m->data[pos + 1] = (uint8_t)(w >> 8);
        break;
    }
    case MUT_ARITH_BYTE: {
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        int delta = (int)(prng_range(rng, 1, 35));
        if (prng_next(rng) & 1) delta = -delta;
        m->data[pos] = (uint8_t)((int)m->data[pos] + delta);
        break;
    }
    case MUT_INSERT_BYTE: {
        if (m->len >= MAX_BUF) {

            pos = (size_t)prng_range(rng, 0, m->len - 1);
            m->data[pos] = (uint8_t)prng_next(rng);
            break;
        }
        pos = (size_t)prng_range(rng, 0, m->len);
        memmove(m->data + pos + 1, m->data + pos, m->len - pos);
        m->data[pos] = (uint8_t)prng_next(rng);
        m->len++;
        break;
    }
    case MUT_DELETE_BYTE: {
        if (m->len <= 1) break;
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        memmove(m->data + pos, m->data + pos + 1, m->len - pos - 1);
        m->len--;
        break;
    }
    case MUT_CLONE_CHUNK: {

        size_t chunk = (size_t)prng_range(rng, 1, 32);
        if (chunk >= m->len) chunk = m->len;
        size_t src = (size_t)prng_range(rng, 0, m->len - chunk);
        size_t dst = (size_t)prng_range(rng, 0, m->len - 1);
        size_t copy = chunk < m->len - dst ? chunk : m->len - dst;
        memmove(m->data + dst, m->data + src, copy);
        break;
    }
    case MUT_ZERO_CHUNK: {
        size_t chunk = (size_t)prng_range(rng, 1, 16);
        pos = (size_t)prng_range(rng, 0, m->len - 1);
        size_t end = pos + chunk < m->len ? pos + chunk : m->len;
        memset(m->data + pos, 0, end - pos);
        break;
    }
    case MUT_SPLICE_SEED: {
        if (nseed == 0) break;
        size_t si = (size_t)(prng_next(rng) % nseed);
        size_t slen = seed_lens[si] < MAX_SEED ? seed_lens[si] : MAX_SEED;
        if (slen == 0) break;

        size_t cut_m = m->len / 2;
        size_t cut_s = slen / 2;
        size_t total = cut_m + (slen - cut_s);
        if (total > MAX_BUF) total = MAX_BUF;
        memmove(m->data + cut_m, seeds[si] + cut_s,
                total - cut_m < slen - cut_s ? total - cut_m : slen - cut_s);
        m->len = total;
        break;
    }
    case MUT__COUNT:
        break;
    }

    m->data[m->len] = '\0';
}

typedef struct { const uint8_t *data; size_t len; } seed_entry_t;

#define S(lit)     { (const uint8_t *)(lit), sizeof(lit) - 1 }
#define B(...)     { (const uint8_t[]){__VA_ARGS__}, \
                     sizeof((const uint8_t[]){__VA_ARGS__}) }

static const seed_entry_t CORPUS[] = {

    S(".x = 42;\n"),
    S(".x = -17;\n"),
    S(".x = 3.14;\n"),
    S(".x = \"hello world\";\n"),
    S(".x = true;\n"),
    S(".x = ;\n"),
    S(".x = $nan$;\n"),
    S(".x = $infinity$;\n"),
    S(".x = $-infinity$;\n"),
    S(".x = .5;\n"),
    S(".x = 123.;\n"),
    S(".x = 1e6;\n"),
    S(".x = -2.5e-3;\n"),
    S(".x = 007;\n"),

    S(".x = \"tab:\\there\";\n"),
    S(".x = \"nl\\nhere\";\n"),
    S(".x = \"q\\\"here\";\n"),
    S(".x = \"bs\\\\here\";\n"),
    S(".x = \"hello \" \"world\";\n"),

    S(".x = <uint:8> 255;\n"),
    S(".x = <uint:16> 65535;\n"),
    S(".x = <uint:32> 0;\n"),
    S(".x = <uint:64> 18446744073709551615;\n"),
    S(".x = <sint:8> -128;\n"),
    S(".x = <sint:8> 127;\n"),
    S(".x = <sint:32> -2147483648;\n"),
    S(".x = <sint:64> 9223372036854775807;\n"),
    S(".x = <float:16> 1.0;\n"),
    S(".x = <float:32> 3.14;\n"),
    S(".x = <float:64> 1e100;\n"),
    S(".x = <float:64> $nan$;\n"),
    S(".x = <float:128> 1.0;\n"),
    S(".x = <utf8> \"text\";\n"),

    S(".x = <uint:_16> \"ff\";\n"),
    S(".x = <uint:_2> \"1010\";\n"),
    S(".x = <sint:_16> \"-1f\";\n"),

    S(".x = <float:64,s> 2.5;\n"),
    S(".x = <float:64,no_unit> 1.0;\n"),
    S(".x = <uint:64,Ki-B> 1;\n"),
    S(".x = <uint:64,Mi-B> 1;\n"),

    S(".x = <float:64,m/s> 9.81;\n"),
    S(".x = <float:64,m/s\xC2\xB2> 9.81;\n"),
    S(".x = <float:64,k-g\xC2\xB7m/s\xC2\xB2> 9.81;\n"),
    S(".x = <float:64,k-g/m\xC2\xB3> 7800.0;\n"),
    S(".x = <float:64,m*s> 1.0;\n"),
    S(".x = <float:64,k-g\xC2\xB7m\xC2\xB2/s\xC2\xB2> 100.0;\n"),
    S(".x = <float:64,V/m> 150.0;\n"),

    S(".a = [1, 2, 3];\n"),
    S(".a = [1,2,3]/[4,5,6];\n"),
    S(".a = [,1,,2,];\n"),
    S(".a = [[1,2],[3,4]];\n"),
    S(".a = [\"a\", \"b\", \"c\"];\n"),
    S(".a = [<sint:16> 1, <sint:16> , <sint:16> 3];\n"),
    S(".a = [1,2,3]/[4,5,6]/[7,8,9];\n"),

    S(".s = {};\n"),
    S(".s = { .x = 1; .y = 2; };\n"),
    S(".s = { .inner = { .val = 42; }; };\n"),
    S(".a = [{.name = \"Alice\"; .age = 30;},{.name = \"Bob\"; .age = 25;}];\n"),

    S(".host = \"localhost\";\n.ref = &.host;\n"),
    S(".status = ok;\n"),
    S(".flags = [red, green, blue];\n"),

    S("# comment\n.x = 1;\n"),
    S(".x = 1; # inline\n"),

    S("\xEF\xBB\xBF.x = 1;\n"),

    B('.','b',' ','=',' ',
      0x00,
      0x01, 0x05,0x00, 'h','e','l','l','o',
      0x01, 0x03,0x00, 'b','y','e',
      0x00,
      ';','\n'),

    S(". = 42;\n"),
    S(".x = <uint:8> 256;\n"),
    S(".x = <uint:8> -1;\n"),
    S(".x = <float:8> 1.0;\n"),
    S(".x = <float:64,_2> 1.0;\n"),
    S(".x = \"\\q\";\n"),
    S(".x = <utf8> 42;\n"),
    S(".x = [[1,2],[3,4,5]];\n"),
    S(".x = <float:64,m//s> 1.0;\n"),
    S(".x = };\n"),

    S(".x = <float:64,m*s*k-g*A*K*mol*cd*b*Hz> 1.0;\n"),

    S(""),
    S("   \n\t  \n"),
    S(".a=1;.b=2;.c=3;.d=4;.e=5;.f=6;\n"),
    S(".x = <uint:32,no_unit> 42;\n"),
    S(".x = <uint:64> 0;\n.y = <sint:64> -9223372036854775808;\n"),
};

#undef S
#undef B

#define N_CORPUS (sizeof(CORPUS) / sizeof(CORPUS[0]))

typedef struct {
    uint32_t unverified;
    uint32_t verified;
    bool     first_event;
    uint32_t abort_after;
    bool     aborted;
} reader_ctx_t;

static bool reader_on_unverified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    reader_ctx_t *ctx = ud;
    ctx->unverified++;

    if (!ctx->first_event) {
        if (ev != ev_stream_start) __builtin_trap();
        ctx->first_event = true;
    }
    (void)d;
    return true;
}

static bool reader_on_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    reader_ctx_t *ctx = ud;
    ctx->verified++;
    if (ctx->abort_after && ctx->verified >= ctx->abort_after) {
        ctx->aborted = true;
        return false;
    }
    (void)ev; (void)d;
    return true;
}

static void reader_on_error(void *ud, error_code_t err,
                            uint64_t line, uint64_t col,
                            uint32_t byte, uint64_t off)
{
    if ((unsigned)err >= (unsigned)BVN_ERROR_COUNT) __builtin_trap();
    (void)ud; (void)line; (void)col; (void)byte; (void)off;
}

static void harness_reader(const uint8_t *data, size_t len,
                           uint8_t cfg, uint8_t abort_n)
{
    reader_ctx_t ctx = {
        .abort_after = (cfg & 8u) ? (uint32_t)(abort_n + 1u) : 0u,
    };

    bvnr_read_flags_t flags;
    memset(&flags, 0, sizeof(flags));

    switch (cfg & 3u) {
    case 0:
        flags.max_identifier_length = 255;
        flags.max_string_length     = 65535;
        flags.max_number_length     = 255;
        flags.max_symbol_length     = 255;
        flags.max_reference_length  = 65535;
        flags.max_struct_nesting    = 255;
        flags.max_array_nesting     = 255;
        break;
    case 1:
        flags.max_identifier_length = 4;
        flags.max_string_length     = 8;
        flags.max_number_length     = 4;
        flags.max_symbol_length     = 4;
        flags.max_reference_length  = 16;
        flags.max_array_items       = 3;
        flags.max_text_bytes        = 32;
        flags.max_file_size         = 64;
        flags.max_struct_nesting    = 4;
        flags.max_array_nesting     = 4;
        break;
    case 2:
        break;
    case 3:
        flags.max_identifier_length = 255;
        flags.max_string_length     = 16;
        flags.max_number_length     = 255;
        flags.max_symbol_length     = 255;
        flags.max_reference_length  = 16;
        flags.max_array_items       = 64;
        flags.max_text_bytes        = 256;
        flags.max_struct_nesting    = 8;
        flags.max_array_nesting     = 8;
        break;
    }

    flags.userdata         = &ctx;
    flags.on_unverified    = reader_on_unverified;
    flags.on_verified      = reader_on_verified;
    flags.continue_on_error = (cfg >> 2) & 1u;
    flags.on_error         = reader_on_error;

    bvnr_reader_t *r = bvnr_reader_create();
    if (!r) return;

    if (bvnr_open_read_mem(r, data, (uint32_t)(len > UINT32_MAX ? UINT32_MAX : len),
                           NULL, 0, &flags)) {
        (void)bvnr_read(r);
    }

    error_code_t err = bvnr_reader_get_error(r);
    if ((unsigned)err >= (unsigned)BVN_ERROR_COUNT) __builtin_trap();

    bvnr_reader_destroy(r);
}

static void dom_walk_node(const bvn_dom_node_t *node, unsigned depth);

static void dom_exercise_node(const bvn_dom_node_t *node)
{
    if (!node) return;

    bvn_dom_type_t dtype = bvn_dom_node_type(node);
    if ((unsigned)dtype > (unsigned)BVN_DOM_OCTET_STREAM) __builtin_trap();

    (void)bvn_dom_is_null(node);

    value_type_spec_t vts = bvn_dom_get_value_type(node);
    if ((unsigned)vts.family >= (unsigned)vt_illegal + 1u) __builtin_trap();

    value_unit_t vu = bvn_dom_get_unit(node);
    if (vu.num_components > BVNR_MAX_UNIT_COMPONENTS + 1u) __builtin_trap();

    char ubuf[256];
    (void)bvn_dom_get_unit_string(node, ubuf, sizeof(ubuf));
    (void)bvn_dom_get_value_in_base_units(node);

    { int64_t  v; (void)bvn_dom_get_i64(node, &v); }
    { uint64_t v; (void)bvn_dom_get_u64(node, &v); }
    { double   v; (void)bvn_dom_get_float(node, &v); }
    {
        const char *s; uint32_t n;
        if (bvn_dom_get_string(node, &s, &n) && s && n)
            (void)s[n - 1];
    }
    {
        const char *s; uint32_t n;
        if (bvn_dom_get_symbol(node, &s, &n) && s && n)
            (void)s[n - 1];
    }
    {
        const char *s; uint32_t n;
        if (bvn_dom_get_reference(node, &s, &n) && s && n)
            (void)s[n - 1];
    }
    {
        const uint8_t *b; uint32_t n;
        if (bvn_dom_get_octets(node, &b, &n) && b && n)
            (void)b[n - 1];
    }

    uint32_t acnt = bvn_dom_array_count(node);
    (void)bvn_dom_array_dims(node);
    if (acnt > 0) {
        (void)bvn_dom_array_at(node, 0);
        (void)bvn_dom_array_at(node, acnt - 1);
    }
    (void)bvn_dom_array_at(node, acnt);
    (void)bvn_dom_array_at(node, UINT32_MAX);

    (void)bvn_dom_struct_count(node);
    (void)bvn_dom_struct_entries(node);
}

static void dom_walk_node(const bvn_dom_node_t *node, unsigned depth)
{
    if (!node || depth >= WALK_DEPTH) return;

    dom_exercise_node(node);

    bvn_dom_type_t dtype = bvn_dom_node_type(node);

    if (dtype == BVN_DOM_STRUCT) {
        uint32_t cnt = bvn_dom_struct_count(node);
        const bvn_dom_entry_t *entries = bvn_dom_struct_entries(node);
        for (uint32_t i = 0; i < cnt; i++) {
            if (entries[i].key) (void)strlen(entries[i].key);
            dom_walk_node(entries[i].value, depth + 1);
            if (entries[i].key)
                (void)bvn_dom_struct_get(node, entries[i].key);
        }
        (void)bvn_dom_struct_get(node, "");
        (void)bvn_dom_struct_get(node, "___no_such_key___");

    } else if (dtype == BVN_DOM_ARRAY) {
        uint32_t cnt = bvn_dom_array_count(node);
        uint32_t limit = cnt < 64u ? cnt : 64u;
        for (uint32_t i = 0; i < limit; i++)
            dom_walk_node(bvn_dom_array_at(node, i), depth + 1);
    }
}

static void harness_dom(const uint8_t *data, size_t len)
{
    uint32_t plen = len > UINT32_MAX ? UINT32_MAX : (uint32_t)len;
    bvn_dom_doc_t *doc = bvn_dom_parse(data, plen);
    if (!doc) return;

    if ((unsigned)bvn_dom_doc_get_parse_error(doc) >= (unsigned)BVN_ERROR_COUNT)
        __builtin_trap();

    uint32_t cnt = bvn_dom_doc_count(doc);
    const bvn_dom_entry_t *entries = bvn_dom_doc_entries(doc);

    for (uint32_t i = 0; i < cnt; i++) {
        if (entries[i].key) (void)strlen(entries[i].key);
        dom_walk_node(entries[i].value, 0);

        if (entries[i].key) {
            char path[512];
            snprintf(path, sizeof(path), ".%s", entries[i].key);
            (void)bvn_dom_lookup(doc, path);
        }
    }

    (void)bvn_dom_lookup(doc, "");
    (void)bvn_dom_lookup(doc, ".");
    (void)bvn_dom_lookup(doc, "no_dot");

    bvn_dom_doc_destroy(doc);
}

static value_type_spec_t util_make_type(uint8_t fam_byte, uint8_t param_byte)
{
    static const value_type_family_t fams[] = {
        vt_plain, vt_utf8, vt_sint, vt_uint,
        vt_float
    };
    static const uint32_t widths[] = { 0, 8, 16, 32, 64, 128, 255 };
    static const uint32_t bases[]  = { 0, 2, 8, 10, 16, 36 };
    return (value_type_spec_t){
        .family = fams[fam_byte % 5u],
        .width  = widths[(param_byte & 7u) % 7u],
        .base   = bases[((param_byte >> 3) & 7u) % 6u],
    };
}

static void harness_utils(const uint8_t *data, size_t len,
                          uint8_t selector, uint8_t aux_a, uint8_t aux_b,
                          bool verbose)
{

    char scratch[MAX_BUF + 1];
    size_t slen = len < MAX_BUF ? len : MAX_BUF;
    memcpy(scratch, data, slen);
    scratch[slen] = '\0';

    static const char *const util_names[N_UTIL_FUNCS] = {
        "bvn_validate_identifier",
        "bvn_validate_symbol",
        "bvn_validate_reference",
        "bvn_validate_number",
        "bvn_validate_string",
        "bvn_is_special_number_string",
        "bvn_validate_digits_for_base",
        "bvn_validate_number_in_base",
        "bvn_validate_uint_range",
        "bvn_validate_sint_range",
        "bvn_parse_unit / unit helpers",
        "bvn_parse_int64",
        "bvn_parse_uint64",
        "bvn_parse_double",
        "bvn_parse_double_in_base",
        "bvn_looks_like_double",
        "bvn_format_uint64",
        "bvn_format_int64",
        "bvn_format_double",
        "bvn_char_to_digit / bvn_min_digits_for_type",
        "bvn_parse_type_annotation",
    };

    uint8_t op = selector % N_UTIL_FUNCS;
    if (verbose) {
        printf("[utils] %s selector=%u aux_a=%u aux_b=%u len=%zu\n",
               util_names[op], op, aux_a, aux_b, len);
    }

    static const uint32_t bases_u[]  = { 0, 2, 8, 10, 16, 36, 62, 64, 85, 255 };
    static const uint32_t widths_u[] = { 0, 8, 16, 32, 64, 128 };

    switch (op) {

    case 0: (void)bvn_validate_identifier(scratch); break;
    case 1: (void)bvn_validate_symbol(scratch); break;
    case 2: (void)bvn_validate_reference(scratch); break;
    case 3: (void)bvn_validate_number(scratch); break;
    case 4: (void)bvn_validate_string((const uint8_t *)data, len); break;
    case 5: (void)bvn_is_special_number_string(scratch); break;
    case 6: {
        uint32_t base = bases_u[aux_a % 10u];
        (void)bvn_validate_digits_for_base(scratch, base);
        break;
    }
    case 7: {
        uint32_t base = bases_u[aux_a % 10u];
        (void)bvn_validate_number_in_base(scratch, base);
        break;
    }
    case 8: {
        uint32_t w = widths_u[aux_a % 6u];
        uint32_t b = bases_u[aux_b % 10u];
        (void)bvn_validate_uint_range(scratch, w, b);
        break;
    }
    case 9: {
        uint32_t w = widths_u[aux_a % 6u];
        uint32_t b = bases_u[aux_b % 10u];
        (void)bvn_validate_sint_range(scratch, w, b);
        break;
    }
    case 10: {
        bool ok = false;
        value_unit_t u = bvn_parse_unit((const uint8_t *)scratch, &ok);
        if (ok) {
            if (u.num_components > BVNR_MAX_UNIT_COMPONENTS + 1u)
                __builtin_trap();
            char buf[256];
            int32_t r = bvn_unit_to_string(u, buf, sizeof(buf));
            if (r >= 0 && (size_t)r >= sizeof(buf)) __builtin_trap();
            (void)bvn_unit_prefix_factor(u);
            (void)bvn_unit_prefix_factor(u);
            (void)bvn_unit_prefix_exponent(u);
        }
        break;
    }
    case 11: {
        value_type_spec_t vts = util_make_type(aux_a, aux_b);
        int64_t out = 0;
        (void)bvn_parse_int64(scratch, vts, &out);
        break;
    }
    case 12: {
        value_type_spec_t vts = util_make_type(aux_a, aux_b);
        uint64_t out = 0;
        (void)bvn_parse_uint64(scratch, vts, &out);
        break;
    }
    case 13: {
        value_type_spec_t vts = util_make_type(aux_a, aux_b);
        double out = 0.0;
        (void)bvn_parse_double(scratch, vts, &out);
        break;
    }
    case 14: {
        static const uint32_t blist[] = { 2, 10, 16, 36, 64, 85 };
        uint32_t b = blist[aux_a % 6u];
        double out = 0.0;
        (void)bvn_parse_double_in_base(scratch, b, &out);
        break;
    }
    case 15: (void)bvn_looks_like_double(scratch); break;
    case 16: {
        uint64_t val = 0;
        memcpy(&val, data, slen < 8u ? slen : 8u);
        static const uint32_t bl[] = { 2, 8, 10, 16, 36, 62, 64, 85 };
        static const uint32_t ml[] = { 0, 1, 4, 8, 16 };
        char buf[1024];
        int32_t r = bvn_format_uint64(buf, sizeof(buf),
                                      val, bl[aux_a % 8u], ml[aux_b % 5u]);
        if (r >= 0 && (size_t)r >= sizeof(buf)) __builtin_trap();
        break;
    }
    case 17: {
        int64_t val = 0;
        memcpy(&val, data, slen < 8u ? slen : 8u);
        static const uint32_t bl[] = { 2, 8, 10, 16 };
        char buf[256];
        int32_t r = bvn_format_int64(buf, sizeof(buf),
                                     val, bl[aux_a % 4u], aux_b % 9u);
        if (r >= 0 && (size_t)r >= sizeof(buf)) __builtin_trap();
        break;
    }
    case 18: {
        double val = 0.0;
        memcpy(&val, data, slen < 8u ? slen : 8u);
        value_type_spec_t vts = util_make_type(aux_a, aux_b);
        char buf[256];
        int32_t r = bvn_format_double(buf, sizeof(buf), val, vts);
        if (r >= 0 && (size_t)r >= sizeof(buf)) __builtin_trap();
        break;
    }
    case 19: {
        static const uint32_t bl[] = { 2, 8, 10, 16, 36, 62, 85 };
        for (size_t i = 0; i < slen && i < 16u; i++)
            for (size_t b = 0; b < 7u; b++)
                (void)bvn_char_to_digit((uint32_t)(uint8_t)scratch[i], bl[b]);
        value_type_spec_t vts = util_make_type(aux_a, aux_b);
        (void)bvn_min_digits_for_type(vts);
        break;
    }
    case 20: {
        bool type_ok = false, unit_ok = false, unit_too_long = false;
        value_unit_t out_unit;
        memset(&out_unit, 0, sizeof(out_unit));
        uint8_t unit_buf[256];
        uint8_t unit_buf_len = 0;
        value_type_spec_t vts = bvn_parse_type_annotation(
            (const uint8_t *)data,
            (uint32_t)(len > UINT32_MAX ? UINT32_MAX : len),
            &type_ok, &unit_ok, &unit_too_long,
            &out_unit, unit_buf, &unit_buf_len);
        if (type_ok && (unsigned)vts.family >= (unsigned)vt_illegal + 1u)
            __builtin_trap();
        if (unit_ok && out_unit.num_components > BVNR_MAX_UNIT_COMPONENTS + 1u)
            __builtin_trap();
        break;
    }
    }
}

static const uint8_t READER_CFGS[] = {
    0x00,
    0x04,
    0x01,
    0x05,
    0x02,
    0x06,
    0x03,
    0x09,
};
#define N_READER_CFGS (sizeof(READER_CFGS) / sizeof(READER_CFGS[0]))

typedef enum { HARNESS_READER, HARNESS_DOM, HARNESS_UTILS, HARNESS_ALL } harness_sel_t;

typedef struct {
    harness_sel_t harness;
    size_t        iterations;
    uint64_t      prng_seed;
    bool          verbose;
} run_opts_t;

static void run_seeds(const run_opts_t *opts)
{
    if (opts->verbose)
        printf("[seeds] running %zu seeds through all harnesses\n", N_CORPUS);

    for (size_t i = 0; i < N_CORPUS; i++) {
        const uint8_t *d = CORPUS[i].data;
        size_t         n = CORPUS[i].len;

        if (opts->harness == HARNESS_READER || opts->harness == HARNESS_ALL) {
            for (size_t c = 0; c < N_READER_CFGS; c++) {
                if (opts->verbose)
                    printf("[reader] seed=%zu cfg=0x%02x len=%zu\n",
                           i, READER_CFGS[c], n);
                set_crash_context("reader", (unsigned)i, d, n);
                harness_reader(d, n, READER_CFGS[c], (uint8_t)(i & 0xFF));
            }
        }
        if (opts->harness == HARNESS_DOM || opts->harness == HARNESS_ALL) {
            if (opts->verbose)
                printf("[dom] seed=%zu len=%zu\n", i, n);
            set_crash_context("dom", (unsigned)i, d, n);
            harness_dom(d, n);
        }
        if (opts->harness == HARNESS_UTILS || opts->harness == HARNESS_ALL) {
            for (uint8_t sel = 0; sel < N_UTIL_FUNCS; sel++) {
                set_crash_context("utils", (unsigned)i, d, n);
                harness_utils(d, n, sel, (uint8_t)(i & 0xFF), 0, opts->verbose);
            }
        }
    }
}

static void run_mutations(const run_opts_t *opts)
{
    prng_t rng;
    prng_seed(&rng, opts->prng_seed);

    const uint8_t *seed_ptrs[N_CORPUS];
    size_t         seed_lens[N_CORPUS];
    for (size_t i = 0; i < N_CORPUS; i++) {
        seed_ptrs[i] = CORPUS[i].data;
        seed_lens[i] = CORPUS[i].len;
    }

    mbuf_t mbuf;

    for (size_t iter = 0; iter < opts->iterations; iter++) {

        size_t si = (size_t)(prng_next(&rng) % N_CORPUS);
        mbuf_from_seed(&mbuf, CORPUS[si].data, CORPUS[si].len);

        size_t nmut = (size_t)(prng_range(&rng, 1, 4));
        for (size_t m = 0; m < nmut; m++)
            mutate(&mbuf, &rng, seed_ptrs, seed_lens, N_CORPUS);

        uint8_t cfg     = READER_CFGS[iter % N_READER_CFGS];
        uint8_t abort_n = (uint8_t)(prng_next(&rng) & 0xFF);
        uint8_t sel     = (uint8_t)(iter % N_UTIL_FUNCS);
        uint8_t aux_a   = (uint8_t)(prng_next(&rng) & 0xFF);
        uint8_t aux_b   = (uint8_t)(prng_next(&rng) & 0xFF);

        if (opts->harness == HARNESS_READER || opts->harness == HARNESS_ALL) {
            if (opts->verbose)
                printf("[reader] iter=%zu cfg=0x%02x abort_n=%u len=%zu\n",
                       iter, cfg, abort_n, mbuf.len);
            set_crash_context("reader", (unsigned)iter, mbuf.data, mbuf.len);
            harness_reader(mbuf.data, mbuf.len, cfg, abort_n);
        }
        if (opts->harness == HARNESS_DOM || opts->harness == HARNESS_ALL) {
            if (opts->verbose)
                printf("[dom] iter=%zu len=%zu\n", iter, mbuf.len);
            set_crash_context("dom", (unsigned)iter, mbuf.data, mbuf.len);
            harness_dom(mbuf.data, mbuf.len);
        }
        if (opts->harness == HARNESS_UTILS || opts->harness == HARNESS_ALL) {
            set_crash_context("utils", (unsigned)iter, mbuf.data, mbuf.len);
            harness_utils(mbuf.data, mbuf.len, sel, aux_a, aux_b, opts->verbose);
        }

        if (opts->verbose && (iter + 1) % 500 == 0) {
            printf("[fuzz] %zu / %zu iterations done\n",
                   iter + 1, opts->iterations);
            fflush(stdout);
        }
    }
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --harness  reader|dom|utils|all  (default: all)\n"
        "  --iterations N                   mutation rounds (default: 5000)\n"
        "  --seed N                         PRNG seed; 0=time (default: 1)\n"
        "  --verbose                        print progress and API calls being tested\n"
        "\nEnvironment:\n"
        "  BVNR_FUZZ_ITERATIONS   override --iterations\n"
        "  BVNR_FUZZ_SEED         override --seed\n",
        argv0);
}

int main(int argc, char **argv)
{
    install_crash_handler();

    run_opts_t opts = {
        .harness    = HARNESS_ALL,
        .iterations = 5000,
        .prng_seed  = 1,
        .verbose    = false,
    };

    const char *env_iter = getenv("BVNR_FUZZ_ITERATIONS");
    const char *env_seed = getenv("BVNR_FUZZ_SEED");
    if (env_iter) opts.iterations = (size_t)strtoul(env_iter, NULL, 10);
    if (env_seed) opts.prng_seed  = strtoull(env_seed, NULL, 10);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--harness") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "reader") == 0) opts.harness = HARNESS_READER;
            else if (strcmp(argv[i], "dom")    == 0) opts.harness = HARNESS_DOM;
            else if (strcmp(argv[i], "utils")  == 0) opts.harness = HARNESS_UTILS;
            else if (strcmp(argv[i], "all")    == 0) opts.harness = HARNESS_ALL;
            else { fprintf(stderr, "unknown harness: %s\n", argv[i]); return 2; }
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            opts.iterations = (size_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            opts.prng_seed = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = true;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (opts.prng_seed == 0)
        opts.prng_seed = (uint64_t)time(NULL);

    printf("[bvnr_fuzz] harness=%s  iterations=%zu  seed=%llu\n",
           opts.harness == HARNESS_READER ? "reader" :
           opts.harness == HARNESS_DOM    ? "dom"    :
           opts.harness == HARNESS_UTILS  ? "utils"  : "all",
           opts.iterations,
           (unsigned long long)opts.prng_seed);
    fflush(stdout);

    run_seeds(&opts);
    run_mutations(&opts);

    printf("[bvnr_fuzz] PASS  %zu seed runs + %zu mutation iterations\n",
           N_CORPUS, opts.iterations);
    return 0;
}
