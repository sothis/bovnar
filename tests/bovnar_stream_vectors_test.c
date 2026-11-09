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
 * The shared streaming vectors (tests/stream_vectors/, doc/10 §9) against the
 * C library: the reference side of what the Rust codec is held to.
 *
 * The manifest is read with the DOM. Every multiplexed vector is read by the
 * batch demultiplexer (bvnr_demux_on_event on bvnr_read) and by the push-fed
 * one drained with bvnr_demux_next, fed whole, a byte at a time, cut in two
 * at every position (sampled positions for the large vectors) and in random
 * pieces; each must hand out the messages the manifest lists and end with
 * its error. Every canonical vector must come out of bvnr_mux_send byte for
 * byte, its preamble replayed into the writer first, with and without a
 * bvnr_write_flush after each message. The frame vectors are read by the
 * pull-mode frame reader and by the parsing one, with and without
 * continue_past_failed, cut the same ways, and by bvnr_doc_stream_read_ex;
 * the parsing readers must report each document's own outcome. A frame
 * vector that ends cleanly must come out of bvnr_frame_write byte for byte.
 *
 * Usage: bvnr_stream_vectors_test <tests/stream_vectors>
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_dom.h"
#include "bovnar_stream.h"

static int g_failures = 0;
static int g_tests    = 0;

#define CHECK(cond, ...) do {                                                \
	g_tests++;                                                           \
	if (!(cond)) {                                                       \
		g_failures++;                                                \
		fprintf(stderr, "  FAIL: ");                                 \
		fprintf(stderr, __VA_ARGS__);                                \
		fprintf(stderr, "\n");                                       \
	}                                                                    \
} while (0)

/* Vectors up to this size are cut in two at every position. */
#define EVERY_CUT_MAX 4096u

typedef struct { uint8_t* p; size_t n, cap; } buf_t;

static void* xrealloc(void* p, size_t n)
{
	void* q = realloc(p, n ? n : 1);
	if (!q) { fprintf(stderr, "out of memory\n"); exit(2); }
	return q;
}

static void buf_put(buf_t* b, const void* data, size_t len)
{
	if (b->n + len > b->cap) {
		b->cap = (b->n + len) * 2;
		b->p = xrealloc(b->p, b->cap);
	}
	if (len) memcpy(b->p + b->n, data, len);
	b->n += len;
}

static void buf_free(buf_t* b)
{
	free(b->p);
	memset(b, 0, sizeof(*b));
}

static uint64_t fnv1a64(const uint8_t* p, uint64_t n, uint64_t h)
{
	for (uint64_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= UINT64_C(0x100000001b3);
	}
	return h;
}
#define FNV_INIT UINT64_C(0xcbf29ce484222325)

static uint64_t rng_next(uint64_t* s)
{
	*s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
	return *s;
}

static bool read_file(const char* path, buf_t* out)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	uint8_t tmp[65536];
	size_t n;
	while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0)
		buf_put(out, tmp, n);
	bool ok = !ferror(f);
	fclose(f);
	return ok;
}

/* ---------------------------------------------------------------------------
 * The manifest.
 * ------------------------------------------------------------------------- */
typedef struct {
	uint64_t    channel, length, fnv;
	const char* error;        /* a document's outcome when parsed alone */
} msg_t;

typedef struct {
	const char* name;
	const char* file;
	bool        is_mux;
	uint64_t    size;
	uint64_t    max_value;    /* max_message or max_document_size */
	msg_t*      items;        /* messages, or documents (channel unused) */
	uint32_t    nitems;
	uint64_t    digest;
	const char* error;
	bool        canonical;
	const char* preamble;
	uint32_t    preamble_len;
} vector_t;

static const char* get_str(const bvn_dom_node_t* s, const char* key,
	uint32_t* len)
{
	const char* out = NULL;
	uint32_t n = 0;
	bvn_dom_node_t* v = bvn_dom_struct_get(s, key);
	if (!bvn_dom_get_string(v, &out, &n) && !bvn_dom_get_symbol(v, &out, &n)) {
		fprintf(stderr, "manifest: .%s missing or not text\n", key);
		exit(2);
	}
	if (len) *len = n;
	return out;
}

