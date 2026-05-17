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


@needs_lib
class TestNewUnitSIFactors:
    """bvn_unit_to_si_factor values for all 10 new base units."""

    def _factor(self, sym):
        import bovnar
        return unit_to_si_factor(bovnar.parse_unit(sym)).factor

    def test_kgf_factor(self):
        assert math.isclose(self._factor("kgf"), 9.80665, rel_tol=1e-9)

    def test_inHg_factor(self):
        assert math.isclose(self._factor("inHg"), 3386.388645, rel_tol=1e-7)

    def test_rpm_factor(self):
        assert math.isclose(self._factor("rpm"), 1.0 / 60.0, rel_tol=1e-12)

    def test_ft_lb_factor(self):
        assert math.isclose(self._factor("ft_lb"), 1.3558179483, rel_tol=1e-9)

    def test_dr_factor(self):
        assert math.isclose(self._factor("dr"), 1.7718451953125e-3, rel_tol=1e-12)

    def test_dwt_factor(self):
        assert math.isclose(self._factor("dwt"), 1.55517384e-3, rel_tol=1e-9)

    def test_chain_factor(self):
        assert math.isclose(self._factor("ch"), 20.1168, rel_tol=1e-9)

    def test_rod_factor(self):
        assert math.isclose(self._factor("rd"), 5.0292, rel_tol=1e-9)

    def test_gill_factor(self):
        assert math.isclose(self._factor("gi"), 1.18294118750e-4, rel_tol=1e-9)

    def test_gill_uk_factor(self):
        assert math.isclose(self._factor("gi_uk"), 1.420653125e-4, rel_tol=1e-9)

    def test_kgf_not_affine(self):
        import bovnar
        conv = unit_to_si_factor(bovnar.parse_unit("kgf"))
        assert not conv.is_affine

    def test_rpm_not_affine(self):
        import bovnar
        conv = unit_to_si_factor(bovnar.parse_unit("rpm"))
        assert not conv.is_affine


@needs_lib
class TestNewUnitDimensionVectors:
    """SI dimension vectors [m, kg, s, A, K, mol, cd] for new units."""

    def _dims(self, sym):
        import bovnar
        return unit_dimension_vector(bovnar.parse_unit(sym))

    def test_kgf_dims(self):
        assert self._dims("kgf") == [1, 1, -2, 0, 0, 0, 0]

    def test_inHg_dims(self):
        assert self._dims("inHg") == [-1, 1, -2, 0, 0, 0, 0]

    def test_rpm_dims(self):
        assert self._dims("rpm") == [0, 0, -1, 0, 0, 0, 0]

    def test_ft_lb_dims(self):
        assert self._dims("ft_lb") == [2, 1, -2, 0, 0, 0, 0]

    def test_dr_dims(self):
        assert self._dims("dr") == [0, 1, 0, 0, 0, 0, 0]

    def test_dwt_dims(self):
        assert self._dims("dwt") == [0, 1, 0, 0, 0, 0, 0]

    def test_chain_dims(self):
        assert self._dims("ch") == [1, 0, 0, 0, 0, 0, 0]

    def test_rod_dims(self):
        assert self._dims("rd") == [1, 0, 0, 0, 0, 0, 0]

    def test_gill_dims(self):
        assert self._dims("gi") == [3, 0, 0, 0, 0, 0, 0]

    def test_gill_uk_dims(self):
        assert self._dims("gi_uk") == [3, 0, 0, 0, 0, 0, 0]


