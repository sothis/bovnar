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

"""Reader and writer plumbing the bindings could not reach: the writer's
on_event observer and nesting caps, the reader's input mirror, the
canonicalising observer, DocumentFeed's on_unverified, the error of each
failed framed document, and the per-type writer helpers write_plain,
write_datetime and write_type_annotation."""

import glob
import io
import os
import subprocess

import pytest

from conftest import needs_lib
import bovnar
from bovnar import (Reader, Writer, ErrorCode, Event, UnitFlags,
                    ValueTypeFamily, make_type_spec)
from bovnar.exceptions import (BovnarArgumentError, BovnarParseError,
                               BovnarWriteError)
from bovnar.stream import (DocumentFeed, FrameFeed, frame_documents,
                           load_documents)

pytestmark = needs_lib

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
_EXAMPLES = sorted(glob.glob(os.path.join(_REPO, 'examples', '*.bvnr')))


class Boom(Exception):
    pass


def _cli():
    """The built CLI: $BOVNAR_CLI, the one beside the library under test, or
    build/bovnar; None when there is none."""
    candidates = [os.environ.get('BOVNAR_CLI')]
    lib = os.environ.get('LIBBOVNAR_PATH')
    if lib:
        candidates.append(os.path.join(os.path.dirname(lib), 'bovnar'))
    candidates.append(os.path.join(_REPO, 'build', 'bovnar'))
    for c in candidates:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


# -- the writer's on_event observer -----------------------------------------

def test_observer_sees_what_reached_the_output():
    seen = []
    w = Writer.to_mem(pretty=False, unit_flags=UnitFlags.REDUCE,
                      on_event=lambda ev, d: seen.append((ev, d)))
    w.write_uint('d', 5, unit_str='k~m')
    w.write_string('s', 'x')
    w.finish()
    assert w.get_output() == b'.d=<uint:64,m>5000;.s="x";'
    events = [ev for ev, _ in seen]
    assert events[0] == Event.ASSIGNMENT_START
    data = [d for ev, d in seen if ev == Event.DATA]
    # The rescale's digits and unit, not the 5 k~m the writer was handed.
    assert data[0].raw_bytes() == b'5000'
    assert bovnar.unit_to_str(data[0].value_unit) == 'm'
    assert data[1].raw_bytes() == b'x'
    # A snapshot: still readable after the writer is gone.
    w.destroy()
    assert seen[0][1].raw_bytes() == b'd'


def test_observer_can_veto_a_write():
    def veto(ev, d):
        return not (ev == Event.DATA and d.raw_bytes() == b'2')
    w = Writer.to_mem(pretty=False, on_event=veto)
    w.write_uint('a', 1)
    with pytest.raises(BovnarWriteError) as e:
        w.write_uint('b', 2)
    assert e.value.code == ErrorCode.SCANNER_CALLBACK_FAILED
    # The writer is in its error state from then on.
    with pytest.raises(BovnarWriteError):
        w.write_uint('c', 3)


def test_observer_exception_reaches_the_write_that_ran_it():
    def boom(ev, d):
        if ev == Event.DATA:
            raise Boom()
    w = Writer.to_mem(on_event=boom)
    with pytest.raises(Boom):
        w.write_uint('a', 1)
    # Later calls raise the latched error, chained to the exception.
    with pytest.raises(BovnarWriteError) as e:
        w.write_uint('b', 1)
    assert e.value.code == ErrorCode.SCANNER_CALLBACK_FAILED
    assert isinstance(e.value.__cause__, Boom)


def test_observer_may_not_use_its_writer():
    errors = []
    holder = {}

    def reenter(ev, d):
        for call in (lambda: holder['w'].write_uint('x', 1),
                     holder['w'].finish, holder['w'].flush,
                     holder['w'].destroy):
            try:
                call()
            except BovnarArgumentError as exc:
                errors.append(exc)
    w = holder['w'] = Writer.to_mem(on_event=reenter)
    w.write_null('a')
    w.finish()
    assert len(errors) == 4 * 2      # two events, four refused calls each
    assert bovnar.loads(w.get_output()) == {'a': None}


