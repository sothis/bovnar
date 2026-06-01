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
from bovnar.enums import (
    Event, ValueTypeFamily, PrefixSystem,
    SIPrefix, IECPrefix, BaseUnit, Exponent, ErrorCode,
)

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
        assert ValueTypeFamily.ILLEGAL == 8

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

        assert len(BaseUnit) == 379

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
        assert len(ErrorCode) == 42

    def test_roundtrip(self):
        for code in ErrorCode:
            assert ErrorCode(int(code)) is code
