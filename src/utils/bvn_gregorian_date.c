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

#include "bvn_gregorian_date.h"
#include "bvn_date_impl.h"

typedef struct {
	int64_t era;
	int64_t yoe;
	int64_t doy;
	int64_t m;
} gdate_parts_t;

/* Intended for internal use only. */
static inline bool mjd_decompose(int64_t mjd, gdate_parts_t* p)
{
	if ((mjd < BVN_GDATE_MJD_MIN) || (mjd > BVN_GDATE_MJD_MAX))
		return false;

	int64_t shifted = mjd + BVN_MJD_OFFSET;
	int64_t era = floor_div(shifted, 146097);
	int64_t doe = shifted - era * 146097;
	int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
	int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);

	p->era = era;
	p->yoe = yoe;
	p->doy = doy;
	p->m   = (5 * doy + 2) / 153;
	return true;
}

/* Intended for internal use only. Only valid for non-negative inputs.
 * Do not use as generic leap year check. */
static inline int64_t yoe_is_leap(int64_t yoe_cal)
{
	return (!(yoe_cal & 3)) & ((!!(yoe_cal % 100)) | !yoe_cal);
}

int64_t bvn_gregorian_date_to_mjd(const bvn_gregorian_date_t* gdate)
{
	if (!gdate)
		return BVN_GDATE_OVF;

	int64_t y = gdate->year, m, yy;
	bool ov = false;

	ov |= bvn_ckd_sub(&m, gdate->month, (int64_t)1);
	ov |= bvn_ckd_add(&yy, y, floor_div(m, 12));
	int64_t mm = floor_mod(m, 12) + 1;
	int64_t is_jf = (mm < 3);
	ov |= bvn_ckd_sub(&yy, yy, is_jf);
	mm += 12*is_jf - 3;

	int64_t era = floor_div(yy, 400);
	int64_t era400, yoe, t, r;
	ov |= bvn_ckd_mul(&era400, era, (int64_t)400);
	ov |= bvn_ckd_sub(&yoe, yy, era400);
	ov |= bvn_ckd_mul(&r, era, (int64_t)146097);
	ov |= bvn_ckd_mul(&t, yoe, (int64_t)365);
	ov |= bvn_ckd_add(&r, r, t);
	ov |= bvn_ckd_add(&r, r, yoe / 4);
	ov |= bvn_ckd_sub(&r, r, yoe / 100);
	ov |= bvn_ckd_add(&r, r, (153*mm + 2) / 5);
	ov |= bvn_ckd_add(&r, r, gdate->day);
	ov |= bvn_ckd_sub(&r, r, (int64_t)1);
	ov |= bvn_ckd_sub(&r, r, (int64_t)BVN_MJD_OFFSET);
	ov |= (r < BVN_GDATE_MJD_MIN) | (r > BVN_GDATE_MJD_MAX);
	return ov ? BVN_GDATE_OVF : r;
}

/* MJD of (y, m, d) */
static inline int64_t ymd_to_mjd(int64_t y, int64_t m, int64_t d)
{
    bvn_gregorian_date_t t = { .year = y, .month = m, .day = d };
    return bvn_gregorian_date_to_mjd(&t);
}

/* MJD of the last day of (y, m) == MJD of day 0 of (y, m+1). */
static inline int64_t eom_mjd(int64_t y, int64_t m)
{
    int64_t m1;
    if (bvn_ckd_add(&m1, m, (int64_t)1))
        return BVN_GDATE_OVF;
    return ymd_to_mjd(y, m1, 0);
}

static bool banking_day_canon(bvn_gregorian_date_t* c);

typedef bool (*date_pred_t)(bvn_gregorian_date_t*);
/* Returns false when the walk hits the supported MJD range boundary before
 * the predicate is satisfied (stepping further would make
 * bvn_gregorian_date_from_mjd() a no-op, leaving d stale and looping
 * forever). */
static bool walk_until(bvn_gregorian_date_t* d, int64_t* mjd,
                       int step, date_pred_t pred)
{
    while (!pred(d)) {
        int64_t next = *mjd + step;
        if (next < BVN_GDATE_MJD_MIN || next > BVN_GDATE_MJD_MAX)
            return false;
        *mjd = next;
        bvn_gregorian_date_from_mjd(d, next);
    }
    return true;
}

