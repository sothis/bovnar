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
