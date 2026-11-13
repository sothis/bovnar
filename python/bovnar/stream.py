# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
Python bindings for the bovnar streaming/framing layer (bovnar_stream.h):

  * Endless streaming   – BVNR_FILESIZE_UNLIMITED (a max_file_size sentinel).
  * Multi-document      – frame_documents / dump_documents / load_documents.
  * Octet multiplexing  – mux_dump / mux_load (cross-chunk reassembly in C).
  * Document-in-document – embed_document / parse_embedded.

and, for input that arrives in pieces (bvnr_open_read_feed and the push-fed
frame reader and demux):

  * DocumentFeed – one document fed in pieces: its events, or its top-level
                   values as each one completes.
  * FrameFeed    – a frame stream fed in pieces: each document's bytes, or
                   each document parsed.
  * DemuxFeed    – a multiplexed document fed in pieces: (channel, payload) per
                   message, and the values before and after the mux stream.

and for output that leaves as it is made (bvnr_write_flush):

  * MuxWriter    – a multiplexed document written a message at a time, each
                   handed to the socket or file as it is sent.
  * FrameWriter  – a frame stream written a document at a time.

These wrap the C functions directly, so behaviour (framing, varint reassembly,
the memory-amplification guards, the strict-vs-resilient document policy) matches
the native library exactly. See doc/10_bovnar_streaming.md for the conventions.

The loaders also take a socket or a binary file object in place of bytes, and
the dumpers a ``file=`` to write to; both then stream through the C library's
callback source and sink instead of holding the whole input or output.
"""

import ctypes
import operator
import weakref
from collections import deque
from collections.abc import Iterable
from functools import lru_cache
from typing import Any, NamedTuple, overload

from ._ffi import get_library
from ._fileio import PullSource, PushSink, is_stream, push_sink
from .enums import ErrorCode, Event
from .exceptions import BovnarParseError, BovnarWriteError, BovnarArgumentError
from .structs import (
    BvnrSink, BvnrSource, BvnrDocStreamOpts, BvnrData,
    BvnrDemuxItem,
    EVENT_CALLBACK_FUNC, ON_DOCUMENT_FUNC, MUX_ON_MSG_FUNC, ON_ERROR_FUNC,
)
from .reader import Reader, UnitPolicy, _read_limit, read_flags
from .writer import Writer

# Mirrors of the C constants (bovnar.h / bovnar_stream.h).
# 0 == no total-byte cap (the default for a zero-initialised max_file_size);
# the text is still bounded by max_text_bytes.
BVNR_FILESIZE_UNLIMITED = 0
BVNR_DOC_DEFAULT_MAX    = 256 * 1024 * 1024
BVNR_MUX_DEFAULT_MAX    = 64 * 1024 * 1024
BVNR_MUX_MAX_MESSAGE    = 65536 - 20
# The largest uint64_t: for max_text_bytes and max_array_items, no cap (0
# selects their finite default of 2147483647). An endless text document read
# with a DocumentFeed needs max_text_bytes=BVNR_NO_LIMIT.
BVNR_NO_LIMIT           = (1 << 64) - 1

__all__ = [
    'BVNR_FILESIZE_UNLIMITED', 'BVNR_DOC_DEFAULT_MAX', 'BVNR_MUX_DEFAULT_MAX',
    'BVNR_MUX_MAX_MESSAGE', 'BVNR_NO_LIMIT',
    'frame_documents', 'dump_documents', 'load_documents',
    'mux_dump', 'mux_load',
    'embed_document', 'parse_embedded',
    'DocumentFeed', 'FrameFeed', 'DemuxFeed',
    'MuxWriter', 'FrameWriter',
    'Message', 'Value', 'DocumentEvent',
]

# Builds a NamedTuple without its Python-level __new__: a third of the cost of a
# message on the hot path otherwise.
_tuple_new = tuple.__new__

# bvnr_frame_reader_next / bvnr_demux_next results (bovnar_stream.h); any
# other value is BVNR_NEXT_ERROR.
_NEXT_NONE  = 0
_NEXT_ITEM  = 1     # BVNR_NEXT_DOCUMENT, BVNR_NEXT_MESSAGE
_NEXT_EVENT = 2


def _as_bytes(x) -> bytes:
    """The bytes of a payload: a bytes-like object, or a str as UTF-8.

    Only what has a buffer is taken, as _held_bytes takes it: bytes() of a
    list, a range or a dict of ints made bytes of its items or keys, and
    bytes(5) five NUL bytes, none of them what the caller meant."""
    if isinstance(x, bytes):
        return x
    if isinstance(x, str):
        return _utf8(x, "a payload")
    try:
        return memoryview(x).tobytes()
    except TypeError:
        raise BovnarArgumentError(
            f"a payload is bytes-like or str, not {type(x).__name__}") from None


def _utf8(text: str, what: str) -> bytes:
    """``text`` as UTF-8; a lone surrogate, which has no UTF-8 form, is
    refused with BovnarArgumentError rather than a UnicodeEncodeError."""
    try:
        return text.encode('utf-8')
    except UnicodeEncodeError:
        raise BovnarArgumentError(
            f"{what} holds a lone surrogate, which has no UTF-8 form: "
            f"{text!r}") from None


# ===========================================================================
# Multi-document record framing
# ===========================================================================

@overload
def frame_documents(docs: Iterable[bytes], *, file: None = None) -> bytes: ...
@overload
def frame_documents(docs: Iterable[bytes], *, file: Any) -> None: ...
def frame_documents(docs, *, file=None) -> bytes | None:
    """Wrap each already-serialised document in a length-prefixed frame and
    return the concatenated frame stream (``"BVF1" + u64 len + payload`` each).

    With ``file`` (a socket or a binary file object) each frame is written
    there as it is made, and nothing is returned."""
    lib = get_library()
    if file is not None:
        out = PushSink(file)
        for d in docs:
            # bvnr_frame_write copies what it writes before it returns, so the
            # bytes object is handed over as it is.
            b = _as_bytes(d)
            if not lib.bvnr_frame_write(ctypes.byref(out.handle), b, len(b)):
                if out.exc is not None:
                    raise out.exc
                raise BovnarWriteError(ErrorCode.WRITING_TO_SINK,
                                       message="bvnr_frame_write failed")
        # As the document dumpers do at finish: what the object buffers
        # reaches its destination before this returns.
        flush = getattr(file, 'flush', None)
        if flush is not None:
            flush()
        return None
    blobs = [_as_bytes(d) for d in docs]
    total = sum(12 + len(b) for b in blobs)
    out   = (ctypes.c_char * (total if total else 1))()
    sink  = BvnrSink()
    lib.bvnr_sink_to_mem(ctypes.byref(sink), ctypes.cast(out, ctypes.c_void_p),
                         total)
    for b in blobs:
        if not lib.bvnr_frame_write(ctypes.byref(sink), b, len(b)):
            # The buffer is sized to fit every frame, so this is a memory
            # sink that ran out: the code says so, never NONE.
            raise BovnarWriteError(ErrorCode.SINK_BUFFER_EXHAUSTED,
                                   message="bvnr_frame_write failed")
    return bytes(out[:total])


@overload
def dump_documents(objs: Iterable[dict], *, pretty: bool = True,
                   file: None = None) -> bytes: ...
@overload
def dump_documents(objs: Iterable[dict], *, pretty: bool = True,
                   file: Any) -> None: ...
def dump_documents(objs, *, pretty: bool = True, file=None):
    """Serialise each dict (via :func:`bovnar.dumps`) and frame the results;
    with ``file``, write each frame there as it is made (see
    :func:`frame_documents`)."""
    from . import dumps  # deferred: avoids an import cycle with the package root
    return frame_documents((dumps(o, pretty=pretty) for o in objs), file=file)


def load_documents(data,
                   *,
                   typed: bool = False,
                   continue_past_failed: bool = False,
                   max_document_size: int = 0,
                   return_errors: bool = False,
                   **options) -> list:
    """Parse a frame stream into a list of documents (one dict per frame).

    With ``continue_past_failed=False`` (default) the first malformed document
    raises :class:`BovnarParseError`. With ``continue_past_failed=True`` every
    frame is processed and a failed document is represented by ``None`` in the
    returned list -- or, with ``return_errors=True``, by the
    :class:`BovnarParseError` it failed with: its ``code`` the reader's error
    code, its ``index`` the document's position in the stream (and in the
    list). ``typed=True`` wraps scalars in :class:`bovnar.Quantity`.

    ``data`` may also be a socket or a binary file object, read until it
    reports end of input; the frames are then parsed one at a time as they are
    pulled, each gathered whole first (up to ``max_document_size``), never the
    stream. For a stream that does not end, or to stream each frame's payload
    into the parser as it arrives, use :class:`FrameFeed`.

    Each document is read under the read options of :meth:`Reader.read_mem`
    (:data:`bovnar.reader.READ_OPTIONS`): ``max_file_size`` caps one document,
    ``max_text_bytes``, ``strict_version``, ``text_only`` and the rest apply
    as there.
    """
    from . import _DictParser, _TypedDictParser  # deferred (import cycle)
    lib  = get_library()
    flags = read_flags('load_documents', options)
    max_document_size = _read_limit('max_document_size', max_document_size)
    if return_errors and not continue_past_failed:
        raise BovnarArgumentError(
            "return_errors applies to continue_past_failed=True: without it "
            "the first failed document raises")
    source = PullSource(data) if is_stream(data) else None
    if source is None:
        data = _as_bytes(data)

    def _new_parser():
        return _TypedDictParser() if typed else _DictParser()

    results: list = []
    holder = {'parser': _new_parser()}
    fail   = {'index': None, 'err': None}
    state: dict[str, Any] = {'exc': None}

    def on_event(ev, d):
        return holder['parser'].on_event(ev, d)

    def on_document(index, ok, err):
        if ok:
            results.append(holder['parser'].result())
        else:
            results.append(_failed_document(lib, index, err)
                           if return_errors else None)
        if not ok and fail['index'] is None:
            fail['index'], fail['err'] = index, err
        holder['parser'] = _new_parser()
        return True

    cb_event = Reader._wrap_callback(on_event)

    def _c_on_doc(_ud, index, ok, err):
        try:
            return bool(on_document(int(index), bool(ok), int(err)))
        except BaseException as exc:               # noqa: BLE001 (propagated below)
            if state['exc'] is None:
                state['exc'] = exc
            return False
    cb_doc = ON_DOCUMENT_FUNC(_c_on_doc)

    flags.on_verified        = cb_event

    opts = BvnrDocStreamOpts()
    opts.flags                = ctypes.pointer(flags)
    opts.on_document          = cb_doc
    opts.continue_past_failed = continue_past_failed
    opts.max_document_size    = max_document_size

    if source is not None:
        src = source.handle
    else:
        # The source borrows the bytes for the doc_stream_read call below,
        # which is synchronous, so they are handed over as they are.
        src = BvnrSource()
        lib.bvnr_source_from_mem(ctypes.byref(src), data, len(data))

    out_count = ctypes.c_uint64(0)
    out_error = ctypes.c_int(0)
    ok = lib.bvnr_doc_stream_read_ex(ctypes.byref(src), ctypes.byref(opts),
                                     ctypes.byref(out_count),
                                     ctypes.byref(out_error))

    if source is not None and source.exc is not None:
        raise source.exc
    cb_state = getattr(cb_event, '_bvnr_state', None)
    if cb_state is not None and cb_state.get('exc') is not None:
        raise cb_state['exc']
    if state['exc'] is not None:
        raise state['exc']
    if not ok:
        # Why the stream ended, with the codes FrameFeed gives. A failed
        # document ends it only without continue_past_failed; with it, a
        # document that failed earlier is a None in the results.
        code = ErrorCode(out_error.value)
        if (fail['err'] is not None and not continue_past_failed
                and fail['err'] == int(code)):
            raise BovnarParseError(
                code, message=f"document {fail['index']} failed to parse")
        msg_b = lib.bvn_error_to_string(int(code))
        raise BovnarParseError(
            code, message="frame stream: " + (msg_b.decode('utf-8') if msg_b
                                               else code.name.lower()))
    return results


def _failed_document(lib, index: int, err: int) -> BovnarParseError:
    """What ``return_errors=True`` puts in place of a failed document: its
    error code, and its index in the stream as ``.index``. The code is all
    the frame reader reports (no position)."""
    code = ErrorCode(err)
    msg_b = lib.bvn_error_to_string(int(code))
    exc = BovnarParseError(
        code, message=f"document {index} failed to parse: "
                      + (msg_b.decode('utf-8') if msg_b else code.name.lower()))
    exc.index = index
    return exc


# ===========================================================================
# Octet multiplexing
# ===========================================================================

@overload
def mux_dump(messages: Iterable[tuple], *, key: str = 'mux',
             preamble: dict | None = None, file: None = None) -> bytes: ...
@overload
def mux_dump(messages: Iterable[tuple], *, key: str = 'mux',
             preamble: dict | None = None, file: Any) -> None: ...
def mux_dump(messages, *, key: str = 'mux', preamble=None, file=None) -> bytes | None:
    """Serialise ``(channel:int, payload:bytes)`` messages as one multiplexed
    octet-stream document under ``key``. Channels may repeat and interleave;
    messages of any size are split across octet chunks and reassembled by
    :func:`mux_load`.

    ``preamble`` is a dict written as ordinary values before the stream opens
    -- what the stream is, say ``{"meta": {"codec": "av1"}}`` -- for a reader
    to have before the first message: :class:`DemuxFeed` with ``values=True``
    hands it over first. :func:`bovnar.loads` reads it as ordinary values,
    beside the stream's raw bytes under ``key``.

    With ``file`` (a socket or a binary file object) the document is written
    there, through the writer's 64 KiB buffer, and nothing is returned."""
    lib  = get_library()
    msgs = [(_mux_channel(ch), _as_bytes(p)) for ch, p in messages]
    if preamble is not None and not isinstance(preamble, dict):
        raise BovnarArgumentError("mux_dump() preamble must be a dict")
    kb = _writer_key(key)

    def write(w):
        _mux_open(w, kb, preamble)
        for ch, payload in msgs:
            # mux_send copies the message into the writer (or writes it
            # straight from here) before it returns; the payload is not kept.
            if not lib.bvnr_mux_send(w._ptr, ch, payload, len(payload)):
                w._raise_error()
        if not lib.bvnr_mux_end(w._ptr):
            w._raise_error()
        w.finish()

    if file is not None:
        w = Writer.to_stream(file, pretty=False)
        try:
            write(w)
        finally:
            w.destroy()
        return None

    total = sum(len(p) for _, p in msgs)
    # Output is the payload plus octet-chunk framing (a few bytes per 64 KiB
    # chunk); start with a tight over-estimate and grow on exhaustion so large
    # or many-chunk messages cannot overflow a fixed guess.
    cap     = total + total // 4000 + len(msgs) * 64 + len(kb) + 8192
    cap_max = max(256 * 1024 * 1024, total * 2 + 1024 * 1024)

    while True:
        w = Writer.to_mem(cap=cap, pretty=False)
        try:
            write(w)
            return w.get_output()
        except BovnarWriteError as exc:
            if exc.code == ErrorCode.SINK_BUFFER_EXHAUSTED and cap < cap_max:
                cap = min(cap * 2, cap_max)
                continue
            raise
        finally:
            w.destroy()


