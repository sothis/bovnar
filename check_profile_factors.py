#!/usr/bin/env python3
"""
check_profile_factors.py — prove the profile tables against their publishers.

THE GAP THIS CLOSES. gen_profiles.py checks that every `.bovnar` target names
something the native registry has, and the per-profile test files check that a
code translates to the unit the table says. Neither checks the table against the
publication the codes come from, and doc/11 §9.2, §10.2 and §10.4 all say so:
five tables wrong in the same way would agree with each other perfectly. This is
the missing half. It reads the publisher's own machine-readable definitions,
resolves each code to a factor and a dimension vector, asks the REFERENCE
LIBRARY what the mapped native target is worth, and compares.

It found, on the tree it was written against: udunits' `oz` mapped to the
avoirdupois ounce when UDUNITS defines it as the US fluid ounce (a volume read
as a mass), the unqualified `calorie` mapped to the thermochemical one when
UDUNITS means the IT calorie, `year`/`month` mapped to the Julian year when
UDUNITS means the tropical one, and `acre`/`chain`/`rod`/`furlong` mapped onto
international lengths when UDUNITS builds them on the US survey foot. Extending
it to QUDT found six local names that vocabulary does not define, a month that
was the lunar one, and a rotational speed 2π out.

WHY IT ASKS THE LIBRARY RATHER THAN COMPUTING THE NATIVE SIDE ITSELF. A second
implementation of the native unit grammar in Python is exactly how a generated
table starts disagreeing with the parser it feeds — gen_profiles.py's own header
says this. So the native side of every comparison goes through bvn_parse_unit
and bvn_unit_to_si_factor via the ctypes bindings. If those are wrong this tool
cannot see it; what it can see is the table disagreeing with the publisher,
which is the failure mode nothing else covers.

THREE OUTCOMES, AND ONLY TWO ARE ERRORS.

  MISMATCH   the code exists upstream and means something else — a different
             dimension, or a factor outside tolerance. This is the one that
             corrupts data silently, and it fails the run.
  DEAD       the table accepts a spelling the publisher does not define. It can
             never be produced by a conforming producer, so it costs nothing but
             an unverifiable row. Reported, not fatal (--strict-dead to change).
  MISSING    the publisher defines a code that is EXACTLY a native unit and the
             table does not map it. Advisory: it is a coverage suggestion, not a
             defect, and the decision to carry a unit is editorial.

WHAT IT DOES NOT CHECK. Four of the five profiles are covered: `ucum`, `udunits`,
`qudt` and `qudt-qk`. UN/ECE Rec 20 is the one left out — it publishes a code
list whose conversion factors are prose, so there is nothing to resolve. (QUDT
does carry a `qudt:uneceCommonCode` cross-reference on many units, which would
give a machine-readable handle on Rec 20 at one remove; it would be QUDT's claim
about UNECE rather than UNECE's own, and is not used here.)

Arbitrary and special units (UCUM `isArbitrary`/`isSpecial`, QUDT units with no
`conversionMultiplier`) have no factor to check and are skipped — they are
exactly the ones bovnar carries as opaque or refuses outright.

QUDT NEEDS NO EVALUATOR. UCUM and UDUNITS state a unit as an EXPRESSION over
other units, so both need a parser; QUDT states each unit's own
`conversionMultiplier` and its dimension vector as an IRI local name
("A0E0L1I0M0H0T0D0"), so reading it is a table lookup. Its quantity kinds have
no multiplier at all, and there the check is the claim doc/11 §12.3 actually
makes: that a kind maps to the COHERENT SI unit of that kind. Reporting a kind
as (1.0, its dimensions) turns that into two ordinary assertions — the
dimensions agree and the native factor is exactly 1 — so a kind mapped to a
non-coherent unit fails on the factor even though its dimensions are perfect.

THE TWO UNIT SYSTEMS ARE NOT THE SAME SYSTEM, and the corrections below are the
substance of the UCUM comparison rather than a detail of it:

  * UCUM's mass base is the GRAM; bovnar's factors are coherent SI, i.e. kg. A
    UCUM factor is divided by 1000^(mass exponent).
  * UCUM has no amount base — the mole is a NUMBER, Avogadro's. Anything with a
    mole in it therefore carries N_A that the bovnar factor does not, so the
    UCUM factor is divided by N_A^(amount exponent), using UCUM's OWN value of
    N_A so it cancels exactly rather than to fifteen digits.
  * UCUM's electrical base is CHARGE (dim Q); bovnar's is current. Q maps to
    current+1 and time+1, which is what makes `A` = `C/s` come out as a bare
    current and `C` as current·time.
  * UCUM's plane angle (dim A) is a base; bovnar's radian is dimensionless. The
    A component is dropped, which is what lets `rad`, `sr` and `deg` compare.

Usage:
    python3 check_profile_factors.py --fetch     # download, then check
    python3 check_profile_factors.py             # check; skip if no cache
    python3 check_profile_factors.py --strict    # a missing cache is a failure
    python3 check_profile_factors.py --verbose   # list every row checked
    python3 check_profile_factors.py --profile ucum
"""
import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.abspath(__file__))
GENDATA = os.path.join(REPO, "src", "gendata")
DEFAULT_CACHE = os.environ.get(
    "BVNR_VOCAB_DIR", os.path.join(REPO, "build", "vocab"))

