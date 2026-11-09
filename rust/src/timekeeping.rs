// SPDX-License-Identifier: MIT
//! Civil time and calendars: the C library's `bvn_datetime.h` and
//! `bvn_gregorian_date.h`.
//!
//! Epoch seconds to and from a civil date and time, between epochs, from GPS
//! and Galileo week + time of week, UTC to and from TAI with the leap-second
//! table, local time at a fixed offset; the proleptic Gregorian calendar on
//! Modified Julian Days, Easter and the feasts that hang off it, the German
//! federal holidays, banking and business days and the summer-time switch
//! days.
//!
//! Every result is computed by the C library; nothing here re-derives a date.
//! A [`GregorianDate`] and a [`CivilDateTime`] are always real ones (the
//! constructors refuse 30 February and hour 24), and an input or a result
//! outside what the C functions support is an [`Error::Argument`] where C
//! would return a sentinel, false, or leave its output untouched. Nothing
//! reads the host's time zone, so every function gives the same answer on
//! every target, `wasm32-wasip2` included.

use std::cmp::Ordering;
use std::ffi::c_int;
use std::fmt;
use std::hash::{Hash, Hasher};

use crate::enums::ValueTypeFamily;
use crate::error::{Error, Result};
use crate::ffi::timekeeping_fns::{self as c, BvnDatetime, BvnGregorianDate, GDATE_OVF};
use crate::ffi::{self, ValueTypeSpec};

/// The first supported Modified Julian Day, `BVN_GDATE_MJD_MIN`: 1 January
/// of [`YEAR_MIN`].
pub const MJD_MIN: i64 = i64::MIN + 254;
/// The last supported Modified Julian Day, `BVN_GDATE_MJD_MAX`: 31 December
/// of [`YEAR_MAX`].
pub const MJD_MAX: i64 = i64::MAX - 678_881 - 268;
/// The first supported proleptic Gregorian year, `BVN_GDATE_YEAR_MIN`
/// (astronomical numbering: 1 BC is year 0).
pub const YEAR_MIN: i64 = -25_252_734_927_764_695;
/// The last supported proleptic Gregorian year, `BVN_GDATE_YEAR_MAX`.
pub const YEAR_MAX: i64 = 25_252_734_927_766_553;

/// `bvn_epoch_t`: the origin of a uniform seconds count (86400 s per day).
/// Each discriminant is the epoch's Modified Julian Day, as a datetime
/// [`Quantity::epoch_mjd`](crate::Quantity::epoch_mjd) gives it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum Epoch {
    /// 1858-11-17.
    Mjd = 0,
    /// 1900-01-01.
    Ntp = 15020,
    /// 1958-01-01.
    Tai = 36204,
    /// 1970-01-01.
    Unix = 40587,
    /// 1980-01-06.
    Gps = 44244,
    /// 1996-01-01.
    Glonass = 50083,
    /// 1999-08-22.
    Galileo = 51412,
    /// 2000-01-01 (C's `bvn_epoch_2000`, the library's `"y2000"`).
    Y2000 = 51544,
    /// 2006-01-01.
    Beidou = 53736,
}

impl Epoch {
    /// Every epoch, in the order of `bvn_epoch_t`.
    pub const ALL: [Epoch; 9] = [
        Epoch::Mjd,
        Epoch::Ntp,
        Epoch::Tai,
        Epoch::Unix,
        Epoch::Gps,
        Epoch::Glonass,
        Epoch::Galileo,
        Epoch::Y2000,
        Epoch::Beidou,
    ];

    /// The epoch's Modified Julian Day.
    pub const fn mjd(self) -> i32 {
        self as i32
    }

    /// The epoch whose Modified Julian Day is `mjd`.
    pub fn from_mjd(mjd: i32) -> Option<Epoch> {
        Epoch::ALL.into_iter().find(|e| e.mjd() == mjd)
    }