def _writer_key(key: str) -> bytes:
    """The UTF-8 of the key a stream is written under. A key no reader takes
    back is refused as dumps() refuses it, with BovnarArgumentError. (It
    crosses to C as a NUL-terminated string, so a NUL in it used to cut it
    short: the stream was written under another key.)"""
    from . import _check_key  # deferred (import cycle)
    if not isinstance(key, str):
        raise BovnarArgumentError(
            f"a stream key is a str, not {type(key).__name__}")
    _check_key(key)
    return key.encode('utf-8')


def _filter_key(key: str | None) -> bytes | None:
    """The UTF-8 of the key a reader is scoped to (None: every stream). A NUL
    would cut it short as it crosses to C, so the filter would match another
    key; no key holds one, so it is refused."""
    if key is None:
        return None
    if not isinstance(key, str):
        raise BovnarArgumentError(
            f"a stream key is a str, not {type(key).__name__}")
    if '\x00' in key:
        raise BovnarArgumentError(f"a stream key cannot hold NUL: {key!r}")
    return _utf8(key, "a stream key")


def _mux_channel(channel) -> int:
    if type(channel) is int:
        ch = channel
    else:
        # An integer, or what stands for one (operator.index): int(1.9) would
        # send on channel 1, one the caller never named.
        try:
            ch = operator.index(channel)
        except TypeError:
            raise BovnarArgumentError(
                f"a mux channel is an int, not {type(channel).__name__}") from None
    # The channel crosses the FFI as a uint64_t, and ctypes wraps an
    # out-of-range int without a word: channel -1 was written as 2**64-1, and
    # the reader then reported a channel the producer never used.
    if not 0 <= ch < (1 << 64):
        raise BovnarArgumentError(
            f"mux channel {ch} is outside the unsigned 64-bit range")
    return ch


def _mux_open(w: Writer, key: bytes, preamble: dict | None) -> None:
    """The preamble's values, behind the version directive they need, then
    the opener of the mux stream under ``key``."""
    from . import _emit_dict, _uses_spec_1_1, _uses_spec_1_2  # deferred (import cycle)
    if preamble:
        need = (1, 2) if _uses_spec_1_2(preamble) else \
               (1, 1) if _uses_spec_1_1(preamble) else None
        if need:
            w.write_version(*need)
        _emit_dict(w, preamble)
    if not w._lib.bvnr_mux_begin(w._ptr, key):
        w._raise_error()


