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

"""Civil time and calendars: the C library's ``bvn_datetime.h`` and
``bvn_gregorian_date.h``.

Epoch seconds to and from a civil date and time, between epochs, from GPS and
Galileo week + time of week, UTC to and from TAI with the leap-second table,
local time at a fixed offset; the proleptic Gregorian calendar on Modified
Julian Days, Easter and the feasts that hang off it, the German federal
holidays, banking and business days and the summer-time switch days.

Every result is computed by the C library; nothing here re-derives a date. A
:class:`GregorianDate` and a :class:`CivilDateTime` are always real ones (the
constructors refuse 30 February and hour 24), and an input or a result outside
what the C functions support raises :class:`BovnarArgumentError` where C would
return a sentinel, false, or leave its output untouched.
"""

import ctypes
import datetime as _datetime
import enum
import operator
from dataclasses import dataclass, field

from .exceptions import BovnarArgumentError

_INT64_MIN = -(1 << 63)
_INT64_MAX = (1 << 63) - 1
_OVF = _INT64_MIN                      # BVN_GDATE_OVF

#: The supported Modified Julian Day range, ``BVN_GDATE_MJD_MIN`` ..
#: ``BVN_GDATE_MJD_MAX``: 1 January of :data:`YEAR_MIN` .. 31 December of
#: :data:`YEAR_MAX`.
MJD_MIN = _INT64_MIN + 254
MJD_MAX = _INT64_MAX - 678881 - 268
#: The supported proleptic Gregorian years (astronomical numbering: 1 BC is
#: year 0), ``BVN_GDATE_YEAR_MIN`` .. ``BVN_GDATE_YEAR_MAX``.
YEAR_MIN = -25252734927764695
YEAR_MAX = 25252734927766553


def _lib():
    from ._ffi import get_library
    return get_library()


def _int(name, value, lo=_INT64_MIN, hi=_INT64_MAX) -> int:
    """``value`` as an int in [lo, hi]; ctypes would silently wrap one past
    int64, so the range is checked here."""
    if isinstance(value, bool):
        raise BovnarArgumentError(f"{name} must be an int, not bool")
    try:
        value = operator.index(value)
    except TypeError:
        raise BovnarArgumentError(
            f"{name} must be an int, not {type(value).__name__}") from None
    if not lo <= value <= hi:
        raise BovnarArgumentError(f"{name} {value} is outside {lo}..{hi}")
    return value


class Epoch(enum.IntEnum):
    """``bvn_epoch_t``: the origin of a uniform seconds count (86400 s per
    day). Each value is the epoch's Modified Julian Day, as a datetime
    :class:`~bovnar.Quantity`'s ``epoch_mjd`` gives it, so ``Epoch(q.epoch_mjd)``
    is the epoch of ``q``. The member names are the library's epoch names in
    upper case (``"y2000"`` is ``Y2000``, C's ``bvn_epoch_2000``)."""
    MJD = 0             # 1858-11-17
    NTP = 15020         # 1900-01-01
    TAI = 36204         # 1958-01-01
    UNIX = 40587        # 1970-01-01
    GPS = 44244         # 1980-01-06
    GLONASS = 50083     # 1996-01-01
    GALILEO = 51412     # 1999-08-22
    Y2000 = 51544       # 2000-01-01
    BEIDOU = 53736      # 2006-01-01

    @classmethod
    def from_name(cls, name: str) -> 'Epoch | None':
        """The epoch the library calls ``name`` (``"unix"``, ``"tai"``, ...;
        ``""`` is unix), or ``None`` for a name it does not know. Resolved by
        the library (``bvnr_datetime_epoch_index`` and
        ``bvnr_datetime_epoch_mjd``)."""
        from . import datetime_epoch_index
        from .enums import ValueTypeFamily
        from .structs import make_type_spec
        index = datetime_epoch_index(name)
        if index is None:
            return None
        vt = make_type_spec(ValueTypeFamily.DATETIME, 64, index)
        return cls(int(_lib().bvnr_datetime_epoch_mjd(vt)))


