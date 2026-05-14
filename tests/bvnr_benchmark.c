#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bovnar.h"

typedef enum {
    PROFILE_SCALARS,
    PROFILE_TYPED,
    PROFILE_STRUCTS,
    PROFILE_ARRAYS,
    PROFILE_UNITS,
    PROFILE_MIXED,
    PROFILE_COUNT
} profile_t;

static const char *profile_names[PROFILE_COUNT] = {
    "scalars", "typed", "structs", "arrays", "units", "mixed"
};

typedef struct {
    profile_t profile;
    size_t    payload_size;
    size_t    num_assignments;
    size_t    num_events;
    double    elapsed_sec;
    double    cpu_sec;
    uint64_t  recovery_count;
} bmark_result_t;

typedef struct {
    bool     profiles[PROFILE_COUNT];
    size_t   sizes[64];
    uint32_t num_sizes;
    uint32_t iterations;
    uint32_t warmup;
    bool     verbose;
    bool     json;
    bool     min_overhead;
} bmark_cfg_t;

static bmark_cfg_t cfg = {
    .profiles   = {true, true, true, true, true, true},
    .sizes      = {1024, 4096, 16384, 65536},
    .num_sizes  = 4,
    .iterations = 100,
    .warmup     = 10,
    .verbose    = false,
    .json       = false,
    .min_overhead = false,
};

typedef struct timespec wall_clock_t;

static wall_clock_t timer_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

static double timer_sec(const wall_clock_t *start, const wall_clock_t *end)
{
    return (double)(end->tv_sec - start->tv_sec)
         + (double)(end->tv_nsec - start->tv_nsec) * 1e-9;
}

static double cpu_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct {
    size_t       event_count;
    size_t       assign_count;
    const bool  *min_mode;
    profile_t    active_profile;
    size_t       payload_id;
} counter_ctx_t;

static bool event_counter(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
    counter_ctx_t *ctx = ud;
    ctx->event_count++;

    if (ev == ev_assignment_start)
        ctx->assign_count++;

    if (ctx->min_mode && *ctx->min_mode)
        return true;

    (void)d;
    return true;
}

static size_t gen_scalars(uint8_t *buf, size_t cap, size_t *out_assignments)
{
    static const char *templates[] = {
        ".k%u=%lld;\n",
        ".k%u=%lld;\n",
        ".k%u=%\"s\";\n",
        ".k%u=true;\n",
    };
    static const int64_t values[] = {0, 42, -17, 1000000, 255, 9999};

    size_t pos   = 0;
    size_t count = 0;

    for (size_t i = 0; ; i++) {
        int tmpl_idx = (int)(i % 4);
        int64_t val  = values[i % 6];
        char buf32[64];

        int n;
        if (tmpl_idx == 2) {

            static const char *strs[] = {"hello","world","test","value","key","data"};
            n = snprintf(buf32, sizeof(buf32), ".k%u=\"%s\";\n",
                         (unsigned)i, strs[i % 6]);
        } else {
            n = snprintf(buf32, sizeof(buf32), templates[tmpl_idx],
                         (unsigned)i, (long long)val);
        }

        if (n < 0 || (size_t)n >= cap - pos - 1)
            break;

        memcpy(buf + pos, buf32, (size_t)n);
        pos   += (size_t)n;
        count++;
    }

    *out_assignments = count;
    return pos;
}

static size_t gen_typed(uint8_t *buf, size_t cap, size_t *out_assignments)
{
    static const char *templates[] = {
           ".k%u=<uint:8>%llu;\n",
                               ".k%u=<uint:16>%llu;\n",
                               ".k%u=<uint:32>%llu;\n",
                               ".k%u=<sint:16>%lld;\n",
                   ".k%u=<float:32>%.17g;\n",
                               ".k%u=<float:64>%.17g;\n",
         ".k%u=<utf8>\"%s\";\n",
    };
    static const uint64_t uint_vals[] = {255, 65535, 100000, 0, 1, 999999};
    static const int64_t  sint_vals[] = {-128, -32768, 0, 100, -1};
    static const double   flt_vals[]  = {3.14, 1e-10, 2.71828, 1.0, 0.0, -1.5};
    static const char    *strs[]      = {"text","data","value","name","label"};

    size_t pos   = 0;
    size_t count = 0;

    for (size_t i = 0; ; i++) {
        int tmpl     = (int)(i % 7);
        char buf64[128];

        int n;
        if (tmpl == 6) {
            n = snprintf(buf64, sizeof(buf64), templates[tmpl],
                         (unsigned)i, strs[i % 5]);
        } else if (tmpl == 4 || tmpl == 5) {
            n = snprintf(buf64, sizeof(buf64), templates[tmpl],
                         (unsigned)i, flt_vals[i % 6]);
        } else if (tmpl == 3) {
            n = snprintf(buf64, sizeof(buf64), templates[tmpl],
                         (unsigned)i, (long long)sint_vals[i % 5]);
        } else {
            n = snprintf(buf64, sizeof(buf64), templates[tmpl],
                         (unsigned)i, (unsigned long long)uint_vals[i % 6]);
        }

        if (n < 0 || (size_t)n >= cap - pos - 1)
            break;

        memcpy(buf + pos, buf64, (size_t)n);
        pos   += (size_t)n;
        count++;
    }

    *out_assignments = count;
    return pos;
}

