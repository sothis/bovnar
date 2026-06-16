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

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "bovnar.h"

static int g_failures = 0;
static int g_tests    = 0;

#define ASSERT_TRUE(cond, msg) do {                                          \
	g_tests++;                                                               \
	if (!(cond)) {                                                           \
		fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, (msg));           \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                        \
	g_tests++;                                                               \
	int64_t _a = (int64_t)(a), _b = (int64_t)(b);                           \
	if (_a != _b) {                                                          \
		fprintf(stderr, "  FAIL line %d: %s\n  got %" PRId64                \
						", expected %" PRId64 "\n",                          \
				__LINE__, (msg), _a, _b);                                    \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                       \
	g_tests++;                                                               \
	uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b);                        \
	if (_a != _b) {                                                          \
		fprintf(stderr, "  FAIL line %d: %s\n  got %" PRIu64                \
						", expected %" PRIu64 "\n",                          \
				__LINE__, (msg), _a, _b);                                    \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

#define ASSERT_STR_EQ(a, b, msg) do {                                        \
	g_tests++;                                                               \
	if (strcmp((a), (b)) != 0) {                                             \
		fprintf(stderr, "  FAIL line %d: %s\n  got \"%s\", expected \"%s\"\n",\
				__LINE__, (msg), (a), (b));                                  \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

#define ASSERT_STR_CONTAINS(haystack, needle, msg) do {                      \
	g_tests++;                                                               \
	if (strstr((haystack), (needle)) == NULL) {                              \
		fprintf(stderr, "  FAIL line %d: %s\n"                              \
						"  \"%s\" not found in \"%s\"\n",                   \
				__LINE__, (msg), (needle), (haystack));                      \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

#define CAP_MAX_KEY   256
#define CAP_MAX_VALUE 512
#define CAP_MAX_UNIT  128
#define CAP_MAX_ENTRIES 512

typedef enum {
	CAP_ASSIGNMENT,
	CAP_STRUCT_BEGIN,
	CAP_STRUCT_END,
} cap_kind_t;

typedef struct {
	cap_kind_t        kind;

	char              key[CAP_MAX_KEY];

	char              value[CAP_MAX_VALUE];
	uint32_t          value_len;
	token_type_t      token;
	value_type_spec_t vt;
	value_unit_t      vu;
	char              unit_str[CAP_MAX_UNIT];
} cap_entry_t;

typedef struct {

	char              pending_key[CAP_MAX_KEY];
	value_type_spec_t pending_vt;
	value_unit_t      pending_vu;
	char              pending_unit_str[CAP_MAX_UNIT];

	cap_entry_t entries[CAP_MAX_ENTRIES];
	int         count;
	bool        overflow;
	bool        error;
} capture_ctx_t;

static bool capture_callback(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	capture_ctx_t *ctx = ud;

	switch (ev) {

	case ev_assignment_start:

		memset(ctx->pending_key, 0, sizeof(ctx->pending_key));
		if (d->length > 0 && d->length < CAP_MAX_KEY) {
			memcpy(ctx->pending_key, d->data, d->length);
			ctx->pending_key[d->length] = '\0';
		}
		ctx->pending_vt = (value_type_spec_t){0};
		ctx->pending_vu = (value_unit_t){.num_components = 0};
		ctx->pending_unit_str[0] = '\0';
		break;

	case ev_type_annotation_type_family_parameter:
		if (d->type == token_is_type_width) {

			ctx->pending_vt = d->value_type;
		} else if (d->type == token_is_unit) {

			uint32_t ulen = d->length < CAP_MAX_UNIT - 1u
						  ? d->length : CAP_MAX_UNIT - 1u;
			memcpy(ctx->pending_unit_str, d->data, ulen);
			ctx->pending_unit_str[ulen] = '\0';
			bool ok = false;
			ctx->pending_vu = bvn_parse_unit(
				(const uint8_t *)ctx->pending_unit_str, &ok);
			if (!ok) ctx->pending_vu = (value_unit_t){0};
		}
		break;

	case ev_struct_start: {
		if (ctx->count >= CAP_MAX_ENTRIES) { ctx->overflow = true; break; }
		cap_entry_t *e = &ctx->entries[ctx->count++];
		memset(e, 0, sizeof(*e));
		e->kind = CAP_STRUCT_BEGIN;
		memcpy(e->key, ctx->pending_key, CAP_MAX_KEY);
		break;
	}

	case ev_struct_end: {
		if (ctx->count >= CAP_MAX_ENTRIES) { ctx->overflow = true; break; }
		cap_entry_t *e = &ctx->entries[ctx->count++];
		memset(e, 0, sizeof(*e));
		e->kind = CAP_STRUCT_END;
		break;
	}

	case ev_data: {
		if (ctx->count >= CAP_MAX_ENTRIES) { ctx->overflow = true; break; }
		cap_entry_t *e = &ctx->entries[ctx->count++];
		memset(e, 0, sizeof(*e));
		e->kind  = CAP_ASSIGNMENT;
		memcpy(e->key, ctx->pending_key, CAP_MAX_KEY);

		e->value_len = d->length < CAP_MAX_VALUE - 1u
					 ? d->length : CAP_MAX_VALUE - 1u;
		memcpy(e->value, d->data, e->value_len);
		e->value[e->value_len] = '\0';

		e->token = d->type;

		e->vt = (ctx->pending_vt.family != vt_plain || d->type == token_is_string
				 || d->type == token_is_null_value || d->type == token_is_symbol
				 || d->type == token_is_bool)
			  ? d->value_type
			  : ctx->pending_vt;

		if (ctx->pending_vt.family != vt_plain)
			e->vt = ctx->pending_vt;

		e->vu = ctx->pending_vu;
		memcpy(e->unit_str, ctx->pending_unit_str, CAP_MAX_UNIT);
		break;
	}

	default:
		break;
	}

	return true;
}

static void on_error_capture(void *ud, error_code_t err,
							 uint64_t line, uint64_t col,
							 uint32_t byte, uint64_t off)
{
	capture_ctx_t *ctx = ud;
	ctx->error = true;
	fprintf(stderr, "  [reader error] %s at line %" PRIu64
					" col %" PRIu64 " byte 0x%02x off %" PRIu64 "\n",
			bvn_error_to_string(err), line, col, byte, off);
}

typedef bool (*writer_fn_t)(bvnr_writer_t *w, bool pretty);

typedef struct {
	int           fd;
	writer_fn_t   build;
	bool          pretty;
	bool          result;
	uint64_t      bytes_written;
} writer_thread_args_t;

static void *writer_thread(void *arg)
{
	writer_thread_args_t *a = arg;

	bvnr_sink_t sink;
	bvnr_sink_to_fd(&sink, a->fd);

	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) goto done_fail;

	if (!bvnr_open_write_sink(w, &sink, a->pretty, NULL))
		goto done_destroy;

	bvnr_data_t hdr = {0};
	if (!bvnr_write_event(w, ev_stream_start, &hdr))
		goto done_destroy;

	if (!a->build(w, a->pretty))
		goto done_destroy;

	if (!bvnr_write_finish(w))
		goto done_destroy;

	a->bytes_written = bvnr_writer_bytes_written(w);
	a->result = true;

done_destroy:
	if (!a->result)
		fprintf(stderr, "  [writer error] %s\n",
				bvn_error_to_string(bvnr_writer_get_error(w)));
	bvnr_writer_destroy(w);
done:
	close(a->fd);
	return NULL;

done_fail:
	a->result = false;
	goto done;
}

typedef struct {
	int           fd;
	capture_ctx_t ctx;
	bool          result;
} reader_thread_args_t;

static void *reader_thread(void *arg)
{
	reader_thread_args_t *a = arg;
	memset(&a->ctx, 0, sizeof(a->ctx));

	bvnr_source_t src;
	bvnr_source_from_fd(&src, a->fd);

	bvnr_read_flags_t flags = {
		.on_verified = capture_callback,
		.on_error    = on_error_capture,
		.userdata    = &a->ctx,
	};

	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) { a->result = false; close(a->fd); return NULL; }

	a->result = bvnr_open_read_source(r, &src, NULL, &flags)
			 && bvnr_read(r);

	if (!a->result)
		fprintf(stderr, "  [reader error final] %s\n",
				bvn_error_to_string(bvnr_reader_get_error(r)));

	bvnr_reader_destroy(r);
	close(a->fd);
	return NULL;
}

static bool run_roundtrip(writer_fn_t build, bool pretty,
						  capture_ctx_t *out_ctx,
						  uint64_t *out_bytes)
{
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
		perror("socketpair");
		return false;
	}

	writer_thread_args_t wargs = { .fd = sv[0], .build = build, .pretty = pretty };
	reader_thread_args_t rargs = { .fd = sv[1] };

	pthread_t wt, rt;
	if (pthread_create(&wt, NULL, writer_thread, &wargs) != 0 ||
		pthread_create(&rt, NULL, reader_thread, &rargs) != 0) {
		close(sv[0]); close(sv[1]);
		return false;
	}

	pthread_join(wt, NULL);
	pthread_join(rt, NULL);

	*out_ctx   = rargs.ctx;
	if (out_bytes) *out_bytes = wargs.bytes_written;

	return wargs.result && rargs.result
		&& !rargs.ctx.error && !rargs.ctx.overflow;
}

