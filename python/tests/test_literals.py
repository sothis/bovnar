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

"""bovnar.literals (the C validators, literal helpers and type-annotation
parser), the DOM builder (NewNode, DomDoc.create / add), and the ok flag of
unit_convert_factor."""

import gc
import math
import struct

import pytest

from conftest import needs_lib
import bovnar
from bovnar import (DomDoc, DomType, ErrorCode, NewNode, ValueTypeFamily as F,
                    literals, make_type_spec, parse_unit, unit_to_str)
from bovnar.exceptions import BovnarArgumentError, BovnarParseError

pytestmark = needs_lib


def vt(family, width=0, base=0):
    return make_type_spec(family, width, base)


def _code(text) -> ErrorCode:
    with pytest.raises(BovnarParseError) as ei:
        literals.parse_type_annotation(text)
    return ei.value.code


# ---------------------------------------------------------------------------
# parse_type_annotation

def test_a_type_annotation_parses_into_spec_unit_and_spelling():
    a = literals.parse_type_annotation("uint:32,_16,k~g")
    assert a.value_type == vt(F.UINT, 32, 16)
    assert unit_to_str(a.unit) == "k~g"
    assert a.unit_source == "k~g"
    # Parameters are told apart by their class, not their place.
    assert literals.parse_type_annotation("uint:_16,32").value_type == vt(F.UINT, 32, 16)
    assert literals.parse_type_annotation("uint: 32, _16").value_type == vt(F.UINT, 32, 16)

    plain = literals.parse_type_annotation("float")
    assert plain.value_type == vt(F.FLOAT)
    assert (plain.unit.num_components, plain.unit_source) == (0, "")

    assert literals.parse_type_annotation("float_fix:32,q8").value_type == vt(F.FLOAT_FIX, 32, 8)
    tai = literals.parse_type_annotation("datetime:64,tai")
    assert tai.value_type.base == bovnar.datetime_epoch_index("tai")
    assert (tai.unit.num_components, tai.unit_source) == (0, "")

    none = literals.parse_type_annotation("float:64,no_unit")
    assert (none.unit.num_components, none.unit_source) == (0, "no_unit")

    ml = literals.parse_type_annotation("float:64,ucum:mL{total}")
    assert (unit_to_str(ml.unit), ml.unit_source) == ("m~L", "ucum:mL{total}")
    cells = literals.parse_type_annotation("float:64,ucum:{cells,total},_16")
    assert (cells.unit_source, cells.value_type.base) == ("ucum:{cells,total}", 16)


@pytest.mark.parametrize("text", [
    "", "<uint:8>", "integer", "uint:", "uint:8,", "uint:8,,_16", "uint;8",
    "uint:8,16", "uint:_16,_16", "uint:8,m,s", "uint:32769", "uint:_1",
    "uint:_63", "uint:_10001", "sint:_64", "sint:_85", "bool:8", "bool:0",
    "utf8:m", "float:24", "float:_8", "float_fix:32,q32", "float_fix:_16",
    "float_dec:48", "uint:q8", "datetime:_16", "datetime:64,martian",
])
def test_a_malformed_annotation_is_illegal_value_type(text):
    assert _code(text) == ErrorCode.ILLEGAL_VALUE_TYPE


@pytest.mark.parametrize("text", [
    "uint:32768", "uint:_2", "uint:_62", "uint:_64", "uint:_85", "float:96",
    "float_fix:32,q31", "float_dec:256", "sint:_62",
])
def test_the_accepted_neighbours(text):
    literals.parse_type_annotation(text)


def test_a_bad_unit_names_which_flag_failed():
    assert _code("uint:8," + "m" * 256) == ErrorCode.UNIT_TOO_LONG
    assert _code("uint:8," + "m" * 255) == ErrorCode.UNIT_ILLEGAL
    assert _code("uint:8,parsec_per_fortnight") == ErrorCode.UNIT_ILLEGAL
    assert _code("uint:8,nosuch:m") == ErrorCode.UNIT_PROFILE_UNKNOWN
    # Nested past the scanner's bound: no text was kept, refused all the same.
    assert _code("uint:8," + "{" * 17) == ErrorCode.UNIT_ILLEGAL
    with pytest.raises(TypeError):
        literals.parse_type_annotation(b"uint:8")