static size_t gen_structs(uint8_t *buf, size_t cap, size_t *out_assignments)
{
    size_t pos   = 0;
    size_t count = 0;

    for (size_t level = 0; ; level++) {
        int n = snprintf((char *)(buf + pos), cap - pos,
                         ".s%zu={.a=%zu;.b=%zu;.c=%zu;",
                         level, level, level + 1, level + 2);
        if (n < 0 || (size_t)n >= cap - pos - 8)
            break;
        pos   += (size_t)n;
        count += 4;
    }

    while (pos < cap) {
        if (pos + 4 >= cap) break;
        memcpy(buf + pos, "};", 2);
        pos += 2;
        if (pos + 2 >= cap) break;
        memcpy(buf + pos, "\n", 1);
        pos += 1;
    }

    *out_assignments = count;
    return pos;
}

static size_t gen_arrays(uint8_t *buf, size_t cap, size_t *out_assignments)
{
    size_t pos     = 0;
    size_t count   = 0;
    int    col     = 0;
    bool   first_row = true;

    while (pos < cap) {

        if (col == 0) {
            if (first_row) {
                int n = snprintf((char *)(buf + pos), cap - pos,
                                 ".a%zu=[", count);
                if (n < 0 || (size_t)n >= cap - pos - 8) break;
                pos += (size_t)n;
                first_row = false;
            } else {

                int n = snprintf((char *)(buf + pos), cap - pos,
                                 "]/[");
                if (n < 0 || (size_t)n >= cap - pos - 8) break;
                pos += (size_t)n;
            }
        }

        if (col > 0) {
            buf[pos++] = ',';
            if (pos >= cap) break;
        }

        int elem_type = count % 5;
        char val32[32];
        int n;
        switch (elem_type) {
        case 0: n = snprintf(val32, sizeof(val32), "%d", 42); break;
        case 1: n = snprintf(val32, sizeof(val32), "%d", 0); break;
        case 2: n = snprintf(val32, sizeof(val32), "%d", -1); break;
        case 3: n = snprintf(val32, sizeof(val32), "\"%s\"", "x"); break;
        case 4: n = snprintf(val32, sizeof(val32), "%d", 999); break;
        default: n = snprintf(val32, sizeof(val32), "0"); break;
        }

        size_t vlen = (n > 0) ? (size_t)n : 0;
        if (pos + vlen >= cap - 4) break;
        memcpy(buf + pos, val32, vlen);
        pos += vlen;

        col++;
        count++;

        if (col >= 5) {
            col = 0;
        }
    }

    if (pos + 4 < cap) {
        memcpy(buf + pos, "];\n", 3);
        pos += 3;
    }

    *out_assignments = 1;
    return pos;
}

