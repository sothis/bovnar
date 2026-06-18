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

#ifndef BVN_DATETIME_H_
#define BVN_DATETIME_H_

#include "bvn_gregorian_date.h"
#include <stdint.h>
#ifndef BVN_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define BVN_API  /* DLL symbols exported via CMake WINDOWS_EXPORT_ALL_SYMBOLS */
#  elif defined(__GNUC__) || defined(__clang__)
#    define BVN_API __attribute__((visibility("default")))
#  else
#    define BVN_API
#  endif
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Civil date plus time of day.
 *
 * The mjd and submjd members are caches: mjd is the Modified Julian Day of
 * `date`, submjd the time of day expressed in 1e-5 day units.  They are
 * populated by bvn_dt_epoch_seconds_to_datetime() / bvn_dt_tai_seconds_to_*();
 * bvn_dt_datetime_to_epoch_seconds() refreshes mjd.  They are output-only and
 * never read back; the equality functions deliberately ignore them. */
typedef struct bvn_datetime_t {
	bvn_gregorian_date_t date;
	int64_t hour;
	int64_t minute;
	int64_t second;
	int64_t mjd;
	int64_t submjd;
} bvn_datetime_t;

/* Modified Julian Dates for several epochs, marking the begin of an epoch at
 * the start of the day at 00:00:00.
 * Used for monotonic time scale calculations such as NTP, UNIX or TAI
 * in which every day consists of exactly 86400 SI seconds. */
typedef enum bvn_epoch_t {
	bvn_epoch_mjd		= 0,     /* MJD 17.11.1858 00:00:00 */
	bvn_epoch_ntp		= 15020, /* MJD 01.01.1900 00:00:00 */
	bvn_epoch_tai		= 36204, /* MJD 01.01.1958 00:00:00 */
	/* DCF77 (MJD 36569) and UTC (MJD 41317) are intentionally omitted: they are
	 * not monotonic time scales and so have no fixed-length-day epoch here. */
	bvn_epoch_unix		= 40587, /* MJD 01.01.1970 00:00:00 */
	bvn_epoch_gps		= 44244, /* MJD 06.01.1980 00:00:00 */
	bvn_epoch_glonass	= 50083, /* MJD 01.01.1996 00:00:00 */
	bvn_epoch_galileo	= 51412, /* MJD 22.08.1999 00:00:00 */
	bvn_epoch_2000		= 51544, /* MJD 01.01.2000 00:00:00 */
	bvn_epoch_beidou	= 53736  /* MJD 01.01.2006 00:00:00 */
} bvn_epoch_t;


/* Civil datetime <-> uniform epoch seconds (86400 s/day, no leap seconds).
 *
 * bvn_dt_datetime_to_epoch_seconds() normalizes dt->date in place (see
 * bvn_gregorian_date_normalize()) and refreshes dt->mjd.  The hour, minute
 * and second fields are not range-checked: out-of-range values roll over
 * into adjacent days, mktime-style.  Returns BVN_GDATE_OVF if the date is
 * out of range or the result would overflow; a successful conversion never
 * returns that value (INT64_MIN is reserved as the error sentinel).
 *
 * bvn_dt_epoch_seconds_to_datetime() accepts negative seconds (instants
 * before the epoch) and fills all fields with floor semantics. */
BVN_API int64_t bvn_dt_datetime_to_epoch_seconds(bvn_datetime_t* dt, bvn_epoch_t epoch);
BVN_API void bvn_dt_epoch_seconds_to_datetime(bvn_datetime_t* dt, bvn_epoch_t epoch, int64_t seconds);

/* Relabel a uniform-scale timestamp from one epoch origin to another.
 * Returns BVN_GDATE_OVF when the shifted value does not fit in int64_t. */
BVN_API int64_t bvn_dt_convert_epoch_seconds(bvn_epoch_t epoch_from, bvn_epoch_t epoch_to, int64_t seconds);

/* GNSS week + time-of-week (milliseconds, rounded to the nearest second)
 * to uniform epoch seconds.  Returns BVN_GDATE_OVF on overflow.
 *
 * The result stays in the GNSS timescale: GPS and Galileo system time run
 * at a constant 19 s behind TAI and carry no leap seconds, so for any epoch
 * other than bvn_epoch_tai the returned value is ahead of the corresponding
 * UTC-based wall-clock timestamp by (TAI-UTC) - 19 s (18 s since 2017).
 * For civil UTC convert via bvn_epoch_tai and bvn_dt_tai_seconds_to_utc(). */
BVN_API int64_t bvn_dt_epoch_seconds_from_gps_time(int64_t timeofweek_ms, int64_t gps_week, bvn_epoch_t epoch);
BVN_API int64_t bvn_dt_tai_seconds_from_gps_time(int64_t timeofweek_ms, int64_t gps_week);

BVN_API int64_t bvn_dt_epoch_seconds_from_galileo_time(int64_t timeofweek_ms, int64_t galileo_week, bvn_epoch_t epoch);
BVN_API int64_t bvn_dt_tai_seconds_from_galileo_time(int64_t timeofweek_ms, int64_t galileo_week);


/* UTC <-> TAI with the full IERS leap-second table (1972 .. 2017+).
 * Instants before 1972 predate leap seconds and are approximated with the
 * initial 10 s offset.  The inserted leap second itself (23:59:60) is not
 * representable: it collapses onto 00:00:00 of the following UTC day.
 *
 * bvn_dt_utc_to_tai_seconds() converts via bvn_dt_datetime_to_epoch_seconds()
 * and therefore normalizes dt->date in place and refreshes dt->mjd; it
 * returns BVN_GDATE_OVF on out-of-range input or overflow.
 * bvn_dt_tai_seconds_to_utc() and bvn_dt_tai_seconds_to_local_time() leave
 * *dt untouched when the offset/timezone adjustment would overflow int64_t
 * (only possible within a few tens of seconds of INT64_MIN/INT64_MAX). */
BVN_API int64_t bvn_dt_utc_to_tai_seconds(bvn_datetime_t* dt);
BVN_API void bvn_dt_tai_seconds_to_utc(bvn_datetime_t* dt, int64_t tai_seconds);
BVN_API void bvn_dt_tai_seconds_to_local_time(bvn_datetime_t* dt, int64_t tai_seconds, int64_t timezone_offset_seconds, int dls_active);


/* Field-wise comparison of the civil representation (date resp. h:m:s).
 * The mjd/submjd cache members are ignored. */
BVN_API int bvn_dt_is_date_equal(bvn_datetime_t* a, bvn_datetime_t* b);
BVN_API int bvn_dt_is_time_equal(bvn_datetime_t* a, bvn_datetime_t* b);
BVN_API int bvn_dt_is_equal(bvn_datetime_t* a, bvn_datetime_t* b);

/* Returns 1 if the TAI instant falls inside European daylight saving time
 * (last Sunday of March 01:00 UTC .. last Sunday of October 01:00 UTC),
 * 0 otherwise — including when the instant cannot be resolved (int64
 * extremes). */
BVN_API int bvn_dt_tai_second_is_dls_in_europe(int64_t tai_second);

BVN_API void bvn_dt_dump(bvn_datetime_t* dt);
BVN_API void bvn_dt_print(bvn_datetime_t* dt);

#ifdef __cplusplus
}
#endif
#endif /* BVN_DATETIME_H_ */