    /// The epoch the library calls `name` (`"unix"`, `"tai"`, ...; `""` is
    /// unix), resolved by the library (`bvnr_datetime_epoch_index`,
    /// `bvnr_datetime_epoch_mjd`); `None` for a name it does not know.
    pub fn from_name(name: &str) -> Option<Epoch> {
        let base = ffi::datetime_epoch_index(name)?;
        let vt = ValueTypeSpec {
            family: ValueTypeFamily::Datetime as i32,
            width: 64,
            base,
        };
        Epoch::from_mjd(ffi::datetime_epoch(vt).1)
    }

    fn is_gnss(self) -> bool {
        matches!(
            self,
            Epoch::Gps | Epoch::Galileo | Epoch::Glonass | Epoch::Beidou
        )
    }

    fn c(self) -> c_int {
        self as c_int
    }
}

fn iso_year(f: &mut fmt::Formatter<'_>, year: i64) -> fmt::Result {
    let sign = if year < 0 { "-" } else { "" };
    write!(f, "{sign}{:04}", year.unsigned_abs())
}

/// A date of the proleptic Gregorian calendar (`bvn_gregorian_date_t`).
///
/// [`GregorianDate::new`] takes a real date and refuses anything else
/// (2023-02-29, month 13, a year outside [`YEAR_MIN`] ..= [`YEAR_MAX`]);
/// [`GregorianDate::normalize`] carries such fields into a real date instead.
/// The year is astronomical: 1 BC is year 0.
///
/// `==` is `bvn_gregorian_date_is_equal`; dates order chronologically.
/// `Display` writes `2024-03-31`.
#[derive(Clone, Copy, Debug)]
pub struct GregorianDate {
    year: i64,
    month: u32,
    day: u32,
    leap: bool,
    wday: u32,
    yday: u32,
}

impl GregorianDate {
    /// The date `year`-`month`-`day`; an error unless it is a real date in
    /// the supported range.
    pub fn new(year: i64, month: u32, day: u32) -> Result<GregorianDate> {
        let g = GregorianDate::normalize(year, month.into(), day.into())?;
        if (g.year, g.month, g.day) != (year, month, day) {
            return Err(Error::argument(format!(
                "{year}-{month:02}-{day:02} is not a date (it normalizes to {g}; \
                 GregorianDate::normalize carries the fields)"
            )));
        }
        Ok(g)
    }

    /// The date `year`-`month`-`day` names once an out-of-range month or day
    /// is carried into the year and month, mktime-style
    /// (`bvn_gregorian_date_normalize`): `(2024, 2, 30)` is 1 March 2024,
    /// `(2024, 13, 1)` 1 January 2025, `(2024, 3, 0)` 29 February 2024. An
    /// error when the result is outside the supported range.
    pub fn normalize(year: i64, month: i64, day: i64) -> Result<GregorianDate> {
        let raw = BvnGregorianDate {
            year,
            month,
            day,
            ..Default::default()
        };
        c::date_normalize(raw).map(wrap_date).ok_or_else(|| {
            Error::argument(format!(
                "year {year}, month {month}, day {day} is outside the supported date range"
            ))
        })
    }

    /// The date of Modified Julian Day `mjd` (`bvn_gregorian_date_from_mjd`);
    /// MJD 0 is 1858-11-17. An error outside [`MJD_MIN`] ..= [`MJD_MAX`].
    pub fn from_mjd(mjd: i64) -> Result<GregorianDate> {
        c::date_from_mjd(mjd)
            .map(wrap_date)
            .ok_or_else(|| Error::argument(format!("MJD {mjd} is outside {MJD_MIN}..={MJD_MAX}")))
    }

    /// This date's Modified Julian Day (`bvn_gregorian_date_to_mjd`).
    pub fn to_mjd(&self) -> i64 {
        let mjd = c::date_to_mjd(self.raw());
        debug_assert_ne!(mjd, GDATE_OVF, "a GregorianDate is always in range");
        mjd
    }

    /// The year (astronomical: 1 BC is 0).
    pub fn year(&self) -> i64 {
        self.year
    }

    /// The month, 1 ..= 12.
    pub fn month(&self) -> u32 {
        self.month
    }

    /// The day of the month, 1 ..= 31.
    pub fn day(&self) -> u32 {
        self.day
    }

    /// Whether the year is a leap year.
    pub fn leap(&self) -> bool {
        self.leap
    }

