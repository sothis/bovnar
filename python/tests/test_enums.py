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
import pathlib
import re

from bovnar.enums import (
    Event, TokenType, ValueTypeFamily, PrefixSystem,
    SIPrefix, IECPrefix, BaseUnit, Exponent, ErrorCode,
)

_HEADER = pathlib.Path(__file__).resolve().parents[2] / "include" / "bovnar.h"


def _c_enum(name: str) -> dict:
    """Parse `typedef enum <name>_e { ... }` out of the C header.

    Reading the header directly is the point: a hand-kept Python copy of a C
    enum only stays correct if something compares the two.
    """
    text = _HEADER.read_text(encoding="utf-8")
    m = re.search(r"typedef enum " + name + r"_e \{(.*?)\}", text, re.S)
    assert m, f"{name}_e not found in {_HEADER}"
    out, nxt = {}, 0
    for line in m.group(1).splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).split("//")[0].strip().rstrip(",")
        if not line:
            continue
        em = re.match(r"^(\w+)\s*(?:=\s*(-?\d+))?$", line)
        if not em:
            continue
        nxt = int(em.group(2)) if em.group(2) is not None else nxt
        out[em.group(1)] = nxt
        nxt += 1
    return out


class TestTokenTypeMirrorsC:
    # BvnrData.type reaches users as this raw int, and the writer selects
    # behaviour on it, so a drift here is silent and consequential.
    def test_every_c_token_type_is_mirrored(self):
        c = _c_enum("token_type")
        assert c, "parsed no token_type_e members"
        for cname, cval in c.items():
            pyname = cname[len("token_is_"):].upper()
            assert hasattr(TokenType, pyname), f"TokenType is missing {pyname}"
            assert int(getattr(TokenType, pyname)) == cval, (
                f"TokenType.{pyname} is {int(getattr(TokenType, pyname))}, "
                f"C {cname} is {cval}")

    def test_no_extra_python_members(self):
        c = {n[len("token_is_"):].upper() for n in _c_enum("token_type")}
        assert {m.name for m in TokenType} == c


class TestErrorCodeMirrorsC:
    def test_every_c_error_code_is_mirrored(self):
        c = _c_enum("error_code")
        assert c, "parsed no error_code_e members"
        by_value = {int(e): e.name for e in ErrorCode}
        for cname, cval in c.items():
            assert cval in by_value, f"ErrorCode is missing {cname} = {cval}"


class TestEvent:
    def test_stream_begin_is_zero(self):
        assert Event.STREAM_START == 0

    def test_assignment_start(self):
        assert Event.ASSIGNMENT_START == 1

    def test_data(self):
        assert Event.DATA == 9

    def test_type_annotation_family_param(self):
        assert Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM == 13

    def test_all_events_unique(self):
        values = [int(e) for e in Event]
        assert len(values) == len(set(values))

    def test_roundtrip_from_int(self):
        for ev in Event:
            assert Event(int(ev)) is ev

    def test_count(self):

        assert len(Event) == 15

class TestValueTypeFamily:
    def test_plain_is_zero(self):
        assert ValueTypeFamily.PLAIN == 0

    def test_illegal(self):
        assert ValueTypeFamily.BOOL == 7
        assert ValueTypeFamily.DATETIME == 8
        assert ValueTypeFamily.ILLEGAL == 9

    def test_numeric_families(self):
        assert ValueTypeFamily.UINT == 3
        assert ValueTypeFamily.SINT == 2
        assert ValueTypeFamily.FLOAT == 4
        assert ValueTypeFamily.FLOAT_FIX == 5
        assert ValueTypeFamily.FLOAT_DEC == 6

    def test_text_family(self):
        assert ValueTypeFamily.UTF8 == 1

class TestSIPrefix:
    def test_none_is_zero(self):
        assert SIPrefix.NONE == 0

    def test_kilo(self):
        assert SIPrefix.KILO == 15

    def test_milli(self):
        assert SIPrefix.MILLI == 10

    def test_all_unique(self):
        values = [int(p) for p in SIPrefix]
        assert len(values) == len(set(values))

    def test_count(self):

        assert len(SIPrefix) == 26

    def test_quecto_quetta(self):
        assert SIPrefix.QUECTO == 1
        assert SIPrefix.QUETTA == 24

