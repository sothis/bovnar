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

import pytest
from bovnar.enums import Event, ValueTypeFamily
from bovnar.__init__ import _DictParser, _ArrScope, _seal_array


class _FakeVT:
    def __init__(self, family=ValueTypeFamily.PLAIN, base=0):
        self.family = int(family)
        self.base   = base


class _FakeData:
    def __init__(self, raw=b'', family=ValueTypeFamily.PLAIN, base=0):
        self._raw       = raw
        self.value_type = _FakeVT(family, base)
        self.value_unit = None

    def raw_bytes(self):
        return self._raw


def _d(raw=b'', family=ValueTypeFamily.PLAIN, base=0):
    return _FakeData(raw, family, base)


def _uint(n: int):
    return _d(str(n).encode(), ValueTypeFamily.UINT, 10)


def _float(v: float):
    return _d(repr(v).encode(), ValueTypeFamily.FLOAT)


def _str(s: str):
    return _d(s.encode(), ValueTypeFamily.UTF8)


def _key(k: str):
    return _d(k.encode())


def run(*events):
    p = _DictParser()
    for ev, *args in events:
        p.on_event(ev, args[0] if args else None)
    return p.result()


AS  = Event.ASSIGNMENT_START
ARS = Event.ARRAY_ROW_START
ARE = Event.ARRAY_ROW_END
ADS = Event.ARRAY_DIM_START
SS  = Event.STRUCT_START
SE  = Event.STRUCT_END
D   = Event.DATA


class TestSealArray:
    def test_single_row_unwraps(self):
        arr = _ArrScope()
        arr.rows = [[1, 2, 3]]
        assert _seal_array(arr) == [1, 2, 3]

    def test_multi_row_stays_nested(self):
        arr = _ArrScope()
        arr.rows = [[1, 2], [3, 4]]
        assert _seal_array(arr) == [[1, 2], [3, 4]]

    def test_empty_single_row(self):
        arr = _ArrScope()
        arr.rows = [[]]
        assert _seal_array(arr) == []

    def test_three_rows(self):
        arr = _ArrScope()
        arr.rows = [[1], [2], [3]]
        assert _seal_array(arr) == [[1], [2], [3]]


class TestFlatArrays:
    def test_int_row(self):
        r = run(
            (AS, _key('v')),
            (ARS,), (D, _uint(1)), (D, _uint(2)), (D, _uint(3)), (ARE,),
        )
        assert r == {'v': [1, 2, 3]}

    def test_float_row(self):
        r = run(
            (AS, _key('f')),
            (ARS,), (D, _float(1.5)), (D, _float(2.5)), (ARE,),
        )
        assert r == {'f': [1.5, 2.5]}

    def test_string_row(self):
        r = run(
            (AS, _key('tags')),
            (ARS,), (D, _str('a')), (D, _str('b')), (ARE,),
        )
        assert r == {'tags': ['a', 'b']}

    def test_single_element(self):
        r = run(
            (AS, _key('x')),
            (ARS,), (D, _uint(7)), (ARE,),
        )
        assert r == {'x': [7]}

    def test_empty_row(self):
        r = run(
            (AS, _key('empty')),
            (ARS,), (ARE,),
        )
        assert r == {'empty': []}

    def test_mixed_int_float(self):
        r = run(
            (AS, _key('m')),
            (ARS,), (D, _uint(1)), (D, _float(2.0)), (ARE,),
        )
        assert r == {'m': [1, 2.0]}


class TestMultiRowArrays:
    def test_2x2_matrix(self):
        r = run(
            (AS, _key('mat')),
            (ARS,), (D, _uint(1)), (D, _uint(2)), (ARE,),
            (ADS,),
            (ARS,), (D, _uint(3)), (D, _uint(4)), (ARE,),
        )
        assert r == {'mat': [[1, 2], [3, 4]]}

    def test_3x3_matrix(self):
        r = run(
            (AS, _key('m')),
            (ARS,), (D, _uint(1)), (D, _uint(2)), (D, _uint(3)), (ARE,),
            (ADS,),
            (ARS,), (D, _uint(4)), (D, _uint(5)), (D, _uint(6)), (ARE,),
            (ADS,),
            (ARS,), (D, _uint(7)), (D, _uint(8)), (D, _uint(9)), (ARE,),
        )
        assert r == {'m': [[1, 2, 3], [4, 5, 6], [7, 8, 9]]}

    def test_uneven_rows(self):
        r = run(
            (AS, _key('u')),
            (ARS,), (D, _uint(1)), (ARE,),
            (ADS,),
            (ARS,), (D, _uint(2)), (D, _uint(3)), (D, _uint(4)), (ARE,),
        )
        assert r == {'u': [[1], [2, 3, 4]]}

    def test_dim_continue_resets_row(self):
        r = run(
            (AS, _key('d')),
            (ARS,), (D, _uint(10)), (ARE,),
            (ADS,),
            (ARS,), (D, _uint(20)), (ARE,),
            (ADS,),
            (ARS,), (D, _uint(30)), (ARE,),
        )
        assert r == {'d': [[10], [20], [30]]}


class TestNestedArrays:
    def test_nested_two_inner_rows(self):
        r = run(
            (AS, _key('n')),
            (ARS,),
              (ARS,), (D, _uint(1)), (D, _uint(2)), (ARE,),
              (ARS,), (D, _uint(3)), (D, _uint(4)), (ARE,),
            (ARE,),
        )
        assert r == {'n': [[1, 2], [3, 4]]}

    def test_nested_three_inner_rows(self):
        r = run(
            (AS, _key('n')),
            (ARS,),
              (ARS,), (D, _uint(1)), (ARE,),
              (ARS,), (D, _uint(2)), (ARE,),
              (ARS,), (D, _uint(3)), (ARE,),
            (ARE,),
        )
        assert r == {'n': [[1], [2], [3]]}

    def test_nested_empty_inner(self):
        r = run(
            (AS, _key('n')),
            (ARS,),
              (ARS,), (ARE,),
            (ARE,),
        )
        assert r == {'n': [[]]}


