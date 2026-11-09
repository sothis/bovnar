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

"""The push-fed readers (DocumentFeed, FrameFeed, DemuxFeed), held to the batch
functions they stand in for: the same events, values, messages, documents and
error codes, wherever the input was cut, and the shared wire vectors."""

import pickle
import random
import socket
from pathlib import Path

import pytest

from conftest import needs_lib
import bovnar
from bovnar import stream, Reader, Event, ErrorCode
from bovnar.exceptions import BovnarArgumentError, BovnarParseError
from bovnar.stream import DemuxFeed, DocumentEvent, DocumentFeed, FrameFeed, Message, Value

pytestmark = needs_lib

_REPO = Path(__file__).resolve().parents[2]
_DOCS = sorted(list((_REPO / "examples").glob("*.bvnr")) +
               list((_REPO / "tests").glob("*.bvnr")))
_EVERY_BYTE_MAX = 2048          # documents up to this size are cut at every byte
_EVERY_CUT_MAX = 600            # ...and up to this size in two at every point


def _pieces(data: bytes, rng: random.Random, top: int = 97):
    at = 0
    while at < len(data):
        n = rng.randint(1, top)
        yield data[at:at + n]
        at += n


def _splits(data: bytes, seed: int = 0):
    """Ways to cut `data`: whole, at every byte and in two at every point when
    small, and in random pieces."""
    yield [data]
    if len(data) <= _EVERY_BYTE_MAX:
        yield [data[i:i + 1] for i in range(len(data))]
    if len(data) <= _EVERY_CUT_MAX:
        for i in range(1, len(data)):
            yield [data[:i], data[i:]]
    rng = random.Random(seed)
    for top in ((3, 64, 1460) if len(data) <= 20_000 else (64, 1460)):
        yield list(_pieces(data, rng, top))


def _ev(ev, d):
    """What a verified event says, as a comparable value."""
    if d is None:
        return (ev, None)
    vt = d.value_type
    return (ev, d.type, d.raw_bytes(), (vt.family, vt.width, vt.base),
            bytes(d.value_unit), d.frac_str(), d.unit_source_str())


def _read_events(data: bytes, **kw) -> list:
    out = []
    Reader().read_mem(data, on_verified=lambda e, d: out.append(_ev(e, d)), **kw)
    return out


def _fed_events(pieces, **kw) -> list:
    out = []
    with DocumentFeed(**kw) as f:
        for p in pieces:
            out.extend(_ev(i.event, i.data) for i in f.feed(p))
        out.extend(_ev(i.event, i.data) for i in f.finish())
    return out


def _fed_values(pieces, **kw) -> list:
    out = []
    with DocumentFeed(events=False, values=True, **kw) as f:
        for p in pieces:
            out.extend(f.feed(p))
        out.extend(f.finish())
    return out


def _parse_error(fn):
    with pytest.raises(BovnarParseError) as ei:
        fn()
    e = ei.value
    return (e.code, e.line, e.column, e.offset)


