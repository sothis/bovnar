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

"""C API reachable from Python: every read option at every reading entry
point (one table, bovnar.READ_OPTIONS), the on_error callback, the DOM's size
cap, the demux's reader counters, the general Writer's mux and embedding, and
the helpers the bindings had no wrapper for."""

import io
import os
import tempfile
from fractions import Fraction

import pytest

from conftest import needs_lib
import bovnar
from bovnar import (Reader, Writer, DomDoc, ErrorCode, Event, ValueTypeFamily,
                    stream, parse_unit, make_type_spec)
from bovnar.exceptions import BovnarArgumentError, BovnarParseError
from bovnar.stream import DemuxFeed, DocumentFeed, FrameFeed, Message

pytestmark = needs_lib

# One document per length cap, with the cap that refuses it.
_CAPS = [
    (b'.abc = 1;', 'max_identifier_length', 2, ErrorCode.IDENTIFIER_TOO_LONG),
    (b'.a = "abcdef";', 'max_string_length', 3, ErrorCode.STRING_TOO_LONG),
    (b'.a = 12345;', 'max_number_length', 3, ErrorCode.NUMBER_TOO_LONG),
    (b'.a = abcdef;', 'max_symbol_length', 3, ErrorCode.SYMBOL_TOO_LONG),
    (b'.a = &.abcdef;', 'max_reference_length', 3,
     ErrorCode.REFERENCE_TOO_LONG),
]


def _code(fn, *args, **kw) -> ErrorCode:
    with pytest.raises(BovnarParseError) as ei:
        fn(*args, **kw)
    return ei.value.code


def _nested(depth: int) -> bytes:
    return (''.join(f'.s{i} = {{' for i in range(depth)) + '.x = 1;'
            + '};' * depth).encode()


@pytest.mark.parametrize('doc,name,cap,want', _CAPS)
def test_every_length_cap_reaches_the_reader(doc, name, cap, want):
    Reader().read_mem(doc)
    assert _code(Reader().read_mem, doc, **{name: cap}) == want
    Reader().read_mem(doc, **{name: 64})
    # ... and every other reading entry point.
    assert _code(bovnar.loads, doc, **{name: cap}) == want
    assert _code(bovnar.load, io.BytesIO(doc), **{name: cap}) == want
    assert _code(stream.parse_embedded, doc, **{name: cap}) == want
    frames = stream.frame_documents([doc])
    assert _code(stream.load_documents, frames, **{name: cap}) == want
    feed = DocumentFeed(**{name: cap})
    with pytest.raises(BovnarParseError) as ei:
        feed.feed(doc).finish()
    assert ei.value.code == want
    feed.close()


def test_nesting_caps_default_to_255_and_lower():
    Reader().read_mem(_nested(100))
    Reader().read_mem(_nested(3), max_struct_nesting=3)
    assert (_code(Reader().read_mem, _nested(4), max_struct_nesting=3)
            == ErrorCode.STRUCT_NESTING_TOO_HIGH)
    assert (_code(bovnar.loads, b'.a = [[1], [2]];', max_array_nesting=1)
            == ErrorCode.ARRAY_NESTING_TOO_HIGH)
    assert bovnar.loads(b'.a = [1, 2];', max_array_nesting=1) == {'a': [1, 2]}


def test_read_options_are_checked():
    with pytest.raises(TypeError, match='max_string_lenght'):
        Reader().read_mem(b'.a = 1;', max_string_lenght=3)
    with pytest.raises(TypeError, match='loads'):
        bovnar.loads(b'.a = 1;', bogus=1)
    with pytest.raises(TypeError, match='mux_load'):
        stream.mux_load(b'', bogus=1)
    with pytest.raises(TypeError, match='FrameFeed'):
        FrameFeed(bogus=1)
    with pytest.raises(TypeError, match='DemuxFeed'):
        DemuxFeed(bogus=1)
    for bad in (-1, 1 << 16, 1.5, '3'):
        with pytest.raises(BovnarArgumentError):
            Reader().read_mem(b'.a = 1;', max_string_length=bad)
    with pytest.raises(BovnarArgumentError):
        Reader().read_mem(b'.a = 1;', max_struct_nesting=256)
    # The table is the one source: every option is a bvnr_read_flags_t field.
    assert set(bovnar.READ_OPTIONS) <= {f for f, _ in bovnar.BvnrReadFlags._fields_}


def test_read_file_keeps_its_cap_and_takes_the_others():
    with tempfile.NamedTemporaryFile('wb', suffix='.bvnr', delete=False) as f:
        f.write(b'.a = "abcdef";')
    try:
        Reader().read_file(f.name)
        assert (_code(Reader().read_file, f.name, max_string_length=3)
                == ErrorCode.STRING_TOO_LONG)
        assert (_code(Reader().read_file, f.name, max_file_size=5)
                == ErrorCode.FILE_TOO_LONG)
    finally:
        os.unlink(f.name)


