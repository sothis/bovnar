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
 * A build with every unit profile switched off: does it still work, and does it
 * refuse a profile in the one way that is defined?
 *
 * WHY THIS EXISTS AS A SEPARATE BINARY. The per-vocabulary switches
 * (BVNR_WITH_UCUM_PROFILE and its six siblings) are compile-time, so no test
 * inside the ordinary build can exercise the off state -- and an unexercised
 * build configuration is one that stops compiling and nobody finds out. The
 * driver (cmake/profiles_off_build.cmake) compiles the amalgamation with all
 * seven set to 0, under -Werror, and runs this. Two failure modes it catches
 * that a reading of the source will not:
 *
 *   * the registry array left EMPTY, which C99 does not allow (there is a
 *     sentinel row for exactly this, and this test is what proves it is needed
 *     and sufficient);
 *   * a table that becomes unreferenced and trips -Wunused-const-variable, or a
 *     helper that becomes unused -- either of which is an error under the strict
 *     flags an integrator compiles the amalgamation with.
 *
 * It also pins the CONTRACT of an absent profile, which is a promise to
 * consumers and not an implementation detail: the namespace is
 * error_unit_profile_unknown (not error_unit_illegal -- the document is not
 * wrong, this build simply cannot read it), the reporting pair says so up front,
 * and everything native is untouched.
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

int main(void)
{
	printf("profiles-off smoke test\n");

	/* The reporting pair answers, rather than the caller having to probe with a
	 * document and read an error code back. */
	CHECK(bvn_unit_profile_count() == 0u,
	      "bvn_unit_profile_count() is 0 with every profile off");
	CHECK(bvn_unit_profile_name(0) == NULL,
	      "bvn_unit_profile_name(0) is NULL when there are none");

	/* Every namespace is unknown, and unknown is not illegal: a consumer that
	 * cannot read ucum:m must be able to tell "this build has no ucum" from
	 * "that is not a unit", which is the whole reason the code exists. */
	CHECK(unit_err("ucum:m")      == error_unit_profile_unknown, "ucum: unknown");
	CHECK(unit_err("unece:KGM")   == error_unit_profile_unknown, "unece: unknown");
	CHECK(unit_err("qudt:Metre")  == error_unit_profile_unknown, "qudt: unknown");
	CHECK(unit_err("qudt-qk:Length")
	                              == error_unit_profile_unknown, "qudt-qk: unknown");
	CHECK(unit_err("udunits:m")   == error_unit_profile_unknown, "udunits: unknown");
	CHECK(unit_err("om:metre")    == error_unit_profile_unknown, "om: unknown");
	CHECK(unit_err("cf:air_temperature")
	                              == error_unit_profile_unknown, "cf: unknown");
	/* A namespace no build has ever defined answers the same, which is what
	 * makes the previous seven a statement about this build and not about the
	 * spelling. */
	CHECK(unit_err("nosuch:m")    == error_unit_profile_unknown,
	      "an invented namespace is unknown too");

	/* Native units are what a build like this is for, and they are untouched. */
	CHECK(unit_err("m/s")   == error_none, "m/s still parses");
	CHECK(unit_err("k~g")   == error_none, "k~g still parses");
	CHECK(unit_err("Gi~B")  == error_none, "Gi~B still parses");
	CHECK(unit_err("$USD")  == error_none, "$USD still parses");
	CHECK(unit_err("nosuchunit") == error_unit_illegal,
	      "a bad native unit is still illegal");

	/* Conversion and equality never knew the profiles existed; prove it here,
	 * because "the tables are gone" must not mean "the unit system is thinner". */
	bool ok = false;
	value_unit_t km = bvn_parse_unit((const uint8_t *)"k~m", &ok);
	CHECK(ok, "k~m parses");
	value_unit_t m  = bvn_parse_unit((const uint8_t *)"m", &ok);
	CHECK(ok, "m parses");
	CHECK(bvn_units_compatible(km, m), "k~m and m are still compatible");
	CHECK(!bvn_unit_equal(km, m),      "k~m and m are still not equal");

	/* Writing a profile code is refused symmetrically: a build that cannot read
	 * a namespace cannot emit one either. */
	char buf[BVNR_UNIT_STRING_MAX];
	CHECK(bvn_unit_to_profile("ucum", m, buf, sizeof buf) < 0,
	      "bvn_unit_to_profile refuses an absent namespace");
	CHECK(bvn_unit_to_ucum(m, buf, sizeof buf) < 0,
	      "bvn_unit_to_ucum refuses when ucum is absent");
	/* ...while the native spelling still round-trips. */
	CHECK(bvn_unit_to_string(m, buf, sizeof buf) > 0 && strcmp(buf, "m") == 0,
	      "the native spelling still round-trips");

	/* A whole document, through the reader that a consumer actually uses. */
	static const char doc_ok[]  = "#!bovnar 1.2\n.v = <float:64,m/s> 9.81;\n";
	static const char doc_bad[] = "#!bovnar 1.2\n.v = <float:64,ucum:m/s> 9.81;\n";
	bvn_dom_doc_t *d = bvn_dom_parse(doc_ok, (uint32_t)(sizeof doc_ok - 1u));
	CHECK(d && bvn_dom_doc_get_parse_error(d) == error_none,
	      "a native document parses");
	bvn_dom_doc_destroy(d);
	d = bvn_dom_parse(doc_bad, (uint32_t)(sizeof doc_bad - 1u));
	CHECK(d && bvn_dom_doc_get_parse_error(d) == error_unit_profile_unknown,
	      "a profile document fails as unit_profile_unknown");
	bvn_dom_doc_destroy(d);

	if (failures) {
		printf("profiles-off smoke test: %d failures\n", failures);
		return 1;
	}
	printf("profiles-off smoke test: all checks passed\n");
	return 0;
}
