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

"""The unit's own spelling (spec 1.2, bvnr_data_t.unit_source) through the
Python bindings: the reader hands it over, a typed load keeps it on the
Quantity, dumps() writes it back, and the DOM exposes it.

doc/11 §5.2 is the specification; tests/bovnar_unit_source_test.c holds the C
library to it. What is checked here is that the bindings carry it rather than
quietly dropping back to the canonical form.
"""

import pytest

import bovnar
from bovnar import Quantity, Reader, Event
from bovnar.exceptions import BovnarArgumentError, BovnarWriteError

DOC = (b'#!bovnar 1.2\n'
       b'.a = <float:64,ucum:mL{total}> 1.0;\n'
       b'.b = <float:64,ucum:{RBC}/uL> 2.0;\n'
       b'.c = <float:64,k~m> [1.0, 2.0];\n'
       b'.d = <uint:8,km> [1, 2] / [3, 4];\n'
       b'.e = <float:64,udunits:kg m-2 s-1> [1.0, 2.0];\n'
       b'.s = <utf8> "x" m;\n'
       b'.n = 7;\n')


def test_reader_event_carries_the_spelling():
    seen = []

    def on_event(ev, data):
        if ev == Event.DATA:
            seen.append(data.unit_source_str())
        return True

    with Reader() as r:
        r.read_mem(b'#!bovnar 1.2\n.a = <float:64,ucum:mL{total}> 1.0;'
                   b'.b = 2 km; .c = 3;', on_verified=on_event)
    assert seen == ['ucum:mL{total}', 'km', None]


def test_typed_load_keeps_it_on_the_quantity():
    d = bovnar.loads(DOC, typed=True)
    assert d['a'].unit_source == 'ucum:mL{total}'
    # `unit` is what it means; the annotation lives only in the spelling.
    assert str(d['a'].unit) == 'm~L'
    assert d['d'][0][0].unit_source == 'km'
    assert d['s'].unit_source == 'm'
    assert d['n'].unit_source is None


def test_typed_round_trip_is_byte_identical():
    # Every spelling here is one the canonical form would have replaced:
    # two UCUM annotations, a compact prefix, a space-multiplied UDUNITS code.
    assert bovnar.dumps(bovnar.loads(DOC, typed=True)) == DOC


def test_dumps_declares_1_2_only_when_the_spelling_needs_it():
    out = bovnar.dumps(bovnar.loads(b'.a = <float:64,km> 1.0;', typed=True))
    assert out == b'.a = <float:64,km> 1.0;\n'
    out = bovnar.dumps(bovnar.loads(
        b'#!bovnar 1.2\n.a = <float:64,ucum:mm[Hg]> 1.0;', typed=True))
    assert out == b'#!bovnar 1.2\n.a = <float:64,ucum:mm[Hg]> 1.0;\n'


def test_dumps_writes_a_profile_only_unit():
    # This failed before spellings were kept: [IU] has no native form, the C
    # writer refuses it without a 1.2 directive, and dumps() never wrote one.
    src = b'#!bovnar 1.2\n.a = <float:64,ucum:[IU]> 2.0;\n'
    assert bovnar.dumps(bovnar.loads(src, typed=True)) == src
    q = Quantity('2.0', bovnar.make_type_spec(bovnar.ValueTypeFamily.FLOAT, 64),
                 'ucum:[IU]')
    assert bovnar.dumps({'a': q}) == src


def test_an_array_keeps_one_spelling_or_none():
    d = bovnar.loads(b'.a = <uint:8,km> [1, 2];', typed=True)
    assert bovnar.dumps(d) == b'.a = <uint:8,km> [1, 2];\n'
    # Leaves that disagree cannot share an annotation's one spelling.
    vt = d['a'][0].vtype
    d['a'][1] = Quantity('2', vt, d['a'][0].unit)        # no spelling kept
    assert bovnar.dumps(d) == b'.a = <uint:8,k~m> [1, 2];\n'


def test_a_spelling_that_disagrees_with_the_unit_is_refused():
    vt = bovnar.make_type_spec(bovnar.ValueTypeFamily.FLOAT, 64)
    with pytest.raises(BovnarWriteError) as e:
        bovnar.dumps({'a': Quantity('1.0', vt, 'm', unit_source='s')})
    assert e.value.code == bovnar.ErrorCode.UNIT_MISMATCH
    with pytest.raises(BovnarArgumentError):
        Quantity('1.0', vt, 'm', unit_source=5)


def test_dom_exposes_it_where_the_unit_cannot_give_it_back():
    dom = bovnar.dom_parse(DOC)
    assert dom.lookup('.a').unit_source == 'ucum:mL{total}'
    assert dom.lookup('.c')[0].unit_source is None       # canonical already
    assert dom.lookup('.n').unit_source is None
