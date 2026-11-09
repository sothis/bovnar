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

"""bovnar.timekeeping: the civil-time and calendar functions of bvn_datetime.h
and bvn_gregorian_date.h. The tables at the top are the same as in
rust/tests/test_timekeeping.rs, so the two bindings are held to one set of
answers."""

import datetime

import pytest

from conftest import needs_lib
import bovnar
from bovnar.exceptions import BovnarArgumentError
from bovnar.timekeeping import (
    MJD_MAX, MJD_MIN, YEAR_MAX, YEAR_MIN, CivilDateTime, Epoch, GregorianDate,
    ascension_day, convert_epoch_seconds, datetime_to_epoch_seconds,
    easter_monday, easter_sunday, easter_sunday_mjd,
    epoch_seconds_from_galileo_time, epoch_seconds_from_gps_time,
    epoch_seconds_to_datetime, epoch_seconds_to_utc, good_friday,
    mjd_in_leap_year, mjd_to_wday, mjd_to_yday, tai_second_is_dls_in_europe,
    tai_seconds_from_galileo_time, tai_seconds_from_gps_time,
    tai_seconds_to_local_time, tai_seconds_to_utc, utc_to_epoch_seconds,
    utc_to_tai_seconds, whit_monday,
)

pytestmark = needs_lib

I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1

# ── shared with rust/tests/test_timekeeping.rs ─────────────────────────────

# The UTC days that end in an inserted leap second, 23:59:60; TAI-UTC is 11 s
# after the first and one more after each.
LEAP_DAYS = [
    (1972, 6, 30), (1972, 12, 31), (1973, 12, 31), (1974, 12, 31),
    (1975, 12, 31), (1976, 12, 31), (1977, 12, 31), (1978, 12, 31),
    (1979, 12, 31), (1981, 6, 30), (1982, 6, 30), (1983, 6, 30),
    (1985, 6, 30), (1987, 12, 31), (1989, 12, 31), (1990, 12, 31),
    (1992, 6, 30), (1993, 6, 30), (1994, 6, 30), (1995, 12, 31),
    (1997, 6, 30), (1998, 12, 31), (2005, 12, 31), (2008, 12, 31),
    (2012, 6, 30), (2015, 6, 30), (2016, 12, 31),
]

EASTER = [
    (1818, 3, 22), (1943, 4, 25), (2000, 4, 23), (2019, 4, 21),
    (2024, 3, 31), (2025, 4, 20), (2038, 4, 25), (2285, 3, 22),
]

GERMAN_HOLIDAYS_2024 = [
    (1, 1), (3, 29), (4, 1), (5, 1), (5, 9), (5, 20), (10, 3), (12, 25),
    (12, 26),
]

# (date, what the month and quarter getters give for it)
BANKING_2024 = [
    # date        last         penultimate  mid          first
    ((2024, 3, 5), (2024, 3, 28), (2024, 3, 27), (2024, 3, 15), (2024, 3, 1)),
    ((2024, 5, 31), (2024, 5, 31), (2024, 5, 30), (2024, 5, 15), (2024, 5, 2)),
    ((2024, 6, 1), (2024, 6, 28), (2024, 6, 27), (2024, 6, 17), (2024, 6, 3)),
    ((2024, 12, 9), (2024, 12, 30), (2024, 12, 27), (2024, 12, 16), (2024, 12, 2)),
]


def D(y, m, d):
    return GregorianDate(y, m, d)


# ── GregorianDate ──────────────────────────────────────────────────────────

def test_mjd_of_known_days():
    assert D(2000, 1, 1).to_mjd() == 51544
    assert D(1858, 11, 17).to_mjd() == 0
    assert GregorianDate.from_mjd(51544) == D(2000, 1, 1)
    assert GregorianDate.from_mjd(44244) == D(1980, 1, 6)
    d = D(2000, 1, 1)
    assert (d.leap, d.wday, d.yday) == (True, 6, 0)      # a Saturday
    d = D(2024, 12, 31)
    assert (d.leap, d.wday, d.yday) == (True, 2, 365)


