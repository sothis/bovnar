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
 * A build with SOME unit profiles on and some off — the state between the two
 * extremes, and the only one where the interesting mistakes are reachable.
 *
 * WHY THIS EXISTS BESIDE profiles_off_smoke.c. That file drives all seven at 0,
 * where the registry loses every row and every table its last reference: the
 * failures it catches are about a configuration compiling at all. The failures
 * HERE are about a configuration being CORRECT while half-populated, and none
 * of them can occur when the answer is uniformly "none":
 *
 *   * bvn_unit_profile_name(i) must walk the compiled-in profiles with no gap
 *     and no duplicate. With every profile off the count is 0 and the walk is
 *     vacuous; with a mixed set the index has to skip the absent rows, which is
 *     an off-by-one waiting to be written.
 *   * The OPAQUE ID SPACE must not compact. Each profile owns a 10000-wide
 *     block whether or not it is compiled in, and bu_unece_one stays 300000
 *     with unece absent — otherwise two builds would disagree about what an id
 *     means, which is the one thing the block layout exists to prevent. All-off
 *     cannot show this: with nothing present there is nothing to shift past.
 *   * An opaque unit of an ABSENT profile is still in the opaque range, so it
 *     is still incommensurable with everything — and it has no spelling, so the
 *     writer must refuse it rather than invent one. Reachable only where some
 *     other profile is present to keep the machinery alive.
 *
 * The configuration is chosen to cover the shapes rather than to be a likely
 * one: ucum (an EXPRESSION profile that owns opaque units) and qudt-qk (a FLAT
 * profile that owns none) are on; the other five are off, and one of those —
 * unece — owns opaque units, which is what makes the third case above testable.
 *
 * Driven by cmake/profiles_off_build.cmake, which compiles it against the
 * amalgamation under the same strict flags an integrator uses.
 */
#include <stdio.h>
#include <string.h>
#include "bovnar.h"   /* amalgamated single header: units, DOM and all */

static int failures = 0;

#define CHECK(cond, what)                                                      \
	do {                                                                       \
		if (!(cond)) {                                                         \
			printf("  FAIL %s\n", (what));                                     \
			++failures;                                                        \
		}                                                                      \
	} while (0)

static error_code_t unit_err(const char *s)
{
	return bvn_unit_error_code((const uint8_t *)s, (uint32_t)strlen(s));
}

static const char *fmt(value_unit_t u, char *buf, size_t cap)
{
	return bvn_unit_to_string(u, buf, cap) < 0 ? "(unwritable)" : buf;
}

