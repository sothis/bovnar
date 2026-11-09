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
	/* Invoked after each document is parsed (may be NULL). `index` is the
	 * document's 0-based position, `ok` the parse result and `err` the reader's
	 * error code. Return false to abort the sequence early; a document the
	 * callback vetoes is still counted in *out_count (it was seen), matching the
	 * parse-failure abort path. */
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
 * bvnr_doc_stream_read, saying why it returned false: *out_error (optional)
 * receives the code the push-fed frame reader (bvnr_frame_reader_error) gives
 * for the same input, error_none on success:
 *
 *   error_unexpected_input_byte       a header without the "BVF1" magic
 *   error_value_out_of_range          a declared length over the cap
 *   error_got_incomplete_bvnr_stream  input ending inside a header or payload
 *   error_reading_from_source_fd      the source's pull failed
 *   error_scanner_callback_failed     on_document returned false
 *   the document's own code           a failed document, without
 *                                     continue_past_failed
 *   error_invalid_argument            a NULL or pull-less source, or no memory
 */
BVN_API bool bvnr_doc_stream_read_ex(
	bvnr_source_t* src, const bvnr_doc_stream_opts_t* opts,
	uint64_t* out_count, error_code_t* out_error);

/*
 * Pull-style calls (bvnr_frame_reader_next, bvnr_demux_next) return one of
 * these. They exist so that a caller who pays for every callback -- a binding
 * calling back through an FFI -- can drain everything one feed made available
 * in a single loop of plain calls.
 */
#define BVNR_NEXT_ERROR      (-1)  /* the stream failed; see the *_error getter
                                    * (a NULL argument, or a call on a reader or
                                    * demux of the other mode, returns it too and
                                    * leaves that getter as it was) */
#define BVNR_NEXT_NONE       0     /* nothing more until more input is fed */
#define BVNR_NEXT_DOCUMENT   1     /* bvnr_frame_reader_next: a document */
#define BVNR_NEXT_MESSAGE    1     /* bvnr_demux_next: a multiplexed message */
#define BVNR_NEXT_EVENT      2     /* bvnr_demux_next: a document event */

/*
 * Push-fed frame reading: the twin of bvnr_doc_stream_read for input that is
 * handed over in pieces (bvnr_frame_reader_feed) rather than pulled, so an
 * endless frame stream on a non-blocking socket costs one reader for its whole
 * life. Two kinds, chosen at creation:
 *
 * bvnr_frame_reader_create(opts) PARSES every document, exactly as
 * bvnr_doc_stream_read does with the same opts: the same events through
 * opts->flags, the same on_document calls, the same continue_past_failed and
 * max_document_size policy (opts may be NULL: validate only). The payload is
 * streamed into one reused push-fed reader (bvnr_open_read_feed) as it arrives,
 * so a frame is never buffered whole, however large. One consequence: the
 * events of a document whose frame the stream cuts off have been delivered up
 * to the cut (without its ev_stream_end or an on_document call), where
 * bvnr_doc_stream_read, which reads a frame whole first, delivers none. opts
 * and *opts->flags are copied.
 *
 * bvnr_frame_reader_create_pull(max_document_size) hands documents out instead,
 * as bytes, through bvnr_frame_reader_next; the caller parses them as it likes.
 * A document whose frame lies whole in the fed buffer is handed out from that
 * buffer without a copy; one split across feeds is gathered into a buffer the
 * reader owns and reuses (it grows with the bytes that arrive, never to a
 * declared length up front). Either pointer is valid until the next call on the
 * reader. max_document_size 0 selects BVNR_DOC_DEFAULT_MAX, as for
 * bvnr_doc_stream_opts_t.
 *
 * bvnr_frame_reader_feed: in parse mode, every frame the bytes complete is
 * parsed and reported before it returns, and the buffer may be reused then. In
 * pull mode the bytes are only referenced: they must stay valid until
 * bvnr_frame_reader_next has returned BVNR_NEXT_NONE, and feeding again before
 * that returns false and changes nothing. Returns false once the stream has
 * failed: a bad magic (error_unexpected_input_byte), a declared length over the
 * cap (error_value_out_of_range), on_document returning false
 * (error_scanner_callback_failed), or in parse mode a failed document without
 * continue_past_failed (that document's error code). Running out of input
 * mid-frame is never a feed error.
 *
 * bvnr_frame_reader_next (pull mode only, BVNR_NEXT_ERROR otherwise) returns
 * BVNR_NEXT_DOCUMENT with the next document in *doc and *len, BVNR_NEXT_NONE when
 * the fed bytes are used up, or BVNR_NEXT_ERROR.
 *
 * bvnr_frame_reader_finish declares end of input: true on a frame boundary,
 * false with error_got_incomplete_bvnr_stream inside a header or a payload (a
 * cut-off document is not reported to on_document, as with
 * bvnr_doc_stream_read, though in parse mode its events up to the cut have
 * been delivered), false if the stream had already failed. In pull mode
 * it refuses, changing nothing, while fed bytes are left undrained.
 *
 * bvnr_frame_reader_count is the number of documents seen (parsed or handed
 * out), bvnr_frame_reader_error the error that ended the stream (error_none
 * while it has not).
 */
