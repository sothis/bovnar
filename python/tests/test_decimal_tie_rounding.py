# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""An exact decimal tie must round half-to-even, as IEEE 754 requires.

Quantity's decimal-interchange helpers used to encode the PARSED bvn_float --
i.e. a value that had already been rounded into binary. A decimal tie is in
general not exactly representable in binary, so it lands slightly above or below
and the decimal rounding then follows the intermediate instead of the tie rule.
Over 4000 exact decimal64 ties that route disagreed with IEEE on 26.4 % of them,
and not consistently in one direction -- it was whichever side the binary landed
on, which is why it went unnoticed.

The C library had the right primitive all along (bvn_float_parse_dec*, used by
nothing public); it is now exported as bvn_float_strtodec and this is what the
bindings call.
"""

import random
from decimal import Decimal, Context, ROUND_HALF_EVEN

import pytest

from conftest import needs_lib

import bovnar

_CTX = Context(prec=16, rounding=ROUND_HALF_EVEN)


def _ties(n, seed):
    """Literals with a 17-digit coefficient ending in 5: exactly half a unit in
    decimal64's last kept place."""
    rnd = random.Random(seed)
    for _ in range(n):
        coeff = rnd.randrange(10 ** 15, 10 ** 16) * 10 + 5
        yield "%s%de%d" % ("-" if rnd.random() < 0.5 else "",
                           coeff, rnd.randrange(-60, 60))


@needs_lib
def test_exact_decimal64_ties_round_half_to_even():
    bad = []
    n = 0
    for lit in _ties(2000, 3):
        doc = "#!bovnar 1.1\n.v = <float_dec:64> %s;\n" % lit
        q = bovnar.loads(doc, typed=True)["v"]
        n += 1
        want = _CTX.create_decimal(Decimal(lit))
        got = Decimal(q.stored_value())
        if got != want:
            bad.append((lit, str(got), str(want)))
    assert n >= 2000
    assert not bad, "%d/%d ties rounded wrong, first: %s" % (len(bad), n, bad[:3])


@needs_lib
def test_named_tie_cases():
    """Spelled out so a regression names itself rather than reporting a rate."""
    cases = {
        "1.0000000000000015": "1.000000000000002",   # last kept digit odd -> up
        "1.0000000000000025": "1.000000000000002",   # even -> stays
        "52906102648424555e-10": "5290610.264842456",
        "57123991439313165e-11": "571239.9143931316",
    }
    for lit, want in cases.items():
        doc = "#!bovnar 1.1\n.v = <float_dec:64> %s;\n" % lit
        q = bovnar.loads(doc, typed=True)["v"]
        assert Decimal(q.stored_value()) == Decimal(want), \
            "%s -> %s, want %s" % (lit, q.stored_value(), want)
