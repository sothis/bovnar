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
"""bovnar <-> pint bridge tests.

The headline is TestUnitTableIntegrity: it re-derives the expected dimension and
magnitude of every physical bovnar unit from bovnar itself and asserts the pint
bridge reproduces both.  That locks the hand-curated table in _pint_units against
silent drift when pint's own definitions change between versions — the failure
mode the table exists to prevent (bovnar "b"=bit vs pint barn, "R"=roentgen vs
pint gas constant, byte=1 vs 8*bit, ...).
"""
import math

import pytest

pint = pytest.importorskip("pint")
from conftest import needs_lib

import bovnar
from bovnar import (
    BaseUnit, SIPrefix, IECPrefix, Exponent,
    unit_to_str, unit_dimension_vector, unit_to_si_factor,
    to_pint_unit, from_pint_unit,
)
from bovnar.structs import make_unit_si, make_unit_iec, make_unit_compound
from bovnar._pint_units import (
    SEMANTIC_CAVEATS, CURRENCY_TOKENS, KIND_DIMENSIONS, BASE_UNIT_TO_PINT,
    build_registry, is_kind_scale,
)
from bovnar.units import units_compatible
from bovnar.exceptions import BovnarArgumentError

# pint dimension name -> index in bovnar's 7-vector [m, kg, s, A, K, mol, cd]
_PINT_DIM = {
    '[length]': 0, '[mass]': 1, '[time]': 2, '[current]': 3,
    '[temperature]': 4, '[substance]': 5, '[luminosity]': 6,
}
_AFFINE = {'CELSIUS', 'FAHRENHEIT', 'REAUMUR', 'DELISLE', 'NEWTON_TEMP', 'ROMER'}
_CAVEATS = set(SEMANTIC_CAVEATS)            # DECIBEL, NEPER, PH_SCALE, VAL

# Units bovnar keeps in a quantity kind: in pint they carry a synthetic
# dimension instead of an SI one, on purpose, so that pint refuses the same
# conversions bovnar does. Derived from the bridge table rather than restated,
# so a new kind cannot be added without this set noticing.
_KIND_UNITS = {bu.name for bu in BaseUnit
               if not bu.name.startswith('_')
               and BASE_UNIT_TO_PINT.get(int(bu)) in KIND_DIMENSIONS}

def _shares_the_angle_dimension():
    """Units carrying [angle] in pint, because bovnar puts them in the angle
    kind: the angle family itself, and the photometric units built on the
    steradian (lm = cd·sr, lx = lm/m², ph = lm/cm²). Asked of the registry
    rather than listed by hand — the hand-written list did not grow when the
    photometric units joined the kind, and the SI-dimension check below then
    failed for a unit that was behaving correctly."""
    reg = build_registry()
    out = set()
    for bu in BaseUnit:
        if bu.name.startswith('_') or int(bu) == 0:
            continue
        if int(bu) in CURRENCY_TOKENS:
            continue
        token = BASE_UNIT_TO_PINT.get(int(bu))
        if token is None:
            continue
        if any(str(d) == '[angle]' for d in reg.Unit(token).dimensionality):
            out.add(bu.name)
    return out


_ANGLE_UNITS = _shares_the_angle_dimension()
_ISOLATED = _KIND_UNITS | _ANGLE_UNITS


def _pint_dimvec(unit):
    vec = [0] * 7
    known = True
    for name, exp in unit.dimensionality.items():
        if name in _PINT_DIM:
            vec[_PINT_DIM[name]] += exp
        else:
            known = False
    return vec, known


def _named(predicate):
    return [bu for bu in BaseUnit
            if not bu.name.startswith('_') and int(bu) != 0 and predicate(bu)]


_PHYSICAL = _named(lambda bu: int(bu) not in CURRENCY_TOKENS
                   and bu.name not in _AFFINE and bu.name not in _ISOLATED)
_ALL_BASE = _named(lambda bu: True)
_CURRENCY = _named(lambda bu: int(bu) in CURRENCY_TOKENS)


@pytest.fixture(scope="module")
def reg():
    return build_registry()