def mux_load(data, *, max_message: int = 0, key: str | None = None,
             **options) -> list:
    """Demultiplex an octet-stream document, returning a list of
    ``(channel:int, payload:bytes)`` in arrival order.

    By default every octet stream in the document is treated as multiplexed.
    Pass ``key`` to scope the demux to streams opened under that key only, so a
    document mixing a mux stream with ordinary binary octet payloads ignores the
    latter (mirrors ``bvnr_demux_set_key``).

    ``data`` may also be a socket or a binary file object, read until it
    reports end of input and demultiplexed as it is pulled. For a stream that
    does not end, or to read the values around the mux stream, use
    :class:`DemuxFeed`.

    The document is read under the read options of :meth:`Reader.read_mem`
    (:data:`bovnar.reader.READ_OPTIONS`); ``max_file_size`` caps it (``0``,
    the default: no cap)."""
    lib  = get_library()
    flags = read_flags('mux_load', options)
    max_message = _read_limit('max_message', max_message)
    source = PullSource(data) if is_stream(data) else None
    if source is None:
        data = _as_bytes(data)

    results: list = []
    kfilter = _filter_key(key)      # before the demux: it may refuse the key

    if source is None:
        # In memory: drive the push-fed demux with plain calls. A Python
        # callback per message was the cost of the pull-free path for a
        # producer of many small messages; bvnr_demux_next hands each one
        # over without re-entering Python from C, and the payload is
        # borrowed until the next call, so only one copy is made here.
        dm = lib.bvnr_demux_create(MUX_ON_MSG_FUNC(), None, max_message)
        if not dm:
            raise MemoryError("bvnr_demux_create() returned NULL")
        try:
            if kfilter is not None and not lib.bvnr_demux_set_key(dm, kfilter):
                raise MemoryError("bvnr_demux_set_key() failed")
            if not lib.bvnr_demux_open_feed(dm, ctypes.byref(flags), False):
                raise MemoryError("bvnr_demux_open_feed() failed")
            reader = lib.bvnr_demux_get_reader(dm)
            nxt = lib.bvnr_demux_next
            item = BvnrDemuxItem()
            ref = ctypes.byref(item)

            def drain():
                while True:
                    rc = nxt(dm, ref)
                    if rc == _NEXT_ITEM:
                        n = item.length
                        results.append((int(item.channel),
                                        ctypes.string_at(item.data, n) if n else b''))
                    elif rc == _NEXT_NONE:
                        return
                    else:
                        raise _demux_error(lib, lib.bvnr_demux_error(dm), reader)

            if data and not lib.bvnr_demux_feed(dm, data, len(data)):
                raise _demux_error(lib, lib.bvnr_demux_error(dm), reader)
            drain()
            done = lib.bvnr_demux_finish(dm)
            drain()
            if not done:
                raise _demux_error(lib, lib.bvnr_demux_error(dm), reader)
            # A message the stream's end cut short is the demux's error even
            # though the document read to its end, as for the batch path.
            derr = lib.bvnr_demux_error(dm)
            if derr != int(ErrorCode.NONE):
                raise _demux_error(lib, derr, reader)
            return results
        finally:
            lib.bvnr_demux_destroy(dm)

    state: dict[str, Any] = {'exc': None}

    def _on_msg(_ud, channel, data_ptr, length):
        try:
            payload = ctypes.string_at(data_ptr, length) if (data_ptr and length) else b''
            results.append((int(channel), payload))
            return True
        except BaseException as exc:               # noqa: BLE001 (propagated below)
            if state['exc'] is None:
                state['exc'] = exc
            return False
    cb_msg = MUX_ON_MSG_FUNC(_on_msg)

    dm = lib.bvnr_demux_create(cb_msg, None, max_message)
    if not dm:
        raise MemoryError("bvnr_demux_create() returned NULL")
    if kfilter is not None and not lib.bvnr_demux_set_key(dm, kfilter):
        lib.bvnr_demux_destroy(dm)
        raise MemoryError("bvnr_demux_set_key() failed")
    rptr = lib.bvnr_reader_create()
    if not rptr:
        lib.bvnr_demux_destroy(dm)
        raise MemoryError("bvnr_reader_create() returned NULL")
    try:
        # Route every event straight into the C demultiplexer; only the
        # per-message callback above is Python.
        def _trampoline(_ud, ev, dptr):
            return lib.bvnr_demux_on_event(dm, ev, dptr)
        cb_ev = EVENT_CALLBACK_FUNC(_trampoline)
        flags.on_verified        = cb_ev

        ok = lib.bvnr_open_read_source(rptr, ctypes.byref(source.handle),
                                       None, ctypes.byref(flags))
        ok = ok and lib.bvnr_read(rptr)

        if source.exc is not None:
            raise source.exc
        if state['exc'] is not None:
            raise state['exc']
        derr = lib.bvnr_demux_error(dm)
        if derr != int(ErrorCode.NONE):
            raise _demux_error(lib, derr)
        if not ok:
            raise BovnarParseError(
                ErrorCode(lib.bvnr_reader_get_error(rptr)),
                message="multiplexed stream parse error")
        return results
    finally:
        lib.bvnr_reader_destroy(rptr)
        lib.bvnr_demux_destroy(dm)


def _demux_error(lib, err: int, reader=None) -> BovnarParseError:
    """The BovnarParseError for a demux error code, with the position of the
    reader under a push-fed demux when the error is the reader's own."""
    # Name the error the demultiplexer reported. Every code used to be called
    # a "demux desync", including a stream that ended part-way through a
    # message (octet_stream_truncated) and an oversized one
    # (value_out_of_range); only octet_stream_out_of_sync is a desync.
    code = ErrorCode(err)
    msg_b = lib.bvn_error_to_string(int(code))
    what = msg_b.decode('utf-8') if msg_b else code.name.lower()
    if code == ErrorCode.OCTET_STREAM_OUT_OF_SYNC:
        what = f"demux desync ({what})"
    else:
        what = f"demux error: {what}"
    if reader and lib.bvnr_reader_get_error(reader) == err:
        return BovnarParseError(
            code, lib.bvnr_reader_get_error_line(reader),
            lib.bvnr_reader_get_error_column(reader),
            lib.bvnr_reader_get_error_offset(reader),
            lib.bvnr_reader_get_error_byte(reader), what)
    return BovnarParseError(code, message=what)


# ===========================================================================
# Document-in-document
# ===========================================================================

@overload
def embed_document(inner, *, key: str = 'payload', pretty: bool = True,
                   file: None = None) -> bytes: ...
@overload
def embed_document(inner, *, key: str = 'payload', pretty: bool = True,
                   file: Any) -> None: ...
def embed_document(inner, *, key: str = 'payload', pretty: bool = True,
                   file=None) -> bytes | None:
    """Return a complete document that embeds ``inner`` (raw document bytes) as
    the octet-stream payload of ``key``; with ``file`` (a socket or a binary
    file object), write it there and return nothing."""
    lib   = get_library()
    inner = _as_bytes(inner)
    kb    = _writer_key(key)
    if file is not None:
        w = Writer.to_stream(file, pretty=pretty)
        try:
            if not lib.bvnr_embed_document(w._ptr, kb, inner, len(inner)):
                w._raise_error()
            w.finish()
        finally:
            w.destroy()
        return None
    # As in mux_dump: payload + per-chunk octet framing; grow on exhaustion.
    cap     = len(inner) + len(inner) // 4000 + len(kb) + 8192
    cap_max = max(256 * 1024 * 1024, len(inner) * 2 + 1024 * 1024)

    while True:
        w = Writer.to_mem(cap=cap, pretty=pretty)
        try:
            if not lib.bvnr_embed_document(w._ptr, kb, inner, len(inner)):
                w._raise_error()
            w.finish()
            return w.get_output()
        except BovnarWriteError as exc:
            if exc.code == ErrorCode.SINK_BUFFER_EXHAUSTED and cap < cap_max:
                cap = min(cap * 2, cap_max)
                continue
            raise
        finally:
            w.destroy()


def parse_embedded(inner, *, typed: bool = False, **options) -> dict:
    """Parse an embedded document's bytes (as extracted from an octet stream)
    into a dict — a thin wrapper over the C ``bvnr_parse_embedded``. The
    keywords besides ``typed`` are the read options of :meth:`Reader.read_mem`
    (:data:`bovnar.reader.READ_OPTIONS`)."""
    from . import _DictParser, _TypedDictParser  # deferred (import cycle)
    flags  = read_flags('parse_embedded', options)
    lib    = get_library()
    inner  = _as_bytes(inner)
    parser = _TypedDictParser() if typed else _DictParser()

    cb = Reader._wrap_callback(parser.on_event)

    # bvnr_parse_embedded owns its reader and answers one bool, so the error it
    # hit is gone by the time it returns -- and every failure used to be
    # reported as GOT_INCOMPLETE_BVNR_STREAM, an unexpected byte included. The
    # reader does tell its on_error hook what went wrong and where; that is the
    # only place the code survives, so it is caught there. The last report is
    # the one that stopped the parse (under continue_on_error the ones before
    # it were recovered from).
    seen: list = []

    def _on_error(_ud, err, line, column, byte, offset):
        seen[:] = [(int(err), int(line), int(column), int(offset),
                    int(byte))]
    cb_err = ON_ERROR_FUNC(_on_error)

    flags.on_verified        = cb
    flags.on_error           = cb_err

    # The parse is synchronous and bvnr_parse_embedded copies what it keeps.
    ok = lib.bvnr_parse_embedded(inner, len(inner), ctypes.byref(flags))

    cb_state = getattr(cb, '_bvnr_state', None)
    if cb_state is not None and cb_state.get('exc') is not None:
        raise cb_state['exc']
    if not ok:
        if seen:
            code, line, column, offset, byte = seen[0]
            msg_b = lib.bvn_error_to_string(code)
            raise BovnarParseError(
                ErrorCode(code), line, column, offset, byte,
                "embedded document: " + (msg_b.decode('utf-8') if msg_b else ''))
        raise BovnarParseError(ErrorCode.GOT_INCOMPLETE_BVNR_STREAM,
                               message="embedded document parse error")
    return parser.result()