static void verify_assignment(const cap_entry_t *e,
							  const char *expected_key,
							  const char *value_contains,
							  value_type_family_t family,
							  uint32_t width,
							  const char *label)
{
	char msg[256];

	snprintf(msg, sizeof(msg), "%s: entry is CAP_ASSIGNMENT", label);
	ASSERT_TRUE(e->kind == CAP_ASSIGNMENT, msg);

	snprintf(msg, sizeof(msg), "%s: key matches", label);
	ASSERT_STR_EQ(e->key, expected_key, msg);

	if (value_contains) {
		snprintf(msg, sizeof(msg), "%s: value contains \"%s\"",
				 label, value_contains);
		ASSERT_STR_CONTAINS(e->value, value_contains, msg);
	}

	if (family != vt_illegal) {
		snprintf(msg, sizeof(msg), "%s: type family", label);
		ASSERT_EQ_INT(e->vt.family, (int)family, msg);
	}

	if (width != 0) {
		snprintf(msg, sizeof(msg), "%s: type width", label);

		uint32_t eff = e->vt.width ? e->vt.width : 64u;
		ASSERT_EQ_UINT(eff, (uint64_t)width, msg);
	}
}

static void verify_struct_begin(const cap_entry_t *e,
								const char *key,
								const char *label)
{
	char msg[256];
	snprintf(msg, sizeof(msg), "%s: entry is CAP_STRUCT_BEGIN", label);
	ASSERT_TRUE(e->kind == CAP_STRUCT_BEGIN, msg);
	snprintf(msg, sizeof(msg), "%s: struct key", label);
	ASSERT_STR_EQ(e->key, key, msg);
}

