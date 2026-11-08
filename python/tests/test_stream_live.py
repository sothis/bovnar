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

"""Live writing (bvnr_write_flush): Writer.flush, stream.MuxWriter and
stream.FrameWriter. The bytes are those of the batch dumpers however often
they are flushed, a reader at the other end of a socket has each message or
document before the next one is written, and an exception raised by the file
or socket comes out of the call that pushed, unchanged."""

import io
import os
import queue
import socket
import subprocess
import threading
from pathlib import Path

import pytest

from conftest import needs_lib
import bovnar
from bovnar import stream, Writer
from bovnar.exceptions import BovnarArgumentError, BovnarParseError

pytestmark = needs_lib

_REPO = Path(__file__).resolve().parents[2]
_CLI = _REPO / "build" / "bovnar"

_PRE = {"meta": {"codec": "av1", "rate": 30}}
_MSGS = [(1, b"frame-1"), (2, b"audio" * 600), (1, b""), (0, b"x"),
         (9, bytes(range(256)) * 300), (2**64 - 1, b"last channel"),
         (3, os.urandom(200_000))]


class _Counting(io.BytesIO):
    """A BytesIO that counts its writes and flushes."""

    def __init__(self) -> None:
        super().__init__()
        self.writes = 0
        self.flushes = 0

    def write(self, b) -> int:
        self.writes += 1
        return super().write(b)

    def flush(self) -> None:
        self.flushes += 1


class _Fails(io.RawIOBase):
    """Takes `ok` writes, then raises from write (or from flush)."""

    def __init__(self, ok: int, on_flush: bool = False) -> None:
        self.data = bytearray()
        self._ok, self._on_flush = ok, on_flush

    def writable(self) -> bool:
        return True

    def write(self, b) -> int:
        if not self._on_flush:
            if self._ok <= 0:
                raise OSError("boom on write")
            self._ok -= 1
        self.data += b
        return len(b)

    def flush(self) -> None:
        if self._on_flush:
            if self._ok <= 0:
                raise RuntimeError("boom on flush")
            self._ok -= 1

    def close(self) -> None:
        self._on_flush = False                   # not from the finaliser
        super().close()


class _Reader(threading.Thread):
    """The other end of a socketpair: feeds what arrives to `feed` and puts
    every item it hands out on a queue, then None at the end."""

    def __init__(self, sock, feed) -> None:
        super().__init__(daemon=True)
        self.sock, self.feed, self.items = sock, feed, queue.Queue()
        self.error = None

    def run(self) -> None:
        try:
            while True:
                data = self.sock.recv(1 << 16)
                if not data:
                    break
                for item in self.feed.feed(data):
                    self.items.put(item if not isinstance(item, memoryview)
                                   else bytes(item))
            for item in self.feed.finish():
                self.items.put(item)
        except BaseException as exc:          # noqa: BLE001 (reported below)
            self.error = exc
        finally:
            self.items.put(None)


# ---------------------------------------------------------------------------
# Writer.flush
# ---------------------------------------------------------------------------
class TestWriterFlush:
    def _write(self, w, flush) -> None:
        w.write_uint("id", 7, width=32)
        flush(w)
        w.begin_struct("s")
        flush(w)
        w.write_string("name", "café")
        flush(w)
        w.write_bool("on", True)
        w.end_struct()
        flush(w)
        w.finish()

    def test_flush_hands_over_and_changes_no_byte(self):
        ref = Writer.to_mem()
        self._write(ref, lambda w: None)
        want = ref.get_output()

        bio = _Counting()
        w = Writer.to_stream(bio)
        w.write_uint("id", 7, width=32)
        assert bio.getvalue() == b""             # still in the writer's buffer
        w.flush()
        assert bio.getvalue() and want.startswith(bio.getvalue())
        assert len(bio.getvalue()) == w.bytes_written
        assert bio.flushes == 1
        w.destroy()

        for flush in (lambda w: w.flush(), lambda w: None):
            bio = io.BytesIO()
            w = Writer.to_stream(bio)
            self._write(w, flush)
            assert bio.getvalue() == want
            w.flush()                            # after finish: nothing to do
            w.destroy()

    def test_memory_writer_and_destroyed(self):
        w = Writer.to_mem()
        self._write(w, lambda w: w.flush())
        ref = Writer.to_mem()
        self._write(ref, lambda w: None)
        assert w.get_output() == ref.get_output()
        w.destroy()
        with pytest.raises(BovnarArgumentError):
            w.flush()

    def test_exceptions_from_the_object(self):
        w = Writer.to_stream(_Fails(0))
        w.write_uint("id", 7, width=32)
        with pytest.raises(OSError, match="boom on write"):
            w.flush()
        w.destroy()
        w = Writer.to_stream(_Fails(0, on_flush=True))
        w.write_uint("id", 7, width=32)
        with pytest.raises(RuntimeError, match="boom on flush"):
            w.flush()
        w.destroy()