sys.path.insert(0, REPO)
import bvnr_data  # noqa: E402

# Where each vocabulary's definitions come from. Fetched only on --fetch: a test
# must not depend on the network (check_web_links.py makes the same rule).
SOURCES = {
    "udunits": [
        ("udunits2-base.xml",
         "https://raw.githubusercontent.com/Unidata/UDUNITS-2/master/lib/udunits2-base.xml"),
        ("udunits2-derived.xml",
         "https://raw.githubusercontent.com/Unidata/UDUNITS-2/master/lib/udunits2-derived.xml"),
        ("udunits2-accepted.xml",
         "https://raw.githubusercontent.com/Unidata/UDUNITS-2/master/lib/udunits2-accepted.xml"),
        ("udunits2-common.xml",
         "https://raw.githubusercontent.com/Unidata/UDUNITS-2/master/lib/udunits2-common.xml"),
    ],
    "ucum": [
        ("ucum-essence.xml",
         "https://raw.githubusercontent.com/ucum-org/ucum/main/ucum-essence.xml"),
    ],
    # One file serves both QUDT namespaces' checks; `qudt-qk` additionally needs
    # the quantity-kind vocabulary. Both are served by content negotiation off
    # the versioned base URI, which is the form that actually resolves — the
    # GitHub raw path for the same file 404s.
    "qudt": [
        ("qudt-units.ttl", "https://qudt.org/3.1.0/vocab/unit"),
    ],
    "qudt-qk": [
        ("qudt-units.ttl", "https://qudt.org/3.1.0/vocab/unit"),
        ("qudt-quantitykinds.ttl", "https://qudt.org/3.1.0/vocab/quantitykind"),
    ],
}

# Bovnar's dimension order, as bvn_unit_dimension_vector fills it.
NDIM = 7
LENGTH, MASS, TIME, CURRENT, TEMP, AMOUNT, LUM = range(NDIM)

# Relative tolerance for a factor comparison, CALIBRATED against the two
# publishers rather than picked round.
#
# A publisher states decimals: UDUNITS writes the horsepower as 7.456999e2 W and
# the atomic mass unit with the 1986 CODATA value, so a correct row still
# disagrees in the seventh digit or so. Running this over the whole of both
# tables, every such disagreement lands at or below 6.9e-7 and there is then a
# clear gap before anything real. The genuine errors this tool was written to
# catch are all above it: the US survey foot at 2e-6, the survey acre at 4e-6,
# the tropical year at 2.1e-5, the IT calorie at 6.7e-4.
#
# So 1e-6 sits in the gap. It is empirical and the gap is only a factor of three
# wide, which is worth saying plainly: a real disagreement between 7e-7 and 2e-6
# would pass unnoticed. Nothing better is available without tracking each
# publisher's stated precision through its whole definition chain.
TOL = 1e-6

# Differences that are MODELLING choices rather than errors. bovnar carries bit
# and byte as two base units of information with no factor between them; UCUM
# and UDUNITS both define the byte as the number 8, which is a different and
# equally defensible model, not a wrong conversion.
#
# QUDT goes further than the other two: it models information as ENTROPY, so its
# bit is ln(2) = 0.693 and its byte 8·ln(2) = 5.545, the SI-coherent unit of
# entropy being the nat. bovnar's bit is 1. qudt-qk.bvnr already refuses
# `InformationEntropy` for exactly this reason.
WAIVED_MODEL = {
    ("ucum", "bit"), ("ucum", "By"), ("ucum", "Bd"),
    ("udunits", "bit"), ("udunits", "byte"), ("udunits", "baud"),
    ("udunits", "Bd"), ("udunits", "bps"),
    ("qudt", "BIT"), ("qudt", "BYTE"),
    ("qudt", "KiloBYTE"), ("qudt", "MegaBYTE"), ("qudt", "GigaBYTE"),
    ("qudt", "TeraBYTE"), ("qudt", "KibiBYTE"), ("qudt", "MebiBYTE"),
    ("qudt", "GibiBYTE"), ("qudt", "TebiBYTE"),
}

