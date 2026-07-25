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

import ctypes
import math
import pytest

from conftest import needs_lib
import bovnar
from bovnar.enums import (
    BaseUnit, SIPrefix, IECPrefix, Exponent,
    PrefixSystem, ValueTypeFamily, Event,
)
from bovnar.structs import (
    ValueUnit, ValueUnitPrefix,
    make_unit_si, make_unit_iec, make_unit_dimensionless,
    make_unit_none,
)
from bovnar.exceptions import BovnarArgumentError
from bovnar.reader import Reader
from bovnar.units import (
    UnitFlags,
    unit_valid, unit_prefix_factor, unit_prefix_exponent,
    prefix_unit_valid,
    unit_to_si_factor, units_compatible, unit_convert_factor,
    unit_dimension_vector, unit_reduce,
    unit_to_str_ex,
    exponent_to_int, int_to_exponent,
    convert_value,
)


class TestUnitHelpersPure:

    def test_dimensionless_is_none_base(self):
        vu = make_unit_dimensionless()
        assert vu.num_components == 1
        assert vu.components[0].base_unit == BaseUnit.NONE

    def test_si_meter_no_prefix(self):
        vu = make_unit_si(BaseUnit.METER)
        c  = vu.components[0]
        assert c.base_unit    == BaseUnit.METER
        assert c.prefix_system == PrefixSystem.SI
        assert c.si_prefix     == SIPrefix.NONE
        assert c.exp           == Exponent.LINEAR

    def test_si_kilogram(self):
        vu = make_unit_si(BaseUnit.GRAM, SIPrefix.KILO)
        c  = vu.components[0]
        assert c.si_prefix == SIPrefix.KILO

    def test_si_meter_squared(self):
        vu = make_unit_si(BaseUnit.METER, SIPrefix.NONE, Exponent.SQUARE)
        assert vu.components[0].exp == Exponent.SQUARE

    def test_iec_gibibyte(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI)
        c  = vu.components[0]
        assert c.prefix_system == PrefixSystem.IEC
        assert c.iec_prefix    == IECPrefix.GIBI
        assert c.base_unit     == BaseUnit.BYTE

    def test_unit_none_has_zero_components(self):
        vu = make_unit_none()
        assert vu.num_components == 0
        assert vu.is_dimensionless

    def test_active_components_length(self):
        vu = make_unit_si(BaseUnit.SECOND)
        assert len(vu.active_components()) == 1


class TestUnitFlags:

    def test_none_is_zero(self):
        assert int(UnitFlags.NONE) == 0

    def test_reduce_bit(self):
        assert int(UnitFlags.REDUCE) == 1

    def test_ascii_exp_bit(self):
        assert int(UnitFlags.ASCII_EXP) == 2

    def test_combine_flags(self):
        combined = UnitFlags.REDUCE | UnitFlags.ASCII_EXP
        assert int(combined) == 3

    def test_none_is_falsy(self):
        assert not UnitFlags.NONE

    def test_reduce_is_truthy(self):
        assert UnitFlags.REDUCE

    def test_membership(self):
        f = UnitFlags.REDUCE | UnitFlags.ASCII_EXP
        assert UnitFlags.REDUCE in f
        assert UnitFlags.ASCII_EXP in f

    def test_from_int(self):
        assert UnitFlags(0) == UnitFlags.NONE
        assert UnitFlags(1) == UnitFlags.REDUCE
        assert UnitFlags(2) == UnitFlags.ASCII_EXP
        assert UnitFlags(3) == UnitFlags.REDUCE | UnitFlags.ASCII_EXP


class TestExponentToInt:

    @needs_lib
    def test_linear(self):
        assert exponent_to_int(Exponent.LINEAR) == 1

    @needs_lib
    def test_square(self):
        assert exponent_to_int(Exponent.SQUARE) == 2

    @needs_lib
    def test_cubic(self):
        assert exponent_to_int(Exponent.CUBIC) == 3

    @needs_lib
    def test_neg_linear(self):
        assert exponent_to_int(Exponent.NEG_LINEAR) == -1

    @needs_lib
    def test_neg_square(self):
        assert exponent_to_int(Exponent.NEG_SQUARE) == -2

    @needs_lib
    def test_neg_cubic(self):
        assert exponent_to_int(Exponent.NEG_CUBIC) == -3

    @needs_lib
    def test_nonic(self):
        assert exponent_to_int(Exponent.NONIC) == 9

    @needs_lib
    def test_neg_nonic(self):
        assert exponent_to_int(Exponent.NEG_NONIC) == -9

    @needs_lib
    def test_invalid_returns_zero(self):
        assert exponent_to_int(Exponent.INVALID) == 0

    @needs_lib
    def test_raw_int_accepted(self):
        assert exponent_to_int(int(Exponent.LINEAR)) == 1


class TestIntToExponent:

    @needs_lib
    def test_1_is_linear(self):
        assert int_to_exponent(1) == Exponent.LINEAR

    @needs_lib
    def test_2_is_square(self):
        assert int_to_exponent(2) == Exponent.SQUARE

    @needs_lib
    def test_3_is_cubic(self):
        assert int_to_exponent(3) == Exponent.CUBIC

    @needs_lib
    def test_neg1_is_neg_linear(self):
        assert int_to_exponent(-1) == Exponent.NEG_LINEAR

    @needs_lib
    def test_neg2_is_neg_square(self):
        assert int_to_exponent(-2) == Exponent.NEG_SQUARE

    @needs_lib
    def test_neg9_is_neg_nonic(self):
        assert int_to_exponent(-9) == Exponent.NEG_NONIC

    @needs_lib
    def test_9_is_nonic(self):
        assert int_to_exponent(9) == Exponent.NONIC

    @needs_lib
    def test_zero_is_invalid(self):
        assert int_to_exponent(0) == Exponent.INVALID

    @needs_lib
    def test_roundtrip_all_valid(self):
        for n in range(-9, 10):
            if n == 0:
                continue
            exp = int_to_exponent(n)
            assert exponent_to_int(exp) == n