_GNSS = frozenset({Epoch.GPS, Epoch.GALILEO, Epoch.GLONASS, Epoch.BEIDOU})


def _epoch(epoch) -> Epoch:
    if isinstance(epoch, Epoch):
        return epoch
    if isinstance(epoch, str):
        e = Epoch.from_name(epoch)
        if e is None:
            raise BovnarArgumentError(f"unknown epoch name {epoch!r}")
        return e
    if isinstance(epoch, int) and not isinstance(epoch, bool):
        try:
            return Epoch(epoch)
        except ValueError:
            pass
    raise BovnarArgumentError(
        f"an epoch is an Epoch, its MJD or its name, not {epoch!r}")


def _iso_year(year: int) -> str:
    return ('-' if year < 0 else '') + '%04d' % abs(year)


@dataclass(frozen=True, order=True)
class GregorianDate:
    """A date of the proleptic Gregorian calendar (``bvn_gregorian_date_t``).

    ``GregorianDate(year, month, day)`` takes a real date and raises
    :class:`BovnarArgumentError` for anything else (``2023-02-29``, month 13,
    a year outside :data:`YEAR_MIN` .. :data:`YEAR_MAX`);
    :meth:`normalize` carries such fields into a real date instead. The year
    is astronomical: 1 BC is year 0.

    ``leap`` (the year is a leap year), ``wday`` (day of the week, 0 = Sunday
    .. 6 = Saturday) and ``yday`` (day of the year, 0 = 1 January) are
    computed by the library. Two dates are equal when they name the same day
    (``==`` is ``bvn_gregorian_date_is_equal``), and order chronologically.
    """
    year: int
    month: int
    day: int
    leap: bool = field(init=False, repr=False)
    wday: int = field(init=False, repr=False)
    yday: int = field(init=False, repr=False)

    def __post_init__(self):
        y = _int('year', self.year)
        m = _int('month', self.month)
        d = _int('day', self.day)
        g = _normalized(y, m, d)
        if (g.year, g.month, g.day) != (y, m, d):
            raise BovnarArgumentError(
                f"{_iso_year(y)}-{m:02d}-{d:02d} is not a date (it normalizes "
                f"to {_iso_year(g.year)}-{g.month:02d}-{g.day:02d}; "
                f"GregorianDate.normalize() carries the fields)")
        _fill(self, g)

    @classmethod
    def normalize(cls, year: int, month: int, day: int) -> 'GregorianDate':
        """The date ``year``-``month``-``day`` names once an out-of-range
        month or day is carried into the year and month, mktime-style
        (``bvn_gregorian_date_normalize``): ``(2024, 2, 30)`` is 1 March 2024,
        ``(2024, 13, 1)`` 1 January 2025, ``(2024, 3, 0)`` 29 February 2024.
        Any int64 fields are taken; a result outside the supported range
        raises."""
        return _wrap_date(_normalized(_int('year', year), _int('month', month),
                                      _int('day', day)))

    @classmethod
    def from_mjd(cls, mjd: int) -> 'GregorianDate':
        """The date of Modified Julian Day ``mjd``
        (``bvn_gregorian_date_from_mjd``); MJD 0 is 1858-11-17. Raises for
        an ``mjd`` outside :data:`MJD_MIN` .. :data:`MJD_MAX`."""
        mjd = _int('mjd', mjd, MJD_MIN, MJD_MAX)
        g = _blank()
        _lib().bvn_gregorian_date_from_mjd(ctypes.byref(g), mjd)
        return _wrap_date(g)

    def to_mjd(self) -> int:
        """This date's Modified Julian Day (``bvn_gregorian_date_to_mjd``)."""
        mjd = _lib().bvn_gregorian_date_to_mjd(ctypes.byref(self._c()))
        if mjd == _OVF:                       # not reachable for a real date
            raise BovnarArgumentError(f"{self} has no MJD")
        return int(mjd)

    def to_date(self) -> _datetime.date:
        """This date as a :class:`datetime.date`; raises
        :class:`BovnarArgumentError` for a year outside 1..9999, which
        ``datetime`` cannot hold."""
        if not 1 <= self.year <= 9999:
            raise BovnarArgumentError(
                f"datetime.date cannot hold the year {self.year}")
        return _datetime.date(self.year, self.month, self.day)

    def __eq__(self, other):
        if not isinstance(other, GregorianDate):
            return NotImplemented
        return bool(_lib().bvn_gregorian_date_is_equal(
            ctypes.byref(self._c()), ctypes.byref(other._c())))

    def __str__(self) -> str:
        return f"{_iso_year(self.year)}-{self.month:02d}-{self.day:02d}"

    def _c(self):
        from .structs import BvnGregorianDate
        return BvnGregorianDate(self.year, self.month, self.day,
                                int(self.leap), self.wday, self.yday)

    # ── German calendar (bvn_gregorian_date.h's higher-level functions) ──

    def is_federal_holiday_in_germany(self) -> bool:
        """Whether this is one of the nine German federal holidays, by the
        rules in force that year: Neujahr, Karfreitag, Ostermontag, 1 May
        (from 1933), Christi Himmelfahrt, Pfingstmontag, 17 June (1954..1989),
        3 October (from 1990), 25 and 26 December. Regional holidays are out
        of scope (``bvn_gregorian_date_is_federal_holiday_in_germany``)."""
        return self._pred('is_federal_holiday_in_germany')

    def is_banking_day_in_germany(self) -> bool:
        """Monday to Friday, not a federal holiday, and not 24 or 31
        December (``bvn_gregorian_date_is_banking_day_in_germany``)."""
        return self._pred('is_banking_day_in_germany')

    def is_business_day_in_germany(self) -> bool:
        """Monday to Saturday and not a federal holiday; 24 and 31 December
        are business days (``bvn_gregorian_date_is_business_day_in_germany``)."""
        return self._pred('is_business_day_in_germany')

    def last_banking_day_in_month(self) -> 'GregorianDate':
        """The last banking day of this date's month."""
        return self._get('get_last_banking_day_in_month')

    def penultimate_banking_day_in_month(self) -> 'GregorianDate':
        """The banking day before the last one of this date's month."""
        return self._get('get_penultimate_banking_day_in_month')

    def mid_banking_day_in_month(self) -> 'GregorianDate':
        """The first banking day on or after the 15th of this date's month."""
        return self._get('get_mid_banking_day_in_month')

    def first_banking_day_in_month(self) -> 'GregorianDate':
        """The first banking day of this date's month."""
        return self._get('get_first_banking_day_in_month')

    def last_sunday_in_month(self) -> 'GregorianDate':
        """The last Sunday of this date's month."""
        return self._get('get_last_sunday_in_month')

    def last_saturday_in_month(self) -> 'GregorianDate':
        """The last Saturday of this date's month."""
        return self._get('get_last_saturday_in_month')

    def last_banking_day_in_quarter(self, quarter: int) -> 'GregorianDate':
        """The last banking day of quarter ``quarter`` (1..4) of this date's
        year."""
        return self._get('get_last_banking_day_in_quarter', _quarter(quarter))

    def first_banking_day_in_quarter(self, quarter: int) -> 'GregorianDate':
        """The first banking day of quarter ``quarter`` (1..4) of this date's
        year."""
        return self._get('get_first_banking_day_in_quarter', _quarter(quarter))

    def is_last_banking_day_in_month(self) -> bool:
        return self._pred('is_last_banking_day_in_month')

    def is_penultimate_banking_day_in_month(self) -> bool:
        return self._pred('is_penultimate_banking_day_in_month')

    def is_mid_banking_day_in_month(self) -> bool:
        return self._pred('is_mid_banking_day_in_month')

    def is_first_banking_day_in_month(self) -> bool:
        return self._pred('is_first_banking_day_in_month')

    def is_last_banking_day_in_quarter(self, quarter: int) -> bool:
        """Whether this is the last banking day of quarter ``quarter`` (1..4)
        of its year."""
        return self._pred('is_last_banking_day_in_quarter', _quarter(quarter))

    def is_first_banking_day_in_quarter(self, quarter: int) -> bool:
        """Whether this is the first banking day of quarter ``quarter``
        (1..4) of its year."""
        return self._pred('is_first_banking_day_in_quarter', _quarter(quarter))

    def is_daylight_saving_switch_in_germany(self) -> bool:
        """Whether German summer time starts or ends on this day, by the rule
        in force that year: none before 1980; 1980 from 6 April to the last
        Sunday of September; 1981..1995 the last Sundays of March and
        September; from 1996 the last Sundays of March and October."""
        return self._pred('is_daylight_saving_switch_in_germany')

    def is_day_before_daylight_saving_switch_in_germany(self) -> bool:
        """Whether German summer time starts or ends on the following day."""
        return self._pred('is_day_before_daylight_saving_switch_in_germany')

    def _pred(self, name, *quarter) -> bool:
        fn = getattr(_lib(), 'bvn_gregorian_date_' + name)
        return bool(fn(*quarter, ctypes.byref(self._c())))

    def _get(self, name, *quarter) -> 'GregorianDate':
        res = _blank()
        getattr(_lib(), 'bvn_gregorian_date_' + name)(
            *quarter, ctypes.byref(res), ctypes.byref(self._c()))
        if res.day == 0:      # left untouched: the walk ran off the range
            raise BovnarArgumentError(
                f"{name[4:]} of {self}: outside the supported date range")
        return _wrap_date(res)


