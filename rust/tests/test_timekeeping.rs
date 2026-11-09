// SPDX-License-Identifier: MIT
//! `bovnar::timekeeping`: the civil-time and calendar functions of
//! `bvn_datetime.h` and `bvn_gregorian_date.h`. The tables at the top are the
//! same as in python/tests/test_timekeeping.py, so the two bindings are held
//! to one set of answers.

use bovnar::timekeeping::{
    CivilDateTime, Epoch, GregorianDate, MJD_MAX, MJD_MIN, YEAR_MAX, YEAR_MIN, ascension_day,
    convert_epoch_seconds, datetime_to_epoch_seconds, easter_monday, easter_sunday,
    easter_sunday_mjd, epoch_seconds_from_galileo_time, epoch_seconds_from_gps_time,
    epoch_seconds_to_datetime, epoch_seconds_to_utc, good_friday, mjd_in_leap_year, mjd_to_wday,
    mjd_to_yday, tai_second_is_dls_in_europe, tai_seconds_from_galileo_time,
    tai_seconds_from_gps_time, tai_seconds_to_local_time, tai_seconds_to_utc, utc_to_epoch_seconds,
    utc_to_tai_seconds, whit_monday,
};
use bovnar::{Error, Value, parse_typed};

// ── shared with python/tests/test_timekeeping.py ───────────────────────────

/// The UTC days that end in an inserted leap second, 23:59:60; TAI-UTC is
/// 11 s after the first and one more after each.
const LEAP_DAYS: [(i64, u32, u32); 27] = [
    (1972, 6, 30),
    (1972, 12, 31),
    (1973, 12, 31),
    (1974, 12, 31),
    (1975, 12, 31),
    (1976, 12, 31),
    (1977, 12, 31),
    (1978, 12, 31),
    (1979, 12, 31),
    (1981, 6, 30),
    (1982, 6, 30),
    (1983, 6, 30),
    (1985, 6, 30),
    (1987, 12, 31),
    (1989, 12, 31),
    (1990, 12, 31),
    (1992, 6, 30),
    (1993, 6, 30),
    (1994, 6, 30),
    (1995, 12, 31),
    (1997, 6, 30),
    (1998, 12, 31),
    (2005, 12, 31),
    (2008, 12, 31),
    (2012, 6, 30),
    (2015, 6, 30),
    (2016, 12, 31),
];

const EASTER: [(i64, u32, u32); 8] = [
    (1818, 3, 22),
    (1943, 4, 25),
    (2000, 4, 23),
    (2019, 4, 21),
    (2024, 3, 31),
    (2025, 4, 20),
    (2038, 4, 25),
    (2285, 3, 22),
];

const GERMAN_HOLIDAYS_2024: [(u32, u32); 9] = [
    (1, 1),
    (3, 29),
    (4, 1),
    (5, 1),
    (5, 9),
    (5, 20),
    (10, 3),
    (12, 25),
    (12, 26),
];

type Ymd = (i64, u32, u32);

/// (date, last, penultimate, mid and first banking day of its month)
const BANKING_2024: [(Ymd, Ymd, Ymd, Ymd, Ymd); 4] = [
    (
        (2024, 3, 5),
        (2024, 3, 28),
        (2024, 3, 27),
        (2024, 3, 15),
        (2024, 3, 1),
    ),
    (
        (2024, 5, 31),
        (2024, 5, 31),
        (2024, 5, 30),
        (2024, 5, 15),
        (2024, 5, 2),
    ),
    (
        (2024, 6, 1),
        (2024, 6, 28),
        (2024, 6, 27),
        (2024, 6, 17),
        (2024, 6, 3),
    ),
    (
        (2024, 12, 9),
        (2024, 12, 30),
        (2024, 12, 27),
        (2024, 12, 16),
        (2024, 12, 2),
    ),
];

fn d(y: i64, m: u32, day: u32) -> GregorianDate {
    GregorianDate::new(y, m, day).unwrap()
}

fn ymd((y, m, day): Ymd) -> GregorianDate {
    d(y, m, day)
}

fn dt(y: i64, m: u32, day: u32, h: u32, min: u32, s: u32) -> CivilDateTime {
    CivilDateTime::new(y, m, day, h, min, s).unwrap()
}