def test_observer_on_every_constructor(tmp_path):
    for make in (lambda **k: Writer.to_mem(**k),
                 lambda **k: Writer.to_stream(io.BytesIO(), **k),
                 lambda **k: Writer.to_file(str(tmp_path / 'o.bvnr'), **k)):
        seen = []
        w = make(on_event=lambda ev, d: seen.append(ev))
        w.write_bool('b', True)
        w.finish()
        assert seen == [Event.ASSIGNMENT_START, Event.DATA]
    with pytest.raises(BovnarArgumentError):
        Writer.to_mem(on_event=42)


# -- nesting caps -----------------------------------------------------------

@pytest.mark.parametrize('cap', [1, 3, 255])
def test_struct_nesting_cap_refuses_one_past_it(cap):
    w = Writer.to_mem(pretty=False, max_struct_nesting=cap,
                      cap=1 << 16)
    for i in range(cap):
        w.begin_struct('s')
    with pytest.raises(BovnarWriteError) as e:
        w.begin_struct('s')
    assert e.value.code == ErrorCode.STRUCT_NESTING_TOO_HIGH


def test_array_nesting_cap_refuses_one_past_it():
    w = Writer.to_mem(pretty=False, max_array_nesting=2)
    w.emit(Event.ASSIGNMENT_START, key='a')
    w.begin_array_row()
    w.begin_array_row()
    with pytest.raises(BovnarWriteError) as e:
        w.begin_array_row()
    assert e.value.code == ErrorCode.ARRAY_NESTING_TOO_HIGH


def test_nesting_zero_is_255_and_bad_caps_are_refused():
    w = Writer.to_mem(pretty=False, max_struct_nesting=0)
    for _ in range(255):
        w.begin_struct('s')
    with pytest.raises(BovnarWriteError):
        w.begin_struct('s')
    for bad in (-1, 256, 1.0, True):
        with pytest.raises(BovnarArgumentError):
            Writer.to_mem(max_array_nesting=bad)


# -- the reader's mirror ----------------------------------------------------

DOC = b'#!bovnar 1.1\n.a = 1;  # c\n.b = { .c = "x"; };\n.o = ' + \
      bovnar.dumps({'k': b'\x00\x01' * 300})[5:]


def test_mirror_gets_exactly_the_input():
    for read in ('mem', 'stream', 'fd'):
        out = bytearray()
        r = Reader()
        if read == 'mem':
            r.read_mem(DOC, mirror=out)
        elif read == 'stream':
            r.read_stream(io.BytesIO(DOC), mirror=out)
        else:
            rd, wr = os.pipe()
            os.write(wr, DOC)
            os.close(wr)
            try:
                r.read_fd(rd, mirror=out)
            finally:
                os.close(rd)
        assert bytes(out) == DOC, read


def test_mirror_into_a_file_object_and_from_a_file(tmp_path):
    path = tmp_path / 'd.bvnr'
    path.write_bytes(DOC)
    out = io.BytesIO()
    Reader().read_file(str(path), mirror=out)
    assert out.getvalue() == DOC


def test_feed_mirror_in_pieces():
    for size in (1, 3, 64, len(DOC)):
        out = bytearray()
        f = DocumentFeed(mirror=out)
        for i in range(0, len(DOC), size):
            list(f.feed(DOC[i:i + size]))
        list(f.finish())
        assert bytes(out) == DOC, size


def test_mirror_stops_before_the_piece_with_the_error():
    # The reader mirrors a piece once it has parsed it, so the piece holding
    # the error never reaches the mirror.
    out = bytearray()
    f = DocumentFeed(mirror=out)
    list(f.feed(b'.a = 1; '))
    with pytest.raises(BovnarParseError) as e:
        f.feed(b'.b = @; .c = 2;')
    assert e.value.code == ErrorCode.UNEXPECTED_INPUT_BYTE
    assert bytes(out) == b'.a = 1; '
    out = bytearray()
    with pytest.raises(BovnarParseError):
        Reader().read_mem(b'.a = 1; .b = @; .c = 2;', mirror=out)
    assert bytes(out) == b''


def test_mirror_exception_reaches_the_caller():
    class Bad(io.RawIOBase):
        def writable(self):
            return True

        def write(self, b):
            raise Boom()
    with pytest.raises(Boom):
        Reader().read_mem(b'.a = 1;', mirror=Bad())
    f = DocumentFeed(mirror=Bad())
    with pytest.raises(Boom):
        f.feed(b'.a = 1;')
    f.close()


# -- canonical form ----------------------------------------------------------