static void verify_struct_end(const cap_entry_t *e, const char *label)
{
	char msg[256];
	snprintf(msg, sizeof(msg), "%s: entry is CAP_STRUCT_END", label);
	ASSERT_TRUE(e->kind == CAP_STRUCT_END, msg);
}

static bool build_scalars(bvnr_writer_t *w, bool pretty)
{
	(void)pretty;
	return bvnr_write_string(w, "name",    "Alice")
		&& bvnr_write_bool  (w, "active",  true)
		&& bvnr_write_bool  (w, "done",    false)
		&& bvnr_write_null  (w, "missing")
		&& bvnr_write_uint  (w, "u8",   8,  255u)
		&& bvnr_write_uint  (w, "u16",  16, 65535u)
		&& bvnr_write_uint  (w, "u32",  32, 0xDEADBEEFu)
		&& bvnr_write_uint  (w, "u64",  64, UINT64_C(12345678901234))
		&& bvnr_write_sint  (w, "s16",  16, -100)
		&& bvnr_write_sint  (w, "s32",  32, INT32_MIN)
		&& bvnr_write_sint  (w, "s64",  64, INT64_MIN)
		&& bvnr_write_float (w, "f32",  32, 3.14f)
		&& bvnr_write_float (w, "f64",  64, 3.141592653589793);
}