static size_t gen_units(uint8_t *buf, size_t cap, size_t *out_assignments)
{
    static const char *unit_templates[] = {
        ".k%u=<float:64,m/s>%.17g;\n",
        ".k%u=<float:64,k-g\xc2\xb7m/s\xc2\xb2>%.17g;\n",
        ".k%u=<float:64,k-J>%.17g;\n",
        ".k%u=<uint:64,Gi-B>%llu;\n",
        ".k%u=<float:64,K>%.17g;\n",
        ".k%u=<uint:64,Mi-b>%llu;\n",
        ".k%u=<float:64,m/s\xc2\xb2>%.17g;\n",
        ".k%u=<float:64,k-Pa>%.17g;\n",
        ".k%u=<float:64,k-g/m\xc2\xb3>%.17g;\n",
        ".k%u=<float:64,m*s>%.17g;\n",
        ".k%u=<uint:32,no_unit>%llu;\n",
        ".k%u=<float:64,V/m>%.17g;\n",
    };
    static const double flt_vals[] = {
        9.81, 9.81, 5400.0, 0, 300.0, 0, 9.81, 101.325, 7800.0, 9.81, 0, 150.0
    };
    static const uint64_t uint_vals[] = {0, 8, 256, 0, 0, 0, 0, 0, 0, 0, 42, 0};

    size_t ntemplates = sizeof(unit_templates) / sizeof(unit_templates[0]);
    size_t pos   = 0;
    size_t count = 0;

    for (size_t i = 0; ; i++) {
        int t_idx = (int)(i % ntemplates);
        char buf128[256];

        int n;
        if (t_idx == 3 || t_idx == 5 || t_idx == 10)
            n = snprintf(buf128, sizeof(buf128), unit_templates[t_idx],
                         (unsigned)i, (unsigned long long)uint_vals[i % 12]);
        else
            n = snprintf(buf128, sizeof(buf128), unit_templates[t_idx],
                         (unsigned)i, flt_vals[i % 12]);

        if (n < 0 || (size_t)n >= cap - pos - 1)
            break;

        memcpy(buf + pos, buf128, (size_t)n);
        pos   += (size_t)n;
        count++;
    }

    *out_assignments = count;
    return pos;
}

static size_t gen_mixed(uint8_t *buf, size_t cap, size_t *out_assignments)
{
    static const char block1[] =
        ".system={\n"
        ".host=\"localhost\";\n"
        ".port=<uint:16>8080;\n"
        ".limits={\n"
        ".timeout=<float:64,s>30;\n"
        ".max_payload=<uint:64,Mi-B>16;\n"
        "};\n"
        "};\n"
        ".sensors=[\n"
        "{.name=\"temp\";.value=<float:64,\xc2\xb0\x43>23.5;.precision=<float:32>0.1;},\n"
        "{.name=\"pressure\";.value=<float:64,k-Pa>101.3;}\n"
        "];\n";

    static const char block2[] =
        ".matrix=[1,2,3,4]/[5,6,7,8]/[9,10,11,12];\n"
        ".count=<uint:32>42;\n"
        ".name=<utf8>\"test object\";\n"
        ".ratio=0.95;\n"
        ".flags=[true,false,true,false];\n";

    size_t pos = 0;
    size_t count = 0;

    size_t block1_size = sizeof(block1) - 1;
    size_t block2_size = sizeof(block2) - 1;
    size_t block1_assigns = 8;
    size_t block2_assigns = 5;

    while (pos < cap) {

        if (pos + block1_size < cap) {
            memcpy(buf + pos, block1, block1_size);
            pos   += block1_size;
            count += block1_assigns;

            if (pos + block2_size < cap) {
                memcpy(buf + pos, block2, block2_size);
                pos   += block2_size;
                count += block2_assigns;
            } else break;
        } else break;
    }

    *out_assignments = count;
    return pos;
}

typedef struct {
    size_t (*gen)(uint8_t*, size_t, size_t*);
    const char *name;
} payload_builder_t;

static const payload_builder_t builders[PROFILE_COUNT] = {
    { gen_scalars, "scalars" },
    { gen_typed,   "typed"   },
    { gen_structs, "structs" },
    { gen_arrays,  "arrays"  },
    { gen_units,   "units"   },
    { gen_mixed,   "mixed"   },
};

