# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Differential oracle for ISO-8601 datetime literal conversion (spec 1.1).

bovnar.loads() parses an ISO-8601 literal through the C reader and yields the
epoch-seconds carrier. This test cross-checks that carrier against an
*independent* reference — CPython's leap-free UTC arithmetic (calendar.timegm)
for the civil epochs, and the IERS leap-second table for tai — over a broad
corpus (years 1..9999 incl. pre-epoch, leap years, month/day/time boundaries).
A divergence means the C conversion is wrong, not the test fixture.
"""

import calendar

import pytest

from conftest import needs_lib

import bovnar

# Epoch MJD origins (mirror bvn_epoch_t). The civil epochs share the leap-free
# uniform scale; tai is the only literal-supported atomic epoch.
EPOCH_MJD = {"unix": 40587, "mjd": 0, "ntp": 15020, "y2000": 51544, "tai": 36204}

# IERS TAI-UTC table: thresholds in TAI-epoch uniform seconds -> (TAI - UTC).
# Verbatim from src/utils/bvn_datetime.c; pre-1972 instants use the first entry.
_LEAP = [
    (441763200, 10), (457488000, 11), (473385600, 12), (504921600, 13),
    (536457600, 14), (567993600, 15), (599616000, 16), (631152000, 17),
    (662688000, 18), (694224000, 19), (741484800, 20), (773020800, 21),
    (804556800, 22), (867715200, 23), (946684800, 24), (1009843200, 25),
    (1041379200, 26), (1088640000, 27), (1120176000, 28), (1151712000, 29),
    (1199145600, 30), (1246406400, 31), (1293840000, 32), (1514764800, 33),
    (1609459200, 34), (1719792000, 35), (1814400000, 36), (1861920000, 37),
]


def _leap_at(uniform_tai):
    off = _LEAP[0][1]
    for thr, val in _LEAP:
        if uniform_tai >= thr:
            off = val
    return off


def _oracle(y, mo, d, H, M, S, epoch):
    unix_s = calendar.timegm((y, mo, d, H, M, S, 0, 0, 0))
    if epoch == "tai":
        uniform_tai = unix_s + (40587 - 36204) * 86400
        return uniform_tai + _leap_at(uniform_tai)
    return unix_s + (40587 - EPOCH_MJD[epoch]) * 86400


def _dim(y, m):
    days = [31, 29 if (y % 4 == 0 and y % 100 != 0) or y % 400 == 0 else 28,
            31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    return days[m - 1]


# Years: leap-second boundaries, epoch starts, pre-epoch, DST-ish, far future.
_YEARS = [1, 100, 1583, 1700, 1900, 1969, 1970, 1971, 1972, 1999, 2000,
          2016, 2017, 2024, 2025, 2026, 2038, 2100, 2400, 9999]
_TIMES = [(0, 0, 0), (12, 0, 0), (23, 59, 59), (6, 30, 15)]
_EPOCHS = ["unix", "mjd", "ntp", "y2000", "tai"]


@needs_lib
def test_iso_conversion_matches_independent_reference():
    mism = []
    n = 0
    for y in _YEARS:
        for mo in (1, 2, 6, 12):
            for d in (1, 15, min(28, _dim(y, mo)), _dim(y, mo)):
                for (H, M, S) in _TIMES:
                    for ep in _EPOCHS:
                        n += 1
                        iso = f"{y:04d}-{mo:02d}-{d:02d}T{H:02d}:{M:02d}:{S:02d}Z"
                        ann = "" if ep == "unix" else f"<datetime:64,{ep}> "
                        doc = f"#!bovnar 1.1\n.t = {ann}{iso};\n"
                        got = bovnar.loads(doc)["t"]
                        exp = _oracle(y, mo, d, H, M, S, ep)
                        if got != exp:
                            mism.append((iso, ep, got, exp))
    assert not mism, f"{len(mism)}/{n} mismatches, first: {mism[:5]}"
    assert n > 5000, f"corpus unexpectedly small ({n})"


@needs_lib
def test_date_only_literal_is_midnight_utc():
    for y in (1970, 2000, 2026, 9999):
        for mo in (1, 6, 12):
            for d in (1, _dim(y, mo)):
                doc = f"#!bovnar 1.1\n.t = {y:04d}-{mo:02d}-{d:02d};\n"
                assert bovnar.loads(doc)["t"] == _oracle(y, mo, d, 0, 0, 0, "unix")
