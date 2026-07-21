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

#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_io_impl.h"
#include "bvn_val_impl.h"
bool bvn_ser_finish_stream(bvnr_serializer_t *s);
/*
 * ===========================================================================
 * Canonicalising observer
 * ===========================================================================
 *
 * A thin adapter that lets the serializer be driven directly by a reader's
 * event callback, without going through the full bvnr_writer_t (and without
 * re-validating — the events already came from a validating reader). Plugging
 * one of these in as a reader's on_verified callback re-emits every event to a
 * sink, producing a canonical (or pretty-printed) copy of the input: the
 * foundation of the pretty-print / canonicalise tooling. It is just a
 * bvnr_serializer_t wrapped in an opaque handle so the raw serializer type
 * stays private to the library.
 */
struct bvnr_canon_observer_s {
	bvnr_serializer_t ser;
	uint16_t          version_major;
	uint16_t          version_minor;
	bool              emit_version;  /* a directive is pending, emit it lazily
					  * before the first serialized event */
};
/*
 * max_array_nesting is set to the maximum because the event source (a reader)
 * has already enforced its own nesting limits; the observer should faithfully
 * reproduce whatever it is fed rather than impose a second, stricter cap.
 */
bvnr_canon_observer_t *bvnr_canon_observer_create(
	const bvnr_sink_t *sink, bool pretty)
{
	if (!sink || !bvn_sink_impl_c(sink)->push) return NULL;
	bvnr_canon_observer_t *obs = malloc(sizeof(*obs));
	if (!obs) return NULL;
	memset(obs, 0, sizeof(*obs));
	obs->ser.sink              = *sink;
	obs->ser.pretty            = pretty;
	obs->ser.max_array_nesting = UINT8_MAX;
	return obs;
}
void bvnr_canon_observer_destroy(bvnr_canon_observer_t *obs)
{
	free(obs);
}
/*
 * Record a "#!bovnar <major>.<minor>" directive to prepend to the canonical
 * output. The directive is emitted lazily, just before the first event is
 * serialized — a caller driving the observer from a reader only learns the
 * source's declared version after the directive line is parsed (mid-stream),
 * which is still ahead of the first value event, so it can inject it here. A
 * version of 0.0, or a call after output has begun, is ignored. Without this
 * the observer reproduces only the value stream, so a spec-1.1 document (one
 * using datetime, the new escapes, …) would canonicalise to output a reader
 * then rejects as 1.0.
 */
void bvnr_canon_observer_set_version(bvnr_canon_observer_t *obs,
	uint16_t major, uint16_t minor)
{
	if (!obs) return;
	if (obs->ser.stream_begun || obs->ser.version_emitted) return;
	if (major == 0u && minor == 0u) return;
	obs->version_major = major;
	obs->version_minor = minor;
	obs->emit_version  = true;
}
bool bvnr_canon_observer_on_event(void *ud,
	bvnr_event_t ev, bvnr_data_t *data)
{
	bvnr_canon_observer_t *obs = (bvnr_canon_observer_t *)ud;
	if (!obs) return true;
	if (obs->emit_version && !obs->ser.version_emitted) {
		if (!bvn_ser_emit_version(&obs->ser, obs->version_major,
				obs->version_minor))
			return false;
		obs->emit_version = false;
	}
	/* set_version guards on ser.stream_begun so a late call cannot inject the
	 * directive mid-document — but only bvn_writer_validate_event sets that flag,
	 * and the observer drives bvn_ser_serialize_event directly, so it stayed
	 * false forever and the guard was dead code.
	 *
	 * Mark it on actual OUTPUT, not on the first event: a caller driving this
	 * from a reader learns the declared version only after the directive line is
	 * parsed, which is mid-stream but still ahead of the first value, and
	 * ev_stream_start emits nothing. Comparing the byte count across the call is
	 * what distinguishes "an event happened" from "bytes reached the document". */
	uint64_t before = bvnr_sink_bytes_written(&obs->ser.sink) +
			  (uint64_t)obs->ser.wbuf_pos;
	bool ok = bvn_ser_serialize_event(&obs->ser, ev, data);
	uint64_t after = bvnr_sink_bytes_written(&obs->ser.sink) +
			 (uint64_t)obs->ser.wbuf_pos;
	if (after != before)
		obs->ser.stream_begun = true;
	return ok;
}
bool bvnr_canon_observer_finish(bvnr_canon_observer_t *obs)
{
	if (!obs) return true;
	return bvn_ser_finish_stream(&obs->ser);
}