static bmark_result_t run_benchmark(profile_t profile,
                                    size_t target_size,
                                    uint32_t iterations)
{
    bmark_result_t result;
    memset(&result, 0, sizeof(result));
    result.profile      = profile;
    result.payload_size = 0;
    result.num_assignments = 0;

    uint8_t *buf = malloc(target_size + 1024 + 256);
    if (!buf) {
        fprintf(stderr, "  ERROR: malloc(%zu) failed\n", target_size + 1280);
        return result;
    }

    size_t assign_count = 0;
    size_t actual_len = builders[profile].gen(buf, target_size + 1024, &assign_count);
    if (actual_len == 0) {
        free(buf);
        return result;
    }

    result.num_assignments = assign_count;

    counter_ctx_t count_ctx = {0, 0, &cfg.min_overhead, profile, 0};
    bvnr_read_flags_t count_flags;
    memset(&count_flags, 0, sizeof(count_flags));
    count_flags.userdata     = &count_ctx;
    count_flags.on_verified  = event_counter;
    count_flags.on_unverified = (cfg.min_overhead) ? NULL : event_counter;

    bvnr_reader_t *r_count = bvnr_reader_create();
    if (!r_count) { free(buf); return result; }

    bool ok = bvnr_open_read_mem(r_count, buf, (uint32_t)actual_len,
                                 NULL, 0, &count_flags)
           && bvnr_read(r_count);

    if (ok)
        result.num_events = count_ctx.event_count;

    bvnr_reader_destroy(r_count);

    if (!ok) {
        fprintf(stderr, "  ERROR: count-parse failed for profile=%s size=%zu\n",
                profile_names[profile], target_size);
        result.payload_size = actual_len;
        free(buf);
        return result;
    }

    for (uint32_t w = 0; w < cfg.warmup; w++) {
        counter_ctx_t warm_ctx = {0, 0, &cfg.min_overhead, profile, 0};
        bvnr_read_flags_t warm_flags;
        memset(&warm_flags, 0, sizeof(warm_flags));
        warm_flags.userdata     = &warm_ctx;
        warm_flags.on_verified  = event_counter;
        warm_flags.on_unverified = (cfg.min_overhead) ? NULL : event_counter;

        bvnr_reader_t *r_warm = bvnr_reader_create();
        if (r_warm) {
            bvnr_open_read_mem(r_warm, buf, (uint32_t)actual_len,
                               NULL, 0, &warm_flags);
            bvnr_read(r_warm);
            bvnr_reader_destroy(r_warm);
        }
    }

    double cpu_start = cpu_sec();
    wall_clock_t wall_start = timer_now();

    for (uint32_t i = 0; i < iterations; i++) {
        counter_ctx_t run_ctx = {0, 0, &cfg.min_overhead, profile, i};
        bvnr_read_flags_t run_flags;
        memset(&run_flags, 0, sizeof(run_flags));
        run_flags.userdata     = &run_ctx;
        run_flags.on_verified  = event_counter;
        run_flags.on_unverified = (cfg.min_overhead) ? NULL : event_counter;

        bvnr_reader_t *r_run = bvnr_reader_create();
        if (!r_run) { free(buf); return result; }

        if (!bvnr_open_read_mem(r_run, buf, (uint32_t)actual_len,
                                NULL, 0, &run_flags)) {
            bvnr_reader_destroy(r_run);
            free(buf);
            return result;
        }

        bvnr_read(r_run);
        bvnr_reader_destroy(r_run);
    }

    wall_clock_t wall_end   = timer_now();
    double       cpu_end    = cpu_sec();

    result.elapsed_sec = timer_sec(&wall_start, &wall_end);
    result.cpu_sec     = cpu_end - cpu_start;
    result.payload_size = actual_len;

    free(buf);
    return result;
}

static void print_header(void)
{
    if (cfg.json) return;

    printf("%-10s %8s %8s %12s %12s %12s %10s %10s\n",
           "Profile", "Bytes", "Assigns", "Wall (ms)", "CPU (ms)",
           "MB/s", "Ass/s", "Ev/s");
    printf("────────── ──────── ──────── ──────────── "
           "──────────── ──────────── ────────── ──────────\n");
}

static void print_result(const bmark_result_t *r, uint32_t iterations)
{
    double wall_ms  = r->elapsed_sec * 1000.0;
    double cpu_ms   = r->cpu_sec * 1000.0;
    double wall_per_iter_ms = wall_ms / (double)iterations;

    double mb_per_sec = (double)r->payload_size * (double)iterations
                      / (1024.0 * 1024.0) / r->elapsed_sec;

    double assign_per_sec = (double)r->num_assignments * (double)iterations
                          / r->elapsed_sec;

    double events_per_sec = (double)r->num_events * (double)iterations
                          / r->elapsed_sec;

    if (cfg.json) {
        printf("{\"profile\":\"%s\",\"bytes\":%zu,\"assignments\":%zu,"
               "\"events\":%zu,\"iterations\":%u,\"wall_ms\":%.3f,"
               "\"cpu_ms\":%.3f,\"mb_per_sec\":%.3f,\"ass_per_sec\":%.0f,"
               "\"ev_per_sec\":%.0f,\"wall_per_iter_us\":%.1f}\n",
               profile_names[r->profile],
               r->payload_size, r->num_assignments, r->num_events,
               iterations,
               wall_ms, cpu_ms,
               mb_per_sec, assign_per_sec, events_per_sec,
               wall_per_iter_ms * 1000.0);
    } else {
        printf("%-10s %8zu %8zu %12.3f %12.3f %12.2f %10.0f %10.0f\n",
               profile_names[r->profile],
               r->payload_size, r->num_assignments,
               wall_ms, cpu_ms,
               mb_per_sec, assign_per_sec, events_per_sec);
    }
}

