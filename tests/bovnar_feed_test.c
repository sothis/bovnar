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

/*
 * The push-fed readers (bvnr_open_read_feed, bvnr_frame_reader_*,
 * bvnr_demux_feed) and the callback sources and sinks.
 *
 * The core property is equivalence: a document fed in pieces, cut anywhere,
 * produces exactly the events, errors and positions that bvnr_read produces
 * for the whole buffer. It is checked by recording everything a read reports
 * -- both event streams with every field, every on_error call, the result and
 * the error getters, the recovery counters and the mirror -- and comparing the
 * records of a whole-buffer read with those of every way of feeding the same
 * bytes: two pieces cut at every position, single bytes, fixed sizes around
 * the octet-chunk and window boundaries, and random partitions.
 *
 * Usage: bvnr_feed_test [examples-dir]
 * With a directory, every *.bvnr in it joins the corpus.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#  include <dirent.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif
#include "bovnar.h"
#include "bovnar_stream.h"

static int g_failures = 0;
static int g_tests    = 0;

#define ASSERT_TRUE(cond, msg) do {                                          \
	g_tests++;                                                               \
	if (!(cond)) {                                                           \
		fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, (msg));           \
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

/* Allocation in the test itself: a failure ends the run, it is not a result. */
static void* xrealloc(void* p, size_t n)
{
	void* q = realloc(p, n ? n : 1);
	if (!q) { fprintf(stderr, "out of memory\n"); exit(2); }
	return q;
}

static void* xmalloc(size_t n)
{
	void* p = malloc(n ? n : 1);
	if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
	return p;
}

/* ---------------------------------------------------------------------------
 * A growable byte buffer, for event records, mirrors and built documents.
 * ------------------------------------------------------------------------- */
typedef struct {
	uint8_t* p;
	size_t   n;
	size_t   cap;
} buf_t;

static void buf_put(buf_t* b, const void* data, size_t len)
{
	if (b->n + len > b->cap) {
		size_t nc = b->cap ? b->cap : 256;
		while (nc < b->n + len) nc *= 2;
		b->p = xrealloc(b->p, nc);
		b->cap = nc;
	}
	if (len) memcpy(b->p + b->n, data, len);
	b->n += len;
}

static void buf_u64(buf_t* b, uint64_t v) { buf_put(b, &v, sizeof(v)); }

static void buf_free(buf_t* b)
{
	free(b->p);
	b->p = NULL;
	b->n = b->cap = 0;
}

static bool buf_eq(const buf_t* a, const buf_t* b)
{
	return a->n == b->n && (a->n == 0 || memcmp(a->p, b->p, a->n) == 0);
}

/* xorshift64*, so a failing partition can be reproduced from its seed. */
static uint64_t rng_next(uint64_t* s)
{
	uint64_t x = *s;
	x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
	*s = x;
	return x * UINT64_C(0x2545F4914F6CDD1D);
}

/* ---------------------------------------------------------------------------
 * The record of one read.
 * ------------------------------------------------------------------------- */
typedef struct {
	buf_t    log;            /* events of both streams, and on_error calls */
	/* What the src_mirror received. Compared for complete reads only: the
	 * text loop mirrors a buffer once it has been lexed, so after an error
	 * the mirror holds whatever earlier buffers there were, which depends on
	 * where the input was cut -- for a pull source as much as for a fed one. */
	buf_t    mirror;
	bool     ok;
	bool     feeds_ok;       /* every bvnr_read_feed returned true */
	error_code_t err;
	uint64_t line, column, offset, recoveries, skipped;
	uint32_t byte;
	bool     has_version;
	uint16_t vmajor, vminor;
	/* Abort after this many verified events (0: never). */
	uint32_t abort_after;
	uint32_t verified;
} rec_t;

static void log_bytes(buf_t* b, const void* p, uint32_t n)
{
	buf_u64(b, n);
	if (n && p) buf_put(b, p, n);
}

static void log_event(rec_t* r, uint8_t which, bvnr_event_t ev,
	const bvnr_data_t* d)
{
	buf_t* b = &r->log;
	buf_put(b, &which, 1);
	buf_u64(b, (uint64_t)ev);
	if (!d) return;
	buf_u64(b, (uint64_t)d->type);
	buf_u64(b, (uint64_t)d->value_type.family);
	buf_u64(b, d->value_type.width);
	buf_u64(b, d->value_type.base);
	buf_u64(b, d->value_unit.num_components);
	for (uint32_t i = 0; i < d->value_unit.num_components &&
	     i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t* c = &d->value_unit.components[i];
		buf_u64(b, (uint64_t)c->base);
		buf_u64(b, (uint64_t)(int64_t)c->exponent);
		buf_u64(b, (uint64_t)c->prefix.system);
		buf_u64(b, (uint64_t)c->prefix.id.si);
	}
	log_bytes(b, d->data, d->length);
	log_bytes(b, d->frac_data, d->frac_length);
	log_bytes(b, d->unit_source, d->unit_source_length);
	buf_u64(b, d->converted);
}

static bool on_unverified(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	log_event((rec_t*)ud, 'u', ev, d);
	return true;
}

static bool on_verified(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	rec_t* r = ud;
	log_event(r, 'v', ev, d);
	if (r->abort_after && ++r->verified >= r->abort_after)
		return false;
	return true;
}

static void on_error(void* ud, error_code_t err, uint64_t line, uint64_t col,
	uint32_t byte, uint64_t off)
{
	rec_t* r = ud;
	uint8_t tag = 'e';
	buf_put(&r->log, &tag, 1);
	buf_u64(&r->log, (uint64_t)err);
	buf_u64(&r->log, line);
	buf_u64(&r->log, col);
	buf_u64(&r->log, byte);
	buf_u64(&r->log, off);
}

/* The reader configurations every document is read under. */
enum {
	CFG_PLAIN, CFG_RECOVER, CFG_CAP, CFG_STRICT, CFG_TEXT_ONLY, CFG_ABORT,
	CFG_SMALL_TEXT, CFG__COUNT
};

static void cfg_flags(int cfg, uint64_t doc_len, rec_t* r,
	bvnr_read_flags_t* f)
{
	memset(f, 0, sizeof(*f));
	f->userdata      = r;
	f->on_verified   = on_verified;
	f->on_unverified = on_unverified;
	f->on_error      = on_error;
	switch (cfg) {
	case CFG_RECOVER:
		f->continue_on_error = true;
		break;
	case CFG_CAP:
		/* Cut the document in the middle, so the cap lands inside text, an
		 * octet chunk or a multi-byte character depending on the input. */
		f->max_file_size = doc_len > 2 ? doc_len / 2 : 1;
		break;
	case CFG_STRICT:
		f->continue_on_error = true;
		f->strict_version    = true;
		break;
	case CFG_TEXT_ONLY:
		f->text_only = true;
		break;
	case CFG_ABORT:
		r->abort_after = 5;
		break;
	case CFG_SMALL_TEXT:
		f->continue_on_error = true;
		f->max_text_bytes    = doc_len > 4 ? doc_len / 3 : 1;
		f->max_string_length = 8;
		break;
	default:
		break;
	}
}

static bool mirror_push(bvnr_sink_t* s, const void* p, uint32_t n)
{
	buf_put((buf_t*)bvnr_sink_get_userdata(s), p, n);
	return true;
}

static void rec_finish(rec_t* r, const bvnr_reader_t* rd)
{
	r->err        = bvnr_reader_get_error(rd);
	r->line       = bvnr_reader_get_error_line(rd);
	r->column     = bvnr_reader_get_error_column(rd);
	r->byte       = bvnr_reader_get_error_byte(rd);
	r->offset     = bvnr_reader_get_error_offset(rd);
	r->recoveries = bvnr_reader_get_recovery_count(rd);
	r->skipped    = bvnr_reader_get_skipped_bytes(rd);
	r->has_version = bvnr_reader_get_declared_version(rd, &r->vmajor,
							   &r->vminor);
}

static void rec_free(rec_t* r)
{
	buf_free(&r->log);
	buf_free(&r->mirror);
}

/* The reference: bvnr_read over the whole buffer. */
static void read_whole(const uint8_t* doc, size_t n, int cfg, rec_t* r)
{
	memset(r, 0, sizeof(*r));
	bvnr_read_flags_t f;
	cfg_flags(cfg, n, r, &f);
	bvnr_source_t src;
	bvnr_sink_t mirror;
	bvnr_source_from_mem(&src, doc, n);
	bvnr_sink_to_callback(&mirror, mirror_push, NULL, &r->mirror);
	bvnr_reader_t* rd = bvnr_reader_create();
	r->ok = bvnr_open_read_source(rd, &src, &mirror, &f) && bvnr_read(rd);
	r->feeds_ok = true;
	rec_finish(r, rd);
	bvnr_reader_destroy(rd);
}

/* The same bytes fed in pieces: cuts[] are the piece ends, ascending, the
 * last one n. */
static void read_fed(const uint8_t* doc, size_t n, int cfg,
	const size_t* cuts, size_t ncuts, rec_t* r)
{
	memset(r, 0, sizeof(*r));
	bvnr_read_flags_t f;
	cfg_flags(cfg, n, r, &f);
	bvnr_sink_t mirror;
	bvnr_sink_to_callback(&mirror, mirror_push, NULL, &r->mirror);
	bvnr_reader_t* rd = bvnr_reader_create();
	r->feeds_ok = true;
	bool alive = bvnr_open_read_feed(rd, &mirror, &f);
	size_t at = 0;
	for (size_t i = 0; alive && i < ncuts; i++) {
		/* Feed from a private copy, overwritten afterwards: the reader must
		 * not keep pointing into a buffer it was handed. */
		size_t len = cuts[i] - at;
		uint8_t* piece = xmalloc(len ? len : 1);
		if (len) memcpy(piece, doc + at, len);
		alive = bvnr_read_feed(rd, piece, len);
		if (len) memset(piece, 0xA5, len);
		free(piece);
		if (!alive) r->feeds_ok = false;
		at = cuts[i];
	}
	r->ok = alive && bvnr_read_finish(rd);
	rec_finish(r, rd);
	bvnr_reader_destroy(rd);
}

/* A pull source handing out the document in random short reads. */
typedef struct {
	const uint8_t* p;
	size_t         left;
	uint64_t       seed;
	size_t         max_step;
} trickle_t;

static bool trickle_pull(bvnr_source_t* s, void* out, uint32_t want,
	uint32_t* got)
{
	trickle_t* t = bvnr_source_get_userdata(s);
	size_t step = 1 + (size_t)(rng_next(&t->seed) % t->max_step);
	if (step > want) step = want;
	if (step > t->left) step = t->left;
	memcpy(out, t->p, step);
	t->p    += step;
	t->left -= step;
	*got = (uint32_t)step;
	return true;
}

static void read_trickle(const uint8_t* doc, size_t n, int cfg, uint64_t seed,
	size_t max_step, rec_t* r)
{
	memset(r, 0, sizeof(*r));
	bvnr_read_flags_t f;
	cfg_flags(cfg, n, r, &f);
	trickle_t t = { doc, n, seed | 1u, max_step };
	bvnr_source_t src;
	bvnr_sink_t mirror;
	bvnr_source_from_callback(&src, trickle_pull, &t);
	bvnr_sink_to_callback(&mirror, mirror_push, NULL, &r->mirror);
	bvnr_reader_t* rd = bvnr_reader_create();
	r->ok = bvnr_open_read_source(rd, &src, &mirror, &f) && bvnr_read(rd);
	r->feeds_ok = true;
	rec_finish(r, rd);
	bvnr_reader_destroy(rd);
}

static int g_mismatch_reports = 0;

/* Compare a record with the reference; report the first few differences in
 * enough detail to reproduce them. */
static bool rec_same(const rec_t* a, const rec_t* b, const char* name,
	int cfg, const char* how)
{
	bool same = a->ok == b->ok && a->err == b->err && a->line == b->line &&
		a->column == b->column && a->byte == b->byte &&
		a->offset == b->offset && a->recoveries == b->recoveries &&
		a->skipped == b->skipped && a->has_version == b->has_version &&
		(!a->has_version || (a->vmajor == b->vmajor &&
				     a->vminor == b->vminor)) &&
		buf_eq(&a->log, &b->log) &&
		(!b->ok || buf_eq(&a->mirror, &b->mirror));
	if (!same && g_mismatch_reports++ < 12) {
		fprintf(stderr, "  MISMATCH %s cfg %d (%s): ok %d/%d err %d/%d "
			"line %" PRIu64 "/%" PRIu64 " col %" PRIu64 "/%" PRIu64
			" off %" PRIu64 "/%" PRIu64 " byte %u/%u rec %" PRIu64
			"/%" PRIu64 " skip %" PRIu64 "/%" PRIu64
			" log %zu/%zu%s mirror %zu/%zu%s\n",
			name, cfg, how, a->ok, b->ok, a->err, b->err,
			a->line, b->line, a->column, b->column,
			a->offset, b->offset, a->byte, b->byte,
			a->recoveries, b->recoveries, a->skipped, b->skipped,
			a->log.n, b->log.n,
			buf_eq(&a->log, &b->log) ? "" : " (differs)",
			a->mirror.n, b->mirror.n,
			buf_eq(&a->mirror, &b->mirror) ? "" : " (differs)");
	}
	return same;
}

/* ---------------------------------------------------------------------------
 * The corpus.
 * ------------------------------------------------------------------------- */
typedef struct {
	char*    name;
	uint8_t* data;
	size_t   len;
} doc_t;

static doc_t* g_docs;
static size_t g_ndocs, g_docs_cap;

static void corpus_add(const char* name, const void* data, size_t len)
{
	if (g_ndocs == g_docs_cap) {
		g_docs_cap = g_docs_cap ? g_docs_cap * 2 : 64;
		g_docs = xrealloc(g_docs, g_docs_cap * sizeof(*g_docs));
	}
	doc_t* d = &g_docs[g_ndocs++];
	d->name = xmalloc(strlen(name) + 1);
	d->data = xmalloc(len);
	strcpy(d->name, name);
	if (len) memcpy(d->data, data, len);
	d->len = len;
}

#define LIT(s) s, sizeof(s) - 1

/* Hand-picked inputs: every lexer region a cut can land in -- BOM, version
 * directive, comments, strings with escapes, multi-byte characters, numbers,
 * annotations, datetime literals, arrays, structs, references, octet streams
 * -- plus the errors and recoveries whose positions must not move. */
static void corpus_builtin(void)
{
	static const struct { const char* name; const char* s; size_t n; } T[] = {
		{ "empty",        LIT("") },
		{ "ws",           LIT("  \n\t \r\n") },
		{ "scalars",      LIT(".a = 42;\n.b = -17;\n.c = 3.14;\n.d = true;\n.e = ;\n.f = nan;\n") },
		{ "string_esc",   LIT(".s = \"tab\\there \\\"q\\\" \\\\ nl\\n\";\n") },
		{ "utf8",         LIT(".s = \"\xc3\xa7\x61\x66\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80\";\n.\xc3\xa9t\xc3\xa9 = 1;\n") },
		{ "bom",          LIT("\xEF\xBB\xBF.x = 1;\n") },
		{ "bom_comment",  LIT("\xEF\xBB\xBF#!bovnar 1.1\n.a = 1;\n") },
		{ "version",      LIT("#!bovnar 1.2\n.u = <float:64,ucum:m/s> 1.5;\n") },
		{ "version_eof",  LIT("#!bovnar 1.1") },
		{ "version_bad",  LIT("#!bovnar 9.9\n.a = 1;\n") },
		{ "version_junk", LIT("#!bovnar 1.x\n.a = 1;\n") },
		{ "comments",     LIT("# lead\n.a = 1; # tail\n# more\n.b = 2;# no newline") },
		{ "annot",        LIT(".x = <uint:8> 255;\n.y = <float:64,k~g\xC2\xB7m/s\xC2\xB2> 9.81;\n.z = <uint:_16> \"ff\";\n") },
		{ "inline_unit",  LIT(".l = 1.5 km;\n.t = 20 \xC2\xB0""C;\n") },
		{ "datetime",     LIT("#!bovnar 1.1\n.t = 2026-06-15T12:00:00.123+05:30;\n.d = 2026-06-15;\n") },
		{ "esc11",        LIT("#!bovnar 1.1\n.s = \"\\x41\\xC3\\xA9\\u{1F600}\";\n") },
		{ "arrays",       LIT(".a = [1, 2, 3]/[4, 5, 6];\n.b = [[1,2],[3,4]];\n.c = [,1,,2,];\n.d = [\"a\", \"b\"];\n") },
		{ "structs",      LIT(".s = { .x = 1; .inner = { .v = [1,2]; }; };\n.a = [{.n = 1;},{.n = 2;}];\n") },
		{ "refs",         LIT("#!bovnar 1.1\n.m = [1,2]/[3,4];\n.r = &.m[1][0];\n.h = \"x\";\n.q = &.h;\n") },
		{ "symbols",      LIT(".s = ok;\n.f = [red, green];\n") },
		{ "trunc_string", LIT(".s = \"unterminated") },
		{ "trunc_struct", LIT(".s = { .a = 1;") },
		{ "trunc_utf8",   LIT(".a = 1; #\xc3") },
		{ "bad_byte",     LIT(".a = 1;\n.b = @;\n.c = 3;\n") },
		{ "bad_utf8",     LIT(".a = \"caf\xe9 cr\xe8me\"; .b = 2;\n") },
		{ "stray_euro",   LIT(".a = 1; \xe2\x82\xac .b = 2;\n") },
		{ "missing_semi", LIT(".s = {.a = 1 }; .b = 2;\n") },
		{ "range",        LIT(".x = <uint:8> 256;\n.y = 1;\n") },
		{ "long_string",  LIT(".s = \"0123456789abcdefghijklmnopqrstuvwxyz\";\n.t = 1;\n") },
		{ "ws_in_param",  LIT(".x = <float:64,k g> 1.0;\n.y = 2;\n") },
	};
	for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++)
		corpus_add(T[i].name, T[i].s, T[i].n);

	static const uint8_t oct_small[] = {
		'.','b',' ','=',' ', 0x00,
		0x01, 0x05,0x00, 'h','e','l','l','o',
		0x01, 0x03,0x00, 'b','y','e',
		0x00, ';','\n', '.','c',' ','=',' ','1',';','\n'
	};
	corpus_add("octet_small", oct_small, sizeof(oct_small));
	static const uint8_t oct_bad_tag[] = {
		'.','b','=', 0x00, 0x01, 0x02,0x00, 'h','i', 0x07, ';'
	};
	corpus_add("octet_bad_tag", oct_bad_tag, sizeof(oct_bad_tag));
	static const uint8_t oct_trunc[] = {
		'.','b','=', 0x00, 0x01, 0x09,0x00, 'h','i'
	};
	corpus_add("octet_trunc", oct_trunc, sizeof(oct_trunc));
}