@needs_lib
class TestUnitParsing:
    def _parse(self, unit_str: str) -> ValueUnit:
        return bovnar.parse_unit(unit_str)

    def test_simple_meter(self):
        vu = self._parse("m")
        assert vu.num_components == 1
        assert vu.components[0].base_unit == BaseUnit.METER

    def test_kilometer(self):
        vu = self._parse("k~m")
        c  = vu.components[0]
        assert c.base_unit == BaseUnit.METER
        assert c.si_prefix == SIPrefix.KILO

    def test_gibibytes(self):
        vu = self._parse("Gi~B")
        c  = vu.components[0]
        assert c.base_unit    == BaseUnit.BYTE
        assert c.prefix_system == PrefixSystem.IEC
        assert c.iec_prefix    == IECPrefix.GIBI

    def test_meters_per_second(self):
        vu = self._parse("m/s")
        assert vu.num_components == 2
        m, s = vu.components[0], vu.components[1]
        assert m.base_unit == BaseUnit.METER
        assert s.base_unit == BaseUnit.SECOND
        assert s.exp       == Exponent.NEG_LINEAR

    def test_acceleration_unicode(self):
        vu = self._parse("m/s\u00b2")
        assert vu.num_components == 2
        assert vu.components[1].exp == Exponent.NEG_SQUARE

    def test_acceleration_ascii_caret(self):
        vu = self._parse("m/s^2")
        assert vu.num_components == 2
        assert vu.components[1].exp == Exponent.NEG_SQUARE

    @pytest.mark.parametrize("tok", ["Da", "dalton", "amu", "u"])
    def test_dalton_aliases(self, tok):
        # Da is the canonical symbol; dalton/amu/u are accepted input aliases
        # (amu and u = unified atomic mass unit). All resolve to bu_dalton and
        # serialise back to "Da".
        vu = self._parse(tok)
        assert vu.num_components == 1
        assert vu.components[0].base_unit == BaseUnit.DALTON
        assert bovnar.unit_to_str(vu) == "Da"

    def test_single_char_u_does_not_shadow_longer_units(self):
        # Guard: adding the 1-char "u" alias must not preempt longest-suffix
        # matching — "au" is still the astronomical unit, "mol" still the mole.
        assert self._parse("au").components[0].base_unit == BaseUnit.ASTRONOMICAL_UNIT
        assert self._parse("mol").components[0].base_unit == BaseUnit.MOL
        assert self._parse("µ~u").components[0].base_unit == BaseUnit.DALTON  # micro-dalton

    @pytest.mark.parametrize("ascii_form,canonical", [
        ("u~m", "µ~m"), ("u~s", "µ~s"), ("u~Pa", "µ~Pa"),
    ])
    def test_ascii_u_micro_prefix(self, ascii_form, canonical):
        # "u" is accepted as an ASCII input alias for the micro prefix "µ";
        # canonical output always normalises back to "µ".
        vu = self._parse(ascii_form)
        assert vu.components[0].si_prefix == SIPrefix.MICRO
        assert bovnar.unit_to_str(vu) == canonical

    def test_u_micro_prefix_and_dalton_base_coexist(self):
        # "u" is BOTH the micro prefix and the dalton base; longest-suffix
        # matching keeps them unambiguous. "u~u" (and "uu") = micro-dalton.
        vu = self._parse("u~u")
        assert vu.components[0].si_prefix == SIPrefix.MICRO
        assert vu.components[0].base_unit == BaseUnit.DALTON
        assert bovnar.unit_to_str(vu) == "µ~Da"

    def test_force_kgms2(self):
        vu = self._parse("k~g\u00b7m/s\u00b2")
        assert vu.num_components == 3
        kg = vu.components[0]
        m  = vu.components[1]
        s  = vu.components[2]
        assert kg.base_unit == BaseUnit.GRAM
        assert kg.si_prefix == SIPrefix.KILO
        assert m.base_unit  == BaseUnit.METER
        assert s.base_unit  == BaseUnit.SECOND
        assert s.exp        == Exponent.NEG_SQUARE

    def test_no_unit_keyword(self):
        vu = self._parse("no_unit")
        assert vu.is_dimensionless

    def test_kelvin(self):
        vu = self._parse("K")
        assert vu.components[0].base_unit == BaseUnit.KELVIN

    def test_megahertz(self):
        vu = self._parse("M~Hz")
        c = vu.components[0]
        assert c.base_unit == BaseUnit.HERTZ
        assert c.si_prefix == SIPrefix.MEGA

    def test_tebibyte(self):
        vu = self._parse("Ti~B")
        c = vu.components[0]
        assert c.base_unit    == BaseUnit.BYTE
        assert c.prefix_system == PrefixSystem.IEC
        assert c.iec_prefix    == IECPrefix.TEBI

    def test_product_asterisk(self):
        vu = self._parse("m*s")
        assert vu.num_components == 2
        assert vu.components[0].base_unit == BaseUnit.METER
        assert vu.components[1].base_unit == BaseUnit.SECOND

    def test_invalid_unit_raises(self):
        with pytest.raises(BovnarArgumentError):
            self._parse("x-m")

    def test_invalid_unit_empty_component(self):
        with pytest.raises(BovnarArgumentError):
            self._parse("m//s")

    def test_too_many_components(self):
        with pytest.raises(BovnarArgumentError):
            self._parse("m*s*k~g*A*K*mol*cd*b*B")

    def test_celsius(self):
        vu = self._parse("\u00b0C")
        assert vu.components[0].base_unit == BaseUnit.CELSIUS


@needs_lib
class TestUnitValid:

    def test_meter_valid(self):
        assert unit_valid(make_unit_si(BaseUnit.METER))

    def test_kilogram_valid(self):
        assert unit_valid(make_unit_si(BaseUnit.GRAM, SIPrefix.KILO))

    def test_gibibyte_valid(self):
        assert unit_valid(make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI))

    def test_dimensionless_valid(self):
        assert unit_valid(make_unit_dimensionless())

    def test_unit_none_valid(self):
        assert unit_valid(make_unit_none())

    def test_celsius_valid(self):
        assert unit_valid(make_unit_si(BaseUnit.CELSIUS))

    def test_parsed_unit_valid(self):
        vu = bovnar.parse_unit("k~g\u00b7m/s\u00b2")
        assert unit_valid(vu)


@needs_lib
class TestUnitPrefixFactor:

    def test_no_prefix_is_one(self):
        vu = make_unit_si(BaseUnit.METER)
        assert math.isclose(unit_prefix_factor(vu), 1.0)

    def test_kilo_is_1000(self):
        vu = make_unit_si(BaseUnit.METER, SIPrefix.KILO)
        assert math.isclose(unit_prefix_factor(vu), 1000.0)

    def test_milli_is_1e_neg3(self):
        vu = make_unit_si(BaseUnit.SECOND, SIPrefix.MILLI)
        assert math.isclose(unit_prefix_factor(vu), 1e-3)

    def test_mega_is_1e6(self):
        vu = make_unit_si(BaseUnit.HERTZ, SIPrefix.MEGA)
        assert math.isclose(unit_prefix_factor(vu), 1e6)

    def test_gibi_is_2_pow_30(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI)
        assert math.isclose(unit_prefix_factor(vu), 2**30)

    def test_kibi_is_2_pow_10(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.KIBI)
        assert math.isclose(unit_prefix_factor(vu), 2**10)

    def test_compound_kilo_numerator(self):
        vu = bovnar.parse_unit("k~m/s")
        assert math.isclose(unit_prefix_factor(vu), 1000.0)

    def test_compound_milli_denominator(self):
        vu = bovnar.parse_unit("m/m~s")
        assert math.isclose(unit_prefix_factor(vu), 1e3)

    def test_dimensionless_is_one(self):
        assert math.isclose(unit_prefix_factor(make_unit_dimensionless()), 1.0)