fn is_arg<T: std::fmt::Debug>(r: Result<T, Error>) -> bool {
    matches!(r, Err(Error::Argument { .. }))
}

// ── GregorianDate ──────────────────────────────────────────────────────────

#[test]
fn mjd_of_known_days() {
    assert_eq!(d(2000, 1, 1).to_mjd(), 51544);
    assert_eq!(d(1858, 11, 17).to_mjd(), 0);
    assert_eq!(GregorianDate::from_mjd(51544).unwrap(), d(2000, 1, 1));
    assert_eq!(GregorianDate::from_mjd(44244).unwrap(), d(1980, 1, 6));
    let g = d(2000, 1, 1);
    assert_eq!((g.leap(), g.wday(), g.yday()), (true, 6, 0));
    let g = d(2024, 12, 31);
    assert_eq!((g.leap(), g.wday(), g.yday()), (true, 2, 365));
}

#[test]
fn range_ends() {
    assert_eq!(GregorianDate::from_mjd(MJD_MIN).unwrap(), d(YEAR_MIN, 1, 1));
    assert_eq!(
        GregorianDate::from_mjd(MJD_MAX).unwrap(),
        d(YEAR_MAX, 12, 31)
    );
    assert_eq!(d(YEAR_MAX, 12, 31).to_mjd(), MJD_MAX);
    for bad in [MJD_MIN - 1, MJD_MAX + 1, i64::MIN] {
        assert!(is_arg(GregorianDate::from_mjd(bad)));
        assert!(is_arg(mjd_in_leap_year(bad)));
        assert!(is_arg(mjd_to_yday(bad)));
    }
    assert!(mjd_to_wday(i64::MIN) < 7 && mjd_to_wday(i64::MAX) < 7);
    assert!(is_arg(GregorianDate::new(YEAR_MAX + 1, 1, 1)));
    assert!(is_arg(GregorianDate::normalize(YEAR_MAX, 13, 1)));
}

#[test]
fn strict_constructor_and_normalize() {
    for (y, m, day) in [
        (2023, 2, 29),
        (2024, 2, 30),
        (2024, 13, 1),
        (2024, 0, 1),
        (2024, 4, 31),
        (2024, 1, 0),
        (1900, 2, 29),
    ] {
        assert!(is_arg(GregorianDate::new(y, m, day)), "{y}-{m}-{day}");
    }
    assert!(d(2000, 2, 29).leap() && d(2024, 2, 29).leap());
    let n = |y, m, day| GregorianDate::normalize(y, m, day).unwrap();
    assert_eq!(n(2024, 2, 30), d(2024, 3, 1));
    assert_eq!(n(2024, 13, 1), d(2025, 1, 1));
    assert_eq!(n(2024, 3, 0), d(2024, 2, 29));
    assert_eq!(n(2024, 0, 1), d(2023, 12, 1));
    assert_eq!(n(2024, 1, -365), d(2022, 12, 31));
    assert_eq!(n(2024, 3, 61), d(2024, 4, 30));
}

#[test]
fn date_value_semantics() {
    use std::collections::HashSet;
    let a = d(2024, 3, 31);
    let b = GregorianDate::from_mjd(a.to_mjd()).unwrap();
    assert_eq!(a, b);
    assert_eq!(HashSet::from([a, b]).len(), 1);
    assert!(d(2024, 3, 30) < a && a < d(2024, 4, 1) && d(2024, 4, 1) < d(2025, 1, 1));
    assert_ne!(a, d(2024, 4, 1));
    assert_eq!(a.to_string(), "2024-03-31");
    assert_eq!(d(-1, 1, 1).to_string(), "-0001-01-01");
}

#[test]
fn mjd_round_trip() {
    // 0001-01-01 .. 9999-12-31, as in the Python test (which also checks
    // these against the standard library's calendar)
    for mjd in (-678575..2973483).step_by(997) {
        let g = GregorianDate::from_mjd(mjd).unwrap();
        assert_eq!(g.to_mjd(), mjd);
        assert_eq!(g.wday(), mjd_to_wday(mjd));
        assert_eq!(g.yday(), mjd_to_yday(mjd).unwrap());
        assert_eq!(g.leap(), mjd_in_leap_year(mjd).unwrap());
        assert_eq!(GregorianDate::new(g.year(), g.month(), g.day()).unwrap(), g);
    }
    for mjd in (MJD_MIN..MJD_MIN + 800).chain(MJD_MAX - 800..=MJD_MAX) {
        assert_eq!(GregorianDate::from_mjd(mjd).unwrap().to_mjd(), mjd);
    }
}