/* Documents written by the library itself: octet streams with chunks of every
 * size class, including the 65536-byte chunk whose length field is 0, and a
 * multiplexed stream whose messages straddle chunks. */
static bool sink_buf_push(bvnr_sink_t* s, const void* p, uint32_t n)
{
	buf_put((buf_t*)bvnr_sink_get_userdata(s), p, n);
	return true;
}

static void corpus_generated(void)
{
	buf_t out = {0};
	bvnr_sink_t sink;
	bvnr_writer_t* w = bvnr_writer_create();
	uint8_t* payload = xmalloc(400000);
	for (size_t i = 0; i < 400000; i++)
		payload[i] = (uint8_t)(i * 131u + (i >> 9));

	/* One octet stream: chunks of 1, 300, 65535 and 65536 bytes. */
	bvnr_sink_to_callback(&sink, sink_buf_push, NULL, &out);
	bvnr_open_write_sink(w, &sink, false, NULL);
	bvnr_write_uint(w, "before", 32, 7);
	bvnr_data_t d = {0};
	d.data = "blob"; d.length = 4;
	bvnr_write_event(w, ev_assignment_start, &d);
	bvnr_data_t empty = {0};
	bvnr_write_event(w, ev_octet_stream_start, &empty);
	static const uint32_t sizes[] = { 1, 300, 65535, 65536, 2 };
	size_t at = 0;
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		bvnr_data_t c = {0};
		c.type = token_is_octet_stream;
		c.data = payload + at; c.length = sizes[i];
		bvnr_write_event(w, ev_data, &c);
		at += sizes[i];
	}
	bvnr_write_event(w, ev_octet_stream_end, &empty);
	bvnr_write_string(w, "after", "\xc3\xa9t\xc3\xa9");
	if (!bvnr_write_finish(w)) { fprintf(stderr, "writer failed\n"); exit(2); }
	corpus_add("gen_octet_chunks", out.p, out.n);
	/* The same, cut inside the 65536-byte chunk and at its closing tag. */
	corpus_add("gen_octet_cut_chunk", out.p, out.n / 2);
	out.n = 0;

	/* A multiplexed stream: 150 B, 30 KB and 400 KB messages on three
	 * channels, the last spanning seven chunks. */
	bvnr_sink_to_callback(&sink, sink_buf_push, NULL, &out);
	bvnr_open_write_sink(w, &sink, false, NULL);
	bvnr_mux_begin(w, "mux");
	bvnr_mux_send(w, 1, payload, 150);
	bvnr_mux_send(w, 300, payload + 150, 30000);
	bvnr_mux_send(w, 1, payload, 0);
	bvnr_mux_send(w, 70000, payload, 400000);
	bvnr_mux_send(w, 1, payload + 7, 150);
	bvnr_mux_end(w);
	bvnr_write_uint(w, "tail", 8, 1);
	if (!bvnr_write_finish(w)) { fprintf(stderr, "writer failed\n"); exit(2); }
	corpus_add("gen_mux", out.p, out.n);
	out.n = 0;

	bvnr_writer_destroy(w);
	free(payload);
	buf_free(&out);
}

static void corpus_dir(const char* dir)
{
#if !defined(_WIN32)
	DIR* dp = opendir(dir);
	if (!dp) {
		fprintf(stderr, "cannot open %s\n", dir);
		g_failures++;
		return;
	}
	struct dirent* e;
	while ((e = readdir(dp)) != NULL) {
		size_t nl = strlen(e->d_name);
		if (nl < 6 || strcmp(e->d_name + nl - 5, ".bvnr") != 0)
			continue;
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		FILE* fp = fopen(path, "rb");
		if (!fp) continue;
		buf_t b = {0};
		uint8_t tmp[65536];
		size_t got;
		while ((got = fread(tmp, 1, sizeof(tmp), fp)) > 0)
			buf_put(&b, tmp, got);
		fclose(fp);
		corpus_add(e->d_name, b.p, b.n);
		buf_free(&b);
	}
	closedir(dp);
#else
	(void)dir;
#endif
}

/* ---------------------------------------------------------------------------
 * Feed equivalence.
 * ------------------------------------------------------------------------- */

/* Every cut position is quadratic in the document; past this size the
 * partitions are sampled instead. */
#define EVERY_CUT_MAX 2048u

static void test_feed_equivalence(void)
{
	printf("feed reader: equivalence with bvnr_read over %zu documents\n",
	       g_ndocs);
	uint64_t runs = 0, mismatches = 0, feed_false = 0;
	size_t* cuts = NULL;
	for (size_t di = 0; di < g_ndocs; di++) {
		const doc_t* doc = &g_docs[di];
		size_t n = doc->len;
		cuts = xrealloc(cuts, (n + 2) * sizeof(*cuts));
		for (int cfg = 0; cfg < CFG__COUNT; cfg++) {
			rec_t ref, got;
			read_whole(doc->data, n, cfg, &ref);

			/* One piece, and nothing fed at all before the finish. */
			cuts[0] = n;
			read_fed(doc->data, n, cfg, cuts, 1, &got);
			runs++;
			if (!rec_same(&got, &ref, doc->name, cfg, "whole"))
				mismatches++;
			rec_free(&got);

			/* Two pieces, cut at every position. */
			if (n <= EVERY_CUT_MAX) {
				for (size_t k = 0; k <= n; k++) {
					cuts[0] = k; cuts[1] = n;
					read_fed(doc->data, n, cfg, cuts, 2, &got);
					runs++;
					char how[48];
					snprintf(how, sizeof(how), "cut at %zu", k);
					if (!rec_same(&got, &ref, doc->name, cfg, how))
						mismatches++;
					if (ref.ok && !got.feeds_ok)
						feed_false++;
					rec_free(&got);
				}
			}

			/* Single bytes, and fixed sizes around the chunk and window
			 * boundaries. */
			static const size_t steps[] = {
				1, 2, 3, 7, 4096, 65535, 65536, 65537, 1u << 20 };
			for (size_t si = 0; si < sizeof(steps) / sizeof(steps[0]);
			     si++) {
				if (steps[si] > 1 && steps[si] >= n && si > 3)
					continue;
				if (steps[si] == 1 && n > 600000)
					continue;
				size_t nc = 0;
				for (size_t at = steps[si]; at < n; at += steps[si])
					cuts[nc++] = at;
				cuts[nc++] = n;
				read_fed(doc->data, n, cfg, cuts, nc, &got);
				runs++;
				char how[48];
				snprintf(how, sizeof(how), "pieces of %zu", steps[si]);
				if (!rec_same(&got, &ref, doc->name, cfg, how))
					mismatches++;
				if (ref.ok && !got.feeds_ok)
					feed_false++;
				rec_free(&got);
			}

			/* Random partitions, and the pull path reading the same bytes
			 * through a callback source in random short reads. */
			int rounds = n <= EVERY_CUT_MAX ? 8 : 24;
			for (int rr = 0; rr < rounds; rr++) {
				uint64_t seed = (uint64_t)di * 1000003u +
						(uint64_t)cfg * 101u + (uint64_t)rr + 1u;
				uint64_t s = seed;
				size_t max_piece = 1 + (size_t)(rng_next(&s) %
					(n > 3 ? (rr & 1 ? 64 : n / 2) : 3));
				size_t nc = 0, at = 0;
				while (at < n) {
					at += 1 + (size_t)(rng_next(&s) % max_piece);
					if (at > n) at = n;
					cuts[nc++] = at;
				}
				if (!nc) cuts[nc++] = n;
				read_fed(doc->data, n, cfg, cuts, nc, &got);
				runs++;
				char how[64];
				snprintf(how, sizeof(how), "random seed %" PRIu64, seed);
				if (!rec_same(&got, &ref, doc->name, cfg, how))
					mismatches++;
				if (ref.ok && !got.feeds_ok)
					feed_false++;
				rec_free(&got);

				read_trickle(doc->data, n, cfg, seed, max_piece, &got);
				runs++;
				snprintf(how, sizeof(how), "trickle seed %" PRIu64, seed);
				if (!rec_same(&got, &ref, doc->name, cfg, how))
					mismatches++;
				rec_free(&got);
			}
			rec_free(&ref);
		}
	}
	free(cuts);
	printf("  %" PRIu64 " fed or trickled reads compared\n", runs);
	ASSERT_EQ_UINT(mismatches, 0, "fed reads match bvnr_read");
	ASSERT_EQ_UINT(feed_false, 0,
		"no feed fails on a document bvnr_read accepts");
}

/* Truncation is a finish result: every prefix of a valid document feeds
 * without error, and finish then says what bvnr_read says of the prefix. */
static void test_feed_prefixes(void)
{
	printf("feed reader: every prefix feeds, finish reports the truncation\n");
	uint64_t bad = 0;
	for (size_t di = 0; di < g_ndocs; di++) {
		const doc_t* doc = &g_docs[di];
		if (doc->len > 1024) continue;
		rec_t whole;
		read_whole(doc->data, doc->len, CFG_PLAIN, &whole);
		bool valid = whole.ok;
		rec_free(&whole);
		if (!valid) continue;
		for (size_t k = 0; k < doc->len; k++) {
			rec_t ref, got;
			read_whole(doc->data, k, CFG_PLAIN, &ref);
			size_t cut = k;
			read_fed(doc->data, k, CFG_PLAIN, &cut, 1, &got);
			if (!got.feeds_ok || !rec_same(&got, &ref, doc->name,
						       CFG_PLAIN, "prefix"))
				bad++;
			rec_free(&ref);
			rec_free(&got);
		}
	}
	ASSERT_EQ_UINT(bad, 0, "prefixes feed cleanly and finish like bvnr_read");
}

/* The calls around the feed: a pull read refuses a fed reader and a feed
 * refuses a pull reader; after the end nothing changes the error; a reader
 * is reusable. */
static void test_feed_api_edges(void)
{
	printf("feed reader: API edges\n");
	bvnr_reader_t* r = bvnr_reader_create();
	ASSERT_TRUE(!bvnr_read_feed(NULL, "x", 1), "NULL reader");
	ASSERT_TRUE(!bvnr_read_finish(NULL), "NULL reader finish");

	ASSERT_TRUE(bvnr_open_read_mem(r, ".a=1;", 5, NULL, 0, NULL), "open mem");
	ASSERT_TRUE(!bvnr_read_feed(r, ".a=1;", 5), "feed on a pull reader");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_invalid_argument,
		"feed on a pull reader is error_invalid_argument");

	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, NULL), "open feed");
	ASSERT_TRUE(!bvnr_read(r), "bvnr_read on a fed reader");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_invalid_argument,
		"bvnr_read on a fed reader is error_invalid_argument");
	/* Mid-read, too: the misuse ends the read, so the error it shows is
	 * never one the read went on past. */
	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, NULL), "open feed again");
	ASSERT_TRUE(bvnr_read_feed(r, ".a = 1; .b = ", 13), "feed a part");
	ASSERT_TRUE(!bvnr_read(r), "bvnr_read mid-feed");
	ASSERT_TRUE(!bvnr_read_feed(r, "2;", 2), "the read is over");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_invalid_argument,
		"and its error stays error_invalid_argument");

	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, NULL), "re-open feed");
	ASSERT_TRUE(bvnr_read_feed(r, NULL, 0), "empty feed");
	ASSERT_TRUE(bvnr_read_feed(r, ".a = ", 5), "feed part 1");
	ASSERT_TRUE(bvnr_read_feed(r, "1;", 2), "feed part 2");
	ASSERT_TRUE(bvnr_read_finish(r), "finish");
	ASSERT_TRUE(!bvnr_read_feed(r, ".b=2;", 5), "feed after finish");
	ASSERT_TRUE(!bvnr_read_finish(r), "second finish");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_none,
		"error getters keep the finished read's state");

	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, NULL), "re-open after finish");
	ASSERT_TRUE(bvnr_read_feed(r, ".a = @", 6) == false, "syntax error");
	error_code_t e = bvnr_reader_get_error(r);
	uint64_t off = bvnr_reader_get_error_offset(r);
	ASSERT_EQ_UINT(e, error_unexpected_input_byte, "error code");
	ASSERT_EQ_UINT(off, 5, "error offset");
	ASSERT_TRUE(!bvnr_read_feed(r, ";", 1), "feed after error");
	ASSERT_TRUE(!bvnr_read_finish(r), "finish after error");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), e, "error kept");
	ASSERT_EQ_UINT(bvnr_reader_get_error_offset(r), off, "offset kept");

	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, NULL), "re-open");
	ASSERT_TRUE(!bvnr_read_feed(r, NULL, 3), "NULL buffer with a length");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_invalid_argument,
		"NULL buffer is error_invalid_argument");

	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, NULL), "re-open");
	ASSERT_TRUE(bvnr_read_feed(r, ".a = {", 6), "open struct");
	ASSERT_TRUE(!bvnr_read_finish(r), "finish inside a struct");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_got_incomplete_bvnr_stream,
		"truncation is reported by finish");
	bvnr_reader_destroy(r);
}

/* An endless document: a million values fed in small pieces, memory flat. */
typedef struct { uint64_t values; } count_ctx_t;

static bool count_values(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	(void)d;
	if (ev == ev_data) ((count_ctx_t*)ud)->values++;
	return true;
}

static void test_feed_endless(void)
{
	printf("feed reader: an endless document in small pieces\n");
	count_ctx_t c = {0};
	bvnr_read_flags_t f;
	memset(&f, 0, sizeof(f));
	f.userdata    = &c;
	f.on_verified = count_values;
	f.max_text_bytes = UINT64_MAX;
	bvnr_reader_t* r = bvnr_reader_create();
	ASSERT_TRUE(bvnr_open_read_feed(r, NULL, &f), "open");
	bool ok = true;
	char line[64];
	for (uint32_t i = 0; ok && i < 1000000u; i++) {
		int n = snprintf(line, sizeof(line), ".k%u = %u;\n", i % 97u, i);
		/* Uneven cuts, so tokens straddle feeds. */
		int cut = (int)(i % (uint32_t)n);
		ok = bvnr_read_feed(r, line, (uint64_t)cut) &&
		     bvnr_read_feed(r, line + cut, (uint64_t)(n - cut));
	}
	ASSERT_TRUE(ok, "every feed accepted");
	ASSERT_TRUE(bvnr_read_finish(r), "finish");
	ASSERT_EQ_UINT(c.values, 1000000u, "every value delivered");
	bvnr_reader_destroy(r);
}

/* ---------------------------------------------------------------------------
 * Callback sources and sinks.
 * ------------------------------------------------------------------------- */
typedef struct {
	buf_t    out;
	uint64_t fail_after;    /* refuse the push that would pass this total */
	uint32_t pushes;
	uint32_t flushes;
	bool     flush_fails;
	bool     saw_copy;      /* the handle passed in was not the caller's */
	const bvnr_sink_t* own;
} cb_sink_t;

static bool cb_push(bvnr_sink_t* s, const void* p, uint32_t n)
{
	cb_sink_t* c = bvnr_sink_get_userdata(s);
	if (!c) return false;
	if (s != c->own) c->saw_copy = true;
	c->pushes++;
	if (c->fail_after && c->out.n + n > c->fail_after)
		return false;
	buf_put(&c->out, p, n);
	return true;
}

static bool cb_flush(bvnr_sink_t* s)
{
	cb_sink_t* c = bvnr_sink_get_userdata(s);
	c->flushes++;
	return !c->flush_fails;
}

typedef struct {
	const uint8_t* p;
	size_t         left;
	bool           fail;
	bool           saw_copy;
	const bvnr_source_t* own;
} cb_src_t;

static bool cb_pull(bvnr_source_t* s, void* out, uint32_t want, uint32_t* got)
{
	cb_src_t* c = bvnr_source_get_userdata(s);
	if (s != c->own) c->saw_copy = true;
	if (c->fail) { *got = 0; return false; }
	uint32_t n = want < 3u ? want : 3u;       /* short reads on purpose */
	if (n > c->left) n = (uint32_t)c->left;
	memcpy(out, c->p, n);
	c->p += n;
	c->left -= n;
	*got = n;
	return true;
}

