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
from bovnar._pint_units import SEMANTIC_CAVEATS, CURRENCY_TOKENS, build_registry
from bovnar.exceptions import BovnarArgumentError

# pint dimension name -> index in bovnar's 7-vector [m, kg, s, A, K, mol, cd]
_PINT_DIM = {
    '[length]': 0, '[mass]': 1, '[time]': 2, '[current]': 3,
    '[temperature]': 4, '[substance]': 5, '[luminosity]': 6,
}
_AFFINE = {'CELSIUS', 'FAHRENHEIT', 'REAUMUR', 'DELISLE', 'NEWTON_TEMP', 'ROMER'}
_CAVEATS = set(SEMANTIC_CAVEATS)            # BYTE, DECIBEL, NEPER


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
                   and bu.name not in _AFFINE and bu.name not in _CAVEATS)
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

    def test_caveats_are_exactly_the_known_set(self):
        # Every entry is a unit whose LABEL round-trips but whose VALUE
        # semantics differ from pint's; a new one must be a deliberate addition,
        # never a silent one.
        assert _CAVEATS == {'BYTE', 'DECIBEL', 'NEPER', 'PH_SCALE', 'VAL',
                            'TURBIDITY_NTU', 'TURBIDITY_FNU', 'PRACTICAL_SALINITY',
                            'TURBIDITY_FTU', 'TURBIDITY_FAU', 'TURBIDITY_JTU'}


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
