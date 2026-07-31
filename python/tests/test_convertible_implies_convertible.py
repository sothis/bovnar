"""
`units_convertible` is a screen, and the gap between it and the conversion is
exactly one shape wide.

doc/05 §12.4 used to claim in one paragraph that `bvn_units_convertible` reports
"exactly the set `bvn_unit_convert_value` and `bvn_unit_convert_rational`
accept", and in the next that it is "a screen, not a guarantee -- `s/°C` passes
here and the conversion entry points still refuse it". The second paragraph was
right, and screening on the first cost three silent policy failures (a target
that claimed `°C/h -> K/h` and then delivered the °C/h value, and two rule modes
that reported OK on a document satisfying neither).

The document now says the screen is a SUPERSET, and that the gap is one shape:
an affine scale outside "alone, at exponent 1". This is the assertion behind
that word "one". If a second shape ever opens, a policy can start claiming
values it cannot deliver again, and the sentence in doc/05 quietly becomes
false.
"""
import itertools
import os
import random
import re

import bovnar
import pytest

AFFINE = ("°C", "°F", "°De", "°N", "°Re", "°Ro")


# The suite runs with cwd=python/, so anchor on this file rather than on the
# working directory: a relative "src/gendata/..." silently skipped the sweep.
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _catalogue():
    path = os.path.join(_REPO, "src", "gendata", "units.bvnr")
    with open(path, encoding="utf-8") as f:
        return re.findall(r'\.symbol\s*=\s*"([^"]+)"', f.read())


def _affine_misplaced(spelling):
    """True when an affine scale appears anywhere but alone at exponent 1."""
    if spelling in AFFINE:
        return False
    return any(a in spelling for a in AFFINE)


@pytest.mark.needs_lib
def test_convertible_that_cannot_convert_is_always_the_affine_shape():
    try:
        symbols = _catalogue()
    except OSError:                       # running from an installed wheel
        pytest.skip("src/gendata is not present")

    rng = random.Random(20260731)
    spellings = list(symbols)
    for sym in symbols:
        spellings += ["%s/%s" % (sym, other) for other in rng.sample(symbols, 2)]
        spellings.append("%s²" % sym)

    units = []
    for spelling in spellings:
        try:
            units.append((spelling, bovnar.parse_unit(spelling)))
        except Exception:                                     # noqa: BLE001
            continue
    rng.shuffle(units)

    screened = 0
    unexplained = []
    for (sa, a), (sb, b) in itertools.islice(
            itertools.combinations(units, 2), 0, 120_000):
        if not bovnar.units_convertible(a, b):
            continue
        screened += 1
        try:
            bovnar.convert_value(1.0, a, b)
        except Exception:                                     # noqa: BLE001
            if not (_affine_misplaced(sa) or _affine_misplaced(sb)):
                unexplained.append((sa, sb))

    assert screened > 500, "the sweep stopped screening anything"
    assert not unexplained, (
        "units_convertible passed a pair the conversion refused, and it is not "
        "the documented affine-in-a-compound shape: %r" % unexplained[:10])


@pytest.mark.needs_lib
def test_the_affine_shape_really_is_in_the_gap():
    """The other half: without this the test above passes vacuously."""
    a, b = bovnar.parse_unit("°C/h"), bovnar.parse_unit("K/h")
    assert bovnar.units_convertible(a, b) is True
    with pytest.raises(Exception):
        bovnar.convert_value(1.0, a, b)
    # ...and a lone affine is not in the gap.
    c, k = bovnar.parse_unit("°C"), bovnar.parse_unit("K")
    assert bovnar.units_convertible(c, k) is True
    assert bovnar.convert_value(25.0, c, k) == pytest.approx(298.15)