static uint64_t write_sample(bvnr_writer_t* w)
{
	bool ok = bvnr_write_uint(w, "id", 32, 7) &&
		  bvnr_write_string(w, "name", "caf\xc3\xa9") &&
		  bvnr_write_struct_start(w, "s") &&
		  bvnr_write_bool(w, "f", true) &&
		  bvnr_write_struct_end(w);
	return ok ? 1 : 0;
}

static void test_callback_io(void)
{
	printf("callback source and sink\n");
	bvnr_source_t s0;
	bvnr_sink_t k0;
	bvnr_source_from_mem(&s0, "x", 1);
	bvnr_sink_to_mem(&k0, (char[4]){0}, 4);
	ASSERT_TRUE(bvnr_source_get_userdata(&s0) == NULL,
		"a memory source has no userdata");
	ASSERT_TRUE(bvnr_sink_get_userdata(&k0) == NULL,
		"a memory sink has no userdata");
	ASSERT_TRUE(bvnr_source_get_userdata(NULL) == NULL, "NULL source");
	ASSERT_TRUE(bvnr_sink_get_userdata(NULL) == NULL, "NULL sink");

	/* Write through a callback sink, read back through a callback source. */
	cb_sink_t cs = {0};
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	cs.own = &sink;
	ASSERT_TRUE(bvnr_sink_get_userdata(&sink) == &cs, "sink userdata");
	bvnr_writer_t* w = bvnr_writer_create();
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, true, NULL), "open sink");
	ASSERT_TRUE(write_sample(w), "write");
	ASSERT_EQ_UINT(cs.pushes, 0, "small output stays in the writer buffer");
	ASSERT_TRUE(bvnr_write_finish(w), "finish");
	ASSERT_EQ_UINT(cs.flushes, 1, "finish flushes once");
	ASSERT_EQ_UINT(cs.pushes, 1, "one batched push");
	ASSERT_TRUE(cs.saw_copy, "the writer pushes through its own copy");
	ASSERT_EQ_UINT(bvnr_writer_bytes_written(w), cs.out.n,
		"bytes_written counts the callback sink");

	uint8_t mem[512];
	bvnr_writer_t* wm = bvnr_writer_create();
	bvnr_open_write_mem(wm, mem, sizeof(mem), true, NULL);
	write_sample(wm);
	bvnr_write_finish(wm);
	ASSERT_TRUE(bvnr_writer_bytes_written(wm) == cs.out.n &&
		    memcmp(mem, cs.out.p, cs.out.n) == 0,
		"callback sink output equals memory sink output");
	bvnr_writer_destroy(wm);

	cb_src_t src_ctx = { cs.out.p, cs.out.n, false, false, NULL };
	bvnr_source_t src;
	bvnr_source_from_callback(&src, cb_pull, &src_ctx);
	src_ctx.own = &src;
	ASSERT_TRUE(bvnr_source_get_userdata(&src) == &src_ctx, "source userdata");
	rec_t rec;
	memset(&rec, 0, sizeof(rec));
	bvnr_read_flags_t f;
	cfg_flags(CFG_PLAIN, 0, &rec, &f);
	bvnr_reader_t* r = bvnr_reader_create();
	ASSERT_TRUE(bvnr_open_read_source(r, &src, NULL, &f) && bvnr_read(r),
		"read back through a callback source");
	ASSERT_TRUE(src_ctx.saw_copy, "the reader pulls through its own copy");
	rec_t ref;
	read_whole(cs.out.p, cs.out.n, CFG_PLAIN, &ref);
	ASSERT_TRUE(buf_eq(&rec.log, &ref.log), "same events as a memory read");
	rec_free(&rec);
	rec_free(&ref);

	/* A failing pull is an I/O error, not end of input. */
	src_ctx.fail = true;
	bvnr_source_from_callback(&src, cb_pull, &src_ctx);
	ASSERT_TRUE(bvnr_open_read_source(r, &src, NULL, NULL) && !bvnr_read(r),
		"failing pull fails the read");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_reading_from_source_fd,
		"failing pull is error_reading_from_source_fd");
	bvnr_reader_destroy(r);

	/* A failing push is error_writing_to_sink, never "buffer exhausted". */
	buf_free(&cs.out);
	memset(&cs, 0, sizeof(cs));
	cs.fail_after = 10;
	bvnr_sink_to_callback(&sink, cb_push, NULL, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "re-open");
	write_sample(w);
	ASSERT_TRUE(!bvnr_write_finish(w), "finish fails on a refused push");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_writing_to_sink,
		"refused push is error_writing_to_sink");

	/* Large output goes out in 64 KiB pushes, not one per token. */
	buf_free(&cs.out);
	memset(&cs, 0, sizeof(cs));
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "re-open");
	for (uint32_t i = 0; i < 20000; i++)
		bvnr_write_uint(w, "value", 32, i);
	ASSERT_TRUE(bvnr_write_finish(w), "large finish");
	ASSERT_TRUE(cs.pushes > 1 && cs.pushes <= cs.out.n / 65536u + 1u,
		"the writer batches into 64 KiB pushes");

	/* A failing flush fails the finish with error_writing_to_sink. */
	buf_free(&cs.out);
	memset(&cs, 0, sizeof(cs));
	cs.flush_fails = true;
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "re-open");
	write_sample(w);
	ASSERT_TRUE(!bvnr_write_finish(w), "finish fails on a refused flush");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_writing_to_sink,
		"refused flush is error_writing_to_sink");
	ASSERT_EQ_UINT(cs.flushes, 1, "flush called once");
	buf_free(&cs.out);
	bvnr_writer_destroy(w);

	/* A callback sink as the frame writer's target and a callback source
	 * as bvnr_doc_stream_read's. */
	memset(&cs, 0, sizeof(cs));
	bvnr_sink_to_callback(&sink, cb_push, NULL, &cs);
	ASSERT_TRUE(bvnr_frame_write(&sink, ".a = 1;", 7) &&
		    bvnr_frame_write(&sink, ".b = 2;", 7), "frames to a callback sink");
	ASSERT_EQ_UINT(bvnr_sink_bytes_written(&sink), 2u * (12u + 7u),
		"bvnr_sink_bytes_written counts a callback sink");
	cb_src_t fs = { cs.out.p, cs.out.n, false, false, NULL };
	bvnr_source_from_callback(&src, cb_pull, &fs);
	uint64_t count = 0;
	ASSERT_TRUE(bvnr_doc_stream_read(&src, NULL, &count) && count == 2,
		"frames from a callback source");
	buf_free(&cs.out);
}

/* ---------------------------------------------------------------------------
 * Partitions shared by the stream tests: for a buffer of n bytes, the cut
 * lists to try. Every two-piece cut for small inputs, then single bytes,
 * fixed sizes and random partitions.
 * ------------------------------------------------------------------------- */
typedef void (*partition_fn)(void* ctx, const size_t* cuts, size_t ncuts,
	const char* how);

static void for_partitions(size_t n, uint64_t seed, partition_fn fn, void* ctx)
{
	size_t* cuts = xmalloc((n + 2) * sizeof(*cuts));
	char how[64];
	cuts[0] = n;
	fn(ctx, cuts, 1, "whole");
	if (n <= EVERY_CUT_MAX) {
		for (size_t k = 0; k <= n; k++) {
			cuts[0] = k; cuts[1] = n;
			snprintf(how, sizeof(how), "cut at %zu", k);
			fn(ctx, cuts, 2, how);
		}
	}
	static const size_t steps[] = { 1, 2, 5, 13, 4096, 65536, 65537 };
	for (size_t si = 0; si < sizeof(steps) / sizeof(steps[0]); si++) {
		if (steps[si] >= n && si > 2) continue;
		if (steps[si] == 1 && n > 600000) continue;
		size_t nc = 0;
		for (size_t at = steps[si]; at < n; at += steps[si])
			cuts[nc++] = at;
		cuts[nc++] = n;
		snprintf(how, sizeof(how), "pieces of %zu", steps[si]);
		fn(ctx, cuts, nc, how);
	}
	for (int rr = 0; rr < 16; rr++) {
		uint64_t s = seed * 7919u + (uint64_t)rr + 1u;
		size_t max_piece = 1 + (size_t)(rng_next(&s) %
			(n > 3 ? (rr & 1 ? 97 : n / 3 + 1) : 3));
		size_t nc = 0, at = 0;
		while (at < n) {
			at += 1 + (size_t)(rng_next(&s) % max_piece);
			if (at > n) at = n;
			cuts[nc++] = at;
		}
		if (!nc) cuts[nc++] = n;
		snprintf(how, sizeof(how), "random %d", rr);
		fn(ctx, cuts, nc, how);
	}
	free(cuts);
}

/* ---------------------------------------------------------------------------
 * Frame reader.
 * ------------------------------------------------------------------------- */
typedef struct {
	rec_t    rec;          /* events through opts->flags */
	buf_t    docs;         /* on_document calls */
	uint32_t veto_at;      /* on_document returns false for this index + 1 */
} frame_ref_t;

static bool frame_on_document(void* ud, uint64_t index, bool ok,
	error_code_t err)
{
	frame_ref_t* f = ud;
	buf_u64(&f->docs, index);
	buf_u64(&f->docs, ok);
	buf_u64(&f->docs, (uint64_t)err);
	/* The event record marks the document boundary too, so a document's
	 * events cannot leak into its neighbour unnoticed. */
	uint8_t tag = 'D';
	buf_put(&f->rec.log, &tag, 1);
	return !(f->veto_at && index + 1 == f->veto_at);
}

static void frame_opts(frame_ref_t* f, bvnr_read_flags_t* fl, bool resilient,
	uint64_t max_doc, int cfg, bvnr_doc_stream_opts_t* o)
{
	cfg_flags(cfg, 0, &f->rec, fl);
	memset(o, 0, sizeof(*o));
	o->flags = fl;
	o->userdata = f;
	o->on_document = frame_on_document;
	o->continue_past_failed = resilient;
	o->max_document_size = max_doc;
}

typedef struct {
	const uint8_t* stream;
	size_t         n;
	bool           resilient;
	uint64_t       max_doc;
	uint32_t       veto_at;
	int            cfg;        /* the reader configuration, CFG_PLAIN by default */
	frame_ref_t    ref;
	bool           ref_ok;
	uint64_t       ref_count;
	error_code_t   ref_err;    /* what ended the stream, error_none if nothing */
	uint64_t       mismatches;
	const char*    name;
} frame_case_t;

static void frame_try(void* ctx, const size_t* cuts, size_t ncuts,
	const char* how)
{
	frame_case_t* fc = ctx;
	frame_ref_t got;
	memset(&got, 0, sizeof(got));
	got.veto_at = fc->veto_at;
	bvnr_read_flags_t fl;
	bvnr_doc_stream_opts_t o;
	frame_opts(&got, &fl, fc->resilient, fc->max_doc, fc->cfg, &o);
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create(&o);
	bool alive = true;
	size_t at = 0;
	for (size_t i = 0; alive && i < ncuts; i++) {
		alive = bvnr_frame_reader_feed(fr, fc->stream + at, cuts[i] - at);
		at = cuts[i];
	}
	bool ok = alive && bvnr_frame_reader_finish(fr);
	uint64_t count = bvnr_frame_reader_count(fr);
	error_code_t err = bvnr_frame_reader_error(fr);
	bvnr_frame_reader_destroy(fr);
	bool same = ok == fc->ref_ok && count == fc->ref_count &&
		err == fc->ref_err &&
		buf_eq(&got.docs, &fc->ref.docs) &&
		buf_eq(&got.rec.log, &fc->ref.rec.log);
	if (!same) {
		if (fc->mismatches++ < 4)
			fprintf(stderr, "  MISMATCH frames %s (%s): ok %d/%d "
				"count %" PRIu64 "/%" PRIu64 " error %d/%d\n",
				fc->name, how, ok, fc->ref_ok, count,
				fc->ref_count, (int)err, (int)fc->ref_err);
	}
	buf_free(&got.docs);
	rec_free(&got.rec);
}

/* Pull mode: the documents handed out must be the framed bytes, in order. */
typedef struct {
	const uint8_t* stream;
	size_t         n;
	const buf_t*   expect;     /* the payloads, each prefixed by its u64 length */
	uint64_t       ndocs;
	uint64_t       mismatches;
	uint64_t       zero_copy;  /* documents handed out from the fed buffer */
	const char*    name;
} frame_pull_case_t;

static void frame_pull_try(void* ctx, const size_t* cuts, size_t ncuts,
	const char* how)
{
	frame_pull_case_t* fc = ctx;
	buf_t got = {0};
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create_pull(0);
	bool ok = true;
	size_t at = 0;
	for (size_t i = 0; ok && i < ncuts; i++) {
		size_t len = cuts[i] - at;
		uint8_t* piece = xmalloc(len ? len : 1);
		if (len) memcpy(piece, fc->stream + at, len);
		ok = bvnr_frame_reader_feed(fr, piece, len);
		const uint8_t* doc;
		uint64_t dl;
		int rc;
		while (ok && (rc = bvnr_frame_reader_next(fr, &doc, &dl)) ==
		       BVNR_NEXT_DOCUMENT) {
			buf_u64(&got, dl);
			buf_put(&got, doc, (size_t)dl);
			if (dl && doc >= piece && doc + dl <= piece + len)
				fc->zero_copy++;
		}
		if (ok && rc == BVNR_NEXT_ERROR) ok = false;
		free(piece);
		at = cuts[i];
	}
	ok = ok && bvnr_frame_reader_finish(fr);
	bool same = ok && bvnr_frame_reader_count(fr) == fc->ndocs &&
		buf_eq(&got, fc->expect);
	if (!same && fc->mismatches++ < 4)
		fprintf(stderr, "  MISMATCH frame pull %s (%s)\n", fc->name, how);
	bvnr_frame_reader_destroy(fr);
	buf_free(&got);
}

static void frame_reference(frame_case_t* fc)
{
	memset(&fc->ref, 0, sizeof(fc->ref));
	fc->ref.veto_at = fc->veto_at;
	bvnr_read_flags_t fl;
	bvnr_doc_stream_opts_t o;
	frame_opts(&fc->ref, &fl, fc->resilient, fc->max_doc, fc->cfg, &o);
	bvnr_source_t src;
	bvnr_source_from_mem(&src, fc->stream, fc->n);
	fc->ref_ok = bvnr_doc_stream_read_ex(&src, &o, &fc->ref_count,
					     &fc->ref_err);
}

static void frame_add(buf_t* stream, buf_t* expect, const void* doc, size_t n)
{
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, sink_buf_push, NULL, stream);
	bvnr_frame_write(&sink, doc, n);
	if (expect) {
		buf_u64(expect, n);
		buf_put(expect, doc, n);
	}
}