@pytest.mark.parametrize("text", ["uint:16,_16,k~m", "sint:8", "float:32,m/s",
                                  "float_fix:16,q4"])
def test_an_annotation_parses_as_a_document_reads_it(text):
    a = literals.parse_type_annotation(text)
    q = bovnar.loads(f".v = <{text}> 1;".encode(), typed=True)["v"]
    assert q.vtype == a.value_type
    assert bytes(bovnar.unit.Unit(a.unit).raw) == bytes(q.unit.raw)


# ---------------------------------------------------------------------------
# Validators

@pytest.mark.parametrize("s", ["a", "_", "_a9", "Ab", "a-b", "a+b", "é", "aé",
                               "Àx", "x\U0010ffff"])
def test_identifiers(s):
    assert literals.validate_identifier(s) and literals.validate_symbol(s)


@pytest.mark.parametrize("s", ["", "1a", "-a", "a b", "a.b", "a,b", "a:b",
                               "a=b", "a b", "¿", "a\x7f", "a\x00b",
                               "a\tb", "a\ud800"])
def test_not_identifiers(s):
    assert not literals.validate_identifier(s)
    assert not literals.validate_symbol(s)


def test_references():
    for s in [".a", ".a.b", ".a[0]", ".a[0][12].b", ".é.x-y"]:
        assert literals.validate_reference(s), s
    for s in ["", "a", ".", ".a.", "..a", ".1", ".a[]", ".a[x]", ".a[0]b",
              "&.a", ".a b", ".a\x00"]:
        assert not literals.validate_reference(s), s
    with pytest.raises(TypeError):
        literals.validate_reference(b".a")


def test_the_writer_key_check_agrees_with_the_c_validator():
    # Writer keys go through _check_key, which adds the reader's 255-byte cap.
    for key in ["mux", "_", "a-b", "é", "a b", "a.b", "1a", "", "a ",
                "À", "x~", "a+", "a" * 255]:
        try:
            bovnar.dumps({key: 1})
            accepted = True
        except BovnarArgumentError:
            accepted = False
        assert accepted == literals.validate_identifier(key), key
    assert literals.validate_identifier("a" * 256)        # no length limit here


def test_number_validators():
    for s in ["0", "-1", "01", "1.", ".5", "1.5e3", "1E+5", "1e-5"]:
        assert literals.validate_number(s), s
    for s in ["", "-", "+1", "1e", "e5", ".", "1.2.3", "1e5.0", "inf", "0x1", " 1"]:
        assert not literals.validate_number(s), s
    for s in ["inf", "ninf", "nan"]:
        assert literals.is_special_number_string(s)
    for s in ["-inf", "+inf", "NaN", "infinity", "", "in"]:
        assert not literals.is_special_number_string(s), s

    v = literals.validate_digits_for_base
    assert v("ff", 16) and v("-FF", 16) and not v("+ff", 16) and not v("fg", 16)
    assert v("zZ", 62) and v("+/", 64) and v("-", 85)
    assert not v("2", 2) and not v("", 10) and not v("-", 10)
    # The base is not checked against those a type may declare.
    assert v("0", 1) and not v("1", 1) and not v("0", 0) and v("zZ", 99)
    with pytest.raises(BovnarArgumentError):
        v("1", -1)

    n = literals.validate_number_in_base
    assert n("+ff", 16) and n("1.8p3", 16) and n("1.5e3", 10) and n("1p4", 8)
    assert not n("1p4", 12) and not n("1e", 10) and not n(".", 10)
    assert n("1e5", 15)                         # 'e' is the digit 14 there
    assert n("+5.", 85) == v("+5.", 85)