# ---------------------------------------------------------------------------
# MuxWriter
# ---------------------------------------------------------------------------
class TestMuxWriter:
    @pytest.mark.parametrize("flush_each", [True, False])
    @pytest.mark.parametrize("preamble", [None, _PRE])
    def test_bytes_are_mux_dumps(self, flush_each, preamble):
        want = stream.mux_dump(_MSGS, key="media", preamble=preamble)
        bio = io.BytesIO()
        with stream.MuxWriter(bio, key="media", preamble=preamble,
                              flush_each=flush_each) as mux:
            for ch, payload in _MSGS:
                mux.send(ch, payload)
                if flush_each:
                    assert mux.bytes_written == len(bio.getvalue())
        assert bio.getvalue() == want
        assert stream.mux_load(bio.getvalue(), key="media") == _MSGS

    def test_flushed_after_every_send_or_by_hand(self):
        bio = _Counting()
        with stream.MuxWriter(bio, preamble=_PRE) as mux:
            assert bio.getvalue() and bio.flushes == 1   # the preamble is out
            for i, (ch, payload) in enumerate(_MSGS):
                mux.send(ch, payload)
                assert bio.flushes == 2 + i
        bio = _Counting()
        mux = stream.MuxWriter(bio, preamble=_PRE, flush_each=False)
        mux.send(1, b"small")
        assert bio.getvalue() == b"" and bio.flushes == 0
        mux.flush()
        assert bio.getvalue() and bio.flushes == 1
        mux.close()
        assert bio.getvalue() == stream.mux_dump([(1, b"small")], preamble=_PRE)

    def test_payloads_of_every_buffer_type(self):
        bio = io.BytesIO()
        with stream.MuxWriter(bio) as mux:
            mux.send(1, bytearray(b"ab"))
            mux.send(2, memoryview(b"cdef")[1:3])
            mux.send(3, "é")
            mux.send(True, b"bool channel")
        assert stream.mux_load(bio.getvalue()) == [
            (1, b"ab"), (2, b"de"), (3, "é".encode()), (1, b"bool channel")]

    @pytest.mark.parametrize("flush_each", [True, False])
    def test_socketpair_each_message_before_the_next(self, flush_each):
        a, b = socket.socketpair()
        reader = _Reader(b, stream.DemuxFeed(key="media", values=True))
        reader.start()
        try:
            with stream.MuxWriter(a, key="media", preamble=_PRE,
                                  flush_each=flush_each) as mux:
                if not flush_each:
                    mux.flush()
                assert reader.items.get(timeout=10) == ("meta", _PRE["meta"])
                for ch, payload in _MSGS:
                    mux.send(ch, payload)
                    if not flush_each:
                        if len(payload) < 1000:
                            # Batched: nothing goes out until the flush.
                            with pytest.raises(queue.Empty):
                                reader.items.get(timeout=0.05)
                        mux.flush()
                    # Message N has arrived; N+1 is not written yet.
                    item = reader.items.get(timeout=10)
                    assert isinstance(item, stream.Message)
                    assert item == (ch, payload)
            a.shutdown(socket.SHUT_WR)
            assert reader.items.get(timeout=10) is None
        finally:
            reader.join(10)
            a.close()
            b.close()
        assert reader.error is None

    def test_exceptions_from_the_object_propagate(self):
        # Raised while the preamble goes out.
        with pytest.raises(OSError, match="boom on write"):
            stream.MuxWriter(_Fails(0), preamble=_PRE)
        # Raised by the push of a message.
        f = _Fails(1)
        mux = stream.MuxWriter(f, preamble=_PRE)
        with pytest.raises(OSError, match="boom on write"):
            mux.send(1, b"x")
        mux.close()                              # releases, writes nothing
        with pytest.raises(BovnarArgumentError):
            mux.send(1, b"y")
        # Raised by the object's flush.
        mux = stream.MuxWriter(_Fails(1, on_flush=True))
        with pytest.raises(RuntimeError, match="boom on flush"):
            mux.send(1, b"x")
        mux.close()
        # In a batched writer, from the flush that pushed.
        mux = stream.MuxWriter(_Fails(0), flush_each=False)
        mux.send(1, b"x")
        with pytest.raises(OSError, match="boom on write"):
            mux.flush()
        mux.close()
        # From the close of a batched writer, which pushes the rest.
        mux = stream.MuxWriter(_Fails(0), flush_each=False)
        mux.send(1, b"x")
        with pytest.raises(OSError, match="boom on write"):
            mux.close()
        mux.close()

    def test_context_manager_ends_the_stream(self, tmp_path):
        p = tmp_path / "live.bvnr"
        msgs = [(1, b"a" * 10), (42, b"b" * 70_000)]
        with open(p, "wb") as f:
            with stream.MuxWriter(f, preamble=_PRE) as mux:
                for ch, payload in msgs:
                    mux.send(ch, payload)
        data = p.read_bytes()
        assert stream.mux_load(data) == msgs
        assert bovnar.loads(data)["meta"] == _PRE["meta"]
        if _CLI.exists():
            out = subprocess.run([str(_CLI), "mux", "list", str(p)],
                                 capture_output=True, text=True, check=True)
            assert out.stdout.split("\n")[:2] == [
                "channel 1: 10 bytes", "channel 42: 70000 bytes"]

    def test_an_exception_in_the_block_leaves_the_stream_open(self):
        bio = io.BytesIO()
        with pytest.raises(KeyError):
            with stream.MuxWriter(bio) as mux:
                mux.send(1, b"sent")
                raise KeyError("producer failed")
        assert bio.getvalue()                    # the message went out...
        with pytest.raises(BovnarParseError):    # ...but the stream never ended
            stream.mux_load(bio.getvalue())
        with pytest.raises(BovnarArgumentError):
            mux.send(1, b"after")

    def test_arguments(self):
        with pytest.raises(BovnarArgumentError):
            stream.MuxWriter(io.BytesIO(), preamble=[("a", 1)])
        with pytest.raises(BovnarArgumentError):
            stream.MuxWriter(io.StringIO())
        bio = io.BytesIO()
        mux = stream.MuxWriter(bio)
        for bad in (-1, 2**64):
            with pytest.raises(BovnarArgumentError):
                mux.send(bad, b"x")
        mux.send(5, b"still usable")
        mux.close()
        mux.close()
        assert stream.mux_load(bio.getvalue()) == [(5, b"still usable")]
        with pytest.raises(BovnarArgumentError):
            mux.flush()