// ── Easter and the German calendar ─────────────────────────────────────────

#[test]
fn easter() {
    for (y, m, day) in EASTER {
        assert_eq!(easter_sunday(y).unwrap(), d(y, m, day));
        assert_eq!(easter_sunday_mjd(y).unwrap(), d(y, m, day).to_mjd());
    }
    assert_eq!(good_friday(2024).unwrap(), d(2024, 3, 29));
    assert_eq!(easter_monday(2024).unwrap(), d(2024, 4, 1));
    assert_eq!(ascension_day(2024).unwrap(), d(2024, 5, 9));
    assert_eq!(whit_monday(2024).unwrap(), d(2024, 5, 20));
    assert_eq!(whit_monday(2025).unwrap(), d(2025, 6, 9));
    assert_eq!(easter_sunday(YEAR_MAX).unwrap().year(), YEAR_MAX);
    assert_eq!(whit_monday(YEAR_MAX).unwrap().year(), YEAR_MAX);
    for y in [YEAR_MAX + 1, YEAR_MIN - 1] {
        assert!(is_arg(easter_sunday(y)));
        assert!(is_arg(good_friday(y)));
        assert!(is_arg(whit_monday(y)));
        assert!(is_arg(easter_sunday_mjd(y)));
    }
}

#[test]
fn german_federal_holidays() {
    let holidays: Vec<(u32, u32)> = (d(2024, 1, 1).to_mjd()..d(2025, 1, 1).to_mjd())
        .map(|m| GregorianDate::from_mjd(m).unwrap())
        .filter(|g| g.is_federal_holiday_in_germany())
        .map(|g| (g.month(), g.day()))
        .collect();
    assert_eq!(holidays, GERMAN_HOLIDAYS_2024);
    assert!(d(1985, 6, 17).is_federal_holiday_in_germany());
    assert!(!d(1985, 10, 3).is_federal_holiday_in_germany());
    assert!(d(1990, 10, 3).is_federal_holiday_in_germany());
    assert!(!d(1990, 6, 17).is_federal_holiday_in_germany());
    assert!(!d(1932, 5, 1).is_federal_holiday_in_germany());
    assert!(d(1933, 5, 1).is_federal_holiday_in_germany());
    assert!(!d(2024, 12, 24).is_federal_holiday_in_germany());
}

#[test]
fn banking_and_business_days() {
    assert!(d(2024, 3, 28).is_banking_day_in_germany());
    for g in [
        d(2024, 3, 29),
        d(2024, 3, 30),
        d(2024, 3, 31),
        d(2024, 12, 24),
        d(2024, 12, 31),
    ] {
        assert!(!g.is_banking_day_in_germany(), "{g}");
    }
    assert!(d(2024, 3, 30).is_business_day_in_germany());
    assert!(d(2024, 12, 24).is_business_day_in_germany());
    assert!(d(2024, 12, 31).is_business_day_in_germany());
    assert!(!d(2024, 3, 31).is_business_day_in_germany());
    assert!(!d(2024, 3, 29).is_business_day_in_germany());
}