@needs_lib
class TestUnitTableIntegrity:
    """Every physical unit must keep bovnar's dimension AND magnitude through pint."""

    @pytest.mark.parametrize("bu", _PHYSICAL, ids=lambda b: b.name)
    def test_dimension_and_magnitude(self, bu, reg):
        vu = make_unit_si(bu, SIPrefix.NONE, Exponent.LINEAR)
        bov_dim = list(unit_dimension_vector(vu))
        bov_factor = unit_to_si_factor(vu).factor

        unit = to_pint_unit(vu, ureg=reg)
        pint_dim, known = _pint_dimvec(unit)
        assert known, f"{bu.name}: pint unit has a non-SI dimension {dict(unit.dimensionality)}"
        assert pint_dim == bov_dim, f"{bu.name}: dim bovnar={bov_dim} pint={pint_dim}"

        pint_factor = reg.Quantity(1.0, unit).to_base_units().magnitude
        # 1e-5 absorbs bovnar's ~6-sig-fig stored constants vs pint's exact
        # derivations (furlong/acre/...); a real mismatch is off by whole ratios.
        assert math.isclose(bov_factor, pint_factor, rel_tol=1e-5), \
            f"{bu.name}: factor bovnar={bov_factor!r} pint={pint_factor!r}"

    @pytest.mark.parametrize("name", sorted(_AFFINE))
    def test_affine_offset(self, name, reg):
        vu = make_unit_si(BaseUnit[name], SIPrefix.NONE, Exponent.LINEAR)
        conv = unit_to_si_factor(vu)
        unit = to_pint_unit(vu, ureg=reg)
        for value in (0.0, 100.0):
            k_bovnar = value * conv.factor + conv.affine_offset
            k_pint = reg.Quantity(value, unit).to('kelvin').magnitude
            assert math.isclose(k_bovnar, k_pint, rel_tol=1e-6, abs_tol=1e-9), \
                f"{name} at {value}: bovnar={k_bovnar} pint={k_pint}"

    @pytest.mark.parametrize("name", sorted(_ISOLATED), ids=str)
    def test_isolated_kinds_carry_their_own_dimension(self, name, reg):
        # A quantity kind is represented in pint as a dimension of its own, so
        # the SI dimension vector deliberately does NOT apply. What must still
        # hold is the magnitude, in that kind's own base unit.
        bu = BaseUnit[name]
        vu = make_unit_si(bu, SIPrefix.NONE, Exponent.LINEAR)
        unit = to_pint_unit(vu, ureg=reg)
        _vec, known_si = _pint_dimvec(unit)
        assert not known_si, f"{name}: expected a synthetic dimension, got SI"
        assert is_kind_scale(unit), f"{name}: not recognised as a kind scale"
        bov_factor = unit_to_si_factor(vu).factor
        pint_factor = reg.Quantity(1.0, unit).to_base_units().magnitude
        assert math.isclose(bov_factor, pint_factor, rel_tol=1e-9), \
            f"{name}: factor bovnar={bov_factor!r} pint={pint_factor!r}"

    def test_caveats_are_exactly_the_known_set(self):
        # What survives kind isolation: the label round-trips and the refusals
        # match, but pint will still do ARITHMETIC that the scale does not
        # support (adding decibels), or cannot know a convention bovnar states
        # in prose (val is the divalent equivalent). A new entry must be a
        # deliberate addition, never a silent one.
        assert _CAVEATS == {'DECIBEL', 'NEPER', 'PH_SCALE', 'VAL'}