# ===========================================================================
# Push-fed reading: documents, frames and multiplexed streams in pieces
# ===========================================================================
#
# The C feed API keeps all parse state between feeds, so input can arrive in
# pieces of any size -- from a non-blocking socket, an event loop, a queue --
# and nothing is lost or misread where a piece was cut. DocumentFeed and the
# parsing FrameFeed are driven by the reader's callbacks, one ctypes call per
# event, as Reader is. DemuxFeed and the pulling FrameFeed are drained with
# bvnr_demux_next / bvnr_frame_reader_next instead: one plain call per item and
# no callback, which is what keeps a message in the microseconds.

class Message(NamedTuple):
    """One demultiplexed message. A tuple, so ``channel, payload = item``."""
    channel: int
    payload: 'bytes | memoryview'


class Value(NamedTuple):
    """A top-level value of the document, handed over once it is complete."""
    key: str
    value: object


class DocumentEvent(NamedTuple):
    """One verified reader event, as a Reader callback receives it: the event
    and a BvnrData the binding owns (``None`` for the events that carry none)."""
    event: Event
    data: 'BvnrData | None'


@lru_cache(maxsize=None)
def _top_level_class(base):
    """A dict parser that reports each top-level value as it completes and
    then drops it, so an endless document of values costs flat memory."""

    class _TopLevel(base):
        def __init__(self) -> None:
            super().__init__()
            self.completed: list = []
            self._opening = False
            self._open_key = None

        def _done(self, key, value) -> None:
            self._doc.pop(key, None)
            self.completed.append(Value(key, value))

        def _push_value(self, value) -> None:
            at_top = len(self._scope_stack) == 1
            key = self._current_key
            super()._push_value(value)
            if at_top and key is not None:
                if self._opening:           # a struct: complete at its end
                    self._open_key = key
                else:
                    self._done(key, value)

        def _commit_sealed(self, arr, value) -> None:
            at_top = len(self._scope_stack) == 1
            super()._commit_sealed(arr, value)
            if at_top and arr.parent_key is not None:
                self._done(arr.parent_key, value)

        def on_event(self, ev, data) -> bool:
            if ev == Event.STRUCT_START:
                self._opening = True
                try:
                    return super().on_event(ev, data)
                finally:
                    self._opening = False
            if ev == Event.STRUCT_END:
                was_nested = len(self._scope_stack) > 1
                ok = super().on_event(ev, data)
                if (was_nested and len(self._scope_stack) == 1
                        and self._open_key is not None):
                    key, self._open_key = self._open_key, None
                    self._done(key, self._doc[key])
                return ok
            if ev == Event.STREAM_END:
                # An array is sealed by whatever follows it; at the end of the
                # document nothing does.
                self._maybe_seal_array()
            return super().on_event(ev, data)

    return _TopLevel


def _top_level_parser(typed: bool):
    from . import _DictParser, _TypedDictParser  # deferred (import cycle)
    return _top_level_class(_TypedDictParser if typed else _DictParser)()


def _held_bytes(data) -> bytes:
    """The bytes of one feed, as an object nobody can change while C reads it.

    A pulled feed is only referenced by C until it is drained, so a bytearray
    the caller refills (the usual recv_into loop) is copied; bytes are used as
    they are."""
    if isinstance(data, bytes):
        return data
    if isinstance(data, str):
        return data.encode('utf-8')
    try:
        # Any bytes-like object: bytearray, memoryview, array.array, mmap, a
        # NumPy array -- its bytes in memory order.
        return memoryview(data).tobytes()
    except TypeError:
        raise BovnarArgumentError(
            f"feed() takes a bytes-like object, not {type(data).__name__}") from None


class _Feed:
    """What the three feeds share: the item queue, the iterator protocol, the
    memoryview a pulled item may be handed out as, and closing."""

    def __init__(self) -> None:
        self._lib      = get_library()
        self._queue    = deque()
        self._view     = None
        self._view_mem = None
        self._fed: bytes | None = None   # the bytes C reads from, until drained
        self._fed_at: int | None = None  # their address, found on demand
        self._fed_mv: memoryview | None = None  # a view of them, for view=True
        self._failure  = None     # an error kept for the iteration to raise
        self._finished = False
        self._closed   = False

    # -- iteration ---------------------------------------------------------
    def __iter__(self):
        return self

    def __next__(self) -> Any:
        # The item type is the feed's: DocumentEvent (and Value), Message, or
        # a document's bytes. Each class's docstring says which; one annotation
        # cannot say three, so the base leaves it to them.
        if self._closed:
            raise StopIteration
        # A view is the only thing a call has to release; the usual item is
        # bytes, and this runs once per item of a live stream.
        if self._view is not None or self._view_mem is not None:
            self._release()
        if self._queue:
            return self._queue.popleft()
        if self._failure is not None:
            exc, self._failure = self._failure, None
            raise exc
        item = self._pull()
        if item is None:
            raise StopIteration
        return item

    def _pull(self) -> "memoryview | bytes | None":
        """The next item C has for the fed bytes, or None."""
        return None

    # -- memoryview lifetime -----------------------------------------------
    def _fed_address(self, fed: bytes) -> int:
        at = self._fed_at
        if at is None:
            # c_char_p keeps the bytes object alive (no copy), and c_void_p
            # gives the address as an int.
            at = self._fed_at = int(
                ctypes.cast(ctypes.c_char_p(fed), ctypes.c_void_p).value or 0)
        return at

    def _fed_view(self, fed: bytes) -> memoryview:
        """One memoryview of the fed bytes, reused: a view of a view costs
        half what a fresh memoryview(fed) does, and this is per item."""
        view = self._fed_mv
        if view is None or view.obj is not fed:
            view = self._fed_mv = memoryview(fed)
        return view

    def _view_of(self, ptr: int, n: int) -> memoryview:
        """A read-only view of the n bytes at ptr. Where they lie whole in
        the fed bytes it is a slice of those, which keeps them alive by
        itself, so a buffer made from it never makes a call refuse; else it
        is a view of the C object's memory (_hand_out)."""
        fed = self._fed
        if n and fed is not None:
            at = (ptr or 0) - self._fed_address(fed)
            if 0 <= at <= len(fed) - n:
                view = self._fed_view(fed)[at:at + n]
                self._view = view
                return view
        return self._hand_out(ptr, n)

    def _hand_out(self, ptr: int, n: int) -> memoryview:
        # Read-only, as a view of the fed bytes is: the memory is the C
        # object's.
        if not n:
            view = memoryview(b'')
        else:
            mem = (ctypes.c_ubyte * n).from_address(ptr)
            # Whatever is made from the view (memoryview(view),
            # numpy.frombuffer(view)) keeps `mem` alive, and `mem` keeps the
            # feed, so the memory is not freed under it; the weak reference
            # tells whether anything still points there, and frees the C
            # object of a feed closed meanwhile once nothing does.
            # Assigning the attribute is the point (it keeps `mem` alive from
            # the view, which the weak reference below watches); through Any,
            # as ctypes' Array has no such member to type-check against.
            owner_mem: Any = mem
            owner_mem._bvnr_owner = self
            self._view_mem = weakref.ref(mem, self._view_gone)
            view = memoryview(mem).toreadonly()
        self._view = view
        return view

    def _view_gone(self, ref) -> None:
        # The memory of the last view is unreferenced now (weakref callback).
        if ref is not self._view_mem:
            return
        self._view_mem = None
        if self._closed and self._view is None:
            try:
                self._destroy()
            except Exception:                       # noqa: BLE001 (callback)
                pass

    def _view_in_use(self) -> bool:
        """Release the last view; true while something made from it is
        alive. The answer rests on the view's memory alone, so a refused call
        is refused again until that buffer is gone."""
        view = self._view
        if view is not None:
            try:
                view.release()
            except BufferError:
                # A view of the fed bytes (a bytes object) points at nothing
                # C reuses, and its exports keep those bytes alive: not in
                # use. One of the C object's memory is: keep the view, so a
                # later call can release it once the buffer made from it is
                # gone, and report it in use now.
                if not isinstance(view.obj, bytes):
                    return True
            self._view = None
        mem = self._view_mem
        if mem is not None:
            if mem() is not None:
                return True
            self._view_mem = None
        return False

    def _release(self) -> None:
        # The view is over memory C reuses at the next call. Releasing it makes
        # any later use of it raise ValueError instead of reading that memory.
        # A buffer made FROM it (a held export, memoryview(view),
        # numpy.frombuffer(view)) would keep pointing there, so the call is
        # refused: the stream is left as it was, and the call can be repeated
        # once that buffer is gone.
        if self._view_in_use():
            raise BovnarArgumentError(
                "a buffer made from the memoryview of the previous item is "
                "still alive (memoryview(view), numpy.frombuffer(view), an "
                "export); copy it with bytes(view) and drop the buffer before "
                "the next call") from None

    # -- life cycle --------------------------------------------------------
    def _check_open(self) -> None:
        if self._closed:
            raise BovnarArgumentError(f"{type(self).__name__} has been closed")
        if self._finished:
            raise BovnarArgumentError(
                f"{type(self).__name__} has been finished")

    def close(self) -> None:
        """Release the C object. Safe to call twice.

        While a buffer made from the last memoryview is still alive, the C
        object is released when that buffer goes, not now: the feed is closed
        either way, and iterates over nothing more."""
        if self._closed:
            return
        if getattr(self, '_busy', False):
            raise BovnarArgumentError(
                f"{type(self).__name__}.close() from inside one of its own "
                "callbacks")
        self._closed = True
        self._queue.clear()
        self._failure = None
        # Nothing more is handed out; a view still alive keeps what it needs.
        self._fed = None
        if self._view_in_use():
            # The feed no longer holds the view: an export (or a buffer made
            # from it) keeps the memory alive, and _view_gone frees the C
            # object once that goes. Dropping the reference here also keeps
            # the feed from keeping the view, and its memory, alive itself.
            self._view = None
            return
        self._destroy()

    def _destroy(self) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self):
        # Never raise from a finaliser. A view's memory keeps the feed alive
        # (_hand_out), so when this runs nothing points into the C object any
        # more, and it is freed whether or not close() was called.
        if not hasattr(self, '_closed'):
            return
        self._view = None
        self._closed = True
        try:
            self._destroy()
        except Exception:                           # noqa: BLE001 (finaliser)
            pass


