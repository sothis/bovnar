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

"""File objects and sockets as sources and sinks (Reader.read_stream,
Writer.to_stream, bovnar.load / dump, and the stream module's loaders and
dumpers): results equal to the in-memory functions', short reads and partial
writes, and an exception raised by the object coming out unchanged."""

import io
import os
import socket
import threading
from pathlib import Path

import pytest

from conftest import needs_lib
import bovnar
from bovnar import stream, Reader
from bovnar.exceptions import BovnarArgumentError, BovnarParseError

pytestmark = needs_lib

_REPO = Path(__file__).resolve().parents[2]
_DOCS = sorted(list((_REPO / "examples").glob("*.bvnr")) +
               list((_REPO / "tests").glob("*.bvnr")))
_SMALL = 2048           # documents up to this size are also read a byte at a time


def _ev(ev, d):
    """What a verified event says, as a comparable value."""
    if d is None:
        return (ev, None)
    vt = d.value_type
    return (ev, d.type, d.raw_bytes(), (vt.family, vt.width, vt.base),
            bytes(d.value_unit), d.frac_str(), d.unit_source_str())


def _read_events(data: bytes) -> list:
    out = []
    Reader().read_mem(data, on_verified=lambda e, d: out.append(_ev(e, d)))
    return out


def _parse_error(fn):
    with pytest.raises(BovnarParseError) as ei:
        fn()
    e = ei.value
    return (e.code, e.line, e.column, e.offset)


# ---------------------------------------------------------------------------
# Sources and sinks: file objects and sockets
# ---------------------------------------------------------------------------
class _Trickle(io.RawIOBase):
    """A raw stream that hands over at most `step` bytes per read."""

    def __init__(self, data: bytes, step: int = 1) -> None:
        self._data, self._at, self._step = data, 0, step

    def readable(self) -> bool:
        return True

    def readinto(self, b) -> int:
        n = min(self._step, len(b), len(self._data) - self._at)
        b[:n] = self._data[self._at:self._at + n]
        self._at += n
        return n


class _ReadOnly:
    """Only read(n), as some file-likes have."""

    def __init__(self, data: bytes) -> None:
        self._f = io.BytesIO(data)

    def read(self, n: int) -> bytes:
        return self._f.read(min(n, 5))


class _PartialWriter:
    """write() takes at most 1000 bytes at a time, as a raw file may."""

    def __init__(self) -> None:
        self.data = bytearray()
        self.flushed = 0

    def write(self, b) -> int:
        b = bytes(b[:1000])
        self.data += b
        return len(b)

    def flush(self) -> None:
        self.flushed += 1


class _Failing(io.RawIOBase):
    """Hands over `first`, then raises on the next read and on every write."""

    def __init__(self, first: bytes = b'') -> None:
        self.first = first

    def readable(self) -> bool:
        return True

    def writable(self) -> bool:
        return True

    def readinto(self, b) -> int:
        if not self.first:
            raise OSError("boom on read")
        n = len(self.first)
        b[:n], self.first = self.first, b''
        return n

    def write(self, b) -> int:
        raise OSError("boom on write")


_OBJ = {"meta": {"codec": "av1", "rate": 30.0}, "values": list(range(2000)),
        "blob": bytes(range(256)) * 900, "name": "ä" * 100}