static void parse_profile_list(const char *list)
{

    for (int i = 0; i < PROFILE_COUNT; i++)
        cfg.profiles[i] = false;

    if (strcmp(list, "all") == 0) {
        for (int i = 0; i < PROFILE_COUNT; i++)
            cfg.profiles[i] = true;
        return;
    }

    char *copy = strdup(list);
    if (!copy) return;

    char *token = strtok(copy, ",");
    while (token) {
        for (int i = 0; i < PROFILE_COUNT; i++) {
            if (strcmp(token, profile_names[i]) == 0) {
                cfg.profiles[i] = true;
                break;
            }
        }
        token = strtok(NULL, ",");
    }
    free(copy);
}

static void parse_size_list(const char *list)
{
    cfg.num_sizes = 0;

    char *copy = strdup(list);
    if (!copy) return;

    char *token = strtok(copy, ",");
    while (token && cfg.num_sizes < 64) {
        cfg.sizes[cfg.num_sizes++] = (size_t)strtoul(token, NULL, 10);
        token = strtok(NULL, ",");
    }
    free(copy);
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --profile <list>   Comma-separated profile names:\n"
        "                     all,scalars,typed,structs,arrays,units,mixed\n"
        "                     (default: all)\n"
        "  --size <list>      Comma-separated payload sizes in bytes\n"
        "                     (default: 1024,4096,16384,65536)\n"
        "  --iterations <N>   Parse rounds per size×profile (default: 100)\n"
        "  --warmup <N>       Warm-up iterations (default: 10)\n"
        "  --verbose          Print per-run details\n"
        "  --json             Machine-readable JSON output\n"
        "  --min-overhead     Skip on_verified callback for pure lexer throughput\n"
        "  -h, --help         Show this help and exit\n"
        "\n"
        "Examples:\n"
        "  %s --profile scalars --size 4096\n"
        "  %s --profile all --size 1024,65536 --iterations 200 --json\n"
        "  %s --min-overhead --profile scalars,units --size 4096\n",
        prog, prog, prog, prog);
}

int main(int argc, char **argv)
{

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            parse_profile_list(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            parse_size_list(argv[++i]);
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            cfg.iterations = (uint32_t)strtoul(argv[++i], NULL, 10);
            if (cfg.iterations < 1) cfg.iterations = 1;
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            cfg.warmup = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            cfg.verbose = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            cfg.json = true;
        } else if (strcmp(argv[i], "--min-overhead") == 0) {
            cfg.min_overhead = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (cfg.json) {

        printf("{\n\"config\":{\"iterations\":%u,\"warmup\":%u,"
               "\"min_overhead\":%s},\n\"results\":[\n",
               cfg.iterations, cfg.warmup,
               cfg.min_overhead ? "true" : "false");
    } else {
        printf("═════════════════════════════════════════════════════════════\n");
        printf("  Bovnar Parsing Throughput Benchmark\n");
        printf("  Iterations: %u   Warmup: %u   Min-overhead: %s\n",
               cfg.iterations, cfg.warmup,
               cfg.min_overhead ? "yes" : "no");
        printf("═════════════════════════════════════════════════════════════\n");
        print_header();
    }

    bool first = true;

    for (int p = 0; p < PROFILE_COUNT; p++) {
        if (!cfg.profiles[p]) continue;

        for (uint32_t s = 0; s < cfg.num_sizes; s++) {
            uint32_t iterations = cfg.iterations;

            bmark_result_t r = run_benchmark((profile_t)p, cfg.sizes[s],
                                             iterations);

            if (r.payload_size == 0) {
                fprintf(stderr, "  SKIP: %s/%zu (generation or parse failed)\n",
                        profile_names[p], cfg.sizes[s]);
                continue;
            }

            if (cfg.json && !first)
                printf(",\n");
            first = false;

            print_result(&r, iterations);

            if (cfg.verbose && !cfg.json) {
                printf("  ── detail: profile=%s, size=%zu bytes, "
                       "assignments=%zu, events=%zu, "
                       "avg_cost=%.3f us/assign\n",
                       profile_names[p], r.payload_size,
                       r.num_assignments, r.num_events,
                       (r.elapsed_sec * 1e6) / (double)(r.num_assignments * iterations));
            }
        }
    }

    if (cfg.json)
        printf("\n]}\n");

    printf("\n");
    return 0;
}