def test_canonical_form_of_a_small_document():
    doc = b'.a = 1 ;  # one\n.b = { .c = "x"; };'
    assert bovnar.canonicalize(doc) == \
        b'.a=<uint:64,_10,no_unit>1;.b={.c=<utf8>"x";};'
    pretty = bovnar.canonicalize(doc, pretty=True)
    assert b'\n' in pretty and bovnar.loads(pretty) == bovnar.loads(doc)


@pytest.mark.parametrize('path', _EXAMPLES,
                         ids=[os.path.basename(p) for p in _EXAMPLES])
@pytest.mark.parametrize('pretty', [False, True])
def test_canonical_form_matches_the_cli_and_is_idempotent(path, pretty):
    with open(path, 'rb') as f:
        data = f.read()
    canon = bovnar.canonicalize(data, pretty=pretty)
    # Idempotent, and the same document.
    assert bovnar.canonicalize(canon, pretty=pretty) == canon
    assert bovnar.loads(canon) == bovnar.loads(data)
    # The stream form sniffs the directive from the pulled bytes.
    with open(path, 'rb') as f:
        assert bovnar.canonicalize(f, pretty=pretty) == canon
    out = io.BytesIO()
    bovnar.canonicalize_to(io.BytesIO(data), out, pretty=pretty)
    assert out.getvalue() == canon
    cli = _cli()
    if cli is None:
        pytest.skip('no built CLI to compare with')
    # `bovnar events -d` writes the observer's output to stderr.
    args = [cli, 'events', '-d'] + (['-p'] if pretty else []) + [path]
    run = subprocess.run(args, capture_output=True, check=True)
    assert run.stderr == canon


def test_canonical_form_keeps_the_directive():
    canon = bovnar.canonicalize(b'\xef\xbb\xbf  \n#!bovnar 1.1\n.a = 1;')
    assert canon.startswith(b'#!bovnar 1.1\n')
    # From a stream handed over a byte at a time, too.

    class Trickle(io.RawIOBase):
        def __init__(self, data):
            self._data = data

        def readable(self):
            return True

        def readinto(self, b):
            if not self._data:
                return 0
            b[0] = self._data[0]
            self._data = self._data[1:]
            return 1
    assert bovnar.canonicalize(
        Trickle(b'\xef\xbb\xbf  \n#!bovnar 1.1\n.a = 1;')) == canon
    assert not bovnar.canonicalize(b'# just a comment\n.a=1;').startswith(b'#')


def test_canonicalize_refuses_a_recovered_document():
    doc = b'.a=[1,2,{.b=@;},3]; .c = 2;'
    with pytest.raises(BovnarParseError) as e:
        bovnar.canonicalize(doc, continue_on_error=True)
    assert e.value.code == ErrorCode.UNEXPECTED_INPUT_BYTE
    assert e.value.line == 1
    # Without continue_on_error it is the reader's own error.
    with pytest.raises(BovnarParseError):
        bovnar.canonicalize(doc)
    # A clean document reads the same with continue_on_error.
    assert bovnar.canonicalize(b'.a=1;', continue_on_error=True) == \
        bovnar.canonicalize(b'.a=1;')


def test_canonicalize_options_and_errors():
    with pytest.raises(TypeError):
        bovnar.canonicalize(b'.a=1;', no_such_option=1)
    with pytest.raises(BovnarParseError) as e:
        bovnar.canonicalize(b'.a="xxxx";', max_string_length=2)
    assert e.value.code == ErrorCode.STRING_TOO_LONG
    with pytest.raises(BovnarArgumentError):
        bovnar.canonicalize('.a=1;')

    class Bad:
        def write(self, b):
            raise Boom()
    with pytest.raises(Boom):
        bovnar.canonicalize_to(b'.a=1;', Bad())

    class BadIn(io.RawIOBase):
        def readable(self):
            return True

        def readinto(self, b):
            raise Boom()
    with pytest.raises(Boom):
        bovnar.canonicalize(BadIn())


# -- DocumentFeed's on_unverified --------------------------------------------

def test_feed_on_unverified_sees_the_lexer_events():
    doc = b'.a = <uint:8> 7; .b = "x";'
    want = []
    Reader().read_mem(doc, on_unverified=lambda ev, d: want.append(
        (ev, d.raw_bytes() if d else b'')))
    for size in (1, 5, len(doc)):
        got = []
        f = DocumentFeed(on_unverified=lambda ev, d: got.append(
            (ev, d.raw_bytes() if d else b'')))
        for i in range(0, len(doc), size):
            list(f.feed(doc[i:i + size]))
        list(f.finish())
        assert got == want, size