def test_range_ends():
    assert GregorianDate.from_mjd(MJD_MIN) == D(YEAR_MIN, 1, 1)
    assert GregorianDate.from_mjd(MJD_MAX) == D(YEAR_MAX, 12, 31)
    assert D(YEAR_MAX, 12, 31).to_mjd() == MJD_MAX
    for bad in (MJD_MIN - 1, MJD_MAX + 1, I64_MIN):
        with pytest.raises(BovnarArgumentError):
            GregorianDate.from_mjd(bad)
        with pytest.raises(BovnarArgumentError):
            mjd_in_leap_year(bad)
        with pytest.raises(BovnarArgumentError):
            mjd_to_yday(bad)
    # The weekday is defined for every int64.
    assert mjd_to_wday(I64_MIN) in range(7) and mjd_to_wday(I64_MAX) in range(7)
    with pytest.raises(BovnarArgumentError):
        D(YEAR_MAX + 1, 1, 1)
    with pytest.raises(BovnarArgumentError):
        GregorianDate.normalize(YEAR_MAX, 13, 1)


def test_strict_constructor_and_normalize():
    for y, m, d in [(2023, 2, 29), (2024, 2, 30), (2024, 13, 1), (2024, 0, 1),
                    (2024, 4, 31), (2024, 1, 0), (1900, 2, 29)]:
        with pytest.raises(BovnarArgumentError):
            D(y, m, d)
    assert D(2000, 2, 29).leap and D(2024, 2, 29).leap
    assert GregorianDate.normalize(2024, 2, 30) == D(2024, 3, 1)
    assert GregorianDate.normalize(2024, 13, 1) == D(2025, 1, 1)
    assert GregorianDate.normalize(2024, 3, 0) == D(2024, 2, 29)
    assert GregorianDate.normalize(2024, 0, 1) == D(2023, 12, 1)
    assert GregorianDate.normalize(2024, 1, -365) == D(2022, 12, 31)
    assert GregorianDate.normalize(2024, 3, 61) == D(2024, 4, 30)
    for bad in (1.0, '1', None, True, 1 << 63):
        with pytest.raises(BovnarArgumentError):
            D(2024, 1, bad)


def test_date_value_semantics():
    a, b = D(2024, 3, 31), GregorianDate.from_mjd(D(2024, 3, 31).to_mjd())
    assert a == b and hash(a) == hash(b) and a is not b
    assert D(2024, 3, 30) < a < D(2024, 4, 1) < D(2025, 1, 1)
    assert a != D(2024, 4, 1) and a != (2024, 3, 31)
    assert str(a) == '2024-03-31' and str(D(-1, 1, 1)) == '-0001-01-01'
    assert repr(a) == 'GregorianDate(year=2024, month=3, day=31)'
    assert a.to_date() == datetime.date(2024, 3, 31)
    with pytest.raises(BovnarArgumentError):
        D(0, 1, 1).to_date()
    with pytest.raises(Exception):
        a.year = 2025                            # frozen


def test_mjd_round_trip_against_the_stdlib():
    base = datetime.date(2000, 1, 1)
    for mjd in range(-678575, 2973483, 997):    # 0001-01-01 .. 9999-12-31
        g = GregorianDate.from_mjd(mjd)
        assert g.to_mjd() == mjd
        assert g.wday == mjd_to_wday(mjd)
        assert g.yday == mjd_to_yday(mjd)
        assert g.leap == mjd_in_leap_year(mjd)
        py = base + datetime.timedelta(days=mjd - 51544)
        assert (g.year, g.month, g.day) == (py.year, py.month, py.day)
        assert g.wday == py.isoweekday() % 7
        assert g.yday == py.timetuple().tm_yday - 1
    for mjd in range(MJD_MIN, MJD_MIN + 800):
        assert GregorianDate.from_mjd(mjd).to_mjd() == mjd
    for mjd in range(MJD_MAX - 800, MJD_MAX + 1):
        assert GregorianDate.from_mjd(mjd).to_mjd() == mjd


# ── Easter and the German calendar ─────────────────────────────────────────

def test_easter():
    for y, m, d in EASTER:
        assert easter_sunday(y) == D(y, m, d)
        assert easter_sunday_mjd(y) == D(y, m, d).to_mjd()
    assert good_friday(2024) == D(2024, 3, 29)
    assert easter_monday(2024) == D(2024, 4, 1)
    assert ascension_day(2024) == D(2024, 5, 9)
    assert whit_monday(2024) == D(2024, 5, 20)
    assert whit_monday(2025) == D(2025, 6, 9)
    assert easter_sunday(YEAR_MAX).year == YEAR_MAX
    assert whit_monday(YEAR_MAX).year == YEAR_MAX
    for fn in (easter_sunday, good_friday, whit_monday, easter_sunday_mjd):
        with pytest.raises(BovnarArgumentError):
            fn(YEAR_MAX + 1)
        with pytest.raises(BovnarArgumentError):
            fn(YEAR_MIN - 1)