@needs_lib
class TestUnitPrefixExponent:

    def test_no_prefix_is_zero(self):
        vu = make_unit_si(BaseUnit.METER)
        assert unit_prefix_exponent(vu) == 0

    def test_kilo_is_3(self):
        vu = make_unit_si(BaseUnit.METER, SIPrefix.KILO)
        assert unit_prefix_exponent(vu) == 3

    def test_mega_is_6(self):
        vu = make_unit_si(BaseUnit.HERTZ, SIPrefix.MEGA)
        assert unit_prefix_exponent(vu) == 6

    def test_milli_is_neg3(self):
        vu = make_unit_si(BaseUnit.SECOND, SIPrefix.MILLI)
        assert unit_prefix_exponent(vu) == -3

    def test_gibi_is_30(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI)
        assert unit_prefix_exponent(vu) == 30

    def test_kibi_is_10(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.KIBI)
        assert unit_prefix_exponent(vu) == 10

    def test_dimensionless_is_zero(self):
        assert unit_prefix_exponent(make_unit_dimensionless()) == 0


@needs_lib
class TestPrefixUnitValid:

    def test_si_none_on_meter_valid(self):
        p = ValueUnitPrefix.make_si(SIPrefix.NONE)
        assert prefix_unit_valid(p, BaseUnit.METER)

    def test_si_kilo_on_meter_valid(self):
        p = ValueUnitPrefix.make_si(SIPrefix.KILO)
        assert prefix_unit_valid(p, BaseUnit.METER)

    def test_si_milli_on_second_valid(self):
        p = ValueUnitPrefix.make_si(SIPrefix.MILLI)
        assert prefix_unit_valid(p, BaseUnit.SECOND)

    def test_iec_gibi_on_byte_valid(self):
        p = ValueUnitPrefix.make_iec(IECPrefix.GIBI)
        assert prefix_unit_valid(p, BaseUnit.BYTE)

    def test_iec_kibi_on_bit_valid(self):
        p = ValueUnitPrefix.make_iec(IECPrefix.KIBI)
        assert prefix_unit_valid(p, BaseUnit.BIT)

    def test_iec_gibi_on_meter_invalid(self):
        p = ValueUnitPrefix.make_iec(IECPrefix.GIBI)
        assert not prefix_unit_valid(p, BaseUnit.METER)

    def test_iec_kibi_on_second_invalid(self):
        p = ValueUnitPrefix.make_iec(IECPrefix.KIBI)
        assert not prefix_unit_valid(p, BaseUnit.SECOND)

    def test_iec_mebi_on_gram_invalid(self):
        p = ValueUnitPrefix.make_iec(IECPrefix.MEBI)
        assert not prefix_unit_valid(p, BaseUnit.GRAM)

    def test_prefix_from_parsed_unit_component(self):
        vu = bovnar.parse_unit("Gi~B")
        comp = vu.components[0]
        assert prefix_unit_valid(comp.prefix, comp.base_unit)


@needs_lib
class TestUnitSerialisation:
    def test_meter(self):
        vu  = make_unit_si(BaseUnit.METER)
        s   = bovnar.unit_to_str(vu)
        assert s == "m"

    def test_kilometer(self):
        vu = make_unit_si(BaseUnit.GRAM, SIPrefix.KILO)
        s  = bovnar.unit_to_str(vu)
        assert s == "k~g"

    def test_gibibyte(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI)
        s  = bovnar.unit_to_str(vu)
        assert s == "Gi~B"

    def test_roundtrip_simple(self):
        vu1 = bovnar.parse_unit("m/s")
        s   = bovnar.unit_to_str(vu1)
        vu2 = bovnar.parse_unit(s)
        assert vu2.num_components == vu1.num_components

    def test_roundtrip_compound(self):
        orig = "k~g\u00b7m/s\u00b2"
        vu   = bovnar.parse_unit(orig)
        out  = bovnar.unit_to_str(vu)
        vu2  = bovnar.parse_unit(out)
        assert vu2.num_components == vu.num_components


@needs_lib
class TestUnitToStrEx:

    def test_ascii_exp_flag_produces_caret(self):
        vu = bovnar.parse_unit("m/s\u00b2")
        s  = unit_to_str_ex(vu, UnitFlags.ASCII_EXP)
        assert "^" in s
        assert "\u00b2" not in s

    def test_no_flags_produces_unicode(self):
        vu = bovnar.parse_unit("m/s\u00b2")
        s  = unit_to_str_ex(vu, UnitFlags.NONE)
        assert "\u00b2" in s

    def test_default_flags_same_as_none(self):
        vu = bovnar.parse_unit("m/s\u00b2")
        assert unit_to_str_ex(vu) == unit_to_str_ex(vu, UnitFlags.NONE)

    def test_ascii_exp_roundtrips(self):
        vu1 = bovnar.parse_unit("k~g\u00b7m/s\u00b2")
        s   = unit_to_str_ex(vu1, UnitFlags.ASCII_EXP)
        vu2 = bovnar.parse_unit(s)
        assert vu2.num_components == vu1.num_components
        for i in range(vu1.num_components):
            assert vu2.components[i].base_unit == vu1.components[i].base_unit

    def test_reduce_flag_simplifies_force(self):
        vu = bovnar.parse_unit("k~g\u00b7m/s\u00b2")
        s  = unit_to_str_ex(vu, UnitFlags.REDUCE)
        assert s == "N"

    def test_reduce_and_ascii_exp_combined(self):
        vu = bovnar.parse_unit("m\u00b2")
        s  = unit_to_str_ex(vu, UnitFlags.REDUCE | UnitFlags.ASCII_EXP)
        assert isinstance(s, str)
        assert len(s) > 0

    def test_int_flags_accepted(self):
        vu = bovnar.parse_unit("m/s")
        s  = unit_to_str_ex(vu, 0)
        assert "m" in s