def test_range_validators():
    u, s = literals.validate_uint_range, literals.validate_sint_range
    assert u("255", 8) and not u("256", 8)
    assert u("ff", 8, 16) and not u("100", 8, 16)
    assert not u("-0", 8) and u("nan", 8) and u("9" * 30, 0)
    assert u(str(2**128 - 1), 128) and not u(str(2**128), 128)
    assert not u("1" * 40, 128)
    # Not digits: refused, though strtoull reads "" and " 5" as numbers.
    for bad in ("", "+", " 5", "5 ", "1_0"):
        assert not u(bad, 8) and not u(bad, 128) and not s(bad, 8)
    assert not s("-", 8) and not s(" -5", 128)
    assert s("127", 8) and not s("128", 8) and s("-128", 8) and not s("-129", 8)
    assert s("-80", 8, 16) and not s("-81", 8, 16)
    assert s(str(-2**127), 128) and not s(str(-2**127 - 1), 128)
    assert s(str(2**127 - 1), 128) and not s(str(2**127), 128)


@pytest.mark.parametrize("data", [
    b"abc", b"\xc3\xa9", b"\xc3", b"\xed\xa0\x80", b"\xc0\xae",
    b"\xf4\x90\x80\x80", b"\x00\x01\x7f", b"\xe2\x82", b"\xf0\x9f\x98\x80", b"",
])
def test_validate_string_is_utf8_well_formedness(data):
    try:
        data.decode('utf-8')
        native = True
    except UnicodeDecodeError:
        native = False
    assert literals.validate_string(data) == native
    assert literals.validate_string(bytearray(data)) == native
    assert literals.validate_string(memoryview(data)) == native


def test_validate_string_takes_bytes():
    with pytest.raises(TypeError):
        literals.validate_string("abc")


def test_char_to_digit():
    c = literals.char_to_digit
    assert c('a', 16) == 10 and c('A', 16) == 10 and c('A', 62) == 36
    assert c('g', 16) is None
    assert c('A', 64) == 0 and c('+', 64) == 62
    assert c('!', 85) == 0 and c('u', 85) == 84 and c('v', 85) is None
    assert c('0', 1) == 0 and c('0', 0) is None and c('é', 62) is None
    with pytest.raises(TypeError):
        c('ab', 16)


def test_min_digits_for_type():
    m = literals.min_digits_for_type
    assert m(vt(F.UINT, 8, 16)) == 2
    assert m(vt(F.UINT, 8)) == 3
    assert m(vt(F.SINT, 8, 2)) == 7
    assert m(vt(F.UINT, 64)) == 20
    assert m(vt(F.UINT, 0)) == 0
    assert m(vt(F.UINT, 128)) == 0
    assert m(vt(F.UTF8)) == 0
    assert m(vt(F.FLOAT, 64)) == 20   # answered as an unsigned integer


# ---------------------------------------------------------------------------
# Formatters and parsers

def test_integer_formatters():
    f = literals.format_uint64
    assert f(255, 16) == "ff" and f(5, 2, 8) == "00000101" and f(0) == "0"
    assert f(2**64 - 1, 2) == "1" * 64
    assert f(61, 62) == "Z" and f(62, 64) == "+"
    assert len(f(1, 10, 100)) == 64          # min_digits is capped at 64
    for base in (0, 1, 63, 86):
        with pytest.raises(BovnarArgumentError):
            f(1, base)
    for bad in (-1, 2**64, 1.0):
        with pytest.raises(BovnarArgumentError):
            f(bad)
    g = literals.format_int64
    assert g(-255, 16, 4) == "-00ff"
    assert g(-2**63) == str(-2**63)
    assert g(5, 85) == "&"
    for base in (64, 85):
        with pytest.raises(BovnarArgumentError):
            g(-5, base)
    with pytest.raises(BovnarArgumentError):
        g(2**63)