static void test_frame_reader(void)
{
	printf("frame reader: parse and pull modes against bvnr_doc_stream_read\n");
	buf_t stream = {0}, expect = {0};
	frame_add(&stream, &expect, LIT(".id = 1; .name = \"doc1\";"));
	frame_add(&stream, &expect, "", 0);
	frame_add(&stream, &expect, LIT(".bad = @;"));
	frame_add(&stream, &expect, LIT(".s = { .x = [1,2]/[3,4]; };"));
	frame_add(&stream, &expect, LIT(".cut = \"unterminated"));
	frame_add(&stream, &expect, LIT("#!bovnar 1.1\n.t = 2026-06-15;"));

	uint64_t mism = 0;
	for (int resilient = 0; resilient < 2; resilient++) {
		for (uint32_t veto = 0; veto < 3; veto += 2) {
			frame_case_t fc = { .stream = stream.p, .n = stream.n,
				.resilient = resilient != 0, .veto_at = veto,
				.name = "small" };
			frame_reference(&fc);
			for_partitions(stream.n, 11u + (uint64_t)resilient,
				       frame_try, &fc);
			mism += fc.mismatches;
			buf_free(&fc.ref.docs);
			rec_free(&fc.ref.rec);
		}
	}
	ASSERT_EQ_UINT(mism, 0, "parse mode matches bvnr_doc_stream_read");

	frame_pull_case_t pc = { .stream = stream.p, .n = stream.n,
		.expect = &expect, .ndocs = 6, .name = "small" };
	for_partitions(stream.n, 5, frame_pull_try, &pc);
	ASSERT_EQ_UINT(pc.mismatches, 0, "pull mode hands out the framed bytes");
	ASSERT_TRUE(pc.zero_copy > 0, "whole frames are handed out without a copy");

	/* Large documents: the generated octet and mux documents, framed. */
	buf_t big = {0}, big_expect = {0};
	for (size_t di = 0; di < g_ndocs; di++)
		if (strncmp(g_docs[di].name, "gen_", 4) == 0)
			frame_add(&big, &big_expect, g_docs[di].data, g_docs[di].len);
	frame_case_t fb = { .stream = big.p, .n = big.n, .resilient = true,
		.name = "big" };
	frame_reference(&fb);
	for_partitions(big.n, 3, frame_try, &fb);
	ASSERT_EQ_UINT(fb.mismatches, 0, "large frames match bvnr_doc_stream_read");
	buf_free(&fb.ref.docs);
	rec_free(&fb.ref.rec);
	frame_pull_case_t pb = { .stream = big.p, .n = big.n,
		.expect = &big_expect, .ndocs = 3, .name = "big" };
	for_partitions(big.n, 4, frame_pull_try, &pb);
	ASSERT_EQ_UINT(pb.mismatches, 0, "large frames in pull mode");

	/* Errors. */
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create_pull(0);
	ASSERT_TRUE(bvnr_frame_reader_feed(fr, "BVF2\0\0\0\0\0\0\0\0", 12),
		"feed a bad magic");
	const uint8_t* doc; uint64_t dl;
	ASSERT_TRUE(bvnr_frame_reader_next(fr, &doc, &dl) == BVNR_NEXT_ERROR,
		"bad magic fails");
	ASSERT_EQ_UINT(bvnr_frame_reader_error(fr), error_unexpected_input_byte,
		"bad magic is error_unexpected_input_byte");
	ASSERT_TRUE(!bvnr_frame_reader_feed(fr, "x", 1), "no feed after failure");
	bvnr_frame_reader_destroy(fr);

	fr = bvnr_frame_reader_create_pull(16);
	ASSERT_TRUE(bvnr_frame_reader_feed(fr, "BVF1\x11\0\0\0\0\0\0\0", 12),
		"feed an oversized header");
	ASSERT_TRUE(bvnr_frame_reader_next(fr, &doc, &dl) == BVNR_NEXT_ERROR,
		"oversized declared length fails");
	ASSERT_EQ_UINT(bvnr_frame_reader_error(fr), error_value_out_of_range,
		"oversize is error_value_out_of_range");
	bvnr_frame_reader_destroy(fr);

	for (size_t k = 1; k < 12 + 7; k++) {
		fr = bvnr_frame_reader_create_pull(0);
		bvnr_frame_reader_feed(fr, stream.p, k);
		while (bvnr_frame_reader_next(fr, &doc, &dl) == BVNR_NEXT_DOCUMENT)
			;
		ASSERT_TRUE(!bvnr_frame_reader_finish(fr) &&
			bvnr_frame_reader_error(fr) == error_got_incomplete_bvnr_stream,
			"a cut frame is error_got_incomplete_bvnr_stream at finish");
		bvnr_frame_reader_destroy(fr);
	}
	fr = bvnr_frame_reader_create_pull(0);
	bvnr_frame_reader_feed(fr, stream.p, stream.n);
	ASSERT_TRUE(!bvnr_frame_reader_feed(fr, stream.p, 1),
		"feeding before the last feed is drained is refused");
	ASSERT_TRUE(!bvnr_frame_reader_finish(fr), "finish while undrained refused");
	while (bvnr_frame_reader_next(fr, &doc, &dl) == BVNR_NEXT_DOCUMENT)
		;
	ASSERT_TRUE(bvnr_frame_reader_finish(fr), "clean finish on a boundary");
	ASSERT_EQ_UINT(bvnr_frame_reader_count(fr), 6, "six documents");
	bvnr_frame_reader_destroy(fr);

	fr = bvnr_frame_reader_create(NULL);
	ASSERT_TRUE(bvnr_frame_reader_feed(fr, stream.p, stream.n) == false,
		"strict parse mode stops at the failed document");
	ASSERT_EQ_UINT(bvnr_frame_reader_error(fr), error_unexpected_input_byte,
		"the failed document's error code");
	ASSERT_EQ_UINT(bvnr_frame_reader_count(fr), 3, "three documents seen");
	bvnr_frame_reader_destroy(fr);

	buf_free(&stream); buf_free(&expect);
	buf_free(&big); buf_free(&big_expect);
}

/* ---------------------------------------------------------------------------
 * Reuse: nothing one document leaves in a reader reaches the next.
 *
 * A re-opened reader keeps its buffers (the frame stack, the scratch), so
 * every path that reads document after document through one reader -- the
 * parsing frame reader, bvnr_doc_stream_read, a fed demux, a caller re-opening
 * its own -- depends on the open clearing everything else: the version
 * directive, the limits and flags, the error and its position, the recovery
 * counters, the array frames a document left half written. Each check here
 * reads a document on a reader that has just read another, under other
 * settings, and requires the record a reader fresh from bvnr_reader_create
 * produces for it.
 * ------------------------------------------------------------------------- */

/* Settings: a configuration of cfg_flags, and an array-nesting limit (0: the
 * default), so a document can be read under a smaller or a larger frame stack
 * than the one before it. */
static const uint8_t REUSE_NESTING[] = { 0, 2, 255 };
#define REUSE_NNEST (sizeof(REUSE_NESTING) / sizeof(REUSE_NESTING[0]))
#define REUSE_NVAR  ((size_t)CFG__COUNT * REUSE_NNEST)

/* Read one document on `rd` under setting `var`: from memory (piece 0), or
 * fed in pieces of `piece` bytes (SIZE_MAX: one piece). */
static void read_on(bvnr_reader_t* rd, const uint8_t* doc, size_t n,
	size_t var, size_t piece, rec_t* r)
{
	memset(r, 0, sizeof(*r));
	bvnr_read_flags_t f;
	cfg_flags((int)(var / REUSE_NNEST), n, r, &f);
	f.max_array_nesting = REUSE_NESTING[var % REUSE_NNEST];
	bvnr_sink_t mirror;
	bvnr_sink_to_callback(&mirror, mirror_push, NULL, &r->mirror);
	r->feeds_ok = true;
	if (!piece) {
		bvnr_source_t src;
		bvnr_source_from_mem(&src, doc, n);
		r->ok = bvnr_open_read_source(rd, &src, &mirror, &f) &&
			bvnr_read(rd);
	} else {
		bool alive = bvnr_open_read_feed(rd, &mirror, &f);
		for (size_t at = 0; alive && at < n; at += piece) {
			size_t len = n - at < piece ? n - at : piece;
			alive = bvnr_read_feed(rd, doc + at, len);
			if (!alive) r->feeds_ok = false;
		}
		r->ok = alive && bvnr_read_finish(rd);
	}
	rec_finish(r, rd);
}

static void read_fresh(const uint8_t* doc, size_t n, size_t var, rec_t* r)
{
	bvnr_reader_t* rd = bvnr_reader_create();
	read_on(rd, doc, n, var, 0, r);
	bvnr_reader_destroy(rd);
}

/* Documents that each set something the next must not inherit. */
static const struct { const char* name; const char* s; size_t n; } REUSE_DOCS[] = {
	{ "v11",          LIT("#!bovnar 1.1\n.t = 2026-06-15;\n") },
	{ "no_version",   LIT(".t = 2026-06-15;\n") },
	{ "v12",          LIT("#!bovnar 1.2\n.u = <float:64,ucum:m/s> 1.5;\n") },
	{ "v12_unsaid",   LIT(".u = <float:64,ucum:m/s> 1.5;\n") },
	{ "comment_v11",  LIT("# first\n#!bovnar 1.1\n.t = 2026-06-15;\n") },
	{ "v99",          LIT("#!bovnar 9.9\n.a = 1;\n") },
	{ "v_eof",        LIT("#!bovnar 1.1") },
	{ "deep_cut",     LIT(".deep = [[[[[1,2]/[3,4]]]]") },
	{ "dims",         LIT(".m = [1,2]/[3,4];\n.n = [[1,2,3]/[4,5,6]];\n") },
	{ "dims_wide",    LIT(".m = [1,2,3]/[4,5,6];\n") },
	{ "recover",      LIT(".a = 1;\n.b = @;\n.c = [1,2;\n.d = 3;\n") },
	{ "struct_cut",   LIT(".s = { .a = [1, 2]; .t = { .b = 2;") },
	{ "units",        LIT(".l = 1.5 km;\n.y = <float:64,k~g> 9.81;\n.x = <uint:8> 255;\n") },
	{ "bom",          LIT("\xEF\xBB\xBF.x = 1;\n") },
	{ "empty",        LIT("") },
	{ "string_cut",   LIT(".s = \"unterminated") },
	{ "utf8_cut",     LIT(".a = 1; #\xc3") },
	{ "octet",        LIT(".b = \x00\x01\x02\x00hi\x00;\n.c = 1;\n") },
	{ "octet_cut",    LIT(".b = \x00\x01\x09\x00hi") },
};
#define REUSE_NDOCS (sizeof(REUSE_DOCS) / sizeof(REUSE_DOCS[0]))

static void test_reader_reuse(void)
{
	printf("reader reuse: a re-opened reader reads as a fresh one\n");
	/* The fresh reader's record of every document under every setting. */
	static rec_t ref[REUSE_NDOCS][REUSE_NVAR];
	for (size_t b = 0; b < REUSE_NDOCS; b++)
		for (size_t v = 0; v < REUSE_NVAR; v++)
			read_fresh((const uint8_t*)REUSE_DOCS[b].s, REUSE_DOCS[b].n,
				   v, &ref[b][v]);

	/* Every ordered pair of documents, every pair of settings: A, then B on
	 * the same reader, from memory and fed whole -- and fed one byte at a
	 * time when the settings are the same. */
	bvnr_reader_t* rd = bvnr_reader_create();
	uint64_t pairs = 0, mism = 0;
	for (size_t a = 0; a < REUSE_NDOCS; a++)
	for (size_t b = 0; b < REUSE_NDOCS; b++)
	for (size_t va = 0; va < REUSE_NVAR; va++)
	for (size_t vb = 0; vb < REUSE_NVAR; vb++) {
		static const size_t pieces[] = { 0, SIZE_MAX, 1 };
		for (size_t pi = 0; pi < 3; pi++) {
			if (pi == 2 && va != vb) continue;
			rec_t ra, rb;
			read_on(rd, (const uint8_t*)REUSE_DOCS[a].s, REUSE_DOCS[a].n,
				va, (a + b) & 1 ? SIZE_MAX : 0, &ra);
			read_on(rd, (const uint8_t*)REUSE_DOCS[b].s, REUSE_DOCS[b].n,
				vb, pieces[pi], &rb);
			char how[96];
			snprintf(how, sizeof(how), "after %s (setting %zu), %s",
				 REUSE_DOCS[a].name, va,
				 pi == 0 ? "from memory" : pi == 1 ? "fed whole"
							      : "fed bytewise");
			if (!rec_same(&rb, &ref[b][vb], REUSE_DOCS[b].name,
				      (int)vb, how))
				mism++;
			pairs++;
			rec_free(&ra);
			rec_free(&rb);
		}
	}
	printf("  %" PRIu64 " documents read after another\n", pairs);
	ASSERT_EQ_UINT(mism, 0, "a re-opened reader matches a fresh one");

	/* The whole corpus, one document after another on one reader, under
	 * each configuration, alternately from memory and fed. */
	mism = 0;
	for (int cfg = 0; cfg < CFG__COUNT; cfg++) {
		size_t var = (size_t)cfg * REUSE_NNEST;
		for (size_t di = 0; di < g_ndocs; di++) {
			const doc_t* d = &g_docs[di];
			rec_t got, want;
			read_on(rd, d->data, d->len, var, di & 1 ? 4096 : 0, &got);
			read_fresh(d->data, d->len, var, &want);
			if (!rec_same(&got, &want, d->name, cfg, "corpus in a row"))
				mism++;
			rec_free(&got);
			rec_free(&want);
		}
	}
	ASSERT_EQ_UINT(mism, 0, "the corpus read in a row on one reader");
	bvnr_reader_destroy(rd);

	for (size_t b = 0; b < REUSE_NDOCS; b++)
		for (size_t v = 0; v < REUSE_NVAR; v++)
			rec_free(&ref[b][v]);
}

/* The frame reference with a fresh reader per document: what parse mode and
 * bvnr_doc_stream_read must report, whatever their reader kept. */
static void frame_reference_fresh(frame_case_t* fc, const buf_t* docs)
{
	memset(&fc->ref, 0, sizeof(fc->ref));
	fc->ref.veto_at = fc->veto_at;
	bvnr_read_flags_t fl;
	bvnr_doc_stream_opts_t o;
	frame_opts(&fc->ref, &fl, fc->resilient, fc->max_doc, fc->cfg, &o);
	fc->ref_ok = true;
	fc->ref_count = 0;
	fc->ref_err = error_none;
	for (size_t at = 0; at < docs->n; ) {
		uint64_t len;
		memcpy(&len, docs->p + at, sizeof(len));
		const uint8_t* doc = docs->p + at + sizeof(len);
		at += sizeof(len) + (size_t)len;
		bvnr_reader_t* rd = bvnr_reader_create();
		bool ok = bvnr_open_read_mem(rd, len ? doc : (const void*)"", len,
					     NULL, 0, &fl) && bvnr_read(rd);
		error_code_t err = bvnr_reader_get_error(rd);
		bvnr_reader_destroy(rd);
		uint64_t index = fc->ref_count++;
		if (!frame_on_document(&fc->ref, index, ok, err)) {
			fc->ref_ok = false;
			fc->ref_err = error_scanner_callback_failed;
			break;
		}
		if (!ok && !fc->resilient) {
			fc->ref_ok = false;
			fc->ref_err = err;
			break;
		}
	}
}

static void test_frame_isolation(void)
{
	printf("frame reader: consecutive documents are independent\n");
	/* Ordered so that each document would read differently if the one
	 * before it leaked: a datetime after a 1.1 directive and then without
	 * one, a profile unit with and without 1.2, a directive after a plain
	 * first comment, arrays after arrays left open, a failure (recovered
	 * under CFG_RECOVER) before good documents, an octet stream (refused
	 * under CFG_TEXT_ONLY) between text ones. */
	static const struct { const char* s; size_t n; } D[] = {
		{ LIT("#!bovnar 1.1\n.t = 2026-06-15;") },
		{ LIT(".t = 2026-06-15;") },
		{ LIT(".bad = @; .after = [1, 2];") },
		{ LIT(".ok = 1;") },
		{ LIT(".deep = [[[[1,2]/[3") },
		{ LIT(".m = [1,2,3]/[4,5,6];") },
		{ LIT("#!bovnar 1.2\n.u = <float:64,ucum:m/s> 1.5;") },
		{ LIT(".u = <float:64,ucum:m/s> 1.5;") },
		{ LIT("# a comment\n.c = 1;") },
		{ LIT("#!bovnar 1.1\n.t = 2026-06-15;") },
		{ LIT(".b = \x00\x01\x02\x00hi\x00; .c = 1;") },
		{ LIT("#!bovnar 9.9\n.v = 1;") },
		{ LIT(".s = { .x = 1; .y = [1, 2]; ") },
		{ LIT(".z = 3;") },
		{ LIT("") },
		{ LIT(".a = 1; #\xc3") },
		{ LIT(".last = [1, [2, 3]];") },
	};
	buf_t stream = {0}, docs = {0};
	for (size_t i = 0; i < sizeof(D) / sizeof(D[0]); i++)
		frame_add(&stream, &docs, D[i].s, D[i].n);

	static const int cfgs[] = { CFG_PLAIN, CFG_RECOVER, CFG_STRICT,
				    CFG_TEXT_ONLY };
	uint64_t mism = 0, ds_mism = 0;
	for (size_t ci = 0; ci < sizeof(cfgs) / sizeof(cfgs[0]); ci++) {
		for (int resilient = 0; resilient < 2; resilient++) {
			frame_case_t fc = { .stream = stream.p, .n = stream.n,
				.resilient = resilient != 0, .cfg = cfgs[ci],
				.name = "isolation" };
			frame_reference_fresh(&fc, &docs);
			if (resilient)
				ASSERT_EQ_UINT(fc.ref_count,
					sizeof(D) / sizeof(D[0]),
					"continue_past_failed reads every document");
			/* Every two-piece cut, single bytes, random pieces. */
			for_partitions(stream.n, 29u + ci, frame_try, &fc);
			mism += fc.mismatches;

			/* bvnr_doc_stream_read against the same reference. */
			frame_ref_t got;
			memset(&got, 0, sizeof(got));
			bvnr_read_flags_t fl;
			bvnr_doc_stream_opts_t o;
			frame_opts(&got, &fl, fc.resilient, 0, fc.cfg, &o);
			bvnr_source_t src;
			bvnr_source_from_mem(&src, stream.p, stream.n);
			uint64_t count = 0;
			error_code_t err = error_none;
			bool ok = bvnr_doc_stream_read_ex(&src, &o, &count, &err);
			if (ok != fc.ref_ok || count != fc.ref_count ||
			    err != fc.ref_err ||
			    !buf_eq(&got.docs, &fc.ref.docs) ||
			    !buf_eq(&got.rec.log, &fc.ref.rec.log)) {
				ds_mism++;
				fprintf(stderr, "  MISMATCH doc_stream cfg %d "
					"resilient %d\n", cfgs[ci], resilient);
			}
			buf_free(&got.docs);
			rec_free(&got.rec);
			buf_free(&fc.ref.docs);
			rec_free(&fc.ref.rec);
		}
	}
	ASSERT_EQ_UINT(mism, 0,
		"parse mode matches a fresh reader per document, cut anywhere");
	ASSERT_EQ_UINT(ds_mism, 0,
		"bvnr_doc_stream_read matches a fresh reader per document");
	buf_free(&stream);
	buf_free(&docs);
}

/* ---------------------------------------------------------------------------
 * Demux.
 *
 * The reference is bvnr_read over the whole document with
 * bvnr_demux_on_event installed behind a logger. The record interleaves, in
 * delivery order, every verified event outside the mux stream's chunks and
 * every message, so it is the same thing bvnr_demux_next hands out with
 * deliver_events -- metadata before the stream, the messages, values after it.
 * ------------------------------------------------------------------------- */