# ---------------------------------------------------------------------------
# DocumentFeed
# ---------------------------------------------------------------------------
class TestDocumentFeed:

    @pytest.mark.parametrize("path", _DOCS, ids=lambda p: p.name)
    def test_events_match_the_reader_wherever_cut(self, path):
        data = path.read_bytes()
        want = _read_events(data)
        for pieces in _splits(data, seed=len(data)):
            assert _fed_events(pieces) == want

    @pytest.mark.parametrize("path", _DOCS, ids=lambda p: p.name)
    def test_values_match_loads(self, path):
        data = path.read_bytes()
        want = bovnar.loads(data)
        want_typed = bovnar.loads(data, typed=True)
        rng = random.Random(1)
        for pieces in ([data], list(_pieces(data, rng, 50))):
            got = _fed_values(pieces)
            assert all(isinstance(v, Value) for v in got)
            # repr, not ==: a document may hold a NaN
            assert repr(dict(got)) == repr(want)
            assert repr(dict(_fed_values(pieces, typed=True))) == repr(want_typed)

    def test_a_value_is_handed_over_once_it_is_complete(self):
        f = DocumentFeed(events=False, values=True)
        assert list(f.feed(b'.a = 1;')) == [Value('a', 1)]
        assert list(f.feed(b' .b = { .c = 2; ')) == []
        assert list(f.feed(b'.d = "x"; }')) == [Value('b', {'c': 2, 'd': 'x'})]
        # an array may go on in another dimension: the next key closes it
        assert list(f.feed(b'; .m = [1, 2]')) == []
        assert list(f.feed(b'/[3, 4];')) == []
        assert list(f.feed(b' .n = [5];')) == [Value('m', [[1, 2], [3, 4]])]
        assert list(f.finish()) == [Value('n', [5])]

    def test_values_are_not_kept(self):
        # An endless document of values must not grow the feed.
        f = DocumentFeed(events=False, values=True)
        for i in range(200):
            assert list(f.feed(b'.v = %d;' % i)) == [Value('v', i)]
        assert f._parser._doc == {}

    def test_events_and_values_together(self):
        items = list(DocumentFeed(values=True).feed(b'.a = 2;').finish())
        kinds = [i.event if isinstance(i, DocumentEvent) else i for i in items
                 if not (isinstance(i, DocumentEvent) and
                         i.event.name.startswith('TYPE_ANNOTATION'))]
        assert kinds == [Event.STREAM_START, Event.ASSIGNMENT_START, Event.DATA,
                         Value('a', 2), Event.STREAM_END]

    def test_event_data_outlives_the_feed(self):
        f = DocumentFeed()
        items = list(f.feed(b'.k = <float:64,m/s> 9.5;'))
        f.close()
        data = [i.data for i in items if i.event == Event.DATA][0]
        assert data.raw_str() == '9.5' and data.unit_source_str() == 'm/s'

    @pytest.mark.parametrize("bad", [
        b'.a = @;', b'.a = {.b = 1;', b'.a = 1', b'.a = "unterminated',
        b'.a = <uint:8> 300;', b'.a = \x00\x01\x05\x00ab',
    ])
    def test_errors_match_the_reader(self, bad):
        want = _parse_error(lambda: Reader().read_mem(bad))
        for pieces in _splits(bad):
            assert _parse_error(lambda: _fed_events(pieces)) == want

    def test_running_out_of_input_is_not_an_error(self):
        f = DocumentFeed()
        f.feed(b'.a = {')
        with pytest.raises(BovnarParseError) as ei:
            f.finish()
        assert ei.value.code == ErrorCode.GOT_INCOMPLETE_BVNR_STREAM

    def test_options_are_the_readers(self):
        doc = b'.a = @; .b = 2;'
        f = DocumentFeed(events=False, values=True, continue_on_error=True)
        assert dict(f.feed(doc).finish()) == bovnar.loads(doc, continue_on_error=True)
        assert f.recovery_count == 1
        with pytest.raises(BovnarParseError) as ei:
            DocumentFeed(text_only=True).feed(b'.o = \x00\x01\x01\x00x\x00;')
        assert ei.value.code == ErrorCode.OCTET_STREAM_FORBIDDEN
        with pytest.raises(BovnarParseError) as ei:
            DocumentFeed(max_file_size=4).feed(b'.abc = 1;')
        assert ei.value.code == ErrorCode.FILE_TOO_LONG
        f = DocumentFeed()
        f.feed(b'#!bovnar 1.1\n.a = 1;').finish()
        assert f.declared_version == (1, 1)

    def test_an_exception_in_want_unit_propagates(self):
        class Boom(Exception):
            pass

        def want_unit(_d):
            raise Boom()
        with pytest.raises(Boom):
            DocumentFeed(want_unit=want_unit).feed(b'.a = <float:64,km> 1.0;')

    def test_life_cycle(self):
        f = DocumentFeed()
        f.feed(b'.a = 1;').finish()
        with pytest.raises(BovnarArgumentError):
            f.feed(b'.b = 2;')
        with DocumentFeed() as g:
            pass
        with pytest.raises(BovnarArgumentError):
            g.feed(b'')
        g.close()                                 # twice is fine
        with pytest.raises(BovnarArgumentError):
            DocumentFeed().feed(42)


