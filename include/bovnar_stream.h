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
 * bovnar_stream — streaming/framing applications layered on the lexer/validator
 * event API.
 *
 * The bovnar core is a SAX-style event pipe: the reader turns bytes into a
 * bvnr_event_t stream and the writer turns events back into bytes. The DOM is
 * just one consumer of that pipe. This header adds three more consumers/producers
 * that sit at the same seam — *beside* the DOM, not inside the grammar — plus
 * the policy knob for unbounded streams:
 *
 *   1. Endless streaming      — BVNR_FILESIZE_UNLIMITED (declared in bovnar.h;
 *                               now 0, the zero-init default): uncaps the byte
 *                               counter so a single document or octet stream can
 *                               run to 2^64-1 bytes with no accumulated limit.
 *   2. Multi-document framing — a length-prefixed record codec carrying a
 *                               sequence of independent documents over one
 *                               transport (file/socket/pipe).
 *   3. Octet multiplexing     — many logical channels interleaved *inside one
 *                               octet-stream value*, with the channel id carried
 *                               out-of-band as a varint prefix on each chunk.
 *                               The 1.0 wire format is untouched.
 *   4. Document-in-document   — a complete document embedded as the octet-stream
 *                               payload of a key, then recursively parsed back.
 *
 * Everything here builds only on the public reader/writer event API, so it adds
 * no new grammar and no change to the frozen 1.0 wire format.
 */

#ifndef BOVNAR_STREAM_H_
#define BOVNAR_STREAM_H_
#include <stdbool.h>
#include <stdint.h>
#include "bovnar.h"
#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * 2. Multi-document record framing
 * ===========================================================================
 *
 * A transport framing that carries a sequence of independent bovnar documents
 * over one byte stream. Each record is:
 *
 *     "BVF1" (4-byte magic)   payload_len (uint64, little-endian)   payload
 *
 * The framing lives beside the lexer: each payload is a complete, standalone
 * bovnar document parsed by an ordinary reader. A clean EOF *on a frame
 * boundary* (before any header byte) ends the sequence successfully; an EOF
 * part-way through a header or payload is a truncation error. The grammar has
 * no in-band document separator, which is exactly why this length-prefixed
 * frame is the right place to delimit documents on a long-lived connection.
 */
#define BVNR_FRAME_MAGIC0       'B'
#define BVNR_FRAME_MAGIC1       'V'
#define BVNR_FRAME_MAGIC2       'F'
#define BVNR_FRAME_MAGIC3       '1'
#define BVNR_FRAME_HEADER_SIZE  12u   /* 4 magic + 8 length */

/* Default per-document size guard used when bvnr_doc_stream_opts_t.max_document_size
 * is 0. Unlike max_file_size (where 0 means unlimited), this framing guard keeps a
 * finite default because each frame payload is fully buffered before parsing, so a
 * hostile frame header could otherwise force a huge allocation. To lift it, set an
 * explicit huge cap (e.g. UINT64_MAX), which a 64-bit frame length can never exceed. */
#define BVNR_DOC_DEFAULT_MAX    (256u * 1024u * 1024u)

/*
 * Write one framed document: emits the 12-byte header then the document bytes.
 * `doc` is the already-serialised bytes of a complete document (e.g. produced
 * by a bvnr_writer into a memory sink). Returns false on sink write failure.
 */
BVN_API bool bvnr_frame_write(
	bvnr_sink_t* sink, const void* doc, uint64_t doc_len);

typedef struct bvnr_doc_stream_opts_s {
	/* Per-document reader options (callbacks + limits). The same flags struct
	 * is reused for every document in the sequence. May be NULL.
	 *
	 * Note flags->continue_on_error controls *intra*-document resync (whether a
	 * single document recovers from its own errors) — it is orthogonal to
	 * continue_past_failed below, which controls *inter*-document behaviour. */
	bvnr_read_flags_t*	flags;
	void*			userdata;
	/* Invoked after each document is parsed (may be NULL). `ok` is the parse
	 * result and `err` the reader's error code. Return false to abort the
	 * sequence early. */
	bool			(*on_document)(void* userdata, uint64_t index,
					bool ok, error_code_t err);
	/* Inter-document resilience. Because each document occupies its own
	 * length-delimited frame, a parse failure in one need not stop the rest.
	 *   false (default): a failed document aborts the sequence (strict).
	 *   true:            a failed document is reported via on_document and the
	 *                    reader advances to the next frame; the overall call
	 *                    still succeeds (only framing/IO errors abort).
	 * This is independent of flags->continue_on_error: you can parse each
	 * document strictly (no intra-document resync) yet still process every
	 * frame. */
	bool			continue_past_failed;
	/* Reject any frame whose payload length exceeds this. 0 selects
	 * BVNR_DOC_DEFAULT_MAX (the framing guard is always finite by default);
	 * set an explicit huge cap (e.g. UINT64_MAX) to effectively disable it. */
	uint64_t		max_document_size;
} bvnr_doc_stream_opts_t;

