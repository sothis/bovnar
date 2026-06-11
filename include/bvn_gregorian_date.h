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

#ifndef BVN_GREGORIAN_DATE_H_
#define BVN_GREGORIAN_DATE_H_

/*******************************************************************************
 * bvn_gregorian_date.h - Proleptic Gregorian calendar <-> Modified Julian Day (MJD)
 *
 * OVERVIEW
 * --------
 * This module provides a compact, branch-lean, and overflow-aware conversion
 * library between calendar dates of the proleptic Gregorian calendar and the
 * Modified Julian Day number (MJD). It is intended as a foundation for higher
 * level date/time libraries, schedulers, astronomical computations, and any
 * application that needs robust arithmetic on dates well beyond the range of
 * POSIX `time_t` or the C standard `struct tm`.
 *
 * The algorithms are derived from Howard Hinnant's "date algorithms" which
 * express the Gregorian calendar in terms of 400-year eras (146097 days per
 * era). Each era is a closed group with respect to the leap-year cycle, which
 * eliminates special cases and yields pure integer arithmetic without lookup
 * tables.
 *
 * The internal representation shifts the civil year so that March is treated
 * as month 1 and February as month 12. This places the potentially-leap day
 * (Feb 29) at the end of the internal year and removes all conditional logic
 * from the core conversion formulas.
 *
 * REQUIREMENTS
 * ------------
 * Plain C99: <stdint.h> exact-width types and <stdbool.h>.  Overflow
 * checking is done with portable pre-condition tests (see
 * src/utils/bvn_date_impl.h); no compiler intrinsics or C23 features are
 * required.
 *
 * MJD DEFINITION
 * --------------
 * The Modified Julian Day is defined as MJD = JD - 2400000.5, where JD is the
 * Julian Day number. By convention in this module, MJD 0 corresponds to the
 * civil date 1858-11-17 (Gregorian). Negative MJD values represent dates
 * earlier than this epoch and are fully supported (proleptic extension).
 *
 * COORDINATE SHIFT: BVN_MJD_OFFSET
 * ----------------------------
 * The value BVN_MJD_OFFSET = 678881 is the number of days from the algorithmic
 * epoch 0000-03-01 (era-relative day zero, with March as the first month) to
 * the MJD epoch 1858-11-17. Adding BVN_MJD_OFFSET to an MJD puts the computation
 * into the "era space" where floor division by 146097 directly yields the era
 * index, and the remainder yields the day-of-era.
 *
 * WHY "FAST" AND "CHECKED" VARIANTS?
 * ----------------------------------
 * Two forward converters are exposed:
 *   - bvn_gregorian_date_to_mjd()      : overflow-checked, returns BVN_GDATE_OVF on
 *                                    any intermediate 64-bit overflow or on
 *                                    out-of-range results. Use this for
 *                                    untrusted or user-supplied inputs.
 *   - bvn_gregorian_date_to_mjd_fast() : no overflow checking. Use this only when
 *                                    inputs are already known to lie safely
 *                                    inside the supported range. It is the
 *                                    hot-path variant.
 *
 * BOUNDARY VALUES - WHY THEY ARE WHAT THEY ARE
 * --------------------------------------------
 * The supported MJD range is deliberately *not* the full [INT64_MIN, INT64_MAX]
 * interval. The conversion formulas contain additions and multiplications that
 * can overflow 64-bit signed integers even when the final MJD would still fit.
 * Two "safety margins" are reserved at each end of the number line:
 *
 *   BVN_LOWER_YEAR_BOUNDARY_MARGIN = 254
 *     Reserved at the bottom (near INT64_MIN) so that the full first supported
 *     year can be represented without any intermediate subexpression
 *     underflowing. It also makes room for BVN_GDATE_OVF = INT64_MIN as a
 *     dedicated "no value / overflow" sentinel that can never collide with a
 *     valid MJD.
 *
 *   BVN_UPPER_YEAR_BOUNDARY_MARGIN = 268
 *     Reserved at the top so that the full last supported year can be
 *     represented and so that adding BVN_MJD_OFFSET (to shift into era space)
 *     does not overflow. The margin accounts for one complete year's worth
 *     of days plus the coordinate shift and the day-of-month addition done
 *     at the very end of the forward conversion.
 *
 * Concretely:
 *   BVN_GDATE_OVF     = INT64_MIN                                       (sentinel)
 *   BVN_GDATE_MJD_MIN = INT64_MIN + 254      =  -9223372036854775554
 *                                          maps to  1. 1.-25252734927764695
 *   BVN_GDATE_MJD_MAX = INT64_MAX - 678881 - 268
 *                                        =   9223372036854096658
 *                                          maps to 31.12. 25252734927766553
 *
 * Within this interval every intermediate computation performed by
 * mjd_decompose(), bvn_gregorian_date_from_mjd(), and the fast forward converter
 * is guaranteed not to overflow int64_t. This spans roughly ±2.5 * 10^16
 * years - far beyond any practical astronomical or historical need, but the
 * interval is chosen to be *exactly* as wide as overflow safety permits, not
 * arbitrarily narrow.
 *
 * The checked variant bvn_gregorian_date_to_mjd() additionally validates its
 * result against [BVN_GDATE_MJD_MIN, BVN_GDATE_MJD_MAX] and collapses any overflow or
 * out-of-range condition onto the single sentinel BVN_GDATE_OVF.
 *
 * THREAD SAFETY
 * -------------
 * All functions are pure with respect to their arguments (no mutable global
 * state). They are therefore fully reentrant and safe to call concurrently
 * on distinct bvn_gregorian_date_t instances.
 *
 * ERROR HANDLING CONVENTIONS
 * --------------------------
 *   - bvn_gregorian_date_to_mjd()         : returns BVN_GDATE_OVF on error.
 *   - bvn_gregorian_date_mjd_to_yday()    : returns -1 if mjd is out of range.
 *   - bvn_gregorian_date_mjd_in_leap_year : returns false if mjd is out of range.
 *   - bvn_gregorian_date_from_mjd()       : leaves *gdate untouched on error or on
 *                                       NULL pointer input.
 *   - bvn_gregorian_date_normalize()      : returns false on overflow; otherwise
 *                                       rewrites *gdate with canonical field
 *                                       values (e.g. month==13 becomes month 1
 *                                       of the following year, etc.).
 *   - higher-level get_* helpers      : leave *res untouched when the input
 *                                       date is out of range or the quarter
 *                                       is outside 1..4.
 *   - higher-level is_* predicates    : return false in those cases.
 ******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#ifndef BVN_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define BVN_API __declspec(dllexport)
#  elif defined(__GNUC__) || defined(__clang__)
#    define BVN_API __attribute__((visibility("default")))
#  else
#    define BVN_API
#  endif
#endif
#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
 * Internal constants.
 *
 * BVN_MJD_OFFSET
 *   Days between the algorithmic epoch 0000-03-01 and the MJD epoch
 *   1858-11-17. Used to translate between "era-relative day zero" (where the
 *   400-year cycle starts cleanly) and the externally visible MJD numbering.
 *
 * BVN_LOWER_YEAR_BOUNDARY_MARGIN / BVN_UPPER_YEAR_BOUNDARY_MARGIN
 *   See the detailed rationale in the file banner above. These margins
 *   reserve just enough head-room at each end of the int64_t number line to
 *   guarantee that *every* intermediate expression in the conversion routines
 *   stays representable.
 *----------------------------------------------------------------------------*/