def _c_date(year, month, day):
    from .structs import BvnGregorianDate
    return BvnGregorianDate(year, month, day, 0, 0, 0)


def _blank():
    # day 0 never comes out of the library, so it marks "left untouched".
    return _c_date(0, 0, 0)


def _normalized(year, month, day):
    g = _c_date(year, month, day)
    if not _lib().bvn_gregorian_date_normalize(ctypes.byref(g)):
        raise BovnarArgumentError(
            f"year {year}, month {month}, day {day} is outside the supported "
            f"date range")
    return g


def _fill(obj, g):
    for name, value in (('year', int(g.year)), ('month', int(g.month)),
                        ('day', int(g.day)), ('leap', bool(g.leap)),
                        ('wday', int(g.wday)), ('yday', int(g.yday))):
        object.__setattr__(obj, name, value)


def _wrap_date(g) -> GregorianDate:
    """A GregorianDate from a struct the library filled (already canonical)."""
    obj = object.__new__(GregorianDate)
    _fill(obj, g)
    return obj


def _quarter(quarter) -> int:
    return _int('quarter', quarter, 1, 4)


@dataclass(frozen=True, order=True)
class CivilDateTime:
    """A civil date and time of day to the second (``bvn_datetime_t``).

    The fields are checked: a real date, ``hour`` 0..23, ``minute`` 0..59
    and ``second`` 0..60. ``second == 60`` is an inserted leap second: the
    TAI renderers (:func:`tai_seconds_to_utc`, :func:`tai_seconds_to_local_time`)
    produce it and :func:`utc_to_tai_seconds` takes it; the uniform-scale
    :func:`datetime_to_epoch_seconds` folds it onto the next minute.

    ``==`` compares the date and the time of day (``bvn_dt_is_equal``);
    :meth:`is_date_equal` and :meth:`is_time_equal` compare one of the two.
    Order is chronological. The C struct's ``mjd`` cache is :attr:`mjd`; its
    ``submjd`` (the time of day in 1e-5 days) is not carried.
    """
    year: int
    month: int
    day: int
    hour: int = 0
    minute: int = 0
    second: int = 0
    date: GregorianDate = field(init=False, repr=False, compare=False)

    def __post_init__(self):
        date = GregorianDate(self.year, self.month, self.day)
        for name, value in (('year', date.year), ('month', date.month),
                            ('day', date.day),
                            ('hour', _int('hour', self.hour, 0, 23)),
                            ('minute', _int('minute', self.minute, 0, 59)),
                            ('second', _int('second', self.second, 0, 60))):
            object.__setattr__(self, name, value)
        object.__setattr__(self, 'date', date)

    @property
    def mjd(self) -> int:
        """The Modified Julian Day of the date."""
        return self.date.to_mjd()

    def __eq__(self, other):
        return self._cmp('is_equal', other)

    def is_date_equal(self, other: 'CivilDateTime') -> bool:
        """Whether ``other`` falls on the same date
        (``bvn_dt_is_date_equal``)."""
        return self._cmp('is_date_equal', _civil(other))

    def is_time_equal(self, other: 'CivilDateTime') -> bool:
        """Whether ``other`` has the same time of day, whatever its date
        (``bvn_dt_is_time_equal``)."""
        return self._cmp('is_time_equal', _civil(other))

    def _cmp(self, name, other):
        if not isinstance(other, CivilDateTime):
            return NotImplemented
        return bool(getattr(_lib(), 'bvn_dt_' + name)(
            ctypes.byref(self._c()), ctypes.byref(other._c())))

    def to_datetime(self) -> _datetime.datetime:
        """This time as a naive :class:`datetime.datetime`; raises for a leap
        second (``second == 60``) and a year outside 1..9999, which
        ``datetime`` cannot hold."""
        if self.second == 60:
            raise BovnarArgumentError(
                f"datetime.datetime cannot hold the leap second {self}")
        d = self.date.to_date()
        return _datetime.datetime(d.year, d.month, d.day,
                                  self.hour, self.minute, self.second)

    def __str__(self) -> str:
        return (f"{self.date}T{self.hour:02d}:{self.minute:02d}:"
                f"{self.second:02d}")

    def _c(self):
        from .structs import BvnDatetime
        return BvnDatetime(self.date._c(), self.hour, self.minute, self.second,
                           0, 0)


