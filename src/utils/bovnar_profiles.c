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
 * ===========================================================================
 * The unit profiles
 * ===========================================================================
 *
 * Specified in doc/11_bovnar_unit_profiles.md. In one paragraph:
 * "<namespace>:<code>" in the unit slot is an alternative SPELLING, not a second
 * unit model. This file parses the profile expression, translates it into an
 * ordinary value_unit_t, and hands that back to bvn_parse_unit's caller.
 * Everything downstream -- equality, compatibility, conversion, the reader and
 * writer unit policies, the DOM -- is untouched and cannot tell which notation
 * the document used.
 *
 * ONE FILE, SEVERAL VOCABULARIES. Each registered namespace contributes its own
 * generated tables and picks one of two GRAMMARS; nothing else about it is
 * special-cased. An "expr" profile (UCUM, UDUNITS) writes a code as an
 * expression over prefixed atoms with operators, exponents and grouping. A
 * "flat" profile (QUDT, UNECE) has no operators and no prefixes: the whole code
 * is one token, looked up entire. The two share every table shape, the
 * translation rules below, and all three refusal outcomes -- so adding a
 * vocabulary is a data file and a registry row, not a second parser.
 *
 * THE THREE OUTCOMES (section 3.1 of the document, and the whole design in one
 * rule): a profile expression either becomes a real unit or becomes an error.
 * There is no third state in which a value carries a unit the rest of the
 * library cannot reason about, because every guarantee the unit system makes is
 * a guarantee about units it understands. The refusals split by cause --
 * illegal / unsupported / unknown-profile -- because "you wrote it wrong" and
 * "you wrote it right and we cannot carry it" call for different fixes.
 *
 * WHY THE TRANSLATION IS DECADE-BASED. A mapped atom's native target is a unit
 * EXPRESSION ("k~mmHg", "g·gn", "c~ha"), parsed by the ordinary native parser at
 * first use. A UCUM prefix, a 10*n factor, and a prefix the target cannot carry
 * are all accumulated as one signed power of ten, and discharged at the end into
 * a single component's prefix slot. That is what lets "m[Hg]" -> "k~mmHg" and
 * "mm[Hg]" -> "mmHg" fall out of one rule instead of two special cases, and what
 * makes "10*3/uL" come out as n~L⁻¹ rather than nine orders of magnitude wrong.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_si_units.h"
#include "bvn_internal_dims.h"
#include "bvn_profile_impl.h"

/* Bounds. An expression grammar nests parentheses; the depth cap matches the
 * native unit parser's so a hostile document cannot drive either into deep
 * recursion. */
#define BVN_PROFILE_MAX_DEPTH 16u

/* ── generated tables ───────────────────────────────────────────────────── */

typedef struct {
	const char* code;
	uint8_t     len;
	int32_t     decade;
} bvn_prof_pfx_t;

typedef struct {
	const char*       code;
	uint8_t           len;
	/* Native unit expression for a mapped code; NULL for an opaque one. */
	const char*       target;
	/* The opaque block id for an opaque code; bu_none for a mapped one. */
	value_base_unit_t opaque;
	/* May a prefix attach to this atom at all? UCUM's own metric flag, and
	 * always false in a flat profile, which has no prefix mechanism. */
	bool              metric;
} bvn_prof_atom_t;

typedef struct {
	const char* code;
	uint8_t     len;
} bvn_prof_unsup_t;

/*
 * The inverse of the transliteration: which code writes a given base unit, and
 * what decade that code already carries. Generated, because computing it by
 * re-parsing every mapped target at run time was both slow and wrong -- it
 * picked whichever row the length-sorted parse table reached first, so siemens
 * came out as "mho" and the calorie as "cal_th".
 *
 * `decade` is the code's own scale relative to the base. UCUM's "m[Hg]" is a
 * METRE of mercury, i.e. bovnar's mmHg times 10^3, so writing plain mmHg means
 * emitting the UCUM prefix for (0 - 3) and producing "mm[Hg]". Bases whose UCUM
 * atom is itself prefixed had no UCUM form at all before this. A flat profile
 * has no prefix to emit, so there the decade must MATCH for the row to be
 * usable at all.
 */
typedef struct {
	value_base_unit_t base;
	const char*       code;
	uint8_t           len;
	int32_t           decade;
	bool              metric;
	/* The BINARY prefix this code already carries, or iec_none. Only a flat
	 * profile ever sets it: QUDT names the mebibyte "MebiBYTE", one whole code,
	 * so the row has to be able to say which binary prefix that is. An expr
	 * profile reaches a scale by emitting a decimal prefix and there is none
	 * meaning 2^20, so a binary-prefixed unit has no expr-profile spelling at
	 * all and every expr row here is iec_none. */
	iec_prefix_id_t   iec;
} bvn_prof_rev_t;

/* ── the profile registry ───────────────────────────────────────────────── */

/*
 * How a code is spelled. The two grammars share every table above and every
 * translation rule below; they differ only in how a code is cut into atoms.
 *
 *   expr  an expression over prefixed atoms -- operators, exponents, grouping,
 *         annotations. UCUM and UDUNITS.
 *   flat  one whole token, matched entire against the atom table. No prefix is
 *         stripped and no operator is recognised, which is what makes QUDT's
 *         "M-PER-SEC" and UNECE's "KGM" mean exactly one thing each and never
 *         decompose into something that merely looks like them.
 */
typedef enum {
	bvn_prof_grammar_expr = 0,
	bvn_prof_grammar_flat
} bvn_prof_grammar_t;

typedef struct {
	const char*             ns;
	uint8_t                 ns_len;
	bvn_prof_grammar_t      grammar;
	const bvn_prof_pfx_t*   pfx;      /* NULL for a flat profile */
	const bvn_prof_atom_t*  atoms;
	const bvn_prof_unsup_t* unsup;
	const bvn_prof_rev_t*   rev;
	/* The opaque sub-range this profile owns. first > last means none, which is
	 * how a profile that maps everything onto native units declares itself. */
	int32_t                 opaque_first;
	int32_t                 opaque_last;
	/*
	 * Expression-grammar syntax, ignored by a flat profile. This is where the
	 * UCUM/UDUNITS difference lives: the two vocabularies agree on what division
	 * MEANS -- '/' inverts the term after it, which is ordinary left-to-right
	 * arithmetic -- and disagree only on which bytes spell an operator.
	 *
	 * mul        extra multiplication characters beyond '.', which every expr
	 *            profile has. UDUNITS adds '*' and ' ', so "kg m-2 s-1" parses.
	 * caret_exp  accept "m^2" as well as the bare "m2" both vocabularies allow.
	 */
	const char*             mul;
	bool                    caret_exp;
	/*
	 * A substring whose presence anywhere in a code refuses it outright, as
	 * "known and not carryable" rather than "not a code", or NULL.
	 *
	 * This exists for UDUNITS' reference-time construct, "<unit> since
	 * <timestamp>". The construct cannot be caught as an ATOM: it is three
	 * tokens in UDUNITS and one string here, so there is nothing to look up.
	 * The outcome must be error_unit_profile_unsupported rather than
	 * error_unit_illegal, because a producer who writes it has written valid
	 * UDUNITS and deserves to be told bovnar cannot carry it -- not that they
	 * made a typo.
	 *
	 * Note what it can and cannot reach since the lexer stopped deleting
	 * whitespace inside a type annotation. The spelling UDUNITS actually uses,
	 * "days since 1970-01-01", now fails one layer earlier as
	 * error_type_param_whitespace and never arrives here; what this test still
	 * catches is the concatenated "dayssince1970-01-01", which is the form a
	 * producer reaches for after being told the spaces are illegal, and the form
	 * that must not come back as "not a code".
	 */
	const char*             refuse_substr;
	/* Does this vocabulary have UCUM's "{...}" annotations? They carry no
	 * meaning, but they are UCUM syntax and admitting them everywhere would let
	 * a UDUNITS code that is simply wrong look like a valid annotated one. */
	bool                    annotations;
} bvn_profile_t;