# ---------------------------------------------------------------------------
# DocumentFeed
# ---------------------------------------------------------------------------

def _feed_events(queue: deque, events: bool, parser):
    """DocumentFeed's on_verified: each event queued as a DocumentEvent
    (events), and each top-level value the parser completes as a Value."""
    def on_event(ev, data) -> bool:
        if events:
            queue.append(DocumentEvent(ev, data))
        if parser is not None:
            parser.on_event(ev, data)
            if parser.completed:
                queue.extend(parser.completed)
                parser.completed.clear()
        return True
    return on_event


class DocumentFeed(_Feed):
    """One document, fed in pieces of any size (``bvnr_open_read_feed``).

    ``feed(data)`` hands over the next piece and ``finish()`` says the
    document is over; each returns the feed itself, which iterates over the
    items that call completed:

    * with ``events=True`` (the default) a :class:`DocumentEvent` per verified
      event: the events, and the ``BvnrData`` they carry, that
      ``Reader.read_mem(data, on_verified=...)`` delivers for the whole
      document, wherever it was cut;
    * with ``values=True`` a :class:`Value` per top-level value, once it is
      complete -- at its end for a scalar, a struct or an octet stream, at the
      next key for an array. A value handed over is not kept, so an endless
      document of values costs flat memory; ``dict(v for v in items if
      isinstance(v, Value))`` is what :func:`bovnar.loads` returns. An octet
      stream is gathered whole first: for one that does not end, use the
      events, or a :class:`DemuxFeed` for a multiplexed one.

    Running out of input is never an error; ``finish()`` raises
    ``BovnarParseError`` for a document cut off part-way, as ``Reader`` does.
    Any other error raises from the ``feed()`` whose bytes hit it, with the
    position the reader reports, and the items that call had completed are
    dropped. An exception from ``want_unit``, ``on_error`` or
    ``on_unverified`` is re-raised unchanged; they run inside the C call, so
    they may not close or feed this feed. The keyword options are the
    reading options of :meth:`Reader.read_mem`
    (:data:`bovnar.reader.READ_OPTIONS`), with its ``want_unit``,
    ``on_error``, ``on_unverified`` and ``mirror``; ``on_verified`` it has
    not: the items stand in for it. ``on_unverified(event, data)`` sees each
    raw lexer event of the bytes a call hands over, inside that call, and
    ``mirror`` receives every byte the reader consumes, as for
    :meth:`Reader.read_mem`. ``typed`` applies to ``values=True``. One feed
    reads one document.

    The text of a document is capped at 2147483647 bytes
    (``TEXT_DATA_TOO_LONG``) and an array at as many items
    (``TOO_MANY_ARRAY_ITEMS``) unless ``max_text_bytes`` / ``max_array_items``
    say otherwise (``0`` is that default); for a document that does not end,
    pass :data:`BVNR_NO_LIMIT`, and ``max_file_size`` where a cap still
    matters.
    """

    def __init__(self, *,
                 events: bool = True,
                 values: bool = False,
                 typed: bool = False,
                 want_unit=None,
                 on_error=None,
                 on_unverified=None,
                 mirror=None,
                 **options) -> None:
        super().__init__()
        self._busy   = False
        if typed and not values:
            raise BovnarArgumentError("typed applies to values=True")
        if not events and not values:
            raise BovnarArgumentError(
                "DocumentFeed needs events=True or values=True")
        self._reader = Reader()
        self._parser = _top_level_parser(typed) if values else None
        # The callback closes over the queue and the parser, not the feed: it
        # lives on the feed, and a cycle would leave the C reader to the
        # garbage collector.
        self._flags, self._cb_refs = self._reader._build_flags(
            'DocumentFeed', _feed_events(self._queue, events, self._parser),
            on_unverified, want_unit, on_error, options)
        self._mirror = push_sink(mirror) if mirror is not None else None
        if not self._lib.bvnr_open_read_feed(
                self._reader._ptr,
                ctypes.byref(self._mirror.handle) if self._mirror else None,
                ctypes.byref(self._flags)):
            self._reader._raise_error()

    def _check(self, ok: bool, queued: int) -> None:
        mirror = self._mirror
        if mirror is not None and mirror.exc is not None:
            exc, mirror.exc = mirror.exc, None
            self._drop_after(queued)
            raise exc
        for cb in self._cb_refs:
            st = getattr(cb, '_bvnr_state', None)
            if st is not None and st.get('exc') is not None:
                exc, st['exc'] = st['exc'], None
                self._drop_after(queued)
                raise exc
        if not ok:
            self._drop_after(queued)
            self._reader._raise_error()

    def _drop_after(self, queued: int) -> None:
        while len(self._queue) > queued:
            self._queue.pop()

    def feed(self, data) -> 'DocumentFeed':
        """Parse the next piece of the document (bytes-like; it may be reused
        once this returns) and return the feed, iterating over what it
        completed."""
        self._check_open()
        buf = data if isinstance(data, bytes) else _held_bytes(data)
        queued = len(self._queue)
        ok = self._run(self._lib.bvnr_read_feed, buf, len(buf))
        self._check(ok, queued)
        return self

    def _run(self, fn, *args) -> bool:
        # want_unit is the caller's code, run inside the C call: it must not
        # close the feed (or its reader) or feed it again from there.
        if self._busy:
            raise BovnarArgumentError(
                "DocumentFeed is in use by its own callback")
        self._busy = self._reader._busy = True
        try:
            return fn(self._reader._ptr, *args)
        finally:
            self._busy = self._reader._busy = False

    def finish(self) -> 'DocumentFeed':
        """Declare the end of the document; raises if it is incomplete."""
        self._check_open()
        queued = len(self._queue)
        ok = self._run(self._lib.bvnr_read_finish)
        self._finished = True
        self._check(ok, queued)
        return self

    @property
    def recovery_count(self) -> int:
        """How often a ``continue_on_error`` read recovered (Reader's)."""
        return self._reader.recovery_count

    @property
    def skipped_bytes(self) -> int:
        """Bytes a recovering read discarded (Reader's)."""
        return self._reader.skipped_bytes

    @property
    def declared_version(self):
        """The ``#!bovnar M.N`` the document declared, or ``None``."""
        return self._reader.declared_version

    def set_unit_policy(self, policy: UnitPolicy | None) -> None:
        """Install (or, with ``None``, clear) the unit policy of the reader
        behind this feed, as :meth:`Reader.set_unit_policy` does -- the one
        live object that lacked it, Rust's ``live::DocumentFeed`` having had
        it all along.

        Set it before the first ``feed()``: a policy decides what a unit
        string means, and the document has already been half-read by the time
        a later call could change its mind.
        """
        self._reader.set_unit_policy(policy)

    def _destroy(self) -> None:
        self._reader.close()


# ---------------------------------------------------------------------------
# FrameFeed
# ---------------------------------------------------------------------------