static bool build_structs(bvnr_writer_t *w, bool pretty)
{
	(void)pretty;
	return bvnr_write_struct_start(w, "root")
		&& bvnr_write_string      (w, "version", "1.0")
		&& bvnr_write_struct_start(w, "network")
		&& bvnr_write_string      (w, "host", "127.0.0.1")
		&& bvnr_write_uint        (w, "port",  16, 8080u)
		&& bvnr_write_struct_start(w, "tls")
		&& bvnr_write_bool        (w, "enabled", true)
		&& bvnr_write_string      (w, "cert", "/etc/certs/server.pem")
		&& bvnr_write_struct_end  (w)
		&& bvnr_write_struct_end  (w)
		&& bvnr_write_uint        (w, "workers", 8, 4u)
		&& bvnr_write_struct_end  (w);
}

static bool build_units(bvnr_writer_t *w, bool pretty)
{
	(void)pretty;
	value_unit_t km  = BVN_UNIT_SI (bu_meter, si_kilo);
	value_unit_t kg  = BVN_UNIT_SI (bu_gram,  si_kilo);
	value_unit_t gib = BVN_UNIT_IEC(bu_byte,  iec_gibi);
	value_unit_t ms  = BVN_UNIT_COMPOUND2(
						   bu_meter,  si_none, exp_linear,
						   bu_second, si_none, exp_neg_linear);
	value_unit_t mhz = BVN_UNIT_SI(bu_hertz, si_mega);

	return bvnr_write_uint_unit (w, "distance", 32, 42u,   km)
		&& bvnr_write_float_unit(w, "mass",     32, 70.5,  kg)
		&& bvnr_write_uint_unit (w, "ram",      64, 16u,   gib)
		&& bvnr_write_float_unit(w, "speed",    64, 9.81,  ms)
		&& bvnr_write_uint_unit (w, "freq",     32, 3600u, mhz);
}

static bool build_boundary(bvnr_writer_t *w, bool pretty)
{
	(void)pretty;
	return bvnr_write_uint (w, "zero_u",   64, 0u)
		&& bvnr_write_uint (w, "max_u8",   8,  UINT8_MAX)
		&& bvnr_write_uint (w, "max_u16",  16, UINT16_MAX)
		&& bvnr_write_uint (w, "max_u32",  32, UINT32_MAX)
		&& bvnr_write_uint (w, "max_u64",  64, UINT64_MAX)
		&& bvnr_write_sint (w, "zero_s",   64, 0)
		&& bvnr_write_sint (w, "neg_one",  32, -1)
		&& bvnr_write_sint (w, "min_s8",   8,  INT8_MIN)
		&& bvnr_write_sint (w, "min_s16",  16, INT16_MIN)
		&& bvnr_write_sint (w, "min_s32",  32, INT32_MIN)
		&& bvnr_write_sint (w, "min_s64",  64, INT64_MIN)
		&& bvnr_write_float(w, "pos_zero", 64, +0.0)
		&& bvnr_write_float(w, "neg_zero", 64, -0.0);
}

#define MANY_N 200
static bool build_many(bvnr_writer_t *w, bool pretty)
{
	(void)pretty;
	for (int i = 0; i < MANY_N; i++) {
		char key[32];
		snprintf(key, sizeof(key), "item%03d", i);
		if (!bvnr_write_uint(w, key, 32, (uint64_t)i))
			return false;
	}
	return true;
}

static bool build_reference(bvnr_writer_t *w, bool pretty)
{
	(void)pretty;
	return bvnr_write_string(w, "host",    "example.com")
		&& bvnr_write_uint  (w, "port",    16, 443u)
		&& bvnr_write_bool  (w, "tls",     true)
		&& bvnr_write_float (w, "timeout", 64, 30.0)
		&& bvnr_write_struct_start(w, "auth")
		&& bvnr_write_string      (w, "method", "bearer")
		&& bvnr_write_null        (w, "token")
		&& bvnr_write_struct_end  (w);
}