# ---------------------------------------------------------------------------
# DemuxFeed
# ---------------------------------------------------------------------------
def _leb(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _chunk(payload: bytes) -> bytes:
    assert 1 <= len(payload) <= 65536
    return b'\x01' + (len(payload) & 0xFFFF).to_bytes(2, 'little') + payload


def _mux_doc(*chunks, key=b'mux') -> bytes:
    return b'.' + key + b' = \x00' + b''.join(chunks) + b'\x00;\n'


def _demux(pieces, **kw):
    """Messages and values a DemuxFeed hands out, as plain tuples."""
    out = []
    with DemuxFeed(**kw) as dm:
        for p in pieces:
            out.extend(tuple(i) if isinstance(i, Message) else i
                       for i in dm.feed(p))
        out.extend(dm.finish())
    return out


def _demux_or_error(pieces, **kw):
    try:
        return _demux(pieces, **kw)
    except BovnarParseError as e:
        return e.code


def _mux_load_or_error(data, **kw):
    try:
        return stream.mux_load(data, **kw)
    except BovnarParseError as e:
        return e.code


# The shared wire vectors (TODO item 5): packed chunks, interleaved channels,
# a length varint split across chunks, a 65536-byte chunk (length field 0), the
# terminator, a message over 64 KiB, and the malformed and cut-short streams.
_CH4096 = b''.join(_chunk(_leb(c) + _leb(1) + bytes([c & 0xFF])) for c in range(4096))
_VECTORS = {
    'packed': _mux_doc(_chunk(_leb(1) + _leb(3) + b'abc' + _leb(2) + b'de')),
    'interleaved': _mux_doc(_chunk(_leb(1) + _leb(5) + b'ab'),
                            _chunk(_leb(2) + _leb(1) + b'z'),
                            _chunk(_leb(1) + b'cde')),
    'split_length_varint': _mux_doc(_chunk(_leb(7) + b'\xac'),
                                    _chunk(_leb(7) + b'\x02' + b'x' * 300)),
    'chunk_65536': _mux_doc(_chunk(_leb(1) + _leb(65532) + b'q' * 65532)),
    'empty_stream': _mux_doc(),
    'empty_message': _mux_doc(_chunk(_leb(9) + _leb(0))),
    'over_64k': stream.mux_dump([(3, bytes(range(256)) * 600), (4, b'tail')]),
    'channels_4096': _mux_doc(_CH4096),
    'channels_4097': _mux_doc(_CH4096 + _chunk(_leb(4096) + _leb(1) + b'!')),
    'truncated_message': _mux_doc(_chunk(_leb(1) + _leb(5) + b'ab')),
    'overlong_varint': _mux_doc(_chunk(_leb(1) + b'\xff' * 11)),
    'cut_inside_chunk': _mux_doc(_chunk(_leb(1) + _leb(3) + b'abc'))[:-6],
    'cut_before_terminator': _mux_doc(_chunk(_leb(1) + _leb(3) + b'abc'))[:-3],
    'broken_document': b'.a = @;' + _mux_doc(_chunk(_leb(1) + _leb(1) + b'x')),
}


class TestDemuxFeed:

    @pytest.mark.parametrize("name", sorted(_VECTORS))
    def test_vectors_match_mux_load(self, name):
        data = _VECTORS[name]
        want = _mux_load_or_error(data)
        assert want != [] or name == 'empty_stream'
        for pieces in _splits(data, seed=7):
            assert _demux_or_error(pieces) == want, name

    def test_vector_codes(self):
        # The codes themselves, so that agreeing with mux_load cannot hide both
        # being wrong.
        codes = {n: _mux_load_or_error(d) for n, d in _VECTORS.items()}
        assert codes['channels_4097'] == ErrorCode.OCTET_STREAM_OUT_OF_SYNC
        assert codes['truncated_message'] == ErrorCode.OCTET_STREAM_TRUNCATED
        assert codes['overlong_varint'] == ErrorCode.OCTET_STREAM_OUT_OF_SYNC
        assert codes['broken_document'] == ErrorCode.UNEXPECTED_INPUT_BYTE
        assert isinstance(codes['cut_inside_chunk'], ErrorCode)
        assert len(codes['channels_4096']) == 4096
        assert codes['split_length_varint'] == [(7, b'x' * 300)]
        assert codes['interleaved'] == [(2, b'z'), (1, b'abcde')]

    @pytest.mark.parametrize("sizes", [
        [0, 1, 65535, 65536, 65537, 131072],
        [150] * 40,
        [30_000] * 5,
        [400_000],
    ], ids=['boundaries', 'small', 'medium', 'large'])
    def test_messages_match_mux_load(self, sizes):
        msgs = [(i % 5, bytes((k * 7 + i) & 0xFF for k in range(n)))
                for i, n in enumerate(sizes)]
        data = stream.mux_dump(msgs)
        rng = random.Random(len(data))
        for pieces in ([data], list(_pieces(data, rng, 1460)),
                       [data[i:i + 65537] for i in range(0, len(data), 65537)]):
            assert _demux(pieces) == msgs
        assert stream.mux_load(data) == msgs

    def test_preamble_comes_before_the_first_message(self):
        meta = {"meta": {"codec": "av1", "rate": 30.0}, "seq": [1, 2, 3]}
        data = (stream.mux_dump([(1, b'frame-1'), (2, b'x' * 70_000)],
                                key='media', preamble=meta) + b'.after = 5;\n')
        want = [Value('meta', meta['meta']), Value('seq', [1, 2, 3]),
                (1, b'frame-1'), (2, b'x' * 70_000), Value('after', 5)]
        for pieces in _splits(data, seed=3):
            assert _demux(pieces, key='media', values=True) == want
        assert _demux([data], values=True) == want   # no key: every stream is mux
        assert stream.mux_load(data, key='media') == [w for w in want if
                                                      not isinstance(w, Value)]

    def test_preamble_every_byte(self):
        data = stream.mux_dump([(1, b'm')], key='media',
                               preamble={"meta": {"codec": "av1"}})
        for i in range(1, len(data)):
            got = _demux([data[:i], data[i:]], key='media', values=True)
            assert got == [Value('meta', {'codec': 'av1'}), (1, b'm')]

    def test_events_in_document_order(self):
        data = stream.mux_dump([(1, b'a')], key='media',
                               preamble={"m": 1}) + b'.z = 2;'
        dm = DemuxFeed(key='media', events=True)
        kinds = [(i.event.name if isinstance(i, DocumentEvent) else i)
                 for i in dm.feed(data)]
        kinds += [i.event.name for i in dm.finish()]
        kinds = [k for k in kinds if not str(k).startswith('TYPE_ANNOTATION')]
        assert kinds == ['STREAM_START', 'ASSIGNMENT_START', 'DATA',
                         'ASSIGNMENT_START', 'OCTET_STREAM_START', (1, b'a'),
                         'OCTET_STREAM_END', 'ASSIGNMENT_START', 'DATA',
                         'STREAM_END']

    def test_key_scope(self):
        msgs = [(1, b'hi'), (2, b'there')]
        data = (b'.blob = \x00' + _chunk(b'raw bytes') + b'\x00;\n' +
                stream.mux_dump(msgs, key='data'))
        assert _demux([data], key='data') == msgs == stream.mux_load(data, key='data')
        # the other octet stream is an ordinary value
        assert _demux([data], key='data', values=True) == \
            [Value('blob', b'raw bytes')] + msgs
        assert _demux([data], key='other') == [] == stream.mux_load(data, key='other')
        assert _demux([data], key='other', values=True) == \
            [Value(k, v) for k, v in bovnar.loads(data).items()]

    def test_max_message(self):
        data = stream.mux_dump([(1, b'abcdef')])
        assert _demux_or_error([data], max_message=3) == \
            _mux_load_or_error(data, max_message=3) == ErrorCode.VALUE_OUT_OF_RANGE
        assert _demux([data], max_message=6) == [(1, b'abcdef')]

    def test_items_before_an_error_come_out_first(self):
        data = _mux_doc(_chunk(_leb(1) + _leb(1) + b'a'),
                        _chunk(_leb(1) + b'\xff' * 11))
        dm = DemuxFeed()
        it = dm.feed(data)
        assert next(it) == (1, b'a')
        with pytest.raises(BovnarParseError) as ei:
            next(it)
        assert ei.value.code == ErrorCode.OCTET_STREAM_OUT_OF_SYNC
        assert 'desync' in str(ei.value)
        with pytest.raises(BovnarParseError):
            dm.feed(b'more')                      # the stream stays failed

    def test_error_position_is_the_readers(self):
        bad = b'.a = 1;\n.b = @;'
        with pytest.raises(BovnarParseError) as ei:
            _demux([bad])
        assert (ei.value.line, ei.value.column) == (2, 6)

    def test_feed_before_drained_is_refused(self):
        data = stream.mux_dump([(1, b'a'), (2, b'b')])
        dm = DemuxFeed()
        it = dm.feed(data[:len(data) // 2 + 3])
        with pytest.raises(BovnarArgumentError):
            dm.feed(data[len(data) // 2 + 3:])
        with pytest.raises(BovnarArgumentError):
            dm.finish()
        first = list(it)
        rest = list(dm.feed(data[len(data) // 2 + 3:]))
        assert first + rest == [(1, b'a'), (2, b'b')]
        assert list(dm.finish()) == []

    def test_a_bytearray_may_be_reused_after_feeding(self):
        data = stream.mux_dump([(1, b'hello'), (2, b'world')])
        buf = bytearray(data)
        dm = DemuxFeed()
        it = dm.feed(buf)
        buf[:] = b'\xee' * len(buf)               # refilled by the next recv_into
        assert list(it) == [(1, b'hello'), (2, b'world')]

    def test_memoryview_payloads(self):
        big = bytes(range(256)) * 400              # reassembled across chunks
        msgs = [(1, b'abc'), (2, big), (3, b'')]
        data = stream.mux_dump(msgs)
        for pieces in ([data], [data[i:i + 1000] for i in range(0, len(data), 1000)]):
            dm = DemuxFeed(view=True)
            got = []
            for p in pieces:
                for ch, view in dm.feed(p):
                    assert isinstance(view, memoryview) and view.readonly
                    got.append((ch, bytes(view)))
            dm.finish()
            assert got == msgs

    def test_memoryview_is_released_at_the_next_call(self):
        data = stream.mux_dump([(1, b'abc'), (2, b'x' * 70_000), (3, b'z')])
        dm = DemuxFeed(view=True)
        it = dm.feed(data)
        a = next(it)
        kept = bytes(a.payload)
        b = next(it)                               # a's view is released now
        with pytest.raises(ValueError):
            a.payload[0]
        assert kept == b'abc' and len(b.payload) == 70_000
        list(it)
        with pytest.raises(ValueError):
            bytes(b.payload)
        dm.close()

    def test_an_exported_view_stops_the_next_call(self):
        # A message over one chunk is reassembled in the channel's buffer,
        # which the next call reuses.
        data = stream.mux_dump([(1, b'a' * 70_000), (2, b'def')])
        dm = DemuxFeed(view=True)
        it = dm.feed(data)
        hold = pickle.PickleBuffer(next(it).payload)   # holds a buffer export
        with pytest.raises(BovnarArgumentError):
            next(it)
        hold.release()                             # the last reference to it
        assert next(it) == (2, memoryview(b'def'))
        assert list(it) == []

    def test_an_exported_view_of_the_fed_bytes_stops_nothing(self):
        # A message whole in the fed bytes is a view of them, which the
        # export keeps alive by itself (doc/09 §4.7).
        data = stream.mux_dump([(1, b'abc'), (2, b'def')])
        dm = DemuxFeed(view=True)
        it = dm.feed(data)
        a = next(it)
        hold = pickle.PickleBuffer(a.payload)
        assert next(it) == (2, memoryview(b'def'))
        assert bytes(hold) == b'abc'
        assert list(it) == []

    def test_closed(self):
        dm = DemuxFeed()
        dm.close()
        with pytest.raises(BovnarArgumentError):
            dm.feed(b'.mux = \x00')
        with DemuxFeed() as dm2:
            dm2.feed(b'.mux = \x00')
        with pytest.raises(BovnarArgumentError):
            dm2.finish()


# ---------------------------------------------------------------------------
# FrameFeed
# ---------------------------------------------------------------------------
_GOOD = [bovnar.dumps({"id": i, "name": "n" * i}) for i in range(5)]


class TestFrameFeed:

    def _pulled(self, pieces, **kw):
        out = []
        with FrameFeed(**kw) as fr:
            for p in pieces:
                out.extend(bytes(d) for d in fr.feed(p))
            fr.finish()
            assert fr.count == len(out)
        return out

    def _parsed(self, pieces, **kw):
        out = []
        with FrameFeed(parse=True, **kw) as fr:
            for p in pieces:
                out.extend(fr.feed(p))
            out.extend(fr.finish())
        return out

    def test_documents_wherever_cut(self):
        blob = stream.frame_documents(_GOOD + [b''])
        want = stream.load_documents(blob)
        for pieces in _splits(blob, seed=5):
            assert self._pulled(pieces) == _GOOD + [b'']
            assert self._parsed(pieces) == want

    def test_memoryview_documents(self):
        blob = stream.frame_documents(_GOOD)
        for pieces in ([blob], [blob[i:i + 10] for i in range(0, len(blob), 10)]):
            assert self._pulled(pieces, view=True) == _GOOD

    def test_large_documents(self):
        docs = [bovnar.dumps({"data": list(range(12_000))}), b'.x = 1;']
        assert len(docs[0]) > 65536
        blob = stream.frame_documents(docs)
        assert self._pulled([blob[i:i + 1460] for i in range(0, len(blob), 1460)]) == docs
        assert self._parsed([blob[i:i + 1460] for i in range(0, len(blob), 1460)]) == \
            stream.load_documents(blob)

    def test_a_failed_document_among_good_ones(self):
        blob = stream.frame_documents([_GOOD[0], b'.broken = @@@;', _GOOD[1]])
        for pieces in _splits(blob, seed=9):
            assert self._parsed(pieces, continue_past_failed=True) == \
                stream.load_documents(blob, continue_past_failed=True) == \
                [{"id": 0, "name": ""}, None, {"id": 1, "name": "n"}]
            with pytest.raises(BovnarParseError) as ei:
                self._parsed(pieces)
            assert ei.value.code == ErrorCode.UNEXPECTED_INPUT_BYTE
            assert 'document 1' in str(ei.value)
            assert self._pulled(pieces)[1] == b'.broken = @@@;'
        with pytest.raises(BovnarParseError) as ei:
            stream.load_documents(blob)
        assert ei.value.code == ErrorCode.UNEXPECTED_INPUT_BYTE

    def test_typed(self):
        blob = stream.frame_documents([b'.port = <uint:16> 8080;\n'])
        assert self._parsed([blob], typed=True) == stream.load_documents(blob, typed=True)

    def test_max_document_size(self):
        blob = stream.frame_documents([b'.a = 1;', b'.b = 12345;'])
        for kw in ({}, {'parse': True}):
            with pytest.raises(BovnarParseError) as ei:
                fr = FrameFeed(max_document_size=8, **kw)
                for p in [blob[i:i + 1] for i in range(len(blob))]:
                    list(fr.feed(p))
            assert ei.value.code == ErrorCode.VALUE_OUT_OF_RANGE
        assert self._pulled([blob], max_document_size=11) == [b'.a = 1;', b'.b = 12345;']
        with pytest.raises(BovnarParseError):
            stream.load_documents(blob, max_document_size=8)

    @pytest.mark.parametrize("cut", [1, 4, 11, 12, 15])
    def test_truncation(self, cut):
        blob = stream.frame_documents([b'.a = 1;'])[:cut]
        for kw in ({}, {'parse': True}):
            fr = FrameFeed(**kw)
            assert list(fr.feed(blob)) == []          # running out is not an error
            with pytest.raises(BovnarParseError) as ei:
                fr.finish()
            assert ei.value.code == ErrorCode.GOT_INCOMPLETE_BVNR_STREAM
        with pytest.raises(BovnarParseError):
            stream.load_documents(blob)

    def test_bad_magic(self):
        for kw in ({}, {'parse': True}):
            with pytest.raises(BovnarParseError) as ei:
                list(FrameFeed(**kw).feed(b'BVF2' + bytes(8)))
            assert ei.value.code == ErrorCode.UNEXPECTED_INPUT_BYTE

    def test_feed_before_drained_is_refused(self):
        blob = stream.frame_documents(_GOOD)
        fr = FrameFeed()
        first = list(fr.feed(blob[:30]))
        fr.feed(blob[30:60])
        with pytest.raises(BovnarArgumentError):
            fr.feed(blob[60:])
        with pytest.raises(BovnarArgumentError):
            fr.finish()
        rest = list(fr) + list(fr.feed(blob[60:]))
        assert first + rest == _GOOD
        fr.finish()

    def test_an_exception_in_parsing_propagates(self, monkeypatch):
        class Boom(Exception):
            pass
        fr = FrameFeed(parse=True)

        def explode(*_a):
            raise Boom()
        monkeypatch.setattr(fr._state['parser'], 'on_event', explode)
        with pytest.raises(Boom):
            fr.feed(stream.frame_documents([b'.a = 1;']))

    def test_option_combinations(self):
        with pytest.raises(BovnarArgumentError):
            FrameFeed(typed=True)
        with pytest.raises(BovnarArgumentError):
            FrameFeed(parse=True, view=True)


# ---------------------------------------------------------------------------
# A socket, read the non-blocking way
# ---------------------------------------------------------------------------
class TestSocketFeed:

    def test_a_socket_fed_to_a_demux_feed(self):
        # The non-blocking way: whatever recv returns is fed, nothing waits.
        a, b = socket.socketpair()
        data = stream.mux_dump([(1, b'x' * 5000), (2, b'y')], key='m',
                               preamble={"meta": {"codec": "av1"}})
        a.sendall(data)
        a.shutdown(socket.SHUT_WR)
        got = []
        buf = bytearray(1460)
        with DemuxFeed(key='m', values=True) as dm:
            while (n := b.recv_into(buf)):
                got.extend(dm.feed(memoryview(buf)[:n]))
            got.extend(dm.finish())
        a.close()
        b.close()
        assert got == [Value('meta', {'codec': 'av1'}), (1, b'x' * 5000), (2, b'y')]


# ---------------------------------------------------------------------------
# Fixes from the streaming review
# ---------------------------------------------------------------------------
class TestReviewFixes:
    def test_a_view_keeps_its_feed_alive(self):
        # A reassembled message is viewed in the demux's own buffer: dropping
        # the feed must not free that buffer under the view.
        big = bytes(range(256)) * 800
        dm = DemuxFeed(view=True)
        first = next(iter(dm.feed(stream.mux_dump([(1, big)]))))
        del dm
        import gc
        gc.collect()
        assert bytes(first.payload) == big

    def test_a_buffer_made_from_a_view_is_detected(self):
        dm = DemuxFeed(view=True)
        it = dm.feed(stream.mux_dump([(1, b'x' * 70000), (2, b'y' * 70000)]))
        first = next(it)
        held = memoryview(first.payload)          # no export on the view
        with pytest.raises(BovnarArgumentError):
            next(it)
        assert bytes(held[:1]) == b'x'
        del held
        assert next(it).channel == 2              # the call can be repeated
        dm.close()

    def test_close_with_a_buffer_alive_defers_the_free(self):
        fr = FrameFeed(view=True)
        wire = stream.frame_documents([b'.a = 1;'])
        it = fr.feed(bytearray(wire[:5]))
        assert list(it) == []
        doc = next(iter(fr.feed(bytearray(wire[5:]))))
        held = memoryview(doc)
        fr.close()                                # does not raise
        assert bytes(held) == b'.a = 1;'
        del held, doc

    def test_a_failed_document_raises_from_the_iteration(self):
        wire = stream.frame_documents([b'.a = 1;', b'.b = = 1;', b'.c = 3;'])
        fr = FrameFeed(parse=True)
        it = fr.feed(wire)                        # does not raise
        assert next(it) == {'a': 1}
        with pytest.raises(BovnarParseError):
            next(it)
        assert list(it) == []

    def test_an_int_is_not_a_payload(self):
        with pytest.raises(BovnarArgumentError):
            stream.frame_documents([5])


class TestSecondReviewFixes:
    """What a second review of the feeds found (2026-11-09)."""

    def test_a_refused_call_is_refused_again(self):
        # The refusal used to forget the view, so a repeated call went ahead
        # and freed or reused the memory the held buffer still read.
        dm = DemuxFeed(view=True)
        it = dm.feed(stream.mux_dump([(1, b'x' * 70000), (1, b'y' * 70000)]))
        first = next(it)
        held = memoryview(first.payload)
        for _ in range(3):
            with pytest.raises(BovnarArgumentError):
                next(it)
        dm.close()                                # deferred: held is alive
        assert dm._dm is not None
        assert bytes(held[:3]) == b'xxx' and bytes(held[-3:]) == b'xxx'
        del held, first
        import gc
        gc.collect()
        assert dm._dm is None                     # freed when the buffer went

    def test_a_closed_feed_hands_out_nothing(self):
        dm = DemuxFeed(view=True)
        it = dm.feed(stream.mux_dump([(1, b'abc'), (2, b'def')]))
        first = next(it)
        exp = pickle.PickleBuffer(first.payload)  # an export of the view
        dm.close()
        del exp
        assert list(it) == []
        fr = FrameFeed(view=True)
        it = fr.feed(stream.frame_documents([b'.a=1;', b'.b=2;']))
        d0 = next(it)
        exp = pickle.PickleBuffer(d0)
        fr.close()
        assert list(it) == []
        with pytest.raises(BovnarArgumentError):
            fr.feed(b'BVF1')
        del exp, d0

    def test_a_view_into_the_fed_bytes_outlives_close(self):
        fr = FrameFeed(view=True)
        doc = next(iter(fr.feed(bytearray(stream.frame_documents([b'.a = 1;'])))))
        held = memoryview(doc)
        fr.close()
        import gc
        gc.collect()
        assert bytes(held) == b'.a = 1;'

    def test_close_or_feed_from_want_unit_is_refused(self):
        for action in ('close', 'feed'):
            f = None

            def want(_d):
                f.close() if action == 'close' else f.feed(b'.b = 2;')
            f = DocumentFeed(want_unit=want)
            with pytest.raises(BovnarArgumentError, match='own callback'):
                f.feed(b'.a = <float:64,km> 1.0; .c = 3;')
            f.close()                             # after the call: fine

    def test_a_parse_error_from_a_callback_propagates_from_feed(self):
        # A parser exception, even a BovnarParseError, is the caller's, not
        # the stream's: it is raised by the feed() that ran the callback.
        fr = FrameFeed(parse=True)
        boom = BovnarParseError(ErrorCode.NONE, message='from the parser')

        def raising(*_a):
            raise boom
        fr._state['parser'].on_event = raising
        with pytest.raises(BovnarParseError, match='from the parser'):
            fr.feed(stream.frame_documents([b'.a = 1;']))

    def test_a_channel_is_an_int(self):
        with pytest.raises(BovnarArgumentError):
            stream.mux_dump([(1.9, b'x')])
        import operator

        class Index:
            def __index__(self):
                return 3
        assert stream.mux_load(stream.mux_dump([(Index(), b'x')])) == [(3, b'x')]
        assert operator.index(Index()) == 3

    def test_any_bytes_like_object_is_fed(self):
        import array
        wire = stream.mux_dump([(1, b'abcd')])
        got = list(DemuxFeed().feed(array.array('B', wire)))
        assert got == [Message(1, b'abcd')]

    def test_options_that_would_be_ignored_are_refused(self):
        with pytest.raises(BovnarArgumentError):
            DemuxFeed(typed=True)
        with pytest.raises(BovnarArgumentError):
            DocumentFeed(typed=True)
        with pytest.raises(BovnarArgumentError):
            DocumentFeed(events=False, values=False)

    def test_mux_load_refuses_a_bad_key_before_it_allocates(self):
        with pytest.raises(BovnarArgumentError):
            stream.mux_load(stream.mux_dump([(1, b'x')]), key='a\x00b')

    def test_the_closing_items_survive_a_truncated_message(self):
        # A message cut short is the demux's error, not the document's: the
        # document's closing events are still handed out after finish raises.
        doc = _mux_doc(_chunk(_leb(4) + _leb(1000) + b'0123456789')) + b'.n = [1, 2];'
        dm = DemuxFeed(events=True, values=True)
        before = list(dm.feed(doc))
        assert not any(isinstance(i, Message) for i in before)
        with pytest.raises(BovnarParseError) as ei:
            dm.finish()
        assert ei.value.code == ErrorCode.OCTET_STREAM_TRUNCATED
        after = list(dm)
        assert Value('n', [1, 2]) in after
        assert after[-1].event == Event.STREAM_END


class TestReadLimits:
    """max_text_bytes and max_array_items (2.1.0): 0 keeps the library's
    default of 2147483647, BVNR_NO_LIMIT lifts it, and a small cap fails
    where Reader.read_mem fails, fed or whole."""

    DOC = b'.a = 1; .b = [1, 2, 3];'

    def _whole(self, **kw):
        try:
            Reader().read_mem(self.DOC, **kw)
            return None
        except BovnarParseError as e:
            return e.code

    def _fed(self, **kw):
        try:
            f = DocumentFeed(**kw)
            list(f.feed(self.DOC[:9]))
            list(f.feed(self.DOC[9:]))
            list(f.finish())
            return None
        except BovnarParseError as e:
            return e.code

    @pytest.mark.parametrize('kw, want', [
        ({'max_text_bytes': 10}, ErrorCode.TEXT_DATA_TOO_LONG),
        ({'max_text_bytes': len(DOC)}, None),
        ({'max_text_bytes': stream.BVNR_NO_LIMIT}, None),
        ({'max_array_items': 2}, ErrorCode.TOO_MANY_ARRAY_ITEMS),
        ({'max_array_items': 3}, None),
        ({'max_array_items': stream.BVNR_NO_LIMIT}, None),
    ])
    def test_reader_and_feed_agree(self, kw, want):
        assert self._whole(**kw) == want
        assert self._fed(**kw) == want

    def test_demux_and_frame_feeds_take_them(self):
        doc = stream.mux_dump([(1, b'x')], preamble={'n': [1, 2, 3]})
        with pytest.raises(BovnarParseError) as ei:
            list(DemuxFeed(values=True, max_array_items=2).feed(doc))
        assert ei.value.code == ErrorCode.TOO_MANY_ARRAY_ITEMS
        assert Message(1, b'x') in list(DemuxFeed(max_array_items=3).feed(doc))
        wire = stream.frame_documents([self.DOC])
        fr = FrameFeed(parse=True, max_text_bytes=10)
        with pytest.raises(BovnarParseError) as ei:
            list(fr.feed(wire))
        assert ei.value.code == ErrorCode.TEXT_DATA_TOO_LONG
        assert list(FrameFeed(parse=True, max_text_bytes=len(self.DOC))
                    .feed(wire)) == [{'a': 1, 'b': [1, 2, 3]}]
        with pytest.raises(BovnarArgumentError):
            FrameFeed(max_text_bytes=10)      # applies to parse=True

    @pytest.mark.parametrize('bad', [-1, 1 << 64, 1.5, '10', None])
    def test_a_value_ctypes_would_wrap_is_refused(self, bad):
        with pytest.raises(BovnarArgumentError):
            Reader().read_mem(self.DOC, max_text_bytes=bad)
        with pytest.raises(BovnarArgumentError):
            DocumentFeed(max_array_items=bad)


class TestThirdReviewFixes:
    """What a third review of the feeds found (2026-11-09)."""

    @pytest.mark.parametrize('bad', [-1, 1 << 64, 1.5, '10', None])
    def test_a_size_cap_ctypes_would_wrap_is_refused(self, bad):
        # -1 went to C as 2**64 - 1, which lifted the cap.
        with pytest.raises(BovnarArgumentError):
            FrameFeed(max_document_size=bad)
        with pytest.raises(BovnarArgumentError):
            FrameFeed(parse=True, max_document_size=bad)
        with pytest.raises(BovnarArgumentError):
            DemuxFeed(max_message=bad)

    @pytest.mark.parametrize('make', [
        lambda: DocumentFeed(),
        lambda: DocumentFeed(events=False, values=True),
        lambda: FrameFeed(parse=True),
        lambda: FrameFeed(),
        lambda: DemuxFeed(values=True),
    ])
    def test_a_dropped_feed_is_freed_without_the_collector(self, make):
        import gc
        import weakref
        gc.disable()
        try:
            feed = make()
            ref = weakref.ref(feed)
            del feed
            assert ref() is None
        finally:
            gc.enable()

    def test_a_frame_view_of_the_fed_bytes_stops_nothing(self):
        blob = stream.frame_documents([b'.a = 1;', b'.b = 2;'])
        fr = FrameFeed(view=True)
        it = fr.feed(blob)
        first = next(it)
        held = memoryview(first)                   # a buffer made from it
        assert bytes(next(it)) == b'.b = 2;'
        assert bytes(held) == b'.a = 1;'
        assert list(it) == []

    def test_a_frame_view_of_the_reader_buffer_still_stops(self):
        # A document cut across two feeds is gathered in the frame reader's
        # buffer, which the next call reuses.
        blob = stream.frame_documents([b'.a = 1;', b'.b = 2;'])
        fr = FrameFeed(view=True)
        assert list(fr.feed(blob[:15])) == []
        first = next(iter(fr.feed(blob[15:])))
        held = memoryview(first)
        with pytest.raises(BovnarArgumentError):
            next(fr)
        del held
        assert bytes(next(fr)) == b'.b = 2;'

    def test_a_held_fed_view_does_not_refuse_once(self):
        import struct
        dm = DemuxFeed(view=True)
        it = dm.feed(stream.mux_dump([(1, b'abc'), (2, b'def')]))
        rows = struct.iter_unpack('B', next(it).payload)   # a real export
        assert next(it) == (2, memoryview(b'def'))
        assert [r[0] for r in rows] == list(b'abc')

    def test_a_closed_demux_stops_with_a_view_held(self):
        dm = DemuxFeed(view=True)
        it = dm.feed(stream.mux_dump([(1, b'x' * 70_000), (2, b'y')]))
        held = memoryview(next(it).payload)        # the channel's buffer
        dm.close()
        assert list(it) == []
        with pytest.raises(StopIteration):
            next(dm)
        del held

    def test_an_mmap_is_fed_whole(self):
        import mmap
        doc = b'.a = 1; .b = 2;'
        mm = mmap.mmap(-1, len(doc))
        mm.write(doc)                              # its position is the end
        f = DocumentFeed(events=False, values=True)
        got = list(f.feed(mm)) + list(f.finish())
        assert got == [Value('a', 1), Value('b', 2)]
        mm.close()