def test_feed_on_unverified_exception():
    def boom(ev, d):
        raise Boom()
    f = DocumentFeed(on_unverified=boom)
    with pytest.raises(Boom):
        f.feed(b'.a = 1;')
    f.close()


# -- failed framed documents --------------------------------------------------

FRAMES = frame_documents([b'.a = 1;', b'.a = @;', b'.b = 2;', b'.c = "x'])


def test_load_documents_return_errors():
    assert load_documents(FRAMES, continue_past_failed=True) == \
        [{'a': 1}, None, {'b': 2}, None]
    docs = load_documents(FRAMES, continue_past_failed=True,
                          return_errors=True)
    assert docs[0] == {'a': 1} and docs[2] == {'b': 2}
    assert isinstance(docs[1], BovnarParseError)
    assert docs[1].code == ErrorCode.UNEXPECTED_INPUT_BYTE
    assert docs[1].index == 1
    assert docs[3].code == ErrorCode.GOT_INCOMPLETE_BVNR_STREAM
    assert docs[3].index == 3
    # From a stream too.
    assert [type(d) for d in load_documents(
        io.BytesIO(FRAMES), continue_past_failed=True, return_errors=True)] \
        == [dict, BovnarParseError, dict, BovnarParseError]


def test_frame_feed_return_errors():
    f = FrameFeed(parse=True, continue_past_failed=True, return_errors=True)
    items = []
    for i in range(0, len(FRAMES), 7):
        items.extend(f.feed(FRAMES[i:i + 7]))
    items.extend(f.finish())
    assert [type(x) for x in items] == \
        [dict, BovnarParseError, dict, BovnarParseError]
    assert (items[1].index, items[1].code) == \
        (1, ErrorCode.UNEXPECTED_INPUT_BYTE)
    with pytest.raises(BovnarArgumentError):
        FrameFeed(return_errors=True)


# -- the per-type writer helpers ---------------------------------------------

def test_write_plain_datetime_and_type_annotation():
    w = Writer.to_mem(pretty=False, emit_version=True)
    w.write_plain('mode', 'auto')
    w.write_datetime('t', 1750000000)
    w.write_datetime('g', -5, width=32, epoch='gps')
    u8 = make_type_spec(ValueTypeFamily.UINT, 8)
    w.emit(Event.ASSIGNMENT_START, key='v')
    w.write_type_annotation(u8, 'm')
    w.begin_array_row()
    for v in ('1', '2'):
        w.emit(Event.DATA, value=v, vt=u8)
    w.end_array_row()
    w.write_float('vol', 5.0, unit_str='ucum:mL{total}',
                  unit_source='ucum:mL{total}')
    w.finish()
    out = w.get_output()
    assert out.split(b'\n', 1)[1] == (
        b'.mode=auto;.t=<datetime:64>1750000000;.g=<datetime:32,gps>-5;'
        b'.v=<uint:8,m>[1,2];.vol=<float:64,ucum:mL{total}>5e+0;')
    typed = bovnar.loads(out, typed=True)
    assert typed['vol'].unit_source == 'ucum:mL{total}'
    assert typed['g'].vtype.base == bovnar.datetime_epoch_index('gps')


def test_write_helpers_refuse_what_c_would_mangle():
    w = Writer.to_mem()
    with pytest.raises(BovnarArgumentError):
        w.write_plain('a', 'x\0y')
    with pytest.raises(BovnarArgumentError):
        w.write_datetime('t', 1, epoch='martian')
    with pytest.raises(BovnarArgumentError):
        w.write_datetime('t', 1 << 63)
    with pytest.raises(BovnarArgumentError):
        w.write_uint('n', 1, unit_source='m')
    # Datetime is spec 1.1: without the directive the C writer refuses it.
    with pytest.raises(BovnarWriteError) as e:
        w.write_datetime('t', 1)
    assert e.value.code == ErrorCode.UNSUPPORTED_SPEC_VERSION
    w = Writer.to_mem()
    with pytest.raises(BovnarWriteError):
        w.write_plain('a', 'not a symbol')