@needs_lib
class TestNewUnitConversions:
    """Numerical accuracy of convert_value for the 10 new units."""

    def _p(self, s):
        import bovnar
        return bovnar.parse_unit(s)

    def test_kgf_to_newton(self):
        v = convert_value(1.0, self._p("kgf"), self._p("N"))
        assert math.isclose(v, 9.80665, rel_tol=1e-9)

    def test_newton_to_kgf(self):
        v = convert_value(9.80665, self._p("N"), self._p("kgf"))
        assert math.isclose(v, 1.0, rel_tol=1e-9)

    def test_kgf_to_lbf(self):
        v = convert_value(1.0, self._p("kgf"), self._p("lbf"))
        assert math.isclose(v, 2.20462262185, rel_tol=1e-6)

    def test_inHg_to_pa(self):
        v = convert_value(1.0, self._p("inHg"), self._p("Pa"))
        assert math.isclose(v, 3386.388645, rel_tol=1e-6)

    def test_standard_atm_in_inHg(self):
        v = convert_value(101325.0, self._p("Pa"), self._p("inHg"))
        assert math.isclose(v, 29.9212, rel_tol=1e-4)

    def test_inHg_to_mmHg(self):
        v = convert_value(1.0, self._p("inHg"), self._p("mmHg"))
        assert math.isclose(v, 25.4, rel_tol=1e-6)

    def test_rpm_to_hz(self):
        v = convert_value(60.0, self._p("rpm"), self._p("Hz"))
        assert math.isclose(v, 1.0, rel_tol=1e-12)

    def test_rpm_3600_to_60hz(self):
        v = convert_value(3600.0, self._p("rpm"), self._p("Hz"))
        assert math.isclose(v, 60.0, rel_tol=1e-12)

    def test_hz_to_rpm(self):
        v = convert_value(50.0, self._p("Hz"), self._p("rpm"))
        assert math.isclose(v, 3000.0, rel_tol=1e-12)

    def test_ft_lb_to_joule(self):
        v = convert_value(1.0, self._p("ft_lb"), self._p("J"))
        assert math.isclose(v, 1.3558179483, rel_tol=1e-9)

    def test_joule_to_ft_lb(self):
        v = convert_value(1.3558179483, self._p("J"), self._p("ft_lb"))
        assert math.isclose(v, 1.0, rel_tol=1e-9)

    def test_ft_lb_to_cal(self):
        v = convert_value(1.0, self._p("ft_lb"), self._p("cal"))
        assert math.isclose(v, 1.3558179483 / 4.184, rel_tol=1e-9)

    def test_16_dr_equals_1_oz(self):
        v = convert_value(16.0, self._p("dr"), self._p("oz"))
        assert math.isclose(v, 1.0, rel_tol=1e-10)

    def test_dr_to_gram(self):
        v = convert_value(1.0, self._p("dr"), self._p("g"))
        assert math.isclose(v, 1.7718451953125, rel_tol=1e-10)

    def test_20_dwt_equals_1_oz_t(self):
        v = convert_value(20.0, self._p("dwt"), self._p("oz_t"))
        assert math.isclose(v, 1.0, rel_tol=1e-10)

    def test_dwt_to_gram(self):
        v = convert_value(1.0, self._p("dwt"), self._p("g"))
        assert math.isclose(v, 1.55517384, rel_tol=1e-9)

    def test_80_chains_equals_1_mile(self):
        v = convert_value(80.0, self._p("ch"), self._p("mi"))
        assert math.isclose(v, 1.0, rel_tol=1e-9)

    def test_chain_to_feet(self):
        v = convert_value(1.0, self._p("ch"), self._p("ft"))
        assert math.isclose(v, 66.0, rel_tol=1e-9)

    def test_4_rods_equals_1_chain(self):
        v = convert_value(4.0, self._p("rd"), self._p("ch"))
        assert math.isclose(v, 1.0, rel_tol=1e-9)

    def test_rod_to_feet(self):
        v = convert_value(1.0, self._p("rd"), self._p("ft"))
        assert math.isclose(v, 16.5, rel_tol=1e-9)

    def test_4_gill_equals_1_pint(self):
        v = convert_value(4.0, self._p("gi"), self._p("pt"))
        assert math.isclose(v, 1.0, rel_tol=1e-7)

    def test_gill_to_fl_oz(self):
        v = convert_value(1.0, self._p("gi"), self._p("fl_oz"))
        assert math.isclose(v, 4.0, rel_tol=1e-9)

    def test_4_gill_uk_equals_1_pt_uk(self):
        v = convert_value(4.0, self._p("gi_uk"), self._p("pt_uk"))
        assert math.isclose(v, 1.0, rel_tol=1e-9)

    def test_32_gill_uk_equals_1_gal_uk(self):
        v = convert_value(32.0, self._p("gi_uk"), self._p("gal_uk"))
        assert math.isclose(v, 1.0, rel_tol=1e-9)

    def test_gill_to_ml(self):
        v = convert_value(1.0, self._p("gi"), self._p("m~L"))
        assert math.isclose(v, 118.294118750, rel_tol=1e-7)

    def test_gill_uk_to_ml(self):
        v = convert_value(1.0, self._p("gi_uk"), self._p("m~L"))
        assert math.isclose(v, 142.0653125, rel_tol=1e-7)