int main(void)
{
	char buf[BVNR_UNIT_STRING_MAX];
	bool ok = false;

	printf("profiles-partial smoke test (ucum + qudt-qk on, five off)\n");

	/* ── the reporting pair, which is what a consumer asks ───────────────── */

	/* Exactly the compiled-in ones, in registry order, with no hole where an
	 * absent profile was. The names matter as much as the count: an index that
	 * walked the whole registry would report "unece" here. */
	CHECK(bvn_unit_profile_count() == 2u,
	      "two profiles are compiled in");
	CHECK(bvn_unit_profile_name(0) &&
	      strcmp(bvn_unit_profile_name(0), "ucum") == 0,
	      "the first is ucum");
	CHECK(bvn_unit_profile_name(1) &&
	      strcmp(bvn_unit_profile_name(1), "qudt-qk") == 0,
	      "the second is qudt-qk, the absent ones skipped");
	CHECK(bvn_unit_profile_name(2) == NULL,
	      "one past the end is NULL, not an absent profile's name");

	/* ── present and absent, in one binary ───────────────────────────────── */

	CHECK(unit_err("ucum:kg")        == error_none, "ucum: translates");
	CHECK(unit_err("ucum:mm[Hg]")    == error_none, "a ucum expression translates");
	CHECK(unit_err("qudt-qk:Length") == error_none, "qudt-qk: translates");

	CHECK(unit_err("unece:KGM")      == error_unit_profile_unknown, "unece: absent");
	CHECK(unit_err("qudt:KiloGM")    == error_unit_profile_unknown, "qudt: absent");
	CHECK(unit_err("udunits:m")      == error_unit_profile_unknown, "udunits: absent");
	CHECK(unit_err("om:metre")       == error_unit_profile_unknown, "om: absent");
	CHECK(unit_err("cf:air_temperature")
	                                 == error_unit_profile_unknown, "cf: absent");

	/* An absent namespace is UNKNOWN and a bad code in a PRESENT one is
	 * ILLEGAL. Telling those apart is the whole reason both codes exist, and a
	 * mixed build is the only place the distinction is observable at once. */
	CHECK(unit_err("ucum:metre")     == error_unit_illegal,
	      "a bad code in a present namespace is illegal, not unknown");
	CHECK(unit_err("ucum:B[SPL]")    == error_unit_profile_unsupported,
	      "a known-but-uncarryable code in a present namespace is unsupported");

	/* ── the id space does not compact ───────────────────────────────────── */

	/* Every block keeps its tag whether or not its profile is compiled in. If
	 * these moved, an id would mean different things in two builds -- and the
	 * whole point of a blocked space is that it never does. */
	CHECK(BVN_PROFILE_UCUM_OPAQUE_FIRST  == 200000,
	      "block 20 is still UCUM's");
	CHECK(BVN_PROFILE_UNECE_OPAQUE_FIRST == 300000,
	      "block 30 is still UN/ECE's, with UN/ECE absent");
	CHECK((int)bu_ucum_iu    == 200000, "bu_ucum_iu keeps its id");
	CHECK((int)bu_unece_one  == 300000,
	      "an ABSENT profile's enumerator keeps its id");

	/* And the dense tables keep a row per defined unit rather than shrinking to
	 * the compiled-in ones. Written as the relation rather than as a literal:
	 * adding a unit or an opaque code moves the total, and should. */
	CHECK(BVN_UNIT_SLOT_COUNT ==
	      1 + BVN_UNIT_NATIVE_COUNT + BVN_PROFILE_OPAQUE_COUNT,
	      "the slot space still covers every defined unit");

	/* ── opaque units, present and absent ────────────────────────────────── */

	/* A present profile's opaque unit round-trips through its own notation --
	 * it has no native spelling, so this is the only form it has. */
	value_unit_t iu = bvn_parse_unit((const uint8_t *)"ucum:[IU]", &ok);
	CHECK(ok, "ucum:[IU] parses");
	CHECK(bvn_unit_is_profile_only(iu), "and has no native spelling");
	CHECK(strcmp(fmt(iu, buf, sizeof buf), "ucum:[IU]") == 0,
	      "and writes back as itself");

	/* An ABSENT profile's opaque unit, built by hand the way a consumer holding
	 * an id from another build would. It is still opaque -- the block is a
	 * range, not a table lookup -- so it stays incommensurable; but nothing can
	 * spell it, and the writer must say so rather than reach for another
	 * namespace. */
	value_unit_t stray = BVN_UNIT_NO_PREFIX(bu_unece_one);
	CHECK(bvn_unit_is_profile_only(stray),
	      "an absent profile's opaque unit is still opaque");
	CHECK(bvn_unit_to_string(stray, buf, sizeof buf) < 0,
	      "...and has no spelling in a build without its profile");
	value_unit_t metre = bvn_parse_unit((const uint8_t *)"m", &ok);
	CHECK(ok, "m parses");
	CHECK(!bvn_units_compatible(stray, metre),
	      "...and is still incommensurable with a native unit");

	/* ── writing is symmetric with reading ───────────────────────────────── */

	CHECK(bvn_unit_to_ucum(metre, buf, sizeof buf) > 0,
	      "a present namespace can be written");
	CHECK(bvn_unit_to_profile("ucum", metre, buf, sizeof buf) > 0,
	      "...by name as well");
	CHECK(bvn_unit_to_profile("unece", metre, buf, sizeof buf) < 0,
	      "an absent namespace cannot be written");
	CHECK(bvn_unit_to_profile("om", metre, buf, sizeof buf) < 0,
	      "nor another one");

	/* ── everything native, and agreement among what is present ──────────── */

	CHECK(unit_err("m/s") == error_none, "m/s still parses");
	CHECK(unit_err("k~g") == error_none, "k~g still parses");
	CHECK(unit_err("$USD") == error_none, "$USD still parses");
	CHECK(unit_err("acUS") == error_none, "a recent native unit still parses");

	value_unit_t a = bvn_parse_unit((const uint8_t *)"ucum:kg", &ok);
	CHECK(ok, "ucum:kg parses");
	value_unit_t b = bvn_parse_unit((const uint8_t *)"k~g", &ok);
	CHECK(ok, "k~g parses");
	CHECK(bvn_unit_equal(a, b),
	      "a translated unit still equals its native spelling");

	/* ── a whole document, through the reader a consumer actually uses ───── */

	static const char doc_ok[]  =
		"#!bovnar 1.2\n.v = <float:64,ucum:kg> 1.0;\n";
	static const char doc_bad[] =
		"#!bovnar 1.2\n.v = <float:64,unece:KGM> 1.0;\n";
	bvn_dom_doc_t *d = bvn_dom_parse(doc_ok, (uint32_t)(sizeof doc_ok - 1u));
	CHECK(d && bvn_dom_doc_get_parse_error(d) == error_none,
	      "a document in a present namespace parses");
	bvn_dom_doc_destroy(d);
	d = bvn_dom_parse(doc_bad, (uint32_t)(sizeof doc_bad - 1u));
	CHECK(d && bvn_dom_doc_get_parse_error(d) == error_unit_profile_unknown,
	      "a document in an absent namespace fails as unit_profile_unknown");
	bvn_dom_doc_destroy(d);

	if (failures == 0)
		printf("  ok: the mixed configuration behaves as documented\n");
	else
		printf("  %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