# Places where the PUBLISHER is wrong, or states a value rounded past the
# tolerance above, and bovnar is deliberately not following it. Each is reported
# as a note on every run rather than silently skipped: a waiver nobody sees is
# how a real regression hides behind an old excuse. Adding to this list is a
# claim about the publisher and needs the same evidence as changing a table.
WAIVED_UPSTREAM = {
    ("ucum", "ph"):
        "UCUM defines the phot as 1e-4 lx. A phot is one lumen per square "
        "centimetre, i.e. 1e4 lx — the value is inverted. Native ph is correct "
        "and the profile keeps mapping to it.",
    ("ucum", "m[Hg]"):
        "UCUM rounds the mercury column to 133.3220 kPa (7 digits); native mmHg "
        "is the exact conventional 133.322387415 Pa. 2.9 ppm, publisher "
        "rounding rather than a different unit.",
    ("qudt", "TORR"):
        "QUDT rounds the torr to 133.322 Pa (6 digits); native Torr is the "
        "exact 101325/760. 2.8 ppm, the same publisher rounding as UCUM's "
        "mercury column.",
}


def zero():
    return [0] * NDIM


class Unresolved(Exception):
    pass


# ── the reference library, via the shipped ctypes bindings ──────────────────

def load_native():
    """(parse, si_factor, dims) bound to the built library, or None.

    Everything native goes through here rather than through a Python
    reimplementation of the unit grammar."""
    sys.path.insert(0, os.path.join(REPO, "python"))
    try:
        import bovnar
        from bovnar import units as bunits
        bovnar._ffi.load_library()
    except Exception:
        return None

    def resolve(text):
        """(factor, dims) for a native unit expression, or None if the library
        refuses it — an affine unit has no multiplicative factor, and that is an
        answer rather than a failure."""
        try:
            u = bovnar.parse_unit(text)
        except Exception:
            raise Unresolved("native parse failed: %r" % text)
        try:
            conv = bunits.unit_to_si_factor(u)
        except Exception:
            return None
        if conv.is_affine:
            return None
        try:
            d = bunits.unit_dimension_vector(u)
        except Exception:
            return None
        return (float(conv.factor), list(d))

    return resolve


# ── UDUNITS-2 ───────────────────────────────────────────────────────────────

UD_PFX = {
    'yotta': 24, 'zetta': 21, 'exa': 18, 'peta': 15, 'tera': 12, 'giga': 9,
    'mega': 6, 'kilo': 3, 'hecto': 2, 'deka': 1, 'deci': -1, 'centi': -2,
    'milli': -3, 'micro': -6, 'nano': -9, 'pico': -12, 'femto': -15,
    'atto': -18, 'zepto': -21, 'yocto': -24,
    'Y': 24, 'Z': 21, 'E': 18, 'P': 15, 'T': 12, 'G': 9, 'M': 6, 'k': 3,
    'h': 2, 'da': 1, 'd': -1, 'c': -2, 'm': -3, 'u': -6, 'µ': -6, 'n': -9,
    'p': -12, 'f': -15, 'a': -18, 'z': -21, 'y': -24,
}
UD_BASE = {
    'm':   (1.0, [1, 0, 0, 0, 0, 0, 0]),
    'kg':  (1.0, [0, 1, 0, 0, 0, 0, 0]),
    's':   (1.0, [0, 0, 1, 0, 0, 0, 0]),
    'A':   (1.0, [0, 0, 0, 1, 0, 0, 0]),
    'K':   (1.0, [0, 0, 0, 0, 1, 0, 0]),
    'mol': (1.0, [0, 0, 0, 0, 0, 1, 0]),
    'cd':  (1.0, [0, 0, 0, 0, 0, 0, 1]),
    'radian': (1.0, zero()), 'rad': (1.0, zero()), 'sr': (1.0, zero()),
}
_NUM = re.compile(r'\d+\.?\d*(?:[eE][-+]?\d+)?')


