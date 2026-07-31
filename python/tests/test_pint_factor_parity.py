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
from bovnar.enums import BaseUnit, UNIT_NATIVE_LAST
from bovnar.exceptions import BovnarArgumentError
from bovnar.structs import make_unit_compound

pint = pytest.importorskip("pint")

from bovnar._pint_units import BASE_UNIT_TO_PINT, build_registry
from bovnar._pint_bridge import to_pint
from bovnar.units import unit_to_si_factor

# The set test_pint_bridge parametrises test_affine_offset over. Imported rather
# than repeated so the closure test below compares the library against the set
# that is actually driving the other file's coverage.
from test_pint_bridge import _AFFINE as _BRIDGE_AFFINE


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

# There used to be a _NOT_A_MULTIPLICATIVE_FACTOR = {'byte', 'bit', 'decibel',
# 'neper'} here, excluding four units whose "factor" is not a plain ratio. It
# never excluded anything: the map spells them bvnr_byte, bvnr_bit, bvnr_decibel
# and bvnr_neper -- their own definitions, stating bovnar's own magnitudes -- so
# the bare pint names never matched a token and all four were being compared and
# agreeing the whole time. Deleting the set rather than correcting the spellings,
# because an exclusion that passes is an assertion given up for nothing.


@needs_lib
def test_every_mapped_unit_matches_the_c_si_factor():
    """Sweep the whole mapping table, not just the units that were reported."""
    ureg = build_registry()
    bad = []
    checked = 0
    skipped_affine = []
    for code, token in BASE_UNIT_TO_PINT.items():
        vu = make_unit_compound([{'base': BaseUnit(code)}])
        si = unit_to_si_factor(vu)
        if si.is_affine or not si.factor:
            skipped_affine.append(token)   # compared by test_affine_offset
            continue
        try:
            got = ureg.Quantity(1.0, token).to_base_units().magnitude
        except Exception as exc:
            bad.append((token, "pint: %s" % exc))
            continue
        checked += 1
        rel = abs(got - si.factor) / abs(si.factor)
        if rel > _KNOWN_DEFINITION_GAPS.get(token, 1e-12):
            bad.append((token, "C=%r pint=%r rel=%.2e" % (si.factor, got, rel)))

    # The sweep is CLOSED, not merely large. It used to assert `checked > 100`
    # against a real total of 209, which left room for fifty units to fall out
    # of the comparison unnoticed -- and the three `except: continue` arms it
    # used to have were exactly the way that happens. Every row now either gets
    # compared or is named as affine, and the two must account for all of them.
    assert checked + len(skipped_affine) == len(BASE_UNIT_TO_PINT), (
        "%d compared + %d affine != %d mapped units — something left the sweep"
        % (checked, len(skipped_affine), len(BASE_UNIT_TO_PINT)))
    assert not bad, "%d unit(s) disagree: %s" % (len(bad), bad[:6])


@needs_lib
def test_the_mapping_is_closed_against_the_enum():
    """Every native unit has a pint row, and no row names a unit that is gone.

    The direction the sweep above cannot check: it iterates the MAP, so a native
    unit added to src/gendata and wired into BaseUnit but never given a pint
    token is simply absent from it, and every assertion still passes. The bridge
    then ships incomplete and the hole surfaces at a user's runtime as
    `no pint mapping for bovnar base unit code NNNNNN`.

    This is the next link in the chain test_enums.py starts when it closes
    BaseUnit against src/gendata: gendata -> BaseUnit -> BASE_UNIT_TO_PINT.
    """
    native = [b for b in BaseUnit if 0 < int(b) <= int(UNIT_NATIVE_LAST)]
    mapped = set(BASE_UNIT_TO_PINT)
    missing = sorted(b.name for b in native if int(b) not in mapped)
    extra = sorted(mapped - {int(b) for b in native})
    assert not missing, (
        "%d native unit(s) have no pint mapping: %s — add a row to "
        "BASE_UNIT_TO_PINT (and a definition to BOVNAR_PINT_DEFINITIONS if "
        "pint has no unit of that magnitude)" % (len(missing), missing))
    assert not extra, (
        "BASE_UNIT_TO_PINT names %d code(s) that are not native units: %s"
        % (len(extra), extra))


@needs_lib
def test_the_affine_set_is_the_librarys_own():
    """The affine units the sweep skips are exactly the ones C calls affine.

    test_pint_bridge's _AFFINE is a hand-written set of six names, and the sweep
    above hands every affine unit to it. Nothing checked the two agree, so a
    seventh affine unit would be skipped HERE (no multiplicative factor) and
    absent THERE (not in the set) -- uncovered by both, which is worse than
    being uncovered by one."""
    from bovnar.structs import make_unit_compound as _mk
    c_affine = set()
    for b in BaseUnit:
        if not (0 < int(b) <= int(UNIT_NATIVE_LAST)):
            continue
        if int(b) not in BASE_UNIT_TO_PINT:
            continue
        if unit_to_si_factor(_mk([{'base': b}])).is_affine:
            c_affine.add(b.name)
    assert c_affine == _BRIDGE_AFFINE, (
        "the library reports %s as affine; test_pint_bridge._AFFINE says %s"
        % (sorted(c_affine), sorted(_BRIDGE_AFFINE)))


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