typedef struct {
	buf_t        log;
	bvnr_demux_t* dm;          /* reference only: the demux behind the logger */
	char         key[16];      /* the assignment being read */
	bool         in_mux;       /* inside the stream under the mux key */
	const char*  mux_key;      /* NULL: every octet stream is the mux */
} mux_rec_t;

static void mux_track(mux_rec_t* m, bvnr_event_t ev, const bvnr_data_t* d)
{
	if (ev == ev_assignment_start) {
		size_t n = d->length < sizeof(m->key) - 1 ? d->length
							   : sizeof(m->key) - 1;
		memcpy(m->key, d->data, n);
		m->key[n] = 0;
	} else if (ev == ev_octet_stream_start) {
		m->in_mux = !m->mux_key || strcmp(m->key, m->mux_key) == 0;
	} else if (ev == ev_octet_stream_end) {
		m->in_mux = false;
	}
}

static bool mux_is_chunk(const mux_rec_t* m, bvnr_event_t ev,
	const bvnr_data_t* d)
{
	return ev == ev_data && m->in_mux && d->type == token_is_octet_stream;
}

static void mux_log_event(mux_rec_t* m, bvnr_event_t ev, const bvnr_data_t* d)
{
	rec_t tmp;
	memset(&tmp, 0, sizeof(tmp));
	tmp.log = m->log;
	log_event(&tmp, 'v', ev, d);
	m->log = tmp.log;
}

static void mux_log_msg(mux_rec_t* m, uint64_t ch, const uint8_t* p,
	uint64_t n)
{
	uint8_t tag = 'M';
	buf_put(&m->log, &tag, 1);
	buf_u64(&m->log, ch);
	buf_u64(&m->log, n);
	buf_put(&m->log, p, (size_t)n);
}

static bool mux_on_msg(void* ud, uint64_t ch, const uint8_t* p, uint64_t n)
{
	mux_log_msg(ud, ch, p, n);
	return true;
}

/* The logger in front of the reference demux, and the caller's on_verified of
 * a callback-mode fed demux: both see every event. */
static bool mux_ref_verified(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	mux_rec_t* m = ud;
	mux_track(m, ev, d);
	if (!mux_is_chunk(m, ev, d))
		mux_log_event(m, ev, d);
	return m->dm ? bvnr_demux_on_event(m->dm, ev, d) : true;
}

typedef struct {
	const uint8_t* doc;
	size_t         n;
	const char*    mux_key;    /* NULL: no key scope */
	uint64_t       max_message;
	/* reference */
	buf_t          ref_log;
	bool           ref_ok;
	error_code_t   ref_derr;
	error_code_t   ref_rerr;
	uint64_t       ref_off;
	uint64_t       mismatches;
	uint64_t       zero_copy;
	const char*    name;
} mux_case_t;

static void mux_reference(mux_case_t* mc)
{
	mux_rec_t m;
	memset(&m, 0, sizeof(m));
	m.mux_key = mc->mux_key;
	m.dm = bvnr_demux_create(mux_on_msg, &m, mc->max_message);
	if (mc->mux_key) bvnr_demux_set_key(m.dm, mc->mux_key);
	bvnr_read_flags_t f;
	memset(&f, 0, sizeof(f));
	f.userdata = &m;
	f.on_verified = mux_ref_verified;
	bvnr_reader_t* r = bvnr_reader_create();
	mc->ref_ok = bvnr_open_read_mem(r, mc->doc, mc->n, NULL, 0, &f) &&
		     bvnr_read(r);
	mc->ref_derr = bvnr_demux_error(m.dm);
	mc->ref_rerr = bvnr_reader_get_error(r);
	mc->ref_off  = bvnr_reader_get_error_offset(r);
	mc->ref_log  = m.log;
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(m.dm);
}

static void mux_check(mux_case_t* mc, bool ok, const mux_rec_t* m,
	bvnr_demux_t* dm, bool exact_reader, const char* mode, const char* how)
{
	const bvnr_reader_t* r = bvnr_demux_get_reader(dm);
	/* A fed demux with no error of its own reports its reader's. */
	error_code_t want = mc->ref_derr != error_none ? mc->ref_derr
						       : mc->ref_rerr;
	bool same = ok == mc->ref_ok && bvnr_demux_error(dm) == want &&
		buf_eq(&m->log, &mc->ref_log);
	/* The reader's own error and position match too, except where the demux
	 * stopped a pull-mode read itself: there the reader was paused, not
	 * failed. */
	if (exact_reader)
		same = same && bvnr_reader_get_error(r) == mc->ref_rerr &&
		       bvnr_reader_get_error_offset(r) == mc->ref_off;
	if (!same && mc->mismatches++ < 4)
		fprintf(stderr, "  MISMATCH demux %s %s (%s): ok %d/%d derr %d/%d "
			"rerr %d/%d log %zu/%zu\n", mc->name, mode, how, ok,
			mc->ref_ok, bvnr_demux_error(dm), mc->ref_derr,
			bvnr_reader_get_error(r), mc->ref_rerr, m->log.n,
			mc->ref_log.n);
}

/* Callback mode: on_message, plus the caller's on_verified. */
static void mux_try_callback(void* ctx, const size_t* cuts, size_t ncuts,
	const char* how)
{
	mux_case_t* mc = ctx;
	mux_rec_t m;
	memset(&m, 0, sizeof(m));
	m.mux_key = mc->mux_key;
	bvnr_demux_t* dm = bvnr_demux_create(mux_on_msg, &m, mc->max_message);
	if (mc->mux_key) bvnr_demux_set_key(dm, mc->mux_key);
	bvnr_read_flags_t f;
	memset(&f, 0, sizeof(f));
	f.userdata = &m;
	f.on_verified = mux_ref_verified;
	bool ok = bvnr_demux_open_feed(dm, &f, false);
	size_t at = 0;
	for (size_t i = 0; ok && i < ncuts; i++) {
		ok = bvnr_demux_feed(dm, mc->doc + at, cuts[i] - at);
		at = cuts[i];
	}
	ok = ok && bvnr_demux_finish(dm);
	mux_check(mc, ok, &m, dm, true, "callback", how);
	buf_free(&m.log);
	bvnr_demux_destroy(dm);
}

/* Pull mode with deliver_events: one loop, no callbacks. */
static void mux_try_pull(void* ctx, const size_t* cuts, size_t ncuts,
	const char* how)
{
	mux_case_t* mc = ctx;
	mux_rec_t m;
	memset(&m, 0, sizeof(m));
	bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, mc->max_message);
	if (mc->mux_key) bvnr_demux_set_key(dm, mc->mux_key);
	bool ok = bvnr_demux_open_feed(dm, NULL, true);
	bool errored = false;
	size_t at = 0;
	bvnr_demux_item_t it;
	for (size_t i = 0; ok && i <= ncuts; i++) {
		const uint8_t* piece = NULL;
		size_t len = 0;
		if (i < ncuts) {
			len = cuts[i] - at;
			piece = mc->doc + at;
			ok = bvnr_demux_feed(dm, piece, len);
			at = cuts[i];
		} else {
			ok = bvnr_demux_finish(dm);
		}
		int rc;
		while ((rc = bvnr_demux_next(dm, &it)) > 0) {
			if (rc == BVNR_NEXT_EVENT) {
				mux_log_event(&m, it.event, it.value);
			} else {
				mux_log_msg(&m, it.channel, it.data, it.length);
				if (it.length && it.data >= piece &&
				    it.data + it.length <= piece + len)
					mc->zero_copy++;
			}
		}
		if (rc == BVNR_NEXT_ERROR) { errored = true; ok = false; }
	}
	(void)errored;
	mux_check(mc, ok, &m, dm, false, "pull", how);
	buf_free(&m.log);
	bvnr_demux_destroy(dm);
}

static void mux_run(mux_case_t* mc, uint64_t seed)
{
	mux_reference(mc);
	for_partitions(mc->n, seed, mux_try_callback, mc);
	for_partitions(mc->n, seed + 1, mux_try_pull, mc);
	buf_free(&mc->ref_log);
}

/* A document with metadata before and after a mux stream, written with the
 * normal writer. */
static void build_media_doc(buf_t* out, bool with_stream, bool after,
	bool big)
{
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, sink_buf_push, NULL, out);
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_sink(w, &sink, true, NULL);
	bvnr_write_struct_start(w, "meta");
	bvnr_write_string(w, "codec", "av1");
	bvnr_write_float_unit(w, "rate", 32, 30.0,
		BVN_UNIT_NO_PREFIX(bu_hertz));
	bvnr_write_string(w, "title", "caf\xc3\xa9 \xe2\x82\xac");
	bvnr_write_struct_start(w, "size");
	bvnr_write_uint(w, "w", 16, 1920);
	bvnr_write_uint(w, "h", 16, 1080);
	bvnr_write_struct_end(w);
	bvnr_write_struct_end(w);
	bvnr_write_float_unit(w, "len", 64, 12.5, BVN_UNIT_SI(bu_meter, si_kilo));
	if (with_stream) {
		uint8_t* p = xmalloc(70000);
		for (size_t i = 0; i < 70000; i++) p[i] = (uint8_t)(i ^ (i >> 7));
		bvnr_mux_begin(w, "media");
		bvnr_mux_send(w, 1, p, 150);
		bvnr_mux_send(w, 2, p + 1, big ? 3000 : 300);
		bvnr_mux_send(w, 1, p, 0);
		bvnr_mux_send(w, 9, p, big ? 70000 : 200);
		bvnr_mux_send(w, 2, p + 5, 150);
		bvnr_mux_end(w);
		free(p);
	}
	if (after) {
		bvnr_write_string(w, "trailer", "done");
		bvnr_write_uint(w, "frames", 32, 4);
	}
	if (!bvnr_write_finish(w)) { fprintf(stderr, "writer failed\n"); exit(2); }
	bvnr_writer_destroy(w);
}

/* A document around raw octet chunks, for wire shapes bvnr_mux_send does not
 * produce. Each chunk is given as its payload; the 0x01 tag and the length are
 * added here. */
static void raw_doc_begin(buf_t* b)
{
	buf_put(b, LIT(".meta = \"m\";\n.media = "));
	uint8_t z = 0x00;
	buf_put(b, &z, 1);
}

static void raw_chunk(buf_t* b, const uint8_t* p, uint32_t n)
{
	uint8_t hdr[3] = { 0x01, (uint8_t)(n & 0xFF), (uint8_t)((n >> 8) & 0xFF) };
	buf_put(b, hdr, 3);
	buf_put(b, p, n);
}

static void raw_doc_end(buf_t* b)
{
	uint8_t z = 0x00;
	buf_put(b, &z, 1);
	buf_put(b, LIT(";\n.after = 1;\n"));
}

static uint32_t put_varint(uint8_t* out, uint64_t v)
{
	uint32_t n = 0;
	do {
		uint8_t byte = (uint8_t)(v & 0x7F);
		v >>= 7;
		if (v) byte |= 0x80;
		out[n++] = byte;
	} while (v);
	return n;
}

static void test_demux_feed(void)
{
	printf("demux: fed in pieces, callback and pull modes, against bvnr_read\n");
	buf_t doc = {0};

	/* Metadata, the stream, values after it: small enough to be cut at every
	 * byte, then with messages that span chunks. */
	build_media_doc(&doc, true, true, false);
	ASSERT_TRUE(doc.n <= EVERY_CUT_MAX, "the small media document is cut "
		"at every byte");
	mux_case_t ms = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.name = "metadata+stream+after (small)" };
	mux_run(&ms, 20);
	ASSERT_EQ_UINT(ms.mismatches, 0, "metadata, stream, trailing values, "
		"every cut");
	doc.n = 0;
	build_media_doc(&doc, true, true, true);
	mux_case_t mc = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "metadata+stream+after" };
	mux_run(&mc, 21);
	ASSERT_EQ_UINT(mc.mismatches, 0, "metadata, stream, trailing values");
	ASSERT_TRUE(mc.zero_copy > 0, "messages handed out from the fed buffer");
	doc.n = 0;

	/* Metadata only, no stream. */
	build_media_doc(&doc, false, true, false);
	mux_case_t mo = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "metadata only" };
	mux_run(&mo, 22);
	ASSERT_EQ_UINT(mo.mismatches, 0, "metadata only");
	doc.n = 0;

	/* The generated 150 B / 30 KB / 400 KB stream, no key scope. */
	for (size_t di = 0; di < g_ndocs; di++) {
		if (strcmp(g_docs[di].name, "gen_mux") != 0) continue;
		mux_case_t mg = { .doc = g_docs[di].data, .n = g_docs[di].len, .mux_key = NULL,
		.max_message = 0, .name = "gen_mux" };
		mux_run(&mg, 23);
		ASSERT_EQ_UINT(mg.mismatches, 0, "150 B, 30 KB, 400 KB messages");
	}

	/* Packed chunks: several messages in one, a zero-length one among them;
	 * interleaved channels; a length varint split across chunks; a 65536-byte
	 * chunk (length field 0). */
	uint8_t c[65536];
	uint32_t k;
	raw_doc_begin(&doc);
	k = put_varint(c, 1);
	for (int i = 0; i < 4; i++) {
		k += put_varint(c + k, (uint64_t)(i * 3));
		for (int j = 0; j < i * 3; j++) c[k++] = (uint8_t)('a' + j);
	}
	raw_chunk(&doc, c, k);
	/* channel 7: a 300-byte message whose length varint (0xAC 0x02) is
	 * split, interleaved with channel 8 */
	k = put_varint(c, 7); c[k++] = 0xAC;
	raw_chunk(&doc, c, k);
	k = put_varint(c, 8); k += put_varint(c + k, 2); c[k++] = 'x'; c[k++] = 'y';
	raw_chunk(&doc, c, k);
	k = put_varint(c, 7); c[k++] = 0x02;
	for (int j = 0; j < 100; j++) c[k++] = (uint8_t)j;
	raw_chunk(&doc, c, k);
	k = put_varint(c, 300);
	k += put_varint(c + k, 5); memcpy(c + k, "hello", 5); k += 5;
	raw_chunk(&doc, c, k);
	k = put_varint(c, 7);
	for (int j = 0; j < 200; j++) c[k++] = (uint8_t)(j + 1);
	raw_chunk(&doc, c, k);
	/* a full 65536-byte chunk: channel 3, message of 65533 bytes */
	k = put_varint(c, 3);
	k += put_varint(c + k, 65536u - 4u);
	for (; k < 65536u; k++) c[k] = (uint8_t)k;
	/* raw_chunk writes the length field as given: 0 means 65536, and the
	 * payload follows separately. */
	raw_chunk(&doc, c, 0);
	buf_put(&doc, c, 65536);
	raw_doc_end(&doc);
	mux_case_t mp = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "packed/interleaved/split varint/65536" };
	mux_run(&mp, 24);
	ASSERT_EQ_UINT(mp.mismatches, 0,
		"packed chunks, interleaved channels, split varint, 65536 chunk");
	ASSERT_EQ_UINT(mp.ref_derr, error_none, "that stream is clean");
	doc.n = 0;

	/* 4096 channels pass, the 4097th is out of sync. */
	for (int over = 0; over < 2; over++) {
		raw_doc_begin(&doc);
		for (uint32_t ch = 0; ch < 4096u + (uint32_t)over; ch++) {
			k = put_varint(c, (uint64_t)ch * 1000u);
			k += put_varint(c + k, 1);
			c[k++] = (uint8_t)ch;
			raw_chunk(&doc, c, k);
		}
		raw_doc_end(&doc);
		mux_case_t mch = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = over ? "4097 channels" : "4096 channels" };
		mux_reference(&mch);
		ASSERT_EQ_UINT(mch.ref_derr, over ? error_octet_stream_out_of_sync
			: error_none, "the channel limit");
		for_partitions(doc.n, 30u + (uint64_t)over, mux_try_pull, &mch);
		ASSERT_EQ_UINT(mch.mismatches, 0, "the channel limit, fed");
		buf_free(&mch.ref_log);
		doc.n = 0;
	}

	/* Truncated: the stream ends inside a message; latched, read succeeds. */
	raw_doc_begin(&doc);
	k = put_varint(c, 4); k += put_varint(c + k, 1000);
	memcpy(c + k, "0123456789", 10); k += 10;
	raw_chunk(&doc, c, k);
	raw_doc_end(&doc);
	mux_case_t mt = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "truncated message" };
	mux_run(&mt, 25);
	ASSERT_EQ_UINT(mt.mismatches, 0, "truncated message, fed");
	ASSERT_TRUE(mt.ref_ok && mt.ref_derr == error_octet_stream_truncated,
		"truncation latched, read succeeds");
	doc.n = 0;

	/* Desync: an overlong channel varint. */
	raw_doc_begin(&doc);
	for (k = 0; k < 11; k++) c[k] = 0x80;
	c[k++] = 0x01;
	raw_chunk(&doc, c, k);
	raw_doc_end(&doc);
	mux_case_t md = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "desync" };
	mux_run(&md, 26);
	ASSERT_EQ_UINT(md.mismatches, 0, "desync, fed");
	ASSERT_EQ_UINT(md.ref_derr, error_octet_stream_out_of_sync, "desync code");
	doc.n = 0;

	/* Oversize against max_message. */
	raw_doc_begin(&doc);
	k = put_varint(c, 1); k += put_varint(c + k, 101);
	for (int j = 0; j < 101; j++) c[k++] = 'z';
	raw_chunk(&doc, c, k);
	raw_doc_end(&doc);
	mux_case_t mz = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 100, .name = "oversize" };
	mux_run(&mz, 27);
	ASSERT_EQ_UINT(mz.mismatches, 0, "oversize, fed");
	ASSERT_EQ_UINT(mz.ref_derr, error_value_out_of_range, "oversize code");
	doc.n = 0;

	/* Key scope: an ordinary octet stream under another key is a value, not
	 * a mux stream, and arrives as events. */
	buf_put(&doc, LIT(".bin = "));
	uint8_t z = 0;
	buf_put(&doc, &z, 1);
	raw_chunk(&doc, (const uint8_t*)"\xff\xff\xff", 3);
	buf_put(&doc, &z, 1);
	buf_put(&doc, LIT(";\n"));
	build_media_doc(&doc, true, false, true);
	mux_case_t mk = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "key scope" };
	mux_run(&mk, 28);
	ASSERT_EQ_UINT(mk.mismatches, 0, "key scope, fed");
	ASSERT_EQ_UINT(mk.ref_derr, error_none, "the other stream is not demuxed");
	doc.n = 0;

	/* A malformed document around the stream: the reader's error. */
	build_media_doc(&doc, true, false, true);
	buf_put(&doc, LIT(".x = @;"));
	mux_case_t mb = { .doc = doc.p, .n = doc.n, .mux_key = "media",
		.max_message = 0, .name = "bad document" };
	mux_run(&mb, 29);
	ASSERT_EQ_UINT(mb.mismatches, 0, "a malformed document, fed");
	ASSERT_EQ_UINT(mb.ref_derr, error_none, "the demux itself is clean");
	doc.n = 0;

	/* Usage: pull calls on a callback demux, feeding before draining. */
	build_media_doc(&doc, true, true, true);
	bvnr_demux_t* dm = bvnr_demux_create(mux_on_msg, NULL, 0);
	bvnr_demux_item_t it;
	ASSERT_TRUE(bvnr_demux_next(dm, &it) == BVNR_NEXT_ERROR,
		"next before open_feed");
	ASSERT_TRUE(bvnr_demux_get_reader(dm) == NULL, "no reader before open");
	bvnr_demux_destroy(dm);
	dm = bvnr_demux_create(NULL, NULL, 0);
	bvnr_demux_open_feed(dm, NULL, false);
	ASSERT_TRUE(bvnr_demux_feed(dm, doc.p, doc.n), "feed");
	ASSERT_TRUE(!bvnr_demux_feed(dm, doc.p, 1), "feed before drained refused");
	ASSERT_TRUE(!bvnr_demux_finish(dm), "finish before drained refused");
	int rc, msgs = 0;
	while ((rc = bvnr_demux_next(dm, &it)) == BVNR_NEXT_MESSAGE)
		msgs++;
	ASSERT_EQ_UINT(rc, BVNR_NEXT_NONE, "drained");
	ASSERT_EQ_UINT(msgs, 5, "five messages, no events without deliver_events");
	ASSERT_TRUE(bvnr_demux_finish(dm), "finish");
	ASSERT_EQ_UINT(bvnr_demux_next(dm, &it), BVNR_NEXT_NONE, "nothing after");
	/* The same demux, re-opened for a second document. */
	ASSERT_TRUE(bvnr_demux_open_feed(dm, NULL, false), "re-open");
	ASSERT_TRUE(bvnr_demux_feed(dm, doc.p, doc.n), "feed again");
	msgs = 0;
	while (bvnr_demux_next(dm, &it) == BVNR_NEXT_MESSAGE)
		msgs++;
	ASSERT_TRUE(msgs == 5 && bvnr_demux_finish(dm), "second document");
	bvnr_demux_destroy(dm);

	/* Each document re-opened on one demux is its own stream: two documents
	 * of 4096 distinct ids each (8192 over the demux's life) both pass, and
	 * a message spanning chunks reassembles in a previous document's buffer. */
	dm = bvnr_demux_create(NULL, NULL, 0);
	for (uint32_t d2 = 0; d2 < 3; d2++) {
		doc.n = 0;
		raw_doc_begin(&doc);
		for (uint32_t ch = 0; ch < 4096u; ch++) {
			k = put_varint(c, (uint64_t)d2 * 4096u + ch);
			k += put_varint(c + k, ch == 7 ? 10 : 1);
			if (ch == 7) {
				memcpy(c + k, "01234", 5); k += 5;
				raw_chunk(&doc, c, k);
				k = put_varint(c, (uint64_t)d2 * 4096u + ch);
				memcpy(c + k, "56789", 5); k += 5;
			} else {
				c[k++] = (uint8_t)ch;
			}
			raw_chunk(&doc, c, k);
		}
		raw_doc_end(&doc);
		ASSERT_TRUE(bvnr_demux_open_feed(dm, NULL, false), "re-open");
		ASSERT_TRUE(bvnr_demux_feed(dm, doc.p, doc.n), "feed 4096 ids");
		uint32_t got = 0;
		bool spanned = false;
		while ((rc = bvnr_demux_next(dm, &it)) == BVNR_NEXT_MESSAGE) {
			got++;
			if (it.channel == (uint64_t)d2 * 4096u + 7u)
				spanned = it.length == 10 &&
					  !memcmp(it.data, "0123456789", 10);
		}
		ASSERT_EQ_UINT(rc, BVNR_NEXT_NONE, "4096 new ids per document");
		ASSERT_EQ_UINT(got, 4096, "every message of the document");
		ASSERT_TRUE(spanned, "the spanning message, in a reused buffer");
		ASSERT_TRUE(bvnr_demux_finish(dm) &&
			bvnr_demux_error(dm) == error_none, "document clean");
	}
	bvnr_demux_destroy(dm);
	buf_free(&doc);
}