static bool pred_banking (bvn_gregorian_date_t* d) { return banking_day_canon(d); }
static bool pred_sunday  (bvn_gregorian_date_t* d) { return d->wday == 0; }
static bool pred_saturday(bvn_gregorian_date_t* d) { return d->wday == 6; }

static inline bool same_day(const bvn_gregorian_date_t* a,
                            const bvn_gregorian_date_t* b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day;
}

int64_t bvn_gregorian_date_to_mjd_fast(const bvn_gregorian_date_t* gdate)
{
	int64_t y = gdate->year;
	int64_t m = gdate->month - 1;
	int64_t is_jan_feb;

	y += floor_div(m, 12);
	m  = floor_mod(m, 12) + 1;

	is_jan_feb = (int64_t)((uint64_t)(m - 3) >> 63);
	y -= is_jan_feb;
	m += 12 * is_jan_feb - 3;

	int64_t era = floor_div(y, 400);
	int64_t yoe = y - era * 400;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + (153 * m + 2) / 5
			+ gdate->day - 1 - BVN_MJD_OFFSET;
}

int32_t bvn_gregorian_date_mjd_to_wday(int64_t mjd)
{
	int64_t r = floor_mod(mjd, 7) + 3;
	return (int32_t)(r >= 7 ? r - 7 : r);
}

bool bvn_gregorian_date_mjd_in_leap_year(int64_t mjd)
{
	gdate_parts_t p;
	if (!mjd_decompose(mjd, &p))
		return false;

	int64_t yoe_cal = p.yoe + (p.m >= 10);
	yoe_cal -= 400 & -(int64_t)(yoe_cal == 400);
	return (bool)yoe_is_leap(yoe_cal);
}

int32_t bvn_gregorian_date_mjd_to_yday(int64_t mjd)
{
	gdate_parts_t p;
	if (!mjd_decompose(mjd, &p))
		return -1;

	int64_t is_jan_feb = (int64_t)(p.m >= 10);
	int64_t leap = yoe_is_leap(p.yoe);
	return (int32_t)(p.doy + 59 + leap - is_jan_feb * (365 + leap));
}

void bvn_gregorian_date_from_mjd(bvn_gregorian_date_t* gdate, int64_t mjd)
{
	if (!gdate)
		return;

	gdate_parts_t p;
	if (!mjd_decompose(mjd, &p))
		return;

	int64_t is_jan_feb = (int64_t)(p.m >= 10);
	int64_t yoe_cal = p.yoe + is_jan_feb;

	yoe_cal -= 400 & -(int64_t)(yoe_cal == 400);

	gdate->day   = p.doy - (153 * p.m + 2) / 5 + 1;
	gdate->month = p.m + 3 - 12 * is_jan_feb;
	gdate->year  = p.era * 400 + p.yoe + is_jan_feb;
	gdate->wday  = bvn_gregorian_date_mjd_to_wday(mjd);

	int64_t leap_yoe = yoe_is_leap(p.yoe);
	gdate->yday  = (int32_t)(p.doy + 59 + leap_yoe - is_jan_feb
							* (365 + leap_yoe));
	gdate->leap  = (int32_t)yoe_is_leap(yoe_cal);
}

bool bvn_gregorian_date_normalize(bvn_gregorian_date_t* gdate)
{
	int64_t mjd = bvn_gregorian_date_to_mjd(gdate);
	if (mjd == BVN_GDATE_OVF) {
		return false;
	}

	bvn_gregorian_date_from_mjd(gdate, mjd);
	return true;
}

