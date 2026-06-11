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

/*
 * Unit tests for the date/time + calendar module:
 *   - bvn_gregorian_date.{c,h} : proleptic Gregorian <-> MJD, calendar helpers
 *   - bvn_datetime.{c,h}       : civil datetime <-> epoch seconds, TAI/UTC, DST
 *
 * The Gregorian core is cross-checked against a fully independent reference: a
 * day-by-day civil walk anchored at the well-established fact
 *   2000-01-01 = MJD 51544 = Saturday
 * advancing/retreating one day at a time with a month-length table and the
 * textbook civil leap-year rule.  This shares no arithmetic with Hinnant's
 * era algorithm under test, so an off-by-one or leap-handling slip in either
 * direction is caught.  The remaining sections pin behaviour with hand-verified
 * values (Easter dates, German holidays, banking days, the IERS leap-second
 * table, GNSS epochs, DST switch instants).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>

#include "bvn_datetime.h"
#include "bvn_gregorian_date.h"

static int g_fails = 0;
static int g_pass  = 0;

#define CHECK(cond, ...) do {                                    \
	if (!(cond)) {                                               \
		fprintf(stderr, "FAIL  line %d: ", __LINE__);           \
		fprintf(stderr, __VA_ARGS__);                           \
		fprintf(stderr, "\n");                                   \
		g_fails++;                                               \
	} else {                                                     \
		g_pass++;                                                \
	}                                                            \
} while (0)

/* ---- independent reference helpers (no shared math with the impl) -------- */