/* The fixed bvnr_mux_send vector: channel ids and lengths at every varint and
 * chunk boundary. */
static const uint64_t MUXVEC_CH[] = {
	0, 1, 127, 128, 300, UINT64_C(34359738368), UINT64_MAX };
static const uint64_t MUXVEC_LEN[] = {
	0, 1, 127, 128, 65516, 65517, 65522, 65535, 65536, 65537, 131077, 400000 };

/* The vector into an opened writer, flushed after every send or not. */
static uint64_t muxvec_send(bvnr_writer_t* w, bool flush)
{
	/* Each message starts up to 6 bytes in, so the payload has room. */
	uint8_t* payload = xmalloc(400008);
	for (size_t i = 0; i < 400008; i++)
		payload[i] = (uint8_t)(i * 2654435761u >> 13);
	bvnr_mux_begin(w, "mux");
	size_t nl = sizeof(MUXVEC_LEN) / sizeof(MUXVEC_LEN[0]);
	size_t nc = sizeof(MUXVEC_CH) / sizeof(MUXVEC_CH[0]);
	bool ok = true;
	for (size_t i = 0; ok && i < nl; i++)
		for (size_t j = 0; ok && j < nc; j++)
			ok = bvnr_mux_send(w, MUXVEC_CH[j], payload + (i + j) % 7,
					   MUXVEC_LEN[i]) &&
			     (!flush || bvnr_write_flush(w));
	ok = ok && bvnr_mux_end(w) && bvnr_write_finish(w);
	free(payload);
	return ok ? bvnr_writer_bytes_written(w) : 0;
}

static uint64_t muxvec_write(uint8_t* out, size_t cap)
{
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_mem(w, out, cap, false, NULL);
	uint64_t n = muxvec_send(w, false);
	bvnr_writer_destroy(w);
	return n;
}

static uint64_t fnv1a64(const uint8_t* p, uint64_t n)
{
	uint64_t h = UINT64_C(0xcbf29ce484222325);
	for (uint64_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= UINT64_C(0x100000001b3);
	}
	return h;
}

/* The chunks the vector must produce, built from the wire convention of
 * doc/10 §3.1 alone: every chunk leads with the channel varint, the first
 * also with the length varint, each holds as much as fits in 65536 bytes. */
static void muxvec_expect(buf_t* b)
{
	uint8_t* payload = xmalloc(400008);
	for (size_t i = 0; i < 400008; i++)
		payload[i] = (uint8_t)(i * 2654435761u >> 13);
	size_t nl = sizeof(MUXVEC_LEN) / sizeof(MUXVEC_LEN[0]);
	size_t nc = sizeof(MUXVEC_CH) / sizeof(MUXVEC_CH[0]);
	for (size_t i = 0; i < nl; i++) {
		for (size_t j = 0; j < nc; j++) {
			const uint8_t* d = payload + (i + j) % 7;
			uint64_t left = MUXVEC_LEN[i];
			uint8_t chv[10], lv[10];
			uint32_t chn = put_varint(chv, MUXVEC_CH[j]);
			uint32_t lvn = put_varint(lv, left);
			bool first = true;
			do {
				uint32_t head = chn + (first ? lvn : 0);
				uint64_t take = 65536u - head;
				if (take > left) take = left;
				uint32_t clen = head + (uint32_t)take;
				uint8_t hdr[3] = { 0x01, (uint8_t)(clen & 0xFF),
					(uint8_t)((clen >> 8) & 0xFF) };
				buf_put(b, hdr, 3);
				buf_put(b, chv, chn);
				if (first) buf_put(b, lv, lvn);
				buf_put(b, d, (size_t)take);
				d += take;
				left -= take;
				first = false;
			} while (left);
		}
	}
	free(payload);
}

static void test_mux_send_vector(void)
{
	printf("bvnr_mux_send: a fixed vector, byte for byte\n");
	size_t cap = 16u << 20;
	uint8_t* out = xmalloc(cap);
	uint64_t n = muxvec_write(out, cap);
	/* Pinned from the library before the chunk buffer moved onto the
	 * writer (2.0.2): the output must not have changed by a byte. */
	ASSERT_EQ_UINT(n, 6472695u, "vector length");
	ASSERT_EQ_UINT(fnv1a64(out, n), UINT64_C(0x2657c0c38be056c3),
		"vector hash");
	buf_t want = {0};
	muxvec_expect(&want);
	const uint8_t* open = memchr(out, 0x00, (size_t)n);
	ASSERT_TRUE(open && (uint64_t)(open - out) + 1u + want.n < n &&
		memcmp(open + 1, want.p, want.n) == 0 && open[1 + want.n] == 0x00,
		"the chunks follow the wire convention");
	buf_free(&want);

	/* Live: through a callback sink, flushed after every send. */
	cb_sink_t cs;
	memset(&cs, 0, sizeof(cs));
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_sink(w, &sink, false, NULL);
	uint64_t live = muxvec_send(w, true);
	bvnr_writer_destroy(w);
	ASSERT_TRUE(live == n && cs.out.n == n && memcmp(cs.out.p, out, n) == 0,
		"flushed after every send, the vector is the same");
	ASSERT_EQ_UINT(cs.flushes, 1u + sizeof(MUXVEC_LEN) / sizeof(MUXVEC_LEN[0])
		* (sizeof(MUXVEC_CH) / sizeof(MUXVEC_CH[0])),
		"one sink flush per send, and the finish's");
	buf_free(&cs.out);
	free(out);
}

/* ---------------------------------------------------------------------------
 * bvnr_write_flush: live writing. A flush moves bytes out of the writer's
 * buffer and changes nothing else, so every corpus document replayed through
 * a writer comes out the same whether it is flushed after no event, after
 * every event or after any single one -- and after each flush the sink holds
 * every byte written so far.
 * ------------------------------------------------------------------------- */
#define FLUSH_NEVER  (-2L)
#define FLUSH_EVERY  (-1L)

typedef struct {
	bvnr_writer_t* w;
	cb_sink_t*     cs;          /* NULL for a memory sink */
	long           flush_at;    /* an event index, FLUSH_NEVER, FLUSH_EVERY */
	long           index;
	bool           flush_failed;   /* a flush of a healthy writer failed */
	bool           held_back;      /* bytes left in the buffer after a flush */
} replay_t;

static bool replay_event(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	replay_t* r = ud;
	bool ok = bvnr_write_event(r->w, ev, d);
	long i = r->index++;
	if (ok && (r->flush_at == FLUSH_EVERY || r->flush_at == i)) {
		if (!bvnr_write_flush(r->w))
			r->flush_failed = true;
		if (r->cs && r->cs->out.n != bvnr_writer_bytes_written(r->w))
			r->held_back = true;
	}
	return ok;
}

typedef struct {
	buf_t        out;
	bool         read_ok, finish_ok;
	error_code_t err;
	uint64_t     err_offset;
	long         events;
	uint64_t     flushes;
	bool         flush_failed, held_back;
} replay_res_t;

/* Read `doc` and write every verified event again, into a callback sink, or
 * into a memory sink of `mem_cap` bytes when mem_cap is not 0. */
static void replay_doc(const doc_t* doc, bool pretty, long flush_at,
	uint64_t mem_cap, replay_res_t* res)
{
	memset(res, 0, sizeof(*res));
	cb_sink_t cs;
	memset(&cs, 0, sizeof(cs));
	bvnr_writer_t* w = bvnr_writer_create();
	uint8_t* mem = NULL;
	if (mem_cap) {
		mem = xmalloc((size_t)mem_cap);
		bvnr_open_write_mem(w, mem, mem_cap, pretty, NULL);
	} else {
		bvnr_sink_t sink;
		bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
		bvnr_open_write_sink(w, &sink, pretty, NULL);
	}
	replay_t r = { w, mem_cap ? NULL : &cs, flush_at, 0, false, false };
	bvnr_read_flags_t f;
	memset(&f, 0, sizeof(f));
	f.userdata    = &r;
	f.on_verified = replay_event;
	bvnr_reader_t* rd = bvnr_reader_create();
	res->read_ok = bvnr_open_read_mem(rd, doc->data, doc->len, NULL, 0, &f) &&
		       bvnr_read(rd);
	bvnr_reader_destroy(rd);
	res->finish_ok  = bvnr_write_finish(w);
	res->err        = bvnr_writer_get_error(w);
	res->err_offset = bvnr_writer_get_error_offset(w);
	res->events       = r.index;
	res->flush_failed = r.flush_failed;
	res->held_back    = r.held_back;
	if (mem_cap) {
		buf_put(&res->out, mem, (size_t)bvnr_writer_bytes_written(w));
		free(mem);
	} else {
		res->out = cs.out;
		res->flushes = cs.flushes;
	}
	bvnr_writer_destroy(w);
}

/* `a` matches the memory-sink run `mem`: the same result, and its sink got
 * exactly what the memory sink holds -- also when the finish failed, on an
 * earlier error or on a document it found incomplete (a read that stopped
 * part-way), because a failing bvnr_write_finish still pushes what is
 * buffered. */
static bool replay_same(const replay_res_t* a, const replay_res_t* mem)
{
	return buf_eq(&a->out, &mem->out) &&
	       a->read_ok == mem->read_ok && a->finish_ok == mem->finish_ok &&
	       a->err == mem->err && a->err_offset == mem->err_offset &&
	       a->events == mem->events;
}

/* Every single flush point is tried for documents of at most this many
 * events; flushing at all of them at once covers the rest. */
#define FLUSH_EACH_MAX 400

static void test_write_flush_identity(void)
{
	printf("bvnr_write_flush: the same bytes, flushed anywhere\n");
	unsigned long runs = 0;
	for (size_t i = 0; i < g_ndocs; i++) {
		const doc_t* doc = &g_docs[i];
		for (int pretty = 0; pretty < 2; pretty++) {
			replay_res_t ref, every, mem, one;
			replay_doc(doc, pretty, FLUSH_NEVER, 0, &ref);
			replay_doc(doc, pretty, FLUSH_EVERY, 0, &every);
			/* Room for what the callback sink got and one byte more: a
			 * memory sink that had to hold more would run out, and the
			 * comparison below would fail. */
			uint64_t cap = (uint64_t)ref.out.n + 1u;
			replay_doc(doc, pretty, FLUSH_EVERY, cap, &mem);
			char msg[160];
			snprintf(msg, sizeof(msg), "%s (%s): flushed after every "
				 "event", doc->name, pretty ? "pretty" : "compact");
			ASSERT_TRUE(replay_same(&every, &mem) &&
				    buf_eq(&every.out, &mem.out), msg);
			ASSERT_TRUE(!every.flush_failed && !every.held_back &&
				    !mem.flush_failed, msg);
			snprintf(msg, sizeof(msg), "%s (%s): never flushed",
				 doc->name, pretty ? "pretty" : "compact");
			ASSERT_TRUE(replay_same(&ref, &mem), msg);
			runs += 3;
			if (ref.events <= FLUSH_EACH_MAX) {
				bool all_same = true, clean = true;
				for (long k = 0; k < ref.events; k++) {
					replay_doc(doc, pretty, k, 0, &one);
					all_same = all_same && replay_same(&one, &mem);
					clean = clean && !one.flush_failed && !one.held_back;
					buf_free(&one.out);
					runs++;
				}
				snprintf(msg, sizeof(msg), "%s (%s): flushed after any "
					 "one event", doc->name,
					 pretty ? "pretty" : "compact");
				ASSERT_TRUE(all_same && clean, msg);
			}
			buf_free(&ref.out);
			buf_free(&every.out);
			buf_free(&mem.out);
		}
	}
	printf("  %lu replays of %zu documents\n", runs, g_ndocs);
}

/* A writer whose sink is a fed demux: what a socket does between two
 * processes, in one. Every push goes straight into the demux. */