def test_on_error_sees_each_recovered_error():
    doc = b'.a = 1; .b = @; .c = 2; .d = "x; .e = 3;'
    errors, keys = [], []

    def on_verified(ev, d):
        if ev == Event.ASSIGNMENT_START:
            keys.append(d.raw_bytes())

    r = Reader()
    try:
        r.read_mem(doc, on_verified=on_verified, on_error=errors.append,
                   continue_on_error=True)
    except BovnarParseError as e:
        assert errors[-1].code == e.code
    assert r.recovery_count >= 1
    assert len(errors) >= r.recovery_count
    first = errors[0]
    assert isinstance(first, BovnarParseError)
    assert (first.code, first.line, first.offset, first.byte) == \
        (ErrorCode.UNEXPECTED_INPUT_BYTE, 1, 13, ord('@'))
    assert b'c' in keys


def test_on_error_sees_the_error_that_ends_a_read():
    seen = []
    r = Reader()
    with pytest.raises(BovnarParseError) as ei:
        r.read_mem(b'.a = @;', on_error=seen.append)
    assert [e.code for e in seen] == [ei.value.code]
    assert (seen[0].offset, r.error_offset, r.error_byte) == (5, 5, ord('@'))


def test_an_exception_in_on_error_stops_the_read():
    class Stop(Exception):
        pass

    events = []

    def on_error(err):
        raise Stop()

    with pytest.raises(Stop):
        Reader().read_mem(b'.a = @; .b = 1; .c = 2;',
                          on_verified=lambda ev, d: events.append(ev),
                          on_error=on_error, continue_on_error=True)
    # Stopped at the next event, not run to the end of the document.
    assert Event.STREAM_END not in events


def test_on_error_in_a_document_feed():
    errors = []
    feed = DocumentFeed(continue_on_error=True, on_error=errors.append)
    for b in b'.a = @; .b = 1;':
        list(feed.feed(bytes([b])))
    list(feed.finish())
    assert errors and errors[0].code == ErrorCode.UNEXPECTED_INPUT_BYTE
    assert feed.recovery_count >= 1
    feed.close()


def test_stream_functions_take_every_option():
    mux = stream.mux_dump([(1, b'hi')])
    assert stream.mux_load(mux) == [(1, b'hi')]
    assert (_code(stream.mux_load, mux, text_only=True)
            == ErrorCode.OCTET_STREAM_FORBIDDEN)
    assert (_code(stream.mux_load, io.BytesIO(mux), max_file_size=10)
            == ErrorCode.FILE_TOO_LONG)
    frames = stream.frame_documents([b'#!bovnar 9.0\n.a = 1;'])
    assert stream.load_documents(frames) == [{'a': 1}]
    assert (_code(stream.load_documents, frames, strict_version=True)
            == ErrorCode.UNSUPPORTED_SPEC_VERSION)
    ff = FrameFeed(parse=True, strict_version=True)
    with pytest.raises(BovnarParseError) as ei:
        list(ff.feed(frames))
    assert ei.value.code == ErrorCode.UNSUPPORTED_SPEC_VERSION
    ff.close()
    with pytest.raises(BovnarArgumentError):
        FrameFeed(strict_version=True)          # options need parse=True
    dm = DemuxFeed(text_only=True)
    with pytest.raises(BovnarParseError) as ei:
        list(dm.feed(mux))
    assert ei.value.code == ErrorCode.OCTET_STREAM_FORBIDDEN
    dm.close()


def test_parse_embedded_reports_the_error_that_ended_it():
    # Under continue_on_error the first error is recovered from.
    assert (_code(stream.parse_embedded, b'.a = @; .b = "x',
                  continue_on_error=True)
            == ErrorCode.GOT_INCOMPLETE_BVNR_STREAM)


def test_demux_feed_reports_its_readers_counters():
    doc = b'#!bovnar 1.1\n' + stream.mux_dump([(3, b'payload')])
    dm = DemuxFeed()
    assert dm.declared_version is None
    items = list(dm.feed(doc)) + list(dm.finish())
    assert items == [Message(3, b'payload')]
    assert dm.declared_version == (1, 1)
    assert (dm.recovery_count, dm.skipped_bytes) == (0, 0)
    dm.close()
    with pytest.raises(BovnarArgumentError):
        dm.recovery_count