@needs_lib
class TestUnitFactor:
    def test_no_prefix_factor_is_one(self):
        f = bovnar.unit_factor("m")
        assert math.isclose(f, 1.0)

    def test_kilo_factor(self):
        f = bovnar.unit_factor("k~m")
        assert math.isclose(f, 1000.0)

    def test_milli_factor(self):
        f = bovnar.unit_factor("m~s")
        assert math.isclose(f, 1e-3)

    def test_kilo_in_numerator(self):
        f = bovnar.unit_factor("k~m/s")
        assert math.isclose(f, 1000.0)

    def test_denominator_inverts(self):
        f = bovnar.unit_factor("m/k~s")
        assert math.isclose(f, 1e-3)

    def test_gibi_factor(self):
        f = bovnar.unit_factor("Gi~B")
        assert math.isclose(f, 2**30)

    def test_dimensionless_factor(self):
        f = bovnar.unit_factor("no_unit")
        assert math.isclose(f, 1.0)

    def test_micro_factor(self):
        f = bovnar.unit_factor("\u00b5~m")
        assert math.isclose(f, 1e-6)

    def test_invalid_unit_raises(self):
        with pytest.raises(BovnarArgumentError):
            bovnar.unit_factor("XYZ")


@needs_lib
class TestUnitInEventStream:
    def _get_unit_from_param(self, payload: bytes) -> ValueUnit | None:
        found = [None]
        def cb(ev, d):
            if ev == Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM and d:
                vu = d.value_unit
                if not vu.is_dimensionless:
                    found[0] = d.value_unit
            return True
        with Reader() as r:
            r.read_mem(payload, on_verified=cb)
        return found[0]

    def test_float_velocity_unit(self, simple_scalar_float):
        vu = self._get_unit_from_param(simple_scalar_float)
        assert vu is not None
        assert vu.num_components == 2
        bases = [vu.components[i].base_unit for i in range(2)]
        assert BaseUnit.METER  in bases
        assert BaseUnit.SECOND in bases

    def test_iec_unit_in_event(self, iec_unit_payload):
        vu = self._get_unit_from_param(iec_unit_payload)
        assert vu is not None
        c = vu.components[0]
        assert c.prefix_system == PrefixSystem.IEC
        assert c.iec_prefix    == IECPrefix.GIBI
        assert c.base_unit     == BaseUnit.BYTE

    def test_compound_unit_in_event(self, compound_unit_payload):
        vu = self._get_unit_from_param(compound_unit_payload)
        assert vu is not None
        assert vu.num_components == 3

    def test_si_temperature_unit(self, all_si_units):
        units_found = []
        def cb(ev, d):
            if ev == Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM and d:
                if not d.value_unit.is_dimensionless:
                    units_found.append(d.value_unit)
            return True
        with Reader() as r:
            r.read_mem(all_si_units, on_verified=cb)
        assert len(units_found) >= 1

    def test_no_annotation_dimensionless(self, untyped_integer):
        found = []
        def cb(ev, d):
            if ev == Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM and d:
                found.append(d.value_unit.is_dimensionless)
            return True
        with Reader() as r:
            r.read_mem(untyped_integer, on_verified=cb)
        assert any(found)


@needs_lib
class TestWriterUnitAnnotation:
    def test_unit_str_in_output(self):
        from bovnar.writer import Writer
        with Writer.to_mem() as w:
            w.write_float("velocity", 9.81, width=64, unit_str="m/s")
        out = w.get_output()
        assert b'm' in out

    def test_iec_unit_in_output(self):
        from bovnar.writer import Writer
        with Writer.to_mem() as w:
            w.write_uint("ram", 8, width=64,
                         unit_iec_base=BaseUnit.BYTE,
                         unit_iec_prefix=IECPrefix.GIBI)
        out = w.get_output()
        assert b'ram' in out


from bovnar.structs import make_unit_compound

class TestMakeUnitCompoundPhysics:

    def test_two_component_velocity_structure(self):
        vu = make_unit_compound([
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_LINEAR},
        ])
        assert vu.num_components == 2

    def test_force_structure(self):
        vu = make_unit_compound([
            {'base': BaseUnit.GRAM,   'exp': Exponent.LINEAR,     'si_prefix': SIPrefix.KILO},
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE},
        ])
        assert vu.num_components == 3


@needs_lib
class TestMakeUnitCompoundRoundtrip:

    def test_velocity_roundtrip(self):
        vu  = make_unit_compound([
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_LINEAR},
        ])
        s   = bovnar.unit_to_str(vu)
        vu2 = bovnar.parse_unit(s)
        assert vu2.num_components == 2

    def test_force_roundtrip(self):
        vu  = make_unit_compound([
            {'base': BaseUnit.GRAM,   'exp': Exponent.LINEAR,     'si_prefix': SIPrefix.KILO},
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE},
        ])
        s   = bovnar.unit_to_str(vu)
        vu2 = bovnar.parse_unit(s)
        assert vu2.num_components == vu.num_components

    def test_gibibyte_roundtrip(self):
        vu  = make_unit_compound([
            {'base': BaseUnit.BYTE, 'iec_prefix': IECPrefix.GIBI},
        ])
        s   = bovnar.unit_to_str(vu)
        vu2 = bovnar.parse_unit(s)
        assert vu2.num_components == 1


@needs_lib
class TestMakeUnitCompoundCompatibility:

    def test_velocity_compatible_with_parsed(self):
        vu_built  = make_unit_compound([
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_LINEAR},
        ])
        vu_parsed = bovnar.parse_unit("m/s")
        assert bovnar.units_compatible(vu_built, vu_parsed)

    def test_force_compatible_with_parsed(self):
        vu_built  = make_unit_compound([
            {'base': BaseUnit.GRAM,   'exp': Exponent.LINEAR,     'si_prefix': SIPrefix.KILO},
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE},
        ])
        vu_parsed = bovnar.parse_unit("k~g\u00b7m/s\u00b2")
        assert bovnar.units_compatible(vu_built, vu_parsed)


