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
 * The UCUM unit profile
 * ===========================================================================
 *
 * Specified in doc/11_bovnar_ucum_profile.md. In one paragraph:
 * "ucum:<code>" in the unit slot is an alternative SPELLING, not a second unit
 * model. This file parses the
 * UCUM expression, translates it into an ordinary value_unit_t, and hands that
 * back to bvn_parse_unit's caller. Everything downstream -- equality,
 * compatibility, conversion, the reader and writer unit policies, the DOM -- is
 * untouched and cannot tell which notation the document used.
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
#include "bvn_ucum_impl.h"

/* The one namespace this build defines. Everything else before a ':' is
 * error_unit_profile_unknown, which is how a consumer tells "this build has no
 * UCUM profile" from "that is not a unit". */
#define BVN_UCUM_NS      "ucum"
#define BVN_UCUM_NS_LEN  4u

/* Bounds. UCUM nests parentheses; the depth cap matches the native unit parser's
 * so a hostile document cannot drive either into deep recursion. */
#define BVN_UCUM_MAX_DEPTH 16u

/* ── generated tables ───────────────────────────────────────────────────── */

typedef struct {
	const char* code;
	uint8_t     len;
	int32_t     decade;
} bvn_ucum_pfx_t;

static const bvn_ucum_pfx_t ucum_pfx_table[] = {
#include "bovnar_ucum_prefix.gen.inc"
	{ NULL, 0, 0 }
};

typedef struct {
	const char*       code;
	uint8_t           len;
	/* Native unit expression for a mapped atom; NULL for an arbitrary one. */
	const char*       target;
	/* The arbitrary block id for an arbitrary atom; bu_none for a mapped one. */
	value_base_unit_t arbitrary;
	/* UCUM's own metric flag: may a prefix attach to this atom at all? */
	bool              metric;
} bvn_ucum_atom_t;

static const bvn_ucum_atom_t ucum_atom_table[] = {
#include "bovnar_ucum_atom.gen.inc"
	{ NULL, 0, NULL, bu_none, false }
};

typedef struct {
	const char* code;
	uint8_t     len;
} bvn_ucum_unsup_t;

static const bvn_ucum_unsup_t ucum_unsup_table[] = {
#include "bovnar_ucum_unsupported.gen.inc"
	{ NULL, 0 }
};

/*
 * The inverse of the transliteration: which UCUM atom writes a given base unit,
 * and what decade that atom already carries. Generated, because computing it by
 * re-parsing every mapped target at run time was both slow and wrong -- it
 * picked whichever row the length-sorted parse table reached first, so siemens
 * came out as "mho" and the calorie as "cal_th".
 *
 * `decade` is the atom's own scale relative to the base. UCUM's "m[Hg]" is a
 * METRE of mercury, i.e. bovnar's mmHg times 10^3, so writing plain mmHg means
 * emitting the UCUM prefix for (0 - 3) and producing "mm[Hg]". Bases whose UCUM
 * atom is itself prefixed had no UCUM form at all before this.
 */
typedef struct {
	value_base_unit_t base;
	const char*       code;
	uint8_t           len;
	int32_t           decade;
	bool              metric;
} bvn_ucum_rev_t;

static const bvn_ucum_rev_t ucum_rev_table[] = {
#include "bovnar_ucum_reverse.gen.inc"
	{ bu_none, NULL, 0, 0, false }
};

/* ── the arbitrary block ────────────────────────────────────────────────── */

bool bvni_is_arbitrary(value_base_unit_t base)
{
	return (int)base >= BVN_UCUM_ARBITRARY_FIRST &&
	       (int)base <= BVN_UCUM_ARBITRARY_LAST;
}