def _wrap_dt(s) -> CivilDateTime:
    obj = object.__new__(CivilDateTime)
    date = _wrap_date(s.date)
    for name, value in (('year', date.year), ('month', date.month),
                        ('day', date.day), ('hour', int(s.hour)),
                        ('minute', int(s.minute)), ('second', int(s.second)),
                        ('date', date)):
        object.__setattr__(obj, name, value)
    return obj


def _blank_dt():
    from .structs import BvnDatetime
    return BvnDatetime(_blank(), 0, 0, 0, 0, 0)


def _civil(dt) -> CivilDateTime:
    if not isinstance(dt, CivilDateTime):
        raise BovnarArgumentError(
            f"expected a CivilDateTime, not {type(dt).__name__}")
    return dt


def _seconds(result, what) -> int:
    if result == _OVF:                    # BVN_GDATE_OVF
        raise BovnarArgumentError(f"{what}: the result does not fit in int64")
    return int(result)


# ── bvn_datetime.h ─────────────────────────────────────────────────────────

def epoch_seconds_to_datetime(seconds: int, epoch) -> CivilDateTime:
    """The civil date and time ``seconds`` uniform seconds (86400 per day, no
    leap seconds) after ``epoch`` (``bvn_dt_epoch_seconds_to_datetime``).
    Negative seconds are instants before the epoch. ``epoch`` is an
    :class:`Epoch`, its MJD or its name. Every int64 is accepted.

    On :data:`Epoch.TAI` this is the uniform TAI calendar, not UTC: use
    :func:`tai_seconds_to_utc` for that, or :func:`epoch_seconds_to_utc`,
    which picks the right one for the epoch."""
    s = _blank_dt()
    _lib().bvn_dt_epoch_seconds_to_datetime(
        ctypes.byref(s), int(_epoch(epoch)), _int('seconds', seconds))
    return _wrap_dt(s)