int64_t bvn_gregorian_date_easter_sunday_mjd(int64_t year)
{
	if ((year < BVN_GDATE_YEAR_MIN) || (year > BVN_GDATE_YEAR_MAX)) {
		return BVN_GDATE_OVF;
	}

	int64_t a = floor_mod(year, 19);
	int64_t b = floor_div(year, 100);
	int64_t c = floor_mod(year, 100);
	int64_t d = floor_div(b, 4);
	int64_t e = floor_mod(b, 4);
	int64_t f = floor_div(b + 8, 25);
	int64_t g = floor_div(b - f + 1, 3);
	int64_t h = floor_mod(19*a + b - d - g + 15, 30);
	int64_t i = floor_div(c, 4);
	int64_t k = floor_mod(c, 4);
	int64_t l = floor_mod(32 + 2*e + 2*i - h - k, 7);
	int64_t m = floor_div(a + 11*h + 22*l, 451);
	int64_t n = h + l - 7*m + 114;

	bvn_gregorian_date_t tmp = {
		.year  = year,
		.month = n / 31,
		.day   = (n % 31) + 1,
	};

	return bvn_gregorian_date_to_mjd_fast(&tmp);
}

/** higher level calendar functions **/

static bool easter_plus(bvn_gregorian_date_t* g, int64_t year, int64_t offset)
{
    if (!g) return false;
    int64_t mjd = bvn_gregorian_date_easter_sunday_mjd(year);
    if (mjd == BVN_GDATE_OVF) return false;
    /* Easter sits well inside the MJD range, but a positive offset (up to +50
     * for Whit Monday) can push past BVN_GDATE_MJD_MAX in the last supported
     * year. bvn_gregorian_date_from_mjd() is a no-op out of range, which would
     * otherwise leave *g stale yet still return true. (mjd + offset cannot
     * overflow int64: mjd is a valid in-range MJD and |offset| <= 50.) */
    int64_t target = mjd + offset;
    if (target < BVN_GDATE_MJD_MIN || target > BVN_GDATE_MJD_MAX)
        return false;
    bvn_gregorian_date_from_mjd(g, target);
    return true;
}

bool bvn_gregorian_date_calc_easter_sunday (bvn_gregorian_date_t* g, int64_t y) { return easter_plus(g, y,  0); }
bool bvn_gregorian_date_calc_good_friday   (bvn_gregorian_date_t* g, int64_t y) { return easter_plus(g, y, -2); }
bool bvn_gregorian_date_calc_easter_monday (bvn_gregorian_date_t* g, int64_t y) { return easter_plus(g, y,  1); }
bool bvn_gregorian_date_calc_ascension_day (bvn_gregorian_date_t* g, int64_t y) { return easter_plus(g, y, 39); }
bool bvn_gregorian_date_calc_whit_monday   (bvn_gregorian_date_t* g, int64_t y) { return easter_plus(g, y, 50); }

/* Normalize *d into *c so the predicates below always see one canonical
 * record; every check (fixed-date, weekday, easter-relative) then agrees
 * on the same civil day.  Returns false for out-of-range input. */
static bool gdate_canonicalize(bvn_gregorian_date_t* c, const bvn_gregorian_date_t* d)
{
    *c = *d;
    return bvn_gregorian_date_normalize(c);
}

/* The *_canon helpers require a canonical record (as produced by
 * bvn_gregorian_date_from_mjd()/_normalize()); the public wrappers
 * canonicalize first, the walk predicates call them directly. */
/* The fixed-date holidays are not timeless, and applying today's set to every
 * year gave plainly wrong answers: 1985-10-03 was reported as a holiday six
 * years before the day existed, while 1985-06-17 -- the national holiday then in
 * force -- was not. The three datable rules are gated; Neujahr, Weihnachten and
 * the Easter-derived days predate any year this module is useful for. */
#define BVN_DE_UNITY_DAY_FROM     1990   /* 3 Oct, Tag der Deutschen Einheit  */
#define BVN_DE_JUNE17_FROM        1954   /* 17 Jun, its predecessor ...        */
#define BVN_DE_JUNE17_UNTIL       1989   /* ... last observed 1989             */
#define BVN_DE_LABOUR_DAY_FROM    1933   /* 1 May became a legal holiday       */

