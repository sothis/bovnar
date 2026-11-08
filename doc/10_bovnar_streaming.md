# Bovnar — Streaming, Framing & Multiplexing

> **Spec version:** 1.2
> **Status:** Reference — protocols layered on the event API, not part of the wire format
> **Scope:** Endless streaming, multi-document framing, octet multiplexing, and embedded documents.

This document describes four capabilities layered **on top of** the Bovnar
lexer/validator event API, *beside* the DOM. None of them change the frozen 1.0
grammar or wire format (see the [Specification](03_bovnar_spec.md)); they are
applications built on the SAX-style event pipe and the octet-stream value form.

| # | Capability | Where it lives | Header |
|---|------------|----------------|--------|
| 1 | Endless streaming | core policy knob | `bovnar.h` |
| 2 | Multi-document framing | transport beside the lexer | `bovnar_stream.h` |
| 3 | Octet multiplexing | convention inside one octet stream | `bovnar_stream.h` |
| 4 | Document-in-document | a document embedded as octet payload | `bovnar_stream.h` |

The mental model: the **reader** turns bytes into a `bvnr_event_t` stream and the
**writer** turns events back into bytes. The DOM is one consumer of that pipe;
everything here is another. Octet streams (spec §9) are the binary substrate —
opaque, chunked, UTF-8-validation-suspended byte regions — and three of the four
capabilities are sub-protocols carried in or around them.

---

## Table of Contents