def test_german_federal_holidays():
    days = [GregorianDate.from_mjd(m)
            for m in range(D(2024, 1, 1).to_mjd(), D(2025, 1, 1).to_mjd())]
    holidays = [(d.month, d.day) for d in days
                if d.is_federal_holiday_in_germany()]
    assert holidays == GERMAN_HOLIDAYS_2024
    # The dated rules apply per year.
    assert D(1985, 6, 17).is_federal_holiday_in_germany()
    assert not D(1985, 10, 3).is_federal_holiday_in_germany()
    assert D(1990, 10, 3).is_federal_holiday_in_germany()
    assert not D(1990, 6, 17).is_federal_holiday_in_germany()
    assert not D(1932, 5, 1).is_federal_holiday_in_germany()
    assert D(1933, 5, 1).is_federal_holiday_in_germany()
    assert not D(2024, 12, 24).is_federal_holiday_in_germany()


def test_banking_and_business_days():
    assert D(2024, 3, 28).is_banking_day_in_germany()          # Thursday
    for d in (D(2024, 3, 29), D(2024, 3, 30), D(2024, 3, 31),  # Fri hol., Sat, Sun
              D(2024, 12, 24), D(2024, 12, 31)):
        assert not d.is_banking_day_in_germany(), d
    assert D(2024, 3, 30).is_business_day_in_germany()         # Saturday
    assert D(2024, 12, 24).is_business_day_in_germany()
    assert D(2024, 12, 31).is_business_day_in_germany()
    assert not D(2024, 3, 31).is_business_day_in_germany()     # Sunday
    assert not D(2024, 3, 29).is_business_day_in_germany()     # Good Friday


def test_banking_day_getters_and_predicates():
    for date, last, pen, mid, first in BANKING_2024:
        g = D(*date)
        assert g.last_banking_day_in_month() == D(*last)
        assert g.penultimate_banking_day_in_month() == D(*pen)
        assert g.mid_banking_day_in_month() == D(*mid)
        assert g.first_banking_day_in_month() == D(*first)
        assert D(*last).is_last_banking_day_in_month()
        assert D(*pen).is_penultimate_banking_day_in_month()
        assert D(*mid).is_mid_banking_day_in_month()
        assert D(*first).is_first_banking_day_in_month()
        assert not D(*pen).is_last_banking_day_in_month()
    g = D(2024, 8, 8)
    assert g.last_banking_day_in_quarter(1) == D(2024, 3, 28)
    assert g.first_banking_day_in_quarter(2) == D(2024, 4, 2)  # 1 April: Easter Monday
    assert g.last_banking_day_in_quarter(4) == D(2024, 12, 30)
    assert g.first_banking_day_in_quarter(1) == D(2024, 1, 2)
    assert D(2024, 12, 30).is_last_banking_day_in_quarter(4)
    assert not D(2024, 12, 30).is_last_banking_day_in_quarter(3)
    assert D(2024, 4, 2).is_first_banking_day_in_quarter(2)
    for q in (0, 5, -1):
        with pytest.raises(BovnarArgumentError):
            g.last_banking_day_in_quarter(q)
        with pytest.raises(BovnarArgumentError):
            g.is_first_banking_day_in_quarter(q)
    assert g.last_sunday_in_month() == D(2024, 8, 25)
    assert g.last_saturday_in_month() == D(2024, 8, 31)
    assert D(2024, 3, 1).last_sunday_in_month() == D(2024, 3, 31)
    assert D(2024, 10, 1).last_sunday_in_month() == D(2024, 10, 27)


def test_daylight_saving_switch_days():
    for y, m, d in [(2024, 3, 31), (2024, 10, 27), (1995, 9, 24),
                    (1996, 10, 27), (1980, 4, 6), (1980, 9, 28)]:
        assert D(y, m, d).is_daylight_saving_switch_in_germany(), (y, m, d)
        assert GregorianDate.normalize(y, m, d - 1) \
            .is_day_before_daylight_saving_switch_in_germany()
    for y, m, d in [(1979, 3, 25), (1995, 10, 29), (1980, 3, 30),
                    (2024, 3, 24), (2024, 3, 30)]:
        assert not D(y, m, d).is_daylight_saving_switch_in_germany(), (y, m, d)
    assert D(2024, 3, 30).is_day_before_daylight_saving_switch_in_germany()


# ── bvn_datetime.h ─────────────────────────────────────────────────────────