    /// The day of the week, 0 = Sunday ..= 6 = Saturday.
    pub fn wday(&self) -> u32 {
        self.wday
    }

    /// The day of the year, 0 = 1 January ..= 365.
    pub fn yday(&self) -> u32 {
        self.yday
    }

    fn raw(&self) -> BvnGregorianDate {
        BvnGregorianDate {
            year: self.year,
            month: self.month.into(),
            day: self.day.into(),
            leap: self.leap.into(),
            wday: self.wday as i32,
            yday: self.yday as i32,
        }
    }

    fn get(
        &self,
        what: &str,
        f: impl FnOnce(BvnGregorianDate) -> Option<BvnGregorianDate>,
    ) -> Result<GregorianDate> {
        f(self.raw()).map(wrap_date).ok_or_else(|| {
            Error::argument(format!(
                "{what} of {self}: outside the supported date range"
            ))
        })
    }

    // ── German calendar (bvn_gregorian_date.h's higher-level functions) ──

    /// Whether this is one of the nine German federal holidays, by the rules
    /// in force that year: Neujahr, Karfreitag, Ostermontag, 1 May (from
    /// 1933), Christi Himmelfahrt, Pfingstmontag, 17 June (1954 ..= 1989),
    /// 3 October (from 1990), 25 and 26 December. Regional holidays are out
    /// of scope (`bvn_gregorian_date_is_federal_holiday_in_germany`).
    pub fn is_federal_holiday_in_germany(&self) -> bool {
        c::is_federal_holiday_in_germany(self.raw())
    }

    /// Monday to Friday, not a federal holiday, and not 24 or 31 December
    /// (`bvn_gregorian_date_is_banking_day_in_germany`).
    pub fn is_banking_day_in_germany(&self) -> bool {
        c::is_banking_day_in_germany(self.raw())
    }

    /// Monday to Saturday and not a federal holiday; 24 and 31 December are
    /// business days (`bvn_gregorian_date_is_business_day_in_germany`).
    pub fn is_business_day_in_germany(&self) -> bool {
        c::is_business_day_in_germany(self.raw())
    }

    /// The last banking day of this date's month.
    pub fn last_banking_day_in_month(&self) -> Result<GregorianDate> {
        self.get("the last banking day", c::last_banking_day_in_month)
    }

    /// The banking day before the last one of this date's month.
    pub fn penultimate_banking_day_in_month(&self) -> Result<GregorianDate> {
        self.get(
            "the penultimate banking day",
            c::penultimate_banking_day_in_month,
        )
    }

    /// The first banking day on or after the 15th of this date's month.
    pub fn mid_banking_day_in_month(&self) -> Result<GregorianDate> {
        self.get("the mid banking day", c::mid_banking_day_in_month)
    }

    /// The first banking day of this date's month.
    pub fn first_banking_day_in_month(&self) -> Result<GregorianDate> {
        self.get("the first banking day", c::first_banking_day_in_month)
    }

    /// The last Sunday of this date's month.
    pub fn last_sunday_in_month(&self) -> Result<GregorianDate> {
        self.get("the last Sunday", c::last_sunday_in_month)
    }

    /// The last Saturday of this date's month.
    pub fn last_saturday_in_month(&self) -> Result<GregorianDate> {
        self.get("the last Saturday", c::last_saturday_in_month)
    }

    /// The last banking day of quarter `quarter` (1 ..= 4) of this date's
    /// year.
    pub fn last_banking_day_in_quarter(&self, quarter: u32) -> Result<GregorianDate> {
        let q = check_quarter(quarter)?;
        self.get("the last banking day of the quarter", |d| {
            c::banking_day_in_quarter(true, q, d)
        })
    }

    /// The first banking day of quarter `quarter` (1 ..= 4) of this date's
    /// year.
    pub fn first_banking_day_in_quarter(&self, quarter: u32) -> Result<GregorianDate> {
        let q = check_quarter(quarter)?;
        self.get("the first banking day of the quarter", |d| {
            c::banking_day_in_quarter(false, q, d)
        })
    }