typedef struct bvnr_frame_reader_s bvnr_frame_reader_t;
BVN_API bvnr_frame_reader_t* bvnr_frame_reader_create(
	const bvnr_doc_stream_opts_t* opts);
BVN_API bvnr_frame_reader_t* bvnr_frame_reader_create_pull(
	uint64_t max_document_size);
BVN_API void bvnr_frame_reader_destroy(bvnr_frame_reader_t* fr);
BVN_API bool bvnr_frame_reader_feed(bvnr_frame_reader_t* fr,
	const void* buf, uint64_t len);
BVN_API int  bvnr_frame_reader_next(bvnr_frame_reader_t* fr,
	const uint8_t** doc, uint64_t* len);
BVN_API bool bvnr_frame_reader_finish(bvnr_frame_reader_t* fr);
BVN_API error_code_t bvnr_frame_reader_error(const bvnr_frame_reader_t* fr);
BVN_API uint64_t bvnr_frame_reader_count(const bvnr_frame_reader_t* fr);

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
 * reassembles each channel independently. Both the per-message data *and* its
 * length varint may straddle chunk boundaries: the demultiplexer accumulates a
 * partial length varint per channel across chunks. The one wire requirement a
 * producer must respect is that the per-chunk *channel* varint — the routing
 * prefix the demultiplexer needs to attribute the chunk — is whole within its
 * chunk (bvnr_mux_send always satisfies this; the channel id is <=10 bytes and
 * leads every chunk). Messages of any size up to a caller-set cap are supported.
 *
 * BVNR_MUX_MAX_MESSAGE is a conservative single-chunk *guarantee*, not an
 * enforced cap: any message of at most this many bytes is emitted by
 * bvnr_mux_send in one octet chunk regardless of the channel id or length
 * magnitude (the bound subtracts the maximum 10+10 varint bytes from the 65536
 * chunk size). Larger messages are simply split across chunks; nothing rejects
 * them.
 */
#define BVNR_MUX_MAX_MESSAGE   (65536u - 20u)  /* <=this fits one chunk: channel + len varints + data */

/* Default reassembly cap for bvnr_demux_create's max_message=0. */
#define BVNR_MUX_DEFAULT_MAX   (64u * 1024u * 1024u)

/* Open an octet stream under `key` to carry multiplexed channels. The stream is
 * one value of an ordinary document: whatever the writer wrote before it -- a
 * `.meta = { ... };` describing the stream, say -- is read back as ordinary
 * values ahead of the first message, and the document may go on after
 * bvnr_mux_end. */
BVN_API bool bvnr_mux_begin(bvnr_writer_t* w, const char* key);
/* Send one message on `channel`, split across as many octet chunks as needed.
 * The chunk is staged in a buffer the writer allocates on the first send and
 * keeps, so a send allocates nothing. For an fd or callback sink the message
 * then waits in the writer's 64 KiB buffer; call bvnr_write_flush after the
 * send to hand it to a live consumer at once. */
BVN_API bool bvnr_mux_send(bvnr_writer_t* w, uint64_t channel,
	const void* data, uint64_t len);