static uint64_t get_u64(const bvn_dom_node_t* s, const char* key)
{
	uint64_t v = 0;
	if (!bvn_dom_get_u64(bvn_dom_struct_get(s, key),
			     &v)) {
		fprintf(stderr, "manifest: .%s missing or not an integer\n", key);
		exit(2);
	}
	return v;
}

static uint64_t get_hex(const bvn_dom_node_t* s, const char* key)
{
	return strtoull(get_str(s, key, NULL), NULL, 16);
}

static void load_vector(const bvn_dom_entry_t* e, vector_t* v)
{
	const bvn_dom_node_t* s = e->value;
	memset(v, 0, sizeof(*v));
	v->name   = e->key;
	v->file   = get_str(s, "file", NULL);
	v->is_mux = strcmp(get_str(s, "kind", NULL), "mux") == 0;
	v->size   = get_u64(s, "size");
	v->error  = get_str(s, "error", NULL);
	const char* list = v->is_mux ? "messages" : "documents";
	bvn_dom_node_t* a = bvn_dom_struct_get(s, list);
	v->nitems = bvn_dom_array_count(a);
	v->items  = xrealloc(NULL, v->nitems * sizeof(*v->items));
	for (uint32_t i = 0; i < v->nitems; i++) {
		bvn_dom_node_t* m = bvn_dom_array_at(a, i);
		v->items[i].channel = v->is_mux ? get_u64(m, "channel") : 0;
		v->items[i].length  = get_u64(m, "length");
		v->items[i].fnv     = get_hex(m, "fnv");
		v->items[i].error   = v->is_mux ? "none"
					: get_str(m, "error", NULL);
	}
	if (v->is_mux) {
		v->max_value = get_u64(s, "max_message");
		v->digest    = get_hex(s, "digest");
		v->preamble  = get_str(s, "preamble", &v->preamble_len);
		bool c = false;
		bvn_dom_get_bool(bvn_dom_struct_get(s, "canonical"), &c);
		v->canonical = c;
	} else {
		v->max_value = get_u64(s, "max_document_size");
	}
}

/* ---------------------------------------------------------------------------
 * What a reader handed out.
 * ------------------------------------------------------------------------- */
typedef struct {
	msg_t*   items;
	uint32_t n, cap;
	uint64_t digest;
	buf_t    payloads;    /* kept for the writers, when asked */
	bool     keep;
} got_t;

static void got_add(got_t* g, uint64_t ch, const uint8_t* p, uint64_t len)
{
	if (g->n == g->cap) {
		g->cap = g->cap ? g->cap * 2 : 16;
		g->items = xrealloc(g->items, g->cap * sizeof(*g->items));
	}
	g->items[g->n].channel = ch;
	g->items[g->n].length  = len;
	g->items[g->n].fnv     = fnv1a64(p, len, FNV_INIT);
	g->n++;
	uint8_t le[16];
	for (int i = 0; i < 8; i++) {
		le[i]     = (uint8_t)(ch >> (8 * i));
		le[8 + i] = (uint8_t)(len >> (8 * i));
	}
	g->digest = fnv1a64(p, len, fnv1a64(le, 16, g->digest));
	if (g->keep) buf_put(&g->payloads, p, (size_t)len);
}

static void got_free(got_t* g)
{
	free(g->items);
	buf_free(&g->payloads);
}

/* The messages (or documents) and the outcome match the manifest. */
static bool same(const vector_t* v, const got_t* g, error_code_t err,
	const char* reader, const char* how)
{
	bool ok = g->n == v->nitems &&
		  strcmp(bvn_error_to_string(err), v->error) == 0;
	for (uint32_t i = 0; ok && i < g->n; i++)
		ok = g->items[i].channel == v->items[i].channel &&
		     g->items[i].length == v->items[i].length &&
		     g->items[i].fnv == v->items[i].fnv;
	if (ok && v->is_mux)
		ok = g->digest == v->digest;
	if (!ok)
		fprintf(stderr, "  %s %s (%s): %u items, %s; expected %u, %s\n",
			v->name, reader, how, g->n, bvn_error_to_string(err),
			v->nitems, v->error);
	return ok;
}