def test_float_formatter():
    f64 = vt(F.FLOAT, 64)
    f = literals.format_double
    assert f(0.1, f64) == "1e-1"
    assert f(-0.0, f64) == "-0.0"
    assert f(math.inf, f64) == "inf" and f(math.nan, f64) == "nan"
    assert f(5e-324, f64) == "5e-324" and f(1e300, f64) == "1e+300"
    assert f(0.1, vt(F.FLOAT, 64, 16)) == "1.999999999999ap-4"
    for x in (0.1, 1 / 3, 6.02214076e23, -2.5e-310, 1.7976931348623157e308,
              2.2250738585072014e-308):
        assert literals.parse_double(f(x, f64), f64) == x
    # float:16 and float:32 round to IEEE binary16 and binary32 first, as
    # the C writer writes them, and refuse what the format cannot hold.
    assert f(2049.0, vt(F.FLOAT, 16)) == "2.048e+3"
    assert f(16777217.0, vt(F.FLOAT, 32)) == "1.6777216e+7"
    assert f(65504.0, vt(F.FLOAT, 16)) == "6.5504e+4"
    assert f(0.1, vt(F.FLOAT, 16)) == "9.9976e-2"
    assert f(math.inf, vt(F.FLOAT, 16)) == "inf"
    for x, w in ((1e5, 16), (65520.0, 16), (1e39, 32), (-1e300, 32)):
        with pytest.raises(BovnarArgumentError):
            f(x, vt(F.FLOAT, w))
    # Base 10 (or 0) and 16 only.
    assert f(1.5, f64) == "1.5e+0"
    for base in (2, 8, 36, 62):
        with pytest.raises(BovnarArgumentError):
            f(1.0, vt(F.FLOAT, 64, base))


def test_integer_parsers_bound_by_the_type():
    u8 = vt(F.UINT, 8)
    p = literals.parse_uint64
    assert p("255", u8) == 255
    for bad in ("256", "-1", "+1", " 1", "", "1\x00"):
        with pytest.raises(BovnarArgumentError):
            p(bad, u8)
    assert p(str(2**64 - 1), vt(F.UINT)) == 2**64 - 1
    with pytest.raises(BovnarArgumentError):
        p(str(2**64), vt(F.UINT))
    s8 = vt(F.SINT, 8)
    q = literals.parse_int64
    assert q("-128", s8) == -128
    for bad in ("-129", "128", "-"):
        with pytest.raises(BovnarArgumentError):
            q(bad, s8)
    assert q(str(-2**63), vt(F.SINT, 64)) == -2**63
    assert q("-zZ", vt(F.SINT, 64, 62)) == -(35 * 62 + 61)
    assert p("-.", vt(F.UINT, 64, 85)) == 12 * 85 + 13
    assert p("+", vt(F.UINT, 64, 64)) == 62
    # strtoll's 0x prefix in base 16 is refused, as the reader refuses it.
    for bad in ("0x10", "0X10"):
        with pytest.raises(BovnarArgumentError):
            p(bad, vt(F.UINT, 64, 16))
    for bad in ("0x10", "-0x10"):
        with pytest.raises(BovnarArgumentError):
            q(bad, vt(F.SINT, 64, 16))
    assert p("10", vt(F.UINT, 64, 16)) == 16 and q("-0", vt(F.SINT, 8, 16)) == 0
    with pytest.raises(BovnarParseError):
        bovnar.loads(b".a = <uint:64,_16> 0x10;")


def test_float_parsers():
    f64 = vt(F.FLOAT, 64)
    p = literals.parse_double
    assert p("1.5", f64) == 1.5 and p("+5", f64) == 5.0
    assert p("inf", f64) == math.inf and p("ninf", f64) == -math.inf
    assert math.isnan(p("nan", f64))
    for bad in ("-inf", "", "abc", "1x", "1e", "0x1p3"):
        with pytest.raises(BovnarArgumentError):
            p(bad, f64)
    # Correctly rounded: the largest subnormal, not the smallest normal.
    largest_subnormal = struct.unpack('<d', struct.pack('<Q', 0x000fffffffffffff))[0]
    assert p("2.2250738585072011e-308", f64) == largest_subnormal
    b = literals.parse_double_in_base
    assert b("1.8p1", 16) == 3.0 and b("1e3", 16) == 483.0
    for base in (8, 2):
        with pytest.raises(BovnarArgumentError):
            b("1.5", base)


def test_the_escape_table():
    assert literals.escape_repl_table() == {
        '"': '"', '\\': '\\', 'f': '\f', 'n': '\n', 'r': '\r', 't': '\t',
        'v': '\v',
    }


# ---------------------------------------------------------------------------
# unit_convert_factor's ok