    /// Whether this is the last banking day of its month.
    pub fn is_last_banking_day_in_month(&self) -> bool {
        c::is_last_banking_day_in_month(self.raw())
    }

    /// Whether this is the penultimate banking day of its month.
    pub fn is_penultimate_banking_day_in_month(&self) -> bool {
        c::is_penultimate_banking_day_in_month(self.raw())
    }

    /// Whether this is the mid banking day of its month.
    pub fn is_mid_banking_day_in_month(&self) -> bool {
        c::is_mid_banking_day_in_month(self.raw())
    }

    /// Whether this is the first banking day of its month.
    pub fn is_first_banking_day_in_month(&self) -> bool {
        c::is_first_banking_day_in_month(self.raw())
    }

    /// Whether this is the last banking day of quarter `quarter` (1 ..= 4)
    /// of its year; an error for another quarter.
    pub fn is_last_banking_day_in_quarter(&self, quarter: u32) -> Result<bool> {
        Ok(c::is_banking_day_in_quarter(
            true,
            check_quarter(quarter)?,
            self.raw(),
        ))
    }

    /// Whether this is the first banking day of quarter `quarter` (1 ..= 4)
    /// of its year; an error for another quarter.
    pub fn is_first_banking_day_in_quarter(&self, quarter: u32) -> Result<bool> {
        Ok(c::is_banking_day_in_quarter(
            false,
            check_quarter(quarter)?,
            self.raw(),
        ))
    }

    /// Whether German summer time starts or ends on this day, by the rule
    /// in force that year: none before 1980; 1980 from 6 April to the last
    /// Sunday of September; 1981 ..= 1995 the last Sundays of March and
    /// September; from 1996 the last Sundays of March and October.
    pub fn is_daylight_saving_switch_in_germany(&self) -> bool {
        c::is_daylight_saving_switch_in_germany(self.raw())
    }

    /// Whether German summer time starts or ends on the following day.
    pub fn is_day_before_daylight_saving_switch_in_germany(&self) -> bool {
        c::is_day_before_daylight_saving_switch_in_germany(self.raw())
    }
}

/// A date the library filled in, and therefore canonical.
fn wrap_date(g: BvnGregorianDate) -> GregorianDate {
    // The library's output fields are in range: month 1..=12, day 1..=31,
    // wday 0..=6, yday 0..=365.
    GregorianDate {
        year: g.year,
        month: g.month as u32,
        day: g.day as u32,
        leap: g.leap != 0,
        wday: g.wday as u32,
        yday: g.yday as u32,
    }
}

fn check_quarter(quarter: u32) -> Result<c_int> {
    match quarter {
        1..=4 => Ok(quarter as c_int),
        _ => Err(Error::argument(format!(
            "quarter {quarter} is outside 1..=4"
        ))),
    }
}

impl PartialEq for GregorianDate {
    fn eq(&self, other: &Self) -> bool {
        c::date_is_equal(self.raw(), other.raw())
    }
}

impl Eq for GregorianDate {}

impl Hash for GregorianDate {
    fn hash<H: Hasher>(&self, state: &mut H) {
        (self.year, self.month, self.day).hash(state);
    }
}

impl Ord for GregorianDate {
    fn cmp(&self, other: &Self) -> Ordering {
        (self.year, self.month, self.day).cmp(&(other.year, other.month, other.day))
    }
}

impl PartialOrd for GregorianDate {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for GregorianDate {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        iso_year(f, self.year)?;
        write!(f, "-{:02}-{:02}", self.month, self.day)
    }
}

/// A civil date and time of day to the second (`bvn_datetime_t`).
///
/// The fields are checked: a real date, `hour` 0 ..= 23, `minute` 0 ..= 59
/// and `second` 0 ..= 60. `second == 60` is an inserted leap second: the TAI
/// renderers ([`tai_seconds_to_utc`], [`tai_seconds_to_local_time`]) produce
/// it and [`utc_to_tai_seconds`] takes it; the uniform-scale
/// [`datetime_to_epoch_seconds`] folds it onto the next minute.
///
/// `==` compares the date and the time of day (`bvn_dt_is_equal`);
/// [`is_date_equal`](Self::is_date_equal) and
/// [`is_time_equal`](Self::is_time_equal) compare one of the two. Order is
/// chronological. The C struct's `mjd` cache is [`mjd`](Self::mjd); its
/// `submjd` (the time of day in 1e-5 days) is not carried. `Display` writes
/// `2016-12-31T23:59:60`.
#[derive(Clone, Copy, Debug)]
pub struct CivilDateTime {
    date: GregorianDate,
    hour: u32,
    minute: u32,
    second: u32,
}