@needs_lib
class TestNewUnitParsing:

    def _parse(self, s):
        return bovnar.parse_unit(s)

    def _base(self, s):
        return self._parse(s).components[0].base_unit

    def test_kgf_symbol(self):
        assert self._base("kgf") == BaseUnit.KILOGRAM_FORCE

    def test_kgf_long(self):
        assert self._base("kilogram_force") == BaseUnit.KILOGRAM_FORCE

    def test_inHg_symbol(self):
        assert self._base("inHg") == BaseUnit.INCH_HG

    def test_inHg_long(self):
        assert self._base("inch_hg") == BaseUnit.INCH_HG

    def test_inch_mercury_long(self):
        assert self._base("inch_mercury") == BaseUnit.INCH_HG

    def test_rpm_symbol(self):
        assert self._base("rpm") == BaseUnit.RPM

    def test_ft_lb_symbol(self):
        assert self._base("ft_lb") == BaseUnit.FOOT_POUND

    def test_foot_pound_long(self):
        assert self._base("foot_pound") == BaseUnit.FOOT_POUND

    def test_foot_pounds_plural(self):
        assert self._base("foot_pounds") == BaseUnit.FOOT_POUND

    def test_dr_symbol(self):
        assert self._base("dr") == BaseUnit.DRAM

    def test_dram_long(self):
        assert self._base("dram") == BaseUnit.DRAM

    def test_drams_plural(self):
        assert self._base("drams") == BaseUnit.DRAM

    def test_dwt_symbol(self):
        assert self._base("dwt") == BaseUnit.PENNYWEIGHT

    def test_pennyweight_long(self):
        assert self._base("pennyweight") == BaseUnit.PENNYWEIGHT

    def test_pennyweights_plural(self):
        assert self._base("pennyweights") == BaseUnit.PENNYWEIGHT

    def test_ch_symbol(self):
        assert self._base("ch") == BaseUnit.CHAIN

    def test_chain_long(self):
        assert self._base("chain") == BaseUnit.CHAIN

    def test_chains_plural(self):
        assert self._base("chains") == BaseUnit.CHAIN

    def test_rd_symbol(self):
        assert self._base("rd") == BaseUnit.ROD

    def test_rod_long(self):
        assert self._base("rod") == BaseUnit.ROD

    def test_rods_plural(self):
        assert self._base("rods") == BaseUnit.ROD

    def test_gi_symbol(self):
        assert self._base("gi") == BaseUnit.GILL

    def test_gill_long(self):
        assert self._base("gill") == BaseUnit.GILL

    def test_gills_plural(self):
        assert self._base("gills") == BaseUnit.GILL

    def test_gi_uk_symbol(self):
        assert self._base("gi_uk") == BaseUnit.GILL_UK

    def test_gill_uk_long(self):
        assert self._base("gill_uk") == BaseUnit.GILL_UK

    def test_gills_uk_plural(self):
        assert self._base("gills_uk") == BaseUnit.GILL_UK


@needs_lib
class TestCompactPrefixForm:
    """A prefix may be written without the '~' separator: "kg" == "k~g".

    The compact spelling is an input alias only — it parses to the same
    ValueUnit and serialises back to the separated canonical form, so nothing
    downstream (the pint bridge, the writer, a stored document) sees a new
    spelling unless a human typed it.
    """

    def _parse(self, s):
        return bovnar.parse_unit(s)

    @pytest.mark.parametrize("compact,separated", [
        ("kg",    "k~g"),
        ("km",    "k~m"),
        ("ms",    "m~s"),
        ("us",    "u~s"),
        ("µs", "µ~s"),
        ("MHz",   "M~Hz"),
        ("hPa",   "h~Pa"),
        ("mmol",  "m~mol"),
        ("mL",    "m~L"),
        ("MeV",   "M~eV"),
        ("Mpc",   "M~pc"),
        ("KiB",   "Ki~B"),
        ("MiB",   "Mi~B"),
        ("Gbit",  "G~bit"),
        ("km/h",  "k~m/h"),
        ("kg·m/s²", "k~g·m/s²"),
        ("k$USD", "k~$USD"),      # currencies too: the '$' sigil separates
        ("M$EUR", "M~$EUR"),
        ("G$ETH", "G~$ETH"),
    ])
    def test_compact_equals_separated(self, compact, separated):
        a, b = self._parse(compact), self._parse(separated)
        assert a.num_components == b.num_components
        for i in range(a.num_components):
            ca, cb = a.components[i], b.components[i]
            assert ca.base_unit     == cb.base_unit
            assert ca.prefix_system == cb.prefix_system
            assert ca.si_prefix     == cb.si_prefix
            assert ca.exp           == cb.exp
        assert unit_prefix_factor(a) == unit_prefix_factor(b)

    def test_serialises_back_to_the_separated_form(self):
        assert bovnar.unit_to_str(self._parse("kg")) == "k~g"
        assert bovnar.unit_to_str(self._parse("MiB")) == "Mi~B"

    @pytest.mark.parametrize("tok,base", [
        ("min", BaseUnit.MINUTE),                # not milli-inch
        ("cd",  BaseUnit.CANDELA),               # not centi-day
        ("ft",  BaseUnit.FOOT),                  # not femto-tonne
        ("at",  BaseUnit.ATMOSPHERE_TECHNICAL),  # not atto-tonne
        ("au",  BaseUnit.ASTRONOMICAL_UNIT),     # not atto-dalton
        ("PS",  BaseUnit.METRIC_HORSEPOWER),     # not peta-siemens
    ])
    def test_bare_alias_wins(self, tok, base):
        c = self._parse(tok).components[0]
        assert c.base_unit == base
        assert c.si_prefix == SIPrefix.NONE

    @pytest.mark.parametrize("tok", [
        "kkg", "k~kg",       # a prefix cannot be stacked
        "k~~g", "kg~",       # malformed separated form, not a compact one
        "Kim", "mB",         # prefix policy still applies
        "pH", "mph", "kph",  # refused by name (units.bvnr .compact_exceptions)
        "kWh", "mcg",        # never were units, still are not
        "kUSD", "k$XYZ",     # the currency sigil and a known code stay required
        "Ki$USD", "kg$USD",
    ])
    def test_rejected(self, tok):
        with pytest.raises(BovnarArgumentError):
            self._parse(tok)

    @pytest.mark.parametrize("tok,base,prefix", [
        ("p~H",  BaseUnit.HENRY, SIPrefix.PICO),
        ("m~ph", BaseUnit.PHOT,  SIPrefix.MILLI),
        ("nH",   BaseUnit.HENRY, SIPrefix.NANO),
    ])
    def test_refusal_is_per_spelling(self, tok, base, prefix):
        c = self._parse(tok).components[0]
        assert c.base_unit == base
        assert c.si_prefix == prefix


@needs_lib
class TestNewUnitPrefixFactorAndExponent:

    def _p(self, s):
        return bovnar.parse_unit(s)

    def test_kgf_prefix_factor_is_one(self):
        assert math.isclose(unit_prefix_factor(self._p("kgf")), 1.0)

    def test_rpm_prefix_factor_is_one(self):
        assert math.isclose(unit_prefix_factor(self._p("rpm")), 1.0)

    def test_kilo_kgf_prefix_factor_is_1000(self):
        assert math.isclose(unit_prefix_factor(self._p("k~kgf")), 1000.0)

    def test_kgf_prefix_exponent_is_zero(self):
        assert unit_prefix_exponent(self._p("kgf")) == 0

    def test_kilo_kgf_prefix_exponent_is_3(self):
        assert unit_prefix_exponent(self._p("k~kgf")) == 3


