/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "bvn_datetime.h"
#include "bvn_date_impl.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/* TAI-UTC offset table.  utc_seconds is the UTC instant at which the offset
 * takes effect, expressed as uniform seconds (86400 s/day) since the TAI
 * epoch 1958-01-01 00:00:00.  Entries must stay sorted ascending; when IERS
 * announces the next leap second, append one entry here — nothing else in
 * the module needs to change. */
typedef struct {
	int64_t utc_seconds;
	int64_t tai_minus_utc;
} bvn_dt_leap_entry_t;

static const bvn_dt_leap_entry_t bvn_dt_leap_table[] = {
	{ INT64_C(  441763200), 10 }, /* 1972-01-01 */
	{ INT64_C(  457488000), 11 }, /* 1972-07-01 */
	{ INT64_C(  473385600), 12 }, /* 1973-01-01 */
	{ INT64_C(  504921600), 13 }, /* 1974-01-01 */
	{ INT64_C(  536457600), 14 }, /* 1975-01-01 */
	{ INT64_C(  567993600), 15 }, /* 1976-01-01 */
	{ INT64_C(  599616000), 16 }, /* 1977-01-01 */
	{ INT64_C(  631152000), 17 }, /* 1978-01-01 */
	{ INT64_C(  662688000), 18 }, /* 1979-01-01 */
	{ INT64_C(  694224000), 19 }, /* 1980-01-01 */
	{ INT64_C(  741484800), 20 }, /* 1981-07-01 */
	{ INT64_C(  773020800), 21 }, /* 1982-07-01 */
	{ INT64_C(  804556800), 22 }, /* 1983-07-01 */
	{ INT64_C(  867715200), 23 }, /* 1985-07-01 */
	{ INT64_C(  946684800), 24 }, /* 1988-01-01 */
	{ INT64_C( 1009843200), 25 }, /* 1990-01-01 */
	{ INT64_C( 1041379200), 26 }, /* 1991-01-01 */
	{ INT64_C( 1088640000), 27 }, /* 1992-07-01 */
	{ INT64_C( 1120176000), 28 }, /* 1993-07-01 */
	{ INT64_C( 1151712000), 29 }, /* 1994-07-01 */
	{ INT64_C( 1199145600), 30 }, /* 1996-01-01 */
	{ INT64_C( 1246406400), 31 }, /* 1997-07-01 */
	{ INT64_C( 1293840000), 32 }, /* 1999-01-01 */
	{ INT64_C( 1514764800), 33 }, /* 2006-01-01 */
	{ INT64_C( 1609459200), 34 }, /* 2009-01-01 */
	{ INT64_C( 1719792000), 35 }, /* 2012-07-01 */
	{ INT64_C( 1814400000), 36 }, /* 2015-07-01 */
	{ INT64_C( 1861920000), 37 }, /* 2017-01-01 */
};

#define BVN_DT_LEAP_TABLE_LEN \
	(sizeof(bvn_dt_leap_table) / sizeof(bvn_dt_leap_table[0]))

/* TAI-UTC at the given UTC instant (uniform seconds since the TAI epoch).
 * Instants before 1972 predate UTC leap seconds (rubber-second era) and are
 * approximated with the first table entry. */
static int64_t bvn_dt_tai_minus_utc_at(int64_t utc_seconds)
{
	for (size_t i = BVN_DT_LEAP_TABLE_LEN; i-- > 0;) {
		if (utc_seconds >= bvn_dt_leap_table[i].utc_seconds)
			return bvn_dt_leap_table[i].tai_minus_utc;
	}
	return bvn_dt_leap_table[0].tai_minus_utc;
}

void bvn_dt_epoch_seconds_to_datetime(bvn_datetime_t* dt, bvn_epoch_t epoch, int64_t seconds)
{
	/* Floor semantics: instants before the epoch must roll back into the
	 * previous day, not produce negative time-of-day fields. */
	int64_t u = floor_div(seconds, 86400);
	int64_t s = floor_mod(seconds, 86400);

	dt->submjd = div_round_closest(s * 100000, 86400);

	dt->second = s % 60; s /= 60;
	dt->minute = s % 60; s /= 60;
	dt->hour = s;

	dt->mjd = u + (int64_t)epoch;
	bvn_gregorian_date_from_mjd(&dt->date, dt->mjd);
}

int64_t bvn_dt_datetime_to_epoch_seconds(bvn_datetime_t* dt, bvn_epoch_t epoch)
{
	if (!bvn_gregorian_date_normalize(&dt->date))
		return BVN_GDATE_OVF;

	/* normalize() succeeded, so the date is canonical and in range; the
	 * fast converter is safe here. */
	dt->mjd = bvn_gregorian_date_to_mjd_fast(&dt->date);

	int64_t r, t;
	bool ov = false;
	ov |= bvn_ckd_sub(&r, dt->mjd, (int64_t)epoch);
	ov |= bvn_ckd_mul(&r, r, (int64_t)86400);
	ov |= bvn_ckd_mul(&t, dt->hour, (int64_t)3600);
	ov |= bvn_ckd_add(&r, r, t);
	ov |= bvn_ckd_mul(&t, dt->minute, (int64_t)60);
	ov |= bvn_ckd_add(&r, r, t);
	ov |= bvn_ckd_add(&r, r, dt->second);
	/* INT64_MIN is reserved as the error sentinel; report the one
	 * (absurd-input) computation that lands on it exactly as overflow so
	 * a successful return can never collide with BVN_GDATE_OVF. */
	if (ov || r == BVN_GDATE_OVF)
		return BVN_GDATE_OVF;
	return r;
}