impl CivilDateTime {
    /// The time `hour`:`minute`:`second` on `year`-`month`-`day`; an error
    /// unless the date is real and the time of day in range.
    pub fn new(
        year: i64,
        month: u32,
        day: u32,
        hour: u32,
        minute: u32,
        second: u32,
    ) -> Result<CivilDateTime> {
        let date = GregorianDate::new(year, month, day)?;
        for (name, value, max) in [
            ("hour", hour, 23),
            ("minute", minute, 59),
            ("second", second, 60),
        ] {
            if value > max {
                return Err(Error::argument(format!(
                    "{name} {value} is outside 0..={max}"
                )));
            }
        }
        Ok(CivilDateTime {
            date,
            hour,
            minute,
            second,
        })
    }

    /// The date.
    pub fn date(&self) -> GregorianDate {
        self.date
    }

    /// The year of the date.
    pub fn year(&self) -> i64 {
        self.date.year
    }

    /// The month of the date, 1 ..= 12.
    pub fn month(&self) -> u32 {
        self.date.month
    }

    /// The day of the month, 1 ..= 31.
    pub fn day(&self) -> u32 {
        self.date.day
    }

    /// The hour, 0 ..= 23.
    pub fn hour(&self) -> u32 {
        self.hour
    }

    /// The minute, 0 ..= 59.
    pub fn minute(&self) -> u32 {
        self.minute
    }

    /// The second, 0 ..= 60 (60 is a leap second).
    pub fn second(&self) -> u32 {
        self.second
    }

    /// The Modified Julian Day of the date.
    pub fn mjd(&self) -> i64 {
        self.date.to_mjd()
    }

    /// Whether `other` falls on the same date (`bvn_dt_is_date_equal`).
    pub fn is_date_equal(&self, other: &CivilDateTime) -> bool {
        c::dt_is_date_equal(self.raw(), other.raw())
    }

    /// Whether `other` has the same time of day, whatever its date
    /// (`bvn_dt_is_time_equal`).
    pub fn is_time_equal(&self, other: &CivilDateTime) -> bool {
        c::dt_is_time_equal(self.raw(), other.raw())
    }

    fn raw(&self) -> BvnDatetime {
        BvnDatetime {
            date: self.date.raw(),
            hour: self.hour.into(),
            minute: self.minute.into(),
            second: self.second.into(),
            ..Default::default()
        }
    }
}

/// A datetime the library filled in: hour 0..=23, minute 0..=59, second
/// 0..=60.
fn wrap_dt(dt: BvnDatetime) -> CivilDateTime {
    CivilDateTime {
        date: wrap_date(dt.date),
        hour: dt.hour as u32,
        minute: dt.minute as u32,
        second: dt.second as u32,
    }
}

impl PartialEq for CivilDateTime {
    fn eq(&self, other: &Self) -> bool {
        c::dt_is_equal(self.raw(), other.raw())
    }
}

impl Eq for CivilDateTime {}

impl Hash for CivilDateTime {
    fn hash<H: Hasher>(&self, state: &mut H) {
        (self.date, self.hour, self.minute, self.second).hash(state);
    }
}

impl Ord for CivilDateTime {
    fn cmp(&self, other: &Self) -> Ordering {
        (self.date, self.hour, self.minute, self.second).cmp(&(
            other.date,
            other.hour,
            other.minute,
            other.second,
        ))
    }
}

impl PartialOrd for CivilDateTime {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for CivilDateTime {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}T{:02}:{:02}:{:02}",
            self.date, self.hour, self.minute, self.second
        )
    }
}