def datetime_to_epoch_seconds(dt: CivilDateTime, epoch) -> int:
    """Uniform seconds from ``epoch`` to ``dt`` (86400 per day, no leap
    seconds; ``bvn_dt_datetime_to_epoch_seconds``). ``second == 60`` counts
    as the first second of the next minute. Raises when the result does not
    fit in int64."""
    s = _civil(dt)._c()
    return _seconds(_lib().bvn_dt_datetime_to_epoch_seconds(
        ctypes.byref(s), int(_epoch(epoch))), str(dt))


def convert_epoch_seconds(seconds: int, epoch_from, epoch_to) -> int:
    """``seconds`` counted from ``epoch_from`` relabelled to count from
    ``epoch_to``: the same uniform instant, a whole number of days shifted
    (``bvn_dt_convert_epoch_seconds``). It changes the origin only, never the
    time scale: GPS seconds stay GPS time. Raises when the result does not
    fit in int64."""
    return _seconds(_lib().bvn_dt_convert_epoch_seconds(
        int(_epoch(epoch_from)), int(_epoch(epoch_to)),
        _int('seconds', seconds)), f"{seconds} s")


def epoch_seconds_from_gps_time(time_of_week_ms: int, week: int, epoch) -> int:
    """GPS ``week`` and time of week in milliseconds (rounded to the nearest
    second) as seconds since ``epoch`` (``bvn_dt_epoch_seconds_from_gps_time``).
    The result stays in GPS time, which carries no leap seconds, except on
    :data:`Epoch.TAI`, where the constant 19 s is added and the result is
    TAI. For civil UTC take the TAI seconds (:func:`tai_seconds_from_gps_time`)
    to :func:`tai_seconds_to_utc`."""
    return _seconds(_lib().bvn_dt_epoch_seconds_from_gps_time(
        _int('time_of_week_ms', time_of_week_ms), _int('week', week),
        int(_epoch(epoch))), "the GPS time")