class FrameFeed(_Feed):
    """A ``BVF1`` frame stream (:func:`frame_documents`), fed in pieces of any
    size; one C frame reader for the whole stream, however long.

    ``feed(data)`` returns the feed, which iterates over the documents those
    bytes completed:

    * by default each document's bytes (``bytes``, or with ``view=True`` a
      ``memoryview`` valid until the next call on the feed, see
      :class:`DemuxFeed`), drained with ``bvnr_frame_reader_next``, for the
      caller to parse as it likes;
    * with ``parse=True`` each document parsed to a dict, exactly as
      :func:`load_documents` parses it (``typed``, ``continue_past_failed``,
      ``None`` for a failed document), each payload streamed into one reused
      reader as it arrives, so a frame is never held whole. With
      ``return_errors=True`` a failed document is the
      :class:`BovnarParseError` it failed with instead of ``None``, its
      ``index`` the document's position in the stream, as for
      :func:`load_documents`.

    The read options of :meth:`Reader.read_mem`
    (:data:`bovnar.reader.READ_OPTIONS`: ``max_text_bytes``,
    ``max_array_items``, ``strict_version``, ``text_only`` and the rest) apply
    to each parsed document, as for :class:`DocumentFeed`; ``max_file_size``
    caps one document. ``max_document_size`` is that of
    :func:`load_documents` (``0`` selects
    ``BVNR_DOC_DEFAULT_MAX``). A failed document (without
    ``continue_past_failed``) raises ``BovnarParseError`` with its own code, as
    there; a bad magic and a declared length over the cap raise it with
    ``UNEXPECTED_INPUT_BYTE`` and ``VALUE_OUT_OF_RANGE``, as there too.
    Running out of input is never an error, and ``finish()`` raises
    ``GOT_INCOMPLETE_BVNR_STREAM`` for a stream cut inside a frame.

    Unparsed, the fed bytes stay referenced until the documents are drained,
    and feeding again before then raises ``BovnarArgumentError``.
    """

    def __init__(self, *,
                 parse: bool = False,
                 typed: bool = False,
                 continue_past_failed: bool = False,
                 max_document_size: int = 0,
                 view: bool = False,
                 return_errors: bool = False,
                 **options) -> None:
        super().__init__()
        self._fr     = None
        self._parse  = parse
        self._as_view = view
        self._continue = continue_past_failed
        lib = self._lib
        # The read options are checked whether or not they apply, so a typo
        # is a TypeError in either mode.
        read_flags('FrameFeed', options)
        max_document_size = _read_limit('max_document_size', max_document_size)
        if not parse:
            if (typed or continue_past_failed or return_errors
                    or any(options.values())):
                raise BovnarArgumentError(
                    "typed, continue_past_failed, return_errors and the "
                    "read options apply to parse=True")
            self._fr = lib.bvnr_frame_reader_create_pull(max_document_size)
            if not self._fr:
                raise MemoryError("bvnr_frame_reader_create_pull() returned NULL")
            self._doc = ctypes.c_void_p()
            self._len = ctypes.c_uint64()
            self._doc_ref = ctypes.byref(self._doc)
            self._len_ref = ctypes.byref(self._len)
            return
        if view:
            raise BovnarArgumentError("view applies to parse=False")
        if return_errors and not continue_past_failed:
            raise BovnarArgumentError(
                "return_errors applies to continue_past_failed=True: without "
                "it the first failed document raises")

        from . import _DictParser, _TypedDictParser  # deferred (import cycle)
        new_parser = _TypedDictParser if typed else _DictParser
        # The callbacks close over this state and the queue, not the feed:
        # they live on the feed, and a cycle would leave the C frame reader
        # to the garbage collector.
        st = self._state = {'exc': None, 'parser': new_parser(),
                            'index': None, 'err': None}
        queue = self._queue

        def on_event(ev, d):
            return st['parser'].on_event(ev, d)

        def on_document(_ud, index, ok, err):
            try:
                # Without continue_past_failed a failed document is the
                # stream's error, not an item.
                if ok:
                    queue.append(st['parser'].result())
                elif continue_past_failed:
                    queue.append(
                        _failed_document(lib, int(index), int(err))
                        if return_errors else None)
                if not ok and st['index'] is None:
                    st['index'], st['err'] = int(index), int(err)
                st['parser'] = new_parser()
                return True
            except BaseException as exc:           # noqa: BLE001 (re-raised by feed)
                if st['exc'] is None:
                    st['exc'] = exc
                return False

        self._cb_event = Reader._wrap_callback(on_event)
        self._cb_doc   = ON_DOCUMENT_FUNC(on_document)
        # Each document is read under these options (max_file_size caps one
        # document, not the stream; max_document_size bounds a frame).
        flags = read_flags('FrameFeed', options)
        flags.on_verified       = self._cb_event
        opts = BvnrDocStreamOpts()
        opts.flags                = ctypes.pointer(flags)
        opts.on_document          = self._cb_doc
        opts.continue_past_failed = continue_past_failed
        opts.max_document_size    = max_document_size
        self._fr = lib.bvnr_frame_reader_create(ctypes.byref(opts))
        if not self._fr:
            raise MemoryError("bvnr_frame_reader_create() returned NULL")

    def feed(self, data) -> 'FrameFeed':
        """Hand over the next piece of the frame stream and return the feed,
        iterating over the documents it completed."""
        self._check_open()
        self._release()
        if not self._parse:
            if self._fed is not None:
                raise BovnarArgumentError(
                    "the previous feed has not been drained; iterate the "
                    "FrameFeed to its end before feeding it again")
            buf = _held_bytes(data)
            if buf:
                if not self._lib.bvnr_frame_reader_feed(self._fr, buf, len(buf)):
                    self._raise_error()
                self._fed, self._fed_at = buf, None
            return self
        buf = data if isinstance(data, bytes) else _held_bytes(data)
        ok = self._lib.bvnr_frame_reader_feed(self._fr, buf, len(buf))
        # The documents this feed completed before the stream's error come
        # out first; the iteration that reaches the error raises it. (An
        # exception from a callback is not the stream's: it propagates now,
        # whatever its class.)
        self._raise_callback_exc()
        if not ok:
            try:
                self._raise_error()
            except BovnarParseError as exc:
                if self._failure is None:
                    self._failure = exc
        return self

    def _raise_callback_exc(self) -> None:
        cb_state = getattr(self._cb_event, '_bvnr_state', None)
        for st in (cb_state, self._state):
            if st is not None and st.get('exc') is not None:
                exc, st['exc'] = st['exc'], None
                raise exc

    def _check_parse(self, ok: bool) -> None:
        self._raise_callback_exc()
        if not ok:
            self._raise_error()

    def _raise_error(self):
        # A failed document without continue_past_failed ends the stream with
        # that document's own error code; say which document it was, as
        # load_documents does.
        code = ErrorCode(self._lib.bvnr_frame_reader_error(self._fr))
        if code == ErrorCode.NONE:
            code = ErrorCode.GOT_INCOMPLETE_BVNR_STREAM
        st = getattr(self, '_state', None)
        if (st is not None and not self._continue
                and st['err'] == int(code)):
            raise BovnarParseError(
                code, message=f"document {st['index']} failed to parse")
        msg_b = self._lib.bvn_error_to_string(int(code))
        raise BovnarParseError(
            code, message="frame stream: " + (msg_b.decode('utf-8') if msg_b
                                               else code.name.lower()))

    def _pull(self) -> "memoryview | bytes | None":
        fed = self._fed
        if fed is None:
            return None
        rc = self._lib.bvnr_frame_reader_next(self._fr, self._doc_ref,
                                              self._len_ref)
        doc_at = int(self._doc.value or 0)
        if rc == _NEXT_ITEM:
            n = self._len.value
            if self._as_view:
                # A document whole in the fed bytes is a slice of them; one
                # gathered across feeds lies in the frame reader's buffer.
                return self._view_of(doc_at, n)
            if not n:
                return b''
            # As in DemuxFeed: a document that lies whole in the fed bytes is
            # a slice of them, cheaper than a string_at call; one gathered
            # across feeds is copied out of the reader's buffer.
            at = doc_at - self._fed_address(fed)
            if 0 <= at <= len(fed) - n:
                return fed[at:at + n]
            return ctypes.string_at(doc_at, n)
        self._fed = None
        if rc == _NEXT_NONE:
            return None
        self._raise_error()

    def finish(self) -> 'FrameFeed':
        """Declare the end of the stream; raises if it stopped inside a frame."""
        self._check_open()
        self._release()
        if self._fed is not None:
            raise BovnarArgumentError(
                "the last feed has not been drained; iterate the FrameFeed to "
                "its end before finishing it")
        ok = self._lib.bvnr_frame_reader_finish(self._fr)
        self._finished = True
        if self._parse:
            self._check_parse(ok)
        elif not ok:
            self._raise_error()
        return self

    @property
    def count(self) -> int:
        """Documents seen so far (parsed or handed out)."""
        return self._lib.bvnr_frame_reader_count(self._fr) if self._fr else 0

    def _destroy(self) -> None:
        if self._fr:
            self._lib.bvnr_frame_reader_destroy(self._fr)
            self._fr = None
        self._fed = None


# ---------------------------------------------------------------------------
# DemuxFeed
# ---------------------------------------------------------------------------