@needs_lib
class TestNewUnitSerialization:

    def _roundtrip(self, sym):
        vu1 = bovnar.parse_unit(sym)
        s   = bovnar.unit_to_str(vu1)
        vu2 = bovnar.parse_unit(s)
        assert vu1.num_components == vu2.num_components
        assert vu2.components[0].base_unit == vu1.components[0].base_unit

    def test_roundtrip_kgf(self):         self._roundtrip("kgf")
    def test_roundtrip_inHg(self):        self._roundtrip("inHg")
    def test_roundtrip_rpm(self):         self._roundtrip("rpm")
    def test_roundtrip_ft_lb(self):       self._roundtrip("ft_lb")
    def test_roundtrip_dr(self):          self._roundtrip("dr")
    def test_roundtrip_dwt(self):         self._roundtrip("dwt")
    def test_roundtrip_ch(self):          self._roundtrip("ch")
    def test_roundtrip_rd(self):          self._roundtrip("rd")
    def test_roundtrip_gi(self):          self._roundtrip("gi")
    def test_roundtrip_gi_uk(self):       self._roundtrip("gi_uk")


@needs_lib
class TestNewUnitCompatibility:

    def _compat(self, a, b):
        return bovnar.units_compatible(bovnar.parse_unit(a), bovnar.parse_unit(b))

    def test_kgf_compatible_with_newton(self):    assert self._compat("kgf", "N")
    def test_kgf_compatible_with_lbf(self):       assert self._compat("kgf", "lbf")
    def test_kgf_compatible_with_dyn(self):       assert self._compat("kgf", "dyn")
    def test_kgf_compatible_with_kip(self):       assert self._compat("kgf", "kip")

    def test_inHg_compatible_with_pa(self):       assert self._compat("inHg", "Pa")
    def test_inHg_compatible_with_atm(self):      assert self._compat("inHg", "atm")
    def test_inHg_compatible_with_mmHg(self):     assert self._compat("inHg", "mmHg")
    def test_inHg_compatible_with_psi(self):      assert self._compat("inHg", "psi")
    def test_inHg_compatible_with_bar(self):      assert self._compat("inHg", "bar")

    def test_rpm_compatible_with_hz(self):        assert self._compat("rpm", "Hz")
    def test_rpm_compatible_with_bq(self):        assert self._compat("rpm", "Bq")

    def test_ft_lb_compatible_with_joule(self):   assert self._compat("ft_lb", "J")
    def test_ft_lb_compatible_with_cal(self):     assert self._compat("ft_lb", "cal")
    def test_ft_lb_compatible_with_btu(self):     assert self._compat("ft_lb", "Btu")
    def test_ft_lb_compatible_with_erg(self):     assert self._compat("ft_lb", "erg")
    def test_ft_lb_compatible_with_eV(self):      assert self._compat("ft_lb", "eV")

    def test_dr_compatible_with_oz(self):         assert self._compat("dr", "oz")
    def test_dr_compatible_with_lb(self):         assert self._compat("dr", "lb")
    def test_dr_compatible_with_gram(self):       assert self._compat("dr", "g")
    def test_dwt_compatible_with_oz_t(self):      assert self._compat("dwt", "oz_t")
    def test_dwt_compatible_with_lb(self):        assert self._compat("dwt", "lb")
    def test_dwt_compatible_with_dr(self):        assert self._compat("dwt", "dr")

    def test_ch_compatible_with_meter(self):      assert self._compat("ch", "m")
    def test_ch_compatible_with_foot(self):       assert self._compat("ch", "ft")
    def test_ch_compatible_with_mile(self):       assert self._compat("ch", "mi")
    def test_rd_compatible_with_meter(self):      assert self._compat("rd", "m")
    def test_rd_compatible_with_ch(self):         assert self._compat("rd", "ch")

    def test_gi_compatible_with_fl_oz(self):      assert self._compat("gi", "fl_oz")
    def test_gi_compatible_with_pint(self):       assert self._compat("gi", "pt")
    def test_gi_compatible_with_liter(self):      assert self._compat("gi", "L")
    def test_gi_uk_compatible_with_pt_uk(self):   assert self._compat("gi_uk", "pt_uk")
    def test_gi_uk_compatible_with_gi(self):      assert self._compat("gi_uk", "gi")
    def test_gi_uk_compatible_with_gal_uk(self):  assert self._compat("gi_uk", "gal_uk")


