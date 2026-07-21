# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Every unit the pint bridge maps must carry bovnar's magnitude, not pint's.

Two ways that failed silently:

* pint defines fathom/rod/chain/furlong/league/acre from its SURVEY foot
  (1200/3937 m) while bovnar uses the international foot -- a 2 ppm error
  (4 ppm for acre, an area). pint's foot, mile, yard and inch are already
  international, which is why the discrepancy looked implausible and went
  unnoticed. Those six now have their own bvnr_* definitions.

* An affine base inside a PRODUCT: bovnar allows it at exponent 1 and applies
  the offset, so bvn_unit_convert_value(20, °C/h, K/h) is 983360. pint forbids
  offset units in compounds and silently rewrote degC to delta_degree_Celsius,
  giving 20 -- a different number with no diagnostic. The bridge refuses rather
  than answering differently from the reference.
"""

import pytest

from conftest import needs_lib

import bovnar
from bovnar.enums import BaseUnit
from bovnar.exceptions import BovnarArgumentError
from bovnar.structs import make_unit_compound

pint = pytest.importorskip("pint")

from bovnar._pint_units import BASE_UNIT_TO_PINT, build_registry
from bovnar._pint_bridge import to_pint
from bovnar.units import unit_to_si_factor


# Units whose pint definition differs from bovnar's by a DEFINITION revision
# rather than a wrong base: CODATA updates (dalton), which BTU is meant
# (IT vs thermochemical), and rounding in derived constants. Each is pinned to
# just above its measured gap so it cannot silently widen, and so that a NEW
# divergence -- like the 2 ppm survey-foot class this test was written for --
# fails immediately.
_KNOWN_DEFINITION_GAPS = {
    'british_thermal_unit': 2e-7,
    'dalton':               2e-9,
    'inch_Hg':              2e-9,
    'foot_pound':           1e-10,
    'slug':                 1e-10,
    'parsec':               1e-11,
}

# Not comparable this way at all: bit/byte are counts (bovnar's factor relates
# byte to bit, pint's relates it to nothing dimensional) and decibel/neper are
# logarithmic, where a multiplicative "factor" is not the same object.
_NOT_A_MULTIPLICATIVE_FACTOR = {'byte', 'bit', 'decibel', 'neper'}


@needs_lib
def test_every_mapped_unit_matches_the_c_si_factor():
    """Sweep the whole mapping table, not just the units that were reported."""
    ureg = build_registry()
    bad = []
    checked = 0
    for code, token in BASE_UNIT_TO_PINT.items():
        try:
            vu = make_unit_compound([{'base': BaseUnit(code)}])
        except Exception:
            continue
        try:
            si = unit_to_si_factor(vu)
        except Exception:
            continue
        if si.is_affine or not si.factor:
            continue          # affine units are compared separately below
        try:
            got = ureg.Quantity(1.0, token).to_base_units().magnitude
        except Exception as exc:
            bad.append((token, "pint: %s" % exc))
            continue
        if token in _NOT_A_MULTIPLICATIVE_FACTOR:
            continue
        checked += 1
        rel = abs(got - si.factor) / abs(si.factor)
        if rel > _KNOWN_DEFINITION_GAPS.get(token, 1e-12):
            bad.append((token, "C=%r pint=%r rel=%.2e" % (si.factor, got, rel)))
    assert checked > 100, "swept only %d units" % checked
    assert not bad, "%d unit(s) disagree: %s" % (len(bad), bad[:6])


@needs_lib
def test_the_six_survey_foot_units_are_exact():
    """The units this test was written for, pinned by name and value."""
    ureg = build_registry()
    for sym, want in (('fath', 1.8288), ('rd', 5.0292), ('ch', 20.1168),
                      ('fur', 201.168), ('lea', 4828.032), ('ac', 4046.8564224)):
        vu = bovnar.parse_unit(sym)
        token = BASE_UNIT_TO_PINT[int(vu.components[0].base)]
        got = ureg.Quantity(1.0, token).to_base_units().magnitude
        assert abs(got - want) / want < 1e-15, \
            "%s: pint %r, want %r (survey vs international foot)" % (sym, got, want)
        assert abs(unit_to_si_factor(vu).factor - want) / want < 1e-15, sym


@needs_lib
def test_affine_unit_alone_still_converts():
    q = to_pint(20.0, bovnar.parse_unit("°C"))
    assert abs(q.to("kelvin").magnitude - 293.15) < 1e-9


@needs_lib
@pytest.mark.parametrize("unit", ["°C/h", "°F*m", "°C/s"])
def test_affine_unit_in_a_compound_is_refused(unit):
    with pytest.raises(BovnarArgumentError):
        to_pint(20.0, bovnar.parse_unit(unit))