static int ref_leap(int64_t y)
{
	/* Multiples test against 0 only, so C's truncating % is correct for
	 * negative proleptic years too (-400 % 400 == 0, etc.). */
	return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

static int ref_mdays(int64_t y, int m)
{
	static const int mlen[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
	if (m == 2 && ref_leap(y))
		return 29;
	return mlen[m - 1];
}

static int ref_yday(int64_t y, int m, int d)
{
	int s = d - 1;
	for (int mm = 1; mm < m; mm++)
		s += ref_mdays(y, mm);
	return s;
}

/* ---- 1. Gregorian core: independent day-by-day differential walk -------- */

static void test_walk(void)
{
	/* forward from 2000-01-01 (Saturday) ~2700 years into the future */
	{
		int64_t y = 2000, mjd = 51544;
		int m = 1, d = 1, wday = 6; /* 6 == Saturday */
		for (long i = 0; i < 1000000; i++) {
			bvn_gregorian_date_t g;
			bvn_gregorian_date_from_mjd(&g, mjd);
			CHECK(g.year == y && g.month == m && g.day == d,
				"fwd ymd mjd=%lld got %lld-%lld-%lld want %lld-%d-%d",
				(long long)mjd, (long long)g.year, (long long)g.month,
				(long long)g.day, (long long)y, m, d);
			CHECK(g.wday == wday, "fwd wday mjd=%lld got %d want %d",
				(long long)mjd, g.wday, wday);
			CHECK(g.yday == ref_yday(y, m, d),
				"fwd yday %lld-%d-%d got %d want %d",
				(long long)y, m, d, g.yday, ref_yday(y, m, d));
			CHECK((g.leap != 0) == ref_leap(y),
				"fwd leap y=%lld got %d want %d",
				(long long)y, g.leap, ref_leap(y));
			CHECK(bvn_gregorian_date_to_mjd(&g) == mjd,
				"fwd to_mjd %lld-%d-%d", (long long)y, m, d);
			CHECK(bvn_gregorian_date_to_mjd_fast(&g) == mjd,
				"fwd fast %lld-%d-%d", (long long)y, m, d);
			/* advance one civil day */
			mjd++;
			wday = (wday + 1) % 7;
			if (++d > ref_mdays(y, m)) {
				d = 1;
				if (++m > 12) { m = 1; y++; }
			}
		}
	}
	/* backward from 2000-01-01 into proleptic negative years */
	{
		int64_t y = 2000, mjd = 51544;
		int m = 1, d = 1, wday = 6;
		for (long i = 0; i < 1000000; i++) {
			bvn_gregorian_date_t g;
			bvn_gregorian_date_from_mjd(&g, mjd);
			CHECK(g.year == y && g.month == m && g.day == d,
				"bwd ymd mjd=%lld got %lld-%lld-%lld want %lld-%d-%d",
				(long long)mjd, (long long)g.year, (long long)g.month,
				(long long)g.day, (long long)y, m, d);
			CHECK(g.wday == wday, "bwd wday mjd=%lld got %d want %d",
				(long long)mjd, g.wday, wday);
			CHECK(g.yday == ref_yday(y, m, d),
				"bwd yday %lld-%d-%d got %d", (long long)y, m, d, g.yday);
			CHECK((g.leap != 0) == ref_leap(y),
				"bwd leap y=%lld got %d", (long long)y, g.leap);
			CHECK(bvn_gregorian_date_to_mjd(&g) == mjd,
				"bwd to_mjd %lld-%d-%d", (long long)y, m, d);
			/* retreat one civil day */
			mjd--;
			wday = (wday + 6) % 7;
			if (--d < 1) {
				if (--m < 1) { m = 12; y--; }
				d = ref_mdays(y, m);
			}
		}
	}
}

/* ---- 2. fixed anchors (well-known dates) -------------------------------- */

static void test_anchors(void)
{
	struct { int64_t y; int m, d, mjd, wday; } a[] = {
		{ 1858, 11, 17,     0, 3 }, /* MJD epoch, Wednesday */
		{ 1970,  1,  1, 40587, 4 }, /* Unix epoch, Thursday */
		{ 2000,  1,  1, 51544, 6 }, /* Saturday             */
		{ 1900,  1,  1, 15020, 1 }, /* Monday (not a leap yr)*/
		{ 2024,  2, 29, 60369, 4 }, /* leap day, Thursday   */
	};
	for (size_t i = 0; i < sizeof(a)/sizeof(a[0]); i++) {
		bvn_gregorian_date_t g = { .year = a[i].y, .month = a[i].m,
		                           .day = a[i].d };
		int64_t mjd = bvn_gregorian_date_to_mjd(&g);
		CHECK(mjd == a[i].mjd, "anchor mjd %lld-%d-%d got %lld want %d",
			(long long)a[i].y, a[i].m, a[i].d, (long long)mjd, a[i].mjd);
		CHECK(bvn_gregorian_date_mjd_to_wday(mjd) == a[i].wday,
			"anchor wday %lld-%d-%d got %d want %d", (long long)a[i].y,
			a[i].m, a[i].d, bvn_gregorian_date_mjd_to_wday(mjd), a[i].wday);
	}
	/* 1900 is NOT a leap year (divisible by 100, not 400); 2000 IS. */
	{ bvn_gregorian_date_t g = { .year = 1900, .month = 2, .day = 28 };
	  CHECK(!bvn_gregorian_date_mjd_in_leap_year(bvn_gregorian_date_to_mjd(&g)),
		"1900 not leap"); }
	{ bvn_gregorian_date_t g = { .year = 2000, .month = 2, .day = 29 };
	  CHECK(bvn_gregorian_date_mjd_in_leap_year(bvn_gregorian_date_to_mjd(&g)),
		"2000 leap"); }
}

/* ---- 3. boundaries, overflow, NULL -------------------------------------- */

static void test_boundaries(void)
{
	int64_t b[] = { BVN_GDATE_MJD_MIN, BVN_GDATE_MJD_MIN + 1,
	                BVN_GDATE_MJD_MAX, BVN_GDATE_MJD_MAX - 1, 0, -1, 1 };
	for (size_t i = 0; i < sizeof(b)/sizeof(b[0]); i++) {
		bvn_gregorian_date_t g;
		bvn_gregorian_date_from_mjd(&g, b[i]);
		CHECK(bvn_gregorian_date_to_mjd(&g) == b[i],
			"boundary rt %lld", (long long)b[i]);
		CHECK(bvn_gregorian_date_to_mjd_fast(&g) == b[i],
			"boundary fast rt %lld", (long long)b[i]);
	}
	/* extreme year endpoints map to the MJD endpoints */
	{ bvn_gregorian_date_t g = { .year = BVN_GDATE_YEAR_MAX, .month = 12,
	                             .day = 31 };
	  CHECK(bvn_gregorian_date_to_mjd(&g) == BVN_GDATE_MJD_MAX,
		"year max dec31 -> mjd max"); }
	{ bvn_gregorian_date_t g = { .year = BVN_GDATE_YEAR_MIN, .month = 1,
	                             .day = 1 };
	  CHECK(bvn_gregorian_date_to_mjd(&g) == BVN_GDATE_MJD_MIN,
		"year min jan1 -> mjd min"); }
	/* overflow / invalid -> sentinel */
	{ bvn_gregorian_date_t g = { .year = BVN_GDATE_YEAR_MAX + 1, .month = 1,
	                             .day = 1 };
	  CHECK(bvn_gregorian_date_to_mjd(&g) == BVN_GDATE_OVF,
		"year max+1 overflow"); }
	CHECK(bvn_gregorian_date_to_mjd(NULL) == BVN_GDATE_OVF, "NULL -> OVF");
	/* out-of-range MJD leaves the record untouched and queries degrade */
	{ bvn_gregorian_date_t g = { .year = 1234, .month = 5, .day = 6 };
	  bvn_gregorian_date_from_mjd(&g, BVN_GDATE_MJD_MAX + 1);
	  CHECK(g.year == 1234 && g.month == 5 && g.day == 6,
		"oob from_mjd left record untouched"); }
	CHECK(bvn_gregorian_date_mjd_to_yday(BVN_GDATE_MJD_MAX + 1) == -1,
		"oob yday -> -1");
	CHECK(!bvn_gregorian_date_mjd_in_leap_year(BVN_GDATE_MJD_MIN - 1),
		"oob leap -> false");
}

/* ---- 4. normalize (denormalized inputs) --------------------------------- */

static void test_normalize(void)
{
	struct { int64_t y, m, d; int64_t ey, em, ed; } t[] = {
		{ 2024, 13,  1, 2025,  1,  1 }, /* month carry up   */
		{ 2024,  0,  1, 2023, 12,  1 }, /* month carry down */
		{ 2024,  1, 32, 2024,  2,  1 }, /* day carry up     */
		{ 2024,  3,  0, 2024,  2, 29 }, /* day 0 -> prev eom (leap) */
		{ 2023,  3,  0, 2023,  2, 28 }, /* non-leap eom     */
		{ 2024,  2, 30, 2024,  3,  1 }, /* Feb 30 -> Mar 1  */
		{ 2024, 25,  1, 2026,  1,  1 }, /* 2 years of months*/
	};
	for (size_t i = 0; i < sizeof(t)/sizeof(t[0]); i++) {
		bvn_gregorian_date_t g = { .year = t[i].y, .month = t[i].m,
		                           .day = t[i].d };
		CHECK(bvn_gregorian_date_normalize(&g), "normalize ok %zu", i);
		CHECK(g.year == t[i].ey && g.month == t[i].em && g.day == t[i].ed,
			"normalize %lld-%lld-%lld got %lld-%lld-%lld want %lld-%lld-%lld",
			(long long)t[i].y, (long long)t[i].m, (long long)t[i].d,
			(long long)g.year, (long long)g.month, (long long)g.day,
			(long long)t[i].ey, (long long)t[i].em, (long long)t[i].ed);
	}
}

/* ---- 5. Easter (Anonymous Gregorian algorithm, verified dates) ---------- */

static void test_easter(void)
{
	struct { int64_t y; int m, d; } e[] = {
		{ 1818, 3, 22 }, /* earliest in range of table */
		{ 1961, 4,  2 },
		{ 2000, 4, 23 },
		{ 2024, 3, 31 },
		{ 2025, 4, 20 },
		{ 2026, 4,  5 },
		{ 2038, 4, 25 }, /* latest possible date */
		{ 2285, 3, 22 }, /* earliest possible date */
	};
	for (size_t i = 0; i < sizeof(e)/sizeof(e[0]); i++) {
		bvn_gregorian_date_t g;
		CHECK(bvn_gregorian_date_calc_easter_sunday(&g, e[i].y),
			"easter calc %lld", (long long)e[i].y);
		CHECK(g.month == e[i].m && g.day == e[i].d,
			"easter %lld got %lld-%lld want %d-%d", (long long)e[i].y,
			(long long)g.month, (long long)g.day, e[i].m, e[i].d);
		CHECK(g.wday == 0, "easter is a Sunday %lld", (long long)e[i].y);
	}
	/* derived feasts for 2024 */
	bvn_gregorian_date_t gf, em, as, wm;
	bvn_gregorian_date_calc_good_friday(&gf, 2024);
	bvn_gregorian_date_calc_easter_monday(&em, 2024);
	bvn_gregorian_date_calc_ascension_day(&as, 2024);
	bvn_gregorian_date_calc_whit_monday(&wm, 2024);
	CHECK(gf.month == 3 && gf.day == 29, "good friday 2024");
	CHECK(em.month == 4 && em.day ==  1, "easter monday 2024");
	CHECK(as.month == 5 && as.day ==  9, "ascension 2024");
	CHECK(wm.month == 5 && wm.day == 20, "whit monday 2024");
	/* out-of-range year -> sentinel */
	CHECK(bvn_gregorian_date_easter_sunday_mjd(BVN_GDATE_YEAR_MAX + 1)
		== BVN_GDATE_OVF, "easter year oob -> OVF");
}

/* ---- 6. German holidays / banking / business days ----------------------- */

static int is_holiday(int64_t y, int m, int d)
{
	bvn_gregorian_date_t g = { .year = y, .month = m, .day = d };
	return bvn_gregorian_date_is_federal_holiday_in_germany(&g);
}

static void test_holidays(void)
{
	CHECK(is_holiday(2024,  1,  1), "Neujahr");
	CHECK(is_holiday(2024,  5,  1), "1. Mai");
	CHECK(is_holiday(2024, 10,  3), "Tag der Einheit");
	CHECK(is_holiday(2024, 12, 25), "1. Weihnachtstag");
	CHECK(is_holiday(2024, 12, 26), "2. Weihnachtstag");
	CHECK(is_holiday(2024,  3, 29), "Karfreitag (easter-2)");
	CHECK(is_holiday(2024,  4,  1), "Ostermontag (easter+1)");
	CHECK(is_holiday(2024,  5,  9), "Christi Himmelfahrt (easter+39)");
	CHECK(is_holiday(2024,  5, 20), "Pfingstmontag (easter+50)");
	CHECK(!is_holiday(2024,  7, 15), "ordinary weekday not a holiday");
	CHECK(!is_holiday(2024, 12, 24), "Christmas Eve not a federal holiday");

	/* banking days: weekends, Dec 24/31 and federal holidays are excluded */
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 12, .day = 24 };
	  CHECK(!bvn_gregorian_date_is_banking_day_in_germany(&g), "Dec24 not banking"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 12, .day = 31 };
	  CHECK(!bvn_gregorian_date_is_banking_day_in_germany(&g), "Dec31 not banking"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 7, .day = 15 }; /* Mon */
	  CHECK(bvn_gregorian_date_is_banking_day_in_germany(&g), "Jul15 banking"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 7, .day = 13 }; /* Sat */
	  CHECK(!bvn_gregorian_date_is_banking_day_in_germany(&g), "Sat not banking"); }

	/* business day: Saturday counts, only Sunday + holidays excluded */
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 7, .day = 13 }; /* Sat */
	  CHECK(bvn_gregorian_date_is_business_day_in_germany(&g), "Sat is business day"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 7, .day = 14 }; /* Sun */
	  CHECK(!bvn_gregorian_date_is_business_day_in_germany(&g), "Sun not business day"); }
}