bool bvni_unit_has_arbitrary(value_unit_t u)
{
	uint32_t n = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	           ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	for (uint32_t i = 0; i < n; i++) {
		if (bvni_is_arbitrary(u.components[i].base))
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
} bvn_ucum_acc_t;

static bool acc_push(bvn_ucum_acc_t* a, value_unit_component_t c)
{
	if (a->u.num_components >= BVNR_MAX_UNIT_COMPONENTS)
		return false;
	a->u.components[a->u.num_components++] = c;
	return true;
}

/* ── table lookup ───────────────────────────────────────────────────────── */

static const bvn_ucum_atom_t* find_atom(const char* s, uint32_t len)
{
	for (const bvn_ucum_atom_t* e = ucum_atom_table; e->code; e++) {
		if (e->len == len && memcmp(s, e->code, len) == 0)
			return e;
	}
	return NULL;
}

static bool is_known_unsupported(const char* s, uint32_t len)
{
	for (const bvn_ucum_unsup_t* e = ucum_unsup_table; e->code; e++) {
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
static const bvn_ucum_atom_t* resolve_prefixed(const char* s, uint32_t len,
                                               int32_t* decade)
{
	for (const bvn_ucum_pfx_t* p = ucum_pfx_table; p->code; p++) {
		if (p->len >= len || memcmp(s, p->code, p->len) != 0)
			continue;
		const bvn_ucum_atom_t* cand = find_atom(s + p->len, len - p->len);
		/* UCUM permits a prefix only on a metric atom, which is what keeps
		 * "k[arb'U]" an error. A non-metric hit is not a match, so the walk
		 * continues to a shorter prefix rather than stopping here. */
		if (cand && cand->metric) {
			*decade = p->decade;
			return cand;
		}
	}
	return NULL;
}

static bool resolve_prefixed_unsupported(const char* s, uint32_t len)
{
	for (const bvn_ucum_pfx_t* p = ucum_pfx_table; p->code; p++) {
		if (p->len >= len || memcmp(s, p->code, p->len) != 0)
			continue;
		if (is_known_unsupported(s + p->len, len - p->len))
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
static bool fold_decade(bvn_ucum_acc_t* a)
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

static bool parse_expr(const char* s, uint32_t len, uint32_t depth,
                       bvn_ucum_acc_t* acc, bvni_ucum_status_t* status);

/*
 * One UCUM "annotatable": an atom, optionally prefixed, optionally exponentiated,
 * optionally trailed by an annotation. `sign` is +1 in the numerator and -1 in
 * the denominator -- UCUM's '/' is a binary operator that applies to the ONE
 * component that follows it, unlike bovnar's native '/', which latches for the
 * rest of the expression. Getting that wrong turns "kg/m.s2" into kg·m⁻¹·s⁻²
 * when it means kg·m⁻¹·s².
 */
static bool parse_component(const char* s, uint32_t len, int sign,
                            uint32_t depth, bvn_ucum_acc_t* acc,
                            bvni_ucum_status_t* status)
{
	if (len == 0) {
		*status = bvni_ucum_illegal;
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
			*status = bvni_ucum_illegal;
			return false;
		}
		int32_t v = 0;
		for (; k < len; k++) {
			if (s[k] < '0' || s[k] > '9') {
				*status = bvni_ucum_illegal;
				return false;
			}
			v = v * 10 + (s[k] - '0');
			if (v > 400) {               /* far past any SI prefix decade */
				*status = bvni_ucum_unsupported;
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
			*status = bvni_ucum_unsupported;
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
		while (d > 0 && s[d - 1] >= '0' && s[d - 1] <= '9')
			d--;
		if (d < len) {
			uint32_t ds = d;
			if (ds > 0 && (s[ds - 1] == '-' || s[ds - 1] == '+'))
				ds--;
			if (ds == 0) {               /* all digits: handled above */
				*status = bvni_ucum_illegal;
				return false;
			}
			/*
			 * The accumulator is bounded before every multiply. Without it a
			 * long digit run overflowed a signed int -- undefined behaviour, and
			 * in practice "ucum:m999999999999999999" wrapped to a plausible
			 * exponent and parsed as m⁻¹ rather than being refused. A unit
			 * parameter may be 255 bytes, so this is reachable from a document.
			 * The bound is well above the ±9 the format can spell; anything past
			 * it is caught by the range check below.
			 */
			int32_t v = 0;
			for (uint32_t k = d; k < len; k++) {
				if (v > 99) {
					*status = bvni_ucum_unsupported;
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
		*status = bvni_ucum_illegal;
		return false;
	}
	if (exp == 0 || exp > 9 || exp < -9) {
		/* Bovnar spells exponents -9..+9 and has no representation for a
		 * component raised to zero. */
		*status = bvni_ucum_unsupported;
		return false;
	}

	/* Atom, then atom-with-prefix. Whole-token first: a prefix must never win
	 * over a complete atom, or "mho" would read as milli-ho and "min" as
	 * milli-inch -- the same rule the native parser applies from the other end. */
	const char*             abeg = s;
	uint32_t                blen = alen;
	int32_t                 pdec = 0;
	const bvn_ucum_atom_t*  atom = find_atom(abeg, blen);
	if (!atom)
		atom = resolve_prefixed(abeg, blen, &pdec);
	if (!atom) {
		/*
		 * Not translatable. Distinguish "UCUM knows this and we cannot carry it"
		 * from "that is not a UCUM atom" — the whole reason the unsupported list
		 * exists.
		 */
		bool known = is_known_unsupported(abeg, blen) ||
		             resolve_prefixed_unsupported(abeg, blen);
		*status = known ? bvni_ucum_unsupported : bvni_ucum_illegal;
		return false;
	}

	/* Materialise the atom as components. */
	value_unit_t base;
	if (atom->target) {
		bool ok = true;
		base = bvn_parse_unit_n((const uint8_t*)atom->target,
		                        (uint32_t)strlen(atom->target), &ok);
		if (!ok) {
			/* A target that does not parse is a defect in ucum.bvnr, not in the
			 * document. gen_ucum.py checks every target against the registry, so
			 * reaching here means the table and the parser have diverged. */
			*status = bvni_ucum_unsupported;
			return false;
		}
	} else {
		base = BVN_UNIT_NO_PREFIX(atom->arbitrary);
	}

	int32_t  total = sign * exp;
	uint32_t first = acc->u.num_components;
	for (uint32_t i = 0; i < base.num_components; i++) {
		value_unit_component_t c = base.components[i];
		int32_t ce = bvn_exponent_to_int(c.exponent);
		int32_t ne = ce * total;
		if (ne == 0 || ne > 9 || ne < -9) {
			*status = bvni_ucum_unsupported;
			return false;
		}
		c.exponent = bvn_int_to_exponent(ne);
		if (!acc_push(acc, c)) {
			*status = bvni_ucum_unsupported;
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
	(void)depth;
	return true;
}

/*
 * "[/] term { ('.' | '/') term }" -- UCUM's operators, left to right, each '/'
 * inverting only the term that follows it.
 */
static bool parse_expr(const char* s, uint32_t len, uint32_t depth,
                       bvn_ucum_acc_t* acc, bvni_ucum_status_t* status)
{
	if (depth > BVN_UCUM_MAX_DEPTH) {
		*status = bvni_ucum_illegal;
		return false;
	}
	if (len == 0) {
		*status = bvni_ucum_illegal;
		return false;
	}
	uint32_t i    = 0;
	int      sign = 1;
	if (s[0] == '/') {          /* the reciprocal form, "/min" */
		sign = -1;
		i    = 1;
		if (i >= len) {
			*status = bvni_ucum_illegal;
			return false;
		}
	}
	for (;;) {
		if (i >= len) {
			*status = bvni_ucum_illegal;   /* trailing operator */
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
				*status = bvni_ucum_illegal;
				return false;
			}
			bvn_ucum_acc_t sub = { BVN_UNIT_NONE, 0 };
			if (!parse_expr(s + i + 1, j - (i + 1), depth + 1, &sub, status))
				return false;
			for (uint32_t k = 0; k < sub.u.num_components; k++) {
				value_unit_component_t c = sub.u.components[k];
				if (sign < 0)
					c.exponent = bvn_int_to_exponent(
						-bvn_exponent_to_int(c.exponent));
				if (!acc_push(acc, c)) {
					*status = bvni_ucum_unsupported;
					return false;
				}
			}
			acc->decade += sign * sub.decade;
			i = j + 1;
		} else if (s[i] == '{') {
			/* A standalone annotation is the unity: no components, no decade. */
			if (!scan_annotation(s, len, &i)) {
				*status = bvni_ucum_illegal;
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
				else if (bd == 0 && (b == '.' || b == '/' || b == '{' ||
				                     b == '(')) break;
				i++;
			}
			if (bd != 0) {
				*status = bvni_ucum_illegal;   /* unbalanced '[' */
				return false;
			}
			if (!parse_component(s + start, i - start, sign, depth, acc, status))
				return false;
			/* An annotation may trail a component; it changes nothing. */
			if (i < len && s[i] == '{') {
				if (!scan_annotation(s, len, &i)) {
					*status = bvni_ucum_illegal;
					return false;
				}
			}
		}
		if (i >= len)
			break;
		if (s[i] == '.')      sign =  1;
		else if (s[i] == '/') sign = -1;
		else {
			*status = bvni_ucum_illegal;   /* e.g. implicit multiplication */
			return false;
		}
		i++;
	}
	return true;
}

/* ── entry points ───────────────────────────────────────────────────────── */

bool bvni_unit_has_profile(const char* s, uint32_t len)
{
	/* A namespace is lowercase letters followed by ':'. No native unit alias or
	 * currency code contains a colon, so noticing one is enough to hand off --
	 * and a ':' anywhere else in a unit was already an error. */
	for (uint32_t i = 0; i < len; i++) {
		if (s[i] == ':')
			return i > 0;
		if (s[i] < 'a' || s[i] > 'z')
			return false;
	}
	return false;
}

value_unit_t bvni_parse_profile_unit(const char* s, uint32_t len,
                                     bvni_ucum_status_t* status)
{
	value_unit_t none = BVN_UNIT_NONE;
	*status = bvni_ucum_ok;
	uint32_t ns = 0;
	while (ns < len && s[ns] != ':')
		ns++;
	if (ns >= len) {
		*status = bvni_ucum_illegal;
		return none;
	}
	if (ns != BVN_UCUM_NS_LEN || memcmp(s, BVN_UCUM_NS, BVN_UCUM_NS_LEN) != 0) {
		*status = bvni_ucum_unknown_profile;
		return none;
	}
	const char* code = s + ns + 1;
	uint32_t    clen = len - ns - 1;
	if (clen == 0) {
		*status = bvni_ucum_illegal;
		return none;
	}
	bvn_ucum_acc_t acc = { BVN_UNIT_NONE, 0 };
	if (!parse_expr(code, clen, 0, &acc, status))
		return none;
	if (!fold_decade(&acc)) {
		/* A residual scale with no prefix to put it in. There is no prefix for
		 * 10^4, 10^5, 10^7 or 10^8, and none at all when the expression has no
		 * component to carry one ("ucum:10*3" alone). */
		*status = bvni_ucum_unsupported;
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
		*status = bvni_ucum_unsupported;
		return none;
	}
	return acc.u;
}

/* ── writing UCUM back out ──────────────────────────────────────────────── */

/* The UCUM spelling of a decimal decade, or NULL if UCUM has no prefix for it
 * (which is every decade the SI prefixes skip). */
static const char* ucum_prefix_for_decade(int32_t d, uint32_t* len)
{
	if (d == 0) {
		*len = 0;
		return "";
	}
	for (const bvn_ucum_pfx_t* e = ucum_pfx_table; e->code; e++) {
		if (e->decade == d) {
			*len = e->len;
			return e->code;
		}
	}
	return NULL;
}

/*
 * How to write one component as UCUM: the atom, and the UCUM prefix that
 * reconciles the component's decade with the atom's own.
 *
 * The subtraction is what makes a prefixed atom work. bu_mmhg's atom is
 * "m[Hg]", a METRE of mercury carrying decade +3, so an unprefixed mmHg wants
 * the UCUM prefix for 0 - 3 = -3, giving "mm[Hg]". Before the reverse table
 * existed, every base in that shape simply had no UCUM form.
 */
static bool ucum_write_atom(value_base_unit_t b, si_prefix_id_t sp,
                            const char** pfx, uint32_t* plen,
                            const char** code, uint32_t* clen)
{
	if (bvni_is_arbitrary(b)) {
		for (const bvn_ucum_atom_t* e = ucum_atom_table; e->code; e++) {
			if (e->arbitrary != b)
				continue;
			int32_t want = si_decade_of(sp);
			if (want != 0 && !e->metric)
				return false;
			*pfx = ucum_prefix_for_decade(want, plen);
			if (!*pfx)
				return false;
			*code = e->code;
			*clen = e->len;
			return true;
		}
		return false;
	}
	for (const bvn_ucum_rev_t* e = ucum_rev_table; e->code; e++) {
		if (e->base != b)
			continue;
		int32_t want = si_decade_of(sp) - e->decade;
		/* A prefix on a non-metric atom is not legal UCUM, so a component that
		 * would need one has no UCUM spelling at all. */
		if (want != 0 && !e->metric)
			return false;
		*pfx = ucum_prefix_for_decade(want, plen);
		if (!*pfx)
			return false;
		*code = e->code;
		*clen = e->len;
		return true;
	}
	return false;
}

int32_t bvni_unit_to_ucum(value_unit_t u, char* buf, size_t bufsize)
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
	size_t pos = 0;
	for (uint32_t i = 0; i < nc; i++) {
		const value_unit_component_t* c = &u.components[i];
		if (c->base == bu_none)
			return -1;
		/* A binary prefix has no UCUM spelling in this profile. */
		if (c->prefix.system != prefix_si)
			return -1;
		const char* pfx  = NULL;
		const char* code = NULL;
		uint32_t    plen = 0, clen = 0;
		if (!ucum_write_atom(c->base, c->prefix.id.si,
		                     &pfx, &plen, &code, &clen))
			return -1;
		int32_t e = bvn_exponent_to_int(c->exponent);
		if (e == 0)
			return -1;
		char expbuf[4];
		int  explen = 0;
		if (e != 1) {
			int32_t v = e;
			if (v < 0) {
				expbuf[explen++] = '-';
				v = -v;
			}
			expbuf[explen++] = (char)('0' + v);
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