#[test]
fn banking_day_getters_and_predicates() -> Result<(), Error> {
    for (date, last, pen, mid, first) in BANKING_2024 {
        let g = ymd(date);
        assert_eq!(g.last_banking_day_in_month()?, ymd(last));
        assert_eq!(g.penultimate_banking_day_in_month()?, ymd(pen));
        assert_eq!(g.mid_banking_day_in_month()?, ymd(mid));
        assert_eq!(g.first_banking_day_in_month()?, ymd(first));
        assert!(ymd(last).is_last_banking_day_in_month());
        assert!(ymd(pen).is_penultimate_banking_day_in_month());
        assert!(ymd(mid).is_mid_banking_day_in_month());
        assert!(ymd(first).is_first_banking_day_in_month());
        assert!(!ymd(pen).is_last_banking_day_in_month());
    }
    let g = d(2024, 8, 8);
    assert_eq!(g.last_banking_day_in_quarter(1)?, d(2024, 3, 28));
    assert_eq!(g.first_banking_day_in_quarter(2)?, d(2024, 4, 2));
    assert_eq!(g.last_banking_day_in_quarter(4)?, d(2024, 12, 30));
    assert_eq!(g.first_banking_day_in_quarter(1)?, d(2024, 1, 2));
    assert!(d(2024, 12, 30).is_last_banking_day_in_quarter(4)?);
    assert!(!d(2024, 12, 30).is_last_banking_day_in_quarter(3)?);
    assert!(d(2024, 4, 2).is_first_banking_day_in_quarter(2)?);
    for q in [0, 5, u32::MAX] {
        assert!(is_arg(g.last_banking_day_in_quarter(q)));
        assert!(is_arg(g.is_first_banking_day_in_quarter(q)));
    }
    assert_eq!(g.last_sunday_in_month()?, d(2024, 8, 25));
    assert_eq!(g.last_saturday_in_month()?, d(2024, 8, 31));
    assert_eq!(d(2024, 3, 1).last_sunday_in_month()?, d(2024, 3, 31));
    assert_eq!(d(2024, 10, 1).last_sunday_in_month()?, d(2024, 10, 27));
    Ok(())
}

#[test]
fn daylight_saving_switch_days() {
    for (y, m, day) in [
        (2024, 3, 31),
        (2024, 10, 27),
        (1995, 9, 24),
        (1996, 10, 27),
        (1980, 4, 6),
        (1980, 9, 28),
    ] {
        assert!(
            d(y, m, day).is_daylight_saving_switch_in_germany(),
            "{y}-{m}-{day}"
        );
        assert!(
            GregorianDate::normalize(y, m.into(), i64::from(day) - 1)
                .unwrap()
                .is_day_before_daylight_saving_switch_in_germany()
        );
    }
    for (y, m, day) in [
        (1979, 3, 25),
        (1995, 10, 29),
        (1980, 3, 30),
        (2024, 3, 24),
        (2024, 3, 30),
    ] {
        assert!(
            !d(y, m, day).is_daylight_saving_switch_in_germany(),
            "{y}-{m}-{day}"
        );
    }
    assert!(d(2024, 3, 30).is_day_before_daylight_saving_switch_in_germany());
}

// ── bvn_datetime.h ─────────────────────────────────────────────────────────

#[test]
fn civil_datetime_checks_its_fields() {
    for (h, min, s) in [(24, 0, 0), (0, 60, 0), (0, 0, 61)] {
        assert!(is_arg(CivilDateTime::new(2024, 1, 1, h, min, s)));
    }
    assert!(is_arg(CivilDateTime::new(2024, 2, 30, 0, 0, 0)));
    let t = dt(2016, 12, 31, 23, 59, 60);
    assert_eq!(t.to_string(), "2016-12-31T23:59:60");
    assert_eq!(t.date(), d(2016, 12, 31));
    assert_eq!(t.mjd(), 57753);
    assert_eq!(t, dt(2016, 12, 31, 23, 59, 60));
    assert_ne!(t, dt(2016, 12, 31, 23, 59, 59));
    assert!(dt(2016, 12, 31, 23, 59, 59) < t && t < dt(2017, 1, 1, 0, 0, 0));
    assert!(t.is_date_equal(&dt(2016, 12, 31, 0, 0, 0)));
    assert!(t.is_time_equal(&dt(2000, 1, 1, 23, 59, 60)));
    assert!(!t.is_time_equal(&dt(2016, 12, 31, 0, 0, 0)));
}

#[test]
fn epochs() {
    let mjds: Vec<i32> = Epoch::ALL.iter().map(|e| e.mjd()).collect();
    assert_eq!(
        mjds,
        [0, 15020, 36204, 40587, 44244, 50083, 51412, 51544, 53736]
    );
    for (e, name) in Epoch::ALL.into_iter().zip([
        "mjd", "ntp", "tai", "unix", "gps", "glonass", "galileo", "y2000", "beidou",
    ]) {
        assert_eq!(Epoch::from_name(name), Some(e));
        assert_eq!(Epoch::from_mjd(e.mjd()), Some(e));
        assert_eq!(
            epoch_seconds_to_datetime(0, e).to_string(),
            format!(
                "{}T00:00:00",
                GregorianDate::from_mjd(e.mjd().into()).unwrap()
            )
        );
    }
    assert_eq!(Epoch::from_name(""), Some(Epoch::Unix));
    assert_eq!(Epoch::from_name("nope"), None);
    assert_eq!(Epoch::from_mjd(51545), None);
}