static bool federal_holiday_canon(bvn_gregorian_date_t* c)
{
    switch (c->month) {
    case  1: if (c->day ==  1)                 return true; break; /* Neujahr */
    case  5: /* 1. Mai */
        if (c->day == 1 && c->year >= BVN_DE_LABOUR_DAY_FROM)
            return true;
        break;
    case  6: /* 17. Juni -- the national holiday before 1990 */
        if (c->day == 17 && c->year >= BVN_DE_JUNE17_FROM
                         && c->year <= BVN_DE_JUNE17_UNTIL)
            return true;
        break;
    case 10: /* Tag der Deutschen Einheit */
        if (c->day == 3 && c->year >= BVN_DE_UNITY_DAY_FROM)
            return true;
        break;
    case 12: if (c->day == 25 || c->day == 26) return true; break; /* Weihn.  */
    }

    int64_t easter = bvn_gregorian_date_easter_sunday_mjd(c->year);
    if (easter == BVN_GDATE_OVF) return false;

    int64_t off = bvn_gregorian_date_to_mjd_fast(c) - easter;
    return off == -2 || off == 1 || off == 39 || off == 50;
}

static bool banking_day_canon(bvn_gregorian_date_t* c)
{
    if (c->wday == 0 || c->wday == 6)                     return false;
    if (c->month == 12 && (c->day == 24 || c->day == 31)) return false;
    return !federal_holiday_canon(c);
}

bool bvn_gregorian_date_is_federal_holiday_in_germany(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c;
    return gdate_canonicalize(&c, d) && federal_holiday_canon(&c);
}

bool bvn_gregorian_date_is_banking_day_in_germany(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c;
    return gdate_canonicalize(&c, d) && banking_day_canon(&c);
}

bool bvn_gregorian_date_is_business_day_in_germany(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c;
    return gdate_canonicalize(&c, d)
        && c.wday != 0
        && !federal_holiday_canon(&c);
}

bool bvn_gregorian_date_is_equal(bvn_gregorian_date_t* a, bvn_gregorian_date_t* b)
{
	return (a->day == b->day) && (a->month == b->month) && (a->year == b->year)
			&& (a->wday == b->wday) && (a->yday == b->yday)
			&& (a->leap == b->leap);
}

/* Seed tmp/mjd for a walk.  Returns false when the seed is the BVN_GDATE_OVF
 * sentinel or lies outside the supported MJD range, so every walk-based
 * getter degrades to the documented leave-res-unchanged error behavior. */
static bool seed_walk(bvn_gregorian_date_t* tmp, int64_t* mjd, int64_t seed)
{
    if (seed < BVN_GDATE_MJD_MIN || seed > BVN_GDATE_MJD_MAX)
        return false;
    *mjd = seed;
    bvn_gregorian_date_from_mjd(tmp, seed);
    return true;
}

/* Walk from seed in step direction until pred holds; assign *res only on
 * success so failed walks honor the leave-res-untouched error contract. */
static void get_walk_day(bvn_gregorian_date_t* res, int64_t seed,
                         int step, date_pred_t pred)
{
    bvn_gregorian_date_t tmp;
    int64_t mjd;
    if (!seed_walk(&tmp, &mjd, seed))
        return;
    if (!walk_until(&tmp, &mjd, step, pred))
        return;
    *res = tmp;
}

/* The get_* functions below read year/month off the input record. The header
 * promises the whole higher-level family judges "the civil day the input
 * normalizes to", and the is_* predicates already canonicalize -- these did not,
 * so the two contradicted each other on the same record: for {2024, 3, 61},
 * which normalizes to 2024-04-30, is_last_banking_day_in_month() answered true
 * while get_last_banking_day_in_month() returned 2024-03-28. The month alone was
 * safe (eom_mjd/ymd_to_mjd floor-reduce it); an out-of-range DAY was silently
 * dropped. Canonicalize first, and on failure leave *res untouched, which is
 * what the header already specifies for an out-of-range quarter. */
static bool gdate_canon_ym(const bvn_gregorian_date_t* d, int64_t* y, int64_t* m)
{
    bvn_gregorian_date_t c;
    if (!d || !gdate_canonicalize(&c, d))
        return false;
    *y = c.year;
    *m = c.month;
    return true;
}

void bvn_gregorian_date_get_last_banking_day_in_month(bvn_gregorian_date_t* res,
                                                  bvn_gregorian_date_t* date)
{
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    get_walk_day(res, eom_mjd(y, m), -1, pred_banking);
}