/* ---------------------------------------------------------------------------
 * Ways to cut a buffer: a list of piece ends.
 * ------------------------------------------------------------------------- */
typedef bool (*try_fn)(const vector_t* v, const uint8_t* data, size_t n,
	const size_t* cuts, size_t ncuts, const char* how);

static bool for_cuts(const vector_t* v, const uint8_t* data, size_t n,
	try_fn fn)
{
	size_t* cuts = xrealloc(NULL, (n + 2) * sizeof(*cuts));
	char how[64];
	bool ok = true;
	cuts[0] = n;
	ok &= fn(v, data, n, cuts, 1, "whole");
	for (size_t k = 0; k < n; k++)
		cuts[k] = k + 1;
	ok &= fn(v, data, n, cuts, n ? n : 1, "a byte at a time");
	/* Two pieces: at every position for a small vector; for a large one at
	 * every 4093rd and around every chunk boundary of the octet stream. */
	for (size_t k = 0; k <= n; k++) {
		if (n > EVERY_CUT_MAX && k % 4093u != 0 &&
		    (k % 65539u > 8u && k % 65539u < 65531u))
			continue;
		cuts[0] = k; cuts[1] = n;
		snprintf(how, sizeof(how), "cut at %zu", k);
		if (!fn(v, data, n, cuts, 2, how)) { ok = false; break; }
	}
	uint64_t seed = 0x9E3779B97F4A7C15u;
	for (int r = 0; r < 8; r++) {
		size_t nc = 0, at = 0, max = r & 1 ? 97 : 9000;
		while (at < n) {
			at += 1 + (size_t)(rng_next(&seed) % max);
			if (at > n) at = n;
			cuts[nc++] = at;
		}
		if (!nc) cuts[nc++] = n;
		snprintf(how, sizeof(how), "random %d", r);
		ok &= fn(v, data, n, cuts, nc, how);
	}
	free(cuts);
	return ok;
}

/* ---------------------------------------------------------------------------
 * Multiplexed vectors.
 * ------------------------------------------------------------------------- */
static bool on_msg(void* ud, uint64_t ch, const uint8_t* p, uint64_t n)
{
	got_add(ud, ch, p, n);
	return true;
}

/* bvnr_demux_on_event on bvnr_read of the whole buffer. */
static bool mux_batch(const vector_t* v, const uint8_t* data, size_t n,
	got_t* g)
{
	bvnr_demux_t* dm = bvnr_demux_create(on_msg, g, v->max_value);
	bvnr_demux_set_key(dm, "mux");
	bvnr_read_flags_t f;
	memset(&f, 0, sizeof(f));
	f.userdata    = dm;
	f.on_verified = bvnr_demux_on_event;
	bvnr_reader_t* r = bvnr_reader_create();
	bool ok = bvnr_open_read_mem(r, data, n, NULL, 0, &f) && bvnr_read(r);
	error_code_t err = bvnr_demux_error(dm);
	if (err == error_none && !ok) err = bvnr_reader_get_error(r);
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(dm);
	return same(v, g, err, "bvnr_read + bvnr_demux_on_event", "whole");
}

/* The push-fed demux, drained with bvnr_demux_next. */
static bool mux_fed(const vector_t* v, const uint8_t* data, size_t n,
	const size_t* cuts, size_t ncuts, const char* how)
{
	got_t g;
	memset(&g, 0, sizeof(g));
	g.digest = FNV_INIT;
	bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, v->max_value);
	bvnr_demux_set_key(dm, "mux");
	bool alive = bvnr_demux_open_feed(dm, NULL, false);
	bvnr_demux_item_t it;
	size_t at = 0;
	for (size_t i = 0; alive && i <= ncuts; i++) {
		if (i < ncuts) {
			alive = bvnr_demux_feed(dm, data + at, cuts[i] - at);
			at = cuts[i];
		} else {
			alive = bvnr_demux_finish(dm);
		}
		int rc;
		while ((rc = bvnr_demux_next(dm, &it)) == BVNR_NEXT_MESSAGE)
			got_add(&g, it.channel, it.data, it.length);
		if (rc == BVNR_NEXT_ERROR) alive = false;
	}
	(void)n;
	bool ok = same(v, &g, bvnr_demux_error(dm), "bvnr_demux_next", how);
	bvnr_demux_destroy(dm);
	got_free(&g);
	return ok;
}