class DemuxFeed(_Feed):
    """A document carrying a multiplexed octet stream (:func:`mux_dump`), fed
    in pieces of any size and drained with ``bvnr_demux_next``: one plain C
    call per item and no callback.

    ``feed(data)`` returns the feed, which iterates, in document order, over
    what the bytes completed:

    * a :class:`Message` ``(channel, payload)`` per message;
    * with ``values=True``, a :class:`Value` per top-level value of the
      document around the stream (as :class:`DocumentFeed` hands them over),
      so a preamble such as ``.meta = { .codec = "av1"; };`` arrives before the
      first message, and whatever follows the stream after the last one. The
      mux stream itself is not a value;
    * with ``events=True``, a :class:`DocumentEvent` per event of the document
      except the mux stream's chunks: the values' events, the stream's
      ``OCTET_STREAM_START`` and ``OCTET_STREAM_END``, ``STREAM_END``.

    ``key``, ``max_message`` (``0`` selects ``BVNR_MUX_DEFAULT_MAX``), the
    4096-channel limit and every error are those of :func:`mux_load`, raised
    as the same ``BovnarParseError`` codes, from the iteration that reaches
    them (the items before them come out first), or from ``finish()``.
    The read options of :meth:`Reader.read_mem`
    (:data:`bovnar.reader.READ_OPTIONS`) apply to the document:
    ``max_file_size`` caps it (``0``: no cap), and ``max_text_bytes``,
    ``max_array_items``, ``continue_on_error``, ``strict_version``,
    ``text_only`` and the rest are those of :class:`DocumentFeed`, for the
    values around the stream. ``recovery_count``, ``skipped_bytes`` and
    ``declared_version`` are those of the demux's reader.

    The fed bytes are referenced until the feed is drained (iterated until it
    stops), and feeding again before that raises ``BovnarArgumentError``. A
    payload is ``bytes`` by default. With ``view=True`` it is a read-only
    ``memoryview`` of the demux's memory instead -- the fed bytes for a message
    that lay whole in one chunk, the channel's buffer otherwise -- which saves
    the copy and is valid ONLY until the next call on the feed (the next item,
    ``feed``, ``finish`` or ``close``). The feed releases it then, so a late use
    raises ``ValueError``; ``bytes(view)`` keeps a copy. A slice or a buffer
    taken from a view of the demux's memory is not released with it: while
    one is alive, the next call raises ``BovnarArgumentError`` (a view of the
    fed bytes is kept alive by such a buffer itself, so it is never refused).

    ``finish()`` declares the end of the document and raises for one cut off
    part-way or for a message the stream's end cut short; the feed then
    iterates over the closing items (after a message cut short as well: they
    are queued before the error is raised).
    """

    def __init__(self, *,
                 key: str | None = None,
                 max_message: int = 0,
                 values: bool = False,
                 events: bool = False,
                 typed: bool = False,
                 view: bool = False,
                 **options) -> None:
        super().__init__()
        self._dm = None
        flags = read_flags('DemuxFeed', options)
        max_message = _read_limit('max_message', max_message)
        if typed and not values:
            raise BovnarArgumentError("typed applies to values=True")
        lib = self._lib
        dm = lib.bvnr_demux_create(MUX_ON_MSG_FUNC(), None, max_message)
        if not dm:
            raise MemoryError("bvnr_demux_create() returned NULL")
        self._dm = dm
        self._key = _filter_key(key)
        if self._key is not None and not lib.bvnr_demux_set_key(dm, self._key):
            raise MemoryError("bvnr_demux_set_key() failed")
        if not lib.bvnr_demux_open_feed(dm, ctypes.byref(flags),
                                        values or events):
            raise MemoryError("bvnr_demux_open_feed() failed")
        self._as_view = view
        self._events  = events
        self._parser  = _top_level_parser(typed) if values else None
        self._last_key = None
        self._in_mux  = False
        self._item    = BvnrDemuxItem()
        self._item_ref = ctypes.byref(self._item)
        self._next    = lib.bvnr_demux_next
        self._feed_c  = lib.bvnr_demux_feed

    def feed(self, data) -> 'DemuxFeed':
        """Hand over the next piece of the document and return the feed,
        iterating over what it completed. Drain it before feeding again."""
        if self._closed or self._finished:
            self._check_open()
        # Only a view handed out (view=True) has anything to release; the
        # bytes path never does, and this is per feed of a live stream.
        if self._view is not None or self._view_mem is not None:
            self._release()
        if self._fed is not None or self._queue:
            raise BovnarArgumentError(
                "the previous feed has not been drained; iterate the DemuxFeed "
                "to its end before feeding it again")
        buf = data if type(data) is bytes else _held_bytes(data)
        if buf:
            if not self._feed_c(self._dm, buf, len(buf)):
                self._raise_error()
            self._fed = buf
            # The address is wanted only when a message is handed out, and a
            # cast costs more than the rest of a small feed: found on demand.
            self._fed_at = None
        return self

    def __next__(self) -> Any:
        # Message, or DocumentEvent when the document's own events are asked
        # for as well; the docstring above says which and when.
        # The per-message path, kept flat: everything else is _resume's.
        if self._closed:
            raise StopIteration
        if self._view is not None or self._view_mem is not None:
            self._release()
        if self._queue:
            return self._queue.popleft()
        fed = self._fed
        if fed is None:
            raise StopIteration
        rc = self._next(self._dm, self._item_ref)
        if rc == _NEXT_ITEM and not self._as_view:
            item = self._item
            n = item.length
            if not n:
                return _tuple_new(Message, (item.channel, b''))
            # A message that lay whole in the fed bytes is a slice of them, far
            # cheaper than a string_at call; one reassembled in the channel's
            # buffer is copied from there.
            at = item.data - self._fed_address(fed)
            if 0 <= at <= len(fed) - n:
                return _tuple_new(Message, (item.channel, fed[at:at + n]))
            return _tuple_new(Message, (item.channel,
                                        ctypes.string_at(item.data, n)))
        if rc == _NEXT_NONE:
            # The fed bytes are used up; a for loop calls this once more
            # after every feed, so it stays out of _resume's way.
            self._fed = None
            raise StopIteration
        item = self._resume(rc, self._as_view)
        if item is None:
            raise StopIteration
        return item

    def _resume(self, rc: int, as_view: bool):
        """Handle what bvnr_demux_next has just answered `rc`, then go on until
        there is an item to hand out (None when the fed bytes are used up)."""
        item, queue = self._item, self._queue
        while True:
            if rc == _NEXT_ITEM:
                return Message(item.channel, self._payload(item, as_view))
            if rc == _NEXT_EVENT:
                self._on_event(item)
                if queue:
                    return queue.popleft()
            elif rc == _NEXT_NONE:
                self._fed = None
                return None
            else:
                self._fed = None
                self._raise_error()
            rc = self._next(self._dm, self._item_ref)

    def _payload(self, item, as_view: bool):
        n = item.length
        if not as_view:
            return ctypes.string_at(item.data, n) if n else b''
        return self._view_of(item.data, n)

    def _on_event(self, item) -> None:
        ev = Event(item.event)
        data = Reader._snapshot(item.value.contents) if item.value else None
        # Which octet stream is the mux stream, as the C demux decides it: every
        # one without a key, else the one opened under the key. Its two events
        # are not a value of the document.
        edge = False
        if ev == Event.ASSIGNMENT_START:
            self._last_key = data.raw_bytes() if data is not None else None
        elif ev == Event.OCTET_STREAM_START:
            edge = self._in_mux = (self._key is None or
                                   self._last_key == self._key)
        elif ev == Event.OCTET_STREAM_END and self._in_mux:
            edge, self._in_mux = True, False
        if self._events:
            self._queue.append(DocumentEvent(ev, data))
        parser = self._parser
        if parser is not None and not edge:
            parser.on_event(ev, data)
            if parser.completed:
                self._queue.extend(parser.completed)
                parser.completed.clear()

    def _raise_error(self):
        lib = self._lib
        raise _demux_error(lib, lib.bvnr_demux_error(self._dm),
                           lib.bvnr_demux_get_reader(self._dm))

    def _demux_reader(self):
        if not self._dm:
            raise BovnarArgumentError("DemuxFeed has been closed")
        return self._lib.bvnr_demux_get_reader(self._dm)

    @property
    def recovery_count(self) -> int:
        """How often a ``continue_on_error`` read of the document recovered."""
        return self._lib.bvnr_reader_get_recovery_count(self._demux_reader())

    @property
    def skipped_bytes(self) -> int:
        """Bytes a recovering read of the document discarded."""
        return self._lib.bvnr_reader_get_skipped_bytes(self._demux_reader())

    @property
    def declared_version(self):
        """The ``#!bovnar M.N`` the document declared, or ``None``."""
        maj, mn = ctypes.c_uint16(0), ctypes.c_uint16(0)
        if self._lib.bvnr_reader_get_declared_version(
                self._demux_reader(), ctypes.byref(maj), ctypes.byref(mn)):
            return (maj.value, mn.value)
        return None

    def finish(self) -> 'DemuxFeed':
        """Declare the end of the document and return the feed, iterating over
        the closing items; raises if the document or a message is incomplete."""
        self._check_open()
        self._release()
        if self._fed is not None or self._queue:
            raise BovnarArgumentError(
                "the last feed has not been drained; iterate the DemuxFeed to "
                "its end before finishing it")
        lib = self._lib
        ok = lib.bvnr_demux_finish(self._dm)
        self._finished = True
        err = lib.bvnr_demux_error(self._dm)
        if ok:
            # The document read to its end: its closing events are drained
            # now, even when a message was cut short (that is the demux's
            # error, not the document's), so they are queued before the
            # error is raised, as the Rust Demux still hands them out. A
            # message cannot complete at the finish, so none is a view.
            self._fed, self._fed_at = b'', 0
            while (it := self._resume(self._next(self._dm, self._item_ref),
                                      False)) is not None:
                self._queue.append(it)
        if not ok or err != int(ErrorCode.NONE):
            if err == int(ErrorCode.NONE):
                err = int(ErrorCode.GOT_INCOMPLETE_BVNR_STREAM)
            raise _demux_error(lib, err, lib.bvnr_demux_get_reader(self._dm))
        return self

    def _destroy(self) -> None:
        if self._dm:
            self._lib.bvnr_demux_destroy(self._dm)
            self._dm = None
        self._fed = None


