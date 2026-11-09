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
        stream.mux_dump(msgs, key='m', preamble={"x": 1}, file=bio)
        assert bio.getvalue() == stream.mux_dump(msgs, key='m', preamble={"x": 1})
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


class TestReviewFixes:
    def test_a_readinto_that_overclaims_is_refused(self):
        class Liar:
            def readinto(self, b):
                b[:6] = b'.a = 1'
                return len(b) + 1000
        with pytest.raises(BovnarArgumentError):
            stream.load_documents(Liar())

    def test_a_write_that_returns_none_took_everything(self):
        class Collect:
            def __init__(self):
                self.data = []

            def write(self, b):
                self.data.append(b.decode('ascii', 'replace'))  # bytes, not a view
        out = Collect()
        bovnar.dump({'a': 1}, out)
        assert bovnar.loads(''.join(out.data).encode()) == {'a': 1}

    def test_a_write_that_takes_nothing_is_an_error(self):
        class Stuck:
            def write(self, b):
                return 0
        with pytest.raises(BovnarArgumentError):
            bovnar.dump({'a': 1}, Stuck())

    def test_frames_to_a_file_are_flushed(self):
        class Buffered(io.BytesIO):
            flushed = 0

            def flush(self):
                self.flushed += 1
        out = Buffered()
        stream.frame_documents([b'.a = 1;'], file=out)
        assert out.flushed == 1

    def test_a_transport_error_is_raised_once(self):
        class Broken:
            def write(self, b):
                raise ConnectionResetError('gone')
        mw = None
        with pytest.raises(ConnectionResetError):
            mw = stream.MuxWriter(Broken())
        sink = Broken()
        mw = stream.MuxWriter(sink, flush_each=False)
        with pytest.raises(ConnectionResetError):
            mw.send(1, b'x' * 70000)              # fills the 64 KiB block
        with pytest.raises(bovnar.BovnarWriteError) as later:
            mw.send(1, b'y')
        assert isinstance(later.value.__cause__, ConnectionResetError)
        mw.close()


class TestThirdReviewFixes:
    """What a third review of the sources and payloads found (2026-11-09)."""

    def test_a_buffered_io_base_with_only_read(self):
        # Its inherited readinto1 calls read1, which it does not have.
        class OnlyRead(io.BufferedIOBase):
            def __init__(self, data):
                self._b = io.BytesIO(data)

            def readable(self):
                return True

            def read(self, n=-1):
                return self._b.read(n)
        assert bovnar.load(OnlyRead(b'.a = 1;')) == {'a': 1}
        blob = stream.frame_documents([b'.a = 1;', b'.b = 2;'])
        assert stream.load_documents(OnlyRead(blob)) == [{'a': 1}, {'b': 2}]
        assert bovnar.canonicalize(OnlyRead(b'.a = 1;')) == \
            bovnar.canonicalize(b'.a = 1;')

    def test_an_mmap_is_bytes_not_a_stream(self):
        # It has read(), and was read from its position: the end once it had
        # been written to, so every loader saw an empty input.
        import mmap

        def written(data):
            mm = mmap.mmap(-1, len(data))
            mm.write(data)
            return mm
        blob = stream.frame_documents([b'.a = 1;', b'.b = 2;'])
        assert stream.load_documents(written(blob)) == [{'a': 1}, {'b': 2}]
        mux = stream.mux_dump([(1, b'abc')])
        assert stream.mux_load(written(mux)) == [(1, b'abc')]
        assert bovnar.canonicalize(written(b'.a = 1;')) == \
            bovnar.canonicalize(b'.a = 1;')

    @pytest.mark.parametrize('bad', [[1, 2, 3], {1: 2}, range(3), None, 1.5, 5])
    def test_a_payload_is_bytes_like(self, bad):
        # bytes() of a list sent its items, of a dict its keys.
        with pytest.raises(BovnarArgumentError):
            stream.mux_dump([(1, bad)])
        with pytest.raises(BovnarArgumentError):
            stream.frame_documents([bad])
        with pytest.raises(BovnarArgumentError):
            stream.embed_document(bad)
        with pytest.raises(BovnarArgumentError):
            stream.FrameWriter(io.BytesIO()).write(bad)
        with pytest.raises(BovnarArgumentError):
            stream.MuxWriter(io.BytesIO()).send(1, bad)
        w = bovnar.Writer.to_mem()
        with pytest.raises(BovnarArgumentError):
            w.write_octets('k', bad)
        assert w.bytes_written == 0               # nothing written first

    def test_payloads_that_are_bytes_like_or_str(self):
        import array
        want = stream.mux_dump([(1, b'hi')])
        for p in (b'hi', bytearray(b'hi'), memoryview(b'hi'), 'hi',
                  array.array('B', b'hi')):
            assert stream.mux_dump([(1, p)]) == want

    @pytest.mark.parametrize('bad', [-1, 1 << 64, 1.5, '10', None])
    def test_a_size_cap_ctypes_would_wrap_is_refused(self, bad):
        with pytest.raises(BovnarArgumentError):
            stream.load_documents(b'', max_document_size=bad)
        with pytest.raises(BovnarArgumentError):
            stream.mux_load(stream.mux_dump([]), max_message=bad)

    def test_a_lone_surrogate_in_a_key_filter(self):
        with pytest.raises(BovnarArgumentError):
            stream.mux_load(stream.mux_dump([]), key='\udc00')

    def test_canonicalize_frees_its_observer_when_the_reader_fails(
            self, monkeypatch):
        from bovnar import reader as reader_mod
        from bovnar._ffi import get_library
        lib = get_library()
        destroyed = []
        real = lib.bvnr_canon_observer_destroy

        def spy(obs):
            destroyed.append(obs)
            return real(obs)

        class NoReader:
            def __init__(self):
                raise MemoryError("no reader")
        monkeypatch.setattr(lib, 'bvnr_canon_observer_destroy', spy)
        monkeypatch.setattr(reader_mod, 'Reader', NoReader)
        with pytest.raises(MemoryError):
            bovnar.canonicalize(b'.a = 1;')
        assert len(destroyed) == 1
