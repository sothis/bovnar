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
bool bvnr_canon_observer_on_event(void *ud,
	bvnr_event_t ev, bvnr_data_t *data)
{
	bvnr_canon_observer_t *obs = (bvnr_canon_observer_t *)ud;
	if (!obs) return true;
	return bvn_ser_serialize_event(&obs->ser, ev, data);
}
bool bvnr_canon_observer_finish(bvnr_canon_observer_t *obs)
{
	if (!obs) return true;
	return bvn_ser_finish_stream(&obs->ser);
}
