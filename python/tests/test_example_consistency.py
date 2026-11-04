# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""The shipped examples must be internally consistent, not merely parseable.

CTest validates every examples/*.bvnr, but validity says nothing about whether
the numbers in them agree with each other -- and these files ship in the release
archive and are what a new user copies from. Two did not:

  * crypto_portfolio.bvnr declared a portfolio total of 27990.00 USD where the
    four holdings sum to 108426.25.
  * its .btc_decimal said 0.547820 BTC beside a .btc_balance_sat of 547820000,
    which is 5.478200 BTC -- a factor of ten.
"""
import os
from decimal import Decimal

import pytest

from conftest import needs_lib

import bovnar

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXAMPLES = os.path.join(ROOT, "examples")


def _load(name):
    with open(os.path.join(EXAMPLES, name), "rb") as f:
        return bovnar.loads(f.read(), typed=True)


@needs_lib
def test_crypto_portfolio_total_matches_its_holdings():
    doc = _load("crypto_portfolio.bvnr")
    holdings = doc["portfolio"]["holdings"]
    total = sum(Decimal(h["amount"].raw) * Decimal(h["current_price"].raw)
                for h in holdings)
    declared = Decimal(doc["portfolio"]["total_value_usd"].raw)
    assert total == declared, (
        "holdings sum to %s but .total_value_usd says %s" % (total, declared))


@needs_lib
def test_btc_decimal_matches_the_satoshi_balance():
    doc = _load("crypto_portfolio.bvnr")
    sats = Decimal(doc["btc_balance_sat"].raw)
    dec = Decimal(doc["btc_decimal"].raw)
    assert dec == sats / Decimal(10) ** 8, (
        "%s sat is %s BTC, not %s" % (sats, sats / Decimal(10) ** 8, dec))


@needs_lib
def test_units_example_derived_count_matches_its_own_header():
    """The section header states a count; the section must actually have it."""
    import re
    path = os.path.join(EXAMPLES, "units.bvnr")
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    claimed = None
    counted = 0
    for i, line in enumerate(lines):
        m = re.search(r"\((\d+) named SI-derived units\)", line)
        if m:
            claimed = int(m.group(1))
            for rest in lines[i + 1:]:
                if rest.startswith("# 4."):
                    break
                if rest.startswith(".u_"):
                    counted += 1
            break
    assert claimed is not None, "the SI-derived section header went missing"
    assert counted == claimed, (
        "header claims %d named SI-derived units, section has %d"
        % (claimed, counted))


def _example_files():
    return sorted(n for n in os.listdir(EXAMPLES) if n.endswith(".bvnr"))


@needs_lib
@pytest.mark.parametrize("name", _example_files())
def test_example_typed_round_trip(name):
    """loads(typed=True) -> dumps -> loads(typed=True) gives the same document
    for every shipped example (doc/09 §4.1). floats.bvnr and strings.bvnr hold a
    quoted float under a default annotation, which dumps() used to write bare
    and the writer refused; arrays.bvnr and units.bvnr spell out defaults
    (`<float>`, `_10`) that the reloaded Quantity did not compare equal to."""
    with open(os.path.join(EXAMPLES, name), "rb") as f:
        doc = bovnar.loads(f.read(), typed=True)
    out = bovnar.dumps(doc)
    assert bovnar.loads(out, typed=True) == doc


@needs_lib
@pytest.mark.parametrize("a, b", [
    (b'.a = <float:0> 2.71828;', b'.a = 2.71828;'),
    (b'.a = <float:64,_10> 5.7;', b'.a = 5.7;'),
    (b'.a = <uint:32,_10,no_unit> 42;', b'.a = <uint:32> 42;'),
    (b'.a = <sint:0,_10> -3;', b'.a = -3;'),
])
def test_quantity_equality_uses_the_effective_type(a, b):
    qa = bovnar.loads(a, typed=True)["a"]
    qb = bovnar.loads(b, typed=True)["a"]
    assert qa == qb and hash(qa) == hash(qb)


@needs_lib
def test_quantity_equality_keeps_q_and_epoch():
    """float_fix's base is Q and datetime's the epoch index: no default there."""
    q8 = bovnar.loads(b'.a = <float_fix:32,q8> 1.5;', typed=True)["a"]
    q16 = bovnar.loads(b'.a = <float_fix:32,q16> 1.5;', typed=True)["a"]
    assert q8 != q16
    doc = b'#!bovnar 1.1\n.a = <datetime:64,%s> 0;'
    unix = bovnar.loads(doc % b'unix', typed=True)["a"]
    tai = bovnar.loads(doc % b'tai', typed=True)["a"]
    assert unix != tai
