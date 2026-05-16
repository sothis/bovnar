import math
import os
import tempfile
import pytest

from conftest import needs_lib

import bovnar
from bovnar.dom import DomDoc, DomNode, DomType
from bovnar.enums import BaseUnit, SIPrefix, ValueTypeFamily
from bovnar.exceptions import BovnarParseError


_SCALAR_INT    = b".x = 42;\n"
_SCALAR_UINT32 = b".x = <uint:32> 42;\n"
_SCALAR_INT8   = b".x = <sint:8> -5;\n"
_SCALAR_FLOAT  = b".v = <float:64,m/s> 9.81;\n"
_SCALAR_STRING = b'.s = "hello";\n'
_NULL          = b".n = ;\n"
_SYMBOL        = b".flag = ok;\n"
_STRUCT        = b".obj = { .a = 1; .b = 2; };\n"
_ARRAY_1D      = b".arr = [1, 2, 3];\n"
_ARRAY_2D      = b".mat = [1, 2]/[3, 4];\n"
_NESTED_STRUCT = b".outer = { .inner = { .val = 99; }; };\n"
_MULTI_KEY     = b".x = 1;\n.y = 2;\n.z = 3;\n"
_STRUCT_ARRAY  = b".recs = [{.a = 10; .b = 20;}];\n"
_TYPED_UNIT    = b".temp = <float:64,K> 300.0;\n"