def tai_seconds_from_gps_time(time_of_week_ms: int, week: int) -> int:
    """GPS week + time of week as TAI seconds
    (``bvn_dt_tai_seconds_from_gps_time``)."""
    return _seconds(_lib().bvn_dt_tai_seconds_from_gps_time(
        _int('time_of_week_ms', time_of_week_ms), _int('week', week)),
        "the GPS time")


def epoch_seconds_from_galileo_time(time_of_week_ms: int, week: int,
                                    epoch) -> int:
    """:func:`epoch_seconds_from_gps_time` for Galileo System Time, whose
    weeks count from 1999-08-22
    (``bvn_dt_epoch_seconds_from_galileo_time``)."""
    return _seconds(_lib().bvn_dt_epoch_seconds_from_galileo_time(
        _int('time_of_week_ms', time_of_week_ms), _int('week', week),
        int(_epoch(epoch))), "the Galileo time")


def tai_seconds_from_galileo_time(time_of_week_ms: int, week: int) -> int:
    """Galileo week + time of week as TAI seconds
    (``bvn_dt_tai_seconds_from_galileo_time``)."""
    return _seconds(_lib().bvn_dt_tai_seconds_from_galileo_time(
        _int('time_of_week_ms', time_of_week_ms), _int('week', week)),
        "the Galileo time")


def utc_to_tai_seconds(dt: CivilDateTime) -> int:
    """The UTC time ``dt`` as TAI seconds (since 1958-01-01), with the IERS
    leap-second table (``bvn_dt_utc_to_tai_seconds``). ``23:59:60`` on a day
    the table records an inserted leap second is that second; anywhere else
    it is the following second. Before 1972 the offset is the initial 10 s.
    Raises when the result does not fit in int64."""
    s = _civil(dt)._c()
    return _seconds(_lib().bvn_dt_utc_to_tai_seconds(ctypes.byref(s)), str(dt))


def tai_seconds_to_utc(tai_seconds: int) -> CivilDateTime:
    """The UTC time of TAI second ``tai_seconds`` (``bvn_dt_tai_seconds_to_utc``),
    the inverse of :func:`utc_to_tai_seconds`: an inserted leap second is
    ``23:59:60`` of the day before the step. Raises within 37 s of the int64
    minimum, where the offset does not fit."""
    s = _blank_dt()
    _lib().bvn_dt_tai_seconds_to_utc(ctypes.byref(s),
                                     _int('tai_seconds', tai_seconds))
    if s.date.day == 0:
        raise BovnarArgumentError(f"TAI {tai_seconds} has no UTC time in int64")
    return _wrap_dt(s)