@needs_lib
class TestPintAgreesWithBovnarOnWhatConverts:
    """The invariant the bridge exists to keep: if bovnar refuses a conversion,
    pint must refuse it too.

    Nothing checked this before, and pint was quietly the more permissive of
    the two — it would turn an NTU into an FNU, a byte into 8 bits and a pH
    into a percentage, which is the exact class of answer bovnar is built to
    withhold. The pairs below are chosen to cover every quantity kind plus
    ordinary dimensioned units as a control.
    """

    # every kind, both directions, plus conversions that MUST keep working
    _TOKENS = ['NTU', 'FNU', 'FTU', 'FAU', 'JTU', 'PSU', 'pH', 'dB', 'Np',
               'b', 'B', 'rad', '°', 'grad', 'rev', 'sr', '%', 'ppm',
               'no_unit', 'm', 'k~m', 's', 'Hz', 'rad/s', 'rpm', 'rev/min',
               '°dH', 'm~mol/L', 'CF', 'm~S/c~m', 'kg', 'K']

    @staticmethod
    def _pint_converts(a, b, reg):
        try:
            reg.Quantity(1.0, to_pint_unit(bovnar.parse_unit(a), ureg=reg)).to(
                to_pint_unit(bovnar.parse_unit(b), ureg=reg))
            return True
        except Exception:
            return False

    @pytest.mark.parametrize("a", _TOKENS)
    def test_convertibility_matches_bovnar(self, a, reg):
        disagree = []
        for b in self._TOKENS:
            bov = units_compatible(bovnar.parse_unit(a), bovnar.parse_unit(b))
            pnt = self._pint_converts(a, b, reg)
            if bov != pnt:
                disagree.append((a, b, 'bovnar' if bov else 'pint'))
        assert not disagree, \
            "only %s allows these: %s" % (disagree[0][2], [(x, y) for x, y, _ in disagree])

    def test_within_a_kind_conversion_still_works(self, reg):
        # Isolation must not cost the conversions that ARE sound.
        for a, b, expected in [('°', 'rad', math.pi / 180.0),
                               ('rev', 'rad', 2.0 * math.pi),
                               ('m~NTU', 'NTU', 0.001),
                               ('k~m', 'm', 1000.0),
                               ('CF', 'm~S/c~m', 0.1)]:
            q = reg.Quantity(1.0, to_pint_unit(bovnar.parse_unit(a), ureg=reg))
            got = q.to(to_pint_unit(bovnar.parse_unit(b), ureg=reg)).magnitude
            assert math.isclose(got, expected, rel_tol=1e-12), (a, b, got, expected)

    def test_steradian_is_radian_squared(self, reg):
        sr = to_pint_unit(bovnar.parse_unit('sr'), ureg=reg)
        rad = to_pint_unit(bovnar.parse_unit('rad'), ureg=reg)
        assert math.isclose(
            reg.Quantity(1.0, sr).to(rad ** 2).magnitude, 1.0, rel_tol=1e-12)

    def test_every_pair_of_base_units_agrees(self, reg):
        """The whole table, not a sample: for all 16k pairs of non-currency base
        units, pint converts exactly when bovnar says they are compatible.
        Runs in under a second, so there is no reason to check less."""
        import itertools
        names = [bu for bu in BaseUnit
                 if not bu.name.startswith('_') and int(bu) != 0
                 and int(bu) not in CURRENCY_TOKENS]
        vus = {bu: make_unit_si(bu, SIPrefix.NONE, Exponent.LINEAR) for bu in names}
        pus = {bu: to_pint_unit(vus[bu], ureg=reg) for bu in names}
        disagree = []
        for a, b in itertools.combinations(names, 2):
            bov = units_compatible(vus[a], vus[b])
            try:
                reg.Quantity(1.0, pus[a]).to(pus[b])
                pnt = True
            except Exception:
                pnt = False
            if bov != pnt:
                disagree.append((a.name, b.name, 'bovnar' if bov else 'pint'))
        assert not disagree, "%d pairs disagree, first few: %s" % (
            len(disagree), disagree[:10])

    def test_isolate_kinds_false_restores_pint_native_interop(self):
        # The escape hatch: aliased back onto pint's natives, a bovnar byte
        # talks to pint's megabyte again — and pint's permissiveness returns
        # with it, which is the trade the flag exists to make.
        reg = build_registry(isolate_kinds=False)
        B = to_pint_unit(bovnar.parse_unit('B'), ureg=reg)
        assert math.isclose(reg.Quantity(1e6, B).to('megabyte').magnitude, 1.0,
                            rel_tol=1e-12)
        assert self._pint_converts('B', 'b', reg)          # pint allows it
        assert not units_compatible(bovnar.parse_unit('B'),
                                    bovnar.parse_unit('b'))  # bovnar does not


