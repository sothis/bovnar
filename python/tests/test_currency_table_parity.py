# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
"""bovnar.currency's table against src/gendata/currencies.bvnr.

currency.py carries its own hand-written copy of all 216 currencies. That is
deliberate and its docstring says why: the module is standalone so a linter, a
validator or a code generator can import it with no libbvnr.so present. What was
missing is the gate. The C table is GENERATED from currencies.bvnr by
gen_currencies.py, so it cannot drift; this third copy is maintained by hand and
nothing compared the two, in a repo whose whole convention is that gendata is the
single source of truth and every derived copy is checked against it —
bvnr_data_parity does exactly this for the dependency-free reader.

The two agreed when this was written. The point is that nothing would have said
so: adding a currency to currencies.bvnr regenerates the C enum and table, and a
Python table left behind would report a wrong minor_unit for money — the field
applications actually format with — while every other gate stayed green.

Pure Python on both sides, no library needed, matching currency.py's own premise.
"""
import os
import sys

import pytest

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, _ROOT)
import bvnr_data  # noqa: E402

from bovnar import currency as cur  # noqa: E402


@pytest.fixture(scope="module")
def tables():
    with open(os.path.join(_ROOT, "src", "gendata", "currencies.bvnr"),
              encoding="utf-8") as f:
        src = {c["id"]: c for c in bvnr_data.load(f.read())["currencies"]}
    py = {int(i.base): i for i in cur.all_currencies()}
    return src, py


def test_the_same_currencies_are_present(tables):
    src, py = tables
    missing = sorted(src[i]["code"] for i in set(src) - set(py))
    extra = sorted(py[i].code for i in set(py) - set(src))
    assert not missing, (
        "currencies.bvnr has these, currency.py does not: %s" % missing)
    assert not extra, (
        "currency.py has these, currencies.bvnr does not: %s" % extra)
    assert len(src) == len(py) == 216


@pytest.mark.parametrize("field,from_src,from_py", [
    ("code",         lambda c: c["code"],            lambda i: i.code),
    ("numeric_code", lambda c: c["numeric_code"],    lambda i: i.numeric_code),
    ("minor_unit",   lambda c: c["minor_unit"],      lambda i: i.minor_unit),
    ("is_crypto",    lambda c: bool(c["is_crypto"]), lambda i: bool(i.is_crypto)),
    ("name",         lambda c: c["name"],            lambda i: i.name),
])
def test_every_field_matches_the_source(field, from_src, from_py, tables):
    src, py = tables
    bad = []
    for base in sorted(set(src) & set(py)):
        want, got = from_src(src[base]), from_py(py[base])
        if want != got:
            bad.append("  %-5s (id %d): currencies.bvnr=%r  currency.py=%r"
                       % (src[base]["code"], base, want, got))
    assert not bad, (
        "%d currency %s value(s) drifted from the source of truth:\n%s"
        % (len(bad), field, "\n".join(bad)))


def test_the_range_predicates_agree_with_the_source(tables):
    """is_currency/is_fiat/is_crypto are range checks over the enum, written
    against the id blocks in currencies.bvnr rather than derived from it."""
    src, _ = tables
    bad = []
    for base, c in sorted(src.items()):
        if not cur.is_currency(base):
            bad.append("%s: is_currency(%d) is False" % (c["code"], base))
        if cur.is_crypto(base) != bool(c["is_crypto"]):
            bad.append("%s: is_crypto disagrees" % c["code"])
        if cur.is_fiat(base) == bool(c["is_crypto"]):
            bad.append("%s: is_fiat disagrees" % c["code"])
    assert not bad, "\n".join(bad)


@pytest.mark.parametrize("base", [0, 1, 133, 348, 377, 380, 396, 397, 100000])
def test_non_currency_bases_are_rejected(base):
    """The id space is not contiguous — physical units run 1..133, 348..377 and
    380.., with currencies at 134..347 and 378..379. A predicate that treated it
    as one block would claim a unit is money."""
    assert not cur.is_currency(base), (
        "base %d is a physical unit or out of range, not a currency" % base)