def test_civil_datetime_checks_its_fields():
    for bad in [(2024, 1, 1, 24, 0, 0), (2024, 1, 1, 0, 60, 0),
                (2024, 1, 1, 0, 0, 61), (2024, 1, 1, -1, 0, 0),
                (2024, 2, 30, 0, 0, 0)]:
        with pytest.raises(BovnarArgumentError):
            CivilDateTime(*bad)
    t = CivilDateTime(2016, 12, 31, 23, 59, 60)
    assert str(t) == '2016-12-31T23:59:60'
    assert t.date == D(2016, 12, 31) and t.mjd == 57753
    assert t == CivilDateTime(2016, 12, 31, 23, 59, 60)
    assert t != CivilDateTime(2016, 12, 31, 23, 59, 59)
    assert CivilDateTime(2016, 12, 31, 23, 59, 59) < t < CivilDateTime(2017, 1, 1)
    assert t.is_date_equal(CivilDateTime(2016, 12, 31))
    assert t.is_time_equal(CivilDateTime(2000, 1, 1, 23, 59, 60))
    assert not t.is_time_equal(CivilDateTime(2016, 12, 31))
    assert CivilDateTime(2024, 3, 31, 12).to_datetime() \
        == datetime.datetime(2024, 3, 31, 12)
    with pytest.raises(BovnarArgumentError):
        t.to_datetime()


def test_epochs():
    assert [int(e) for e in Epoch] == [0, 15020, 36204, 40587, 44244, 50083,
                                       51412, 51544, 53736]
    for e in Epoch:
        assert Epoch.from_name(e.name.lower()) is e
        assert str(epoch_seconds_to_datetime(0, e)) \
            == str(GregorianDate.from_mjd(int(e))) + 'T00:00:00'
    assert Epoch.from_name('') is Epoch.UNIX
    assert Epoch.from_name('nope') is None
    assert epoch_seconds_to_datetime(0, 'gps') == CivilDateTime(1980, 1, 6)
    assert epoch_seconds_to_datetime(0, 51544) == CivilDateTime(2000, 1, 1)
    for bad in ('nope', 1, 51545, None, 1.5):
        with pytest.raises(BovnarArgumentError):
            epoch_seconds_to_datetime(0, bad)


def test_uniform_seconds():
    assert epoch_seconds_to_datetime(-1, Epoch.UNIX) \
        == CivilDateTime(1969, 12, 31, 23, 59, 59)
    assert epoch_seconds_to_datetime(1750000000, Epoch.UNIX) \
        == CivilDateTime(2025, 6, 15, 15, 6, 40)
    assert datetime_to_epoch_seconds(CivilDateTime(2000, 1, 1), Epoch.UNIX) \
        == 946684800
    # second 60 is the first second of the next minute on a uniform scale
    assert datetime_to_epoch_seconds(CivilDateTime(2016, 12, 31, 23, 59, 60),
                                     Epoch.UNIX) == 1483228800
    for s in (I64_MIN + 1, -1, 0, 1, I64_MAX):
        for e in Epoch:
            assert datetime_to_epoch_seconds(
                epoch_seconds_to_datetime(s, e), e) == s, (s, e)
    # INT64_MIN renders, but is the error sentinel and never a result
    with pytest.raises(BovnarArgumentError):
        datetime_to_epoch_seconds(epoch_seconds_to_datetime(I64_MIN, Epoch.UNIX),
                                  Epoch.UNIX)
    with pytest.raises(BovnarArgumentError):
        datetime_to_epoch_seconds(CivilDateTime(YEAR_MAX, 12, 31), Epoch.UNIX)
    with pytest.raises(BovnarArgumentError):
        datetime_to_epoch_seconds((2000, 1, 1), Epoch.UNIX)
    with pytest.raises(BovnarArgumentError):
        epoch_seconds_to_datetime(1 << 63, Epoch.UNIX)
    unix0 = datetime.datetime(1970, 1, 1)
    for s in range(-2208988800, 4102444800, 86400 * 37 + 3601):
        assert epoch_seconds_to_datetime(s, Epoch.UNIX).to_datetime() \
            == unix0 + datetime.timedelta(seconds=s)


def test_convert_epoch_seconds():
    assert convert_epoch_seconds(0, Epoch.UNIX, Epoch.TAI) == 4383 * 86400
    assert convert_epoch_seconds(4383 * 86400, Epoch.TAI, Epoch.UNIX) == 0
    assert convert_epoch_seconds(0, Epoch.GPS, Epoch.UNIX) == 315964800
    assert convert_epoch_seconds(I64_MAX, Epoch.TAI, Epoch.UNIX) \
        == I64_MAX - 4383 * 86400
    with pytest.raises(BovnarArgumentError):
        convert_epoch_seconds(I64_MAX, Epoch.UNIX, Epoch.TAI)
    with pytest.raises(BovnarArgumentError):
        convert_epoch_seconds(I64_MIN, Epoch.TAI, Epoch.UNIX)