class TestSourcesAndSinks:

    def test_load_and_dump_match_loads_and_dumps(self, tmp_path):
        want = bovnar.dumps(_OBJ)
        assert len(want) > 3 * 65536                # several pushes
        bio = io.BytesIO()
        bovnar.dump(_OBJ, bio)
        assert bio.getvalue() == want
        assert bovnar.load(io.BytesIO(want)) == bovnar.loads(want)
        assert bovnar.load(io.BytesIO(want), typed=True) == bovnar.loads(want, typed=True)
        p = tmp_path / "doc.bvnr"
        with open(p, 'wb') as f:
            bovnar.dump(_OBJ, f, pretty=False)
        assert p.read_bytes() == bovnar.dumps(_OBJ, pretty=False)
        with open(p, 'rb') as f:
            assert bovnar.load(f) == bovnar.loads(p.read_bytes())
        with open(p, 'rb', buffering=0) as f:     # a raw FileIO
            assert bovnar.load(f) == _OBJ

    @pytest.mark.parametrize("path", _DOCS, ids=lambda p: p.name)
    def test_short_reads(self, path):
        data = path.read_bytes()
        want = _read_events(data)
        for src in (_Trickle(data, 1 if len(data) <= _SMALL else 61),
                    _Trickle(data, 7), _ReadOnly(data),
                    io.BufferedReader(_Trickle(data, 3))):
            got = []
            Reader().read_stream(src, on_verified=lambda e, d: got.append(_ev(e, d)))
            assert got == want

    def test_partial_writes_and_flush(self):
        w = _PartialWriter()
        bovnar.dump(_OBJ, w)
        assert bytes(w.data) == bovnar.dumps(_OBJ) and w.flushed == 1

    def test_errors_are_the_readers(self):
        bad = b'.a = 1;\n.b = {'
        want = _parse_error(lambda: bovnar.loads(bad))
        assert _parse_error(lambda: bovnar.load(io.BytesIO(bad))) == want

    def test_exceptions_in_the_object_propagate(self):
        with pytest.raises(OSError, match="boom on read"):
            bovnar.load(_Failing(b'.a = 1; .b = '))
        with pytest.raises(OSError, match="boom on read"):
            stream.load_documents(_Failing())
        with pytest.raises(OSError, match="boom on read"):
            stream.mux_load(_Failing())
        with pytest.raises(OSError, match="boom on write"):
            bovnar.dump({"a": 1}, _Failing())
        with pytest.raises(OSError, match="boom on write"):
            stream.frame_documents([b'.a = 1;'], file=_Failing())
        with pytest.raises(OSError, match="boom on write"):
            stream.mux_dump([(1, b'x')], file=_Failing())

        class FlushFails(io.BytesIO):
            def flush(self):
                raise RuntimeError("boom on flush")
        with pytest.raises(RuntimeError, match="boom on flush"):
            bovnar.dump({"a": 1}, FlushFails())

    def test_a_callback_exception_from_a_writer_in_the_middle(self):
        class Fails(io.RawIOBase):
            def writable(self):
                return True

            def write(self, b):
                raise KeyError("pushed")
        w = bovnar.Writer.to_stream(Fails())
        with pytest.raises(KeyError):
            for i in range(20_000):           # past the 64 KiB buffer
                w.write_string("k%d" % i, "v" * 10)
        w.destroy()

    def test_text_mode_and_unreadable_objects_are_refused(self, tmp_path):
        p = tmp_path / "t.bvnr"
        p.write_bytes(b'.a = 1;')
        with open(p, 'r') as f:
            with pytest.raises(BovnarArgumentError):
                bovnar.load(f)
        with pytest.raises(BovnarArgumentError):
            bovnar.dump({"a": 1}, object())
        with pytest.raises(BovnarArgumentError):
            bovnar.load(object())

    def test_stream_functions_unchanged_on_files(self, tmp_path):
        docs = [bovnar.dumps({"id": i}) for i in range(3)]
        msgs = [(1, b'a'), (2, b'b' * 100_000)]
        inner = bovnar.dumps({"v": 1})

        bio = io.BytesIO()
        assert stream.frame_documents(docs, file=bio) is None
        assert bio.getvalue() == stream.frame_documents(docs)
        bio = io.BytesIO()
        stream.dump_documents([{"id": 1}], file=bio)
        assert bio.getvalue() == stream.dump_documents([{"id": 1}])
        bio = io.BytesIO()
        stream.mux_dump(msgs, key='m', file=bio)
        assert bio.getvalue() == stream.mux_dump(msgs, key='m')
        bio = io.BytesIO()
        stream.embed_document(inner, file=bio)
        assert bio.getvalue() == stream.embed_document(inner)

        blob = stream.frame_documents(docs)
        assert stream.load_documents(io.BytesIO(blob)) == stream.load_documents(blob)
        assert stream.load_documents(_Trickle(blob, 5)) == stream.load_documents(blob)
        m = stream.mux_dump(msgs, key='m')
        assert stream.mux_load(io.BytesIO(m), key='m') == stream.mux_load(m, key='m') == msgs
        p = tmp_path / "frames.bin"
        with open(p, 'wb') as f:
            stream.frame_documents(docs, file=f)
        with open(p, 'rb') as f:
            assert stream.load_documents(f) == [bovnar.loads(d) for d in docs]

    def test_socketpair(self):
        a, b = socket.socketpair()
        msgs = [(i % 3, os.urandom(i * 997 % 70_000)) for i in range(30)]

        def produce():
            try:
                bovnar.dump({"meta": {"n": len(msgs)}}, a)
                a.sendall(b'\n')
                stream.mux_dump(msgs, key='media', file=a)
            finally:
                a.shutdown(socket.SHUT_WR)
        t = threading.Thread(target=produce)
        t.start()
        try:
            got = stream.mux_load(b, key='media')
        finally:
            t.join()
            a.close()
            b.close()
        assert got == msgs

    def test_socketpair_document_and_frames(self):
        for send, recv in (
            (lambda s: bovnar.dump(_OBJ, s), bovnar.load),
            (lambda s: stream.dump_documents([{"a": 1}, {"b": 2}], file=s),
             stream.load_documents),
        ):
            a, b = socket.socketpair()
            t = threading.Thread(target=lambda: (send(a), a.shutdown(socket.SHUT_WR)))
            t.start()
            try:
                got = recv(b)
            finally:
                t.join()
                a.close()
                b.close()
            assert got in (_OBJ, [{"a": 1}, {"b": 2}])