fn checked(result: i64, what: impl fmt::Display) -> Result<i64> {
    if result == GDATE_OVF {
        Err(Error::argument(format!(
            "{what}: the result does not fit in int64"
        )))
    } else {
        Ok(result)
    }
}

// ── bvn_datetime.h ─────────────────────────────────────────────────────────

/// The civil date and time `seconds` uniform seconds (86400 per day, no leap
/// seconds) after `epoch` (`bvn_dt_epoch_seconds_to_datetime`). Negative
/// seconds are instants before the epoch; every `i64` is accepted.
///
/// On [`Epoch::Tai`] this is the uniform TAI calendar, not UTC: use
/// [`tai_seconds_to_utc`] for that, or [`epoch_seconds_to_utc`], which picks
/// the right one for the epoch.
pub fn epoch_seconds_to_datetime(seconds: i64, epoch: Epoch) -> CivilDateTime {
    wrap_dt(c::epoch_seconds_to_datetime(epoch.c(), seconds))
}

/// Uniform seconds from `epoch` to `dt` (86400 per day, no leap seconds;
/// `bvn_dt_datetime_to_epoch_seconds`). `second == 60` counts as the first
/// second of the next minute. An error when the result does not fit in
/// `i64`.
pub fn datetime_to_epoch_seconds(dt: &CivilDateTime, epoch: Epoch) -> Result<i64> {
    checked(c::datetime_to_epoch_seconds(dt.raw(), epoch.c()), dt)
}

/// `seconds` counted from `from` relabelled to count from `to`: the same
/// uniform instant, a whole number of days shifted
/// (`bvn_dt_convert_epoch_seconds`). It changes the origin only, never the
/// time scale: GPS seconds stay GPS time. An error when the result does not
/// fit in `i64`.
pub fn convert_epoch_seconds(seconds: i64, from: Epoch, to: Epoch) -> Result<i64> {
    checked(
        c::convert_epoch_seconds(from.c(), to.c(), seconds),
        format_args!("{seconds} s"),
    )
}

/// GPS `week` and time of week in milliseconds (rounded to the nearest
/// second) as seconds since `epoch` (`bvn_dt_epoch_seconds_from_gps_time`).
/// The result stays in GPS time, which carries no leap seconds, except on
/// [`Epoch::Tai`], where the constant 19 s is added and the result is TAI.
/// For civil UTC take the TAI seconds ([`tai_seconds_from_gps_time`]) to
/// [`tai_seconds_to_utc`].
pub fn epoch_seconds_from_gps_time(time_of_week_ms: i64, week: i64, epoch: Epoch) -> Result<i64> {
    checked(
        c::epoch_seconds_from_gps_time(time_of_week_ms, week, epoch.c()),
        "the GPS time",
    )
}

/// GPS week + time of week as TAI seconds
/// (`bvn_dt_tai_seconds_from_gps_time`).
pub fn tai_seconds_from_gps_time(time_of_week_ms: i64, week: i64) -> Result<i64> {
    checked(
        c::tai_seconds_from_gps_time(time_of_week_ms, week),
        "the GPS time",
    )
}

/// [`epoch_seconds_from_gps_time`] for Galileo System Time, whose weeks
/// count from 1999-08-22 (`bvn_dt_epoch_seconds_from_galileo_time`).
pub fn epoch_seconds_from_galileo_time(
    time_of_week_ms: i64,
    week: i64,
    epoch: Epoch,
) -> Result<i64> {
    checked(
        c::epoch_seconds_from_galileo_time(time_of_week_ms, week, epoch.c()),
        "the Galileo time",
    )
}

/// Galileo week + time of week as TAI seconds
/// (`bvn_dt_tai_seconds_from_galileo_time`).
pub fn tai_seconds_from_galileo_time(time_of_week_ms: i64, week: i64) -> Result<i64> {
    checked(
        c::tai_seconds_from_galileo_time(time_of_week_ms, week),
        "the Galileo time",
    )
}

/// The UTC time `dt` as TAI seconds (since 1958-01-01), with the IERS
/// leap-second table (`bvn_dt_utc_to_tai_seconds`). `23:59:60` on a day the
/// table records an inserted leap second is that second; anywhere else it is
/// the following second. Before 1972 the offset is the initial 10 s. An
/// error when the result does not fit in `i64`.
pub fn utc_to_tai_seconds(dt: &CivilDateTime) -> Result<i64> {
    checked(c::utc_to_tai_seconds(dt.raw()), dt)
}