static void test_banking_getters(void)
{
	/* December 2024: 28/29 weekend, 25/26 holidays, 24/31 excluded.
	 * last banking day  = Mon Dec 30, penultimate = Fri Dec 27. */
	bvn_gregorian_date_t dec = { .year = 2024, .month = 12, .day = 1 }, r;
	bvn_gregorian_date_get_last_banking_day_in_month(&r, &dec);
	CHECK(r.month == 12 && r.day == 30, "last bd Dec24 got %lld-%lld",
		(long long)r.month, (long long)r.day);
	bvn_gregorian_date_get_penultimate_banking_day_in_month(&r, &dec);
	CHECK(r.month == 12 && r.day == 27, "penult bd Dec24 got %lld-%lld",
		(long long)r.month, (long long)r.day);

	/* January 2025: Jan 1 holiday -> first banking day Thu Jan 2. */
	bvn_gregorian_date_t jan = { .year = 2025, .month = 1, .day = 1 };
	bvn_gregorian_date_get_first_banking_day_in_month(&r, &jan);
	CHECK(r.month == 1 && r.day == 2, "first bd Jan25 got %lld-%lld",
		(long long)r.month, (long long)r.day);

	/* mid banking day = first banking day on/after the 15th.
	 * 2024-06-15 is a Saturday -> Mon Jun 17. */
	bvn_gregorian_date_t jun = { .year = 2024, .month = 6, .day = 1 };
	bvn_gregorian_date_get_mid_banking_day_in_month(&r, &jun);
	CHECK(r.month == 6 && r.day == 17, "mid bd Jun24 got %lld-%lld",
		(long long)r.month, (long long)r.day);

	/* predicates agree with the getters */
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 12, .day = 30 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(bvn_gregorian_date_is_last_banking_day_in_month(&g), "is last bd"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 12, .day = 27 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(bvn_gregorian_date_is_penultimate_banking_day_in_month(&g), "is penult bd"); }
	{ bvn_gregorian_date_t g = { .year = 2025, .month = 1, .day = 2 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(bvn_gregorian_date_is_first_banking_day_in_month(&g), "is first bd"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 7, .day = 16 }; /* not 30 */
	  bvn_gregorian_date_normalize(&g);
	  CHECK(!bvn_gregorian_date_is_last_banking_day_in_month(&g), "not last bd"); }

	/* Predicates must judge the civil day a denormalized record normalizes to
	 * (header contract).  {2024,11,60} == 2024-12-30 (last banking day of Dec);
	 * {2024,13,2} == 2025-01-02 (first banking day of Jan). */
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 11, .day = 60 };
	  CHECK(bvn_gregorian_date_is_last_banking_day_in_month(&g),
		"denorm last bd in month"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 13, .day = 2 };
	  CHECK(bvn_gregorian_date_is_first_banking_day_in_month(&g),
		"denorm first bd in month"); }
	{ bvn_gregorian_date_t g = { .year = 2023, .month = 15, .day = 28 }; /* 2024-03-28 */
	  CHECK(bvn_gregorian_date_is_last_banking_day_in_quarter(1, &g),
		"denorm last bd in quarter"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 12, .day = 27 }; /* not last */
	  CHECK(!bvn_gregorian_date_is_last_banking_day_in_month(&g),
		"penult is not last bd"); }

	/* quarters: Q1 2024 ends Sun31/Sat30 weekend + Fri29 Good Friday -> Thu Mar 28 */
	bvn_gregorian_date_t y = { .year = 2024, .month = 1, .day = 1 };
	bvn_gregorian_date_get_last_banking_day_in_quarter(1, &r, &y);
	CHECK(r.month == 3 && r.day == 28, "last bd Q1 got %lld-%lld",
		(long long)r.month, (long long)r.day);
	bvn_gregorian_date_get_first_banking_day_in_quarter(1, &r, &y);
	CHECK(r.month == 1 && r.day == 2, "first bd Q1 got %lld-%lld",
		(long long)r.month, (long long)r.day);
	bvn_gregorian_date_get_last_banking_day_in_quarter(4, &r, &y);
	CHECK(r.month == 12 && r.day == 30, "last bd Q4 got %lld-%lld",
		(long long)r.month, (long long)r.day);
	/* invalid quarter leaves res untouched */
	{ bvn_gregorian_date_t res = { .year = 7, .month = 7, .day = 7 };
	  bvn_gregorian_date_get_last_banking_day_in_quarter(5, &res, &y);
	  CHECK(res.year == 7 && res.month == 7 && res.day == 7,
		"invalid quarter leaves res untouched"); }
	CHECK(!bvn_gregorian_date_is_last_banking_day_in_quarter(0, &y),
		"quarter 0 predicate false");

	/* last sunday/saturday of March 2024 = 31 (Sun) / 30 (Sat) */
	bvn_gregorian_date_t mar = { .year = 2024, .month = 3, .day = 1 };
	bvn_gregorian_date_get_last_sunday_in_month(&r, &mar);
	CHECK(r.day == 31 && r.wday == 0, "last sun Mar24 got %lld", (long long)r.day);
	bvn_gregorian_date_get_last_saturday_in_month(&r, &mar);
	CHECK(r.day == 30 && r.wday == 6, "last sat Mar24 got %lld", (long long)r.day);
}