#define BVN_MJD_OFFSET                  (INT64_C(678881))
#define BVN_LOWER_YEAR_BOUNDARY_MARGIN  (INT64_C(254)) /* start of lowest year */
#define BVN_UPPER_YEAR_BOUNDARY_MARGIN  (INT64_C(268)) /* end of highest year */

/*------------------------------------------------------------------------------
 * Public sentinels and boundary constants.
 *
 * BVN_GDATE_OVF
 *   Returned by functions whose natural return type is int64_t to signal that
 *   the requested computation overflowed or the input was invalid. Chosen as
 *   INT64_MIN so that it is distinct from every value in [BVN_GDATE_MJD_MIN,
 *   BVN_GDATE_MJD_MAX].
 *
 * BVN_GDATE_MJD_MIN / BVN_GDATE_MJD_MAX
 *   Inclusive bounds of the supported MJD range. Inputs outside this range
 *   are rejected by all MJD-consuming functions in this module.
 *----------------------------------------------------------------------------*/
#define BVN_GDATE_OVF       (INT64_MIN)
#define BVN_GDATE_MJD_MIN   (INT64_MIN + BVN_LOWER_YEAR_BOUNDARY_MARGIN)
	/* -9223372036854775554, maps to  1. 1.-25252734927764695 */
#define BVN_GDATE_MJD_MAX   (INT64_MAX - BVN_MJD_OFFSET - BVN_UPPER_YEAR_BOUNDARY_MARGIN)
	/*  9223372036854096658, maps to 31.12. 25252734927766553 */
#define BVN_GDATE_YEAR_MIN	INT64_C(-25252734927764695)
#define BVN_GDATE_YEAR_MAX	INT64_C(25252734927766553)