# ===========================================================================
# Live writing
# ===========================================================================

class MuxWriter:
    """A multiplexed stream written live to a socket or a binary file
    object, one message at a time: what :func:`mux_dump` writes, for messages
    that are not known in advance.

    The document opens when the writer is made: ``preamble`` (a dict of
    ordinary values, as for :func:`mux_dump`) and then the octet stream under
    ``key``. ``send(channel, payload)`` writes one message. With
    ``flush_each=True`` (the default) each one is handed to the object at
    once (``bvnr_write_flush``: its ``sendall`` or ``write``, then its
    ``flush`` when it has one), the preamble before the first, so a
    :class:`DemuxFeed` at the other end has message N before message N+1 is
    sent. With ``flush_each=False`` the writer gathers its output into a
    64 KiB buffer and :meth:`flush` hands over what has gathered.
    :meth:`close`, or leaving the ``with`` block normally, ends the stream and
    the document; the object itself is left open. The bytes are those
    :func:`mux_dump` writes for the same messages and preamble, however often
    they were flushed.

    An exception the object raises is re-raised, unchanged, from the call
    that pushed. The writer has failed then, and :meth:`close` only releases
    it. Leaving the ``with`` block by an exception does the same: the stream
    is not ended, so a reader sees it cut off rather than complete.
    """

    def __init__(self, file, *, key: str = 'mux',
                 preamble: dict | None = None,
                 flush_each: bool = True) -> None:
        self._w = None
        self._closed = True
        if preamble is not None and not isinstance(preamble, dict):
            raise BovnarArgumentError("MuxWriter() preamble must be a dict")
        kb = _writer_key(key)
        w = Writer.to_stream(file, pretty=False)
        self._w = w
        lib = w._lib
        try:
            _mux_open(w, kb, preamble)
            if flush_each:
                w.flush()
        except BaseException:
            w.destroy()
            self._w = None
            raise
        self._closed = False
        self._lib = lib
        self._ptr = w._ptr
        self._send = lib.bvnr_mux_send
        self._flush = lib.bvnr_write_flush if flush_each else None
        self._stream = w._stream

    @property
    def _writer(self) -> Writer:
        """The open writer; ``_closed`` is what says whether there is one."""
        w = self._w
        if w is None:
            raise BovnarArgumentError("MuxWriter has been closed")
        return w

    def send(self, channel: int, payload) -> None:
        """Write one message on ``channel`` (an unsigned 64-bit int); a
        payload of any size is split over octet chunks as needed."""
        if self._closed:
            raise BovnarArgumentError("MuxWriter has been closed")
        w = self._writer
        sink = w._stream
        if sink is not None and sink.busy:
            w._check_not_in_sink()
        ch = _mux_channel(channel)
        data = payload if type(payload) is bytes else _as_bytes(payload)
        if not self._send(self._ptr, ch, data, len(data)):
            w._raise_error()
        flush = self._flush
        if flush is not None and not flush(self._ptr):
            w._raise_error()

    def flush(self) -> None:
        """Hand everything written so far to the object now, then call its
        ``flush`` when it has one."""
        if self._closed:
            raise BovnarArgumentError("MuxWriter has been closed")
        self._writer.flush()

    @property
    def bytes_written(self) -> int:
        """Bytes of the document so far, handed over or still buffered."""
        if self._w is None:
            raise BovnarArgumentError("MuxWriter has been closed")
        return self._w.bytes_written

    def close(self) -> None:
        """End the stream and the document, flush, and release the writer.
        After a failure it only releases it. Safe to call twice; refused from
        inside the object's own ``write`` or ``flush``."""
        if self._closed:
            return
        w = self._writer
        w._check_not_in_sink()
        self._closed = True
        try:
            if self._lib.bvnr_writer_get_error(self._ptr) == 0:
                if not self._lib.bvnr_mux_end(self._ptr):
                    w._raise_error()
                w.finish()
        finally:
            self._release()

    def _release(self) -> None:
        if self._w is not None:
            self._w._check_not_in_sink()
        self._closed = True
        if self._w is not None:
            self._w.destroy()
            self._w = None

    def __enter__(self) -> 'MuxWriter':
        return self

    def __exit__(self, exc_type, *_) -> None:
        if exc_type is None:
            self.close()
        else:
            self._release()

    def __del__(self) -> None:
        try:
            self._release()
        except Exception:                           # noqa: BLE001 (finaliser)
            pass


class FrameWriter:
    """A frame stream written live to a socket or a binary file object, one
    document at a time: what :func:`frame_documents` and
    :func:`dump_documents` write, for documents that are not known in
    advance.

    ``write(document)`` frames the bytes of one serialised document and
    ``dump(obj)`` serialises a dict first (:func:`bovnar.dumps`). Each frame
    goes to the object as it is made, header then document, and with
    ``flush_each=True`` (the default) the object's ``flush`` follows when it
    has one, so a :class:`FrameFeed` at the other end has each document
    before the next is written. A frame stream needs no terminator: every
    document written is complete, and :meth:`close` (or leaving the ``with``
    block) only flushes the object, which is left open.

    An exception the object raises is re-raised, unchanged, from the call
    that pushed. A frame may then be half written, so the writer refuses
    further documents. The object's own ``write`` and ``flush`` may not
    write to, flush or close the writer (``BovnarArgumentError``): a frame
    written from there would land inside the one being written.
    """

    def __init__(self, file, *, flush_each: bool = True) -> None:
        self._busy = False
        self._sink = PushSink(file)
        self._sink_ref = ctypes.byref(self._sink.handle)
        flush = getattr(file, 'flush', None)
        self._flush_obj = flush
        self._flush_each = flush if flush_each else None
        self._write = get_library().bvnr_frame_write
        self._failed = False
        self._closed = False
        self._count = 0

    @property
    def count(self) -> int:
        """The number of documents written."""
        return self._count

    def write(self, document) -> None:
        """Write one serialised document (bytes, or str as UTF-8) as a
        frame."""
        if self._closed or self._failed:
            raise BovnarArgumentError(
                "FrameWriter has been closed" if self._closed else
                "FrameWriter failed part-way through a frame")
        if self._sink.busy or self._busy:
            self._check_not_in_sink()
        data = document if type(document) is bytes else _as_bytes(document)
        if not self._write(self._sink_ref, data, len(data)):
            self._failed = True
            exc, self._sink.exc = self._sink.exc, None
            if exc is not None:
                raise exc
            raise BovnarWriteError(ErrorCode.WRITING_TO_SINK,
                                   message="bvnr_frame_write failed")
        self._count += 1
        if self._flush_each is not None:
            try:
                self._call_flush()
            except BaseException:
                self._failed = True
                raise

    def _check_not_in_sink(self) -> None:
        if self._sink.busy or self._busy:
            raise BovnarArgumentError(
                "the FrameWriter is in use by its own object: it cannot be "
                "written to, flushed or closed from the object's write() or "
                "flush()")

    def _call_flush(self) -> None:
        flush_obj = self._flush_obj
        if flush_obj is None:                 # _call_flush is only reached with one
            return
        self._busy = True
        try:
            flush_obj()
        finally:
            self._busy = False

    def dump(self, obj: dict, *, pretty: bool = True) -> None:
        """Serialise ``obj`` with :func:`bovnar.dumps` and write it as a
        frame."""
        from . import dumps  # deferred: avoids an import cycle with the package root
        self.write(dumps(obj, pretty=pretty))

    def flush(self) -> None:
        """Call the object's ``flush`` when it has one."""
        if self._closed:
            raise BovnarArgumentError("FrameWriter has been closed")
        self._check_not_in_sink()
        if self._flush_obj is not None:
            self._call_flush()

    def close(self) -> None:
        """Flush the object and refuse further documents. Safe to call
        twice."""
        if self._closed:
            return
        self._check_not_in_sink()
        self._closed = True
        if self._flush_obj is not None and not self._failed:
            self._call_flush()

    def __enter__(self) -> 'FrameWriter':
        return self

    def __exit__(self, *_) -> None:
        self.close()