int64_t bvn_dt_convert_epoch_seconds(bvn_epoch_t epoch_from, bvn_epoch_t epoch_to, int64_t seconds)
{
	/* The casts matter: enum arithmetic is done in the (32-bit) underlying
	 * type and wraps for negative differences and wide epoch gaps.  The
	 * epoch difference itself is bounded (< 54000 days), only the final
	 * addition can overflow. */
	int64_t r;
	/* INT64_MIN is reserved as the error sentinel; an input whose shifted
	 * value lands on it exactly must be reported as overflow so a successful
	 * return can never collide with BVN_GDATE_OVF (matching the guard in
	 * bvn_dt_datetime_to_epoch_seconds() and the GNSS helper). */
	if (bvn_ckd_add(&r, 86400 * ((int64_t)epoch_from - (int64_t)epoch_to),
			seconds) || r == BVN_GDATE_OVF)
		return BVN_GDATE_OVF;
	return r;
}

/* Shared GNSS week + time-of-week conversion; gnss_epoch is the MJD of the
 * constellation's epoch.  Returns BVN_GDATE_OVF on overflow. */
static int64_t bvn_dt_epoch_seconds_from_gnss_time(int64_t gnss_epoch,
		int64_t timeofweek_ms, int64_t week, bvn_epoch_t epoch)
{
	int64_t timeofweek_s = div_round_closest(timeofweek_ms, 1000);
	int64_t days, t, seconds;
	bool ov = false;

	ov |= bvn_ckd_mul(&days, week, (int64_t)7);
	ov |= bvn_ckd_add(&days, days, gnss_epoch - (int64_t)epoch);
	ov |= bvn_ckd_add(&days, days, timeofweek_s / 86400);
	ov |= bvn_ckd_mul(&t, days, (int64_t)86400);
	ov |= bvn_ckd_add(&seconds, t, timeofweek_s % 86400);
	if (epoch == bvn_epoch_tai)
		ov |= bvn_ckd_add(&seconds, seconds, (int64_t)19);
	if (ov || seconds == BVN_GDATE_OVF)
		return BVN_GDATE_OVF;
	return seconds;
}

int64_t bvn_dt_epoch_seconds_from_gps_time(int64_t timeofweek_ms, int64_t gps_week, bvn_epoch_t epoch)
{
	return bvn_dt_epoch_seconds_from_gnss_time((int64_t)bvn_epoch_gps,
						   timeofweek_ms, gps_week, epoch);
}

int64_t bvn_dt_tai_seconds_from_gps_time(int64_t timeofweek_ms, int64_t gps_week)
{
	return bvn_dt_epoch_seconds_from_gps_time(timeofweek_ms, gps_week,
						  bvn_epoch_tai);
}

int64_t bvn_dt_epoch_seconds_from_galileo_time(int64_t timeofweek_ms, int64_t galileo_week, bvn_epoch_t epoch)
{
	return bvn_dt_epoch_seconds_from_gnss_time((int64_t)bvn_epoch_galileo,
						   timeofweek_ms, galileo_week, epoch);
}

int64_t bvn_dt_tai_seconds_from_galileo_time(int64_t timeofweek_ms, int64_t galileo_week)
{
	return bvn_dt_epoch_seconds_from_galileo_time(timeofweek_ms, galileo_week,
						      bvn_epoch_tai);
}

int64_t bvn_dt_utc_to_tai_seconds(bvn_datetime_t* dt)
{
	int64_t utc_seconds = bvn_dt_datetime_to_epoch_seconds(dt, bvn_epoch_tai);
	if (utc_seconds == BVN_GDATE_OVF)
		return BVN_GDATE_OVF;

	int64_t tai;
	if (bvn_ckd_add(&tai, utc_seconds, bvn_dt_tai_minus_utc_at(utc_seconds))
			|| tai == BVN_GDATE_OVF)
		return BVN_GDATE_OVF;
	return tai;
}

/* TAI-UTC offset valid at the given TAI instant.  Picks the offset whose
 * validity interval (in TAI) contains the instant.  The inserted leap second
 * itself (23:59:60) is not representable and collapses onto 00:00:00 of the
 * following UTC day. */
static int64_t bvn_dt_tai_minus_utc_for_tai(int64_t tai_seconds)
{
	for (size_t i = BVN_DT_LEAP_TABLE_LEN; i-- > 0;) {
		const bvn_dt_leap_entry_t* e = &bvn_dt_leap_table[i];
		if (tai_seconds >= e->utc_seconds + e->tai_minus_utc)
			return e->tai_minus_utc;
	}
	return bvn_dt_leap_table[0].tai_minus_utc;
}

