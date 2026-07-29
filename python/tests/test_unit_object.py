"""
The Unit object and the all-properties accessor shape (1.2).

Both are BREAKING changes and this file is where the new contract is written
down. Before 1.2, Quantity.unit handed back a raw ctypes ValueUnit whose only
useful operations lived elsewhere as module-level functions, and whose `==`
compared by ctypes identity -- so two units that meant the same thing were not
equal. And the value accessors were split between properties (value, epoch_name)
and methods (decimal(), fraction(), fixed_point()) with no rule anyone could
state, so callers guessed and got a TypeError either way.
"""
import pytest

import bovnar
from bovnar import Quantity, Unit
from bovnar.exceptions import BovnarArgumentError


def _q(src: bytes, key: str = "x") -> Quantity:
    return bovnar.loads(src, typed=True)[key]


class TestUnitObject:
    def test_unit_is_a_unit_object(self):
        q = _q(b".x = <float:64,k~m> 5.0;")
        assert isinstance(q.unit, Unit)
        assert str(q.unit) == "k~m"
        assert repr(q.unit) == "Unit('k~m')"

    def test_dimensionless_is_falsey_and_empty(self):
        q = _q(b".x = <float:64> 5.0;")
        assert q.unit.is_dimensionless
        assert not q.unit
        assert str(q.unit) == ""

    def test_percent_is_a_real_unit_not_dimensionless(self):
        """`%` has no dimension but IS a unit; only the absence of a unit is
        dimensionless here. Collapsing the two is how a ratio compares equal to
        a bare count."""
        q = _q(b".x = <float:64,%> 5.0;")
        assert not q.unit.is_dimensionless
        assert bool(q.unit)

    def test_equality_is_by_meaning_not_identity(self):
        """The defect the object exists to fix: ctypes structs compare by
        identity, so two ValueUnits meaning the same thing were never equal."""
        a = _q(b".x = <float:64,m/s> 1.0;").unit
        b = _q(b".x = <float:64,m/s> 2.0;").unit
        assert a == b
        assert hash(a) == hash(b)
        assert a != _q(b".x = <float:64,k~m/s> 1.0;").unit

    def test_equality_accepts_a_string(self):
        q = _q(b".x = <float:64,k~m> 5.0;")
        assert q.unit == "k~m"
        assert q.unit != "m"
        assert q.unit != "not a unit at all"      # unparseable, not an exception

    def test_cross_notation_equality(self):
        """A profile spelling and the native one are the same unit, which is the
        whole promise of doc/11 -- and was not expressible through `==` before."""
        native = Unit.parse("mmHg")
        assert native == Unit.parse("ucum:mm[Hg]")
        assert hash(native) == hash(Unit.parse("ucum:mm[Hg]"))

    def test_factor_and_compatibility(self):
        u = Unit.parse("k~m")
        assert u.factor == 1000.0
        assert u.compatible_with("m")
        assert not u.compatible_with("s")
        assert u.convert_factor("m") == 1000.0

    def test_to_profile(self):
        assert Unit.parse("k~g").to_profile("unece") == "KGM"
        assert Unit.parse("mmHg").to_profile("ucum") == "mm[Hg]"

    def test_profile_only(self):
        u = Unit.parse("ucum:[IU]")
        assert u.is_profile_only
        assert not Unit.parse("m").is_profile_only

    def test_raw_still_reaches_the_struct(self):
        """Nothing that took a ValueUnit is cut off: .raw is the same struct."""
        q = _q(b".x = <float:64,m/s> 1.0;")
        assert bovnar.unit_to_str(q.unit.raw) == "m/s"

    def test_constructor_rejects_nonsense(self):
        with pytest.raises(BovnarArgumentError):
            Unit(42)


class TestAccessorsAreProperties:
    """value, decimal, fraction, fixed_point, stored_value, ieee_bits and
    unit_str are all properties in 1.2. They were split between properties and
    methods with no stateable rule."""

    def test_numeric_accessors(self):
        from decimal import Decimal
        from fractions import Fraction
        q = _q(b".x = <float_dec:64> 3.25;")
        assert q.value == 3.25
        assert q.decimal == Decimal("3.25")
        assert q.fraction == Fraction(13, 4)
        assert q.unit_str == ""

    def test_fixed_point(self):
        q = _q(b".x = <float_fix:32,q8> 3.26953125;")
        mantissa, bits = q.fixed_point
        assert bits == 8
        assert mantissa / (1 << bits) == 3.26953125

    def test_calling_one_is_a_typeerror(self):
        """The point of unifying: there is now exactly one spelling, and the
        other one fails loudly rather than silently returning a bound method."""
        q = _q(b".x = <float:64> 1.0;")
        with pytest.raises(TypeError):
            q.decimal()


class TestQuantityValidation:
    def test_rejects_bad_vtype(self):
        with pytest.raises(BovnarArgumentError, match="ValueTypeSpec"):
            Quantity("1", "nope")

    def test_rejects_bad_raw(self):
        vt = bovnar.make_type_spec(bovnar.ValueTypeFamily.UINT, 64, 0)
        with pytest.raises(BovnarArgumentError, match="raw"):
            Quantity(123, vt)

    def test_rejects_bad_frac(self):
        vt = bovnar.make_type_spec(bovnar.ValueTypeFamily.UINT, 64, 0)
        with pytest.raises(BovnarArgumentError, match="frac"):
            Quantity("1", vt, frac=5)

    def test_accepts_a_unit_string_for_convenience(self):
        vt = bovnar.make_type_spec(bovnar.ValueTypeFamily.FLOAT, 64, 0)
        q = Quantity("1.0", vt, "m/s")
        assert isinstance(q.unit, Unit)
        assert q.unit == "m/s"

    def test_accepts_a_valueunit_and_a_unit(self):
        vt = bovnar.make_type_spec(bovnar.ValueTypeFamily.FLOAT, 64, 0)
        assert Quantity("1.0", vt, bovnar.parse_unit("m")).unit == "m"
        assert Quantity("1.0", vt, Unit.parse("m")).unit == "m"
        assert Quantity("1.0", vt, None).unit.is_dimensionless