@needs_lib
class TestDomDocParse:
    def test_parse_bytes(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert isinstance(doc, DomDoc)

    def test_parse_bytearray(self):
        doc = DomDoc.parse(bytearray(_SCALAR_INT))
        assert len(doc) == 1

    def test_parse_file(self):
        with tempfile.NamedTemporaryFile(suffix='.bvnr', delete=False) as f:
            f.write(_SCALAR_INT)
            name = f.name
        try:
            doc = DomDoc.parse_file(name)
            assert len(doc) == 1
        finally:
            os.unlink(name)

    def test_parse_fd(self):
        with tempfile.NamedTemporaryFile(suffix='.bvnr', delete=False) as f:
            f.write(_MULTI_KEY)
            name = f.name
        try:
            fd = os.open(name, os.O_RDONLY)
            try:
                doc = DomDoc.parse_fd(fd)
                assert len(doc) == 3
            finally:
                os.close(fd)
        finally:
            os.unlink(name)

    def test_invalid_bvnr_raises(self):
        with pytest.raises(BovnarParseError):
            DomDoc.parse(b"!!! this is not valid BVNR !!!")

    def test_empty_doc(self):
        doc = DomDoc.parse(b"")
        assert len(doc) == 0

    def test_repr_contains_ptr(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert 'DomDoc' in repr(doc)


@needs_lib
class TestDomDocAccess:
    def test_len(self):
        doc = DomDoc.parse(_MULTI_KEY)
        assert len(doc) == 3

    def test_contains_existing_key(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert 'x' in doc

    def test_contains_missing_key(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert 'missing' not in doc

    def test_getitem_returns_node(self):
        doc = DomDoc.parse(_SCALAR_INT)
        node = doc['x']
        assert isinstance(node, DomNode)

    def test_getitem_missing_raises_keyerror(self):
        doc = DomDoc.parse(_SCALAR_INT)
        with pytest.raises(KeyError):
            _ = doc['no_such_key']

    def test_entries_returns_list_of_tuples(self):
        doc = DomDoc.parse(_MULTI_KEY)
        entries = doc.entries()
        assert len(entries) == 3
        keys = [k for k, _ in entries]
        assert 'x' in keys
        assert 'y' in keys
        assert 'z' in keys

    def test_iter_yields_tuples(self):
        doc = DomDoc.parse(_MULTI_KEY)
        pairs = list(doc)
        assert len(pairs) == 3
        for k, v in pairs:
            assert isinstance(k, str)
            assert isinstance(v, DomNode)

    def test_to_dict_scalar(self):
        doc = DomDoc.parse(_SCALAR_INT)
        d   = doc.to_dict()
        assert isinstance(d, dict)
        assert d['x'] == 42

    def test_to_dict_multi(self):
        doc = DomDoc.parse(_MULTI_KEY)
        d   = doc.to_dict()
        assert d == {'x': 1, 'y': 2, 'z': 3}


@needs_lib
class TestDomDocLookup:
    def test_lookup_top_level(self):
        doc  = DomDoc.parse(_SCALAR_INT)
        node = doc.lookup('x')
        assert node is not None
        assert isinstance(node, DomNode)

    def test_lookup_missing_returns_none(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert doc.lookup('no_such') is None

    def test_lookup_nested_dot_path(self):
        doc  = DomDoc.parse(_NESTED_STRUCT)
        node = doc.lookup('outer.inner.val')
        assert node is not None
        assert node.as_i64() == 99

    def test_lookup_one_level_deep(self):
        doc  = DomDoc.parse(_NESTED_STRUCT)
        node = doc.lookup('outer.inner')
        assert node is not None
        assert node.dom_type == DomType.STRUCT


@needs_lib
class TestDomNodeType:
    def test_int_type(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert doc['x'].dom_type == DomType.INT

    def test_float_type(self):
        doc = DomDoc.parse(_SCALAR_FLOAT)
        assert doc['v'].dom_type == DomType.FLOAT

    def test_string_type(self):
        doc = DomDoc.parse(_SCALAR_STRING)
        assert doc['s'].dom_type == DomType.STRING

    def test_null_type(self):
        doc = DomDoc.parse(_NULL)
        assert doc['n'].dom_type == DomType.NULL

    def test_symbol_type(self):
        doc = DomDoc.parse(_SYMBOL)
        assert doc['flag'].dom_type == DomType.SYMBOL

    def test_struct_type(self):
        doc = DomDoc.parse(_STRUCT)
        assert doc['obj'].dom_type == DomType.STRUCT

    def test_array_type(self):
        doc = DomDoc.parse(_ARRAY_1D)
        assert doc['arr'].dom_type == DomType.ARRAY

    def test_repr_contains_type(self):
        doc  = DomDoc.parse(_SCALAR_INT)
        node = doc['x']
        assert 'DomNode' in repr(node)


@needs_lib
class TestDomNodeIntAccessors:
    def test_as_i64(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert doc['x'].as_i64() == 42

    def test_as_u64_positive(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert doc['x'].as_u64() == 42

    def test_as_i32(self):
        doc = DomDoc.parse(_SCALAR_UINT32)
        assert doc['x'].as_i32() == 42

    def test_as_u32(self):
        doc = DomDoc.parse(_SCALAR_UINT32)
        assert doc['x'].as_u32() == 42

    def test_as_i8_negative(self):
        doc = DomDoc.parse(_SCALAR_INT8)
        assert doc['x'].as_i8() == -5

    def test_as_u8_small(self):
        doc = DomDoc.parse(b".x = <uint:8> 200;\n")
        assert doc['x'].as_u8() == 200

    def test_as_i16(self):
        doc = DomDoc.parse(b".x = <sint:16> -1000;\n")
        assert doc['x'].as_i16() == -1000

    def test_as_u16(self):
        doc = DomDoc.parse(b".x = <uint:16> 8080;\n")
        assert doc['x'].as_u16() == 8080

    def test_wrong_type_raises(self):
        doc = DomDoc.parse(_SCALAR_STRING)
        with pytest.raises(ValueError):
            doc['s'].as_i64()

    def test_as_int_str_base10(self):
        doc = DomDoc.parse(_SCALAR_INT)
        s   = doc['x'].as_int_str(10)
        assert s == '42'

    def test_as_int_str_base16(self):
        doc = DomDoc.parse(b".x = <uint:8> 255;\n")
        s   = doc['x'].as_int_str(16)
        assert s.lower() in ('ff', '0xff')


@needs_lib
class TestDomNodeFloatAccessor:
    def test_as_float(self):
        doc = DomDoc.parse(_SCALAR_FLOAT)
        v   = doc['v'].as_float()
        assert math.isclose(v, 9.81, rel_tol=1e-9)

    def test_wrong_type_raises(self):
        doc = DomDoc.parse(_SCALAR_STRING)
        with pytest.raises(ValueError):
            doc['s'].as_float()


@needs_lib
class TestDomNodeStringAccessors:
    def test_as_str_string(self):
        doc = DomDoc.parse(_SCALAR_STRING)
        assert doc['s'].as_str() == 'hello'

    def test_as_str_symbol(self):
        doc = DomDoc.parse(_SYMBOL)
        assert doc['flag'].as_str() == 'ok'

    def test_as_str_wrong_type_raises(self):
        doc = DomDoc.parse(_SCALAR_INT)
        with pytest.raises(ValueError):
            doc['x'].as_str()


@needs_lib
class TestDomNodeNullIsNull:
    def test_is_null_true(self):
        doc = DomDoc.parse(_NULL)
        assert doc['n'].is_null()

    def test_is_null_false(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert not doc['x'].is_null()


@needs_lib
class TestDomNodeUnit:
    def test_unit_str_kelvin(self):
        doc = DomDoc.parse(_TYPED_UNIT)
        s   = doc['temp'].unit_str
        assert 'K' in s

    def test_value_type_family_float(self):
        doc = DomDoc.parse(_TYPED_UNIT)
        vt  = doc['temp'].value_type
        assert ValueTypeFamily(vt.family) == ValueTypeFamily.FLOAT

    def test_value_in_base_units_kelvin(self):
        doc = DomDoc.parse(_TYPED_UNIT)
        v   = doc['temp'].value_in_base_units()
        assert math.isclose(v, 300.0, rel_tol=1e-6)

    def test_value_in_base_units_km(self):
        doc = DomDoc.parse(b".d = <uint:32,k~m> 5;\n")
        v   = doc['d'].value_in_base_units()
        assert math.isclose(v, 5000.0, rel_tol=1e-6)

    def test_no_unit_str_empty(self):
        doc = DomDoc.parse(_SCALAR_INT)
        s   = doc['x'].unit_str
        assert isinstance(s, str)


@needs_lib
class TestDomNodeStructAccess:
    def test_len_struct(self):
        doc = DomDoc.parse(_STRUCT)
        assert len(doc['obj']) == 2

    def test_getitem_str_key(self):
        doc = DomDoc.parse(_STRUCT)
        a   = doc['obj']['a']
        assert isinstance(a, DomNode)
        assert a.as_i64() == 1

    def test_getitem_missing_struct_key_raises(self):
        doc = DomDoc.parse(_STRUCT)
        with pytest.raises(KeyError):
            _ = doc['obj']['no_such']

    def test_contains_struct_key(self):
        doc = DomDoc.parse(_STRUCT)
        assert 'a' in doc['obj']
        assert 'z' not in doc['obj']

    def test_entries_struct(self):
        doc = DomDoc.parse(_STRUCT)
        entries = doc['obj'].entries()
        assert len(entries) == 2
        keys = {k for k, _ in entries}
        assert keys == {'a', 'b'}

    def test_iter_struct(self):
        doc   = DomDoc.parse(_STRUCT)
        pairs = list(doc['obj'])
        assert len(pairs) == 2
        for k, v in pairs:
            assert isinstance(k, str)
            assert isinstance(v, DomNode)

    def test_getitem_wrong_type_raises(self):
        doc = DomDoc.parse(_SCALAR_INT)
        with pytest.raises(TypeError):
            _ = doc['x']['key']


@needs_lib
class TestDomNodeArrayAccess:
    def test_len_array(self):
        doc = DomDoc.parse(_ARRAY_1D)
        assert len(doc['arr']) == 3

    def test_getitem_int_index(self):
        doc  = DomDoc.parse(_ARRAY_1D)
        node = doc['arr'][0]
        assert isinstance(node, DomNode)
        assert node.as_i64() == 1

    def test_getitem_last_element(self):
        doc = DomDoc.parse(_ARRAY_1D)
        assert doc['arr'][2].as_i64() == 3

    def test_getitem_negative_index(self):
        doc = DomDoc.parse(_ARRAY_1D)
        assert doc['arr'][-1].as_i64() == 3

    def test_getitem_out_of_range_raises(self):
        doc = DomDoc.parse(_ARRAY_1D)
        with pytest.raises(IndexError):
            _ = doc['arr'][99]

    def test_iter_array(self):
        doc   = DomDoc.parse(_ARRAY_1D)
        nodes = list(doc['arr'])
        assert len(nodes) == 3
        assert all(isinstance(n, DomNode) for n in nodes)

    def test_array_values_correct(self):
        doc    = DomDoc.parse(_ARRAY_1D)
        values = [doc['arr'][i].as_i64() for i in range(3)]
        assert values == [1, 2, 3]

    def test_2d_array_count(self):
        doc = DomDoc.parse(_ARRAY_2D)
        assert len(doc['mat']) == 4

    def test_array_dims_1d(self):
        doc = DomDoc.parse(_ARRAY_1D)
        assert doc['arr'].array_dims() == 1

    def test_array_dims_2d(self):
        doc = DomDoc.parse(_ARRAY_2D)
        assert doc['mat'].array_dims() == 2

    def test_getitem_wrong_type_raises(self):
        doc = DomDoc.parse(_SCALAR_INT)
        with pytest.raises(TypeError):
            _ = doc['x'][0]

    def test_getitem_str_key_on_array_raises(self):
        doc = DomDoc.parse(_ARRAY_1D)
        with pytest.raises(TypeError):
            _ = doc['arr']['bad']


@needs_lib
class TestDomNodeToPython:
    def test_int_scalar(self):
        doc = DomDoc.parse(_SCALAR_INT)
        assert doc['x'].to_python() == 42

    def test_float_scalar(self):
        doc = DomDoc.parse(_SCALAR_FLOAT)
        v   = doc['v'].to_python()
        assert math.isclose(v, 9.81, rel_tol=1e-9)

    def test_string_scalar(self):
        doc = DomDoc.parse(_SCALAR_STRING)
        assert doc['s'].to_python() == 'hello'

    def test_null(self):
        doc = DomDoc.parse(_NULL)
        assert doc['n'].to_python() is None

    def test_struct(self):
        doc = DomDoc.parse(_STRUCT)
        d   = doc['obj'].to_python()
        assert isinstance(d, dict)
        assert d['a'] == 1
        assert d['b'] == 2

    def test_array(self):
        doc = DomDoc.parse(_ARRAY_1D)
        lst = doc['arr'].to_python()
        assert isinstance(lst, list)
        assert lst == [1, 2, 3]

    def test_nested_struct(self):
        doc = DomDoc.parse(_NESTED_STRUCT)
        d   = doc['outer'].to_python()
        assert d == {'inner': {'val': 99}}

    def test_struct_in_array(self):
        doc = DomDoc.parse(_STRUCT_ARRAY)
        lst = doc['recs'].to_python()
        assert isinstance(lst, list)
        assert len(lst) == 1
        assert lst[0] == {'a': 10, 'b': 20}


@needs_lib
class TestDomDocGcSafety:
    def test_node_survives_doc_in_scope(self):
        doc  = DomDoc.parse(_SCALAR_INT)
        node = doc['x']
        assert node.as_i64() == 42
        assert node._doc is doc

    def test_multiple_nodes_from_same_doc(self):
        doc = DomDoc.parse(_MULTI_KEY)
        nodes = [doc[k] for k in ('x', 'y', 'z')]
        values = [n.as_i64() for n in nodes]
        assert values == [1, 2, 3]