# ---------------------------------------------------------------------------
# FrameWriter
# ---------------------------------------------------------------------------
class TestFrameWriter:
    _OBJS = [{"id": 1}, {"id": 2, "name": "café"}, {}]

    def test_bytes_are_dump_documents(self):
        bio = _Counting()
        with stream.FrameWriter(bio) as fw:
            for i, obj in enumerate(self._OBJS):
                fw.dump(obj)
                assert bio.flushes == i + 1
        assert bio.getvalue() == stream.dump_documents(self._OBJS)
        assert fw.count == len(self._OBJS)
        docs = [bovnar.dumps(o, pretty=False) for o in self._OBJS]
        bio = io.BytesIO()
        with stream.FrameWriter(bio, flush_each=False) as fw:
            for d in docs:
                fw.write(d)
        assert bio.getvalue() == stream.frame_documents(docs)

    def test_socketpair_each_document_before_the_next(self):
        a, b = socket.socketpair()
        reader = _Reader(b, stream.FrameFeed(parse=True))
        reader.start()
        try:
            with stream.FrameWriter(a) as fw:
                for obj in self._OBJS:
                    fw.dump(obj)
                    assert reader.items.get(timeout=10) == obj
            a.shutdown(socket.SHUT_WR)
            assert reader.items.get(timeout=10) is None
        finally:
            reader.join(10)
            a.close()
            b.close()
        assert reader.error is None

    def test_exceptions_from_the_object_propagate(self):
        fw = stream.FrameWriter(_Fails(1))       # the header goes, not the body
        with pytest.raises(OSError, match="boom on write"):
            fw.write(b".a = 1;")
        with pytest.raises(BovnarArgumentError):
            fw.write(b".a = 1;")                 # a frame is half written
        fw.close()
        fw = stream.FrameWriter(_Fails(0, on_flush=True))
        with pytest.raises(RuntimeError, match="boom on flush"):
            fw.dump({"a": 1})
        with pytest.raises(BovnarArgumentError):
            fw.dump({"a": 2})
        with stream.FrameWriter(io.BytesIO()) as fw:
            pass
        with pytest.raises(BovnarArgumentError):
            fw.write(b".a = 1;")