static void test_rt_scalars(void)
{
	printf("  test_rt_scalars...\n");

	capture_ctx_t ctx;
	uint64_t bytes = 0;
	ASSERT_TRUE(run_roundtrip(build_scalars, true, &ctx, &bytes),
				"scalar roundtrip must succeed");
	ASSERT_TRUE(bytes > 0, "writer must produce output");

	const int n = ctx.count;
	ASSERT_EQ_INT(n, 13, "scalar document must have exactly 13 captured entries");
	if (n < 13) return;

	int i = 0;

	verify_assignment(&ctx.entries[i++], "name",    "Alice", vt_utf8,   0,  "name");
	verify_assignment(&ctx.entries[i++], "active",  "true",  vt_bool,   0,  "active");
	verify_assignment(&ctx.entries[i++], "done",    "false", vt_bool,   0,  "done");
	verify_assignment(&ctx.entries[i++], "missing", NULL,    vt_plain,  0,  "missing");
	verify_assignment(&ctx.entries[i++], "u8",      "255",   vt_uint,   8,  "u8");
	verify_assignment(&ctx.entries[i++], "u16",     "65535", vt_uint,   16, "u16");
	verify_assignment(&ctx.entries[i++], "u32",     NULL,    vt_uint,   32, "u32");
	verify_assignment(&ctx.entries[i++], "u64",     "12345678901234",
															 vt_uint,   64, "u64");
	verify_assignment(&ctx.entries[i++], "s16",     "-100",  vt_sint,   16, "s16");
	verify_assignment(&ctx.entries[i++], "s32",     "-2147483648",
															 vt_sint,   32, "s32");
	verify_assignment(&ctx.entries[i++], "s64",     "-9223372036854775808",
															 vt_sint,   64, "s64");
	verify_assignment(&ctx.entries[i++], "f32",     NULL,    vt_float,  32, "f32");
	verify_assignment(&ctx.entries[i++], "f64",     "3.14",  vt_float,  64, "f64");
}

static void test_rt_structs(void)
{
	printf("  test_rt_structs...\n");

	capture_ctx_t ctx;
	ASSERT_TRUE(run_roundtrip(build_structs, true, &ctx, NULL),
				"struct roundtrip must succeed");

	const int expected = 12;
	ASSERT_EQ_INT(ctx.count, expected,
				  "struct document must have 12 capture entries");
	if (ctx.count < expected) return;

	int i = 0;
	verify_struct_begin (&ctx.entries[i++], "root",    "root struct begin");
	verify_assignment   (&ctx.entries[i++], "version", "1.0",
						 vt_utf8, 0, "version");
	verify_struct_begin (&ctx.entries[i++], "network", "network struct begin");
	verify_assignment   (&ctx.entries[i++], "host",    "127.0.0.1",
						 vt_utf8, 0, "host");
	verify_assignment   (&ctx.entries[i++], "port",    "8080",
						 vt_uint, 16, "port");
	verify_struct_begin (&ctx.entries[i++], "tls",     "tls struct begin");
	verify_assignment   (&ctx.entries[i++], "enabled", "true",
						 vt_bool, 0, "enabled");
	verify_assignment   (&ctx.entries[i++], "cert",    "/etc/certs/server.pem",
						 vt_utf8, 0, "cert");
	verify_struct_end   (&ctx.entries[i++],            "tls struct end");
	verify_struct_end   (&ctx.entries[i++],            "network struct end");
	verify_assignment   (&ctx.entries[i++], "workers", "4",
						 vt_uint, 8, "workers");
	verify_struct_end   (&ctx.entries[i++],            "root struct end");
}