/*------------------------------------------------------------------------------
 * bvn_gregorian_date_t - canonical calendar-date record.
 *
 * Fields:
 *   year   : proleptic Gregorian year. No offset, astronomical numbering
 *            (i.e. 1 BC == 0, 2 BC == -1). Full int64_t range usable within
 *            the MJD boundary constants above.
 *   month  : 1..12 after normalization. Arbitrary int64_t values are accepted
 *            as input to bvn_gregorian_date_to_mjd()/_normalize(); they will be
 *            carried into the year field via floor-division.
 *   day    : 1..28/29/30/31 after normalization. Arbitrary int64_t values are
 *            likewise accepted and carried.
 *   leap   : 1 if `year` is a Gregorian leap year, 0 otherwise. Output only.
 *   wday   : day of week, 0 == Sunday ... 6 == Saturday. Output only.
 *   yday   : day of year, 0 == January 1st ... 364 or 365 == December 31st.
 *            Output only.
 *
 * The three "output only" fields are populated by bvn_gregorian_date_from_mjd()
 * and bvn_gregorian_date_normalize(). They are ignored by the forward converters.
 *----------------------------------------------------------------------------*/
typedef struct bvn_gregorian_date_t {
	int64_t year;
	int64_t month;
	int64_t day;
	int32_t leap;
	int32_t wday;   /* Sunday - Saturday; 0 - 6                  */
	int32_t yday;   /* 01.01. - 31.12.;  0 - 364 or 365          */
} bvn_gregorian_date_t;

/*------------------------------------------------------------------------------
 * Public API.
 *
 * bvn_gregorian_date_to_mjd(gdate)
 *   Converts a calendar date to its MJD. Every intermediate arithmetic step
 *   is overflow-checked (portable C99 checks, see src/utils/bvn_date_impl.h).
 *   Accepts arbitrary (including
 *   denormalized) month/day values; they are reduced with floor semantics.
 *   Returns BVN_GDATE_OVF on overflow, on out-of-range result, or when gdate is
 *   NULL.
 *
 * bvn_gregorian_date_to_mjd_fast(gdate)
 *   Same conversion without overflow checks. The caller must guarantee that
 *   the input describes a date whose MJD lies safely inside
 *   [BVN_GDATE_MJD_MIN, BVN_GDATE_MJD_MAX]. Undefined behavior otherwise. Intended
 *   for tight loops where inputs have already been validated.
 *
 * bvn_gregorian_date_mjd_to_wday(mjd)
 *   Returns the day of week (0=Sunday .. 6=Saturday) for the given MJD.
 *   Works for the full int64_t range because it uses only floor_mod by 7.
 *
 * bvn_gregorian_date_mjd_in_leap_year(mjd)
 *   Returns true if the civil year containing `mjd` is a Gregorian leap
 *   year. Returns false if mjd is outside the supported range.
 *
 * bvn_gregorian_date_mjd_to_yday(mjd)
 *   Returns the zero-based day of year (0..365) for the given MJD, or -1 if
 *   mjd is outside the supported range.
 *
 * bvn_gregorian_date_from_mjd(gdate, mjd)
 *   Fills *gdate with all six fields derived from `mjd`. If gdate is NULL or
 *   mjd is out of range, *gdate is left unchanged.
 *
 * bvn_gregorian_date_normalize(gdate)
 *   Treats *gdate as possibly denormalized (e.g. month==0, day==40) and
 *   rewrites it with canonical values plus all derived fields. Returns false
 *   on overflow and leaves *gdate unchanged in that case, true on success.
 *----------------------------------------------------------------------------*/

/* proleptic gregorian -> MJD */
BVN_API int64_t bvn_gregorian_date_to_mjd(const bvn_gregorian_date_t* gdate);
BVN_API int64_t bvn_gregorian_date_to_mjd_fast(const bvn_gregorian_date_t* gdate);

/* MJD -> proleptic gregorian */
BVN_API int32_t bvn_gregorian_date_mjd_to_wday(int64_t mjd);
BVN_API bool    bvn_gregorian_date_mjd_in_leap_year(int64_t mjd);
BVN_API int32_t bvn_gregorian_date_mjd_to_yday(int64_t mjd);
BVN_API void    bvn_gregorian_date_from_mjd(bvn_gregorian_date_t* gdate, int64_t mjd);

/* Normalize proleptic gregorian, i.e. calculate correct date out of arbitrary
 * month and day values and fill all other gdate members accordingly. */
BVN_API bool    bvn_gregorian_date_normalize(bvn_gregorian_date_t* gdate);