def test_writer_mux_and_embedding_inside_a_document():
    w = Writer.to_mem(pretty=False)
    w.write_uint('before', 1)
    w.mux_begin('media')
    w.mux_send(2, b'one')
    w.mux_send(2, bytearray(b'two'))
    w.mux_end()
    w.embed_document('inner', b'.x = 5;')
    w.write_octets('raw', memoryview(b'\x00\x01'))
    w.mux_begin('second')
    w.mux_send(9, b'')
    w.mux_end()
    w.write_uint('after', 2)
    w.finish()
    doc = w.get_output()
    w.destroy()
    assert stream.mux_load(doc, key='media') == [(2, b'one'), (2, b'two')]
    assert stream.mux_load(doc, key='second') == [(9, b'')]
    values = bovnar.loads(doc)
    assert (values['before'], values['after'], values['raw']) == (1, 2, b'\x00\x01')
    assert stream.parse_embedded(values['inner']) == {'x': 5}
    # The bytes are those the Rust writer and mux_dump write for a stream.
    w = Writer.to_mem(pretty=False)
    w.mux_begin('mux')
    w.mux_send(1, b'hi')
    w.mux_end()
    w.finish()
    assert w.get_output() == stream.mux_dump([(1, b'hi')])
    w.destroy()


def test_writer_mux_refuses_what_c_refuses():
    w = Writer.to_mem()
    with pytest.raises(BovnarArgumentError):
        w.mux_begin('a\0b')
    with pytest.raises(BovnarArgumentError):
        w.mux_send(-1, b'x')
    w.destroy()
    with pytest.raises(BovnarArgumentError):
        w.mux_begin('m')


def test_dom_parse_fd_takes_a_lower_cap():
    doc = b'.a = 1; .b = 2;'
    with tempfile.NamedTemporaryFile('wb', delete=False) as f:
        f.write(doc)
    try:
        assert DomDoc.parse_file(f.name).lookup('b').as_i64() == 2
        assert DomDoc.parse_file(f.name, max_bytes=len(doc))
        assert (_code(DomDoc.parse_file, f.name, max_bytes=5)
                == ErrorCode.FILE_TOO_LONG)
        policy = bovnar.UnitPolicy()
        assert (_code(DomDoc.parse_file, f.name, policy, max_bytes=5)
                == ErrorCode.FILE_TOO_LONG)
        # Above the built-in cap is the built-in cap, as in C.
        assert DomDoc.parse_file(f.name, max_bytes=(1 << 64) - 1)
        with pytest.raises(BovnarArgumentError):
            DomDoc.parse_file(f.name, max_bytes=-1)
    finally:
        os.unlink(f.name)


def test_convert_rational_is_exact():
    km, m = parse_unit('k~m'), parse_unit('m')
    assert bovnar.convert_rational(3, km, m) == (Fraction(3000), True)
    # 42 km/h is 35/3 m/s: exact as a rational, endless as a decimal.
    assert bovnar.convert_rational(42, parse_unit('k~m/h'),
                                   parse_unit('m/s')) == (Fraction(35, 3), True)
    _, exact = bovnar.convert_rational(180, parse_unit('°'), parse_unit('rad'))
    assert not exact
    with pytest.raises(BovnarArgumentError):
        bovnar.convert_rational(1, m, parse_unit('s'))
    with pytest.raises(BovnarArgumentError):
        bovnar.convert_rational(0.1, km, m)
    # Past any machine integer, and past Python's 4300-digit str() limit.
    big = 10 ** 5000 + 1
    assert bovnar.convert_rational(Fraction(big, 7), km, m) == \
        (Fraction(big * 1000, 7), True)
    assert bovnar.convert_rational(Fraction(-1, 4), km, m) == (Fraction(-250), True)


def test_datetime_epoch_index_inverts_epoch_name():
    assert bovnar.datetime_epoch_index('') == 0
    assert bovnar.datetime_epoch_index('unix') == 0
    assert bovnar.datetime_epoch_index('no such epoch') is None
    with pytest.raises(BovnarArgumentError):
        bovnar.datetime_epoch_index('gps\0')
    for name in ('tai', 'gps', 'galileo', 'y2000'):
        idx = bovnar.datetime_epoch_index(name)
        q = bovnar.loads(f'#!bovnar 1.1\n.t = <datetime:64,{name}> 5;',
                         typed=True)['t']
        assert q.vtype.base == idx
        assert q.epoch_name == name
        vt = make_type_spec(ValueTypeFamily.DATETIME, 64, idx)
        assert bovnar.Quantity('5', vt).epoch_name == name


def test_version_number_and_error_message():
    major, minor, patch = (int(p) for p in bovnar.version().split('.'))
    assert bovnar.version_number() == major * 10000 + minor * 100 + patch
    assert bovnar.error_message(ErrorCode.STRING_TOO_LONG) == 'string_too_long'
    assert bovnar.error_message(0) == 'none'
