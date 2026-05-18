import pytest

from conftest import needs_lib
import bovnar
from bovnar import loads, write_array
from bovnar.writer import Writer
from bovnar.enums import Event, ValueTypeFamily, BaseUnit, SIPrefix
from bovnar.structs import (
    make_type_spec, make_unit_si, make_unit_dimensionless, make_unit_none,
)
from bovnar.exceptions import BovnarArgumentError


def _roundtrip(rows, **kw):
    with Writer.to_mem() as w:
        write_array(w, 'data', rows, **kw)
    raw = w.get_output()
    doc = loads(raw)
    return doc.get('data')


@needs_lib
class TestWriteArrayFlat:
    def test_ints_roundtrip(self):
        result = _roundtrip([1, 2, 3])
        assert result == [1, 2, 3]

    def test_single_element(self):
        result = _roundtrip([42])
        assert result == [42]

    def test_floats(self):
        result = _roundtrip([1.5, 2.5, 3.5])
        assert len(result) == 3
        assert abs(result[0] - 1.5) < 1e-9

    def test_strings(self):
        result = _roundtrip(['a', 'b', 'c'])
        assert result == ['a', 'b', 'c']

    def test_negative_ints(self):
        result = _roundtrip([-1, -2, -3])
        assert result == [-1, -2, -3]

    def test_mixed_int_signs(self):
        result = _roundtrip([-5, 0, 5])
        assert result == [-5, 0, 5]

    def test_empty_list(self):
        # BVNR rows must contain at least one element; an empty row produces
        # an implicit null, so [] round-trips as [None].
        result = _roundtrip([])
        assert result == [None]

    def test_output_contains_key(self):
        with Writer.to_mem() as w:
            write_array(w, 'mykey', [1, 2])
        out = w.get_output()
        assert b'mykey' in out

    def test_output_contains_values(self):
        with Writer.to_mem() as w:
            write_array(w, 'vals', [10, 20, 30])
        out = w.get_output()
        assert b'10' in out
        assert b'20' in out
        assert b'30' in out


@needs_lib
class TestWriteArrayMultiRow:
    def test_2x2_roundtrip(self):
        result = _roundtrip([[1, 2], [3, 4]])
        assert result == [[1, 2], [3, 4]]

    def test_3x1_roundtrip(self):
        result = _roundtrip([[10], [20], [30]])
        assert result == [[10], [20], [30]]

    def test_output_contains_slash_separator(self):
        with Writer.to_mem() as w:
            write_array(w, 'm', [[1, 2], [3, 4]])
        out = w.get_output()
        assert b'/' in out

    def test_1x3_treated_as_flat(self):
        result = _roundtrip([1, 2, 3])
        assert result == [1, 2, 3]

    def test_3x3_matrix(self):
        rows   = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
        result = _roundtrip(rows)
        assert result == rows

    def test_uniform_rows(self):
        # BVNR arrays are rectangular: all rows must have the same column count.
        rows   = [[1, 0], [2, 3], [4, 5]]
        result = _roundtrip(rows)
        assert result == rows


@needs_lib
class TestWriteArrayDictElements:
    def test_single_dict_element(self):
        result = _roundtrip([{'x': 1}])
        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0]['x'] == 1

    def test_two_dict_elements(self):
        result = _roundtrip([{'a': 1}, {'a': 2}])
        assert result == [{'a': 1}, {'a': 2}]

    def test_dict_with_multiple_fields(self):
        result = _roundtrip([{'x': 10, 'y': 20}])
        assert result[0] == {'x': 10, 'y': 20}

    def test_dict_with_nested_struct(self):
        result = _roundtrip([{'cfg': {'port': 80}}])
        assert result[0]['cfg']['port'] == 80

    def test_dict_with_array_field(self):
        result = _roundtrip([{'vals': [1, 2, 3]}])
        assert result[0]['vals'] == [1, 2, 3]


@needs_lib
class TestWriteArrayNestedElements:
    def test_nested_list_as_element(self):
        result = _roundtrip([[1, 2], [3, 4]])
        assert result == [[1, 2], [3, 4]]

    def test_single_nested_list(self):
        with Writer.to_mem() as w:
            write_array(w, 'd', [1, 2, 3])
        out = w.get_output()
        assert b'd' in out