@needs_lib
class TestPintRoundTrip:
    """bovnar unit -> pint -> bovnar must reproduce the canonical text."""

    @pytest.mark.parametrize("bu", _ALL_BASE, ids=lambda b: b.name)
    def test_base_unit_text_roundtrip(self, bu, reg):
        vu0 = make_unit_si(bu, SIPrefix.NONE, Exponent.LINEAR)
        vu1 = from_pint_unit(to_pint_unit(vu0, ureg=reg), ureg=reg)
        assert unit_to_str(vu1) == unit_to_str(vu0)

    def test_prefixed_compound_iec_roundtrip(self, reg):
        cases = [
            make_unit_si(BaseUnit.METER, SIPrefix.KILO),
            make_unit_si(BaseUnit.METER, SIPrefix.MILLI),
            make_unit_si(BaseUnit.VOLT,  SIPrefix.MICRO),
            make_unit_si(BaseUnit.HERTZ, SIPrefix.GIGA),
            make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI),
            make_unit_iec(BaseUnit.BIT,  IECPrefix.KIBI),
            make_unit_compound([{'base': BaseUnit.METER},
                                {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE}]),
            make_unit_compound([{'base': BaseUnit.GRAM, 'si_prefix': SIPrefix.KILO},
                                {'base': BaseUnit.METER},
                                {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE}]),
            make_unit_compound([{'base': BaseUnit.METER, 'si_prefix': SIPrefix.KILO},
                                {'base': BaseUnit.HOUR, 'exp': Exponent.NEG_LINEAR}]),
        ]
        for vu in cases:
            back = from_pint_unit(to_pint_unit(vu, ureg=reg), ureg=reg)
            assert unit_to_str(back) == unit_to_str(vu)

    def test_user_pint_strings_map_to_bovnar(self, reg):
        # arbitrary pint expressions a user might hand us
        assert unit_to_str(from_pint_unit('kilowatt*hour', ureg=reg)) == 'k~W·h'
        assert unit_to_str(from_pint_unit('newton*meter', ureg=reg)) == 'N·m'
        assert unit_to_str(from_pint_unit('micropascal', ureg=reg)) == 'µ~Pa'

    @pytest.mark.parametrize("expr", ['meter**0.5', 'meter**12'])
    def test_unrepresentable_rejected(self, expr, reg):
        with pytest.raises(BovnarArgumentError):
            from_pint_unit(expr, ureg=reg)

    @pytest.mark.parametrize("bad", [__import__('numpy').array([1.0, 2.0]),
                                     ['m'], 5, 3.5])
    def test_non_pint_input_clean_error(self, bad, reg):
        # a non-pint value must give a clear message, not a cryptic AttributeError
        # from deep inside (`'ndarray' object has no attribute '_units'`).
        with pytest.raises(BovnarArgumentError, match="expected a pint"):
            from_pint_unit(bad, ureg=reg)


@needs_lib
class TestCurrencyDimensions:
    """Currencies are non-convertible: each its own [currency_<CODE>] dimension."""

    def test_each_currency_is_isolated(self, reg):
        for tok in CURRENCY_TOKENS.values():
            assert list(reg.Unit(tok).dimensionality) == [f'[currency_{tok}]']

    @pytest.mark.parametrize("a,b", [('USD', 'EUR'), ('BTC', 'ETH'), ('JPY', 'USD')])
    def test_cross_currency_conversion_raises(self, a, b, reg):
        with pytest.raises(pint.DimensionalityError):
            reg.Quantity(1, a).to(b)

    def test_same_currency_arithmetic(self, reg):
        assert (reg.Quantity(3, 'USD') + reg.Quantity(2, 'USD')).magnitude == 5

    @pytest.mark.parametrize("bu", _CURRENCY[:8] + _CURRENCY[-8:],
                             ids=lambda b: b.name)
    def test_currency_text_roundtrip(self, bu, reg):
        vu = make_unit_si(bu, SIPrefix.NONE, Exponent.LINEAR)
        back = from_pint_unit(to_pint_unit(vu, ureg=reg), ureg=reg)
        assert unit_to_str(back) == unit_to_str(vu)


@needs_lib
class TestReverseCacheLifetime:
    """The per-registry reverse map is keyed on the registry object via a
    WeakKeyDictionary, so a transient registry's cache entry is evicted when it
    is collected — no unbounded leak and no id() reuse aliasing a stale map onto
    an unrelated new registry."""

    def test_transient_registry_cache_is_evicted(self):
        import gc
        from bovnar import _pint_bridge as pb

        reg = build_registry()
        from_pint_unit('newton*meter', ureg=reg)        # populate its cache
        assert reg in pb._reverse_cache
        before = len(pb._reverse_cache)

        del reg
        gc.collect()
        # the dropped registry's entry is gone; nothing else removed.
        assert len(pb._reverse_cache) == before - 1