#[test]
fn uniform_seconds() -> Result<(), Error> {
    assert_eq!(
        epoch_seconds_to_datetime(-1, Epoch::Unix),
        dt(1969, 12, 31, 23, 59, 59)
    );
    assert_eq!(
        epoch_seconds_to_datetime(1_750_000_000, Epoch::Unix),
        dt(2025, 6, 15, 15, 6, 40)
    );
    assert_eq!(
        datetime_to_epoch_seconds(&dt(2000, 1, 1, 0, 0, 0), Epoch::Unix)?,
        946_684_800
    );
    assert_eq!(
        datetime_to_epoch_seconds(&dt(2016, 12, 31, 23, 59, 60), Epoch::Unix)?,
        1_483_228_800
    );
    for s in [i64::MIN + 1, -1, 0, 1, i64::MAX] {
        for e in Epoch::ALL {
            assert_eq!(
                datetime_to_epoch_seconds(&epoch_seconds_to_datetime(s, e), e)?,
                s,
                "{s} {e:?}"
            );
        }
    }
    // i64::MIN renders, but is the error sentinel and never a result
    let min = epoch_seconds_to_datetime(i64::MIN, Epoch::Unix);
    assert!(is_arg(datetime_to_epoch_seconds(&min, Epoch::Unix)));
    assert!(is_arg(datetime_to_epoch_seconds(
        &dt(YEAR_MAX, 12, 31, 0, 0, 0),
        Epoch::Unix
    )));
    for s in (-2_208_988_800i64..4_102_444_800).step_by(86400 * 37 + 3601) {
        let t = epoch_seconds_to_datetime(s, Epoch::Unix);
        assert_eq!(datetime_to_epoch_seconds(&t, Epoch::Unix)?, s);
    }
    Ok(())
}

#[test]
fn convert_between_epochs() -> Result<(), Error> {
    assert_eq!(
        convert_epoch_seconds(0, Epoch::Unix, Epoch::Tai)?,
        4383 * 86400
    );
    assert_eq!(
        convert_epoch_seconds(4383 * 86400, Epoch::Tai, Epoch::Unix)?,
        0
    );
    assert_eq!(
        convert_epoch_seconds(0, Epoch::Gps, Epoch::Unix)?,
        315_964_800
    );
    assert_eq!(
        convert_epoch_seconds(i64::MAX, Epoch::Tai, Epoch::Unix)?,
        i64::MAX - 4383 * 86400
    );
    assert!(is_arg(convert_epoch_seconds(
        i64::MAX,
        Epoch::Unix,
        Epoch::Tai
    )));
    assert!(is_arg(convert_epoch_seconds(
        i64::MIN,
        Epoch::Tai,
        Epoch::Unix
    )));
    Ok(())
}

