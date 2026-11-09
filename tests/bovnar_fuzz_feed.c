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
 * Coverage-guided fuzz target for the push-fed readers (libFuzzer or AFL++,
 * built with BVNR_FUZZ_EXTERNAL; see CMakeLists_tests.txt).
 *
 * Input: [config][cut seed][payload]. The payload is read whole with
 * bvnr_read and fed in pieces derived from the cut seed to bvnr_read_feed;
 * the two must report the same events (hashed), result, error and position.
 * Then the payload is fed as a frame stream to both kinds of frame reader and
 * as a mux document to a fed demux in callback and pull mode, which must agree
 * on the messages with bvnr_demux_on_event on a reader of the whole buffer.
 * Any disagreement traps.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_stream.h"

typedef struct {
	uint64_t h;
	uint32_t verified;
	uint32_t abort_after;
} fh_t;

static void mix(fh_t *f, const void *p, size_t n)
{
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++) {
		f->h ^= b[i];
		f->h *= UINT64_C(0x100000001b3);
	}
}

static void mix_u64(fh_t *f, uint64_t v) { mix(f, &v, sizeof(v)); }

static void mix_event(fh_t *f, uint8_t tag, bvnr_event_t ev,
		      const bvnr_data_t *d)
{
	mix(f, &tag, 1);
	mix_u64(f, (uint64_t)ev);
	if (!d) return;
	mix_u64(f, (uint64_t)d->type);
	mix_u64(f, (uint64_t)d->value_type.family);
	mix_u64(f, d->value_type.width);
	mix_u64(f, d->length);
	if (d->data && d->length) mix(f, d->data, d->length);
}

static bool on_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	fh_t *f = ud;
	mix_event(f, 'v', ev, d);
	return !(f->abort_after && ++f->verified >= f->abort_after);
}

static bool on_unverified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	mix_event(ud, 'u', ev, d);
	return true;
}

static void on_error(void *ud, error_code_t err, uint64_t line, uint64_t col,
		     uint32_t byte, uint64_t off)
{
	fh_t *f = ud;
	mix_u64(f, (uint64_t)err);
	mix_u64(f, line);
	mix_u64(f, col);
	mix_u64(f, byte);
	mix_u64(f, off);
}

static bool on_msg(void *ud, uint64_t ch, const uint8_t *p, uint64_t n)
{
	fh_t *f = ud;
	mix_u64(f, ch);
	mix_u64(f, n);
	if (n) mix(f, p, (size_t)n);
	return true;
}

static void flags_for(bvnr_read_flags_t *fl, uint8_t cfg, fh_t *f,
		      uint8_t seed, size_t len)
{
	memset(fl, 0, sizeof(*fl));
	if (cfg & 1u) { fl->max_string_length = 8; fl->max_text_bytes = 64; }
	if (cfg & 2u) fl->max_file_size = len > 2 ? len / 2 : 1;
	fl->continue_on_error = (cfg >> 2) & 1u;
	fl->strict_version    = (cfg >> 4) & 1u;
	fl->userdata      = f;
	fl->on_verified   = on_verified;
	fl->on_unverified = on_unverified;
	fl->on_error      = on_error;
	f->h = UINT64_C(0xcbf29ce484222325);
	f->verified = 0;
	f->abort_after = (cfg & 8u) ? (uint32_t)seed + 1u : 0u;
}

