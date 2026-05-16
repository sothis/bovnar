import math
import pytest

from conftest import needs_lib
from bovnar.enums import BaseUnit, SIPrefix, Exponent
from bovnar.structs import make_unit_si, make_unit_none, make_unit_dimensionless
from bovnar.exceptions import BovnarArgumentError
from bovnar.units import (
    unit_to_si_factor, units_compatible, unit_convert_factor,
    unit_dimension_vector, unit_reduce, convert_value,
    SIConversion, UnitConversion, ReducedUnit, SI_DIM_NAMES,
)


def _meter():
    return make_unit_si(BaseUnit.METER)

def _km():
    return make_unit_si(BaseUnit.METER, SIPrefix.KILO)

def _ms():
    return make_unit_si(BaseUnit.SECOND, SIPrefix.MILLI)

def _kelvin():
    return make_unit_si(BaseUnit.KELVIN)

def _celsius():
    return make_unit_si(BaseUnit.CELSIUS)

def _gram():
    return make_unit_si(BaseUnit.GRAM)

def _kg():
    return make_unit_si(BaseUnit.GRAM, SIPrefix.KILO)

def _m_per_s():
    from bovnar.structs import ValueUnit, ValueUnitComponent, MAX_UNIT_COMPONENTS
    from bovnar.enums import PrefixSystem
    import bovnar
    return bovnar.parse_unit("m/s")

def _force():
    import bovnar
    return bovnar.parse_unit("k~g\u00b7m/s\u00b2")


class TestSIDimNames:
    def test_has_seven_entries(self):
        assert len(SI_DIM_NAMES) == 7

    def test_contains_metre(self):
        assert 'm' in SI_DIM_NAMES

    def test_contains_kilogram(self):
        assert 'kg' in SI_DIM_NAMES

    def test_order_si(self):
        assert SI_DIM_NAMES[0] == 'm'
        assert SI_DIM_NAMES[1] == 'kg'
        assert SI_DIM_NAMES[2] == 's'


@needs_lib
class TestUnitToSiFactor:
    def test_meter_factor_one(self):
        conv = unit_to_si_factor(_meter())
        assert isinstance(conv, SIConversion)
        assert math.isclose(conv.factor, 1.0)
        assert not conv.is_affine

    def test_kilometer_factor_1000(self):
        conv = unit_to_si_factor(_km())
        assert math.isclose(conv.factor, 1000.0)
        assert not conv.is_affine

    def test_millisecond_factor(self):
        conv = unit_to_si_factor(_ms())
        assert math.isclose(conv.factor, 1e-3, rel_tol=1e-9)

    def test_kelvin_not_affine(self):
        conv = unit_to_si_factor(_kelvin())
        assert not conv.is_affine
        assert math.isclose(conv.factor, 1.0)

    def test_celsius_is_affine(self):
        conv = unit_to_si_factor(_celsius())
        assert conv.is_affine
        assert math.isclose(conv.affine_offset, 273.15, rel_tol=1e-6)

    def test_gram_factor(self):
        conv = unit_to_si_factor(_gram())
        assert math.isclose(conv.factor, 1e-3, rel_tol=1e-9)

    def test_kilogram_factor_one(self):
        conv = unit_to_si_factor(_kg())
        assert math.isclose(conv.factor, 1.0, rel_tol=1e-9)

    def test_return_type_is_frozen(self):
        conv = unit_to_si_factor(_meter())
        with pytest.raises((AttributeError, TypeError)):
            conv.factor = 999.0

    def test_dimensionless_factor(self):
        conv = unit_to_si_factor(make_unit_dimensionless())
        assert math.isclose(conv.factor, 1.0)


@needs_lib
class TestUnitsCompatible:
    def test_meter_and_km_compatible(self):
        assert units_compatible(_meter(), _km())

    def test_meter_and_gram_incompatible(self):
        assert not units_compatible(_meter(), _gram())

    def test_meter_and_meter_compatible(self):
        assert units_compatible(_meter(), _meter())

    def test_kelvin_and_celsius_compatible(self):
        assert units_compatible(_kelvin(), _celsius())

    def test_kg_and_gram_compatible(self):
        assert units_compatible(_kg(), _gram())

    def test_m_per_s_and_km_per_h_compatible(self):
        import bovnar
        mps   = bovnar.parse_unit("m/s")
        kmph  = bovnar.parse_unit("k~m/h")
        assert units_compatible(mps, kmph)

    def test_force_and_meter_incompatible(self):
        assert not units_compatible(_force(), _meter())