static void test_rt_units(void)
{
	printf("  test_rt_units...\n");

	capture_ctx_t ctx;
	ASSERT_TRUE(run_roundtrip(build_units, true, &ctx, NULL),
				"units roundtrip must succeed");

	ASSERT_EQ_INT(ctx.count, 5, "units document must have 5 entries");
	if (ctx.count < 5) return;

	verify_assignment(&ctx.entries[0], "distance", "42",   vt_uint,  32, "distance");
	verify_assignment(&ctx.entries[1], "mass",     NULL,   vt_float, 32, "mass");
	verify_assignment(&ctx.entries[2], "ram",      "16",   vt_uint,  64, "ram");
	verify_assignment(&ctx.entries[3], "speed",    NULL,   vt_float, 64, "speed");
	verify_assignment(&ctx.entries[4], "freq",     "3600", vt_uint,  32, "freq");

	{

		const cap_entry_t *e = &ctx.entries[0];
		ASSERT_EQ_INT(e->vu.num_components, 1, "distance: 1 unit component");
		ASSERT_EQ_INT(e->vu.components[0].base,        bu_meter, "distance: base=meter");
		ASSERT_EQ_INT(e->vu.components[0].prefix.id.si, si_kilo,  "distance: prefix=kilo");
	}
	{

		const cap_entry_t *e = &ctx.entries[2];
		ASSERT_EQ_INT(e->vu.num_components, 1, "ram: 1 unit component");
		ASSERT_EQ_INT(e->vu.components[0].base,          bu_byte,  "ram: base=byte");
		ASSERT_EQ_INT(e->vu.components[0].prefix.system, prefix_iec, "ram: IEC prefix");
		ASSERT_EQ_INT(e->vu.components[0].prefix.id.iec, iec_gibi, "ram: gibi");
	}
	{

		const cap_entry_t *e = &ctx.entries[3];
		ASSERT_EQ_INT(e->vu.num_components, 2,          "speed: 2 unit components");
		ASSERT_EQ_INT(e->vu.components[0].base, bu_meter,  "speed: comp0=meter");
		ASSERT_EQ_INT(e->vu.components[1].base, bu_second, "speed: comp1=second");
		ASSERT_EQ_INT(e->vu.components[1].exponent, exp_neg_linear,
					  "speed: comp1 exponent=-1");
	}
	{

		const cap_entry_t *e = &ctx.entries[4];
		ASSERT_EQ_INT(e->vu.num_components, 1, "freq: 1 unit component");
		ASSERT_EQ_INT(e->vu.components[0].base,         bu_hertz, "freq: base=hertz");
		ASSERT_EQ_INT(e->vu.components[0].prefix.id.si, si_mega,  "freq: prefix=mega");
	}
}

static void test_rt_boundary_values(void)
{
	printf("  test_rt_boundary_values...\n");

	capture_ctx_t ctx;
	ASSERT_TRUE(run_roundtrip(build_boundary, true, &ctx, NULL),
				"boundary roundtrip must succeed");

	ASSERT_EQ_INT(ctx.count, 13, "boundary document must have 13 entries");
	if (ctx.count < 13) return;

	int i = 0;
	verify_assignment(&ctx.entries[i++], "zero_u",  "0",                     vt_uint,  64, "zero_u");
	verify_assignment(&ctx.entries[i++], "max_u8",  "255",                   vt_uint,  8,  "max_u8");
	verify_assignment(&ctx.entries[i++], "max_u16", "65535",                 vt_uint,  16, "max_u16");
	verify_assignment(&ctx.entries[i++], "max_u32", "4294967295",            vt_uint,  32, "max_u32");
	verify_assignment(&ctx.entries[i++], "max_u64", "18446744073709551615",  vt_uint,  64, "max_u64");
	verify_assignment(&ctx.entries[i++], "zero_s",  "0",                     vt_sint,  64, "zero_s");
	verify_assignment(&ctx.entries[i++], "neg_one", "-1",                    vt_sint,  32, "neg_one");
	verify_assignment(&ctx.entries[i++], "min_s8",  "-128",                  vt_sint,  8,  "min_s8");
	verify_assignment(&ctx.entries[i++], "min_s16", "-32768",                vt_sint,  16, "min_s16");
	verify_assignment(&ctx.entries[i++], "min_s32", "-2147483648",           vt_sint,  32, "min_s32");
	verify_assignment(&ctx.entries[i++], "min_s64", "-9223372036854775808",  vt_sint,  64, "min_s64");
	verify_assignment(&ctx.entries[i++], "pos_zero", NULL,                   vt_float, 64, "pos_zero");
	verify_assignment(&ctx.entries[i++], "neg_zero", NULL,                   vt_float, 64, "neg_zero");
}