void bvn_gregorian_date_get_penultimate_banking_day_in_month(bvn_gregorian_date_t* res,
                                                         bvn_gregorian_date_t* date)
{
    bvn_gregorian_date_t tmp;
    int64_t mjd, y, m;
    if (!gdate_canon_ym(date, &y, &m))
        return;
    if (!seed_walk(&tmp, &mjd, eom_mjd(y, m)))
        return;
    if (!walk_until(&tmp, &mjd, -1, pred_banking)) /* last banking day */
        return;
    if (!seed_walk(&tmp, &mjd, mjd - 1))           /* step past it ... */
        return;
    if (!walk_until(&tmp, &mjd, -1, pred_banking)) /* ... to the one before */
        return;
    *res = tmp;
}

void bvn_gregorian_date_get_first_banking_day_in_month(bvn_gregorian_date_t* res,
                                                   bvn_gregorian_date_t* date)
{
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    get_walk_day(res, ymd_to_mjd(y, m, 1), +1, pred_banking);
}

void bvn_gregorian_date_get_mid_banking_day_in_month(bvn_gregorian_date_t* res,
                                                 bvn_gregorian_date_t* date)
{
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    get_walk_day(res, ymd_to_mjd(y, m, 15), +1, pred_banking);
}

void bvn_gregorian_date_get_last_sunday_in_month(bvn_gregorian_date_t* res,
                                             bvn_gregorian_date_t* date)
{
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    get_walk_day(res, eom_mjd(y, m), -1, pred_sunday);
}

void bvn_gregorian_date_get_last_saturday_in_month(bvn_gregorian_date_t* res,
                                               bvn_gregorian_date_t* date)
{
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    get_walk_day(res, eom_mjd(y, m), -1, pred_saturday);
}

/* The is_*_banking_day predicates judge the civil day the input normalizes to
 * (see the header contract).  They canonicalize a copy first so that both the
 * getter and the same_day() comparison operate on that canonical day; comparing
 * a normalized getter result against a raw, denormalized input would otherwise
 * never match. */
bool bvn_gregorian_date_is_last_banking_day_in_month(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c, bd = {0};
    if (!gdate_canonicalize(&c, d))
        return false;
    bvn_gregorian_date_get_last_banking_day_in_month(&bd, &c);
    return same_day(&bd, &c);
}

bool bvn_gregorian_date_is_penultimate_banking_day_in_month(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c, bd = {0};
    if (!gdate_canonicalize(&c, d))
        return false;
    bvn_gregorian_date_get_penultimate_banking_day_in_month(&bd, &c);
    return same_day(&bd, &c);
}

bool bvn_gregorian_date_is_first_banking_day_in_month(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c, bd = {0};
    if (!gdate_canonicalize(&c, d))
        return false;
    bvn_gregorian_date_get_first_banking_day_in_month(&bd, &c);
    return same_day(&bd, &c);
}

bool bvn_gregorian_date_is_mid_banking_day_in_month(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c, bd = {0};
    if (!gdate_canonicalize(&c, d))
        return false;
    bvn_gregorian_date_get_mid_banking_day_in_month(&bd, &c);
    return same_day(&bd, &c);
}

bool bvn_gregorian_date_is_last_banking_day_in_quarter(int q, bvn_gregorian_date_t* d)
{
    if (q < 1 || q > 4)
        return false;
    bvn_gregorian_date_t c, bd = {0};
    if (!gdate_canonicalize(&c, d))
        return false;
    bvn_gregorian_date_get_last_banking_day_in_quarter(q, &bd, &c);
    return same_day(&bd, &c);
}

bool bvn_gregorian_date_is_first_banking_day_in_quarter(int q, bvn_gregorian_date_t* d)
{
    if (q < 1 || q > 4)
        return false;
    bvn_gregorian_date_t c, bd = {0};
    if (!gdate_canonicalize(&c, d))
        return false;
    bvn_gregorian_date_get_first_banking_day_in_quarter(q, &bd, &c);
    return same_day(&bd, &c);
}

static inline int q_first_month(int q) { return 3 * (q - 1) + 1; } /* 1,4,7,10 */
static inline int q_last_month (int q) { return 3 *  q;          } /* 3,6,9,12 */