@needs_lib
class TestWriteArrayWithType:
    def test_uint32_annotation_in_output(self):
        vt = make_type_spec(ValueTypeFamily.UINT, 32, 10)
        with Writer.to_mem() as w:
            write_array(w, 'ports', [80, 443], vt=vt)
        out = w.get_output()
        assert b'uint' in out
        assert b'32'   in out

    def test_float64_annotation(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT, 64, 0)
        with Writer.to_mem() as w:
            write_array(w, 'vals', [1.0, 2.0], vt=vt)
        out = w.get_output()
        assert b'float' in out

    def test_unit_annotation(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT, 64, 0)
        vu = make_unit_si(BaseUnit.METER, SIPrefix.KILO)
        with Writer.to_mem() as w:
            write_array(w, 'distances', [1.0, 2.0], vt=vt, vu=vu)
        out = w.get_output()
        assert b'k~m' in out or b'km' in out

    def test_no_annotation_still_valid(self):
        result = _roundtrip([5, 10, 15])
        assert result == [5, 10, 15]


@needs_lib
class TestWriteArrayTokenTypes:
    """
    These tests verify the token type fix (issue #2):
    array elements must use TOKEN_IS_ARRAY_NUMBER / TOKEN_IS_ARRAY_STRING,
    not the scalar token types. The fix is validated indirectly by checking
    that the C writer accepts the output without an error (a wrong token type
    would cause bvnr_write_event to return False and raise BovnarWriteError).
    """

    def test_int_array_parses_without_error(self):
        with Writer.to_mem() as w:
            write_array(w, 'x', [1, 2, 3])

    def test_float_array_parses_without_error(self):
        with Writer.to_mem() as w:
            write_array(w, 'x', [1.0, 2.0, 3.0])

    def test_string_array_parses_without_error(self):
        with Writer.to_mem() as w:
            write_array(w, 'x', ['hello', 'world'])

    def test_int_array_roundtrips(self):
        result = _roundtrip([100, 200, 300])
        assert result == [100, 200, 300]

    def test_string_array_roundtrips(self):
        result = _roundtrip(['foo', 'bar', 'baz'])
        assert result == ['foo', 'bar', 'baz']

    def test_multirow_int_parses_without_error(self):
        with Writer.to_mem() as w:
            write_array(w, 'mat', [[1, 2], [3, 4]])

    def test_multirow_roundtrips(self):
        rows   = [[1, 2], [3, 4]]
        result = _roundtrip(rows)
        assert result == rows


@needs_lib
class TestWriteArrayInDumps:
    """write_array is exercised indirectly through dumps() for list values."""

    def test_list_value_in_dumps(self):
        doc = bovnar.loads(bovnar.dumps({'nums': [1, 2, 3]}))
        assert doc['nums'] == [1, 2, 3]

    def test_nested_list_in_dumps(self):
        out = bovnar.dumps({'mat': [[1, 2], [3, 4]]})
        doc = bovnar.loads(out)
        assert doc['mat'] == [[1, 2], [3, 4]]

    def test_list_of_dicts_in_dumps(self):
        data = {'items': [{'k': 1}, {'k': 2}]}
        out  = bovnar.dumps(data)
        doc  = bovnar.loads(out)
        assert doc['items'] == [{'k': 1}, {'k': 2}]

    def test_bool_elements_in_array(self):
        with Writer.to_mem() as w:
            write_array(w, 'flags', [True, False, True])
        out = w.get_output()
        assert b'true'  in out
        assert b'false' in out


@needs_lib
class TestWriteArraySpecialFloats:
    """Regression tests for Bug 2: float inf/nan must be encoded as bovnar
    special-number literals ($infinity$, $-infinity$, $nan$), not as Python
    repr() strings ('inf', '-inf', 'nan')."""

    def test_nan_in_array_does_not_raise(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('nan')])

    def test_pos_inf_in_array_does_not_raise(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('inf')])

    def test_neg_inf_in_array_does_not_raise(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('-inf')])

    def test_nan_encoded_as_special_literal(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('nan')])
        out = w.get_output()
        assert b'$nan$' in out

    def test_pos_inf_encoded_as_special_literal(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('inf')])
        out = w.get_output()
        assert b'$infinity$' in out

    def test_neg_inf_encoded_as_special_literal(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('-inf')])
        out = w.get_output()
        assert b'$-infinity$' in out

    def test_mixed_specials_and_normal_floats(self):
        with Writer.to_mem() as w:
            write_array(w, 'v', [float('nan'), float('inf'), float('-inf'), 1.5])
        out = w.get_output()
        assert b'$nan$' in out
        assert b'$infinity$' in out
        assert b'$-infinity$' in out
        assert b'1.5' in out