class Udunits:
    """UDUNITS-2's XML database. Definitions are strings like "6 US_survey_feet"
    or "kg.m2.s-3"; '.' and juxtaposition multiply, '/' divides."""

    name = "udunits"

    def __init__(self, cache):
        self.defs, self.real = {}, set()
        for fn in ("udunits2-base.xml", "udunits2-derived.xml",
                   "udunits2-accepted.xml", "udunits2-common.xml"):
            path = os.path.join(cache, fn)
            root = ET.parse(path).getroot()
            for u in root.findall("unit"):
                d = u.find("def")
                text = d.text.strip() if d is not None and d.text else None
                names = []
                for e in u.findall("symbol"):
                    if e.text:
                        names.append(e.text.strip())
                n = u.find("name")
                if n is not None:
                    for sub in ("singular", "plural"):
                        e = n.find(sub)
                        if e is not None and e.text:
                            names.append(e.text.strip())
                al = u.find("aliases")
                if al is not None:
                    for e in al.findall("symbol"):
                        if e.text:
                            names.append(e.text.strip())
                    for nm in al.findall("name"):
                        for sub in ("singular", "plural"):
                            e = nm.find(sub)
                            if e is not None and e.text:
                                names.append(e.text.strip())
                for x in names:
                    self.defs.setdefault(x, text if text is not None else "@BASE@")
                    self.real.add(x)
        # UDUNITS auto-pluralises unless <noplural/>, so the XML stores only the
        # singular and a def like "12 international_inches" names a spelling
        # that is never a key. Synthesise them -- but keep them OUT of `real`,
        # or every plural would be reported as an unmapped coverage gap.
        for n in list(self.defs):
            for p in (n + "s", n + "es"):
                self.defs.setdefault(p, self.defs[n])
        plain = set(self.defs)
        names = set(plain)
        for nm in plain:
            for p in UD_PFX:
                names.add(p + nm)
        # Longest first: without prefixed spellings in the scan set "mm" reads
        # as m*m and "cm2" as c*m^2, both silently wrong by orders of magnitude.
        self._names = sorted(names, key=len, reverse=True)
        self._cache = {}

    def accepts(self, code):
        """Will UDUNITS take this spelling at all? Includes the auto-plurals,
        which are legal input even though the XML stores only the singular."""
        return code in self.defs or code in UD_BASE

    def spellings(self):
        """Only what the XML states, so a coverage suggestion never proposes
        adding "meterses"."""
        return self.real

    def lookup(self, code, depth=0):
        if depth > 40:
            raise Unresolved(code)
        if code in UD_BASE:
            return UD_BASE[code]
        if code in self._cache:
            return self._cache[code]
        if code in self.defs and self.defs[code] != "@BASE@":
            r = self._eval(self.defs[code], depth + 1)
            self._cache[code] = r
            return r
        for p in sorted(UD_PFX, key=len, reverse=True):
            if code.startswith(p) and len(code) > len(p):
                rest = code[len(p):]
                if rest in self.defs or rest in UD_BASE:
                    f, d = self.lookup(rest, depth + 1)
                    return (f * 10.0 ** UD_PFX[p], d)
        raise Unresolved(code)

    def _scan(self, s):
        out, i, n = [], 0, len(s)
        while i < n:
            ch = s[i]
            if ch.isspace() or ch in '*·.':
                out.append('*')
                i += 1
                continue
            if ch in '/()':
                out.append(ch)
                i += 1
                continue
            if ch == '^':
                i += 1
                m = re.match(r'[-+]?\d+', s[i:])
                if not m:
                    raise Unresolved("bad exponent in %r" % s)
                out.append(('e', int(m.group(0))))
                i += m.end()
                continue
            hit = None
            for nm in self._names:
                if s.startswith(nm, i):
                    hit = nm
                    break
            if hit:
                out.append(('u', hit))
                i += len(hit)
                m2 = re.match(r'[-+]?\d+(?![\d.eE])', s[i:])
                if m2:
                    out.append(('e', int(m2.group(0))))
                    i += m2.end()
                continue
            m = _NUM.match(s, i)
            if m:
                out.append(('n', float(m.group(0))))
                i = m.end()
                continue
            raise Unresolved("char %r in %r" % (ch, s))
        return out

    def _eval(self, s, depth):
        toks = self._scan(s)
        pos = [0]

        def peek():
            return toks[pos[0]] if pos[0] < len(toks) else None

        def nxt():
            t = peek()
            pos[0] += 1
            return t

        def atom():
            t = nxt()
            if t == '(':
                f, d = expr()
                if peek() == ')':
                    nxt()
                return f, d
            if isinstance(t, tuple) and t[0] == 'n':
                return t[1], zero()
            if isinstance(t, tuple) and t[0] == 'u':
                if t[1] == 'pi':
                    import math
                    return math.pi, zero()
                return self.lookup(t[1], depth)
            raise Unresolved("token %r in %r" % (t, s))

        def power():
            f, d = atom()
            while isinstance(peek(), tuple) and peek()[0] == 'e':
                e = nxt()[1]
                f, d = f ** e, [x * e for x in d]
            return f, d

        def expr():
            f, d = power()
            while True:
                p = peek()
                if p == '*':
                    nxt()
                    g, e = power()
                    f *= g
                    d = [a + b for a, b in zip(d, e)]
                elif p == '/':
                    nxt()
                    g, e = power()
                    f /= g
                    d = [a - b for a, b in zip(d, e)]
                elif p is None or p == ')':
                    break
                else:
                    g, e = power()
                    f *= g
                    d = [a + b for a, b in zip(d, e)]
            return f, d

        return expr()

    def resolve(self, code):
        """(factor, dims) in coherent SI, or None when there is nothing to
        compare -- UDUNITS' affine and logarithmic units have no factor."""
        try:
            return self.lookup(code)
        except Unresolved:
            return None
        except RecursionError:
            return None