/* Piece ends: pieces of 1..(seed % 61 + 1) bytes, varied by position. */
static size_t make_cuts(size_t len, uint8_t seed, size_t *cuts)
{
	size_t n = 0, at = 0, step = (size_t)(seed % 61u) + 1u;
	uint32_t x = (uint32_t)seed * 2654435761u + 1u;
	while (at < len) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		at += 1u + x % step;
		if (at > len) at = len;
		cuts[n++] = at;
	}
	if (!n) cuts[n++] = 0;
	return n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 2)
		return 0;
	uint8_t cfg = data[0], seed = data[1];
	const uint8_t *p = data + 2;
	size_t len = size - 2;
	size_t *cuts = malloc((len + 1) * sizeof(*cuts));
	if (!cuts) return 0;
	size_t ncuts = make_cuts(len, seed, cuts);

	/* The document, whole and fed. */
	fh_t a, b;
	bvnr_read_flags_t fl;
	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) { free(cuts); return 0; }
	flags_for(&fl, cfg, &a, seed, len);
	bool ok_a = bvnr_open_read_mem(r, p, len, NULL, 0, &fl) && bvnr_read(r);
	error_code_t e_a = bvnr_reader_get_error(r);
	uint64_t o_a = bvnr_reader_get_error_offset(r);
	uint64_t c_a = bvnr_reader_get_error_column(r);
	flags_for(&fl, cfg, &b, seed, len);
	bool ok_b = bvnr_open_read_feed(r, NULL, &fl);
	size_t at = 0;
	for (size_t i = 0; ok_b && i < ncuts; i++) {
		ok_b = bvnr_read_feed(r, p + at, cuts[i] - at);
		at = cuts[i];
	}
	ok_b = ok_b && bvnr_read_finish(r);
	if (ok_a != ok_b || a.h != b.h || e_a != bvnr_reader_get_error(r) ||
	    o_a != bvnr_reader_get_error_offset(r) ||
	    c_a != bvnr_reader_get_error_column(r))
		__builtin_trap();
	bvnr_reader_destroy(r);

	/* A frame stream: parse mode agrees with bvnr_doc_stream_read; pull
	 * mode must not crash and hands out readable documents. */
	bvnr_doc_stream_opts_t o;
	memset(&o, 0, sizeof(o));
	o.continue_past_failed = cfg & 1u;
	o.max_document_size = (cfg & 2u) ? 64u : 0u;
	bvnr_source_t src;
	bvnr_source_from_mem(&src, p, len);
	uint64_t count = 0;
	error_code_t ref_err = error_none;
	bool ref_ok = bvnr_doc_stream_read_ex(&src, &o, &count, &ref_err);
	bvnr_frame_reader_t *fr = bvnr_frame_reader_create(&o);
	bool ok = fr != NULL;
	for (size_t i = 0, from = 0; ok && i < ncuts; from = cuts[i++])
		ok = bvnr_frame_reader_feed(fr, p + from, cuts[i] - from);
	ok = ok && bvnr_frame_reader_finish(fr);
	if (fr && (ok != ref_ok || bvnr_frame_reader_count(fr) != count ||
		   bvnr_frame_reader_error(fr) != ref_err))
		__builtin_trap();
	bvnr_frame_reader_destroy(fr);
	fr = bvnr_frame_reader_create_pull(o.max_document_size);
	ok = fr != NULL;
	for (size_t i = 0, from = 0; ok && i < ncuts; from = cuts[i++]) {
		const uint8_t *doc;
		uint64_t dl;
		ok = bvnr_frame_reader_feed(fr, p + from, cuts[i] - from);
		while (ok && bvnr_frame_reader_next(fr, &doc, &dl) ==
		       BVNR_NEXT_DOCUMENT)
			if (dl) (void)(doc[0] + doc[dl - 1]);
	}
	if (fr) (void)bvnr_frame_reader_finish(fr);
	bvnr_frame_reader_destroy(fr);

	/* A mux document. */
	uint64_t maxm = (cfg & 2u) ? 16u : 0u;
	fh_t ref = { UINT64_C(0xcbf29ce484222325), 0, 0 };
	bvnr_demux_t *dm = bvnr_demux_create(on_msg, &ref, maxm);
	if (dm) {
		if (cfg & 1u) bvnr_demux_set_key(dm, "x");
		memset(&fl, 0, sizeof(fl));
		fl.userdata = dm;
		fl.on_verified = bvnr_demux_on_event;
		r = bvnr_reader_create();
		ref_ok = r && bvnr_open_read_mem(r, p, len, NULL, 0, &fl) &&
			 bvnr_read(r);
		bvnr_reader_destroy(r);
		bvnr_demux_destroy(dm);
	}
	for (int pull = 0; dm && pull < 2; pull++) {
		fh_t got = { UINT64_C(0xcbf29ce484222325), 0, 0 };
		bvnr_demux_t *fd = bvnr_demux_create(pull ? NULL : on_msg, &got,
						     maxm);
		if (!fd) break;
		if (cfg & 1u) bvnr_demux_set_key(fd, "x");
		ok = bvnr_demux_open_feed(fd, NULL, (cfg & 4u) != 0);
		bvnr_demux_item_t it;
		for (size_t i = 0, from = 0; ok && i <= ncuts; i++) {
			int rc;
			if (i < ncuts) {
				ok = bvnr_demux_feed(fd, p + from, cuts[i] - from);
				from = cuts[i];
			} else {
				ok = bvnr_demux_finish(fd);
			}
			if (!pull) continue;
			while ((rc = bvnr_demux_next(fd, &it)) > 0)
				if (rc == BVNR_NEXT_MESSAGE)
					on_msg(&got, it.channel, it.data, it.length);
			if (rc == BVNR_NEXT_ERROR) ok = false;
		}
		if (ok != ref_ok || got.h != ref.h)
			__builtin_trap();
		bvnr_demux_destroy(fd);
	}
	free(cuts);
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

int main(int argc, char **argv)
{
	int ret = 0;
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) { perror(argv[i]); ret = 1; continue; }
		uint8_t *buf = malloc(1u << 20);
		size_t n = buf ? fread(buf, 1, 1u << 20, f) : 0;
		fclose(f);
		if (buf) LLVMFuzzerTestOneInput(buf, n);
		free(buf);
	}
	return ret;
}

#endif