static void test_rt_pretty_vs_compact(void)
{
	printf("  test_rt_pretty_vs_compact...\n");

	capture_ctx_t pretty_ctx, compact_ctx;
	ASSERT_TRUE(run_roundtrip(build_reference, true,  &pretty_ctx,  NULL),
				"pretty roundtrip must succeed");
	ASSERT_TRUE(run_roundtrip(build_reference, false, &compact_ctx, NULL),
				"compact roundtrip must succeed");

	ASSERT_EQ_INT(pretty_ctx.count, compact_ctx.count,
				  "pretty and compact must yield the same number of entries");

	int n = pretty_ctx.count < compact_ctx.count
		  ? pretty_ctx.count : compact_ctx.count;

	for (int i = 0; i < n; i++) {
		const cap_entry_t *p = &pretty_ctx.entries[i];
		const cap_entry_t *c = &compact_ctx.entries[i];
		char msg[256];

		snprintf(msg, sizeof(msg), "entry %d: kind matches", i);
		ASSERT_EQ_INT(p->kind, c->kind, msg);

		snprintf(msg, sizeof(msg), "entry %d: key matches", i);
		ASSERT_STR_EQ(p->key, c->key, msg);

		if (p->kind == CAP_ASSIGNMENT) {
			snprintf(msg, sizeof(msg), "entry %d: type family matches", i);
			ASSERT_EQ_INT(p->vt.family, c->vt.family, msg);

			uint32_t pw = p->vt.width ? p->vt.width : 64u;
			uint32_t cw = c->vt.width ? c->vt.width : 64u;
			snprintf(msg, sizeof(msg), "entry %d: type width matches", i);
			ASSERT_EQ_UINT(pw, cw, msg);

			if (p->vt.family != vt_float && p->vt.family != vt_plain) {
				snprintf(msg, sizeof(msg), "entry %d: value matches", i);
				ASSERT_STR_EQ(p->value, c->value, msg);
			}
		}
	}
}

static void test_rt_write_plain_quirk(void)
{
	printf("  test_rt_write_plain_quirk...\n");

	uint8_t buf[256];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, buf, sizeof(buf));

	bvnr_writer_t *w = bvnr_writer_create();
	ASSERT_TRUE(w != NULL, "writer create");
	if (!w) return;

	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "open write");
	bvnr_data_t hdr = {0};
	ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &hdr), "stream begin");

	ASSERT_TRUE(bvnr_write_plain(w, "sym", "ok"), "write_plain returns true");
	ASSERT_TRUE(bvnr_write_finish(w), "finish");
	uint64_t n = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	buf[n] = '\0';
	ASSERT_TRUE(strstr((char *)buf, ".sym=ok;") != NULL,
				"write_plain serializes symbol value correctly");
	ASSERT_TRUE(strstr((char *)buf, "ok") != NULL,
				"value payload is present in the serialised output");

	capture_ctx_t ctx = {0};
	bvnr_read_flags_t fl = {
		.on_verified = capture_callback,
		.on_error    = on_error_capture,
		.userdata    = &ctx,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	ASSERT_TRUE(r != NULL, "reader create");
	if (r) {
		bool ok = bvnr_open_read_mem(r, buf, n, NULL, 0, &fl) && bvnr_read(r);
		ASSERT_TRUE(ok,            "reader parses symbol assignment without error");
		ASSERT_EQ_INT(ctx.count, 1, "exactly one capture entry");
		if (ctx.count == 1) {
			ASSERT_STR_EQ(ctx.entries[0].key,   "sym", "key preserved");
			ASSERT_EQ_INT(ctx.entries[0].value_len, 2,  "value length is 2");
			ASSERT_STR_EQ(ctx.entries[0].value,  "ok",  "value is ok");
		}
		bvnr_reader_destroy(r);
	}
}

static void test_rt_many_assignments(void)
{
	printf("  test_rt_many_assignments...\n");

	capture_ctx_t ctx;
	uint64_t bytes = 0;
	ASSERT_TRUE(run_roundtrip(build_many, false, &ctx, &bytes),
				"many-assignments roundtrip must succeed");
	ASSERT_TRUE(bytes > 0, "writer must produce output");

	ASSERT_EQ_INT(ctx.count, MANY_N,
				  "captured entry count must equal MANY_N");
	if (ctx.count < MANY_N) return;

	verify_assignment(&ctx.entries[0], "item000", "0",
					  vt_uint, 32, "item000");

	verify_assignment(&ctx.entries[99], "item099", "99",
					  vt_uint, 32, "item099");

	verify_assignment(&ctx.entries[MANY_N - 1],
					  "item199", "199",
					  vt_uint, 32, "item199");
}