# ── UCUM ────────────────────────────────────────────────────────────────────

UCUM_NS = {'u': 'http://unitsofmeasure.org/ucum-essence'}
# UCUM's own base dimensions onto bovnar's vector. A (plane angle) is dropped:
# bovnar's radian is dimensionless. Q (charge) is current AND time, which is
# what makes C/s come out as a bare current.
UCUM_DIM = {
    'L': [(LENGTH, 1)],
    'M': [(MASS, 1)],
    'T': [(TIME, 1)],
    'C': [(TEMP, 1)],
    'F': [(LUM, 1)],
    'Q': [(CURRENT, 1), (TIME, 1)],
    'A': [],
}


class Ucum:
    name = "ucum"

    def __init__(self, cache):
        root = ET.parse(os.path.join(cache, "ucum-essence.xml")).getroot()
        self.base, self.defs, self.skip = {}, {}, {}
        for e in root.findall('u:base-unit', UCUM_NS):
            d = zero()
            for idx, v in UCUM_DIM.get(e.get('dim'), []):
                d[idx] += v
            self.base[e.get('Code')] = (1.0, d)
        self.pfx = {e.get('Code'): float(e.find('u:value', UCUM_NS).get('value'))
                    for e in root.findall('u:prefix', UCUM_NS)}
        for e in root.findall('u:unit', UCUM_NS):
            code = e.get('Code')
            v = e.find('u:value', UCUM_NS)
            # A special unit is a function (dB, Cel, prism dioptre) and an
            # arbitrary one is assay-defined; neither has a factor to check, and
            # both are exactly what bovnar refuses or carries as opaque.
            if e.get('isSpecial') == 'yes':
                self.skip[code] = 'special'
                continue
            if e.get('isArbitrary') == 'yes':
                self.skip[code] = 'arbitrary'
                continue
            if v is None:
                self.skip[code] = 'no value'
                continue
            self.defs[code] = (v.get('value'), v.get('Unit'))
        known = set(self.defs) | set(self.base)
        names = set(known)
        for nm in known:
            for p in self.pfx:
                names.add(p + nm)
        # Longest-known-code first, or "m[Hg]" scans as m * [Hg] and [Hg] is not
        # an atom at all.
        self._names = sorted(names, key=len, reverse=True)
        self._cache = {}
        self._na = None

    def accepts(self, code):
        """Does UCUM define this atom at all? A special (dB, Cel) or arbitrary
        ([IU]) unit is defined -- it simply has no multiplicative value -- so it
        must count as existing or every one would be reported as a dead row."""
        return code in self.defs or code in self.base or code in self.skip

    def spellings(self):
        """Only atoms with a comparable value; the special and arbitrary ones
        have nothing to offer a coverage suggestion."""
        return set(self.defs) | set(self.base)

    def avogadro(self):
        """UCUM's OWN value of N_A, so the mole correction cancels exactly."""
        if self._na is None:
            self._na = self.lookup('mol')[0]
        return self._na

    def lookup(self, code, depth=0):
        if depth > 40:
            raise Unresolved(code)
        if code in self.base:
            return self.base[code]
        if code in self._cache:
            return self._cache[code]
        if code in self.defs:
            val, unit = self.defs[code]
            f, d = self._eval(unit, depth + 1)
            r = (float(val) * f if val else f, d)
            self._cache[code] = r
            return r
        for p in sorted(self.pfx, key=len, reverse=True):
            if code.startswith(p) and len(code) > len(p):
                rest = code[len(p):]
                if rest in self.defs or rest in self.base:
                    f, d = self.lookup(rest, depth + 1)
                    return (f * self.pfx[p], d)
        raise Unresolved(code)

    def _eval(self, s, depth=0):
        f, d, sign, i, n = 1.0, zero(), 1, 0, len(s)
        while i < n:
            ch = s[i]
            if ch == '.':
                sign = 1
                i += 1
                continue
            if ch == '/':
                sign = -1
                i += 1
                continue
            if ch == '(':
                j, lvl = i + 1, 1
                while j < n and lvl:
                    if s[j] == '(':
                        lvl += 1
                    elif s[j] == ')':
                        lvl -= 1
                    j += 1
                sf, sd = self._eval(s[i + 1:j - 1], depth + 1)
                f *= sf ** sign
                d = [a + sign * b for a, b in zip(d, sd)]
                i = j
                continue
            m = re.match(r'10[*^]([+-]?\d+)', s[i:])
            if m:
                f *= (10.0 ** int(m.group(1))) ** sign
                i += m.end()
                continue
            hit = None
            for nm in self._names:
                if s.startswith(nm, i):
                    hit = nm
                    break
            if hit:
                i += len(hit)
                e2 = re.match(r'[+-]?\d+', s[i:])
                exp = 1
                if e2:
                    exp = int(e2.group(0))
                    i += e2.end()
                bf, bd = self.lookup(hit, depth)
                f *= (bf ** exp) ** sign
                d = [a + sign * exp * b for a, b in zip(d, bd)]
                continue
            m = _NUM.match(s, i)
            if m:
                f *= float(m.group(0)) ** sign
                i = m.end()
                continue
            raise Unresolved("char %r in %r" % (ch, s))
        return f, d

    def resolve(self, code):
        try:
            return self.lookup(code)
        except Unresolved:
            return None
        except RecursionError:
            return None

    def normalise(self, up, native_dims):
        """UCUM's system is not SI's. Bring a UCUM (factor, dims) onto bovnar's
        terms, given what the native side says the dimensions are.

        The mass base is the gram, so a factor carries 1000^(mass exponent) that
        bovnar's does not. And UCUM has no amount base -- the mole is Avogadro's
        NUMBER -- so a factor also carries N_A^(amount exponent). Both are read
        off the NATIVE dimension vector, because that is the side that has an
        amount dimension at all."""
        f, d = up
        f = f / (1000.0 ** native_dims[MASS])
        if native_dims[AMOUNT]:
            f = f / (self.avogadro() ** native_dims[AMOUNT])
        d = list(d)
        d[AMOUNT] = native_dims[AMOUNT]      # UCUM cannot express it; trust native
        return f, d