1. [Endless streaming](#1-endless-streaming)
2. [Multi-document record framing](#2-multi-document-record-framing)
    - 2.1 [Wire format](#21-wire-format)
    - 2.2 [API](#22-api)
    - 2.3 [CLI](#23-cli)
    - 2.4 [Push-fed frame reading](#24-push-fed-frame-reading)
3. [Octet multiplexing (out-of-band channels, cross-chunk)](#3-octet-multiplexing-out-of-band-channels-cross-chunk)
    - 3.1 [Wire convention (two layers)](#31-wire-convention-two-layers)
    - 3.2 [API](#32-api)
    - 3.3 [CLI](#33-cli)
    - 3.4 [Push-fed demultiplexing, with metadata before the stream](#34-push-fed-demultiplexing-with-metadata-before-the-stream)
4. [Document-in-document](#4-document-in-document)
5. [Python bindings](#5-python-bindings)
6. [Rust bindings](#6-rust-bindings)
7. [Composition](#7-composition)
8. [Live streams: callback I/O, push-fed reading, flushed writing](#8-live-streams-callback-io-push-fed-reading-flushed-writing)
    - 8.1 [Callback sources and sinks](#81-callback-sources-and-sinks)
    - 8.2 [The push-fed document reader](#82-the-push-fed-document-reader)
    - 8.3 [Draining without a callback per item](#83-draining-without-a-callback-per-item)
    - 8.4 [What it costs](#84-what-it-costs)
    - 8.5 [Writing live: a flush per message](#85-writing-live-a-flush-per-message)
9. [Shared conformance vectors](#9-shared-conformance-vectors)

- [See also](#see-also)

---

## 1. Endless streaming

Every byte/offset/line/column counter in the core is 64-bit, so a single
never-ending document — or a single octet stream of unbounded length — is
limited only by policy knobs on `bvnr_read_flags_t`: `max_file_size` for the
stream as a whole, and — for its *text* — `max_text_bytes`, whose default is
finite. With both left at `0` an octet stream may run without bound, but the
text of a document stops at 2 147 483 647 bytes (`error_text_data_too_long`);
raise `max_text_bytes` as well (to `UINT64_MAX` if need be) for a text stream
longer than 2 GiB. (`max_file_size`
exists on `bvnr_write_flags_t` too, for symmetry, and the writer ignores
it: a writer never limits how much it writes.)

```c
#define BVNR_FILESIZE_UNLIMITED  0   /* in bovnar.h */
```

Semantics — **`0` is unlimited and the default**:

| `max_file_size` value | Effect |
|-----------------------|--------|
| `0` (= `BVNR_FILESIZE_UNLIMITED`, the default) | **No cap** — endless, no accumulated size limit |
| any positive N | Cap at N bytes (`error_file_too_long` beyond) |

```c
bvnr_read_flags_t fl = {
    .on_verified   = my_cb,
    .max_file_size = BVNR_FILESIZE_UNLIMITED,   /* endless */
};
```

A long-lived document is processed incrementally: process each value in the
`on_verified`/`on_unverified` callback and retain nothing, and memory stays flat
regardless of stream length. (The DOM builder, by contrast, retains a tree and
is therefore not the tool for an endless stream.)

---

## 2. Multi-document record framing

The grammar has no in-band document separator: a reader ends one document at EOF.
To carry a **sequence of independent documents** over one byte stream (file,
socket, pipe) without closing it, wrap each in a length-prefixed frame. The
framing lives beside the lexer; each payload is a complete, standalone document
parsed by an ordinary reader.

### 2.1 Wire format

```
frame   = "BVF1"            ; 4-byte magic
          payload_len       ; uint64, little-endian
          payload[payload_len]
stream  = { frame }         ; terminated by EOF on a frame boundary
```

A clean EOF *before any header byte* ends the sequence successfully; an EOF
part-way through a header or payload is a truncation error.

### 2.2 API

```c
/* Producer */
bool bvnr_frame_write(bvnr_sink_t* sink, const void* doc, uint64_t doc_len);

/* Consumer */
typedef struct {
    bvnr_read_flags_t* flags;            /* per-document reader callbacks/limits */
    void*              userdata;
    bool (*on_document)(void* ud, uint64_t index, bool ok, error_code_t err);
    bool               continue_past_failed; /* keep going after a failed document */
    uint64_t           max_document_size;    /* 0 => BVNR_DOC_DEFAULT_MAX (256 MiB) */
} bvnr_doc_stream_opts_t;

bool bvnr_doc_stream_read(bvnr_source_t* src,
                          const bvnr_doc_stream_opts_t* opts,
                          uint64_t* out_count);
```

`bvnr_doc_stream_read` reads frames until EOF, parsing each payload with a fresh
reader (reusing one handle internally) and invoking `on_document` after each. It
returns false on a framing/IO error, or on the first per-document parse failure
unless `continue_past_failed` is set. Each frame payload is buffered in RAM
before parsing (the buffer grows with the bytes that actually arrive, so a
crafted oversized length cannot force a large up-front allocation), bounded by
`max_document_size`. Unlike `max_file_size`, this framing guard stays finite when
`0` (it then selects `BVNR_DOC_DEFAULT_MAX`, 256 MiB); set an explicit huge cap
(e.g. `UINT64_MAX`) to lift it.

Two **orthogonal** error controls — because each document occupies its own
length-delimited frame, the two are independent:

| Control | Scope | Effect |
|---------|-------|--------|
| `flags->continue_on_error` | *within* a document | resync/recover from errors inside one document |
| `continue_past_failed` | *across* documents | report a failed document and advance to the next frame instead of aborting |

So you can parse each document **strictly** (`continue_on_error = false`) yet
**resiliently** process every frame (`continue_past_failed = true`), reporting the
bad ones via `on_document` — the natural mode for a log of independent records.

### 2.3 CLI

```sh
bovnar frames pack a.bvnr b.bvnr c.bvnr > docs.bvf   # wrap each file in a frame
bovnar frames list docs.bvf                          # list documents + status
bovnar frames list -                                 # read the frame stream from stdin
```

### 2.4 Push-fed frame reading

`bvnr_doc_stream_read` pulls from a source until it ends. For a frame stream
that arrives in pieces — a non-blocking socket, an event loop — the frame
reader is **fed** instead (§8), and keeps one reader for the whole stream:

```c
bvnr_frame_reader_t* bvnr_frame_reader_create(const bvnr_doc_stream_opts_t* opts);
bvnr_frame_reader_t* bvnr_frame_reader_create_pull(uint64_t max_document_size);
void         bvnr_frame_reader_destroy(bvnr_frame_reader_t* fr);
bool         bvnr_frame_reader_feed  (bvnr_frame_reader_t* fr, const void* buf, uint64_t len);
int          bvnr_frame_reader_next  (bvnr_frame_reader_t* fr, const uint8_t** doc, uint64_t* len);
bool         bvnr_frame_reader_finish(bvnr_frame_reader_t* fr);
error_code_t bvnr_frame_reader_error (const bvnr_frame_reader_t* fr);
uint64_t     bvnr_frame_reader_count (const bvnr_frame_reader_t* fr);
```

Two kinds, chosen when it is created:

- **Parsing** (`bvnr_frame_reader_create`): every document is parsed exactly as
  `bvnr_doc_stream_read` parses it with the same `opts` — the same events through
  `opts->flags`, the same `on_document` calls, `continue_past_failed` and
  `max_document_size`. Each payload is streamed into a reused push-fed reader as
  its bytes arrive, so a frame is never buffered whole, however large; the
  reader keeps its buffers from one document to the next, so once warm the
  frame reader allocates nothing per document.
- **Pull** (`bvnr_frame_reader_create_pull`): the documents are handed out as
  bytes by `bvnr_frame_reader_next`, for the caller to parse as it likes. A
  document whose frame lies whole in the fed buffer is handed out from that
  buffer; one split across feeds is gathered in a buffer the reader owns and
  reuses. Either pointer is valid until the next call.

Feeding never fails for want of input; `bvnr_frame_reader_finish` is where a
stream cut inside a header or a payload is `error_got_incomplete_bvnr_stream`. A
bad magic is `error_unexpected_input_byte`, a declared length over the cap
`error_value_out_of_range`.


---

## 3. Octet multiplexing (out-of-band channels, cross-chunk)

Many logical channels interleaved inside a **single octet-stream value**. The
channel id and message framing are a convention interpreted *above* `ev_data`,
so to the lexer these are ordinary `0x01` data chunks and `bvnr_data_t` needs no
channel field. The 1.0 wire format is untouched.

### 3.1 Wire convention (two layers)

```
per octet chunk:    varint(channel)  fragment_bytes
per channel:        concat(fragments in order) = the channel's logical stream,
                    a sequence of length-prefixed messages:
                        varint(msg_len)  msg_bytes
```

- `varint` is unsigned LEB128 (1 byte for channels/lengths < 128; up to 10 bytes).
- A message is **length-prefixed**, so it may span any number of chunks, and
  channels may interleave freely at chunk granularity — the demultiplexer
  reassembles each channel independently. Both the message data **and** its
  length varint may straddle chunk boundaries (the demux accumulates a partial
  length varint per channel).
- `BVNR_MUX_MAX_MESSAGE` is a conservative **single-chunk guarantee**, not an
  enforced cap: any message of at most that many bytes is emitted by
  `bvnr_mux_send` in one chunk regardless of channel id or length magnitude
  (it subtracts the maximum 10+10 varint bytes from the 64 KiB chunk). Larger
  messages are simply split; nothing rejects them.
- **The one wire requirement:** the per-chunk **channel** varint — the routing
  prefix the demux needs to attribute a chunk — is whole within its chunk.
  `bvnr_mux_send` always satisfies this (the channel id is ≤ 10 bytes and leads
  every chunk).

### 3.2 API

```c
/* Producer */
bool bvnr_mux_begin(bvnr_writer_t* w, const char* key);
bool bvnr_mux_send (bvnr_writer_t* w, uint64_t channel,
                    const void* data, uint64_t len);   /* split as needed */
bool bvnr_mux_end  (bvnr_writer_t* w);
bool bvnr_write_flush(bvnr_writer_t* w);   /* hand what was sent over now */

/* Consumer */
typedef bool (*bvnr_mux_on_msg_fn)(void* ud, uint64_t channel,
                                   const uint8_t* data, uint64_t len);

bvnr_demux_t* bvnr_demux_create(bvnr_mux_on_msg_fn on_message,
                                void* userdata, uint64_t max_message);
void          bvnr_demux_destroy(bvnr_demux_t* dm);
bool          bvnr_demux_set_key (bvnr_demux_t* dm, const char* key);
bool          bvnr_demux_on_event(void* demux, bvnr_event_t ev, bvnr_data_t* d);
error_code_t  bvnr_demux_error(const bvnr_demux_t* dm);
```

A message sent to an fd or callback sink waits in the writer's 64 KiB buffer
until it fills or the document finishes; a producer feeding a live consumer
calls `bvnr_write_flush` after each `bvnr_mux_send` (§8.5). The bytes are the
same either way.

Install `bvnr_demux_on_event` as `bvnr_read_flags_t.on_verified` with `userdata`
set to the `bvnr_demux_t*`. It peels the channel varint off each chunk,
reassembles per-channel messages (owning a buffer per active channel), and
invokes `on_message` for each complete message. A malformed varint, an oversized
declared length (> `max_message`, default 64 MiB), or an allocation failure
latches `bvnr_demux_error` and aborts the read. If `on_message` returns false the
read is aborted and `bvnr_demux_error` reports `error_scanner_callback_failed`
(distinct from the desync code, so a deliberate stop is distinguishable from a
framing error).

A separate, non-fatal case: if the octet stream *ends* while a channel is still
mid-message (or mid-length-varint), the octet framing itself was well-formed, so
the read still **succeeds** — but the partial message is dropped and
`bvnr_demux_error` latches `error_octet_stream_truncated`. This is deliberately a
different code from the desync `error_octet_stream_out_of_sync`: a truncated
message is a message-layer loss, not a framing error. Always check
`bvnr_demux_error` after the read even when it returned true.

**Key scoping.** By default the demux treats *every* octet stream in the document
as multiplexed. Call `bvnr_demux_set_key(dm, "mux")` so it only demuxes streams
opened under that key — a document can then mix one mux stream with ordinary
binary octet payloads (those under other keys are ignored, not misread as
framing). Pass `NULL` to clear the filter.

**Memory.** The demux keeps one reassembly buffer per channel it has seen; each
grows to that channel's largest message and is retained for reuse until
`bvnr_demux_destroy`, so steady-state memory is the sum of per-channel high-water
marks (bounded by 4096 channels). Channels are not reclaimed individually.

That 4096 is a **hard limit**, not only a memory estimate. A chunk naming a
4097th distinct channel cannot be attributed, so the read aborts and
`bvnr_demux_error` latches `error_octet_stream_out_of_sync` — the demux has lost
track of the stream, even though the wire framing was well formed. Because
channels are never reclaimed, the count is of *distinct channel ids seen over the
whole stream*, not of channels live at any one moment.

```c
bvnr_demux_t* dm = bvnr_demux_create(on_message, ctx, 0 /* default cap */);
bvnr_read_flags_t fl = { .on_verified = bvnr_demux_on_event, .userdata = dm };
/* ... open_read_* + bvnr_read ... */
if (bvnr_demux_error(dm) != error_none) { /* desync, oversize, alloc, or a truncated message */ }
bvnr_demux_destroy(dm);
```

### 3.3 CLI

```sh
bovnar mux pack 1:req.bvnr 42:log.bvnr > mux.bvnr   # files onto channels 1 and 42
bovnar mux list mux.bvnr                            # channel <n>: <len> bytes
bovnar mux pack 7:a.bvnr | bovnar mux list -        # pipe producer to consumer
```

### 3.4 Push-fed demultiplexing, with metadata before the stream

A mux stream is one value of an ordinary document, so the document can say what
the stream is before it starts — and go on after it ends. The producer writes
the metadata with the normal writer, then opens the stream:

```c
bvnr_write_struct_start(w, "meta");
bvnr_write_string(w, "codec", "av1");
bvnr_write_float_unit(w, "rate", 32, 30.0, BVN_UNIT_NO_PREFIX(bu_hertz));
bvnr_write_struct_end(w);
bvnr_mux_begin(w, "media");             /* .media = \x00 ... */
bvnr_mux_send(w, 1, frame, frame_len);
/* ... */
bvnr_mux_end(w);                        /* ... \x00; */
```

which is, on the wire,

<!-- bovnar-example: illustrative -->
```bovnar
.meta = { .codec = "av1"; .rate = <float:32,Hz> 30.0; };
.media = \x00 …chunks… \x00;
```

A consumer reading that live hands the bytes to a **push-fed demux**: it owns a
push-fed reader (§8.2), keeps the key scope, `max_message`, the 4096-channel
limit and every error code of `bvnr_demux_on_event`, and the result is the same
as installing that on a reader of the whole buffer.

```c
bool bvnr_demux_open_feed(bvnr_demux_t* dm, const bvnr_read_flags_t* flags,
                          bool deliver_events);
bool bvnr_demux_feed  (bvnr_demux_t* dm, const void* buf, uint64_t len);
int  bvnr_demux_next  (bvnr_demux_t* dm, bvnr_demux_item_t* item);
bool bvnr_demux_finish(bvnr_demux_t* dm);
const bvnr_reader_t* bvnr_demux_get_reader(const bvnr_demux_t* dm);

typedef struct bvnr_demux_item_s {
    uint64_t           channel;   /* BVNR_NEXT_MESSAGE */
    const uint8_t*     data;
    uint64_t           length;
    bvnr_event_t       event;     /* BVNR_NEXT_EVENT */
    const bvnr_data_t* value;
} bvnr_demux_item_t;
```

**With callbacks** (an `on_message` given to `bvnr_demux_create`): every message
a feed completes is delivered before `bvnr_demux_feed` returns, and the caller's
`flags->on_verified` sees every event of the document — the metadata first, then
the stream, then whatever follows — so the metadata is known before the first
message:

```c
bvnr_demux_t* dm = bvnr_demux_create(on_message, ctx, 0);
bvnr_demux_set_key(dm, "media");
bvnr_read_flags_t fl = { .on_verified = on_meta_event, .userdata = ctx };
bvnr_demux_open_feed(dm, &fl, false);
while ((n = recv(sock, buf, sizeof buf, 0)) > 0)
    if (!bvnr_demux_feed(dm, buf, (uint64_t)n)) break;
bvnr_demux_finish(dm);
if (bvnr_demux_error(dm) != error_none) { /* as in §3.2 */ }
```

**Pulled** (no `on_message`): the fed bytes are drained with `bvnr_demux_next`,
one plain call per item and no callback at all (§8.3). Opened with
`deliver_events`, the document's own events come out of the same loop, in
document order and in order with the messages — the metadata values, the mux
stream's `ev_octet_stream_start`, its messages, its `ev_octet_stream_end`, the
values after it:

```c
bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, 0);
bvnr_demux_set_key(dm, "media");
bvnr_demux_open_feed(dm, NULL, true);
bvnr_demux_item_t it;
while ((n = recv(sock, buf, sizeof buf, 0)) > 0) {
    bvnr_demux_feed(dm, buf, (uint64_t)n);
    int rc;
    while ((rc = bvnr_demux_next(dm, &it)) > 0) {
        if (rc == BVNR_NEXT_EVENT)  on_meta(it.event, it.value);
        else                        on_frame(it.channel, it.data, it.length);
    }
    if (rc == BVNR_NEXT_ERROR) break;          /* bvnr_demux_error(dm) */
}
bvnr_demux_finish(dm);
while (bvnr_demux_next(dm, &it) > 0) { /* the closing events */ }
```

A message that lies whole in the fed buffer is handed out from it without a
copy; one split across chunks or feeds is reassembled in the channel's buffer.
Everything an item points to is valid until the next call on the demux, and the
fed buffer must stay valid until `bvnr_demux_next` has returned
`BVNR_NEXT_NONE` (feeding before that is refused). Truncation is a finish
result, as for any fed reader: `bvnr_demux_finish` returns what `bvnr_read` would
for the document, and a message the stream's end cut short is still the latched,
non-fatal `error_octet_stream_truncated`.


---

## 4. Document-in-document

Embed a complete Bovnar document as the octet-stream payload of a key. The outer
parser treats the inner document as opaque octets; only an application that knows
the key holds a nested document re-feeds those bytes to a sub-reader. This is the
natural recursion primitive — no envelope type, just the octet-stream value form.

```c
/* Producer: emits  .<key> = <octets of doc>;  (64 KiB chunks) */
bool bvnr_embed_document(bvnr_writer_t* w, const char* key,
                         const void* doc, uint64_t doc_len);

/* Consumer: parse the embedded bytes with an ordinary reader (recursion). */
bool bvnr_parse_embedded(const void* doc, uint64_t doc_len,
                         bvnr_read_flags_t* flags);
```

To recurse while reading the outer document, accumulate the octet chunks for the
known key between `ev_octet_stream_start` and `ev_octet_stream_end`, then call
`bvnr_parse_embedded` on the accumulated bytes with the nested callbacks:

```c
case ev_octet_stream_end:
    if (in_payload)
        bvnr_parse_embedded(buf, len, &inner_flags);   /* parses the nested doc */
    break;
```

The inner callbacks may themselves embed/extract further documents, so nesting
is arbitrarily deep (bounded by your own recursion budget).

---

## 5. Python bindings

All four capabilities are exposed through the `bovnar.stream` module (a thin
ctypes layer over the C functions, so framing, varint reassembly and the
strict-vs-resilient document policy behave identically):

```python
import bovnar
from bovnar import stream

# Multi-document framing
blob = stream.dump_documents([{"id": 1}, {"id": 2}])     # list[dict] -> bytes
docs = stream.load_documents(blob)                        # bytes -> list[dict]
docs = stream.load_documents(blob, continue_past_failed=True)  # bad docs -> None

# Octet multiplexing (channel, payload) — any size, interleaved
m   = stream.mux_dump([(1, b"hello"), (42, b"world"), (1, b"x" * 200000)])
out = stream.mux_load(m)                                  # [(1, b"hello"), ...]

# Document-in-document
outer = stream.embed_document(bovnar.dumps({"v": 1}), key="payload")
inner = stream.parse_embedded(bovnar.loads(outer)["payload"])   # {"v": 1}
```

`stream.BVNR_FILESIZE_UNLIMITED` (`== 0`) selects endless mode for
**`max_file_size`** only. Note it does **not** lift the framing/mux guards:
for `max_document_size` and `max_message`, `0` selects their finite defaults
(`BVNR_DOC_DEFAULT_MAX` / `BVNR_MUX_DEFAULT_MAX`), so pass an explicit large
value there to raise them. See [Python Bindings](09_bovnar_python_bindings.md)
and `python/tests/test_stream.py`.

**Sockets and file objects.** The loaders (`load_documents`, `mux_load`, and
`bovnar.load` for one document) also take a socket or a binary file object and
read it to its end through a callback source (§8.1), parsing as they pull; the
dumpers take a `file=` and write through a callback sink. `mux_dump(...,
preamble={...})` writes the values that describe the stream before it opens
(§3.4).

**Live streams** (§8) are read with three feed objects over the push-fed C
readers, each handed bytes in pieces of any size: `DocumentFeed` (§8.2: a
document's events, or each top-level value once it is complete), `FrameFeed`
(§2.4: each document's bytes, or each document parsed) and `DemuxFeed` (§3.4:
`(channel, payload)` per message, and with `values=True` the values before and
after the mux stream, in document order). `DemuxFeed` and the unparsed
`FrameFeed` drain with `bvnr_demux_next` / `bvnr_frame_reader_next`, one plain
call per item and no callback (§8.3); a payload is `bytes`, or with
`view=True` a `memoryview` valid until the next call, which the feed then
releases:

```python
from bovnar import stream

with stream.DemuxFeed(key="media", values=True) as dm:
    for chunk in chunks:                          # whatever recv() returned
        for item in dm.feed(chunk):
            if isinstance(item, stream.Value):    # .meta before the first message
                on_meta(item.key, item.value)
            else:
                on_frame(item.channel, item.payload)
    for item in dm.finish():                      # raises if cut off part-way
        if isinstance(item, stream.Value):        # values that end the document
            on_meta(item.key, item.value)
```

The limits and error codes are those of the batch functions. A message costs
microseconds in Python — the interpreter and ctypes, not the C path of §8.4:
about 3 µs for a 150-byte message and 7 µs for a 30 KB one fed whole, against
5 µs and 36 µs through `mux_load`'s callback, on the machine of §8.4. The
figures, and what feeding in small pieces costs, are in
[Python Bindings §4.7](09_bovnar_python_bindings.md#47-live-streams-feeding-in-pieces)
(`python/bench/bench_stream.py`).

**Writing live** (§8.5) is `stream.MuxWriter(file, key=, preamble=)`, whose
`send(channel, payload)` hands each message to the socket or file before it
returns, and `stream.FrameWriter(file)`, one framed document per `write` or
`dump`; `Writer.flush()` does the same for a document written value by value.
The bytes are those of `mux_dump` and `dump_documents`:

```python
with stream.MuxWriter(sock, key="media", preamble={"meta": {"codec": "av1"}}) as mux:
    for channel, frame in frames:
        mux.send(channel, frame)                  # flushed: the reader has it now
```

A flushed send costs about 5.5 µs for a 150-byte message against 2.2 µs
batched (`flush_each=False`), the ctypes call and the call of the object's
`sendall`
([Python Bindings §4.8](09_bovnar_python_bindings.md#48-live-streams-writing-a-message-at-a-time)).

---

## 6. Rust bindings

The Rust crate has the same batch functions in `bovnar::stream`
(`frame_documents`, `load_documents`, `mux_dump`, `mux_load`,
`embed_document`, `parse_embedded`), over the C functions above. Like
Python's, they work on whole buffers. Their twins over `std::io::Read` and
`Write` (`load_documents_reader`, `mux_dump_to`, …), like `Reader::read_reader`
and `Writer::to_writer`, stream through the callback sources and sinks of §8.1:
they parse as they read and write as they serialise, with the in-memory
functions' results.

For a stream that never ends, `bovnar::codec` writes and reads the framing of
§2 and the multiplexing of §3 incrementally, in pure Rust: `MuxWriter` and
`MuxReader`, `write_frame` and `FrameReader`. The writer's output is byte for
byte that of `bvnr_mux_send` and `bvnr_frame_write`; the reader is fed bytes in
pieces of any size, keeps the demultiplexer's limits and error codes, hands a
message out without a copy when it lies whole in the input, and can peek at a
message's channel, length and first bytes to skip it or forward its chunks
unchanged. A multiplexed stream may open with a **preamble**, ordinary text
values in the same document before the stream's assignment
(`.meta = { … }; .media = <octet stream>;`). Nothing is allocated per message
once the reader's buffers have grown. See
[Rust Bindings §4.8](14_bovnar_rust_bindings.md#48-live-streams-the-framing-codec-bovnarcodec).

To parse documents as they arrive, `bovnar::live` wraps the push-fed readers of
§8: `DocumentFeed` over `bvnr_open_read_feed` (and `ValueFeed`, which hands out
each top-level assignment as a value once it is complete), `FrameParser` and
`FramePull` over the two kinds of fed frame reader (§2.4), and `Demux` over the
fed demultiplexer (§3.4). `FramePull` and `Demux` are drained like the codec's
readers, one item per call, borrowed from the input without a copy where it lies
whole there, and the borrow checker keeps an item from outliving the next call.
With `deliver_events` the demux hands out the document's own events in order
with the messages, so the metadata before a stream is there before its first
message. `EmbeddedFeed` reads the documents embedded in a live document (§4)
by feeding their chunks to one reused reader, and a `Writer` takes
`embed_document` and `mux_begin` / `mux_send` / `mux_end` mid-document.

Either writer sends a stream message by message. A `Writer` into a
`std::io::Write` (`Writer::to_writer`) batches into the C writer's 64 KiB buffer,
and `Writer::flush` (`bvnr_write_flush`, §8.5) hands over what it holds and
flushes the destination, so a `flush` after each `mux_send` gives a consumer
every message as it is sent, inside a document that may hold any values before
and after the stream. `codec::MuxWriter` writes the same bytes with no C call,
for a stream that is the whole document after its preamble, and leaves the
flushing to the caller.
`MuxReader` stays the fast path for a stream that is one octet stream; `Demux`
takes a document of any shape — values after the stream, other octet streams
beside it, every reader limit. See
[Rust Bindings §4.9](14_bovnar_rust_bindings.md#49-live-documents-through-c-bovnarlive).

---

## 7. Composition

The four compose cleanly because they live at different layers:

- A **frame stream** (2) can carry documents that each **embed** sub-documents (4).
- A **multiplexed** octet stream (3) is itself just one value in a document, which
  can be one frame in a frame stream (2).
- Any of the above is **endless** (1) by default (`max_file_size` 0 ==
  `BVNR_FILESIZE_UNLIMITED`); set a positive `max_file_size` to impose a cap.

Because each is a thin application over the public event API, you can also write
your own consumer/producer at the same seam without touching the core.

---

## 8. Live streams: callback I/O, push-fed reading, flushed writing

Everything above reads from a `bvnr_source_t` that is *pulled* until it ends.
That fits a file and a blocking socket. It does not fit a stream that never
ends and arrives when it arrives — a non-blocking socket in an event loop, a
binding that is handed bytes — because a pull that returns nothing *is* end of
input: a short feed would read as a truncated document. This section is the
other way in, and (§8.5) the writing side of it: a producer that hands each
message over as it is sent.

### 8.1 Callback sources and sinks

`bvnr_source_from_callback(s, pull, userdata)` and
`bvnr_sink_to_callback(s, push, flush, userdata)` put the caller's own transport
behind a source or sink, usable everywhere an fd or memory one is
([doc/08 §1.14](08_bovnar_readwrite_api.md#114-bvnr_source_from_callback),
[§2.17](08_bovnar_readwrite_api.md#217-bvnr_sink_to_callback)). The user pointer
lives inside the 64-byte handle, because the reader and writer work on a copy of
it; the callback reads it back with `bvnr_source_get_userdata` /
`bvnr_sink_get_userdata`. A callback sink fails as a transport does
(`error_writing_to_sink`), and the writer batches into its 64 KiB buffer before
pushing, or pushes early at `bvnr_write_flush` (§8.5).

### 8.2 The push-fed document reader

`bvnr_open_read_feed`, `bvnr_read_feed`, `bvnr_read_finish`
([doc/08 §1.15](08_bovnar_readwrite_api.md#115-push-fed-reading-bvnr_open_read_feed)):
the caller hands over bytes in pieces of any size and says when the document is
over. The events are exactly those `bvnr_read` delivers for the whole buffer,
wherever the pieces were cut, and so are the errors and their positions. Running
out of input is never an error until the finish. One reader serves an endless
document with flat memory, and the frame reader (§2.4) and the demux (§3.4) are
built on it.

It is the same state machine `bvnr_read` runs, not a second one: the text lexer
already resumed at any byte, and the octet-stream framing — which used to pull
until a chunk was complete — is now a state machine of its own that stops at the
end of any buffer, so both readers share every line of it. A chunk that lies
whole in the input reaches the callbacks straight from it.

### 8.3 Draining without a callback per item

A caller that pays for every callback — a binding calling back through an FFI —
drains a feed in one loop instead: `bvnr_frame_reader_next` hands out documents,
`bvnr_demux_next` messages and, with `deliver_events`, the document's events.
Each returns `BVNR_NEXT_DOCUMENT` / `BVNR_NEXT_MESSAGE` / `BVNR_NEXT_EVENT` for an
item, `BVNR_NEXT_NONE` when the fed bytes are used up, and `BVNR_NEXT_ERROR` when
the stream failed. Items point into the fed buffer where they lie whole in it,
into the reader's own buffers otherwise, and are valid until the next call; the
fed buffer must stay valid until the drain has returned `BVNR_NEXT_NONE`.

### 8.4 What it costs

Measured with `bvnr_stream_bench` (`tests/bovnar_stream_bench.c`), release
build, one core of an Intel i7-2700K (3.5 GHz, 2011); per message, the 400 KB
message spanning seven chunks. Figures move with the machine; the ratios are the
point.

| Path | 150 B | 30 KB | 400 KB |
|------|------:|------:|-------:|
| `bvnr_mux_send` (2.0.2: allocated a 64 KiB chunk per call) | 262 ns | 8.2 µs | 121 µs |
| `bvnr_mux_send` (chunk buffer kept on the writer) | 202 ns | 8.6 µs | 120 µs |
| `bvnr_read` + `bvnr_demux_on_event` (2.0.2: copied every message) | 183 ns | 7.2 µs | 116 µs |
| `bvnr_read` + `bvnr_demux_on_event` (whole-chunk messages not copied) | 134 ns | 0.25 µs | 84 µs |
| push-fed demux, callback, one feed | 139 ns | 0.25 µs | 83 µs |
| push-fed demux, `bvnr_demux_next`, one feed | 159 ns | 0.27 µs | 86 µs |
| push-fed demux, callback, 1460 B feeds | 152 ns | 7.9 µs | 158 µs |
| push-fed demux, `bvnr_demux_next`, 1460 B feeds | 168 ns | 7.9 µs | 157 µs |
| push-fed demux, `bvnr_demux_next`, 64 B feeds | 327 ns | 28 µs | 206 µs |
| `bvnr_mux_send` into a callback sink, batched | 105 ns | 6.1 µs | 86 µs |
| `bvnr_mux_send` + `bvnr_write_flush` into a callback sink | 113 ns | 6.1 µs | 86 µs |

The two callback-sink rows are from a later run (§8.5), where `bvnr_mux_send`
into a memory sink measured 100 ns, 4.8 µs and 69 µs: the sink copies what it
is pushed, as a socket's send does, minus the system call.

A message that fits one chunk and arrives in one feed is handed out without
touching its bytes, which is why the 30 KB column falls by a factor of thirty.
Fed in small pieces, a chunk is gathered before it is passed on, and a message
split over chunks is reassembled: one copy each.

Per framed document, a 43-byte one and a 30 KB one of numbers and strings:

| Path | 43 B | 30 KB |
|------|-----:|------:|
| `bvnr_doc_stream_read` (2.0.2: the reader's buffers and the payload allocated per document) | 5.6 µs | 757 µs |
| `bvnr_doc_stream_read` (buffers kept across documents) | 2.4 µs | 758 µs |
| parsing frame reader, one feed | 2.2 µs | 766 µs |
| parsing frame reader, 1460 B feeds | 2.3 µs | 764 µs |

Re-opening a reader for the next document used to free its frame stack and
scratch buffers and allocate them again, about 120 KB zeroed, and to copy the
13 KB unit policy out of the validator and back while clearing it: more than a
small document costs to parse, and the frame reader paid it too (5.5 µs for the
43-byte document). A 30 KB document is dominated by its parse. `bvnr_mux_send`,
`bvnr_demux_next` and `bvnr_frame_reader_next` allocate nothing per message once
warm, nor the parsing frame reader and a re-opened fed demux per document; a
`bvnr_doc_stream_read` call allocates the same whatever the number of documents
(`tests/bovnar_alloc_test.c`).

### 8.5 Writing live: a flush per message

The writer gathers what it writes for an fd or callback sink in a 64 KiB buffer
and pushes it when the buffer is full or at `bvnr_write_finish`, so a consumer
at the other end of a socket sees a stream of small messages in 64 KiB bursts,
or not at all until the producer stops. `bvnr_write_flush` hands everything
written so far to the sink, then calls the sink's flush callback if it has one
([doc/08 §2.18](08_bovnar_readwrite_api.md#218-bvnr_write_flush)). It is legal
between any two writer calls — inside an open mux stream as much as between two
values — and changes only when the bytes leave: a separator owed to the next
value stays owed, so the output is byte-identical however often it is flushed.
For a memory sink it does nothing.

A producer that streams frames to a live consumer writes its metadata, opens
the stream, and flushes after every message:

```c
static bool sock_push(bvnr_sink_t* s, const void* buf, uint32_t len)
{
    return send_all(*(int*)bvnr_sink_get_userdata(s), buf, len);
}

bvnr_sink_t sink;
bvnr_sink_to_callback(&sink, sock_push, NULL, &sock);
bvnr_writer_t* w = bvnr_writer_create();
bvnr_open_write_sink(w, &sink, false, NULL);

bvnr_write_struct_start(w, "meta");               /* .meta = { ... }; */
bvnr_write_string(w, "codec", "av1");
bvnr_write_float_unit(w, "rate", 32, 30.0, BVN_UNIT_NO_PREFIX(bu_hertz));
bvnr_write_struct_end(w);
bvnr_mux_begin(w, "media");                       /* .media = \x00 */
bvnr_write_flush(w);                              /* the consumer has .meta now */

while (next_frame(&channel, &frame, &len)) {
    if (!bvnr_mux_send(w, channel, frame, len) || !bvnr_write_flush(w))
        break;                                    /* error_writing_to_sink */
}

bvnr_mux_end(w);                                  /* \x00; */
if (!bvnr_write_finish(w)) { /* bvnr_writer_get_error(w) */ }
bvnr_writer_destroy(w);
```

At the other end a push-fed demux (§3.4) hands out `.meta` before the first
message and each message as soon as its last byte arrives, so message N is
there before message N+1 is sent (`tests/bovnar_feed_test.c` runs exactly this,
with the writer's sink feeding the demux). A failed push or flush latches like
any writer error. The bindings wrap it: `Writer.flush()` and
`stream.MuxWriter` in Python (§5), `Writer::flush` in Rust (§6).

There is no "flush after every message" option on `bvnr_mux_send`: it would
need a field in `bvnr_write_flags_t` or a second send function, and the call
after the send costs what the table in §8.4 shows — about 8 ns on a 150-byte
message, the push of the sink included — and allocates nothing.

---

## 9. Shared conformance vectors

`tests/stream_vectors/` holds one set of wire vectors that the C library and
the Rust crate are both held to, so that the C reference and the pure-Rust
codec (`bovnar::codec`) cannot each agree with their own test code and
disagree with each other. `tests/gen_stream_vectors.py` writes them from the
wire conventions of §2.1 and §3.1 alone, never through the library, and lists
each with its expected outcome in `manifest.bvnr`, a Bovnar document: the
messages (channel, length and 64-bit FNV-1a of each), the preamble text, the
framed documents, and the error the C demultiplexer gives a malformed stream.
The set covers a stream after a preamble of text values (a struct with a
unit, a string) and the same stream without it — messages of 150 B, 30 KB and
400 KB, an empty one, one that fills a 65536-byte chunk, two channels
interleaved message by message —, a frame stream of three documents, and
three malformed streams: one truncated inside a message
(`error_octet_stream_truncated`), one with a bad chunk tag
(`error_octet_stream_out_of_sync`) and one over a stated `max_message`
(`error_value_out_of_range`).

The C demultiplexer, batch and fed (`tests/bovnar_stream_vectors_test.c`),
`codec::MuxReader`, `codec::FrameReader` and `live::Demux`
(`rust/tests/test_stream_vectors.rs`) read every vector fed whole, a byte at
a time, cut in two at every position (sampled positions for the large ones)
and in random pieces; `python/tests/test_stream_vectors.py` reads them through
`mux_load`, `DemuxFeed` and `FrameFeed`, whole and in small pieces.
`bvnr_mux_send` and
`codec::MuxWriter`, its preamble included, write the well-formed multiplexed
vectors byte for byte, and `bvnr_frame_write` and `codec::write_frame` the
frame stream, so each side reads what the other writes. `bovnar mux list` and
`bovnar frames list` read the valid vectors and refuse the malformed ones,
except the one over `max_message`, which the CLI cannot set. CTest checks that
the committed files are what the generator writes.

This is a minimal set. The full one — every reader and writer of every layer,
the remaining wire shapes and limits, and where readers deliberately differ —
is still to come.

---

## See also

- [Specification §9 — Octet Streams](03_bovnar_spec.md#9-octet-streams-binary-mode) — the binary substrate three of these four build on
- [Read & Write API](08_bovnar_readwrite_api.md) — the underlying event interface
- [Python Bindings](09_bovnar_python_bindings.md) — the `bovnar.stream` module described in §5
- [Rust Bindings](14_bovnar_rust_bindings.md) — the `bovnar::stream` module
- `include/bovnar_stream.h` — full declarations and per-function contracts
- `tests/bovnar_stream_test.c` — round-trip tests for all four capabilities
- `tests/bovnar_feed_test.c` — the push-fed readers, callback I/O and the flushed writer against the batch functions
- `tests/stream_vectors/` — the shared conformance vectors of §9, and `tests/gen_stream_vectors.py`, which writes them
- `python/bovnar/stream.py` — Python bindings; `python/tests/test_stream.py`,
  `test_stream_io.py`, `test_stream_feed.py`, `test_stream_live.py`

---

*End of Bovnar — Streaming, Framing & Multiplexing (Bovnar spec 1.2).*