def test_leap_seconds():
    t = utc_to_tai_seconds(CivilDateTime(2016, 12, 31, 23, 59, 60))
    assert t == 1861920036
    assert str(tai_seconds_to_utc(t)) == '2016-12-31T23:59:60'
    assert str(tai_seconds_to_utc(t - 1)) == '2016-12-31T23:59:59'
    assert str(tai_seconds_to_utc(t + 1)) == '2017-01-01T00:00:00'
    for i, (y, m, d) in enumerate(LEAP_DAYS):
        leap = CivilDateTime(y, m, d, 23, 59, 60)
        t = utc_to_tai_seconds(leap)
        assert tai_seconds_to_utc(t) == leap
        nxt = GregorianDate.normalize(y, m, d + 1)
        after = CivilDateTime(nxt.year, nxt.month, nxt.day)
        assert tai_seconds_to_utc(t + 1) == after
        assert utc_to_tai_seconds(after) == t + 1
        assert t + 1 - datetime_to_epoch_seconds(after, Epoch.TAI) == 11 + i
        for k in range(-3, 4):
            assert utc_to_tai_seconds(tai_seconds_to_utc(t + k)) == t + k
    # TAI-UTC: 10 s before 1972, 37 s since 2017
    assert utc_to_tai_seconds(CivilDateTime(1960, 1, 1)) \
        - datetime_to_epoch_seconds(CivilDateTime(1960, 1, 1), Epoch.TAI) == 10
    assert utc_to_tai_seconds(CivilDateTime(2026, 1, 1)) \
        - datetime_to_epoch_seconds(CivilDateTime(2026, 1, 1), Epoch.TAI) == 37
    # :60 on a day without an insertion collapses onto the next second
    assert utc_to_tai_seconds(CivilDateTime(2017, 12, 31, 23, 59, 60)) \
        == utc_to_tai_seconds(CivilDateTime(2018, 1, 1))
    for t in range(-10**10, 10**10, 10**10 // 4999):
        assert utc_to_tai_seconds(tai_seconds_to_utc(t)) == t


def test_tai_extremes():
    with pytest.raises(BovnarArgumentError):
        tai_seconds_to_utc(I64_MIN)
    tai_seconds_to_utc(I64_MIN + 10)            # 10 s offset before 1972
    assert tai_seconds_to_utc(I64_MAX).year > 2 * 10**11
    with pytest.raises(BovnarArgumentError):
        utc_to_tai_seconds(CivilDateTime(YEAR_MIN, 1, 1))


def test_gnss_time():
    assert tai_seconds_to_utc(tai_seconds_from_gps_time(0, 0)) \
        == CivilDateTime(1980, 1, 6)
    assert epoch_seconds_from_gps_time(0, 0, Epoch.GPS) == 0
    assert epoch_seconds_from_gps_time(0, 0, Epoch.TAI) \
        == 19 + convert_epoch_seconds(0, Epoch.GPS, Epoch.TAI)
    # GPS week 2300 starts 2024-02-04 GPS time, 18 s ahead of UTC
    assert tai_seconds_to_utc(tai_seconds_from_gps_time(0, 2300)) \
        == CivilDateTime(2024, 2, 3, 23, 59, 42)
    assert epoch_seconds_to_datetime(
        epoch_seconds_from_gps_time(0, 2300, Epoch.UNIX), Epoch.UNIX) \
        == CivilDateTime(2024, 2, 4)
    # time of week rounds to the nearest second
    assert tai_seconds_from_gps_time(1499, 0) + 1 \
        == tai_seconds_from_gps_time(1500, 0)
    # Galileo week 0 is GPS week 1024
    assert tai_seconds_to_utc(tai_seconds_from_galileo_time(0, 0)) \
        == CivilDateTime(1999, 8, 21, 23, 59, 47)
    for tow, week in [(0, 0), (123456789, 250), (604799000, 1400)]:
        assert tai_seconds_from_galileo_time(tow, week) \
            == tai_seconds_from_gps_time(tow, week + 1024)
        assert epoch_seconds_from_galileo_time(tow, week, Epoch.UNIX) \
            == epoch_seconds_from_gps_time(tow, week + 1024, Epoch.UNIX)
    with pytest.raises(BovnarArgumentError):
        tai_seconds_from_gps_time(0, I64_MAX)
    with pytest.raises(BovnarArgumentError):
        tai_seconds_from_galileo_time(0, I64_MIN)


def test_local_time_and_dst():
    t = utc_to_tai_seconds(CivilDateTime(2016, 12, 31, 23, 59, 60))
    assert str(tai_seconds_to_local_time(t, 3600, False)) == '2017-01-01T00:59:60'
    assert str(tai_seconds_to_local_time(t, -5 * 3600, False)) \
        == '2016-12-31T18:59:60'
    summer = utc_to_tai_seconds(CivilDateTime(2024, 7, 1, 12))
    winter = utc_to_tai_seconds(CivilDateTime(2024, 1, 15, 12))
    assert tai_second_is_dls_in_europe(summer)
    assert not tai_second_is_dls_in_europe(winter)
    assert tai_seconds_to_local_time(summer, 3600, True) \
        == CivilDateTime(2024, 7, 1, 14)
    assert tai_seconds_to_local_time(winter, 3600, False) \
        == CivilDateTime(2024, 1, 15, 13)
    # the switch is at 01:00 UTC
    on = utc_to_tai_seconds(CivilDateTime(2024, 3, 31, 1))
    off = utc_to_tai_seconds(CivilDateTime(2024, 10, 27, 1))
    assert not tai_second_is_dls_in_europe(on - 1)
    assert tai_second_is_dls_in_europe(on)
    assert tai_second_is_dls_in_europe(off - 1)
    assert not tai_second_is_dls_in_europe(off)
    assert not tai_second_is_dls_in_europe(
        utc_to_tai_seconds(CivilDateTime(1979, 7, 1)))
    assert not tai_second_is_dls_in_europe(I64_MIN)
    assert not tai_second_is_dls_in_europe(I64_MAX)
    with pytest.raises(BovnarArgumentError):
        tai_seconds_to_local_time(t, 30, False)       # not whole minutes
    with pytest.raises(BovnarArgumentError):
        tai_seconds_to_local_time(I64_MAX, 3600, False)
    with pytest.raises(BovnarArgumentError):
        tai_seconds_to_local_time(t, 3600, 1)         # not a bool


# ── the datetime family ────────────────────────────────────────────────────

LITERALS = ['2016-12-31T23:59:60Z', '2017-01-01T00:59:60+01:00',
            '2024-03-31T12:00:00+02:00', '1900-01-01T00:00:00Z',
            '1969-12-31T23:59:59Z', '2000-02-29T12:34:56Z']


@pytest.mark.parametrize('epoch', ['unix', 'tai', 'mjd', 'ntp', 'y2000'])
def test_utc_conversion_agrees_with_the_reader(epoch):
    for lit in LITERALS:
        q = bovnar.loads(f'#!bovnar 1.1\n.t = <datetime:64,{epoch}> {lit};',
                         typed=True)['t']
        civil = q.to_civil()
        assert civil == epoch_seconds_to_utc(q.value, epoch)
        assert utc_to_epoch_seconds(civil, epoch) == q.value
        if lit.endswith('Z') and (epoch == 'tai' or ':60' not in lit):
            assert str(civil) + 'Z' == lit


def test_to_civil():
    q = bovnar.loads('#!bovnar 1.1\n.t = <datetime:64,tai> 2016-12-31T23:59:60.25Z;',
                     typed=True)['t']
    assert str(q.to_civil()) == '2016-12-31T23:59:60'
    assert q.datetime_fraction == '25'
    q = bovnar.loads('#!bovnar 1.1\n.t = <datetime> 1750000000;', typed=True)['t']
    assert q.to_civil() == CivilDateTime(2025, 6, 15, 15, 6, 40)
    for doc in ['#!bovnar 1.1\n.t = <datetime:64,gps> 5;', '.t = <uint:8> 5;',
                '#!bovnar 1.1\n.t = <datetime:64,unix> null;']:
        q = bovnar.loads(doc, typed=True)['t']
        with pytest.raises(BovnarArgumentError):
            q.to_civil()
    for e in ('gps', 'galileo', 'glonass', 'beidou'):
        with pytest.raises(BovnarArgumentError):
            epoch_seconds_to_utc(0, e)
        with pytest.raises(BovnarArgumentError):
            utc_to_epoch_seconds(CivilDateTime(2000, 1, 1), e)