/* Returns true if all fields of 'a' and 'b' compare equal. Both dates must be
 * fully populated (e.g. by bvn_gregorian_date_from_mjd() or _normalize()),
 * since the derived fields (leap, wday, yday) take part in the comparison. */
BVN_API bool    bvn_gregorian_date_is_equal(bvn_gregorian_date_t* a, bvn_gregorian_date_t* b);

BVN_API int64_t bvn_gregorian_date_easter_sunday_mjd(int64_t year);

/** higher level calendar functions
 *
 * All predicates below canonicalize a copy of the input record first
 * (bvn_gregorian_date_normalize() semantics), so denormalized records are
 * judged by the civil day they normalize to, and the output-only fields
 * (leap, wday, yday) of the input do not need to be populated.  Quarter
 * arguments must be in 1..4; other values make the get_* functions leave
 * *res untouched and the is_* predicates return false. **/

/* Fill given bvn_gregorian_date_t 'gdate' structure with the gregorian date of
 * easter sunday and all other holidays depanding that date of the given year. */
BVN_API bool bvn_gregorian_date_calc_easter_sunday(bvn_gregorian_date_t* gdate, int64_t year);
BVN_API bool bvn_gregorian_date_calc_good_friday(bvn_gregorian_date_t *gdate, int64_t year);
BVN_API bool bvn_gregorian_date_calc_easter_monday(bvn_gregorian_date_t *gdate, int64_t year);
BVN_API bool bvn_gregorian_date_calc_ascension_day(bvn_gregorian_date_t *gdate, int64_t year);
BVN_API bool bvn_gregorian_date_calc_whit_monday(bvn_gregorian_date_t *gdate, int64_t year);

/* Returns 1 if given date is a federal holiday in Germany. Returns 0 otherwise. */
BVN_API bool bvn_gregorian_date_is_federal_holiday_in_germany(bvn_gregorian_date_t* date);

/* Returns 1 if given date is a banking day in Germany. Returns 0 otherwise. */
BVN_API bool bvn_gregorian_date_is_banking_day_in_germany(bvn_gregorian_date_t* date);

/* Returns 1 if given date is a business day in Germany. Returns 0 otherwise. Regional holidays are not taken into account. */
BVN_API bool bvn_gregorian_date_is_business_day_in_germany(bvn_gregorian_date_t* date);

/* Saves the last banking day of the month and year given by 'date' into 'res' */
BVN_API void bvn_gregorian_date_get_last_banking_day_in_month(bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);

BVN_API void bvn_gregorian_date_get_penultimate_banking_day_in_month(bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);
BVN_API void bvn_gregorian_date_get_mid_banking_day_in_month(bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);

/* Saves the last sunday of the month and year given by 'date' into 'res' */
BVN_API void bvn_gregorian_date_get_last_sunday_in_month(bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);
/* Saves the last saturday of the month and year given by 'date' into 'res' */
BVN_API void bvn_gregorian_date_get_last_saturday_in_month(bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);


/* Saves the first banking day of the month and year given by 'date' into 'res' */
BVN_API void bvn_gregorian_date_get_first_banking_day_in_month(bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);

/* Saves the last banking day of the the given quarter (1-4) and year given by 'date' into 'res' */
BVN_API void bvn_gregorian_date_get_last_banking_day_in_quarter(int quarter, bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);

/* Saves the first banking day of the the given quarter (1-4) and year given by 'date' into 'res' */
BVN_API void bvn_gregorian_date_get_first_banking_day_in_quarter(int quarter, bvn_gregorian_date_t* res, bvn_gregorian_date_t* date);

BVN_API bool bvn_gregorian_date_is_last_banking_day_in_month(bvn_gregorian_date_t* date);
BVN_API bool bvn_gregorian_date_is_penultimate_banking_day_in_month(bvn_gregorian_date_t* date);
BVN_API bool bvn_gregorian_date_is_mid_banking_day_in_month(bvn_gregorian_date_t* date);
BVN_API bool bvn_gregorian_date_is_first_banking_day_in_month(bvn_gregorian_date_t* date);
BVN_API bool bvn_gregorian_date_is_last_banking_day_in_quarter(int quarter, bvn_gregorian_date_t* date);
BVN_API bool bvn_gregorian_date_is_first_banking_day_in_quarter(int quarter, bvn_gregorian_date_t* date);

BVN_API bool bvn_gregorian_date_is_daylight_saving_switch_in_germany(bvn_gregorian_date_t* date);
BVN_API bool bvn_gregorian_date_is_day_before_daylight_saving_switch_in_germany(bvn_gregorian_date_t* date);

#ifdef __cplusplus
}
#endif
#endif /* BVN_GREGORIAN_DATE_H_ */