/*
 * spec 1.1 — a fractional ISO datetime literal read from an fd source whose
 * bytes trickle in (so the '.', the fraction digits, and the 'Z' land in
 * separate read() chunks) must still deliver the complete fraction. The lexer
 * accumulates the literal into its scratch buffer across reads, and frac_data is
 * computed only once the whole token is present. The writer builders can't reach
 * this path (bvnr_write_datetime takes no fraction), so it is otherwise untested.
 */
typedef struct { char carrier[64]; char frac[64]; bool got; } dtfrac_ctx_t;

static bool dtfrac_cb(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	dtfrac_ctx_t *c = ud;
	if (ev == ev_data && d->type == token_is_number &&
	    d->value_type.family == vt_datetime) {
		uint32_t n = d->length < sizeof c->carrier - 1
		             ? d->length : (uint32_t)(sizeof c->carrier - 1);
		if (d->data) memcpy(c->carrier, d->data, n);
		c->carrier[n] = '\0';
		c->frac[0] = '\0';
		if (d->frac_data && d->frac_length) {
			uint32_t f = d->frac_length < sizeof c->frac - 1
			             ? d->frac_length : (uint32_t)(sizeof c->frac - 1);
			memcpy(c->frac, d->frac_data, f);
			c->frac[f] = '\0';
		}
		c->got = true;
	}
	return true;
}

static void *byte_pacer_thread(void *arg)
{
	int fd = *(int *)arg;
	static const char doc[] =
		"#!bovnar 1.1\n.t = 2026-06-15T12:00:00.000123Z;\n";
	for (size_t i = 0; i < sizeof doc - 1; i++) {
		if (write(fd, doc + i, 1) != 1)
			break;
		/* let the blocked reader consume this byte before the next arrives, so
		 * the literal really is split across read() boundaries. */
		nanosleep(&(struct timespec){ 0, 50000 }, NULL);   /* 50 us */
	}
	close(fd);
	return NULL;
}

static void test_rt_datetime_fraction_streamed(void)
{
	printf("  test_rt_datetime_fraction_streamed...\n");
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
		ASSERT_TRUE(false, "socketpair must succeed");
		return;
	}
	pthread_t wt;
	if (pthread_create(&wt, NULL, byte_pacer_thread, &sv[0]) != 0) {
		ASSERT_TRUE(false, "pthread_create must succeed");
		close(sv[0]); close(sv[1]);
		return;
	}

	dtfrac_ctx_t ctx = {0};
	bvnr_source_t src;
	bvnr_source_from_fd(&src, sv[1]);
	bvnr_read_flags_t fl = { .on_verified = dtfrac_cb, .userdata = &ctx };
	bvnr_reader_t *r = bvnr_reader_create();
	bool ok = r && bvnr_open_read_source(r, &src, NULL, &fl) && bvnr_read(r);
	if (r) bvnr_reader_destroy(r);
	pthread_join(wt, NULL);
	close(sv[1]);

	ASSERT_TRUE(ok, "streamed fractional datetime parses");
	ASSERT_TRUE(ctx.got, "datetime value event captured");
	ASSERT_STR_EQ(ctx.carrier, "1781524800",
		"carrier is the whole-second epoch integer");
	ASSERT_STR_EQ(ctx.frac, "000123",
		"fraction reassembled across read boundaries");
}

int main(void)
{
	printf("Running bovnar_socketpair_roundtrip_test suite...\n");

	test_rt_scalars();
	test_rt_structs();
	test_rt_units();
	test_rt_boundary_values();
	test_rt_pretty_vs_compact();
	test_rt_write_plain_quirk();
	test_rt_many_assignments();
	test_rt_datetime_fraction_streamed();

	if (g_failures == 0) {
		printf("PASSED %d tests\n", g_tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", g_failures, g_tests);
	return 1;
}