/* The preamble of the canonical vectors, written as a producer writes it:
 * with the typed calls. (Replaying the events of the preamble text would not
 * do: a plain string reads back as a utf8-typed value, and is written again
 * with its <utf8> annotation.) The bytes are checked against the manifest's
 * .preamble, so this cannot drift from the vectors unnoticed. */
static bool write_preamble(bvnr_writer_t* w)
{
	return bvnr_write_struct_start(w, "meta") &&
	       bvnr_write_string(w, "codec", "av1") &&
	       bvnr_write_float_unit(w, "rate", 64, 29.97,
				     BVN_UNIT_NO_PREFIX(bu_hertz)) &&
	       bvnr_write_struct_end(w) &&
	       bvnr_write_string(w, "title", "caf\xc3\xa9");
}

static bool sink_push(bvnr_sink_t* s, const void* p, uint32_t n)
{
	buf_put(bvnr_sink_get_userdata(s), p, n);
	return true;
}

/* bvnr_mux_send of the messages after the preamble, into a callback sink,
 * with a bvnr_write_flush after each message or not. */
static bool mux_write(const vector_t* v, const got_t* g, bool flush, buf_t* out)
{
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, sink_push, NULL, out);
	bvnr_writer_t* w = bvnr_writer_create();
	bool ok = bvnr_open_write_sink(w, &sink, false, NULL);
	if (ok && v->preamble_len)
		ok = write_preamble(w);
	ok = ok && bvnr_mux_begin(w, "mux");
	const uint8_t* p = g->payloads.p;
	for (uint32_t i = 0; ok && i < g->n; i++) {
		ok = bvnr_mux_send(w, g->items[i].channel, p, g->items[i].length) &&
		     (!flush || bvnr_write_flush(w));
		p += g->items[i].length;
	}
	ok = ok && bvnr_mux_end(w) && bvnr_write_finish(w);
	bvnr_writer_destroy(w);
	CHECK(ok && out->n >= v->preamble_len &&
	      (v->preamble_len == 0 ||
	       memcmp(out->p, v->preamble, v->preamble_len) == 0) &&
	      memcmp(out->p + v->preamble_len, ".mux=", 5) == 0,
	      "%s: the C writer writes the manifest's preamble", v->name);
	return ok;
}

static void test_mux(const vector_t* v, const buf_t* data)
{
	got_t g;
	memset(&g, 0, sizeof(g));
	g.digest = FNV_INIT;
	g.keep = true;
	CHECK(mux_batch(v, data->p, data->n, &g), "%s: the batch demux", v->name);
	CHECK(for_cuts(v, data->p, data->n, mux_fed),
	      "%s: the fed demux, wherever cut", v->name);
	if (v->canonical) {
		for (int flush = 0; flush < 2; flush++) {
			buf_t out = {0};
			bool ok = mux_write(v, &g, flush != 0, &out);
			CHECK(ok && out.n == data->n &&
			      memcmp(out.p, data->p, out.n) == 0,
			      "%s: bvnr_mux_send%s writes the vector byte for "
			      "byte (%zu of %zu bytes)", v->name,
			      flush ? " with a flush per message" : "", out.n,
			      data->n);
			buf_free(&out);
		}
	}
	got_free(&g);
}

/* ---------------------------------------------------------------------------
 * Frame vectors.
 * ------------------------------------------------------------------------- */