/* Close the multiplexed octet stream. */
BVN_API bool bvnr_mux_end(bvnr_writer_t* w);

typedef bool (*bvnr_mux_on_msg_fn)(void* userdata, uint64_t channel,
	const uint8_t* data, uint64_t len);

/* Opaque demultiplexer: owns per-channel reassembly buffers. */
typedef struct bvnr_demux_s bvnr_demux_t;

/* Create a demultiplexer. on_message is invoked with each fully reassembled
 * message; `data` is valid for the duration of the call only (a message that
 * lies whole in one octet chunk is passed straight from the chunk, without a
 * copy). It may be NULL: a push-fed demux is then drained with bvnr_demux_next,
 * and one installed on a reader drops the messages. max_message bounds a single
 * reassembled message (0 selects
 * BVNR_MUX_DEFAULT_MAX); a larger declared length is refused with
 * error_value_out_of_range.
 *
 * The demultiplexer owns one reassembly buffer per channel it has seen; a
 * message that lies whole in one chunk never touches it, so each buffer grows
 * to that channel's largest message spanning chunks and is retained for reuse until
 * bvnr_demux_destroy, so steady-state memory is the sum of per-channel high-water
 * marks (bounded by 4096 channels). Channels are not reclaimed individually.
 *
 * That 4096 is a HARD limit, not only a memory estimate: a chunk naming a
 * 4097th distinct channel cannot be attributed, so the read is aborted and
 * bvnr_demux_error latches error_octet_stream_out_of_sync -- the demux has lost
 * track of the stream, even though the wire framing was well formed. Since
 * channels are never reclaimed, the count is of DISTINCT channel ids seen over
 * the whole stream, not of channels live at any one moment. A push-fed demux
 * opened again for the next document (bvnr_demux_open_feed) starts a new
 * stream, whose ids count afresh. */
BVN_API bvnr_demux_t* bvnr_demux_create(bvnr_mux_on_msg_fn on_message,
	void* userdata, uint64_t max_message);
BVN_API void bvnr_demux_destroy(bvnr_demux_t* dm);

/*
 * Restrict the demultiplexer to octet streams opened under `key`. By default a
 * demux installed on on_verified treats *every* octet stream in the document as
 * multiplexed; set a key so a document can mix one mux stream with ordinary
 * binary octet payloads (those under other keys are ignored). Pass key == NULL
 * to clear the filter and restore the demux-every-stream default. The key string
 * is copied. Returns false on a NULL demux or an allocation failure. */
BVN_API bool bvnr_demux_set_key(bvnr_demux_t* dm, const char* key);

/*
 * Event callback that demultiplexes channels. Install it as
 * bvnr_read_flags_t.on_verified with userdata = the bvnr_demux_t*. It peels the
 * varint channel off each octet chunk, reassembles per-channel messages and
 * routes each complete message to on_message. Returns false (aborting the read)
 * if a handler returns false or the stream desyncs (malformed varint,
 * oversized message, allocation failure). When on_message itself returns false,
 * the read is aborted and bvnr_demux_error reports error_scanner_callback_failed
 * (the library's standard "a callback asked to stop" code) — distinct from the
 * desync code, so a caller can tell a deliberate stop from a framing error.
 */
BVN_API bool bvnr_demux_on_event(void* demux, bvnr_event_t ev, bvnr_data_t* d);

/* Last demux error (error_none if clean). error_octet_stream_out_of_sync marks
 * a framing desync; error_value_out_of_range an oversized message; other codes
 * map to the failure observed. On a push-fed demux, when the demux itself has
 * none, it is the error of its reader -- the document around the mux stream
 * was malformed, cut off, or refused by a limit. The demux's own error comes
 * first, as for a demux installed on a reader: a message cut short
 * (error_octet_stream_truncated, which does not fail the read) is still the
 * answer when the document fails later on, so when the feed or finish has
 * returned false, bvnr_reader_get_error(bvnr_demux_get_reader(dm)) says why. */
BVN_API error_code_t bvnr_demux_error(const bvnr_demux_t* dm);