class TestStructElements:
    def test_one_struct_element(self):
        r = run(
            (AS, _key('recs')),
            (ARS,),
              (SS,), (AS, _key('x')), (D, _uint(1)), (SE,),
            (ARE,),
        )
        assert r == {'recs': [{'x': 1}]}

    def test_two_struct_elements(self):
        r = run(
            (AS, _key('recs')),
            (ARS,),
              (SS,), (AS, _key('x')), (D, _uint(1)), (SE,),
              (SS,), (AS, _key('y')), (D, _uint(2)), (SE,),
            (ARE,),
        )
        assert r == {'recs': [{'x': 1}, {'y': 2}]}

    def test_struct_with_multiple_fields(self):
        r = run(
            (AS, _key('items')),
            (ARS,),
              (SS,),
                (AS, _key('a')), (D, _uint(10)),
                (AS, _key('b')), (D, _uint(20)),
              (SE,),
            (ARE,),
        )
        assert r == {'items': [{'a': 10, 'b': 20}]}

    def test_struct_with_nested_array_field(self):
        r = run(
            (AS, _key('data')),
            (ARS,),
              (SS,),
                (AS, _key('vals')),
                (ARS,), (D, _uint(10)), (D, _uint(20)), (ARE,),
              (SE,),
            (ARE,),
        )
        assert r == {'data': [{'vals': [10, 20]}]}


class TestSealTiming:
    def test_trailing_array_no_following_event(self):
        r = run(
            (AS, _key('last')),
            (ARS,), (D, _uint(9)), (ARE,),
        )
        assert r == {'last': [9]}

    def test_array_sealed_by_next_assignment(self):
        r = run(
            (AS, _key('a')),
            (ARS,), (D, _uint(1)), (ARE,),
            (AS, _key('b')), (D, _uint(2)),
        )
        assert r == {'a': [1], 'b': 2}

    def test_struct_trailing_array(self):
        r = run(
            (AS, _key('s')),
            (SS,),
              (AS, _key('arr')),
              (ARS,), (D, _uint(7)), (ARE,),
            (SE,),
        )
        assert r == {'s': {'arr': [7]}}

    def test_struct_array_followed_by_scalar(self):
        r = run(
            (AS, _key('s')),
            (SS,),
              (AS, _key('arr')),
              (ARS,), (D, _uint(1)), (D, _uint(2)), (ARE,),
              (AS, _key('n')), (D, _uint(99)),
            (SE,),
        )
        assert r == {'s': {'arr': [1, 2], 'n': 99}}

    def test_two_consecutive_top_level_arrays(self):
        r = run(
            (AS, _key('x')),
            (ARS,), (D, _uint(1)), (ARE,),
            (AS, _key('y')),
            (ARS,), (D, _uint(2)), (ARE,),
        )
        assert r == {'x': [1], 'y': [2]}

    def test_three_consecutive_top_level_arrays(self):
        r = run(
            (AS, _key('a')), (ARS,), (D, _uint(1)), (ARE,),
            (AS, _key('b')), (ARS,), (D, _uint(2)), (ARE,),
            (AS, _key('c')), (ARS,), (D, _uint(3)), (ARE,),
        )
        assert r == {'a': [1], 'b': [2], 'c': [3]}


class TestMixedDocuments:
    def test_scalar_then_array_then_scalar(self):
        r = run(
            (AS, _key('a')), (D, _uint(10)),
            (AS, _key('b')), (ARS,), (D, _uint(1)), (D, _uint(2)), (ARE,),
            (AS, _key('c')), (D, _uint(20)),
        )
        assert r == {'a': 10, 'b': [1, 2], 'c': 20}

    def test_struct_then_array(self):
        r = run(
            (AS, _key('cfg')),
            (SS,), (AS, _key('x')), (D, _uint(5)), (SE,),
            (AS, _key('ids')),
            (ARS,), (D, _uint(1)), (D, _uint(2)), (D, _uint(3)), (ARE,),
        )
        assert r == {'cfg': {'x': 5}, 'ids': [1, 2, 3]}

    def test_deeply_nested_struct_with_array(self):
        r = run(
            (AS, _key('a')),
            (SS,),
              (AS, _key('b')),
              (SS,),
                (AS, _key('items')),
                (ARS,), (D, _uint(1)), (D, _uint(2)), (ARE,),
              (SE,),
            (SE,),
        )
        assert r == {'a': {'b': {'items': [1, 2]}}}

    def test_array_of_structs_multi_row(self):
        r = run(
            (AS, _key('recs')),
            (ARS,),
              (SS,), (AS, _key('v')), (D, _uint(1)), (SE,),
            (ARE,),
            (ADS,),
            (ARS,),
              (SS,), (AS, _key('v')), (D, _uint(2)), (SE,),
            (ARE,),
        )
        assert r == {'recs': [[{'v': 1}], [{'v': 2}]]}

    def test_parent_key_not_clobbered_by_struct_start(self):
        r = run(
            (AS, _key('outer')),
            (ARS,),
              (SS,),
                (AS, _key('inner_key')), (D, _str('value')),
              (SE,),
            (ARE,),
        )
        assert r['outer'] == [{'inner_key': 'value'}]