#[test]
fn leap_seconds() -> Result<(), Error> {
    let t = utc_to_tai_seconds(&dt(2016, 12, 31, 23, 59, 60))?;
    assert_eq!(t, 1_861_920_036);
    assert_eq!(tai_seconds_to_utc(t)?.to_string(), "2016-12-31T23:59:60");
    assert_eq!(
        tai_seconds_to_utc(t - 1)?.to_string(),
        "2016-12-31T23:59:59"
    );
    assert_eq!(
        tai_seconds_to_utc(t + 1)?.to_string(),
        "2017-01-01T00:00:00"
    );
    for (i, (y, m, day)) in LEAP_DAYS.into_iter().enumerate() {
        let leap = dt(y, m, day, 23, 59, 60);
        let t = utc_to_tai_seconds(&leap)?;
        assert_eq!(tai_seconds_to_utc(t)?, leap);
        let next = GregorianDate::normalize(y, m.into(), i64::from(day) + 1)?;
        let after = dt(next.year(), next.month(), next.day(), 0, 0, 0);
        assert_eq!(tai_seconds_to_utc(t + 1)?, after);
        assert_eq!(utc_to_tai_seconds(&after)?, t + 1);
        assert_eq!(
            t + 1 - datetime_to_epoch_seconds(&after, Epoch::Tai)?,
            11 + i as i64
        );
        for k in -3..=3 {
            assert_eq!(utc_to_tai_seconds(&tai_seconds_to_utc(t + k)?)?, t + k);
        }
    }
    let offset = |c: CivilDateTime| -> Result<i64, Error> {
        Ok(utc_to_tai_seconds(&c)? - datetime_to_epoch_seconds(&c, Epoch::Tai)?)
    };
    assert_eq!(offset(dt(1960, 1, 1, 0, 0, 0))?, 10);
    assert_eq!(offset(dt(2026, 1, 1, 0, 0, 0))?, 37);
    assert_eq!(
        utc_to_tai_seconds(&dt(2017, 12, 31, 23, 59, 60))?,
        utc_to_tai_seconds(&dt(2018, 1, 1, 0, 0, 0))?
    );
    for t in (-10_000_000_000i64..10_000_000_000).step_by(10_000_000_000 / 4999) {
        assert_eq!(utc_to_tai_seconds(&tai_seconds_to_utc(t)?)?, t);
    }
    Ok(())
}

#[test]
fn tai_extremes() {
    assert!(is_arg(tai_seconds_to_utc(i64::MIN)));
    assert!(tai_seconds_to_utc(i64::MIN + 10).is_ok());
    assert!(tai_seconds_to_utc(i64::MAX).unwrap().year() > 200_000_000_000);
    assert!(is_arg(utc_to_tai_seconds(&dt(YEAR_MIN, 1, 1, 0, 0, 0))));
}

#[test]
fn gnss_time() -> Result<(), Error> {
    assert_eq!(
        tai_seconds_to_utc(tai_seconds_from_gps_time(0, 0)?)?,
        dt(1980, 1, 6, 0, 0, 0)
    );
    assert_eq!(epoch_seconds_from_gps_time(0, 0, Epoch::Gps)?, 0);
    assert_eq!(
        epoch_seconds_from_gps_time(0, 0, Epoch::Tai)?,
        19 + convert_epoch_seconds(0, Epoch::Gps, Epoch::Tai)?
    );
    assert_eq!(
        tai_seconds_to_utc(tai_seconds_from_gps_time(0, 2300)?)?,
        dt(2024, 2, 3, 23, 59, 42)
    );
    assert_eq!(
        epoch_seconds_to_datetime(
            epoch_seconds_from_gps_time(0, 2300, Epoch::Unix)?,
            Epoch::Unix
        ),
        dt(2024, 2, 4, 0, 0, 0)
    );
    assert_eq!(
        tai_seconds_from_gps_time(1499, 0)? + 1,
        tai_seconds_from_gps_time(1500, 0)?
    );
    assert_eq!(
        tai_seconds_to_utc(tai_seconds_from_galileo_time(0, 0)?)?,
        dt(1999, 8, 21, 23, 59, 47)
    );
    for (tow, week) in [(0, 0), (123_456_789, 250), (604_799_000, 1400)] {
        assert_eq!(
            tai_seconds_from_galileo_time(tow, week)?,
            tai_seconds_from_gps_time(tow, week + 1024)?
        );
        assert_eq!(
            epoch_seconds_from_galileo_time(tow, week, Epoch::Unix)?,
            epoch_seconds_from_gps_time(tow, week + 1024, Epoch::Unix)?
        );
    }
    assert!(is_arg(tai_seconds_from_gps_time(0, i64::MAX)));
    assert!(is_arg(tai_seconds_from_galileo_time(0, i64::MIN)));
    Ok(())
}