typedef struct {
	bvnr_demux_t* dm;
	bool          feed_failed;
	uint64_t      msgs;
	uint64_t      last_ch, last_len;
	uint8_t*      last;        /* a copy of the last message */
	uint64_t      last_cap;
	bool          meta_done;   /* the preamble struct has been read */
	bool          msg_before_meta;
	uint64_t      flushes;
} live_t;

static bool live_push(bvnr_sink_t* s, const void* p, uint32_t n)
{
	live_t* l = bvnr_sink_get_userdata(s);
	if (!bvnr_demux_feed(l->dm, p, n))
		l->feed_failed = true;
	return true;
}

static bool live_flush(bvnr_sink_t* s)
{
	live_t* l = bvnr_sink_get_userdata(s);
	l->flushes++;
	return true;
}

static bool live_on_msg(void* ud, uint64_t ch, const uint8_t* p, uint64_t n)
{
	live_t* l = ud;
	if (!l->meta_done) l->msg_before_meta = true;
	if (n > l->last_cap) {
		l->last = xrealloc(l->last, (size_t)n);
		l->last_cap = n;
	}
	if (n) memcpy(l->last, p, (size_t)n);
	l->msgs++;
	l->last_ch = ch;
	l->last_len = n;
	return true;
}

static bool live_on_verified(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	(void)d;
	if (ev == ev_struct_end)
		((live_t*)ud)->meta_done = true;
	return true;
}

static void test_write_flush_live(void)
{
	printf("bvnr_write_flush: each message reaches a live demux when sent\n");
	static const uint64_t lens[] = {
		150, 0, 1, 3000, 65516, 65536, 70000, 150, 200000, 7 };
	const size_t nmsg = sizeof(lens) / sizeof(lens[0]);
	uint8_t* payload = xmalloc(200016);
	for (size_t i = 0; i < 200016; i++)
		payload[i] = (uint8_t)(i * 2654435761u >> 9);

	for (int pretty = 0; pretty < 2; pretty++) {
		live_t l;
		memset(&l, 0, sizeof(l));
		l.dm = bvnr_demux_create(live_on_msg, &l, 0);
		bvnr_demux_set_key(l.dm, "media");
		bvnr_read_flags_t fl;
		memset(&fl, 0, sizeof(fl));
		fl.on_verified = live_on_verified;
		fl.userdata = &l;
		ASSERT_TRUE(bvnr_demux_open_feed(l.dm, &fl, false), "open feed");

		bvnr_sink_t sink;
		bvnr_sink_to_callback(&sink, live_push, live_flush, &l);
		bvnr_writer_t* w = bvnr_writer_create();
		bvnr_open_write_sink(w, &sink, pretty, NULL);
		bvnr_write_struct_start(w, "meta");
		bvnr_write_string(w, "codec", "av1");
		bvnr_write_float_unit(w, "rate", 32, 30.0,
			BVN_UNIT_NO_PREFIX(bu_hertz));
		bvnr_write_struct_end(w);
		ASSERT_TRUE(bvnr_mux_begin(w, "media"), "mux_begin");
		ASSERT_TRUE(bvnr_write_flush(w), "flush after mux_begin");
		ASSERT_TRUE(l.meta_done, "the preamble is read before the first "
			    "message is sent");
		ASSERT_EQ_UINT(l.msgs, 0, "no message yet");

		bool in_step = true, same = true;
		for (size_t m = 0; m < nmsg; m++) {
			const uint8_t* p = payload + m % 13;
			ASSERT_TRUE(bvnr_mux_send(w, m % 3, p, lens[m]), "send");
			if (m == 0)
				ASSERT_EQ_UINT(l.msgs, 0, "a small message waits in the "
					       "buffer until the flush");
			ASSERT_TRUE(bvnr_write_flush(w), "flush after send");
			in_step = in_step && l.msgs == m + 1;
			same = same && l.last_ch == m % 3 && l.last_len == lens[m] &&
			       (lens[m] == 0 ||
				memcmp(l.last, p, (size_t)lens[m]) == 0);
		}
		ASSERT_TRUE(in_step, "message N arrives before message N+1 is sent");
		ASSERT_TRUE(same, "each message arrives whole and unchanged");
		ASSERT_TRUE(!l.msg_before_meta, "the preamble came first");
		ASSERT_EQ_UINT(l.flushes, nmsg + 1, "the sink's flush ran per flush");
		ASSERT_TRUE(bvnr_mux_end(w) && bvnr_write_finish(w), "end, finish");
		ASSERT_TRUE(bvnr_demux_finish(l.dm) && !l.feed_failed &&
			    bvnr_demux_error(l.dm) == error_none,
			    "the demux reads a complete document");
		bvnr_writer_destroy(w);
		bvnr_demux_destroy(l.dm);
		free(l.last);
	}
	free(payload);
}

static void test_write_flush_edges(void)
{
	printf("bvnr_write_flush: errors, memory and fd sinks\n");
	ASSERT_TRUE(!bvnr_write_flush(NULL), "NULL writer");
	bvnr_writer_t* w = bvnr_writer_create();
	ASSERT_TRUE(!bvnr_write_flush(w), "a writer with no sink");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_invalid_argument,
		"no sink is error_invalid_argument");

	/* Flush counts, a sink without a flush callback, after the finish. */
	cb_sink_t cs;
	memset(&cs, 0, sizeof(cs));
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "open");
	ASSERT_TRUE(bvnr_write_flush(w), "flush before any output");
	ASSERT_EQ_UINT(cs.pushes, 0, "nothing to push");
	ASSERT_EQ_UINT(cs.flushes, 1, "the sink's flush still runs");
	write_sample(w);
	ASSERT_TRUE(bvnr_write_flush(w), "flush");
	ASSERT_EQ_UINT(cs.pushes, 1, "one push");
	ASSERT_EQ_UINT(cs.out.n, bvnr_writer_bytes_written(w), "all pushed");
	ASSERT_TRUE(bvnr_write_flush(w), "flush again");
	ASSERT_EQ_UINT(cs.pushes, 1, "an empty buffer is not pushed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish");
	uint64_t pushes = cs.pushes, flushes = cs.flushes;
	ASSERT_TRUE(bvnr_write_flush(w), "flush after the finish");
	ASSERT_TRUE(cs.pushes == pushes && cs.flushes == flushes,
		"after the finish a flush does nothing");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_none, "and latches nothing");
	buf_free(&cs.out);

	memset(&cs, 0, sizeof(cs));
	bvnr_sink_to_callback(&sink, cb_push, NULL, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "re-open");
	write_sample(w);
	ASSERT_TRUE(bvnr_write_flush(w), "flush without a flush callback");
	ASSERT_EQ_UINT(cs.out.n, bvnr_writer_bytes_written(w), "pushed");
	buf_free(&cs.out);

	/* A refused push: the error latches, with the offset of the bytes. */
	memset(&cs, 0, sizeof(cs));
	cs.fail_after = 10;
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "re-open");
	write_sample(w);
	uint64_t at = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(!bvnr_write_flush(w), "a refused push fails the flush");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_writing_to_sink,
		"refused push is error_writing_to_sink");
	ASSERT_EQ_UINT(bvnr_writer_get_error_offset(w), at,
		"the offset is where the output stood");
	ASSERT_EQ_UINT(cs.flushes, 0, "no sink flush after a failed push");
	ASSERT_TRUE(!bvnr_write_uint(w, "more", 8, 1), "the error is latched");
	ASSERT_TRUE(!bvnr_write_flush(w), "a second flush fails at once");
	ASSERT_EQ_UINT(cs.pushes, 1, "and pushes nothing");
	ASSERT_TRUE(!bvnr_write_finish(w), "the finish fails");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_writing_to_sink,
		"the first error stays");
	buf_free(&cs.out);

	/* A refused flush callback. */
	memset(&cs, 0, sizeof(cs));
	cs.flush_fails = true;
	bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "re-open");
	write_sample(w);
	ASSERT_TRUE(!bvnr_write_flush(w), "a refused flush fails the flush");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_writing_to_sink,
		"refused flush is error_writing_to_sink");
	ASSERT_TRUE(cs.flushes == 1 && cs.out.n == bvnr_writer_bytes_written(w),
		"the bytes were pushed before the sink's flush");
	buf_free(&cs.out);

	/* A memory sink: nothing to do, nothing changes. */
	uint8_t ref[512], mem[512];
	bvnr_open_write_mem(w, ref, sizeof(ref), true, NULL);
	write_sample(w);
	bvnr_write_finish(w);
	uint64_t ref_n = bvnr_writer_bytes_written(w);
	bvnr_open_write_mem(w, mem, sizeof(mem), true, NULL);
	ASSERT_TRUE(bvnr_write_flush(w), "memory sink: flush before output");
	bvnr_write_uint(w, "id", 32, 7);
	uint64_t before = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(bvnr_write_flush(w), "memory sink: flush");
	ASSERT_EQ_UINT(bvnr_writer_bytes_written(w), before,
		"memory sink: the count does not move");
	bvnr_write_string(w, "name", "caf\xc3\xa9");
	bvnr_write_struct_start(w, "s");
	ASSERT_TRUE(bvnr_write_flush(w), "memory sink: flush inside a struct");
	bvnr_write_bool(w, "f", true);
	bvnr_write_struct_end(w);
	ASSERT_TRUE(bvnr_write_finish(w), "memory sink: finish");
	ASSERT_TRUE(bvnr_writer_bytes_written(w) == ref_n &&
		    memcmp(ref, mem, (size_t)ref_n) == 0,
		"memory sink: the same bytes");
	/* A full memory sink fails the write; the flush reports the latch. */
	bvnr_open_write_mem(w, mem, 4, false, NULL);
	ASSERT_TRUE(!bvnr_write_uint(w, "value", 32, 12345), "full buffer");
	ASSERT_TRUE(!bvnr_write_flush(w), "memory sink: flush after the error");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_sink_buffer_exhausted,
		"memory sink: the error is the write's");

#if !defined(_WIN32)
	/* An fd sink: the bytes are in the pipe after the flush, not before. */
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0, "pipe");
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	bvnr_sink_t fd_sink;
	bvnr_sink_to_fd(&fd_sink, fds[1]);
	ASSERT_TRUE(bvnr_open_write_sink(w, &fd_sink, true, NULL), "open fd");
	write_sample(w);
	uint8_t got[1024];
	ssize_t n0 = read(fds[0], got, sizeof(got));
	ASSERT_TRUE(n0 < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
		"fd sink: nothing in the pipe before the flush");
	ASSERT_TRUE(bvnr_write_flush(w), "fd sink: flush");
	ssize_t n1 = read(fds[0], got, sizeof(got));
	ASSERT_TRUE(n1 > 0 && (uint64_t)n1 == bvnr_writer_bytes_written(w),
		"fd sink: everything written is in the pipe");
	ASSERT_TRUE(bvnr_write_finish(w), "fd sink: finish");
	ssize_t n2 = read(fds[0], got + (n1 > 0 ? n1 : 0),
			  sizeof(got) - (size_t)(n1 > 0 ? n1 : 0));
	ASSERT_TRUE(n1 > 0 && n2 > 0 && (uint64_t)(n1 + n2) == ref_n &&
		    memcmp(got, ref, (size_t)ref_n) == 0,
		"fd sink: flushed and finished, the same bytes");
	close(fds[0]);
	close(fds[1]);
#endif
	bvnr_writer_destroy(w);
}

/* ---------------------------------------------------------------------------
 * bvnr_write_finish on a document it finds incomplete: it fails, and still
 * hands the sink everything written, as it does after an earlier error. An fd
 * or callback sink used to keep only what its 64 KiB buffer had pushed, a
 * prefix of what a memory sink holds.
 * ------------------------------------------------------------------------- */
enum { STOP_STRUCT, STOP_KEY, STOP_ANNOTATION, STOP_MUX, STOP_SPELLING,
       STOP_COUNT };

static void write_key(bvnr_writer_t* w, const char* key)
{
	bvnr_data_t d;
	memset(&d, 0, sizeof(d));
	d.type   = token_is_identifier;
	d.data   = key;
	d.length = (uint32_t)strlen(key);
	bvnr_write_event(w, ev_assignment_start, &d);
}

/* A document that stops part-way in the way `stop` names, after `filler`
 * bytes of complete values (enough to pass the 64 KiB buffer, so that the
 * sink has had a push before the finish). */
static void write_stopped(bvnr_writer_t* w, int stop, size_t filler)
{
	static char text[4097];
	memset(text, 'x', sizeof(text) - 1u);
	for (size_t n = 0; n < filler; n += sizeof(text) - 1u)
		bvnr_write_string(w, "t", text);
	switch (stop) {
	case STOP_STRUCT:
		bvnr_write_struct_start(w, "s");
		bvnr_write_bool(w, "f", true);
		break;
	case STOP_KEY:
		write_key(w, "k");
		break;
	case STOP_ANNOTATION: {
		bvnr_data_t d;
		memset(&d, 0, sizeof(d));
		write_key(w, "k");
		d.type = token_is_type;
		d.value_type.family = vt_uint;
		d.value_type.width  = 8;
		bvnr_write_event(w, ev_type_annotation_start, &d);
		bvnr_write_event(w, ev_type_annotation_type_family, &d);
		break;
	}
	case STOP_MUX:
		bvnr_mux_begin(w, "media");
		bvnr_mux_send(w, 1, "frame", 5);
		break;
	case STOP_SPELLING:
		/* A complete document, but a unit spelling was handed over for a
		 * value that never came: the finish reports error_unit_mismatch. */
		bvnr_write_uint(w, "n", 8, 1);
		bvnr_writer_set_unit_source(w, "km", 2);
		break;
	}
}

static void test_write_finish_incomplete(void)
{
	printf("bvnr_write_finish: an incomplete document still reaches the sink\n");
	static const error_code_t want[STOP_COUNT] = {
		error_got_incomplete_bvnr_stream, error_got_incomplete_bvnr_stream,
		error_got_incomplete_bvnr_stream, error_got_incomplete_bvnr_stream,
		error_unit_mismatch };
	static const size_t fillers[] = { 0, 100, 70000 };
	const uint64_t cap = 1u << 20;
	uint8_t* mem = xmalloc((size_t)cap);
	for (int stop = 0; stop < STOP_COUNT; stop++) {
		for (size_t f = 0; f < sizeof(fillers) / sizeof(fillers[0]); f++) {
			char msg[160];
			snprintf(msg, sizeof(msg), "stop %d after %zu bytes", stop,
				 fillers[f]);
			bvnr_writer_t* wm = bvnr_writer_create();
			bvnr_open_write_mem(wm, mem, cap, false, NULL);
			write_stopped(wm, stop, fillers[f]);
			bool mem_ok = bvnr_write_finish(wm);
			uint64_t mem_n = bvnr_writer_bytes_written(wm);

			cb_sink_t cs;
			memset(&cs, 0, sizeof(cs));
			bvnr_sink_t sink;
			bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
			bvnr_writer_t* w = bvnr_writer_create();
			bvnr_open_write_sink(w, &sink, false, NULL);
			write_stopped(w, stop, fillers[f]);
			ASSERT_TRUE(bvnr_writer_get_error(w) == error_none, msg);
			ASSERT_TRUE(!mem_ok && !bvnr_write_finish(w), msg);
			ASSERT_EQ_UINT(bvnr_writer_get_error(w), want[stop], msg);
			ASSERT_EQ_UINT(bvnr_writer_get_error(wm), want[stop], msg);
			ASSERT_EQ_UINT(bvnr_writer_get_error_offset(w),
				bvnr_writer_get_error_offset(wm), msg);
			ASSERT_TRUE(cs.out.n == mem_n &&
				    memcmp(cs.out.p, mem, (size_t)mem_n) == 0, msg);
			ASSERT_EQ_UINT(cs.flushes, 1, msg);
			/* A second finish keeps the first error and pushes nothing. */
			uint32_t pushes = cs.pushes;
			ASSERT_TRUE(!bvnr_write_finish(w), msg);
			ASSERT_EQ_UINT(bvnr_writer_get_error(w), want[stop], msg);
			ASSERT_EQ_UINT(cs.pushes, pushes, msg);
			buf_free(&cs.out);

			/* A sink that refuses that last push: the error stays the
			 * finish's own, at the same offset. */
			if (mem_n % 65536u) {
				memset(&cs, 0, sizeof(cs));
				cs.fail_after = mem_n - 1u;
				bvnr_sink_to_callback(&sink, cb_push, cb_flush, &cs);
				bvnr_open_write_sink(w, &sink, false, NULL);
				write_stopped(w, stop, fillers[f]);
				ASSERT_TRUE(!bvnr_write_finish(w), msg);
				ASSERT_EQ_UINT(bvnr_writer_get_error(w), want[stop],
					msg);
				ASSERT_EQ_UINT(bvnr_writer_get_error_offset(w),
					bvnr_writer_get_error_offset(wm), msg);
				ASSERT_EQ_UINT(cs.flushes, 0, msg);
				buf_free(&cs.out);
			}
			bvnr_writer_destroy(w);
			bvnr_writer_destroy(wm);
		}
	}

#if !defined(_WIN32)
	/* An fd sink gets the whole of it too. */
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0, "pipe");
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	bvnr_writer_t* wm = bvnr_writer_create();
	bvnr_open_write_mem(wm, mem, cap, false, NULL);
	write_stopped(wm, STOP_STRUCT, 100);
	bvnr_write_finish(wm);
	uint64_t mem_n = bvnr_writer_bytes_written(wm);
	bvnr_sink_t fd_sink;
	bvnr_sink_to_fd(&fd_sink, fds[1]);
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_sink(w, &fd_sink, false, NULL);
	write_stopped(w, STOP_STRUCT, 100);
	ASSERT_TRUE(!bvnr_write_finish(w), "fd sink: the finish fails");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w),
		error_got_incomplete_bvnr_stream, "fd sink: incomplete");
	uint8_t got[8192];
	ssize_t n = read(fds[0], got, sizeof(got));
	ASSERT_TRUE(n > 0 && (uint64_t)n == mem_n &&
		    memcmp(got, mem, (size_t)mem_n) == 0,
		"fd sink: the bytes a memory sink holds");
	close(fds[0]);
	close(fds[1]);
	bvnr_writer_destroy(w);
	bvnr_writer_destroy(wm);