static bool frames_pull(const vector_t* v, const uint8_t* data, size_t n,
	const size_t* cuts, size_t ncuts, const char* how)
{
	got_t g;
	memset(&g, 0, sizeof(g));
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create_pull(v->max_value);
	bool alive = true;
	size_t at = 0;
	for (size_t i = 0; alive && i < ncuts; i++) {
		alive = bvnr_frame_reader_feed(fr, data + at, cuts[i] - at);
		at = cuts[i];
		const uint8_t* doc;
		uint64_t len;
		int rc;
		while (alive &&
		       (rc = bvnr_frame_reader_next(fr, &doc, &len)) ==
		       BVNR_NEXT_DOCUMENT)
			got_add(&g, 0, doc, len);
		if (alive && rc == BVNR_NEXT_ERROR) alive = false;
	}
	if (alive) bvnr_frame_reader_finish(fr);
	(void)n;
	bool ok = same(v, &g, bvnr_frame_reader_error(fr), "frame reader", how);
	bvnr_frame_reader_destroy(fr);
	got_free(&g);
	return ok;
}

/* What a parsing reader reported of each document. */
typedef struct {
	uint32_t     n;
	bool         mismatch;
	const vector_t* v;
} docs_t;

static bool on_document(void* ud, uint64_t index, bool ok, error_code_t err)
{
	docs_t* d = ud;
	const char* want = index < d->v->nitems ? d->v->items[index].error : "?";
	if (index != d->n || ok != (strcmp(want, "none") == 0) ||
	    (!ok && strcmp(bvn_error_to_string(err), want) != 0))
		d->mismatch = true;
	d->n++;
	return true;
}

/* How a parsing reader ends: the stream's error going on past a failed
 * document; stopping at one, that document's error. Its count is of the
 * documents up to and including the one it stopped at. */
static const char* parse_outcome(const vector_t* v, bool go_on,
	uint32_t* count)
{
	for (uint32_t i = 0; !go_on && i < v->nitems; i++)
		if (strcmp(v->items[i].error, "none") != 0) {
			*count = i + 1;
			return v->items[i].error;
		}
	*count = v->nitems;
	return v->error;
}

static void parse_opts(const vector_t* v, bool go_on, docs_t* d,
	bvnr_read_flags_t* fl, bvnr_doc_stream_opts_t* o)
{
	memset(d, 0, sizeof(*d));
	d->v = v;
	memset(fl, 0, sizeof(*fl));
	memset(o, 0, sizeof(*o));
	o->flags = fl;
	o->userdata = d;
	o->on_document = on_document;
	o->continue_past_failed = go_on;
	o->max_document_size = v->max_value;
}

static bool parse_same(const vector_t* v, bool go_on, const docs_t* d,
	error_code_t err, const char* reader, const char* how)
{
	uint32_t count;
	const char* want = parse_outcome(v, go_on, &count);
	bool ok = !d->mismatch && d->n == count &&
		  strcmp(bvn_error_to_string(err), want) == 0;
	if (!ok)
		fprintf(stderr, "  %s %s%s (%s): %u documents%s, %s; expected "
			"%u, %s\n", v->name, reader,
			go_on ? " going on" : "", how, d->n,
			d->mismatch ? " (one reported wrong)" : "",
			bvn_error_to_string(err), count, want);
	return ok;
}

static bool frames_parse(const vector_t* v, bool go_on, const uint8_t* data,
	const size_t* cuts, size_t ncuts, const char* how)
{
	docs_t d;
	bvnr_read_flags_t fl;
	bvnr_doc_stream_opts_t o;
	parse_opts(v, go_on, &d, &fl, &o);
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create(&o);
	bool alive = true;
	size_t at = 0;
	for (size_t i = 0; alive && i < ncuts; i++) {
		alive = bvnr_frame_reader_feed(fr, data + at, cuts[i] - at);
		at = cuts[i];
	}
	if (alive) bvnr_frame_reader_finish(fr);
	bool ok = parse_same(v, go_on, &d, bvnr_frame_reader_error(fr),
			     "parsing frame reader", how);
	bvnr_frame_reader_destroy(fr);
	return ok;
}

static bool frames_parse_strict(const vector_t* v, const uint8_t* data,
	size_t n, const size_t* cuts, size_t ncuts, const char* how)
{
	(void)n;
	return frames_parse(v, false, data, cuts, ncuts, how);
}