void bvn_dt_tai_seconds_to_utc(bvn_datetime_t* dt, int64_t tai_seconds)
{
	int64_t off = bvn_dt_tai_minus_utc_for_tai(tai_seconds);

	/* Underflows only within 37 s of INT64_MIN; leave *dt untouched then. */
	int64_t utc_seconds;
	if (bvn_ckd_sub(&utc_seconds, tai_seconds, off))
		return;
	bvn_dt_epoch_seconds_to_datetime(dt, bvn_epoch_tai, utc_seconds);
}

void bvn_dt_tai_seconds_to_local_time(bvn_datetime_t* dt, int64_t tai_seconds, int64_t timezone_offset_seconds, int dls_active)
{
	/* The leap-second offset must be taken at the true instant: convert to
	 * UTC first, then apply the zone/DST shift on the UTC scale.  Applying
	 * the shift to TAI before the lookup would, near a leap second, push the
	 * value across a table boundary and select the neighbouring offset,
	 * skewing the rendered local time by one second. */
	int64_t local = tai_seconds;
	if (bvn_ckd_sub(&local, local, bvn_dt_tai_minus_utc_for_tai(tai_seconds)))
		return;	/* leave *dt untouched on overflow */
	if (bvn_ckd_add(&local, local, timezone_offset_seconds))
		return;
	if (dls_active && bvn_ckd_add(&local, local, (int64_t)3600))
		return;
	bvn_dt_epoch_seconds_to_datetime(dt, bvn_epoch_tai, local);
}

int bvn_dt_is_date_equal(bvn_datetime_t* a, bvn_datetime_t* b)
{
	return (a->date.day == b->date.day) && (a->date.month == b->date.month) && (a->date.year == b->date.year);
}

int bvn_dt_is_time_equal(bvn_datetime_t* a, bvn_datetime_t* b)
{
	return (a->hour == b->hour) && (a->minute == b->minute) && (a->second == b->second);
}

int bvn_dt_is_equal(bvn_datetime_t* a, bvn_datetime_t* b)
{
	return bvn_dt_is_date_equal(a, b) && bvn_dt_is_time_equal(a, b);
}


int bvn_dt_tai_second_is_dls_in_europe(int64_t tai_second)
{
	bvn_datetime_t dt = {{0}, 0, 0, 0, 0, 0};
	int64_t tai_dls_on;
	int64_t tai_dls_off;

	/* Resolve the calendar year of the instant; the DST window (last Sunday
	 * of March .. last Sunday of October) lies entirely within that year. */
	bvn_dt_tai_seconds_to_utc(&dt, tai_second);

	bvn_datetime_t dls_on = {
		.date.year = dt.date.year,
		.date.month = 3,
		.hour = 1,
	};

	bvn_datetime_t dls_off = {
		.date.year = dt.date.year,
		.date.month = 10,
		.hour = 1,
	};

	bvn_gregorian_date_get_last_sunday_in_month(&dls_on.date, &dls_on.date);
	bvn_gregorian_date_get_last_sunday_in_month(&dls_off.date, &dls_off.date);

	tai_dls_on = bvn_dt_utc_to_tai_seconds(&dls_on);
	tai_dls_off = bvn_dt_utc_to_tai_seconds(&dls_off);

	/* Unresolvable boundaries (int64 extremes) must not flow the error
	 * sentinel into the comparisons; report them as "not DST".  The input is
	 * already on the TAI scale, so compare it against the boundaries
	 * directly instead of round-tripping it through UTC. */
	if (tai_dls_on == BVN_GDATE_OVF || tai_dls_off == BVN_GDATE_OVF)
		return 0;

	return (tai_second >= tai_dls_on) && (tai_second < tai_dls_off);
}

void bvn_dt_dump(bvn_datetime_t* dt)
{
	printf("day        : %" PRIi64 "\n", dt->date.day);
	printf("month      : %" PRIi64 "\n", dt->date.month);
	printf("year       : %" PRIi64 "\n", dt->date.year);
	printf("is leap    : %" PRIi32 "\n", dt->date.leap);
	printf("day of week: %" PRIi32 "\n", dt->date.wday);
	printf("day of year: %" PRIi32 "\n", dt->date.yday);
	printf("hour       : %" PRIi64 "\n", dt->hour);
	printf("minute     : %" PRIi64 "\n", dt->minute);
	printf("second     : %" PRIi64 "\n", dt->second);
	printf("mjd        : %" PRIi64 "\n", dt->mjd);
	printf("submjd     : %" PRIi64 "\n", dt->submjd);
}

void bvn_dt_print(bvn_datetime_t* dt)
{
	printf("MJD %" PRIi64 ".%.5" PRIi64 " - %.2" PRIi64 ".%.2" PRIi64 ".%.4" PRIi64 " %.2" PRIi64 ":%.2" PRIi64 ":%.2" PRIi64 "\n",
		dt->mjd, dt->submjd,
		dt->date.day, dt->date.month, dt->date.year,
		dt->hour, dt->minute, dt->second);
}