/* Does `s` contain `needle`? Plain scan: the strings are at most 255 bytes and
 * this runs once per profile parse, on a path that is about to do far more
 * work. */
static bool contains(const char* s, uint32_t len, const char* needle)
{
	size_t n = strlen(needle);
	if (n == 0 || (size_t)len < n)
		return false;
	for (uint32_t i = 0; i + n <= len; i++) {
		if (memcmp(s + i, needle, n) == 0)
			return true;
	}
	return false;
}

/* Is `c` a multiplication operator in this profile? */
static bool is_mul(const bvn_profile_t* p, char c)
{
	if (c == '.')
		return true;
	for (const char* m = p->mul; m && *m; m++) {
		if (*m == c)
			return true;
	}
	return false;
}

#if BVNR_WITH_UCUM_PROFILE
static const bvn_prof_pfx_t ucum_pfx_table[] = {
#include "bovnar_profile_ucum_prefix.gen.inc"
	{ NULL, 0, 0 }
};

static const bvn_prof_atom_t ucum_atom_table[] = {
#include "bovnar_profile_ucum_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t ucum_unsup_table[] = {
#include "bovnar_profile_ucum_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t ucum_rev_table[] = {
#include "bovnar_profile_ucum_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_UCUM_PROFILE */

/* UNECE Recommendation 20/21. Flat, so there is no prefix table at all: a Rec 20
 * code is one whole token and "KGM" is the kilogram rather than a kilo applied
 * to something. */
#if BVNR_WITH_UNECE_PROFILE
static const bvn_prof_atom_t unece_atom_table[] = {
#include "bovnar_profile_unece_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t unece_unsup_table[] = {
#include "bovnar_profile_unece_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t unece_rev_table[] = {
#include "bovnar_profile_unece_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_UNECE_PROFILE */

/* QUDT unit local names. Flat: "KiloGM" is one symbol, not a prefix on "GM". */
#if BVNR_WITH_QUDT_PROFILE
static const bvn_prof_atom_t qudt_atom_table[] = {
#include "bovnar_profile_qudt_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t qudt_unsup_table[] = {
#include "bovnar_profile_qudt_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t qudt_rev_table[] = {
#include "bovnar_profile_qudt_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_QUDT_PROFILE */

/*
 * QUDT QUANTITY KINDS, which are not units: a kind says what is measured and
 * nothing about the scale. Each translates to the COHERENT SI UNIT of the kind
 * -- Length to m, Mass to k~g -- which is the only reading that keeps the
 * profile invariant (a profile expression becomes a real value_unit_t or an
 * error) while still admitting the namespace at all. The reasoning, and the
 * failure mode it leaves open, are set out at the top of qudt-qk.bvnr.
 */
#if BVNR_WITH_QUDT_QK_PROFILE
static const bvn_prof_atom_t qudt_qk_atom_table[] = {
#include "bovnar_profile_qudt_qk_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t qudt_qk_unsup_table[] = {
#include "bovnar_profile_qudt_qk_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t qudt_qk_rev_table[] = {
#include "bovnar_profile_qudt_qk_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_QUDT_QK_PROFILE */

/* UDUNITS-2, the units syntax of netCDF and the CF conventions. An expr profile
 * like UCUM, differing only in which bytes spell an operator. */
#if BVNR_WITH_UDUNITS_PROFILE
static const bvn_prof_pfx_t udunits_pfx_table[] = {
#include "bovnar_profile_udunits_prefix.gen.inc"
	{ NULL, 0, 0 }
};

static const bvn_prof_atom_t udunits_atom_table[] = {
#include "bovnar_profile_udunits_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t udunits_unsup_table[] = {
#include "bovnar_profile_udunits_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t udunits_rev_table[] = {
#include "bovnar_profile_udunits_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_UDUNITS_PROFILE */

/*
 * OM 2, the Ontology of units of Measure. A flat profile like QUDT, and for the
 * same reason: "kilogramPerCubicMetre" is one token. OM does state a structure
 * for it -- numerator, denominator, prefix, exponent -- and every target in
 * om.bvnr was BUILT from that structure, but the structure is read at generate
 * time and not at parse time, so a name is matched whole here.
 */
#if BVNR_WITH_OM_PROFILE
static const bvn_prof_atom_t om_atom_table[] = {
#include "bovnar_profile_om_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t om_unsup_table[] = {
#include "bovnar_profile_om_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t om_rev_table[] = {
#include "bovnar_profile_om_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_OM_PROFILE */

/*
 * CF standard names, which are not units: a standard name says what quantity a
 * variable holds, and the unit comes from the `canonical_units` CF states for
 * it. Same shape as the QUDT quantity kinds, and the same bite (a producer
 * whose numbers are not in the canonical units writes a wrong value that parses
 * cleanly); the reasoning is at the top of cf.bvnr.
 *
 * ITS REVERSE TABLE IS EMPTY ON PURPOSE. Sixty-nine standard names state the
 * kelvin, so writing a kelvin back as one of them would pick a quantity out of
 * a hat and assert it. gen_profiles.py marks the profile unwritable and emits
 * no rows, which makes bvn_unit_to_profile("cf", u) return -1 for every unit
 * through the ordinary "no row names this base" path.
 */
#if BVNR_WITH_CF_PROFILE
static const bvn_prof_atom_t cf_atom_table[] = {
#include "bovnar_profile_cf_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

static const bvn_prof_unsup_t cf_unsup_table[] = {
#include "bovnar_profile_cf_unsupported.gen.inc"
	{ NULL, 0 }
};

static const bvn_prof_rev_t cf_rev_table[] = {
#include "bovnar_profile_cf_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false, iec_none }
};
#endif /* BVNR_WITH_CF_PROFILE */

/*
 * Every namespace this build defines. Anything else before a ':' is
 * error_unit_profile_unknown, which is how a consumer tells "this build has no
 * such profile" from "that is not a unit" -- and, since the per-vocabulary build
 * switches of bvn_profile_impl.h, how it tells a namespace that was compiled out
 * from one that never existed. The two are the same answer on purpose: a
 * document is not wrong for naming ucum, and a build without ucum cannot read it
 * either way.
 *
 * A SENTINEL CLOSES THE TABLE, and it is not decoration. Every row here is
 * conditional, so a build with all seven switches off would otherwise leave an
 * empty array initialiser -- which C99 does not have -- and the loops below
 * would index a zero-length object. ns_len 0 can match no namespace (the
 * grammar's name-head is one lower-alpha or digit) and the impossible opaque
 * range (0, -1) can contain no base unit, so the row is unreachable by
 * construction rather than by a skip test each loop has to remember.
 */
static const bvn_profile_t bvn_profiles[] = {
#if BVNR_WITH_UCUM_PROFILE
	{ "ucum", 4, bvn_prof_grammar_expr,
	  ucum_pfx_table, ucum_atom_table, ucum_unsup_table, ucum_rev_table,
	  BVN_PROFILE_UCUM_OPAQUE_FIRST, BVN_PROFILE_UCUM_OPAQUE_LAST,
	  "", false, NULL, true },
#endif
#if BVNR_WITH_UNECE_PROFILE
	{ "unece", 5, bvn_prof_grammar_flat,
	  NULL, unece_atom_table, unece_unsup_table, unece_rev_table,
	  BVN_PROFILE_UNECE_OPAQUE_FIRST, BVN_PROFILE_UNECE_OPAQUE_LAST,
	  NULL, false, NULL, false },
#endif
#if BVNR_WITH_QUDT_PROFILE
	{ "qudt", 4, bvn_prof_grammar_flat,
	  NULL, qudt_atom_table, qudt_unsup_table, qudt_rev_table,
	  BVN_PROFILE_QUDT_OPAQUE_FIRST, BVN_PROFILE_QUDT_OPAQUE_LAST,
	  NULL, false, NULL, false },
#endif
#if BVNR_WITH_QUDT_QK_PROFILE
	{ "qudt-qk", 7, bvn_prof_grammar_flat,
	  NULL, qudt_qk_atom_table, qudt_qk_unsup_table, qudt_qk_rev_table,
	  BVN_PROFILE_QUDT_QK_OPAQUE_FIRST, BVN_PROFILE_QUDT_QK_OPAQUE_LAST,
	  NULL, false, NULL, false },
#endif
	/*
	 * '*' multiplies beside '.', and '^' introduces an exponent, so "kg*m-2"
	 * and "m^2" parse.
	 *
	 * SPACE IS NOT IN THIS SET, although UDUNITS itself multiplies with it and
	 * CF's commonest spelling is "kg m-2 s-1". A space cannot reach this parser
	 * at all: the lexer now refuses whitespace inside a type parameter
	 * (error_type_param_whitespace), so "m s-1" is a parse error rather than the
	 * "ms-1" -- reciprocal milliseconds -- it used to silently become. Adding
	 * ' ' here would mean admitting the space-separated form into the type
	 * grammar, which is a different and much larger decision than a
	 * multiplication character; until it is made, a producer writes
	 * "kg*m-2*s-1" or "kg.m-2.s-1", both of which UDUNITS also accepts. Section
	 * 13.2 of doc/11_bovnar_unit_profiles.md records why.
	 */
#if BVNR_WITH_UDUNITS_PROFILE
	{ "udunits", 7, bvn_prof_grammar_expr,
	  udunits_pfx_table, udunits_atom_table, udunits_unsup_table,
	  udunits_rev_table,
	  BVN_PROFILE_UDUNITS_OPAQUE_FIRST, BVN_PROFILE_UDUNITS_OPAQUE_LAST,
	  "* ", true, "since", false },
#endif
#if BVNR_WITH_OM_PROFILE
	{ "om", 2, bvn_prof_grammar_flat,
	  NULL, om_atom_table, om_unsup_table, om_rev_table,
	  BVN_PROFILE_OM_OPAQUE_FIRST, BVN_PROFILE_OM_OPAQUE_LAST,
	  NULL, false, NULL, false },
#endif
#if BVNR_WITH_CF_PROFILE
	{ "cf", 2, bvn_prof_grammar_flat,
	  NULL, cf_atom_table, cf_unsup_table, cf_rev_table,
	  BVN_PROFILE_CF_OPAQUE_FIRST, BVN_PROFILE_CF_OPAQUE_LAST,
	  NULL, false, NULL, false },
#endif
	{ NULL, 0, bvn_prof_grammar_flat, NULL, NULL, NULL, NULL, 0, -1,
	  NULL, false, NULL, false },
};
#define BVN_PROFILE_COUNT \
	(sizeof(bvn_profiles) / sizeof(bvn_profiles[0]))

/*
 * The namespaces this build actually carries. The sentinel is excluded, which is
 * the whole reason these two do not simply expose BVN_PROFILE_COUNT: that number
 * counts rows in the array, and one of the rows is not a profile.
 *
 * A linear walk rather than a subtraction, so the pair keeps agreeing with
 * profile_by_ns if the table ever grows a second unreachable row.
 */
uint32_t bvni_profile_count(void)
{
	uint32_t n = 0;
	for (size_t i = 0; i < BVN_PROFILE_COUNT; i++) {
		if (bvn_profiles[i].ns)
			++n;
	}
	return n;
}
const char* bvni_profile_name(uint32_t index)
{
	uint32_t n = 0;
	for (size_t i = 0; i < BVN_PROFILE_COUNT; i++) {
		if (!bvn_profiles[i].ns)
			continue;
		if (n == index)
			return bvn_profiles[i].ns;
		++n;
	}
	return NULL;
}

/* The profile whose namespace is exactly s[0..len), or NULL. The !p->ns test is
 * the sentinel row: its ns is NULL, and memcmp against a null pointer is
 * undefined even for a length of zero. */
static const bvn_profile_t* profile_by_ns(const char* s, uint32_t len)
{
	for (size_t i = 0; i < BVN_PROFILE_COUNT; i++) {
		const bvn_profile_t* p = &bvn_profiles[i];
		if (!p->ns)
			continue;
		if (p->ns_len == len && memcmp(s, p->ns, len) == 0)
			return p;
	}
	return NULL;
}

/* The profile that owns `base` in the opaque block, or NULL when `base` is a
 * native unit. Every opaque id belongs to exactly one profile -- the ranges are
 * assigned contiguously and without overlap by gen_profiles.py. */
static const bvn_profile_t* profile_of_opaque(value_base_unit_t base)
{
	for (size_t i = 0; i < BVN_PROFILE_COUNT; i++) {
		const bvn_profile_t* p = &bvn_profiles[i];
		if ((int32_t)base >= p->opaque_first && (int32_t)base <= p->opaque_last)
			return p;
	}
	return NULL;
}

/* ── the arbitrary block ────────────────────────────────────────────────── */

/*
 * The opaque units no longer form one range — each profile has its own block —
 * so this walks the per-profile runs. bu_none and every native unit are below
 * the lowest profile block, which is the common case and the one comparison
 * BVN_PROFILE_OPAQUE_SLOT's chain would otherwise pay for five times over.
 */
bool bvni_is_opaque(value_base_unit_t base)
{
	int32_t v = (int32_t)base;
	if (v <= BVN_UNIT_NATIVE_LAST)
		return false;
	return BVN_PROFILE_OPAQUE_SLOT(v) >= 0;
}

bool bvni_unit_has_opaque(value_unit_t u)
{
	uint32_t n = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	           ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	for (uint32_t i = 0; i < n; i++) {
		if (bvni_is_opaque(u.components[i].base))
			return true;
	}
	return false;
}

/* ── translation state ──────────────────────────────────────────────────── */

/*
 * A partially built unit: the components collected so far, plus the residual
 * decimal decade nothing has been able to absorb yet. Keeping the decade OUT of
 * the components until the very end is the point -- discharging it early would
 * have to pick a component before the expression's exponents are known, and the
 * fold divides by the exponent.
 */
typedef struct {
	value_unit_t u;
	int32_t      decade;
} bvn_prof_acc_t;

static bool acc_push(bvn_prof_acc_t* a, value_unit_component_t c)
{
	if (a->u.num_components >= BVNR_MAX_UNIT_COMPONENTS)
		return false;
	a->u.components[a->u.num_components++] = c;
	return true;
}

/* ── table lookup ───────────────────────────────────────────────────────── */

static const bvn_prof_atom_t* find_atom(const bvn_profile_t* p,
                                        const char* s, uint32_t len)
{
	for (const bvn_prof_atom_t* e = p->atoms; e->code; e++) {
		if (e->len == len && memcmp(s, e->code, len) == 0)
			return e;
	}
	return NULL;
}

static bool is_known_unsupported(const bvn_profile_t* p,
                                 const char* s, uint32_t len)
{
	for (const bvn_prof_unsup_t* e = p->unsup; e->code; e++) {
		if (e->len == len && memcmp(s, e->code, len) == 0)
			return true;
	}
	return false;
}

/*
 * Resolve `s` as prefix + atom, trying every prefix that heads it, longest
 * first (the generated table is length-sorted).
 *
 * Trying only the longest was a defect. UCUM's prefixes are not suffix-free --
 * "d" is a head of "da" -- so "dar" (the deciare) matched "da", left "r", found
 * no atom, and was reported as invalid UCUM although it is perfectly legal.
 * Every viable split has to be tried before the code is refused.
 *
 * `want_unsupported` selects which table to resolve against, so the same
 * longest-first walk decides both "which atom is this" and "is this an atom we
 * know and cannot carry" -- otherwise "cm[H2O]" reports illegal while its own
 * atom "m[H2O]" reports unsupported.
 */
static const bvn_prof_atom_t* resolve_prefixed(const bvn_profile_t* p,
                                               const char* s, uint32_t len,
                                               int32_t* decade)
{
	if (!p->pfx)
		return NULL;
	for (const bvn_prof_pfx_t* f = p->pfx; f->code; f++) {
		if (f->len >= len || memcmp(s, f->code, f->len) != 0)
			continue;
		const bvn_prof_atom_t* cand = find_atom(p, s + f->len, len - f->len);
		/* UCUM permits a prefix only on a metric atom, which is what keeps
		 * "k[arb'U]" an error. A non-metric hit is not a match, so the walk
		 * continues to a shorter prefix rather than stopping here. */
		if (cand && cand->metric) {
			*decade = f->decade;
			return cand;
		}
	}
	return NULL;
}

static bool resolve_prefixed_unsupported(const bvn_profile_t* p,
                                         const char* s, uint32_t len)
{
	if (!p->pfx)
		return false;
	for (const bvn_prof_pfx_t* f = p->pfx; f->code; f++) {
		if (f->len >= len || memcmp(s, f->code, f->len) != 0)
			continue;
		if (is_known_unsupported(p, s + f->len, len - f->len))
			return true;
	}
	return false;
}

/* ── the decade fold ────────────────────────────────────────────────────── */

/* The SI prefix whose decade is exactly `d`, or -1. The prefix decades are not
 * contiguous -- there is nothing for 10^4, 10^5, 10^7 or 10^8 -- which is
 * precisely why the fold can fail and why that failure is a named error rather
 * than a silent rescale. */
/* One table read both ways. Two tables would be two things to keep in step, and
 * a disagreement between them is a silent rescale rather than a build error. */
static const struct { int32_t d; si_prefix_id_t p; } si_decade_tab[] = {
	{ -30, si_quecto }, { -27, si_ronto }, { -24, si_yocto },
	{ -21, si_zepto  }, { -18, si_atto  }, { -15, si_femto },
	{ -12, si_pico   }, {  -9, si_nano  }, {  -6, si_micro },
	{  -3, si_milli  }, {  -2, si_centi }, {  -1, si_deci  },
	{   0, si_none   }, {   1, si_deca  }, {   2, si_hecto },
	{   3, si_kilo   }, {   6, si_mega  }, {   9, si_giga  },
	{  12, si_tera   }, {  15, si_peta  }, {  18, si_exa   },
	{  21, si_zetta  }, {  24, si_yotta }, {  27, si_ronna },
	{  30, si_quetta },
};
#define SI_DECADE_TAB_COUNT \
	(sizeof(si_decade_tab) / sizeof(si_decade_tab[0]))

static int si_prefix_for_decade(int32_t d)
{
	for (size_t i = 0; i < SI_DECADE_TAB_COUNT; i++) {
		if (si_decade_tab[i].d == d)
			return (int)si_decade_tab[i].p;
	}
	return -1;
}

static int32_t si_decade_of(si_prefix_id_t p)
{
	for (size_t i = 0; i < SI_DECADE_TAB_COUNT; i++) {
		if (si_decade_tab[i].p == p)
			return si_decade_tab[i].d;
	}
	return 0;
}

/*
 * Discharge the residual decade D into exactly one component's prefix.
 *
 * A component with exponent e and prefix decade p contributes p*e to the
 * expression's net decade, so absorbing D means moving that component's prefix
 * from p to p + D/e. The division is what makes this correct in a DENOMINATOR
 * and is the whole reason the fold is written out rather than done inline: with
 * "10*3/uL" the residual is +3, the surviving component is L at exponent -1 with
 * prefix micro (-6), and the answer is nano -- 10^12 L^-1. Adding the decade
 * without dividing gives 10^-3 L^-1, wrong by fifteen orders of magnitude and
 * wrong in a way no later check would catch.
 *
 * Leftmost component wins, so the choice is deterministic and does not depend on
 * table order.
 */
static bool fold_decade_range(value_unit_t* u, int32_t* decade,
                              uint32_t from, uint32_t to)
{
	if (*decade == 0)
		return true;
	for (uint32_t i = from; i < to; i++) {
		value_unit_component_t* c = &u->components[i];
		/* A binary prefix counts in powers of two; a decimal decade has no
		 * business being merged into one. */
		if (c->prefix.system != prefix_si)
			continue;
		int32_t e = bvn_exponent_to_int(c->exponent);
		if (e == 0 || (*decade % e) != 0)
			continue;
		int32_t want = si_decade_of(c->prefix.id.si) + *decade / e;
		int p = si_prefix_for_decade(want);
		if (p < 0)
			continue;
		value_unit_prefix_t np;
		np.system = prefix_si;
		np.id.si  = (si_prefix_id_t)p;
		if (!bvn_prefix_unit_valid(np, c->base))
			continue;
		c->prefix = np;
		*decade   = 0;
		return true;
	}
	return false;
}

/* The final, whole-expression fold, over every component. */
static bool fold_decade(bvn_prof_acc_t* a)
{
	return fold_decade_range(&a->u, &a->decade, 0, a->u.num_components);
}

/* ── scanning helpers ───────────────────────────────────────────────────── */

/* A UCUM annotation is "{" ... "}". It carries no meaning -- UCUM defines an
 * annotation standing alone as the unity -- so it contributes nothing here. It
 * is scanned rather than ignored so that an unterminated one is an error and
 * cannot swallow the rest of the expression.
 *
 * The content is restricted to the bytes a bovnar type body accepts. UCUM allows
 * any printable ASCII except braces, but ';' and '#' terminate a value and '<'
 * and '>' delimit a type annotation, so admitting them here would let a unit
 * string end the assignment that contains it. */
static bool scan_annotation(const char* s, uint32_t len, uint32_t* i)
{
	uint32_t j = *i + 1;                 /* past '{' */
	while (j < len && s[j] != '}') {
		unsigned char b = (unsigned char)s[j];
		if (b == '{' || b == ';' || b == '#' || b == '<' || b == '>' ||
			b == '"' || b < 0x20)
			return false;
		j++;
	}
	if (j >= len)
		return false;                    /* unterminated */
	*i = j + 1;
	return true;
}

/* 10^k for a positive integer that is a power of ten, or -1. UCUM permits a bare
 * integer factor; only the powers of ten have a bovnar representation, and only
 * some of those survive the fold. */
static int32_t decade_of_integer(const char* s, uint32_t len)
{
	if (len == 0 || s[0] != '1')
		return -1;
	for (uint32_t i = 1; i < len; i++) {
		if (s[i] != '0')
			return -1;
	}
	return (int32_t)(len - 1);
}

/* ── the expression parser ──────────────────────────────────────────────── */

static bool parse_expr(const bvn_profile_t* p, const char* s, uint32_t len,
                       uint32_t depth,
                       bvn_prof_acc_t* acc, bvni_profile_status_t* status);

/*
 * Turn a resolved atom into components on the accumulator.
 *
 * `total` is the exponent the whole atom is raised to, sign included, and `pdec`
 * the decade of the prefix that was stripped off it (0 when there was none, and
 * always 0 in a flat profile). Shared by both grammars: an expression profile
 * reaches it once per component, a flat profile once per code.
 */
static bool materialise_atom(const bvn_prof_atom_t* atom, int32_t total,
                             int32_t pdec, bvn_prof_acc_t* acc,
                             bvni_profile_status_t* status)
{
	value_unit_t base;
	if (atom->target) {
		bool ok = true;
		base = bvn_parse_unit_n((const uint8_t*)atom->target,
		                        (uint32_t)strlen(atom->target), &ok);
		if (!ok) {
			/* A target that does not parse is a defect in the profile's data
			 * file, not in the document. gen_profiles.py checks every target
			 * against the registry, so reaching here means the table and the
			 * parser have diverged. */
			*status = bvni_profile_unsupported;
			return false;
		}
	} else {
		base = BVN_UNIT_NO_PREFIX(atom->opaque);
	}

	uint32_t first = acc->u.num_components;
	for (uint32_t i = 0; i < base.num_components; i++) {
		value_unit_component_t c = base.components[i];
		int32_t ce = bvn_exponent_to_int(c.exponent);
		int32_t ne = ce * total;
		/* The atom's own exponent times the one written on it: "kat2" is the
		 * katal's mol·s⁻¹ squared. The product is what has to land in range,
		 * not either factor -- and it is computed in int32 so the multiply
		 * cannot wrap before the check sees it. */
		if (ne == 0 || ne > BVN_EXPONENT_MAX || ne < BVN_EXPONENT_MIN) {
			*status = bvni_profile_unsupported;
			return false;
		}
		c.exponent = bvn_int_to_exponent(ne);
		if (!acc_push(acc, c)) {
			*status = bvni_profile_unsupported;
			return false;
		}
	}
	/*
	 * The prefix is a decade on top of whatever the target already is, applied
	 * once for the whole atom and scaled by the exponent: "km2" is (10^3 m)^2,
	 * i.e. six decades, not three.
	 *
	 * Try to discharge it into THIS atom's own components first. It almost
	 * always fits, and when it does the result reads like the code the producer
	 * wrote: "mg/dL" comes out as m~g/d~L. Pooling every prefix into the
	 * expression-wide residual instead is just as correct numerically — the
	 * whole-expression fold would have produced c~g/L, the same quantity — but
	 * it moves scale between components for no reason and makes the output
	 * needlessly hard to recognise.
	 *
	 * Whatever does not fit locally (a target whose base refuses the prefix, or
	 * an exponent that does not divide it) falls through to the residual, where
	 * the final fold gets a second chance with every component to choose from.
	 */
	int32_t local = pdec * total;
	if (local != 0 &&
		!fold_decade_range(&acc->u, &local, first, acc->u.num_components)) {
		acc->decade += local;
	}
	return true;
}

/*
 * A flat profile's whole code, matched entire.
 *
 * No prefix is stripped and no operator is recognised. That is the point: QUDT
 * spells the kilogram "KiloGM" and UNECE spells it "KGM", and neither is a
 * prefix applied to something else -- treating them as one would make "KGM" a
 * kilo-GM and "MTS" a mega-TS, which is how a vocabulary starts inventing units
 * its publisher never defined.
 */
static bool parse_flat(const bvn_profile_t* p, const char* s, uint32_t len,
                       bvn_prof_acc_t* acc, bvni_profile_status_t* status)
{
	const bvn_prof_atom_t* atom = find_atom(p, s, len);
	if (!atom) {
		*status = is_known_unsupported(p, s, len) ? bvni_profile_unsupported
		                                          : bvni_profile_illegal;
		return false;
	}
	return materialise_atom(atom, 1, 0, acc, status);
}

/*
 * One UCUM "annotatable": an atom, optionally prefixed, optionally exponentiated,
 * optionally trailed by an annotation. `sign` is +1 in the numerator and -1 in
 * the denominator -- UCUM's '/' is a binary operator that applies to the ONE
 * component that follows it, unlike bovnar's native '/', which latches for the
 * rest of the expression. Getting that wrong turns "kg/m.s2" into kg·m⁻¹·s⁻²
 * when it means kg·m⁻¹·s².
 */
static bool parse_component(const bvn_profile_t* p, const char* s,
                            uint32_t len, int sign,
                            uint32_t depth, bvn_prof_acc_t* acc,
                            bvni_profile_status_t* status)
{
	if (len == 0) {
		*status = bvni_profile_illegal;
		return false;
	}
	/* A factor: "10*n", "10^n", or a bare power of ten. Contributes a decade and
	 * no components. */
	if (len >= 3 && s[0] == '1' && s[1] == '0' && (s[2] == '*' || s[2] == '^')) {
		uint32_t k = 3;
		int      neg = 0;
		if (k < len && (s[k] == '-' || s[k] == '+')) {
			neg = (s[k] == '-');
			k++;
		}
		if (k >= len) {
			*status = bvni_profile_illegal;
			return false;
		}
		int32_t v = 0;
		for (; k < len; k++) {
			if (s[k] < '0' || s[k] > '9') {
				*status = bvni_profile_illegal;
				return false;
			}
			v = v * 10 + (s[k] - '0');
			if (v > 400) {               /* far past any SI prefix decade */
				*status = bvni_profile_unsupported;
				return false;
			}
		}
		acc->decade += sign * (neg ? -v : v);
		return true;
	}
	if (s[0] >= '0' && s[0] <= '9') {
		int32_t d = decade_of_integer(s, len);
		if (d < 0) {
			/* A bare integer that is not a power of ten -- UCUM allows it, and
			 * bovnar has no component for a numeric factor. */
			*status = bvni_profile_unsupported;
			return false;
		}
		acc->decade += sign * d;
		return true;
	}

	/* Split a trailing signed exponent. An atom never ends in a digit outside
	 * brackets, and a bracketed atom ends in ']', so this cannot bite off part
	 * of a code like "[CCID_50]". */
	uint32_t alen = len;
	int32_t  exp  = 1;
	{
		uint32_t d = len;
		/*
		 * UDUNITS spells an exponent "m^2" as well as "m2"; UCUM has only the
		 * bare form. Cutting the '^' here rather than in a separate branch means
		 * the sign, bound and range checks below run once for both spellings --
		 * "m^-9" and "m-9" take exactly the same path.
		 */
		if (p->caret_exp) {
			uint32_t k = len;
			while (k > 0 && s[k - 1] >= '0' && s[k - 1] <= '9')
				k--;
			if (k > 0 && k < len && (s[k - 1] == '-' || s[k - 1] == '+'))
				k--;
			if (k > 1 && k < len && s[k - 1] == '^') {
				/* Rewrite as the bare form by dropping the caret: scan the
				 * exponent from k, and treat the atom as ending at k-1. */
				int32_t v = 0;
				uint32_t j = k;
				int neg = 0;
				if (s[j] == '-' || s[j] == '+') {
					neg = (s[j] == '-');
					j++;
				}
				if (j >= len) {
					*status = bvni_profile_illegal;
					return false;
				}
				for (; j < len; j++) {
					if (v > 99) {
						*status = bvni_profile_unsupported;
						return false;
					}
					v = v * 10 + (s[j] - '0');
				}
				exp  = neg ? -v : v;
				alen = k - 1;
				if (alen == 0) {
					*status = bvni_profile_illegal;
					return false;
				}
				if (exp == 0 || exp > BVN_EXPONENT_MAX ||
				    exp < BVN_EXPONENT_MIN) {
					*status = bvni_profile_unsupported;
					return false;
				}
				goto have_atom;
			}
		}
		while (d > 0 && s[d - 1] >= '0' && s[d - 1] <= '9')
			d--;
		if (d < len) {
			uint32_t ds = d;
			if (ds > 0 && (s[ds - 1] == '-' || s[ds - 1] == '+'))
				ds--;
			if (ds == 0) {               /* all digits: handled above */
				*status = bvni_profile_illegal;
				return false;
			}
			/*
			 * The accumulator is bounded before every multiply. Without it a
			 * long digit run overflowed a signed int -- undefined behaviour, and
			 * in practice "ucum:m999999999999999999" wrapped to a plausible
			 * exponent and parsed as m⁻¹ rather than being refused. A unit
			 * parameter may be 255 bytes, so this is reachable from a document.
			 * The bound admits at most three digits, which is exactly what
			 * BVN_EXPONENT_MAX needs; a value that scans inside it but lands
			 * outside the range is caught by the range check below.
			 */
			int32_t v = 0;
			for (uint32_t k = d; k < len; k++) {
				if (v > 99) {
					*status = bvni_profile_unsupported;
					return false;
				}
				v = v * 10 + (s[k] - '0');
			}
			if (s[ds] == '-')
				v = -v;
			exp  = v;
			alen = ds;
		}
	}
	if (alen == 0) {
		*status = bvni_profile_illegal;
		return false;
	}
	if (exp == 0 || exp > BVN_EXPONENT_MAX || exp < BVN_EXPONENT_MIN) {
		/* Bovnar spells exponents BVN_EXPONENT_MIN..BVN_EXPONENT_MAX and has
		 * no representation for a component raised to zero. */
		*status = bvni_profile_unsupported;
		return false;
	}

have_atom:
	/* Atom, then atom-with-prefix. Whole-token first: a prefix must never win
	 * over a complete atom, or "mho" would read as milli-ho and "min" as
	 * milli-inch -- the same rule the native parser applies from the other end. */
	;
	const char*             abeg = s;
	uint32_t                blen = alen;
	int32_t                 pdec = 0;
	const bvn_prof_atom_t*  atom = find_atom(p, abeg, blen);
	if (!atom)
		atom = resolve_prefixed(p, abeg, blen, &pdec);
	if (!atom) {
		/*
		 * Not translatable. Distinguish "UCUM knows this and we cannot carry it"
		 * from "that is not a UCUM atom" — the whole reason the unsupported list
		 * exists.
		 */
		bool known = is_known_unsupported(p, abeg, blen) ||
		             resolve_prefixed_unsupported(p, abeg, blen);
		*status = known ? bvni_profile_unsupported : bvni_profile_illegal;
		return false;
	}

	(void)depth;
	return materialise_atom(atom, sign * exp, pdec, acc, status);
}

/*
 * "[/] term { ('.' | '/') term }" -- UCUM's operators, left to right, each '/'
 * inverting only the term that follows it.
 */
static bool parse_expr(const bvn_profile_t* p, const char* s, uint32_t len,
                       uint32_t depth,
                       bvn_prof_acc_t* acc, bvni_profile_status_t* status)
{
	if (depth > BVN_PROFILE_MAX_DEPTH) {
		*status = bvni_profile_illegal;
		return false;
	}
	if (len == 0) {
		*status = bvni_profile_illegal;
		return false;
	}
	uint32_t i    = 0;
	int      sign = 1;
	if (s[0] == '/') {          /* the reciprocal form, "/min" */
		sign = -1;
		i    = 1;
		if (i >= len) {
			*status = bvni_profile_illegal;
			return false;
		}
	}
	for (;;) {
		if (i >= len) {
			*status = bvni_profile_illegal;   /* trailing operator */
			return false;
		}
		if (s[i] == '(') {
			uint32_t pd = 1, j = i + 1;
			for (; j < len; j++) {
				if (s[j] == '(')      pd++;
				else if (s[j] == ')') pd--;
				if (pd == 0) break;
			}
			if (pd != 0) {
				*status = bvni_profile_illegal;
				return false;
			}
			bvn_prof_acc_t sub = { BVN_UNIT_NONE, 0 };
			if (!parse_expr(p, s + i + 1, j - (i + 1), depth + 1, &sub, status))
				return false;
			for (uint32_t k = 0; k < sub.u.num_components; k++) {
				value_unit_component_t c = sub.u.components[k];
				if (sign < 0)
					c.exponent = bvn_int_to_exponent(
						-bvn_exponent_to_int(c.exponent));
				if (!acc_push(acc, c)) {
					*status = bvni_profile_unsupported;
					return false;
				}
			}
			acc->decade += sign * sub.decade;
			i = j + 1;
		} else if (s[i] == '{' && p->annotations) {
			/* A standalone annotation is the unity: no components, no decade. */
			if (!scan_annotation(s, len, &i)) {
				*status = bvni_profile_illegal;
				return false;
			}
		} else {
			/* Scan the annotatable. A '.' or '/' inside brackets belongs to the
			 * atom ("B[10.nV]"), so bracket depth is tracked. */
			uint32_t start = i, bd = 0;
			while (i < len) {
				char b = s[i];
				if (b == '[')      bd++;
				else if (b == ']') { if (bd == 0) break; bd--; }
				else if (bd == 0 && (is_mul(p, b) || b == '/' || b == '(' ||
				                     (b == '{' && p->annotations))) break;
				i++;
			}
			if (bd != 0) {
				*status = bvni_profile_illegal;   /* unbalanced '[' */
				return false;
			}
			if (!parse_component(p, s + start, i - start, sign, depth, acc, status))
				return false;
			/* An annotation may trail a component; it changes nothing. */
			if (i < len && s[i] == '{' && p->annotations) {
				if (!scan_annotation(s, len, &i)) {
					*status = bvni_profile_illegal;
					return false;
				}
			}
		}
		if (i >= len)
			break;
		if (is_mul(p, s[i]))  sign =  1;
		else if (s[i] == '/') sign = -1;
		else {
			*status = bvni_profile_illegal;   /* e.g. implicit multiplication */
			return false;
		}
		i++;
	}
	return true;
}

/* ── entry points ───────────────────────────────────────────────────────── */

bool bvni_unit_has_profile(const char* s, uint32_t len)
{
	/* A namespace is lowercase letters, digits and '-', followed by ':'. No
	 * native unit alias or currency code contains a colon, so noticing one is
	 * enough to hand off -- and a ':' anywhere else in a unit was already an
	 * error.
	 *
	 * The hyphen is here for "qudt-qk", where the vocabulary itself is split
	 * into two namespaces. It cannot lead, so ":" and "-x:" are not namespaces
	 * and fall through to the native parser, which rejects them as it always
	 * did. */
	for (uint32_t i = 0; i < len; i++) {
		if (s[i] == ':')
			return i > 0;
		bool ok = (s[i] >= 'a' && s[i] <= 'z') ||
		          (s[i] >= '0' && s[i] <= '9') ||
		          (s[i] == '-' && i > 0);
		if (!ok)
			return false;
	}
	return false;
}

value_unit_t bvni_parse_profile_unit(const char* s, uint32_t len,
                                     bvni_profile_status_t* status)
{
	value_unit_t none = BVN_UNIT_NONE;
	*status = bvni_profile_ok;
	uint32_t ns = 0;
	while (ns < len && s[ns] != ':')
		ns++;
	if (ns >= len) {
		*status = bvni_profile_illegal;
		return none;
	}
	const bvn_profile_t* p = profile_by_ns(s, ns);
	if (!p) {
		*status = bvni_profile_unknown_ns;
		return none;
	}
	const char* code = s + ns + 1;
	uint32_t    clen = len - ns - 1;
	if (clen == 0) {
		*status = bvni_profile_illegal;
		return none;
	}
	if (p->refuse_substr && contains(code, clen, p->refuse_substr)) {
		*status = bvni_profile_unsupported;
		return none;
	}
	bvn_prof_acc_t acc = { BVN_UNIT_NONE, 0 };
	bool ok = (p->grammar == bvn_prof_grammar_flat)
	        ? parse_flat(p, code, clen, &acc, status)
	        : parse_expr(p, code, clen, 0, &acc, status);
	if (!ok)
		return none;
	if (!fold_decade(&acc)) {
		/* A residual scale with no prefix to put it in. There is no prefix for
		 * 10^4, 10^5, 10^7 or 10^8, and none at all when the expression has no
		 * component to carry one ("ucum:10*3" alone). */
		*status = bvni_profile_unsupported;
		return none;
	}
	if (acc.u.num_components == 0) {
		/* "ucum:1", "ucum:{RBC}" -- UCUM's unity. Explicitly dimensionless. */
		return none;
	}
	/* The translated unit must satisfy every rule a natively written one does;
	 * an atom whose target carries a prefix its base refuses would otherwise
	 * arrive as a unit the writer cannot spell. */
	if (!bvn_unit_valid(acc.u)) {
		*status = bvni_profile_unsupported;
		return none;
	}
	return acc.u;
}

/* ── writing a profile code back out ────────────────────────────────────── */

/*
 * Longest "<prefix><code>" this profile can assemble, for the read-back check
 * below. The longest code in src/gendata is 166 bytes (a CF standard name) and
 * the longest profile prefix 5; a concatenation that does not fit is treated as
 * unverifiable and therefore unwritable, which is the safe direction.
 */
#define BVN_PROF_WRITE_MAX 256u

/*
 * Does "<pfx><code>" read back as the atom it was assembled from?
 *
 * IT DID NOT, AND THAT WAS THE WORST KIND OF BUG THIS FORMAT CAN HAVE. An
 * expression profile joins a prefix to an atom with NOTHING between them --
 * there is no '~' in UCUM or UDUNITS -- so the writer emits one token and the
 * reader resolves it by its own rules: whole atom first, then prefix+atom. When
 * the concatenation happens to BE an atom, the reader takes that atom, and the
 * unit that comes back is not the unit that went in:
 *
 *     k~t  (kilotonne)   -> udunits "kt"  -> the KNOT, a speed
 *     p~H  (picohenry)   -> udunits "pH"  -> the ACIDITY scale
 *     f~t  (femtotonne)  -> udunits "ft"  -> the FOOT
 *     p~t  (picotonne)   -> udunits "pt"  -> the PINT
 *     n~t  (nanotonne)   -> udunits "nt"  -> the NIT, cd/m^2
 *     a~t  (attotonne)   -> udunits "at"  -> the TECHNICAL ATMOSPHERE
 *
 * bvn_unit_to_profile reported success for every one. "kt" is the sharpest:
 * units.bvnr refuses that exact token on INPUT, in compact_exceptions, because
 * "reading a speed as a mass is exactly the failure this format exists to
 * prevent" -- and the writer was emitting it.
 *
 * So a code is now assembled only if it reads back. The check mirrors the
 * parser's own order (find_atom, then resolve_prefixed) rather than restating
 * the rule, because a second description of that order is a second thing to
 * keep in step. When the shortest prefix spelling fails, write_atom tries the
 * next -- UDUNITS spells kilo both "k" and "kilo", and "kilot" is unambiguous
 * -- and refuses only when no spelling survives.
 */
static bool code_reads_back(const bvn_profile_t* p,
                            const char* pfx, uint32_t plen,
                            const char* code, uint32_t clen,
                            const bvn_prof_atom_t* want, int32_t want_decade)
{
	if (!want)
		return false;
	if ((size_t)plen + clen > BVN_PROF_WRITE_MAX)
		return false;
	char joined[BVN_PROF_WRITE_MAX];
	memcpy(joined, pfx, plen);
	memcpy(joined + plen, code, clen);
	uint32_t jlen = plen + clen;

	int32_t                got_decade = 0;
	const bvn_prof_atom_t* got        = find_atom(p, joined, jlen);
	if (!got)
		got = resolve_prefixed(p, joined, jlen, &got_decade);
	return got == want && got_decade == want_decade;
}

/*
 * The prefix for decade `d` whose concatenation with `code` reads back as
 * `want`, shortest spelling first.
 *
 * Same ordering as prefix_for_decade -- producers write "km", not "kilometer" --
 * but a spelling that would resolve to a DIFFERENT atom is skipped rather than
 * emitted. See code_reads_back for what that prevents. Returns NULL when no
 * spelling of this decade survives, which makes the unit unwritable in this
 * profile rather than writable as something else.
 */
static const char* prefix_that_reads_back(const bvn_profile_t* p, int32_t d,
                                          const char* code, uint32_t clen,
                                          const bvn_prof_atom_t* want,
                                          uint32_t* len)
{
	if (d == 0) {
		*len = 0;
		return code_reads_back(p, "", 0u, code, clen, want, 0) ? "" : NULL;
	}
	if (!p->pfx)
		return NULL;
	/* Selection sort over the decade's spellings: take the shortest not yet
	 * tried, test it, and move on. The tables have at most a couple of
	 * spellings per decade, so this stays a scan rather than needing an order. */
	uint32_t tried_len = 0u;
	const char* tried_code = NULL;
	for (;;) {
		const bvn_prof_pfx_t* best = NULL;
		for (const bvn_prof_pfx_t* e = p->pfx; e->code; e++) {
			if (e->decade != d)
				continue;
			/* strictly after the one already tried, in (len, bytes) order */
			if (tried_code &&
			    (e->len < tried_len ||
			     (e->len == tried_len &&
			      memcmp(e->code, tried_code, e->len) <= 0)))
				continue;
			if (!best || e->len < best->len ||
			    (e->len == best->len && memcmp(e->code, best->code, e->len) < 0))
				best = e;
		}
		if (!best)
			return NULL;
		if (code_reads_back(p, best->code, best->len, code, clen, want, d)) {
			*len = best->len;
			return best->code;
		}
		tried_len  = best->len;
		tried_code = best->code;
	}
}

/*
 * How to write one component in this profile: the code, and the prefix that
 * reconciles the component's decade with the code's own.
 *
 * The subtraction is what makes a prefixed atom work. bu_mmhg's UCUM atom is
 * "m[Hg]", a METRE of mercury carrying decade +3, so an unprefixed mmHg wants
 * the UCUM prefix for 0 - 3 = -3, giving "mm[Hg]". Before the reverse table
 * existed, every base in that shape simply had no UCUM form.
 */
static bool write_atom(const bvn_profile_t* p, value_base_unit_t b,
                       value_unit_prefix_t vp,
                       const char** pfx, uint32_t* plen,
                       const char** code, uint32_t* clen)
{
	/*
	 * A BINARY prefix has no decade, so it can only be written by a row that
	 * names that exact prefix as part of its own code -- QUDT's "MebiBYTE". No
	 * profile prefix is emitted alongside it: the code already is the whole
	 * thing.
	 */
	if (vp.system == prefix_iec) {
		if (bvni_is_opaque(b))
			return false;          /* an opaque unit takes no prefix at all */
		for (const bvn_prof_rev_t* e = p->rev; e->code; e++) {
			if (e->base != b || e->iec != vp.id.iec || e->decade != 0)
				continue;
			*pfx  = "";
			*plen = 0;
			*code = e->code;
			*clen = e->len;
			return true;
		}
		return false;
	}
	si_prefix_id_t sp = vp.id.si;
	if (bvni_is_opaque(b)) {
		/* An opaque unit is spelled by the profile that OWNS it. Asking another
		 * profile to write it is not a lookup miss to be searched past -- [IU]
		 * has no UNECE spelling and never will. */
		if (profile_of_opaque(b) != p)
			return false;
		for (const bvn_prof_atom_t* e = p->atoms; e->code; e++) {
			if (e->opaque != b)
				continue;
			int32_t want = si_decade_of(sp);
			if (want != 0 && !e->metric)
				return false;
			*pfx = prefix_that_reads_back(p, want, e->code, e->len, e, plen);
			if (!*pfx)
				return false;
			*code = e->code;
			*clen = e->len;
			return true;
		}
		return false;
	}
	/*
	 * Keep scanning rather than failing on the first row that names `b`. A base
	 * may have SEVERAL rows -- in a flat profile it has one per decade, since
	 * GRM, KGM, MGM and MC are four separate codes for the gram -- and the one
	 * that can carry this component's prefix is not necessarily the first.
	 */
	for (const bvn_prof_rev_t* e = p->rev; e->code; e++) {
		if (e->base != b)
			continue;
		/* A binary-prefixed code cannot spell a decimally-prefixed component:
		 * "MebiBYTE" is not any number of decades of byte. */
		if (e->iec != iec_none)
			continue;
		int32_t want = si_decade_of(sp) - e->decade;
		/* A prefix on a non-metric atom is not legal UCUM, so a component that
		 * would need one has no UCUM spelling at all. In a flat profile there is
		 * no prefix to emit, so the decade has to match outright -- which is
		 * exactly what this test says once every row is non-metric. */
		if (want != 0 && !e->metric)
			continue;
		/* The atom this row names, so the assembled token can be checked
		 * against it. A reverse row always carries an atom's own code, so a
		 * miss here means the two generated tables have diverged. */
		const bvn_prof_atom_t* at = find_atom(p, e->code, e->len);
		const char* got = prefix_that_reads_back(p, want, e->code, e->len,
		                                         at, plen);
		if (!got)
			continue;      /* this spelling would read back as another unit */
		*pfx  = got;
		*code = e->code;
		*clen = e->len;
		return true;
	}
	return false;
}

/*
 * Write `u` as a code in profile `p` (no "<ns>:" prefix).
 *
 * Partial by construction, and in two ways. A native unit outside the
 * transliteration table -- the Old German units, water hardness, turbidity,
 * every currency -- has nowhere to go. And a FLAT profile can only ever write a
 * single unprefixed component, because a flat code is one whole token: there is
 * no operator to join two of them with and no exponent to raise one by. A
 * compound like k~m/h therefore has no flat spelling even though "KMH" parses
 * to exactly it -- which is not a round-trip hole, because that unit has a
 * perfectly good native spelling and only an OPAQUE unit is obliged to come
 * back out in profile notation.
 */
static int32_t unit_to_profile_code(const bvn_profile_t* p, value_unit_t u,
                                    char* buf, size_t bufsize)
{
	if (!buf || bufsize < 2)
		return -1;
	if (!bvn_unit_valid(u))
		return -1;
	uint32_t nc = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	            ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	if (nc == 0) {
		if (bufsize < 2)
			return -1;
		buf[0] = '1';
		buf[1] = '\0';
		return 1;
	}
	if (p->grammar == bvn_prof_grammar_flat && nc != 1)
		return -1;
	size_t pos = 0;
	for (uint32_t i = 0; i < nc; i++) {
		const value_unit_component_t* c = &u.components[i];
		if (c->base == bu_none)
			return -1;
		/* A binary prefix is spellable only where a whole code names it, which
		 * write_atom decides from the reverse table rather than being ruled out
		 * here: UCUM has no binary prefixes and QUDT has "MebiBYTE". */
		const char* pfx  = NULL;
		const char* code = NULL;
		uint32_t    plen = 0, clen = 0;
		if (!write_atom(p, c->base, c->prefix, &pfx, &plen, &code, &clen))
			return -1;
		int32_t e = bvn_exponent_to_int(c->exponent);
		if (e == 0)
			return -1;
		if (p->grammar == bvn_prof_grammar_flat && e != 1)
			return -1;
		/* "-100" is the widest exponent unit_exponent_t can carry, and
		 * check_profile_string_bound in gen_profiles.py sizes the whole
		 * BVNR_UNIT_STRING_MAX budget from that same 4.
		 *
		 * This used to emit ONE character, `(char)('0' + v)`, which was correct
		 * only while exponents stopped at 9. Past that it wrote whatever byte
		 * the arithmetic landed on: m^10 came out as "m:" — a colon, the
		 * profile namespace separator — and m^100 as "m" plus a raw 0x94. The
		 * profile PARSER has accepted two-digit exponents all along (see
		 * scan_exponent), so this was a one-way hole: a unit the library could
		 * read it could not write back. */
		char expbuf[4];
		int  explen = 0;
		if (e != 1) {
			int32_t v = e;
			if (v < 0) {
				expbuf[explen++] = '-';
				v = -v;
			}
			if (v >= 100)
				expbuf[explen++] = (char)('0' + v / 100);
			if (v >= 10)
				expbuf[explen++] = (char)('0' + (v / 10) % 10);
			expbuf[explen++] = (char)('0' + v % 10);
		}
		size_t need = (i ? 1u : 0u) + plen + clen + (size_t)explen;
		if (pos + need + 1 > bufsize)
			return -1;
		if (i)
			buf[pos++] = '.';
		memcpy(buf + pos, pfx, plen);
		pos += plen;
		memcpy(buf + pos, code, clen);
		pos += clen;
		memcpy(buf + pos, expbuf, (size_t)explen);
		pos += (size_t)explen;
	}
	buf[pos] = '\0';
	return (int32_t)pos;
}

int32_t bvni_unit_to_profile(const char* ns, uint32_t ns_len, value_unit_t u,
                             char* buf, size_t bufsize)
{
	const bvn_profile_t* p = profile_by_ns(ns, ns_len);
	if (!p)
		return -1;
	return unit_to_profile_code(p, u, buf, bufsize);
}

int32_t bvni_unit_to_ucum(value_unit_t u, char* buf, size_t bufsize)
{
	return bvni_unit_to_profile("ucum", 4, u, buf, bufsize);
}

int32_t bvni_unit_profile_spelling(value_unit_t u, char* buf, size_t bufsize)
{
	/*
	 * The canonical spelling of a unit that has no native one: "<ns>:<code>",
	 * where <ns> is the profile owning the opaque units in `u`.
	 *
	 * A unit mixing opaque units from two profiles has no single spelling and is
	 * refused rather than written in whichever namespace came first -- the
	 * result would re-parse as something else, which is exactly the round-trip
	 * hole this function exists to close.
	 */
	const bvn_profile_t* owner = NULL;
	uint32_t nc = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	            ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	for (uint32_t i = 0; i < nc; i++) {
		if (!bvni_is_opaque(u.components[i].base))
			continue;
		const bvn_profile_t* p = profile_of_opaque(u.components[i].base);
		if (!p)
			return -1;
		if (owner && owner != p)
			return -1;
		owner = p;
	}
	if (!owner)
		return -1;
	if (!buf || bufsize < (size_t)owner->ns_len + 2u)
		return -1;
	memcpy(buf, owner->ns, owner->ns_len);
	buf[owner->ns_len] = ':';
	int32_t w = unit_to_profile_code(owner, u, buf + owner->ns_len + 1,
	                                 bufsize - owner->ns_len - 1);
	if (w < 0)
		return -1;
	return w + (int32_t)owner->ns_len + 1;
}