def tai_seconds_to_local_time(tai_seconds: int, utc_offset_seconds: int,
                              dls_active: bool) -> CivilDateTime:
    """The wall-clock time of TAI second ``tai_seconds`` in a zone
    ``utc_offset_seconds`` east of UTC, one hour later when ``dls_active``
    (``bvn_dt_tai_seconds_to_local_time``). Nothing is read from the host:
    the offset and the summer-time flag are the caller's, e.g.
    ``tai_seconds_to_local_time(t, 3600, tai_second_is_dls_in_europe(t))``
    for Germany. An inserted leap second keeps its ``:60``. The offset must
    be a whole number of minutes, as a zone offset is: with any other the
    library would spell a leap second at a second that is not one. Raises
    when the shifted instant does not fit in int64."""
    offset = _int('utc_offset_seconds', utc_offset_seconds)
    if offset % 60:
        raise BovnarArgumentError(
            f"utc_offset_seconds {offset} is not a whole number of minutes")
    if not isinstance(dls_active, bool):
        raise BovnarArgumentError("dls_active must be a bool")
    s = _blank_dt()
    _lib().bvn_dt_tai_seconds_to_local_time(
        ctypes.byref(s), _int('tai_seconds', tai_seconds), offset,
        int(dls_active))
    if s.date.day == 0:
        raise BovnarArgumentError(
            f"TAI {tai_seconds} at offset {offset} s has no local time in int64")
    return _wrap_dt(s)


def tai_second_is_dls_in_europe(tai_second: int) -> bool:
    """Whether German/European summer time is in force at TAI second
    ``tai_second``, by the rule of that year (none before 1980; see
    :meth:`GregorianDate.is_daylight_saving_switch_in_germany`); the switch
    is at 01:00 UTC (``bvn_dt_tai_second_is_dls_in_europe``)."""
    return bool(_lib().bvn_dt_tai_second_is_dls_in_europe(
        _int('tai_second', tai_second)))


def epoch_seconds_to_utc(seconds: int, epoch) -> CivilDateTime:
    """The UTC time of a datetime value: ``seconds`` since ``epoch``, read as
    the library reads and writes ISO literals. On :data:`Epoch.TAI` the
    leap-second table applies (:func:`tai_seconds_to_utc`); on the civil
    epochs (unix, mjd, ntp, y2000) the count is uniform
    (:func:`epoch_seconds_to_datetime`). The GNSS epochs (gps, galileo,
    glonass, beidou) raise, as the library has no civil form for them; GPS
    and Galileo week + time of week go through
    :func:`tai_seconds_from_gps_time` instead."""
    e = _epoch(epoch)
    if e in _GNSS:
        raise BovnarArgumentError(
            f"the {e.name.lower()} epoch counts atomic time and has no civil "
            f"UTC form in this library")
    if e is Epoch.TAI:
        return tai_seconds_to_utc(seconds)
    return epoch_seconds_to_datetime(seconds, e)


def utc_to_epoch_seconds(dt: CivilDateTime, epoch) -> int:
    """The inverse of :func:`epoch_seconds_to_utc`: the UTC time ``dt`` as
    seconds since ``epoch``, the conversion the reader applies to an ISO
    literal (:func:`utc_to_tai_seconds` on TAI,
    :func:`datetime_to_epoch_seconds` on a civil epoch). The GNSS epochs
    raise."""
    e = _epoch(epoch)
    if e in _GNSS:
        raise BovnarArgumentError(
            f"the {e.name.lower()} epoch counts atomic time and has no civil "
            f"UTC form in this library")
    if e is Epoch.TAI:
        return utc_to_tai_seconds(dt)
    return datetime_to_epoch_seconds(dt, e)


# ── bvn_gregorian_date.h ───────────────────────────────────────────────────

