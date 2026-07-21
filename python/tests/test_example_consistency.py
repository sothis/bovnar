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