static bool frames_parse_go_on(const vector_t* v, const uint8_t* data,
	size_t n, const size_t* cuts, size_t ncuts, const char* how)
{
	(void)n;
	return frames_parse(v, true, data, cuts, ncuts, how);
}

static void test_frames(const vector_t* v, const buf_t* data)
{
	CHECK(for_cuts(v, data->p, data->n, frames_pull),
	      "%s: the pull frame reader, wherever cut", v->name);
	CHECK(for_cuts(v, data->p, data->n, frames_parse_strict),
	      "%s: the parsing frame reader, wherever cut", v->name);
	CHECK(for_cuts(v, data->p, data->n, frames_parse_go_on),
	      "%s: the parsing frame reader past failed documents, wherever "
	      "cut", v->name);
	for (int go_on = 0; go_on < 2; go_on++) {
		docs_t d;
		bvnr_read_flags_t fl;
		bvnr_doc_stream_opts_t o;
		parse_opts(v, go_on != 0, &d, &fl, &o);
		bvnr_source_t src;
		bvnr_source_from_mem(&src, data->p, data->n);
		uint64_t count = 0;
		error_code_t err = error_none;
		bvnr_doc_stream_read_ex(&src, &o, &count, &err);
		CHECK(parse_same(v, go_on != 0, &d, err,
				 "bvnr_doc_stream_read_ex", "whole") &&
		      count == d.n,
		      "%s: bvnr_doc_stream_read_ex%s", v->name,
		      go_on ? " going on" : "");
	}
	if (strcmp(v->error, "none") != 0)
		return;
	/* bvnr_frame_write of the documents the frames hold. */
	buf_t out = {0};
	bvnr_sink_t sink;
	bvnr_sink_to_callback(&sink, sink_push, NULL, &out);
	bool ok = true;
	size_t at = 0;
	for (uint32_t i = 0; ok && i < v->nitems; i++) {
		at += BVNR_FRAME_HEADER_SIZE;
		ok = at + v->items[i].length <= data->n &&
		     bvnr_frame_write(&sink, data->p + at, v->items[i].length);
		at += (size_t)v->items[i].length;
	}
	/* An empty vector (frames_none) leaves out.p NULL: memcmp may not
	 * be handed one, even for no bytes. */
	CHECK(ok && out.n == data->n &&
	      (out.n == 0 || memcmp(out.p, data->p, out.n) == 0),
	      "%s: bvnr_frame_write writes the vector byte for byte", v->name);
	buf_free(&out);
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <tests/stream_vectors>\n", argv[0]);
		return 2;
	}
	char path[4096];
	snprintf(path, sizeof(path), "%s/manifest.bvnr", argv[1]);
	buf_t man = {0};
	if (!read_file(path, &man)) { perror(path); return 2; }
	bvn_dom_doc_t* doc = bvn_dom_parse(man.p, (uint32_t)man.n);
	if (!doc || bvn_dom_doc_get_parse_error(doc) != error_none) {
		fprintf(stderr, "%s does not parse\n", path);
		return 2;
	}
	const bvn_dom_entry_t* e = bvn_dom_doc_entries(doc);
	uint32_t count = bvn_dom_doc_count(doc);
	CHECK(count >= 25, "the manifest lists the vectors (%u)", count);
	for (uint32_t i = 0; i < count; i++) {
		vector_t v;
		load_vector(&e[i], &v);
		printf("%s\n", v.name);
		snprintf(path, sizeof(path), "%s/%s", argv[1], v.file);
		buf_t data = {0};
		CHECK(read_file(path, &data) && data.n == v.size,
		      "%s: %s holds %" PRIu64 " bytes", v.name, v.file, v.size);
		if (v.is_mux)
			test_mux(&v, &data);
		else
			test_frames(&v, &data);
		buf_free(&data);
		free(v.items);
	}
	bvn_dom_doc_destroy(doc);
	buf_free(&man);
	printf("%d checks, %d failed\n", g_tests, g_failures);
	return g_failures ? 1 : 0;
}