# ── QUDT ────────────────────────────────────────────────────────────────────

# QUDT states a dimension vector as an IRI local name, "A0E0L1I0M0H0T0D0", and
# the letters are its own: A is amount of substance, E electric current, H
# thermodynamic temperature, I luminous intensity, D a dimensionless marker this
# comparison ignores.
QKDV = re.compile(r'A(-?[\d.]+)E(-?[\d.]+)L(-?[\d.]+)I(-?[\d.]+)'
                  r'M(-?[\d.]+)H(-?[\d.]+)T(-?[\d.]+)D(-?[\d.]+)')
QKDV_SLOT = {'L': LENGTH, 'M': MASS, 'T': TIME,
             'E': CURRENT, 'H': TEMP, 'A': AMOUNT, 'I': LUM}


def parse_qkdv(local):
    """The dimension IRI's local name -> bovnar's vector, or None if any
    exponent is fractional (QUDT has a few; bovnar's are integers)."""
    m = QKDV.fullmatch(local)
    if not m:
        return None
    vals = dict(zip("AELIMHTD", m.groups()))
    d = zero()
    for letter, slot in QKDV_SLOT.items():
        v = float(vals[letter])
        if v != int(v):
            return None
        d[slot] = int(v)
    return d


def parse_ttl(path, subject_prefix):
    """Subject local name -> {predicate local name: [values]}.

    A hand-rolled scan rather than rdflib, which is not a dependency this repo
    has. It is safe on exactly the shape QUDT publishes -- one subject per
    block, starting in column 0, properties indented, terminated by a lone '.'
    -- and it reads only three literal-valued predicates, so it never has to
    understand a quoted string containing a semicolon.
    """
    out, subj, props = {}, None, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith(subject_prefix + ":"):
                if subj:
                    out[subj] = props
                subj = line.strip().split()[0][len(subject_prefix) + 1:]
                props = {}
                continue
            if subj is None:
                continue
            t = line.strip()
            if t == ".":
                out[subj] = props
                subj, props = None, None
                continue
            m = re.match(r'(?:qudt):(\w+)\s+(.+?)\s*[;.]?$', t)
            if m:
                props.setdefault(m.group(1), []).append(m.group(2))
    if subj:
        out[subj] = props
    return out