def mjd_to_wday(mjd: int) -> int:
    """The day of the week of Modified Julian Day ``mjd``, 0 = Sunday ..
    6 = Saturday, for any int64 (``bvn_gregorian_date_mjd_to_wday``)."""
    return int(_lib().bvn_gregorian_date_mjd_to_wday(_int('mjd', mjd)))


def mjd_in_leap_year(mjd: int) -> bool:
    """Whether the year holding Modified Julian Day ``mjd`` is a leap year
    (``bvn_gregorian_date_mjd_in_leap_year``). Raises for an ``mjd`` outside
    :data:`MJD_MIN` .. :data:`MJD_MAX`, where C answers ``False``."""
    return bool(_lib().bvn_gregorian_date_mjd_in_leap_year(
        _int('mjd', mjd, MJD_MIN, MJD_MAX)))


def mjd_to_yday(mjd: int) -> int:
    """The day of the year of Modified Julian Day ``mjd``, 0 = 1 January
    (``bvn_gregorian_date_mjd_to_yday``). Raises for an ``mjd`` outside
    :data:`MJD_MIN` .. :data:`MJD_MAX`, where C answers -1."""
    return int(_lib().bvn_gregorian_date_mjd_to_yday(
        _int('mjd', mjd, MJD_MIN, MJD_MAX)))


def easter_sunday_mjd(year: int) -> int:
    """The Modified Julian Day of (Gregorian, western) Easter Sunday of
    ``year`` (``bvn_gregorian_date_easter_sunday_mjd``). Raises for a year
    outside :data:`YEAR_MIN` .. :data:`YEAR_MAX`."""
    return _seconds(_lib().bvn_gregorian_date_easter_sunday_mjd(
        _int('year', year, YEAR_MIN, YEAR_MAX)), f"Easter of {year}")


def _feast(name, year) -> GregorianDate:
    g = _blank()
    if not getattr(_lib(), 'bvn_gregorian_date_calc_' + name)(
            ctypes.byref(g), _int('year', year, YEAR_MIN, YEAR_MAX)):
        raise BovnarArgumentError(
            f"{name} of {year} is outside the supported date range")
    return _wrap_date(g)


def easter_sunday(year: int) -> GregorianDate:
    """Easter Sunday of ``year``
    (``bvn_gregorian_date_calc_easter_sunday``)."""
    return _feast('easter_sunday', year)


def good_friday(year: int) -> GregorianDate:
    """Good Friday, two days before Easter Sunday
    (``bvn_gregorian_date_calc_good_friday``)."""
    return _feast('good_friday', year)


def easter_monday(year: int) -> GregorianDate:
    """Easter Monday (``bvn_gregorian_date_calc_easter_monday``)."""
    return _feast('easter_monday', year)


def ascension_day(year: int) -> GregorianDate:
    """Ascension Day, 39 days after Easter Sunday
    (``bvn_gregorian_date_calc_ascension_day``)."""
    return _feast('ascension_day', year)


def whit_monday(year: int) -> GregorianDate:
    """Whit Monday, 50 days after Easter Sunday
    (``bvn_gregorian_date_calc_whit_monday``)."""
    return _feast('whit_monday', year)


__all__ = [
    'Epoch', 'GregorianDate', 'CivilDateTime',
    'MJD_MIN', 'MJD_MAX', 'YEAR_MIN', 'YEAR_MAX',
    'epoch_seconds_to_datetime', 'datetime_to_epoch_seconds',
    'convert_epoch_seconds',
    'epoch_seconds_from_gps_time', 'tai_seconds_from_gps_time',
    'epoch_seconds_from_galileo_time', 'tai_seconds_from_galileo_time',
    'utc_to_tai_seconds', 'tai_seconds_to_utc', 'tai_seconds_to_local_time',
    'tai_second_is_dls_in_europe',
    'epoch_seconds_to_utc', 'utc_to_epoch_seconds',
    'mjd_to_wday', 'mjd_in_leap_year', 'mjd_to_yday',
    'easter_sunday_mjd', 'easter_sunday', 'good_friday', 'easter_monday',
    'ascension_day', 'whit_monday',
]