class TestIECPrefix:
    def test_none_is_zero(self):
        assert IECPrefix.NONE == 0

    def test_kibi(self):
        assert IECPrefix.KIBI == 1

    def test_quebi(self):
        assert IECPrefix.QUEBI == 10

    def test_count(self):

        assert len(IECPrefix) == 12

    def test_all_unique(self):
        values = [int(p) for p in IECPrefix]
        assert len(values) == len(set(values))

class TestBaseUnit:
    def test_none_is_zero(self):
        assert BaseUnit.NONE == 0

    def test_si_base_units(self):

        assert BaseUnit.SECOND  == 3
        assert BaseUnit.METER   == 4
        assert BaseUnit.GRAM    == 5
        assert BaseUnit.AMPERE  == 6
        assert BaseUnit.KELVIN  == 7
        assert BaseUnit.MOL     == 8
        assert BaseUnit.CANDELA == 9

    def test_digital_units(self):
        assert BaseUnit.BIT  == 1
        assert BaseUnit.BYTE == 2

    def test_named_derived(self):
        assert BaseUnit.HERTZ  == 10
        assert BaseUnit.NEWTON == 11
        assert BaseUnit.PASCAL == 12
        assert BaseUnit.JOULE  == 13
        assert BaseUnit.WATT   == 14
        assert BaseUnit.VOLT   == 15
        assert BaseUnit.OHM    == 16
        assert BaseUnit.FARAD  == 17

    def test_non_si(self):
        assert BaseUnit.LITER   == 29
        assert BaseUnit.MINUTE  == 30
        assert BaseUnit.HOUR    == 31
        assert BaseUnit.DAY     == 32
        assert BaseUnit.DEGREE  == 33
        assert BaseUnit.CELSIUS == 34

    def test_angular_units(self):
        assert BaseUnit.RADIAN    == 35
        assert BaseUnit.STERADIAN == 36

    def test_mass_pressure(self):
        assert BaseUnit.TONNE == 37
        assert BaseUnit.BAR   == 38

    def test_atomic_units(self):
        assert BaseUnit.ELECTRONVOLT      == 39
        assert BaseUnit.DALTON            == 40
        assert BaseUnit.ASTRONOMICAL_UNIT == 41

    def test_area_time_units(self):
        assert BaseUnit.HECTARE == 42
        assert BaseUnit.WEEK    == 43
        assert BaseUnit.YEAR    == 44

    def test_all_unique(self):
        values = [int(u) for u in BaseUnit]
        assert len(values) == len(set(values))

    def test_count(self):

        assert len(BaseUnit) == 381

class TestExponent:
    def test_positive_exponents(self):
        assert Exponent.LINEAR  == 1
        assert Exponent.SQUARE  == 2
        assert Exponent.CUBIC   == 3
        assert Exponent.NONIC   == 9

    def test_negative_exponents(self):
        assert Exponent.NEG_LINEAR  == -1
        assert Exponent.NEG_SQUARE  == -2
        assert Exponent.NEG_NONIC   == -9

    def test_all_unique(self):
        values = [int(e) for e in Exponent]
        assert len(values) == len(set(values))

class TestErrorCode:
    def test_none_is_zero(self):
        assert ErrorCode.NONE == 0

    def test_spot_checks(self):
        assert ErrorCode.EMPTY_IDENTIFIER       == 4
        assert ErrorCode.ILLEGAL_ESCAPE_SEQUENCE == 9
        assert ErrorCode.INVALID_UTF8_BYTE      == 19
        assert ErrorCode.UNIT_ILLEGAL           == 32
        assert ErrorCode.VALUE_OUT_OF_RANGE     == 35
        assert ErrorCode.DIGIT_NOT_IN_BASE      == 36
        assert ErrorCode.RECOVERED              == 37
        assert ErrorCode.UNIT_MISMATCH               == 38
        assert ErrorCode.ARRAY_ELEMENT_TYPE_MISMATCH == 39
        assert ErrorCode.STRUCT_SHAPE_MISMATCH       == 40
        assert ErrorCode.DUPLICATE_STRUCT_KEY        == 41

    def test_all_unique(self):
        values = [int(e) for e in ErrorCode]
        assert len(values) == len(set(values))

    def test_count(self):
        assert len(ErrorCode) == 48

    def test_roundtrip(self):
        for code in ErrorCode:
            assert ErrorCode(int(code)) is code