def _num(vals):
    if not vals:
        return None
    try:
        return float(vals[0].split('^^')[0].strip('"'))
    except ValueError:
        return None


class Qudt:
    """QUDT unit local names. Nothing to evaluate: each unit states its own
    conversionMultiplier and dimension vector, so this is a table read rather
    than an expression parser."""

    name = "qudt"

    def __init__(self, cache):
        self.rows = parse_ttl(os.path.join(cache, "qudt-units.ttl"), "unit")

    def accepts(self, code):
        return code in self.rows

    def spellings(self):
        return set(self.rows)

    def resolve(self, code):
        p = self.rows.get(code)
        if p is None:
            return None
        # An affine unit has no multiplicative factor; the native side returns
        # None for the same reason, so both drop out of the comparison.
        if _num(p.get("conversionOffset")):
            return None
        mult = _num(p.get("conversionMultiplier"))
        dv = p.get("hasDimensionVector")
        if mult is None or not dv:
            return None
        d = parse_qkdv(dv[0].split(":")[-1])
        if d is None:
            return None
        return (mult, d)


class QudtQk(Qudt):
    """QUDT quantity kinds.

    A kind has no scale, so there is no multiplier to compare. What CAN be
    checked is the thing doc/11 §12.3 actually claims: that the code maps to the
    COHERENT SI unit of the kind. That is two assertions -- the dimensions
    agree, and the native target's factor is exactly 1 -- and both fall out of
    the ordinary comparison once a kind is reported as (1.0, its dimensions).
    A row mapped to a non-coherent unit (`FT` for Length, say) fails on the
    factor even though its dimensions are perfect.
    """

    name = "qudt-qk"

    def __init__(self, cache):
        self.rows = parse_ttl(os.path.join(cache, "qudt-quantitykinds.ttl"),
                              "quantitykind")

    def resolve(self, code):
        p = self.rows.get(code)
        if p is None:
            return None
        dv = p.get("hasDimensionVector")
        if not dv:
            return None
        d = parse_qkdv(dv[0].split(":")[-1])
        if d is None:
            return None
        return (1.0, d)


VOCABS = {"udunits": Udunits, "ucum": Ucum, "qudt": Qudt, "qudt-qk": QudtQk}


# ── the comparison ──────────────────────────────────────────────────────────

def close(a, b, tol=TOL):
    if a == b:
        return True
    if b == 0.0:
        return abs(a) < tol
    return abs(a - b) / abs(b) < tol


def check_profile(ns, vocab, native, verbose):
    """-> (mismatches, dead, missing) as lists of printable lines."""
    doc = bvnr_data.load(open(os.path.join(GENDATA, ns + ".bvnr"), "rb").read())
    mapped = doc.get("mapped", [])
    unsupported = {r["code"] for r in doc.get("unsupported", [])}
    opaque = {r["code"] for r in doc.get("opaque", [])}
    declared = {m["code"] for m in mapped} | unsupported | opaque

    mismatch, dead, missing, notes, checked = [], [], [], [], 0

    for m in mapped:
        code, target = m["code"], m["bovnar"]
        if (ns, code) in WAIVED_MODEL:
            if verbose:
                print("    waived  %-24s -> %-14s (modelling difference)"
                      % (code, target))
            continue
        if (ns, code) in WAIVED_UPSTREAM:
            notes.append("  %-20s -> %-14s %s"
                         % (code, target, WAIVED_UPSTREAM[(ns, code)]))
            continue
        if not vocab.accepts(code):
            dead.append("  %-24s -> %-16s not defined by %s"
                        % (code, target, ns.upper()))
            continue
        up = vocab.resolve(code)
        if up is None:
            continue                      # affine/logarithmic: nothing to compare
        nat = native(target)
        if nat is None:
            continue                      # affine target: same
        nf, nd = nat
        uf, ud = vocab.normalise(up, nd) if hasattr(vocab, "normalise") else up
        checked += 1
        if list(ud) != list(nd):
            mismatch.append(
                "  DIM   %-24s -> %-14s %s says %s, native is %s"
                % (code, target, ns.upper(), ud, nd))
        elif not close(uf, nf):
            mismatch.append(
                "  FAC   %-24s -> %-14s %s says %.12g, native is %.12g "
                "(ratio %.10g)" % (code, target, ns.upper(), uf, nf, uf / nf))
        elif verbose:
            print("    ok      %-24s -> %-14s %.10g" % (code, target, nf))

    # Coverage: a publisher's code that IS a native unit exactly, unmapped.
    for code in sorted(vocab.spellings()):
        if code in declared or (ns, code) in WAIVED_MODEL:
            continue
        up = vocab.resolve(code)
        if up is None:
            continue
        try:
            nat = native(code)
        except Unresolved:
            nat = None
        if nat is None:
            continue
        nf, nd = nat
        uf, ud = vocab.normalise(up, nd) if hasattr(vocab, "normalise") else up
        if list(ud) == list(nd) and close(uf, nf, 1e-9):
            missing.append("  %-24s == native %-14s (%.10g)" % (code, code, nf))

    return mismatch, dead, missing, notes, checked