/// The UTC time of TAI second `tai_seconds` (`bvn_dt_tai_seconds_to_utc`),
/// the inverse of [`utc_to_tai_seconds`]: an inserted leap second is
/// `23:59:60` of the day before the step. An error within 37 s of
/// `i64::MIN`, where the offset does not fit.
pub fn tai_seconds_to_utc(tai_seconds: i64) -> Result<CivilDateTime> {
    c::tai_seconds_to_utc(tai_seconds)
        .map(wrap_dt)
        .ok_or_else(|| Error::argument(format!("TAI {tai_seconds} has no UTC time in int64")))
}

/// The wall-clock time of TAI second `tai_seconds` in a zone
/// `utc_offset_seconds` east of UTC, one hour later when `dls_active`
/// (`bvn_dt_tai_seconds_to_local_time`). Nothing is read from the host: the
/// offset and the summer-time flag are the caller's, e.g.
/// `tai_seconds_to_local_time(t, 3600, tai_second_is_dls_in_europe(t))` for
/// Germany. An inserted leap second keeps its `:60`. The offset must be a
/// whole number of minutes, as a zone offset is: with any other the library
/// would spell a leap second at a second that is not one. An error, too,
/// when the shifted instant does not fit in `i64`.
pub fn tai_seconds_to_local_time(
    tai_seconds: i64,
    utc_offset_seconds: i64,
    dls_active: bool,
) -> Result<CivilDateTime> {
    if utc_offset_seconds % 60 != 0 {
        return Err(Error::argument(format!(
            "utc_offset_seconds {utc_offset_seconds} is not a whole number of minutes"
        )));
    }
    c::tai_seconds_to_local_time(tai_seconds, utc_offset_seconds, dls_active)
        .map(wrap_dt)
        .ok_or_else(|| {
            Error::argument(format!(
                "TAI {tai_seconds} at offset {utc_offset_seconds} s has no local time in int64"
            ))
        })
}

/// Whether German/European summer time is in force at TAI second
/// `tai_second`, by the rule of that year (none before 1980; see
/// [`GregorianDate::is_daylight_saving_switch_in_germany`]); the switch is
/// at 01:00 UTC (`bvn_dt_tai_second_is_dls_in_europe`).
pub fn tai_second_is_dls_in_europe(tai_second: i64) -> bool {
    c::tai_second_is_dls_in_europe(tai_second)
}

fn civil_epoch(epoch: Epoch) -> Result<()> {
    if epoch.is_gnss() {
        return Err(Error::argument(format!(
            "the {} epoch counts atomic time and has no civil UTC form in this library",
            format!("{epoch:?}").to_lowercase()
        )));
    }
    Ok(())
}

/// The UTC time of a datetime value: `seconds` since `epoch`, read as the
/// library reads and writes ISO literals. On [`Epoch::Tai`] the leap-second
/// table applies ([`tai_seconds_to_utc`]); on the civil epochs (unix, mjd,
/// ntp, y2000) the count is uniform ([`epoch_seconds_to_datetime`]). The GNSS
/// epochs (gps, galileo, glonass, beidou) are an error, as the library has no
/// civil form for them; GPS and Galileo week + time of week go through
/// [`tai_seconds_from_gps_time`] instead.
pub fn epoch_seconds_to_utc(seconds: i64, epoch: Epoch) -> Result<CivilDateTime> {
    civil_epoch(epoch)?;
    if epoch == Epoch::Tai {
        tai_seconds_to_utc(seconds)
    } else {
        Ok(epoch_seconds_to_datetime(seconds, epoch))
    }
}