static void test_dst_predicates(void)
{
	/* EU DST switches on the last Sunday of March / October. */
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 3, .day = 31 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(bvn_gregorian_date_is_daylight_saving_switch_in_germany(&g),
		"Mar31-2024 is a DST switch"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 10, .day = 27 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(bvn_gregorian_date_is_daylight_saving_switch_in_germany(&g),
		"Oct27-2024 is a DST switch"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 3, .day = 24 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(!bvn_gregorian_date_is_daylight_saving_switch_in_germany(&g),
		"Mar24-2024 (Sun, not last) not a switch"); }
	{ bvn_gregorian_date_t g = { .year = 2024, .month = 3, .day = 30 };
	  bvn_gregorian_date_normalize(&g);
	  CHECK(bvn_gregorian_date_is_day_before_daylight_saving_switch_in_germany(&g),
		"Mar30-2024 is the day before the switch"); }
}

/* ---- 7. epoch seconds <-> datetime -------------------------------------- */

static void test_epoch_seconds(void)
{
	/* round-trip across a wide span including pre-epoch (negative) instants */
	for (int64_t s = -5000000000LL; s < 5000000000LL; s += 1234567) {
		bvn_datetime_t dt;
		memset(&dt, 0, sizeof dt);
		bvn_dt_epoch_seconds_to_datetime(&dt, bvn_epoch_unix, s);
		int64_t back = bvn_dt_datetime_to_epoch_seconds(&dt, bvn_epoch_unix);
		CHECK(back == s, "epoch unix rt s=%lld back=%lld",
			(long long)s, (long long)back);
		/* h:m:s within range */
		CHECK(dt.hour >= 0 && dt.hour < 24 && dt.minute >= 0
			&& dt.minute < 60 && dt.second >= 0 && dt.second < 60,
			"hms in range s=%lld -> %lld:%lld:%lld", (long long)s,
			(long long)dt.hour, (long long)dt.minute, (long long)dt.second);
		CHECK(dt.submjd >= 0 && dt.submjd <= 99999,
			"submjd in range s=%lld got %lld", (long long)s,
			(long long)dt.submjd);
	}
	/* a concrete instant: 2024-06-11 13:45:30 UTC (unix epoch) */
	{
		bvn_datetime_t dt = { .date = { .year = 2024, .month = 6, .day = 11 },
		                      .hour = 13, .minute = 45, .second = 30 };
		int64_t s = bvn_dt_datetime_to_epoch_seconds(&dt, bvn_epoch_unix);
		bvn_datetime_t r;
		memset(&r, 0, sizeof r);
		bvn_dt_epoch_seconds_to_datetime(&r, bvn_epoch_unix, s);
		CHECK(r.date.year == 2024 && r.date.month == 6 && r.date.day == 11
			&& r.hour == 13 && r.minute == 45 && r.second == 30,
			"concrete instant rt");
		CHECK(bvn_dt_is_equal(&dt, &r), "bvn_dt_is_equal after rt");
	}
	/* time-field rollover (mktime-style) */
	{
		bvn_datetime_t a = { .date = { .year = 2024, .month = 1, .day = 1 },
		                     .hour = 25 };
		bvn_datetime_t b = { .date = { .year = 2024, .month = 1, .day = 2 },
		                     .hour = 1 };
		CHECK(bvn_dt_datetime_to_epoch_seconds(&a, bvn_epoch_unix)
			== bvn_dt_datetime_to_epoch_seconds(&b, bvn_epoch_unix),
			"hour=25 rolls into next day");
	}
	{
		bvn_datetime_t a = { .date = { .year = 2024, .month = 1, .day = 2 },
		                     .hour = -1 };
		bvn_datetime_t b = { .date = { .year = 2024, .month = 1, .day = 1 },
		                     .hour = 23 };
		CHECK(bvn_dt_datetime_to_epoch_seconds(&a, bvn_epoch_unix)
			== bvn_dt_datetime_to_epoch_seconds(&b, bvn_epoch_unix),
			"hour=-1 rolls back a day");
	}
	/* convert_epoch_seconds relabels origins: unix 0 in TAI-epoch seconds */
	CHECK(bvn_dt_convert_epoch_seconds(bvn_epoch_unix, bvn_epoch_tai, 0)
		== (int64_t)(bvn_epoch_unix - bvn_epoch_tai) * 86400,
		"convert unix->tai at 0");
	CHECK(bvn_dt_convert_epoch_seconds(bvn_epoch_tai, bvn_epoch_unix, 0)
		== (int64_t)(bvn_epoch_tai - bvn_epoch_unix) * 86400,
		"convert tai->unix at 0");
}

/* ---- 8. TAI <-> UTC leap seconds ---------------------------------------- */

static void test_tai_utc(void)
{
	/* TAI-UTC at 2017-01-01 00:00:00 UTC is 37 s. */
	{
		bvn_datetime_t dt = { .date = { .year = 2017, .month = 1, .day = 1 } };
		int64_t tai = bvn_dt_utc_to_tai_seconds(&dt);
		int64_t utc = bvn_dt_datetime_to_epoch_seconds(&dt, bvn_epoch_tai);
		CHECK(tai - utc == 37, "TAI-UTC 2017 = %lld", (long long)(tai - utc));
	}
	/* TAI-UTC at 1972-01-01 is 10 s (start of the leap-second era). */
	{
		bvn_datetime_t dt = { .date = { .year = 1972, .month = 1, .day = 1 } };
		int64_t tai = bvn_dt_utc_to_tai_seconds(&dt);
		int64_t utc = bvn_dt_datetime_to_epoch_seconds(&dt, bvn_epoch_tai);
		CHECK(tai - utc == 10, "TAI-UTC 1972 = %lld", (long long)(tai - utc));
	}
	/* UTC -> TAI -> UTC round-trips on whole-minute instants away from the
	 * leap-second collapse (June 30 / Dec 31 23:59:60). */
	for (int64_t s = 441763200; s < 2000000000LL; s += 86400 * 30 + 137) {
		bvn_datetime_t dt;
		memset(&dt, 0, sizeof dt);
		bvn_dt_epoch_seconds_to_datetime(&dt, bvn_epoch_tai, s);
		int64_t tai = bvn_dt_utc_to_tai_seconds(&dt);
		bvn_datetime_t u;
		memset(&u, 0, sizeof u);
		bvn_dt_tai_seconds_to_utc(&u, tai);
		int64_t back = bvn_dt_datetime_to_epoch_seconds(&u, bvn_epoch_tai);
		CHECK(back == s, "tai/utc rt s=%lld back=%lld",
			(long long)s, (long long)back);
	}
	/* local time: 2024-07-01 12:00 UTC with +1h zone and +1h DST -> 14:00 */
	{
		bvn_datetime_t utc = { .date = { .year = 2024, .month = 7, .day = 1 },
		                       .hour = 12 };
		int64_t tai = bvn_dt_utc_to_tai_seconds(&utc);
		bvn_datetime_t loc;
		memset(&loc, 0, sizeof loc);
		bvn_dt_tai_seconds_to_local_time(&loc, tai, 3600, 1);
		CHECK(loc.date.month == 7 && loc.date.day == 1 && loc.hour == 14,
			"local time CEST got %lld-%lld %lld:00", (long long)loc.date.month,
			(long long)loc.date.day, (long long)loc.hour);
	}
	/* Regression: the leap-second offset must come from the true instant,
	 * not from the timezone-shifted value.  At 2016-12-31 23:59:59 UTC
	 * (offset 36) an observer at UTC+1h reads 2017-01-01 00:59:59 local;
	 * a naive shift-then-lookup would cross the 2017 boundary (offset 37)
	 * and render 00:59:58. */
	{
		bvn_datetime_t utc = { .date = { .year = 2016, .month = 12, .day = 31 },
		                       .hour = 23, .minute = 59, .second = 59 };
		int64_t tai = bvn_dt_utc_to_tai_seconds(&utc);
		bvn_datetime_t loc;
		memset(&loc, 0, sizeof loc);
		bvn_dt_tai_seconds_to_local_time(&loc, tai, 3600, 0);
		CHECK(loc.date.year == 2017 && loc.date.month == 1 && loc.date.day == 1
			&& loc.hour == 0 && loc.minute == 59 && loc.second == 59,
			"local time across leap boundary got %lld-%lld-%lld %lld:%lld:%lld",
			(long long)loc.date.year, (long long)loc.date.month,
			(long long)loc.date.day, (long long)loc.hour,
			(long long)loc.minute, (long long)loc.second);
	}
	/* Local time with a zero offset must match plain UTC rendering. */
	{
		bvn_datetime_t a, b;
		memset(&a, 0, sizeof a);
		memset(&b, 0, sizeof b);
		int64_t tai = 1500000000;
		bvn_dt_tai_seconds_to_utc(&a, tai);
		bvn_dt_tai_seconds_to_local_time(&b, tai, 0, 0);
		CHECK(bvn_dt_is_equal(&a, &b), "local time tz=0 matches UTC");
	}
}

/* ---- 9. DST window via TAI instants ------------------------------------- */

static void test_dst_window(void)
{
	/* 2024: DST on at Mar 31 01:00 UTC, off at Oct 27 01:00 UTC. */
	bvn_datetime_t on  = { .date = { .year = 2024, .month = 3, .day = 31 },
	                       .hour = 1 };
	bvn_datetime_t off = { .date = { .year = 2024, .month = 10, .day = 27 },
	                       .hour = 1 };
	int64_t t_on  = bvn_dt_utc_to_tai_seconds(&on);
	int64_t t_off = bvn_dt_utc_to_tai_seconds(&off);
	CHECK(bvn_dt_tai_second_is_dls_in_europe(t_on) == 1, "DST on at boundary");
	CHECK(bvn_dt_tai_second_is_dls_in_europe(t_on - 1) == 0, "DST off just before on");
	CHECK(bvn_dt_tai_second_is_dls_in_europe(t_off) == 0, "DST off at boundary");
	CHECK(bvn_dt_tai_second_is_dls_in_europe(t_off - 1) == 1, "DST on just before off");
	/* deep summer / deep winter */
	{ bvn_datetime_t s = { .date = { .year = 2024, .month = 7, .day = 1 } };
	  CHECK(bvn_dt_tai_second_is_dls_in_europe(bvn_dt_utc_to_tai_seconds(&s)) == 1,
		"July is DST"); }
	{ bvn_datetime_t w = { .date = { .year = 2024, .month = 1, .day = 15 } };
	  CHECK(bvn_dt_tai_second_is_dls_in_europe(bvn_dt_utc_to_tai_seconds(&w)) == 0,
		"January is not DST"); }
	/* int64 extremes resolve to "not DST" rather than leaking the sentinel */
	CHECK(bvn_dt_tai_second_is_dls_in_europe(INT64_MAX) == 0, "INT64_MAX not DST");
	CHECK(bvn_dt_tai_second_is_dls_in_europe(INT64_MIN) == 0, "INT64_MIN not DST");
}

/* ---- 10. GNSS week/time-of-week ----------------------------------------- */

static void test_gnss(void)
{
	/* GPS week 0, tow 0 == 1980-01-06 (GPS epoch). */
	{
		int64_t s = bvn_dt_epoch_seconds_from_gps_time(0, 0, bvn_epoch_unix);
		bvn_datetime_t dt;
		memset(&dt, 0, sizeof dt);
		bvn_dt_epoch_seconds_to_datetime(&dt, bvn_epoch_unix, s);
		CHECK(dt.date.year == 1980 && dt.date.month == 1 && dt.date.day == 6,
			"GPS wk0 -> %lld-%lld-%lld", (long long)dt.date.year,
			(long long)dt.date.month, (long long)dt.date.day);
	}
	/* Galileo week 0, tow 0 == 1999-08-22 (Galileo epoch). */
	{
		int64_t s = bvn_dt_epoch_seconds_from_galileo_time(0, 0, bvn_epoch_unix);
		bvn_datetime_t dt;
		memset(&dt, 0, sizeof dt);
		bvn_dt_epoch_seconds_to_datetime(&dt, bvn_epoch_unix, s);
		CHECK(dt.date.year == 1999 && dt.date.month == 8 && dt.date.day == 22,
			"Galileo wk0 -> %lld-%lld-%lld", (long long)dt.date.year,
			(long long)dt.date.month, (long long)dt.date.day);
	}
	/* GPS->TAI runs 19 s ahead of the plain TAI-epoch relabeling. */
	{
		int64_t tai   = bvn_dt_tai_seconds_from_gps_time(0, 0);
		int64_t plain = bvn_dt_epoch_seconds_from_gps_time(0, 0, bvn_epoch_tai);
		CHECK(tai == plain, "gps->tai helper consistent");
		int64_t unixs = bvn_dt_epoch_seconds_from_gps_time(0, 0, bvn_epoch_unix);
		int64_t unix_in_tai =
			bvn_dt_convert_epoch_seconds(bvn_epoch_unix, bvn_epoch_tai, unixs);
		CHECK(tai - unix_in_tai == 19, "gps tai is +19s, got %lld",
			(long long)(tai - unix_in_tai));
	}
	/* time-of-week milliseconds round to the nearest second */
	{
		int64_t a = bvn_dt_epoch_seconds_from_gps_time(1499, 0, bvn_epoch_gps);
		int64_t b = bvn_dt_epoch_seconds_from_gps_time(1501, 0, bvn_epoch_gps);
		CHECK(a == 1 && b == 2, "tow ms rounding a=%lld b=%lld",
			(long long)a, (long long)b);
	}
}

/* ---- 11. equality helpers ----------------------------------------------- */

static void test_equality(void)
{
	bvn_datetime_t a = { .date = { .year = 2024, .month = 6, .day = 11 },
	                     .hour = 1, .minute = 2, .second = 3 };
	bvn_datetime_t b = a;
	CHECK(bvn_dt_is_equal(&a, &b), "identical equal");
	b.mjd = 12345; b.submjd = 678; /* cache members are ignored */
	CHECK(bvn_dt_is_equal(&a, &b), "cache members ignored by equality");
	b.second = 4;
	CHECK(!bvn_dt_is_time_equal(&a, &b), "time differs");
	CHECK(bvn_dt_is_date_equal(&a, &b), "date still equal");
	CHECK(!bvn_dt_is_equal(&a, &b), "overall not equal");

	bvn_gregorian_date_t g1, g2;
	bvn_gregorian_date_from_mjd(&g1, 51544);
	bvn_gregorian_date_from_mjd(&g2, 51544);
	CHECK(bvn_gregorian_date_is_equal(&g1, &g2), "gdate equal");
	bvn_gregorian_date_from_mjd(&g2, 51545);
	CHECK(!bvn_gregorian_date_is_equal(&g1, &g2), "gdate not equal");
}

int main(void)
{
	test_walk();
	test_anchors();
	test_boundaries();
	test_normalize();
	test_easter();
	test_holidays();
	test_banking_getters();
	test_dst_predicates();
	test_epoch_seconds();
	test_tai_utc();
	test_dst_window();
	test_gnss();
	test_equality();

	printf("\n%s  %d passed, %d failed\n",
	       g_fails ? "FAIL" : "PASS", g_pass, g_fails);
	return g_fails ? 1 : 0;
}