def fetch(cache, which):
    import urllib.request
    os.makedirs(cache, exist_ok=True)
    for ns in which:
        for fn, url in SOURCES[ns]:
            dest = os.path.join(cache, fn)
            with urllib.request.urlopen(url, timeout=30) as r:
                data = r.read()
            with open(dest, "wb") as f:
                f.write(data)
            print("  fetched %-26s %7d bytes" % (fn, len(data)))


def have_cache(cache, ns):
    return all(os.path.exists(os.path.join(cache, fn))
               for fn, _ in SOURCES[ns])


def main(argv):
    ap = argparse.ArgumentParser(
        description="check the unit-profile tables against their publishers")
    ap.add_argument("--fetch", action="store_true",
                    help="download the vocabularies into the cache first")
    ap.add_argument("--cache", default=DEFAULT_CACHE,
                    help="where the publisher files live (default %s)"
                         % DEFAULT_CACHE)
    ap.add_argument("--profile", action="append",
                    choices=sorted(VOCABS), help="restrict to one vocabulary")
    ap.add_argument("--strict", action="store_true",
                    help="a missing cache or library is a failure, not a skip")
    ap.add_argument("--strict-dead", action="store_true",
                    help="also fail on a spelling the publisher does not define")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv[1:])

    which = args.profile or sorted(VOCABS)

    if args.fetch:
        print("check_profile_factors: fetching into %s" % args.cache)
        fetch(args.cache, which)

    native = load_native()
    if native is None:
        msg = ("the reference library is not loadable — set LIBBOVNAR_PATH or "
               "build libbvnr first")
        if args.strict:
            print("check_profile_factors: %s" % msg)
            return 1
        print("check_profile_factors: SKIPPED — %s" % msg)
        return 0

    available = [ns for ns in which if have_cache(args.cache, ns)]
    if not available:
        msg = ("no publisher files under %s — run with --fetch (needs network)"
               % args.cache)
        if args.strict:
            print("check_profile_factors: %s" % msg)
            return 1
        print("check_profile_factors: SKIPPED — %s" % msg)
        return 0

    total_mismatch = total_dead = total_missing = total_checked = 0
    for ns in available:
        vocab = VOCABS[ns](args.cache)
        if args.verbose:
            print("  %s:" % ns)
        mism, dead, missing, notes, checked = check_profile(
            ns, vocab, native, args.verbose)
        total_checked += checked
        if mism:
            print("\n%s — THE TABLE DISAGREES WITH THE PUBLISHER (%d):"
                  % (ns.upper(), len(mism)))
            for line in mism:
                print(line)
        if notes:
            print("\n%s — waived, the publisher is wrong or rounded (%d):"
                  % (ns.upper(), len(notes)))
            for line in notes:
                print(line)
        if dead:
            print("\n%s — spellings %s does not define (%d, not fatal):"
                  % (ns.upper(), ns.upper(), len(dead)))
            for line in dead:
                print(line)
        if missing and args.verbose:
            print("\n%s — publisher codes that are exactly a native unit and "
                  "are unmapped (%d, advisory):" % (ns.upper(), len(missing)))
            for line in missing:
                print(line)
        total_mismatch += len(mism)
        total_dead += len(dead)
        total_missing += len(missing)

    print("\ncheck_profile_factors: %d row(s) compared across %s; "
          "%d mismatch, %d dead, %d unmapped-but-exact%s"
          % (total_checked, ", ".join(available), total_mismatch, total_dead,
             total_missing, "" if args.verbose else
             " (--verbose to list the last two)"))
    if total_mismatch:
        return 1
    if total_dead and args.strict_dead:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