void bvn_gregorian_date_get_last_banking_day_in_quarter(int quarter,
                                                    bvn_gregorian_date_t* res,
                                                    bvn_gregorian_date_t* date)
{
    if (quarter < 1 || quarter > 4)
        return;
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    (void)m;
    get_walk_day(res, eom_mjd(y, q_last_month(quarter)),
                 -1, pred_banking);
}

void bvn_gregorian_date_get_first_banking_day_in_quarter(int quarter,
                                                     bvn_gregorian_date_t* res,
                                                     bvn_gregorian_date_t* date)
{
    if (quarter < 1 || quarter > 4)
        return;
    int64_t y, m;
    if (!gdate_canon_ym(date, &y, &m)) return;
    (void)m;
    get_walk_day(res, ymd_to_mjd(y, q_first_month(quarter), 1),
                 +1, pred_banking);
}

/* Germany's summer time is not the same rule in every year, and applying the
 * current one throughout reported 1970 and 1990 switches that never happened
 * while missing the ones that did:
 *
 *   before 1980  no summer time at all (reintroduced 1980; the 1916-18 and
 *                1940-49 wartime spells are irregular and out of scope)
 *   1980         started 6 April -- NOT the last Sunday in March -- ended on
 *                the last Sunday in September
 *   1981-1995    last Sunday in March .. last Sunday in September
 *   from 1996    last Sunday in March .. last Sunday in October (EU directive)
 *
 * bvn_dt_tai_second_is_dls_in_europe() in bvn_datetime.c follows the same
 * table; the two must not drift apart. */
#define BVN_DE_DST_FROM           1980
#define BVN_DE_DST_OCT_END_FROM   1996

/* Which month ends summer time in `year`, or 0 if the year has none. */
static int bvn_de_dst_end_month(int64_t year)
{
    if (year < BVN_DE_DST_FROM)         return 0;
    return (year >= BVN_DE_DST_OCT_END_FROM) ? 10 : 9;
}

/* Last Sunday of `month`. Testing `day > 24` would be wrong here: that holds for
 * a 31-day month, but September has 30 days and its last Sunday can fall on the
 * 24th -- as it did in 1995. Ask whether another week still fits instead. */
static bool bvn_de_is_last_sunday(const bvn_gregorian_date_t* c, int month)
{
    if (c->month != month || c->wday != 0) return false;
    int64_t mjd = bvn_gregorian_date_to_mjd_fast(c);
    int64_t eom = eom_mjd(c->year, month);
    if (mjd == BVN_GDATE_OVF || eom == BVN_GDATE_OVF) return false;
    return mjd + 7 > eom;
}

/* Is `c` the day summer time starts? 1980 began on a fixed date rather than the
 * last Sunday of March. */
static bool bvn_de_dst_start_day(const bvn_gregorian_date_t* c)
{
    if (c->year < BVN_DE_DST_FROM)      return false;
    if (c->year == BVN_DE_DST_FROM)     return c->month == 4 && c->day == 6;
    return bvn_de_is_last_sunday(c, 3);
}

static bool bvn_de_dst_end_day(const bvn_gregorian_date_t* c)
{
    int em = bvn_de_dst_end_month(c->year);
    return em && bvn_de_is_last_sunday(c, em);
}

bool bvn_gregorian_date_is_daylight_saving_switch_in_germany(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c;
    if (!gdate_canonicalize(&c, d)) return false;
    return bvn_de_dst_start_day(&c) || bvn_de_dst_end_day(&c);
}

bool bvn_gregorian_date_is_day_before_daylight_saving_switch_in_germany(bvn_gregorian_date_t* d)
{
    bvn_gregorian_date_t c;
    int64_t mjd;
    if (!gdate_canonicalize(&c, d)) return false;
    /* "the day before a switch" is exactly that: step forward one day and ask.
     * The old form open-coded a Saturday-in-a-window test, which cannot express
     * 1980's Sunday-6-April start. */
    mjd = bvn_gregorian_date_to_mjd_fast(&c);
    if (mjd == BVN_GDATE_OVF || mjd == BVN_GDATE_MJD_MAX) return false;
    bvn_gregorian_date_from_mjd(&c, mjd + 1);
    return bvn_de_dst_start_day(&c) || bvn_de_dst_end_day(&c);
}
