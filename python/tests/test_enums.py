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

        assert BaseUnit.SECOND  == 100002
        assert BaseUnit.METER   == 100003
        assert BaseUnit.GRAM    == 100004
        assert BaseUnit.AMPERE  == 100005
        assert BaseUnit.KELVIN  == 100006
        assert BaseUnit.MOL     == 100007
        assert BaseUnit.CANDELA == 100008

    def test_digital_units(self):
        assert BaseUnit.BIT  == 100000
        assert BaseUnit.BYTE == 100001

    def test_named_derived(self):
        assert BaseUnit.HERTZ  == 100009
        assert BaseUnit.NEWTON == 100010
        assert BaseUnit.PASCAL == 100011
        assert BaseUnit.JOULE  == 100012
        assert BaseUnit.WATT   == 100013
        assert BaseUnit.VOLT   == 100014
        assert BaseUnit.OHM    == 100015
        assert BaseUnit.FARAD  == 100016

    def test_non_si(self):
        assert BaseUnit.LITER   == 100028
        assert BaseUnit.MINUTE  == 100029
        assert BaseUnit.HOUR    == 100030
        assert BaseUnit.DAY     == 100031
        assert BaseUnit.DEGREE  == 100032
        assert BaseUnit.CELSIUS == 100033

    def test_angular_units(self):
        assert BaseUnit.RADIAN    == 100034
        assert BaseUnit.STERADIAN == 100035

    def test_mass_pressure(self):
        assert BaseUnit.TONNE == 100036
        assert BaseUnit.BAR   == 100037

    def test_atomic_units(self):
        assert BaseUnit.ELECTRONVOLT      == 100038
        assert BaseUnit.DALTON            == 100039
        assert BaseUnit.ASTRONOMICAL_UNIT == 100040

    def test_area_time_units(self):
        assert BaseUnit.HECTARE == 100041
        assert BaseUnit.WEEK    == 100042
        assert BaseUnit.YEAR    == 100043

    def test_all_unique(self):
        values = [int(u) for u in BaseUnit]
        assert len(values) == len(set(values))

    def test_count(self):

        # NONE, plus every native unit, plus every currency. There is no
        # sentinel: the id space is blocked and sparse, so nothing can be
        # bounded by "one past the last member". Adding a unit or a currency
        # moves this by exactly one per addition.
        assert len(BaseUnit) == 1 + 180 + 216

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
        assert len(ErrorCode) == 52

    def test_matches_the_c_enum(self):
        """Every code the LIBRARY recognises must have a Python member.

        A hardcoded count cannot catch a code added on the C side: the number
        and the enum go stale together and agree with each other, which is how
        UNIT_PROFILE_UNKNOWN and UNIT_PROFILE_UNSUPPORTED went missing. This
        asks the library instead -- bvn_error_to_string answers "unknown_error"
        for a value that is not a code, so the real ones are exactly the rest.

        Only membership is checked, not the name: the string is a diagnostic
        label rather than the enumerator's spelling, and at least one already
        differs on purpose (code 18 is error_got_incomplete_bvnr_stream and
        reports "incomplete_bvnr_stream").
        """
        import ctypes
        from bovnar._ffi import get_library
        lib = get_library()
        lib.bvn_error_to_string.restype = ctypes.c_char_p
        lib.bvn_error_to_string.argtypes = [ctypes.c_int]
        known = {int(e) for e in ErrorCode}
        # Scan well past the end so a newly appended code cannot hide.
        for value in range(0, 128):
            raw = lib.bvn_error_to_string(value)
            name = raw.decode() if raw else ""
            if not name or name == "unknown_error":
                continue
            assert value in known, (
                f"the library knows error code {value} ({name!r}) but "
                f"ErrorCode has no member for it")

    def test_roundtrip(self):
        for code in ErrorCode:
            assert ErrorCode(int(code)) is code