/*
 * Push-fed demultiplexing: the demux owns a push-fed reader (bvnr_open_read_feed)
 * and is handed the bytes of the DOCUMENT that carries the mux stream, in
 * pieces of any size. The key scope, max_message, the 4096-channel limit and
 * every error code are those of bvnr_demux_on_event; the result is the same as
 * installing it on a reader of the whole buffer.
 *
 * bvnr_demux_open_feed starts a document (call it again for the next one: the
 * channel buffers are kept; the channel ids, and with them the 4096-channel
 * count, the error and all progress are reset).
 * `flags` is copied: its limits and callbacks apply to the document, and its
 * on_verified, if any, sees EVERY event -- the values before and after the mux
 * stream as well as the stream's chunks -- before the demux does, so a stream
 * can be preceded by ordinary values describing it:
 *
 *     .meta = { .codec = "av1"; .rate = <float:32,Hz> 30.0; };
 *     .media = <octet stream carrying the channels>;
 *
 * Its userdata is passed to its own callbacks, as usual.
 *
 * With an on_message callback (bvnr_demux_create), bvnr_demux_feed delivers
 * every message the bytes complete before it returns, and the buffer may be
 * reused then. Without one, bvnr_demux_feed only references the bytes, and the
 * caller drains them with bvnr_demux_next until it returns BVNR_NEXT_NONE; the
 * bytes must stay valid until then, and feeding again before that returns false
 * and changes nothing. bvnr_demux_next returns
 *
 *   BVNR_NEXT_MESSAGE  item->channel, item->data, item->length: a message. One
 *                      that lies whole in the fed buffer is handed out from it
 *                      without a copy, one whole in a chunk that arrived
 *                      split across feeds from the reader's chunk buffer, and
 *                      one split across chunks is reassembled in the
 *                      channel's buffer.
 *   BVNR_NEXT_EVENT    item->event, item->value: a document event (only when
 *                      opened with deliver_events). Every verified event except
 *                      the mux stream's chunks, in document order and in order
 *                      with the messages: the values before the stream, its
 *                      ev_octet_stream_start, its messages, its
 *                      ev_octet_stream_end, the values after it, and ev_stream_end
 *                      once finished. The bvnr_data_t is a copy (its conv.num and
 *                      conv.den are NULL; conv.text is kept).
 *   BVNR_NEXT_NONE     the fed bytes are used up.
 *   BVNR_NEXT_ERROR    the stream failed: bvnr_demux_error says why (events queued
 *                      before the failure are still handed out first).
 *
 * Everything an item points to is valid until the next call on the demux.
 * bvnr_demux_next on a demux with an on_message callback, or one not opened for
 * feeding, returns BVNR_NEXT_ERROR.
 *
 * bvnr_demux_feed returns false once the stream has failed. bvnr_demux_finish
 * declares end of input and returns what bvnr_read would for the document: true
 * when it is complete -- check bvnr_demux_error even then, for a message the
 * stream's end cut short (error_octet_stream_truncated) -- and false when it is
 * not. In pull mode it refuses, changing nothing, while fed bytes are left
 * undrained; after it, bvnr_demux_next hands out the closing events.
 *
 * bvnr_demux_get_reader gives the fed reader, for the error position, the
 * recovery counters and the declared version (NULL before bvnr_demux_open_feed).
 * Do not install bvnr_demux_on_event of a push-fed demux on another reader.
 */
typedef struct bvnr_demux_item_s {
	uint64_t		channel;
	const uint8_t*		data;
	uint64_t		length;
	bvnr_event_t		event;
	const bvnr_data_t*	value;
} bvnr_demux_item_t;
BVN_API bool bvnr_demux_open_feed(bvnr_demux_t* dm,
	const bvnr_read_flags_t* flags, bool deliver_events);
BVN_API bool bvnr_demux_feed(bvnr_demux_t* dm, const void* buf, uint64_t len);
BVN_API int  bvnr_demux_next(bvnr_demux_t* dm, bvnr_demux_item_t* item);
BVN_API bool bvnr_demux_finish(bvnr_demux_t* dm);
BVN_API const bvnr_reader_t* bvnr_demux_get_reader(const bvnr_demux_t* dm);

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