def test_unit_conversion_reports_ok():
    c = bovnar.unit_convert_factor(parse_unit("k~m"), parse_unit("m"))
    assert c.ok and not c.requires_affine and c.factor == 1000.0
    c = bovnar.unit_convert_factor(parse_unit("°C"), parse_unit("K"))
    assert not c.ok and c.requires_affine and c.factor == 0.0
    assert bovnar.convert_value(0.0, parse_unit("°C"), parse_unit("K")) == 273.15
    c = bovnar.unit_convert_factor(parse_unit("°C"), parse_unit("m~°C"))
    assert c.ok and c.requires_affine and c.factor == 1000.0
    # Built with two arguments, as before: ok defaults to True.
    assert bovnar.UnitConversion(2.0, False).ok
    with pytest.raises(BovnarArgumentError):
        bovnar.unit_convert_factor(parse_unit("m"), parse_unit("s"))


# ---------------------------------------------------------------------------
# The DOM builder

def _fixture() -> DomDoc:
    server = NewNode.struct()
    server.add("port", NewNode.integer(8080, width=16))
    server.add("offset", NewNode.integer(-5, width=8))
    server.add("tls", NewNode.null())
    ports = NewNode.array()
    for p in (80, 443):
        ports.append(NewNode.integer(p, width=32))
    ports.append(NewNode.struct())
    server.add("ports", ports)
    doc = DomDoc.create()
    doc.add("server", server)
    doc.add("big", NewNode.integer(2**100, width=128))
    return doc


def test_a_built_tree_reads_back_through_the_getters():
    doc = _fixture()
    assert len(doc) == 2 and doc.parse_error == ErrorCode.NONE
    port = doc["server.port"]
    assert port.dom_type == DomType.INT and port.as_u16() == 8080
    assert port.value_type == vt(F.UINT, 16) and port.unit_str == ''
    assert doc["server.offset"].as_i64() == -5
    assert doc["server.offset"].value_type == vt(F.SINT, 8)
    assert doc["server.tls"].is_null()
    assert doc["server.ports[1]"].as_u32() == 443
    assert len(doc["server.ports"]) == 3
    assert doc["server.ports"].array_dims() == 0
    big = doc["big"]
    assert big.as_int_str(16) == "1" + "0" * 25
    with pytest.raises(ValueError):
        big.as_u64()
    assert doc.to_dict() == {
        "server": {"port": 8080, "offset": -5, "tls": None,
                   "ports": [80, 443, {}]},
        "big": 2**100,
    }


@pytest.mark.parametrize("width", [8, 16, 32, 64])
def test_every_narrow_width_keeps_its_type(width):
    doc = DomDoc.create()
    lo, hi = -(1 << (width - 1)), (1 << width) - 1
    doc.add("s", NewNode.integer(lo, width=width))
    doc.add("u", NewNode.integer(hi, width=width))
    doc.add("p", NewNode.integer(1, width=width, signed=True))
    assert doc["s"].value_type == vt(F.SINT, width)
    assert doc["u"].value_type == vt(F.UINT, width)
    assert doc["p"].value_type == vt(F.SINT, width)
    assert doc.to_dict() == {"s": lo, "u": hi, "p": 1}
    for bad, signed in ((hi + 1, None), (lo - 1, None), (hi, True), (-1, False)):
        with pytest.raises(BovnarArgumentError):
            NewNode.integer(bad, width=width, signed=signed)


def test_wide_integers_bounds_and_units():
    i = NewNode.integer
    i(2**128 - 1, width=128)
    i(-2**127, width=128)
    i(2**127 - 1, width=128, signed=True)
    for bad, signed in ((2**128, None), (-2**127 - 1, None), (2**127, True),
                        (-1, False)):
        with pytest.raises(BovnarArgumentError):
            i(bad, width=128, signed=signed)
    for width in (0, 7, 12, 63, 32769, 64.0):
        with pytest.raises(BovnarArgumentError):
            i(1, width=width)
    i(1, width=65)
    i(2**32768 - 1, width=32768)
    with pytest.raises(TypeError):
        i(1.0)
    with pytest.raises(BovnarArgumentError):
        i(1, width=64, unit="m")         # the narrow constructors take none
    with pytest.raises(BovnarArgumentError):
        i(1, width=128, unit="no such unit")

    doc = DomDoc.create()
    d = doc.add("d", i(-2**100, width=256, unit="k~m"))
    assert d.value_type == vt(F.SINT, 256)
    assert d.unit_str == "k~m" and d.unit_source is None
    assert d.to_python() == -2**100
    assert d.value_in_base_units() == -(2.0 ** 100) * 1000.0
    e = doc.add("e", i(7, width=100, unit=bovnar.Unit.parse("s")))
    assert e.unit_str == "s"