/*
 * Read a sequence of framed documents from `src` until a clean EOF on a frame
 * boundary. Each payload is parsed with a fresh reader using opts->flags.
 * Returns false on a framing/IO error, or on the first per-document parse error
 * unless opts->continue_past_failed is set (in which case failed documents are
 * reported via on_document and the sequence continues). *out_count (optional)
 * receives the number of documents seen.
 */
BVN_API bool bvnr_doc_stream_read(
	bvnr_source_t* src, const bvnr_doc_stream_opts_t* opts,
	uint64_t* out_count);

/*
 * ===========================================================================
 * 3. Octet multiplexing (out-of-band channel convention, cross-chunk)
 * ===========================================================================
 *
 * A multiplexed transport carried inside a *single* octet-stream value. The
 * wire convention has two layers:
 *
 *   Per octet chunk:   varint(channel)  fragment_bytes
 *   Per channel:       the concatenation of that channel's fragments (in order)
 *                      forms its logical byte stream, which is a sequence of
 *                      length-prefixed messages:  varint(msg_len)  msg_bytes
 *
 * To the lexer these are ordinary octet chunks (tag 0x01); the channel id and
 * message framing are a convention interpreted above ev_data, so the frozen 1.0
 * wire format is untouched and bvnr_data_t needs no channel field.
 *
 * Because a message is length-prefixed it may span any number of octet chunks,
 * and channels may interleave freely at chunk granularity — the demultiplexer
 * reassembles each channel independently. Messages of any size up to a caller-
 * set cap are supported; a message that fits BVNR_MUX_MAX_MESSAGE travels in a
 * single chunk (the fast path). The only framing constraint a producer must
 * respect (and bvnr_mux_send does): a message's length varint is never split
 * across two chunks.
 */
#define BVNR_MUX_MAX_MESSAGE   (65536u - 20u)  /* fits one chunk: channel + len varints + data */

/* Default reassembly cap for bvnr_demux_create's max_message=0. */
#define BVNR_MUX_DEFAULT_MAX   (64u * 1024u * 1024u)

/* Open an octet stream under `key` to carry multiplexed channels. */
BVN_API bool bvnr_mux_begin(bvnr_writer_t* w, const char* key);
/* Send one message on `channel`, split across as many octet chunks as needed. */
BVN_API bool bvnr_mux_send(bvnr_writer_t* w, uint64_t channel,
	const void* data, uint64_t len);
/* Close the multiplexed octet stream. */
BVN_API bool bvnr_mux_end(bvnr_writer_t* w);

typedef bool (*bvnr_mux_on_msg_fn)(void* userdata, uint64_t channel,
	const uint8_t* data, uint64_t len);

/* Opaque demultiplexer: owns per-channel reassembly buffers. */
typedef struct bvnr_demux_s bvnr_demux_t;

/* Create a demultiplexer. on_message is invoked with each fully reassembled
 * message. max_message bounds a single reassembled message (0 selects
 * BVNR_MUX_DEFAULT_MAX); a larger declared length is rejected as a desync. */
BVN_API bvnr_demux_t* bvnr_demux_create(bvnr_mux_on_msg_fn on_message,
	void* userdata, uint64_t max_message);
BVN_API void bvnr_demux_destroy(bvnr_demux_t* dm);

/*
 * Event callback that demultiplexes channels. Install it as
 * bvnr_read_flags_t.on_verified with userdata = the bvnr_demux_t*. It peels the
 * varint channel off each octet chunk, reassembles per-channel messages and
 * routes each complete message to on_message. Returns false (aborting the read)
 * if a handler returns false or the stream desyncs (malformed varint,
 * oversized message, allocation failure).
 */
BVN_API bool bvnr_demux_on_event(void* demux, bvnr_event_t ev, bvnr_data_t* d);

/* Last demux error (error_none if clean). error_octet_stream_out_of_sync marks
 * a framing desync; error_value_out_of_range an oversized message; other codes
 * map to the failure observed. */
BVN_API error_code_t bvnr_demux_error(const bvnr_demux_t* dm);

/*
 * ===========================================================================
 * 4. Document-in-document
 * ===========================================================================
 *
 * Embed a complete bovnar document as the octet-stream payload of a key, and
 * recursively parse it back. The outer parser treats the inner document as
 * opaque octets; only an application that knows the key holds a nested document
 * re-feeds those bytes to a sub-reader.
 */

/* Emit `.<key> = <octets>;` where the octet payload is `doc`, split into
 * 65536-byte chunks. */
BVN_API bool bvnr_embed_document(bvnr_writer_t* w, const char* key,
	const void* doc, uint64_t doc_len);

/* Parse an embedded document's bytes with an ordinary reader (a thin wrapper
 * over bvnr_open_read_mem + bvnr_read). `flags` carries the nested callbacks. */
BVN_API bool bvnr_parse_embedded(const void* doc, uint64_t doc_len,
	bvnr_read_flags_t* flags);

#ifdef __cplusplus
}
#endif
#endif