class TestReaderWantUnit:
    """Lossless read-time unit/base conversion via the Reader want_unit hook."""

    @needs_lib
    def test_multiplicative_conversion(self):
        seen = []

        def want(d):
            return make_unit_si(BaseUnit.METER)   # deliver lengths in metres

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append(d.converted_str())
            return True

        Reader().read_mem(b'.d = 5 k~m;', on_verified=on_event, want_unit=want)
        assert seen == ['5000']                    # exact

    @needs_lib
    def test_affine_conversion(self):
        seen = []

        def want(d):
            return make_unit_si(BaseUnit.KELVIN)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append(d.converted_str())
            return True

        Reader().read_mem('.t = 25 °C;'.encode('utf-8'),
                          on_verified=on_event, want_unit=want)
        assert seen == ['298.15']                  # exact

    @needs_lib
    def test_incompatible_is_unit_mismatch(self):
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return make_unit_si(BaseUnit.SECOND)   # seconds for a length

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem(b'.d = 5 k~m;', on_verified=lambda e, d: True,
                              want_unit=want)
        assert ei.value.code == ErrorCode.UNIT_MISMATCH

    @needs_lib
    def test_irrational_is_unit_inexact(self):
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return make_unit_si(BaseUnit.RADIAN)   # degree -> radian is π-based

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem('.a = 90 °;'.encode('utf-8'),
                              on_verified=lambda e, d: True, want_unit=want)
        assert ei.value.code == ErrorCode.UNIT_INEXACT

    @needs_lib
    def test_decline_leaves_value_native(self):
        flags = []

        def want(d):
            return None                            # decline every value

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.type != 0:
                flags.append(bool(d.converted))
            return True

        Reader().read_mem(b'.d = 5 k~m;', on_verified=on_event, want_unit=want)
        assert flags and not any(flags)

    @needs_lib
    def test_pure_base_conversion(self):
        # same unit, different output base: return (unit, base)
        seen = []

        def want(d):
            return (d.value_unit, 16)              # keep unit, render base 16

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append((d.converted_str(), d.conv.base))
            return True

        Reader().read_mem(b'.n = 255;', on_verified=on_event, want_unit=want)
        assert seen == [('ff', 16)]

    @needs_lib
    def test_non_decimal_base_string_carrier_converts(self):
        # A non-decimal base is written as a string literal but still has a
        # numeric family — it must convert. Ask for base-10 output explicitly.
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.METER), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append(d.converted_str())
            return True

        Reader().read_mem(b'.x = <uint:32,_16> "FF" k~m;',
                          on_verified=on_event, want_unit=want)
        assert seen == ['255000']                  # 0xFF k~m -> 255000 m, exact

    @needs_lib
    def test_real_string_never_converted(self):
        flags = []

        def want(d):
            return make_unit_si(BaseUnit.METER)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.type != 0:
                flags.append(bool(d.converted))
            return True

        Reader().read_mem(b'.s = "hello";', on_verified=on_event, want_unit=want)
        assert flags and not any(flags)

    @needs_lib
    def test_multiprecision_integer_converts_losslessly(self):
        # A >64-bit integer converts EXACTLY (no double rounding): uint128 max
        # times 1000, delivered as full decimal digits.
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.METER), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append((d.converted_str(), d.raw_bytes()))
            return True

        big = b'340282366920938463463374607431768211455'   # uint128 max
        Reader().read_mem(b'.x = <uint:128> ' + big + b' k~m;',
                          on_verified=on_event, want_unit=want)
        assert len(seen) == 1
        val, raw = seen[0]
        assert val == '340282366920938463463374607431768211455000'  # exact ×1000
        assert raw == big                              # exact source preserved

    @needs_lib
    def test_wide_float_converts_losslessly(self):
        # A 1056-bit binary float converts EXACTLY: 70 significant digits
        # survive k~m -> m, far beyond what a double could carry.
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.METER), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append(d.converted_str())
            return True

        digits = ('1.234567890123456789012345678901234567890'
                  '123456789012345678901234567891')
        Reader().read_mem(('.d = <float:1056> %s k~m;' % digits).encode(),
                          on_verified=on_event, want_unit=want)
        assert seen == ['1234.567890123456789012345678901234567890'
                        '123456789012345678901234567891']    # exact x1000

    @needs_lib
    def test_high_output_bases(self):
        # Any base bvnr can write is a valid output base, not just 2..36:
        # 255 = 4*62+7 = "47", = 3*64+63 = "D/", = 3*85+0 = "$!".
        for base, expect in ((36, '73'), (62, '47'), (64, 'D/'), (85, '$!')):
            seen = []

            def want(d, _b=base):
                return (make_unit_dimensionless(), _b)

            def on_event(ev, d):
                if ev == Event.DATA and d is not None and d.converted:
                    seen.append((d.converted_str(), d.conv.base))
                return True

            Reader().read_mem(b'.n = 255;', on_verified=on_event, want_unit=want)
            assert seen == [(expect, base)]

    @needs_lib
    def test_native_high_base_is_kept(self):
        # base 0 means "keep the value's own" — including a base above 36, which
        # used to be silently rewritten to 10.
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.METER), 0)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append((d.converted_str(), d.conv.base))
            return True

        Reader().read_mem(b'.x = <uint:64,_62> "zz" m;',
                          on_verified=on_event, want_unit=want)
        assert seen == [('zz', 62)]

    @needs_lib
    def test_unusable_output_base_is_error(self):
        # 63 is not a base bvnr writes; substituting a default would hand back
        # digits in a base the caller never asked for.
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return (make_unit_dimensionless(), 63)

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem(b'.n = 255;', on_verified=lambda e, d: True,
                              want_unit=want)
        assert ei.value.code == ErrorCode.INVALID_ARGUMENT

    @needs_lib
    def test_unrepresentable_value_is_error(self):
        # A finite literal too extreme to build an exact rational from must not
        # be silently passed through in its ORIGINAL unit.
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return make_unit_si(BaseUnit.METER)

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem(b'.d = <float:64> 1e1000000 k~m;',
                              on_verified=lambda e, d: True, want_unit=want)
        assert ei.value.code == ErrorCode.VALUE_OUT_OF_RANGE

    @needs_lib
    def test_special_floats_pass_through(self):
        # nan/inf carry no finite value: nothing to convert, and no error.
        for lit in (b'nan', b'inf', b'ninf'):
            flags = []

            def want(d):
                return make_unit_si(BaseUnit.METER)

            def on_event(ev, d):
                if ev == Event.DATA and d is not None and d.type != 0:
                    flags.append(bool(d.converted))
                return True

            Reader().read_mem(b'.d = <float:64,k~m> ' + lit + b';',
                              on_verified=on_event, want_unit=want)
            assert flags and not any(flags)

    @needs_lib
    def test_nonterminating_refused_by_default(self):
        # 1 m -> mile is 125/201168: exact as a rational, no finite decimal.
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return (make_unit_si(BaseUnit.MILE), 10)

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem(b'.d = 1 m;', on_verified=lambda e, d: True,
                              want_unit=want)
        assert ei.value.code == ErrorCode.UNIT_INEXACT

    @needs_lib
    def test_nonterminating_opt_in_delivers_rational(self):
        # With want_unit_allow_nonterminating the value arrives converted, with
        # no text (the exact value lives in the C-side conv.num/conv.den).
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.MILE), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append((d.converted_str(), d.conv.base))
            return True

        Reader().read_mem(b'.d = 1 m;', on_verified=on_event, want_unit=want,
                          want_unit_allow_nonterminating=True)
        assert seen == [(None, 10)]

    @needs_lib
    def test_irrational_still_refused_with_opt_in(self):
        # The opt-in relaxes only the RENDERING limit; a π-based factor has no
        # exact rational to hand over at all.
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return make_unit_si(BaseUnit.RADIAN)

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem('.a = 90 °;'.encode('utf-8'),
                              on_verified=lambda e, d: True, want_unit=want,
                              want_unit_allow_nonterminating=True)
        assert ei.value.code == ErrorCode.UNIT_INEXACT

    @needs_lib
    def test_corrected_exact_factors(self):
        # Units whose true factor is a non-terminating rational used to ship a
        # 17-digit double repr as their "exact" value and silently produced wrong
        # answers flagged exact. 1 Torr = 101325/760 Pa, so 760 Torr is exactly
        # 101325 Pa — it used to render 101324.9999999999988.
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.PASCAL), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append(d.converted_str())
            return True

        Reader().read_mem('.p = 760 Torr;'.encode('utf-8'),
                          on_verified=on_event, want_unit=want)
        assert seen == ['101325']

    @needs_lib
    def test_nonterminating_factor_now_refuses(self):
        # 1 rpm = 1/60 Hz has no finite decimal, so it must refuse rather than
        # return the old rounded 0.016666666666666666 flagged exact.
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        def want(d):
            return (make_unit_si(BaseUnit.HERTZ), 10)

        with pytest.raises(BovnarParseError) as ei:
            Reader().read_mem(b'.f = 1 rpm;', on_verified=lambda e, d: True,
                              want_unit=want)
        assert ei.value.code == ErrorCode.UNIT_INEXACT

    @needs_lib
    def test_callback_data_survives_the_reader(self):
        # want_unit used to hand over `data_ptr.contents` — a view on reader
        # memory — so an object a callback kept crashed the interpreter once the
        # reader was gone. Both callbacks must snapshot.
        kept = []

        def want(d):
            kept.append(d)
            return make_unit_si(BaseUnit.METER)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None:
                kept.append(d)
            return True

        Reader().read_mem(b'.d = 5 k~m;', on_verified=on_event, want_unit=want)
        import gc
        gc.collect()
        # Touching the by-value fields after the reader is gone must not crash.
        for d in kept:
            assert isinstance(int(d.type), int)
            assert isinstance(int(d.value_type.width), int)

    @needs_lib
    def test_converted_rational_exposes_the_exact_value(self):
        # The non-terminating path carries the value ONLY as a rational, so it
        # has to be reachable from Python or the flag is a dead end.
        from fractions import Fraction
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.MILE), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append((d.converted_str(), d.converted_rational()))
            return True

        Reader().read_mem(b'.d = 1 m;', on_verified=on_event, want_unit=want,
                          want_unit_allow_nonterminating=True)
        text, rat = seen[0]
        assert text is None                       # no finite decimal expansion
        assert Fraction(*rat) == Fraction(1000, 1609344)   # 1 m in miles, exact

    @needs_lib
    def test_converted_in_base_rerenders(self):
        seen = []

        def want(d):
            return (make_unit_si(BaseUnit.METER), 10)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append((d.converted_str(), d.converted_in_base(2),
                             d.converted_in_base(16), d.converted_rational()))
            return True

        Reader().read_mem(b'.d = 5 k~m;', on_verified=on_event, want_unit=want)
        dec, b2, b16, rat = seen[0]
        assert dec == '5000'
        assert b2  == bin(5000)[2:]
        assert b16 == format(5000, 'x')
        assert rat == (5000, 1)

    @needs_lib
    def test_read_file_forwards_every_option(self, tmp_path):
        # read_file used to drop want_unit/strict_version, so a conversion read
        # from a path was impossible.
        p = tmp_path / "doc.bvnr"
        p.write_bytes(b'.d = 5 k~m;')
        seen = []

        def want(d):
            return make_unit_si(BaseUnit.METER)

        def on_event(ev, d):
            if ev == Event.DATA and d is not None and d.converted:
                seen.append(d.converted_str())
            return True

        Reader().read_file(str(p), on_verified=on_event, want_unit=want,
                           strict_version=False)
        assert seen == ['5000']