/// The inverse of [`epoch_seconds_to_utc`]: the UTC time `dt` as seconds
/// since `epoch`, the conversion the reader applies to an ISO literal
/// ([`utc_to_tai_seconds`] on TAI, [`datetime_to_epoch_seconds`] on a civil
/// epoch). The GNSS epochs are an error.
pub fn utc_to_epoch_seconds(dt: &CivilDateTime, epoch: Epoch) -> Result<i64> {
    civil_epoch(epoch)?;
    if epoch == Epoch::Tai {
        utc_to_tai_seconds(dt)
    } else {
        datetime_to_epoch_seconds(dt, epoch)
    }
}

// ── bvn_gregorian_date.h ───────────────────────────────────────────────────

fn check_mjd(mjd: i64) -> Result<i64> {
    if (MJD_MIN..=MJD_MAX).contains(&mjd) {
        Ok(mjd)
    } else {
        Err(Error::argument(format!(
            "MJD {mjd} is outside {MJD_MIN}..={MJD_MAX}"
        )))
    }
}

fn check_year(year: i64) -> Result<i64> {
    if (YEAR_MIN..=YEAR_MAX).contains(&year) {
        Ok(year)
    } else {
        Err(Error::argument(format!(
            "year {year} is outside {YEAR_MIN}..={YEAR_MAX}"
        )))
    }
}

/// The day of the week of Modified Julian Day `mjd`, 0 = Sunday ..= 6 =
/// Saturday, for any `i64` (`bvn_gregorian_date_mjd_to_wday`).
pub fn mjd_to_wday(mjd: i64) -> u32 {
    c::mjd_to_wday(mjd) as u32
}

/// Whether the year holding Modified Julian Day `mjd` is a leap year
/// (`bvn_gregorian_date_mjd_in_leap_year`). An error outside [`MJD_MIN`]
/// ..= [`MJD_MAX`], where C answers false.
pub fn mjd_in_leap_year(mjd: i64) -> Result<bool> {
    Ok(c::mjd_in_leap_year(check_mjd(mjd)?))
}

/// The day of the year of Modified Julian Day `mjd`, 0 = 1 January
/// (`bvn_gregorian_date_mjd_to_yday`). An error outside [`MJD_MIN`] ..=
/// [`MJD_MAX`], where C answers -1.
pub fn mjd_to_yday(mjd: i64) -> Result<u32> {
    Ok(c::mjd_to_yday(check_mjd(mjd)?) as u32)
}

/// The Modified Julian Day of (Gregorian, western) Easter Sunday of `year`
/// (`bvn_gregorian_date_easter_sunday_mjd`). An error outside [`YEAR_MIN`]
/// ..= [`YEAR_MAX`].
pub fn easter_sunday_mjd(year: i64) -> Result<i64> {
    checked(
        c::easter_sunday_mjd(check_year(year)?),
        format_args!("Easter of {year}"),
    )
}

fn feast(name: &str, year: i64, f: fn(i64) -> Option<BvnGregorianDate>) -> Result<GregorianDate> {
    f(check_year(year)?).map(wrap_date).ok_or_else(|| {
        Error::argument(format!(
            "{name} of {year} is outside the supported date range"
        ))
    })
}

/// Easter Sunday of `year` (`bvn_gregorian_date_calc_easter_sunday`).
pub fn easter_sunday(year: i64) -> Result<GregorianDate> {
    feast("Easter Sunday", year, c::calc_easter_sunday)
}

/// Good Friday, two days before Easter Sunday
/// (`bvn_gregorian_date_calc_good_friday`).
pub fn good_friday(year: i64) -> Result<GregorianDate> {
    feast("Good Friday", year, c::calc_good_friday)
}

/// Easter Monday (`bvn_gregorian_date_calc_easter_monday`).
pub fn easter_monday(year: i64) -> Result<GregorianDate> {
    feast("Easter Monday", year, c::calc_easter_monday)
}

/// Ascension Day, 39 days after Easter Sunday
/// (`bvn_gregorian_date_calc_ascension_day`).
pub fn ascension_day(year: i64) -> Result<GregorianDate> {
    feast("Ascension Day", year, c::calc_ascension_day)
}

/// Whit Monday, 50 days after Easter Sunday
/// (`bvn_gregorian_date_calc_whit_monday`).
pub fn whit_monday(year: i64) -> Result<GregorianDate> {
    feast("Whit Monday", year, c::calc_whit_monday)
}
