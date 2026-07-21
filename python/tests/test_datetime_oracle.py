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


def _oracle(y, mo, d, H, M, S, epoch, offset_s=0):
    # fold the tz offset to true UTC: UTC = civil - offset
    unix_s = calendar.timegm((y, mo, d, H, M, S, 0, 0, 0)) - offset_s
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


@needs_lib
def test_fractional_seconds_floor_to_whole_second_carrier():
    # the carrier is whole seconds, so the value floors to the written second
    # (the fraction digits are preserved separately; the carrier is unaffected)
    base = bovnar.loads("#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z;")["t"]
    for frac in (".0", ".5", ".999", ".000000123", ".123456789"):
        doc = f"#!bovnar 1.1\n.t = 2026-06-15T12:00:00{frac}Z;\n"
        assert bovnar.loads(doc)["t"] == base


@needs_lib
def test_tz_offset_folds_to_utc():
    # ±HH:MM offsets, incl. half-hour (India), -12, +14, and tai (leap-correct
    # because the offset is folded before the leap-second lookup). Verified
    # against the independent reference with the offset folded to true UTC.
    OFFS = [(0, "+00:00"), (7200, "+02:00"), (-18000, "-05:00"),
            (19800, "+05:30"), (-43200, "-12:00"), (50400, "+14:00")]
    n = 0
    for y in (1972, 2000, 2017, 2026):
        for (H, M, S) in ((0, 0, 0), (12, 30, 15), (23, 59, 59)):
            for off_s, zone in OFFS:
                for ep in _EPOCHS:
                    n += 1
                    ann = "" if ep == "unix" else f"<datetime:64,{ep}> "
                    doc = (f"#!bovnar 1.1\n.t = {ann}"
                           f"{y:04d}-06-15T{H:02d}:{M:02d}:{S:02d}{zone};\n")
                    assert bovnar.loads(doc)["t"] == _oracle(y, 6, 15, H, M, S, ep, off_s)
    assert n > 200


# Every table entry after the first is a positive leap second of exactly one
# second, i.e. a real insertion UTC spells 23:59:60. Entry 0 establishes the
# initial 1972 offset and inserts nothing.
_INSERTIONS = [(thr, off) for i, (thr, off) in enumerate(_LEAP)
               if i > 0 and off == _LEAP[i - 1][1] + 1]


def _utc_civil_before(uniform_utc):
    """Civil fields of the UTC second one below `uniform_utc` (TAI-epoch scale)."""
    unix_s = uniform_utc - (40587 - 36204) * 86400 - 1
    t = __import__("time").gmtime(unix_s)
    return t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec


@needs_lib
def test_leap_second_has_its_own_tai_value():
    # The inserted second is a real instant on the continuous atomic scale, so
    # on tai it must NOT collapse onto the following second: it sits exactly one
    # TAI second below the boundary. This is what makes UTC<->TAI injective.
    assert len(_INSERTIONS) == 27, f"expected 27 insertions, got {len(_INSERTIONS)}"
    for thr, off in _INSERTIONS:
        y, mo, d, H, M, _ = _utc_civil_before(thr)
        leap = f"{y:04d}-{mo:02d}-{d:02d}T{H:02d}:{M:02d}:60Z"
        got = bovnar.loads(f"#!bovnar 1.1\n.t = <datetime:64,tai> {leap};\n")["t"]
        assert got == thr + off - 1, f"{leap}: got {got}, want {thr + off - 1}"
        # ... and the second after it is a distinct value, one higher.
        nxt = bovnar.loads(
            f"#!bovnar 1.1\n.t = <datetime:64,tai> {_oracle_iso(thr)};\n")["t"]
        assert nxt == got + 1, f"{leap}: next second {nxt} not {got + 1}"


def _oracle_iso(uniform_utc):
    unix_s = uniform_utc - (40587 - 36204) * 86400
    t = __import__("time").gmtime(unix_s)
    return "%04d-%02d-%02dT%02d:%02d:%02dZ" % (
        t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec)


@needs_lib
def test_leap_second_collapses_on_civil_epochs():
    # POSIX time runs a uniform 86400 s day and has no second to spend on the
    # insertion, so there :60 must still fold onto the following second.
    for thr, _ in _INSERTIONS:
        y, mo, d, H, M, _ = _utc_civil_before(thr)
        leap = f"{y:04d}-{mo:02d}-{d:02d}T{H:02d}:{M:02d}:60Z"
        for ep in ("unix", "mjd", "ntp", "y2000"):
            ann = "" if ep == "unix" else f"<datetime:64,{ep}> "
            got = bovnar.loads(f"#!bovnar 1.1\n.t = {ann}{leap};\n")["t"]
            assert got == _oracle(y, mo, d, H, M, 60, ep), f"{leap} on {ep}: {got}"


@needs_lib
def test_leap_second_survives_a_tz_offset():
    # An ISO offset is whole minutes, so it moves the wall clock but not the
    # second-of-minute: the :60 spelling must survive the fold to UTC.
    doc_z = "#!bovnar 1.1\n.t = <datetime:64,tai> 2016-12-31T23:59:60Z;\n"
    doc_tz = "#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T00:59:60+01:00;\n"
    assert bovnar.loads(doc_z)["t"] == bovnar.loads(doc_tz)["t"] == 1861920036


@needs_lib
def test_unrecorded_leap_second_still_collapses():
    # The table is a static snapshot. A build predating an IERS announcement
    # must not reject a document spelling a genuine future leap second, so a
    # :60 the table does not record keeps the historical collapse.
    a = bovnar.loads("#!bovnar 1.1\n.t = <datetime:64,tai> 2020-06-30T23:59:60Z;\n")["t"]
    b = bovnar.loads("#!bovnar 1.1\n.t = <datetime:64,tai> 2020-07-01T00:00:00Z;\n")["t"]
    assert a == b