class TestWantUnitOverTheExampleCorpus:
    """Property test: an IDENTITY conversion must never change a document.

    Requesting each value's own unit is a no-op by construction, so running the
    example corpus with the hook installed must produce exactly the event stream
    it produces without one, and every converted value must equal the original.
    That exercises the hook against real documents — floats, wide integers,
    currencies, datetimes, arrays, references, octet streams — rather than the
    handful of literals a unit test can name.
    """

    import pathlib as _pl
    _EXAMPLES = sorted(
        (_pl.Path(__file__).resolve().parents[2] / "examples").glob("*.bvnr"))

    @staticmethod
    def _read(doc: bytes, base=None, allow=False):
        """Return (stream, converted_pairs). base None means no hook at all."""
        stream, conv = [], []

        def want(d):
            return (d.value_unit, base)

        def on_event(ev, d):
            if d is None:
                stream.append((ev, None, None))
                return True
            stream.append((ev, int(d.type), d.raw_bytes()))
            if d.converted:
                conv.append((d.raw_bytes(), _effective_base(d.value_type),
                             d.converted_str(), int(d.conv.base),
                             d.converted_rational()))
            return True

        kwargs = dict(on_verified=on_event)
        if base is not None:
            kwargs.update(want_unit=want, want_unit_allow_nonterminating=allow)
        Reader().read_mem(doc, **kwargs)
        return stream, conv

    @needs_lib
    @pytest.mark.parametrize("base", [0, 10, 16, 2, 36, 62])
    def test_identity_hook_does_not_perturb_the_corpus(self, base):
        from fractions import Fraction
        from bovnar.exceptions import BovnarParseError
        from bovnar.enums import ErrorCode

        checked = 0
        for path in self._EXAMPLES:
            doc = path.read_bytes()
            plain, _ = self._read(doc)
            try:
                hooked, conv = self._read(doc, base=base)
            except BovnarParseError as exc:
                # A decimal fraction has no finite expansion in a base without a
                # factor of 5 (3.14 in base 16), so a strict run legitimately
                # stops. The opt-in must then carry the same stream through.
                assert exc.code == ErrorCode.UNIT_INEXACT, (path.name, exc.code)
                hooked, conv = self._read(doc, base=base, allow=True)

            assert hooked == plain, f"{path.name}: identity hook changed the stream"

            for raw, src_base, text, out_base, rational in conv:
                assert rational is not None, f"{path.name}: no rational for {raw!r}"
                # The exact rational must equal the literal it came from.
                lit = raw.decode('ascii', 'replace')
                if lit.lower() in ('nan', 'inf', 'ninf'):
                    continue
                assert Fraction(*rational) == _literal_fraction(lit, src_base), (
                    f"{path.name}: identity changed {lit!r} -> {rational}")
                checked += 1
        assert checked > 0, "the corpus produced no conversions to check"


def _effective_base(vt) -> int:
    """Mirror of C bvn_effective_base: for float_fix the .base field holds Q and
    for float_dec/datetime it is not a numeral base at all, so those are always
    decimal. Reading .base blindly would decode the digits in a bogus base."""
    from bovnar.enums import ValueTypeFamily as F
    if int(vt.family) in (F.FLOAT_FIX, F.FLOAT_DEC, F.DATETIME):
        return 10
    return int(vt.base) or 10


def _literal_fraction(lit: str, base: int):
    """Exact value of a bovnar numeric literal, as a Fraction."""
    from fractions import Fraction
    t = lit.strip()
    neg = t.startswith('-')
    if t[:1] in '+-':
        t = t[1:]
    exp = 0
    markers = ('p', 'P') if base == 16 else ('e', 'E') if base <= 14 else ()
    for m in markers:
        if m in t:
            t, e = t.split(m, 1)
            exp = int(e)
            break
    ip, _, fp = t.partition('.')
    # Digit alphabets, mirroring bvn_char_to_digit / bigint_digit_to_char:
    # up to base 36 it is 0-9 a-z and either case is accepted; 37..62 adds A-Z as
    # 36..61 so case distinguishes digits; 64 is the Base64 alphabet and 85 is
    # Ascii85 starting at '!'.
    if base == 64:
        digits = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                  "abcdefghijklmnopqrstuvwxyz0123456789+/")
    elif base == 85:
        digits = "".join(chr(33 + i) for i in range(85))
    elif base <= 36:
        digits = "0123456789abcdefghijklmnopqrstuvwxyz"
    else:
        digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

    def val(s):
        if base <= 36:
            s = s.lower()
        return sum(digits.index(c) * base ** i for i, c in enumerate(reversed(s)))

    v = Fraction(val(ip or '0'))
    if fp:
        v += Fraction(val(fp), base ** len(fp))
    v *= (Fraction(2) if base == 16 and exp else Fraction(base)) ** exp
    return -v if neg else v