def test_a_node_is_attached_once():
    doc = DomDoc.create()
    n = NewNode.integer(1)
    doc.add("a", n)
    # Spent: adding it again, or anywhere else, is refused.
    with pytest.raises(BovnarArgumentError):
        doc.add("b", n)
    s = NewNode.struct()
    with pytest.raises(BovnarArgumentError):
        s.add("x", n)
    with pytest.raises(BovnarArgumentError):
        NewNode.array().append(n)
    with pytest.raises(BovnarArgumentError):
        n.dom_type
    assert repr(n) == "NewNode(attached)"
    # A struct filled after it was attached would reach into the tree: refused.
    t = NewNode.struct()
    doc.add("t", t)
    with pytest.raises(BovnarArgumentError):
        t.add("x", NewNode.null())
    # Nor can a node go into itself, or a child into its spent parent.
    with pytest.raises(BovnarArgumentError):
        s.add("self", s)
    child = NewNode.struct()
    s.add("child", child)
    with pytest.raises(BovnarArgumentError):
        child.add("parent", s)
    assert doc.to_dict() == {"a": 1, "t": {}}


def test_refused_adds_leave_the_node_with_the_caller():
    doc = DomDoc.create()
    doc.add("a", NewNode.integer(1))
    n = NewNode.integer(2)
    with pytest.raises(BovnarArgumentError):
        doc.add("a", n)                   # duplicate
    for key in ["", "1a", "a.b", "a[0]", "a b", "a\x00b"]:
        with pytest.raises(BovnarArgumentError):
            doc.add(key, n)
    with pytest.raises(TypeError):
        doc.add(1, n)
    doc.add("b", n)                       # still the caller's, and usable
    assert doc.to_dict() == {"a": 1, "b": 2}

    s = NewNode.struct()
    s.add("x", NewNode.null())
    m = NewNode.null()
    with pytest.raises(BovnarArgumentError):
        s.add("x", m)
    assert len(s) == 1 and m.dom_type == DomType.NULL
    # Kind mismatches.
    with pytest.raises(BovnarArgumentError):
        NewNode.array().add("x", m)
    with pytest.raises(BovnarArgumentError):
        s.append(m)
    with pytest.raises(TypeError):
        s.add("y", doc["a"])              # a DomNode view is no NewNode
    s.add("y", m)
    assert len(s) == 2


def test_a_parsed_document_takes_built_nodes_too():
    doc = DomDoc.parse(b".a = 1;")
    doc.add("b", NewNode.integer(2, width=8))
    with pytest.raises(BovnarArgumentError):
        doc.add("a", NewNode.null())
    assert doc.to_dict() == {"a": 1, "b": 2}


def test_a_view_from_add_keeps_the_document_alive():
    node = DomDoc.create().add("n", NewNode.integer(5))
    gc.collect()
    assert node.as_i64() == 5


def test_unattached_nodes_are_freed_cleanly():
    # Collected before they are attached, with children: each frees its own
    # subtree once (run under valgrind or ASan for the leak check; here it
    # must at least not crash).
    for _ in range(500):
        s = NewNode.struct()
        a = NewNode.array()
        a.append(NewNode.integer(1))
        a.append(NewNode.integer(2**200, width=256))
        s.add("a", a)
        del s, a
        NewNode.null()
        NewNode.integer(-7, width=100)
    gc.collect()


def test_an_empty_built_document():
    doc = DomDoc.create()
    assert len(doc) == 0 and doc.to_dict() == {} and doc.lookup("a") is None


def test_validate_string_refuses_a_non_buffer():
    for bad in (5, 0, None, [1, 2]):
        with pytest.raises(TypeError):
            literals.validate_string(bad)
    assert literals.validate_string(bytearray(b'ok'))
    assert literals.validate_string(memoryview(b'\xc3\xa9'))