#endif
	free(mem);
}

/* ---- fixes from the streaming review ----------------------------------- */

static bool mux_on_msg_nop(void* ud, uint64_t ch, const uint8_t* p,
	uint64_t n)
{
	(void)ud; (void)ch; (void)p; (void)n;
	return true;
}

static bool count_events(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	(void)ev; (void)d;
	(*(uint64_t*)ud)++;
	return true;
}

/* A pulled demux with deliver_events hands events out in batches: the reader
 * stops once a batch is queued, instead of lexing (and copying) the whole fed
 * buffer before the first bvnr_demux_next returns. */
static void test_demux_event_batches(void)
{
	buf_t doc = {0};
	buf_put(&doc, LIT(".a = ["));
	for (int i = 0; i < 20000; i++)
		buf_put(&doc, LIT("1,"));
	buf_put(&doc, LIT("1];"));
	uint64_t seen = 0;
	bvnr_read_flags_t fl;
	memset(&fl, 0, sizeof(fl));
	fl.on_verified = count_events;
	fl.userdata = &seen;
	bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, 0);
	ASSERT_TRUE(bvnr_demux_open_feed(dm, &fl, true), "batches: open");
	ASSERT_TRUE(bvnr_demux_feed(dm, doc.p, doc.n), "batches: feed");
	bvnr_demux_item_t it;
	ASSERT_EQ_UINT(bvnr_demux_next(dm, &it), BVNR_NEXT_EVENT,
		"batches: an event first");
	ASSERT_TRUE(seen <= 256, "batches: the reader stopped after a batch");
	uint64_t events = 1;
	int rc;
	while ((rc = bvnr_demux_next(dm, &it)) == BVNR_NEXT_EVENT)
		events++;
	ASSERT_EQ_UINT(rc, BVNR_NEXT_NONE, "batches: drained");
	ASSERT_TRUE(bvnr_demux_finish(dm), "batches: finish");
	while (bvnr_demux_next(dm, &it) == BVNR_NEXT_EVENT)
		events++;
	ASSERT_EQ_UINT(events, seen, "batches: every event handed out");
	bvnr_demux_destroy(dm);
	buf_free(&doc);
}

/* An error the pulled demux finds in bvnr_demux_next ends its reader at the
 * position the callback demux reports. */
static void test_demux_pull_error_position(void)
{
	static const uint8_t doc[] = ".m = \x00\x01\x07\x00\x01\x05hello\x00;";
	size_t n = sizeof(doc) - 1;
	bvnr_demux_t* cb = bvnr_demux_create(mux_on_msg_nop, NULL, 4);
	bvnr_demux_open_feed(cb, NULL, false);
	ASSERT_TRUE(!bvnr_demux_feed(cb, doc, n), "pull error: callback fails");
	const bvnr_reader_t* cr = bvnr_demux_get_reader(cb);

	bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, 4);
	bvnr_demux_open_feed(dm, NULL, false);
	ASSERT_TRUE(bvnr_demux_feed(dm, doc, n), "pull error: feed");
	bvnr_demux_item_t it;
	ASSERT_EQ_UINT(bvnr_demux_next(dm, &it), BVNR_NEXT_ERROR,
		"pull error: next fails");
	const bvnr_reader_t* pr = bvnr_demux_get_reader(dm);
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_value_out_of_range,
		"pull error: the demux's code");
	ASSERT_EQ_UINT(bvnr_demux_error(dm), bvnr_demux_error(cb),
		"pull error: the same code in both modes");
	ASSERT_EQ_UINT(bvnr_reader_get_error(pr), bvnr_reader_get_error(cr),
		"pull error: the same reader error");
	ASSERT_EQ_UINT(bvnr_reader_get_error(pr), error_scanner_callback_failed,
		"pull error: the reader was aborted");
	ASSERT_EQ_UINT(bvnr_reader_get_error_offset(pr),
		bvnr_reader_get_error_offset(cr), "pull error: the same offset");
	ASSERT_EQ_UINT(bvnr_reader_get_error_column(pr),
		bvnr_reader_get_error_column(cr), "pull error: the same column");
	ASSERT_TRUE(!bvnr_demux_finish(dm), "pull error: finish fails");
	bvnr_demux_destroy(dm);
	bvnr_demux_destroy(cb);
}

/* A message cut short by the stream's end does not fail the read; when the
 * document then fails, that failure is the error. */
static void test_demux_truncation_then_failure(void)
{
	static const uint8_t doc[] =
		".m = \x00\x01\x04\x00\x01\x09hi\x00;\n.x = = 1;\n";
	bvnr_demux_t* dm = bvnr_demux_create(mux_on_msg_nop, NULL, 0);
	bvnr_demux_open_feed(dm, NULL, false);
	bool ok = bvnr_demux_feed(dm, doc, sizeof(doc) - 1) &&
		  bvnr_demux_finish(dm);
	ASSERT_TRUE(!ok, "truncated+failed: the read fails");
	const bvnr_reader_t* rd = bvnr_demux_get_reader(dm);
	ASSERT_TRUE(bvnr_reader_get_error(rd) != error_none,
		"truncated+failed: the reader failed");
	/* The demux's own error comes first, as on a reader of the whole
	 * buffer; the reader says why the read failed. */
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_octet_stream_truncated,
		"truncated+failed: the demux's error, as in batch mode");
	ASSERT_EQ_UINT(bvnr_reader_get_error(rd), error_unexpected_input_byte,
		"truncated+failed: the reader's error");
	/* The truncation alone is still reported, with a read that succeeds. */
	static const uint8_t cut[] = ".m = \x00\x01\x04\x00\x01\x09hi\x00;\n";
	bvnr_demux_open_feed(dm, NULL, false);
	ok = bvnr_demux_feed(dm, cut, sizeof(cut) - 1) && bvnr_demux_finish(dm);
	ASSERT_TRUE(ok, "truncated: the read succeeds");
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_octet_stream_truncated,
		"truncated: reported");
	bvnr_demux_destroy(dm);
}

/* An empty field of a queued event is an empty string, not NULL. */
static void test_demux_empty_event_field(void)
{
	bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, 0);
	bvnr_demux_open_feed(dm, NULL, true);
	ASSERT_TRUE(bvnr_demux_feed(dm, ".", 1), "empty field: feed");
	bvnr_demux_item_t it;
	ASSERT_EQ_UINT(bvnr_demux_next(dm, &it), BVNR_NEXT_EVENT,
		"empty field: stream start");
	ASSERT_EQ_UINT(it.event, ev_stream_start, "empty field: the event");
	ASSERT_TRUE(it.value && (it.value->data || !it.value->length),
		"empty field: a consistent value");
	bvnr_demux_destroy(dm);
}

/* bvnr_mux_send's own refusals latch as writer errors. */
static void test_mux_send_latches(void)
{
	uint8_t out[256];
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_mem(w, out, sizeof(out), false, NULL);
	ASSERT_TRUE(bvnr_mux_begin(w, "mux"), "mux latch: begin");
	ASSERT_TRUE(!bvnr_mux_send(w, 1, NULL, 3), "mux latch: NULL data");
	ASSERT_EQ_UINT(bvnr_writer_get_error(w), error_invalid_argument,
		"mux latch: latched");
	ASSERT_TRUE(!bvnr_mux_send(w, 1, "abc", 3), "mux latch: sticky");
	ASSERT_TRUE(!bvnr_write_finish(w), "mux latch: finish fails");
	bvnr_writer_destroy(w);
}

/* A pull that claims more bytes than it was asked for is an I/O error. */
static bool lying_pull(bvnr_source_t* s, void* out, uint32_t want,
	uint32_t* got)
{
	(void)s;
	memset(out, ' ', want);
	*got = want + 1000u;
	return true;
}

static void test_pull_overclaim(void)
{
	bvnr_source_t src;
	bvnr_source_from_callback(&src, lying_pull, NULL);
	bvnr_reader_t* r = bvnr_reader_create();
	ASSERT_TRUE(bvnr_open_read_source(r, &src, NULL, NULL),
		"overclaim: open");
	ASSERT_TRUE(!bvnr_read(r), "overclaim: refused");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_reading_from_source_fd,
		"overclaim: an I/O error");
	bvnr_reader_destroy(r);
	bvnr_source_from_callback(&src, lying_pull, NULL);
	uint64_t count = 0;
	ASSERT_TRUE(!bvnr_doc_stream_read(&src, NULL, &count),
		"overclaim: frames refused");
	/* NULL handles are ignored rather than written through. */
	bvnr_source_from_callback(NULL, lying_pull, NULL);
	bvnr_sink_to_callback(NULL, NULL, NULL, NULL);
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, NULL, NULL, NULL);
	ASSERT_TRUE(!bvnr_frame_write(&sink, "x", 1),
		"overclaim: a sink without push is refused");
}

/* After a fed read has ended, bvnr_read leaves its error alone. */
static void test_read_on_finished_feed(void)
{
	bvnr_reader_t* r = bvnr_reader_create();
	bvnr_open_read_feed(r, NULL, NULL);
	bvnr_read_feed(r, LIT(".a = "));
	ASSERT_TRUE(!bvnr_read_finish(r), "finished feed: cut off");
	error_code_t err = bvnr_reader_get_error(r);
	ASSERT_TRUE(!bvnr_read(r), "finished feed: bvnr_read refused");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), err,
		"finished feed: error kept");
	bvnr_open_read_feed(r, NULL, NULL);
	ASSERT_TRUE(!bvnr_read(r), "open feed: bvnr_read refused");
	ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_invalid_argument,
		"open feed: invalid argument");
	bvnr_reader_destroy(r);
}

/* A call into a fed reader, a demux or a parsing frame reader from inside one
 * of its own callbacks is refused: the read fails with error_invalid_argument
 * and the call running stops right after the event that made it. Before the
 * guard, a demux fed again from on_message re-lexed the window the outer feed
 * was still in and underflowed its length. */
typedef struct {
	bvnr_reader_t*		r;
	bvnr_demux_t*		dm;
	bvnr_frame_reader_t*	fr;
	int			mode;
	int			events;
	int			inner_ok;
} reenter_t;

static bool reenter_on_verified(void* ud, bvnr_event_t e, bvnr_data_t* d)
{
	reenter_t* t = (reenter_t*)ud;
	(void)d;
	t->events++;
	if (e != ev_assignment_start || t->events > 4)
		return true;
	switch (t->mode) {
	case 0: t->inner_ok += bvnr_read_feed(t->r, LIT(".z = 9;")); break;
	case 1: t->inner_ok += bvnr_read_finish(t->r); break;
	case 2: t->inner_ok += bvnr_open_read_feed(t->r, NULL, NULL); break;
	case 3: t->inner_ok += bvnr_read(t->r); break;
	default: t->inner_ok += bvnr_frame_reader_feed(t->fr, LIT("x")); break;
	}
	return true;
}

static bool reenter_on_message(void* ud, uint64_t channel,
	const uint8_t* data, uint64_t len)
{
	reenter_t* t = (reenter_t*)ud;
	(void)channel;
	t->events++;
	if (t->mode == 0)
		t->inner_ok += bvnr_demux_feed(t->dm, data, len);
	else
		t->inner_ok += bvnr_demux_finish(t->dm);
	return true;
}

static bool reenter_on_document(void* ud, uint64_t index, bool ok,
	error_code_t err)
{
	reenter_t* t = (reenter_t*)ud;
	(void)index; (void)ok; (void)err;
	t->events++;
	t->inner_ok += bvnr_frame_reader_feed(t->fr, LIT("x"));
	return true;
}

static void test_reentry(void)
{
	printf("re-entry: feeding a reader from inside its own callbacks\n");
	static const char doc[] = ".a = 1; .b = 2; .c = 3; .d = 4; .e = 5;";
	for (int mode = 0; mode < 4; mode++) {
		reenter_t t = { .mode = mode };
		bvnr_read_flags_t f = { .userdata = &t,
			.on_verified = reenter_on_verified };
		t.r = bvnr_reader_create();
		bvnr_open_read_feed(t.r, NULL, &f);
		ASSERT_TRUE(!bvnr_read_feed(t.r, LIT(doc)),
			"re-entry: the outer feed fails");
		ASSERT_EQ_UINT(t.inner_ok, 0, "re-entry: the inner call is refused");
		ASSERT_EQ_UINT(bvnr_reader_get_error(t.r), error_invalid_argument,
			"re-entry: invalid argument");
		/* Stopped after the first assignment's start: two events. */
		ASSERT_EQ_UINT(t.events, 2, "re-entry: the outer feed stops");
		ASSERT_TRUE(!bvnr_read_finish(t.r), "re-entry: the read is over");
		/* The reader is still usable for the next document. */
		t.mode = 9;
		f.on_verified = NULL;
		ASSERT_TRUE(bvnr_open_read_feed(t.r, NULL, &f) &&
			bvnr_read_feed(t.r, LIT(doc)) && bvnr_read_finish(t.r),
			"re-entry: the next document reads");
		bvnr_reader_destroy(t.r);
	}

	/* The demux, callback mode: fed or finished from on_message. */
	uint8_t out[4096];
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_mem(w, out, sizeof(out), false, NULL);
	bvnr_mux_begin(w, "mux");
	for (int i = 0; i < 8; i++)
		bvnr_mux_send(w, 1, "\x01payload", 8);
	bvnr_mux_end(w);
	ASSERT_TRUE(bvnr_write_finish(w), "re-entry: mux document written");
	uint64_t n = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);
	for (int mode = 0; mode < 2; mode++) {
		reenter_t t = { .mode = mode };
		t.dm = bvnr_demux_create(reenter_on_message, &t, 0);
		ASSERT_TRUE(bvnr_demux_open_feed(t.dm, NULL, false),
			"re-entry: demux open");
		ASSERT_TRUE(!bvnr_demux_feed(t.dm, out, n),
			"re-entry: the outer demux feed fails");
		ASSERT_EQ_UINT(t.inner_ok, 0, "re-entry: the inner demux call is "
			"refused");
		ASSERT_EQ_UINT(t.events, 1, "re-entry: no message after it");
		ASSERT_EQ_UINT(bvnr_demux_error(t.dm), error_invalid_argument,
			"re-entry: demux invalid argument");
		ASSERT_TRUE(!bvnr_demux_finish(t.dm), "re-entry: demux over");
		bvnr_demux_destroy(t.dm);
	}

	/* The parsing frame reader, from on_document and from the reader's own
	 * on_verified. */
	uint8_t frames[256];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, frames, sizeof(frames));
	bvnr_frame_write(&sink, LIT(".a = 1;"));
	bvnr_frame_write(&sink, LIT(".b = 2;"));
	uint64_t fn = bvnr_sink_bytes_written(&sink);
	for (int mode = 0; mode < 2; mode++) {
		reenter_t t = { .mode = 4 };
		bvnr_read_flags_t f = { .userdata = &t,
			.on_verified = reenter_on_verified };
		bvnr_doc_stream_opts_t o = { .userdata = &t };
		if (mode == 0)
			o.on_document = reenter_on_document;
		else
			o.flags = &f;
		t.fr = bvnr_frame_reader_create(&o);
		ASSERT_TRUE(!bvnr_frame_reader_feed(t.fr, frames, fn),
			"re-entry: the outer frame feed fails");
		ASSERT_EQ_UINT(t.inner_ok, 0, "re-entry: the inner frame feed is "
			"refused");
		ASSERT_EQ_UINT(bvnr_frame_reader_error(t.fr), error_invalid_argument,
			"re-entry: frame reader invalid argument");
		ASSERT_EQ_UINT(bvnr_frame_reader_count(t.fr), mode == 0 ? 1 : 0,
			"re-entry: no document after it");
		bvnr_frame_reader_destroy(t.fr);
	}
}

int main(int argc, char** argv)
{
	corpus_builtin();
	corpus_generated();
	if (argc > 1)
		corpus_dir(argv[1]);

	test_callback_io();
	test_feed_api_edges();
	test_feed_equivalence();
	test_feed_prefixes();
	test_feed_endless();
	test_frame_reader();
	test_reader_reuse();
	test_frame_isolation();
	test_demux_feed();
	test_mux_send_vector();
	test_write_flush_identity();
	test_write_flush_live();
	test_write_flush_edges();
	test_write_finish_incomplete();
	test_demux_event_batches();
	test_demux_pull_error_position();
	test_demux_truncation_then_failure();
	test_demux_empty_event_field();
	test_mux_send_latches();
	test_pull_overclaim();
	test_read_on_finished_feed();
	test_reentry();

	for (size_t i = 0; i < g_ndocs; i++) {
		free(g_docs[i].name);
		free(g_docs[i].data);
	}
	free(g_docs);
	printf("\n%d checks, %d failures\n", g_tests, g_failures);
	return g_failures ? 1 : 0;
}