#[test]
fn local_time_and_dst() -> Result<(), Error> {
    let t = utc_to_tai_seconds(&dt(2016, 12, 31, 23, 59, 60))?;
    assert_eq!(
        tai_seconds_to_local_time(t, 3600, false)?.to_string(),
        "2017-01-01T00:59:60"
    );
    assert_eq!(
        tai_seconds_to_local_time(t, -5 * 3600, false)?.to_string(),
        "2016-12-31T18:59:60"
    );
    let summer = utc_to_tai_seconds(&dt(2024, 7, 1, 12, 0, 0))?;
    let winter = utc_to_tai_seconds(&dt(2024, 1, 15, 12, 0, 0))?;
    assert!(tai_second_is_dls_in_europe(summer));
    assert!(!tai_second_is_dls_in_europe(winter));
    assert_eq!(
        tai_seconds_to_local_time(summer, 3600, true)?,
        dt(2024, 7, 1, 14, 0, 0)
    );
    assert_eq!(
        tai_seconds_to_local_time(winter, 3600, false)?,
        dt(2024, 1, 15, 13, 0, 0)
    );
    let on = utc_to_tai_seconds(&dt(2024, 3, 31, 1, 0, 0))?;
    let off = utc_to_tai_seconds(&dt(2024, 10, 27, 1, 0, 0))?;
    assert!(!tai_second_is_dls_in_europe(on - 1));
    assert!(tai_second_is_dls_in_europe(on));
    assert!(tai_second_is_dls_in_europe(off - 1));
    assert!(!tai_second_is_dls_in_europe(off));
    assert!(!tai_second_is_dls_in_europe(utc_to_tai_seconds(&dt(
        1979, 7, 1, 0, 0, 0
    ))?));
    assert!(!tai_second_is_dls_in_europe(i64::MIN));
    assert!(!tai_second_is_dls_in_europe(i64::MAX));
    assert!(is_arg(tai_seconds_to_local_time(t, 30, false)));
    assert!(is_arg(tai_seconds_to_local_time(i64::MAX, 3600, false)));
    Ok(())
}

// ── the datetime family ────────────────────────────────────────────────────

const LITERALS: [&str; 6] = [
    "2016-12-31T23:59:60Z",
    "2017-01-01T00:59:60+01:00",
    "2024-03-31T12:00:00+02:00",
    "1900-01-01T00:00:00Z",
    "1969-12-31T23:59:59Z",
    "2000-02-29T12:34:56Z",
];

fn quantity(doc: &str) -> bovnar::Quantity {
    let Value::Struct(fields) = parse_typed(doc.as_bytes()).unwrap() else {
        unreachable!()
    };
    let Value::Quantity(q) = &fields[0].1 else {
        unreachable!()
    };
    (**q).clone()
}

#[test]
fn utc_conversion_agrees_with_the_reader() -> Result<(), Error> {
    for name in ["unix", "tai", "mjd", "ntp", "y2000"] {
        let epoch = Epoch::from_name(name).unwrap();
        for lit in LITERALS {
            let q = quantity(&format!("#!bovnar 1.1\n.t = <datetime:64,{name}> {lit};"));
            let Value::Int(value) = q.value() else {
                unreachable!()
            };
            let value = i64::try_from(value).unwrap();
            let civil = q.to_civil()?;
            assert_eq!(civil, epoch_seconds_to_utc(value, epoch)?);
            assert_eq!(utc_to_epoch_seconds(&civil, epoch)?, value);
            if lit.ends_with('Z') && (name == "tai" || !lit.contains(":60")) {
                assert_eq!(format!("{civil}Z"), lit);
            }
        }
    }
    Ok(())
}

#[test]
fn to_civil() -> Result<(), Error> {
    let q = quantity("#!bovnar 1.1\n.t = <datetime:64,tai> 2016-12-31T23:59:60.25Z;");
    assert_eq!(q.to_civil()?.to_string(), "2016-12-31T23:59:60");
    assert_eq!(q.frac.as_deref(), Some("25"));
    let q = quantity("#!bovnar 1.1\n.t = <datetime> 1750000000;");
    assert_eq!(q.to_civil()?, dt(2025, 6, 15, 15, 6, 40));
    for doc in [
        "#!bovnar 1.1\n.t = <datetime:64,gps> 5;",
        ".t = <uint:8> 5;",
        "#!bovnar 1.1\n.t = <datetime:64,unix> null;",
    ] {
        assert!(is_arg(quantity(doc).to_civil()), "{doc}");
    }
    for e in [Epoch::Gps, Epoch::Galileo, Epoch::Glonass, Epoch::Beidou] {
        assert!(is_arg(epoch_seconds_to_utc(0, e)));
        assert!(is_arg(utc_to_epoch_seconds(&dt(2000, 1, 1, 0, 0, 0), e)));
    }
    Ok(())
}