@needs_lib
class TestUnitConvertFactor:
    def test_km_to_m(self):
        conv = unit_convert_factor(_km(), _meter())
        assert isinstance(conv, UnitConversion)
        assert math.isclose(conv.factor, 1000.0)

    def test_m_to_km(self):
        conv = unit_convert_factor(_meter(), _km())
        assert math.isclose(conv.factor, 1e-3, rel_tol=1e-9)

    def test_same_unit_factor_one(self):
        conv = unit_convert_factor(_meter(), _meter())
        assert math.isclose(conv.factor, 1.0)

    def test_gram_to_kg(self):
        conv = unit_convert_factor(_gram(), _kg())
        assert math.isclose(conv.factor, 1e-3, rel_tol=1e-9)

    def test_kg_to_gram(self):
        conv = unit_convert_factor(_kg(), _gram())
        assert math.isclose(conv.factor, 1000.0)

    def test_celsius_to_kelvin_requires_affine(self):
        conv = unit_convert_factor(_celsius(), _kelvin())
        assert conv.requires_affine

    def test_incompatible_raises(self):
        with pytest.raises(BovnarArgumentError):
            unit_convert_factor(_meter(), _kg())

    def test_return_type_is_frozen(self):
        conv = unit_convert_factor(_meter(), _km())
        with pytest.raises((AttributeError, TypeError)):
            conv.factor = 0.0


@needs_lib
class TestUnitDimensionVector:
    def test_meter_is_length(self):
        v = unit_dimension_vector(_meter())
        assert isinstance(v, list)
        assert len(v) == 7
        assert v[0] == 1
        assert all(x == 0 for x in v[1:])

    def test_gram_is_mass(self):
        v = unit_dimension_vector(_gram())
        assert v[1] == 1
        assert v[0] == 0

    def test_second_is_time(self):
        import bovnar
        s = bovnar.parse_unit("s")
        v = unit_dimension_vector(s)
        assert v[2] == 1
        assert v[0] == 0
        assert v[1] == 0

    def test_m_per_s_velocity(self):
        v = unit_dimension_vector(_m_per_s())
        assert v[0] == 1
        assert v[2] == -1

    def test_force_kg_m_s2(self):
        v = unit_dimension_vector(_force())
        assert v[0] == 1
        assert v[1] == 1
        assert v[2] == -2

    def test_dimensionless_all_zero(self):
        v = unit_dimension_vector(make_unit_dimensionless())
        assert all(x == 0 for x in v)

    def test_km_same_dims_as_m(self):
        vm = unit_dimension_vector(_meter())
        vk = unit_dimension_vector(_km())
        assert vm == vk

    def test_incompatible_dim_vectors_differ(self):
        vm = unit_dimension_vector(_meter())
        vg = unit_dimension_vector(_gram())
        assert vm != vg


@needs_lib
class TestUnitReduce:
    def test_meter_reduces_to_itself(self):
        r = unit_reduce(_meter())
        assert isinstance(r, ReducedUnit)
        assert math.isclose(r.scale, 1.0)

    def test_km_scale_1000(self):
        r = unit_reduce(_km())
        assert math.isclose(r.scale, 1000.0, rel_tol=1e-9)

    def test_gram_scale(self):
        # bvn_unit_reduce strips explicit SI prefixes only.
        # make_unit_si(BaseUnit.GRAM) has no prefix component,
        # so the scale is 1.0 (gram reduces to gram).
        r = unit_reduce(_gram())
        assert math.isclose(r.scale, 1.0)

    def test_reduced_is_frozen(self):
        r = unit_reduce(_meter())
        with pytest.raises((AttributeError, TypeError)):
            r.scale = 999.0

    def test_result_has_unit_and_scale(self):
        r = unit_reduce(_km())
        from bovnar.structs import ValueUnit
        assert isinstance(r.unit, ValueUnit)
        assert isinstance(r.scale, float)


@needs_lib
class TestConvertValue:
    def test_km_to_m(self):
        v = convert_value(5.0, _km(), _meter())
        assert math.isclose(v, 5000.0)

    def test_m_to_km(self):
        v = convert_value(3000.0, _meter(), _km())
        assert math.isclose(v, 3.0)

    def test_identity(self):
        v = convert_value(42.0, _meter(), _meter())
        assert math.isclose(v, 42.0)

    def test_celsius_to_kelvin(self):
        v = convert_value(0.0, _celsius(), _kelvin())
        assert math.isclose(v, 273.15, rel_tol=1e-6)

    def test_celsius_100_to_kelvin(self):
        v = convert_value(100.0, _celsius(), _kelvin())
        assert math.isclose(v, 373.15, rel_tol=1e-6)

    def test_kelvin_to_celsius(self):
        v = convert_value(273.15, _kelvin(), _celsius())
        assert math.isclose(v, 0.0, abs_tol=1e-9)

    def test_incompatible_raises(self):
        with pytest.raises(BovnarArgumentError):
            convert_value(1.0, _meter(), _kg())

    def test_gram_to_kg(self):
        v = convert_value(1000.0, _gram(), _kg())
        assert math.isclose(v, 1.0)
