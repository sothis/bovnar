# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""Conversion factors, re-derived from each unit's DEFINITION.

The pint bridge already checks bovnar's factors against pint — but only where
pint has its own definition. For the ~40 units bovnar defines itself
(`bvnr_furlong`, `bvnr_gauss`, the German historical units, the water-hardness
scales …) the pint definition is generated FROM bovnar's factor, so that check
is circular: a wrong number would agree with itself.

This module closes that gap from the other side. Every factor below is written
out from the unit's defining relation — a furlong is 660 international feet, an
acre is 43560 square feet, an oersted is 1000/4π A/m, a Prussian Zoll is a
twelfth of a Prussian Fuß — and compared against the shipped table. It reads
src/gendata/units.bvnr directly with bvnr_data, so it needs no built library and
fails the moment a factor drifts from its own definition.
"""
import math
import os
import sys

import pytest

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, _ROOT)
import bvnr_data  # noqa: E402


@pytest.fixture(scope="module")
def factors():
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        doc = bvnr_data.load(f.read())
    return {a: u["factor"] for u in doc["units"] for a in u["aliases"]}


# --- the defining constants everything else is built from -------------------
G0 = 9.80665                     # standard gravity, CGPM 1901 (exact)
LB = 0.45359237                  # international pound, 1959 (exact)
FT = 0.3048                      # international foot, 1959 (exact)
IN = FT / 12
GAL = 231 * IN ** 3              # US liquid gallon = 231 in³ (exact)
GAL_UK = 4.54609e-3              # imperial gallon, 1985 (exact)
FL_OZ = GAL / 128
OZ_T = 31.1034768e-3             # troy ounce (exact)
NMI = 1852.0                     # nautical mile, 1929 (exact)
BUSHEL = 2150.42 * IN ** 3       # US bushel (exact)
PR_FUSS = 0.313853               # Prussian foot, 1816 — the historical datum

# unit symbol -> (value in coherent SI, how it is defined)
DERIVED = {
    # length
    'in':      (IN,                 "foot / 12"),
    'yd':      (3 * FT,             "3 ft"),
    'mi':      (5280 * FT,          "5280 ft"),
    'fath':    (6 * FT,             "6 ft"),
    'ch':      (66 * FT,            "66 ft (Gunter's chain)"),
    'rd':      (16.5 * FT,          "16.5 ft"),
    'fur':     (660 * FT,           "660 ft = 1/8 mile"),
    'lea':     (3 * 5280 * FT,      "3 miles"),
    'nmi':     (NMI,                "1852 m exactly"),
    'cbl':     (NMI / 10,           "1/10 nautical mile"),
    'thou':    (IN / 1000,          "inch / 1000"),
    'hand':    (4 * IN,             "4 in"),
    'ftUS':    (1200 / 3937,        "US survey foot"),
    'Å':       (1e-10,              "10⁻¹⁰ m"),
    'au':      (149597870700.0,     "IAU 2012 (exact)"),
    'ly':      (9460730472580800.0, "c × Julian year (exact)"),
    'pc':      (149597870700.0 * 648000 / math.pi, "au × 648000/π"),
    # area
    'ac':      (43560 * FT ** 2,    "43560 ft²"),
    'ha':      (1e4,                "10⁴ m²"),
    # volume
    'gal':     (GAL,                "231 in³"),
    'qt':      (GAL / 4,            "gallon / 4"),
    'pt':      (GAL / 8,            "gallon / 8"),
    'cup':     (GAL / 16,           "gallon / 16"),
    'fl_oz':   (FL_OZ,              "gallon / 128"),
    'tbsp':    (FL_OZ / 2,          "fl oz / 2"),
    'tsp':     (FL_OZ / 6,          "fl oz / 6"),
    'fl_dr':   (FL_OZ / 8,          "fl oz / 8"),
    'minim':   (FL_OZ / 480,        "fl oz / 480"),
    'bbl':     (42 * GAL,           "42 US gallons (petroleum)"),
    'gal_uk':  (GAL_UK,             "4.54609 L exactly"),
    'qt_uk':   (GAL_UK / 4,         "imperial gallon / 4"),
    'pt_uk':   (GAL_UK / 8,         "imperial gallon / 8"),
    'fl_oz_uk': (GAL_UK / 160,      "imperial gallon / 160"),
    'gi_uk':   (GAL_UK / 32,        "imperial gallon / 32"),
    'bsh':     (BUSHEL,             "2150.42 in³"),
    'pk':      (BUSHEL / 4,         "bushel / 4"),
    # mass
    'lb':      (LB,                 "0.45359237 kg exactly"),
    'oz':      (LB / 16,            "pound / 16"),
    'dr':      (LB / 256,           "pound / 256"),
    'gr':      (LB / 7000,          "pound / 7000"),
    'st':      (14 * LB,            "14 lb"),
    'tn_sh':   (2000 * LB,          "2000 lb"),
    'tn_l':    (2240 * LB,          "2240 lb"),
    'oz_t':    (OZ_T,               "troy ounce"),
    'dwt':     (OZ_T / 20,          "troy ounce / 20"),
    'ct':      (0.2e-3,             "200 mg exactly"),
    'qntl':    (100.0,              "100 kg"),
    't':       (1000.0,             "1000 kg"),
    # force, pressure, energy, power
    'lbf':     (LB * G0,            "pound × g₀"),
    'kip':     (1000 * LB * G0,     "1000 lbf"),
    'kgf':     (G0,                 "kilogram × g₀"),
    'gn':      (G0,                 "standard gravity"),
    'dyn':     (1e-5,               "10⁻⁵ N"),
    'psi':     (LB * G0 / IN ** 2,  "lbf / in²"),
    'atm':     (101325.0,           "101325 Pa exactly"),
    'at':      (98066.5,            "1 kgf/cm²"),
    'bar':     (1e5,                "10⁵ Pa"),
    'Torr':    (101325 / 760,       "atm / 760"),
    'mmHg':    (133.322387415,      "conventional mmHg"),
    'cal':     (4.184,              "thermochemical calorie"),
    'ft_lb':   (FT * LB * G0,       "foot × lbf"),
    'slug':    (LB * G0 / FT,       "lbf·s²/ft"),
    'inHg':    (25.4 * 133.322387415, "25.4 conventional mmHg"),
    'erg':     (1e-7,               "10⁻⁷ J"),
    'eV':      (1.602176634e-19,    "elementary charge × volt (exact, SI 2019)"),
    'thm':     (1e5 * 1054.804,     "10⁵ BTU(59 °F) — US therm"),
    'hp':      (550 * FT * LB * G0, "550 ft·lbf/s"),
    'PS':      (75 * G0,            "75 kgf·m/s"),
    # speed
    'kn':      (NMI / 3600,         "nautical mile per hour"),
    'mph':     (5280 * FT / 3600,   "mile per hour"),
    'kph':     (1000 / 3600,        "km per hour"),
    'rpm':     (1 / 60,             "revolution per minute (as a cycle rate)"),
    # CGS and photometry
    'Gal':     (0.01,               "cm/s²"),
    'G':       (1e-4,               "10⁻⁴ T"),
    'Mx':      (1e-8,               "10⁻⁸ Wb"),
    'ph':      (1e4,                "10⁴ lx"),
    'sb':      (1e4,                "10⁴ cd/m²"),
    'Oe':      (1000 / (4 * math.pi), "1000/4π A/m"),
    'Ci':      (3.7e10,             "3.7×10¹⁰ Bq exactly"),
    'R':       (2.58e-4,            "2.58×10⁻⁴ C/kg exactly"),
    # time and angle
    'min':     (60.0,               "60 s"),
    'h':       (3600.0,             "3600 s"),
    'd':       (86400.0,            "86400 s"),
    'wk':      (7 * 86400.0,        "7 d"),
    'fn':      (14 * 86400.0,       "14 d"),
    'yr':      (365.25 * 86400.0,   "Julian year"),
    'mo':      (365.25 * 86400.0 / 12, "Julian year / 12"),
    '°':       (math.pi / 180,      "π/180 rad"),
    'grad':    (math.pi / 200,      "π/200 rad"),
    'rev':     (2 * math.pi,        "2π rad"),
    'arcmin':  (math.pi / 10800,    "degree / 60"),
    'arcsec':  (math.pi / 648000,   "degree / 3600"),
    # ratios and dimensionless scales
    '%':       (0.01,               "10⁻²"),
    '‰':       (1e-3,               "10⁻³"),
    '‱':       (1e-4,               "10⁻⁴"),
    'pcm':     (1e-5,               "10⁻⁵"),
    'ppm':     (1e-6,               "10⁻⁶"),
    'ppb':     (1e-9,               "10⁻⁹"),
    'L':       (1e-3,               "10⁻³ m³"),
    # The one MEASURED constant in the registry, so the only entry in this table
    # whose expected value carries an edition rather than a derivation. Stated at
    # CODATA 2022: m_u = 1.66053906892(52)e-27 kg. src/gendata/units.bvnr says the
    # same in its header; if the two ever disagree, this test is what says so.
    'Da':      (1.66053906892e-27,  "unified atomic mass unit (CODATA 2022)"),
    # German historical: everything derives from the 1816 Prussian foot
    'Pfd':     (0.5,                "Zollverein Pfund = 500 g"),
    'Ztr':     (50.0,               "100 Pfund"),
    'dz':      (100.0,              "200 Pfund"),
    'lot':     (0.5 / 32,           "Pfund / 32"),
    'prf':     (PR_FUSS,            "Prussian foot, 1816"),
    'prz':     (PR_FUSS / 12,       "Fuß / 12"),
    'prln':    (PR_FUSS / 144,      "Zoll / 12"),
    'rute':    (12 * PR_FUSS,       "12 Fuß"),
    'klafter': (6 * PR_FUSS,        "6 Fuß"),
    'elle':    (25.5 * PR_FUSS / 12, "25½ Zoll"),
    'morgen':  (180 * (12 * PR_FUSS) ** 2, "180 square Ruten"),
    'schffl':  (0.0549615,          "published Prussian Scheffel, 54.9615 L "
                                    "(the 3072-Kubikzoll derivation gives 54.967 L, "
                                    "the era's own conversion having used a slightly "
                                    "different Fuß)", 5e-5),
    'dt_mi':   (7420.4386,          "geographische Meile = 1/15 of an equatorial degree", 5e-6),
    # water analysis: molar masses are IUPAC 2021
    '°dH':     (10 / (40.078 + 15.999),                       "10 mg CaO/L ÷ M(CaO)"),
    '°fH':     (10 / (40.078 + 12.011 + 3 * 15.999),          "10 mg CaCO₃/L ÷ M(CaCO₃)"),
    '°aH':     (1 / (40.078 + 12.011 + 3 * 15.999),           "1 mg CaCO₃/L ÷ M(CaCO₃)"),
    '°rH':     (1 / 40.078,                                   "1 mg Ca/L ÷ M(Ca)"),
    '°e':      ((64.79891e-3 * 1000 / 4.54609)
                / (40.078 + 12.011 + 3 * 15.999),  "grain CaCO₃ per imperial gallon"),
    'gpg':     ((64.79891e-3 * 1000 / 3.785411784)
                / (40.078 + 12.011 + 3 * 15.999),  "grain CaCO₃ per US gallon"),
    'val':     (0.5,                "equivalent of a divalent ion = ½ mol"),
    'CF':      (0.01,               "EC in mS/cm × 10 = 0.1 mS/cm"),
    # Temperature scales, each from the two fixed points that define it. The
    # SLOPE is what this table checks; the offset is checked against the same
    # fixed points by test_affine_offsets_follow_from_their_fixed_points.
    # Celsius, Fahrenheit and Réaumur are absent because pint defines all three
    # itself, so the bridge already compares them against an outside authority.
    '°De':     (-100 / 150,         "0 °De = 100 °C and 150 °De = 0 °C, so the "
                                    "scale runs backwards at 100/150 °C per degree"),
    '°N':      (100 / 33,           "0 °N = 0 °C and 33 °N = 100 °C"),
    '°Ro':     (100 / (60 - 7.5),   "7.5 °Ro = 0 °C and 60 °Ro = 100 °C"),
    # The interval units mirror their scale's slope with no offset. ΔK is the
    # kelvin itself: the Celsius interval IS the kelvin (SI Brochure 9th ed.
    # §2.3.1), which is why Δ°C is an alias of this row rather than its own.
    'ΔK':      (1.0,                "the kelvin; Δ°C is the same interval"),
    'Δ°De':    (-100 / 150,         "the Delisle degree as an interval — negative "
                                    "because the scale descends"),
    'Δ°N':     (100 / 33,           "the Newton degree as an interval"),
    'Δ°Ro':    (100 / (60 - 7.5),   "the Rømer degree as an interval"),
    # Ozone column. The conventional value pairs 1 DU with 2.687×10²⁰ molecules
    # per square metre; dividing by the Avogadro constant gives the millimolar
    # figure the registry stores, and both are quoted to four figures, so the
    # comparison is to that precision rather than to the last bit.
    'DU':      (2.687e20 / 6.02214076e23,
                "2.687×10²⁰ molecules/m² ÷ N_A = 0.4462 mmol/m²", 2e-4),
}


# A unit defined by arithmetic on exact constants must match to the last bit.
# A historical one is published to about six significant figures, and demanding
# more of it would assert a precision its source never had; those entries carry
# their own tolerance.
_DEFAULT_TOL = 1e-9

# Units whose defining relation is an exact RATIONAL — not a measurement, not a
# published six-figure datum, but arithmetic on values that are exact by
# definition. For these, "close" is the wrong test: the library advertises
# lossless conversion and drives it from the exact rational in the table, so a
# factor that is merely close makes `1 klafter -> prf` report 6.000006 (or, since
# that has no terminating expansion, refuse the conversion as inexact) while
# still claiming to be exact. Compared as Fractions below, with no tolerance at
# all. Each value is written from its definition, independent of the table.
def _exact_definitions():
    from fractions import Fraction as F
    g0  = F(980665, 100000)          # standard gravity
    lb  = F("0.45359237")            # international pound
    ft  = F("0.3048")                # international foot
    prf = F("0.313853")              # Prussian foot, 1816 — the historical datum
    mmhg = F("133.322387415")        # conventional millimetre of mercury
    return {
        # exact multiples of the Prussian Fuß
        'prussian_zoll':  (prf / 12,            "Fuß / 12"),
        'prussian_line':  (prf / 144,           "Zoll / 12"),
        'prussian_elle':  (F(51, 2) * prf / 12, "25½ Zoll"),
        'klafter':        (6 * prf,             "6 Fuß"),
        'prussian_rute':  (12 * prf,            "12 Fuß"),
        'morgen':         (180 * (12 * prf) ** 2, "180 square Ruten"),
        # exact products of the 1959 pound/foot and standard gravity
        'pound_force':    (lb * g0,             "lb × g₀"),
        'kip':            (1000 * lb * g0,      "1000 lbf"),
        'foot_pound':     (ft * lb * g0,        "ft × lbf"),
        'slug':           (lb * g0 / ft,        "lbf·s²/ft"),
        'psi':            (lb * g0 / (ft / 12) ** 2, "lbf / in²"),
        'horsepower':     (550 * ft * lb * g0,  "550 ft·lbf/s"),
        'metric_horsepower': (75 * g0,          "75 kgf·m/s"),
        # exact multiples of the conventional mmHg
        'inch_hg':        (F(254, 10) * mmhg,   "25.4 mmHg"),
        'torr':           (F(101325) / 760,     "atm / 760"),
        # Temperature slopes. None of the three has a terminating expansion, so
        # each is exactly the case this test exists for: a factor rounded to the
        # repr of a double would still be advertised as exact, and every Δ°N or
        # Δ°Ro conversion would then be quietly inexact. Each scale and its
        # interval unit must carry the SAME rational -- that is what makes a
        # difference on the scale convert like the interval.
        'delisle':           (F(-2, 3),   "0 °De = 100 °C, 150 °De = 0 °C"),
        'delta_delisle':     (F(-2, 3),   "the Delisle degree as an interval"),
        'newton_temp':       (F(100, 33), "0 °N = 0 °C, 33 °N = 100 °C"),
        'delta_newton_temp': (F(100, 33), "the Newton degree as an interval"),
        'romer':             (F(40, 21),  "7.5 °Ro = 0 °C, 60 °Ro = 100 °C"),
        'delta_romer':       (F(40, 21),  "the Rømer degree as an interval"),
    }


def test_exactly_defined_factors_are_exactly_right():
    """The factors the lossless path depends on, compared as rationals.

    gen_units only refuses a decimal of 16+ significant digits — the repr of a
    double — so a factor rounded to six or eleven digits sails through and the
    exact-rational engine then reports it as exact. That is how the Klafter came
    to be 6.000006 Prussian Fuß, and ft·lbf 2.3e-11 short of its own definition.
    """
    import gen_units
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        units = {u["name"]: u for u in bvnr_data.load(f.read())["units"]}
    wrong = []
    for name, (want, how) in _exact_definitions().items():
        assert name in units, f"{name} is no longer in the unit table"
        got = gen_units.exact_rational(units[name], "factor")
        if got != want:
            wrong.append("  %-18s table %s, but %s is %s (off by %.2g relative)"
                         % (name, got, how, want, abs(float((got - want) / want))))
    assert not wrong, (
        "%d factor(s) are not exactly their own definition:\n%s"
        % (len(wrong), "\n".join(wrong)))


def test_affine_offsets_follow_from_their_fixed_points():
    """Every affine scale's offset, re-derived from the fixed points.

    The offset is the kelvin value at scale reading zero, so it is a FUNCTION of
    the slope: Rømer's is 273.15 - 7.5 x 40/21 and Fahrenheit's is
    273.15 - 32 x 5/9, neither terminating. Nothing else compares them against
    the definition -- the generated-table test checks the C table against
    units.bvnr, which is the same number twice, and the pint bridge derives its
    own definition from bovnar for the four scales pint does not carry. A wrong
    offset is the one temperature error that survives every conversion test
    written in terms of differences, because differences cancel it.
    """
    import gen_units
    from fractions import Fraction as F
    zero_c = F(27315, 100)                       # 0 °C in kelvin, exact
    want = {
        'celsius':     (zero_c,                    "0 °C = 273.15 K"),
        'fahrenheit':  (zero_c - 32 * F(5, 9),     "0 °F is 32 °F below the ice point"),
        'delisle':     (zero_c + 100,              "0 °De = 100 °C (the scale is inverted)"),
        'newton_temp': (zero_c,                    "0 °N = 0 °C"),
        'reaumur':     (zero_c,                    "0 °Re = 0 °C"),
        'romer':       (zero_c - F(15, 2) * F(40, 21), "7.5 °Ro = 0 °C"),
    }
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        units = {u["name"]: u for u in bvnr_data.load(f.read())["units"]}
    affine = sorted(n for n, u in units.items() if u["affine"])
    assert affine == sorted(want), (
        "the set of affine scales has changed; derive the new one's offset here "
        "rather than letting it ship unchecked: %s" % affine)
    wrong = []
    for name, (expected, how) in want.items():
        got = gen_units.exact_rational(units[name], "offset")
        if got != expected:
            wrong.append("  %-14s table %s, but %s gives %s"
                         % (name, got, how, expected))
    assert not wrong, ("%d affine offset(s) are not their own definition:\n%s"
                       % (len(wrong), "\n".join(wrong)))

    # An interval unit is the same scale with the offset removed. A non-zero one
    # here would reintroduce the 273.15 the Δ units exist to eliminate.
    stray = sorted(n for n, u in units.items()
                   if n.startswith("delta_") and float(u["offset"]) != 0.0)
    assert not stray, "interval units must carry no offset: %s" % stray


def test_generated_tables_match_the_source(factors):
    """The C tables are generated from units.bvnr; check the generated text
    against the document rather than trusting the generator that wrote both.
    Skipped when the build directory has not produced them yet."""
    import re
    from decimal import Decimal
    from fractions import Fraction
    gen = os.path.join(_ROOT, "build", "generated")
    conv_path = os.path.join(gen, "bovnar_si_conv_table.gen.inc")
    if not os.path.exists(conv_path):
        pytest.skip("generated tables not built")
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        units = bvnr_data.load(f.read())["units"]
    dim_names = ["length", "mass", "time", "current", "temperature",
                 "amount", "luminosity"]
    rows = {}
    for line in open(conv_path, encoding="utf-8"):
        # The designated index is BVN_SLOT_NATIVE(bu_x), not bu_x: the table
        # is dense and indexed by a slot, because the id space is blocked and
        # sparse. The unit still names itself in the row's .base field.
        m = re.match(r'\s*\[BVN_SLOT_NATIVE\((bu_\w+)\)\s*\]\s*=\s*'
                     r'\{\s*bu_\w+,\s*([^,]+),'
                     r'\s*\{([^}]*)\},\s*(true|false),\s*([^,]+),'
                     r'\s*"(-?\d+)",\s*"(\d+)",\s*"(-?\d+)",\s*"(\d+)",'
                     r'\s*(true|false)', line)
        if m:
            rows[m.group(1)] = m.groups()[1:]
    assert len(rows) == len(units), (
        f"si_conv_table has {len(rows)} rows for {len(units)} units")
    for u in units:
        key = "bu_" + u["name"]
        factor, dims, affine, offset, fn, fd, on, od, exact = rows[key]
        assert float(factor) == float(u["factor"]), key
        want = [0] * 7
        if isinstance(u["dims"], dict):
            for name, value in u["dims"].items():
                want[dim_names.index(name)] = value
        assert [int(x) for x in dims.split(",")] == want, f"{key} dims"
        assert (affine == "true") == bool(u["affine"]), f"{key} affine"
        assert float(offset) == float(u["offset"]), f"{key} offset"
        assert (exact == "true") == bool(u.get("exact", True)), f"{key} exact"
        # the generator reduces the rational, so compare as fractions
        stated = (Fraction(int(u["factor_num"]), int(u["factor_den"]))
                  if u.get("factor_num") is not None
                  else Fraction(Decimal(repr(float(u["factor"])))))
        assert Fraction(int(fn), int(fd)) == stated, f"{key} rational"


def test_the_parse_table_is_ordered_longest_first():
    """The suffix matcher takes the FIRST match, so a shorter alias appearing
    before a longer one silently changes what a token parses as."""
    import re
    path = os.path.join(_ROOT, "build", "generated", "bovnar_bu_table.gen.inc")
    if not os.path.exists(path):
        pytest.skip("generated tables not built")
    lengths = [int(m.group(1)) for line in open(path, encoding="utf-8")
               for m in [re.match(r'\s*\{\s*.+?,\s*(\d+),\s*bu_\w+\s*\},', line)]
               if m]
    assert lengths, "no rows parsed out of bovnar_bu_table.gen.inc"
    assert lengths == sorted(lengths, reverse=True), \
        "bu_table is not sorted longest-first"
    assert max(lengths) < 32, "an alias exceeds BU_LEN_INDEX_SIZE"


@pytest.mark.parametrize("symbol", sorted(DERIVED), ids=str)
def test_factor_matches_its_definition(symbol, factors):
    entry = DERIVED[symbol]
    want, how = entry[0], entry[1]
    tol = entry[2] if len(entry) > 2 else _DEFAULT_TOL
    assert symbol in factors, f"{symbol} is no longer in the unit table"
    got = factors[symbol]
    assert math.isclose(got, want, rel_tol=tol), (
        f"{symbol}: table says {got!r}, but {how} gives {want!r} "
        f"(relative difference {abs(got - want) / want:.3g}, tolerance {tol:g})")


def test_the_faq_category_breakdown_adds_up():
    """doc/02 answers "how many base units?" with a headline and then a list.

    The headline was updated when units were added; the list was not, so it
    claimed 180 while its own categories summed to 163 — the seventeen water,
    turbidity, salinity, acidity and named-speed units were simply absent. A sum
    is the one kind of stale number a search for the old figure cannot find,
    because it appears nowhere in the text.
    """
    import re
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        total = len(bvnr_data.load(f.read())["units"])
    with open(os.path.join(_ROOT, "doc", "02_bovnar_faq.md"), encoding="utf-8") as f:
        faq = f.read()
    m = re.search(r"(\d+) named base units across the following categories:\n\n"
                  r"((?:- \*\*.*\n)+)", faq)
    assert m, "the FAQ's unit-count answer has moved; update this test"
    headline = int(m.group(1))
    counts = [int(n) for n in re.findall(r"^- \*\*(\d+) ", m.group(2), re.M)]
    assert headline == total, (
        f"doc/02 says {headline} base units, units.bvnr has {total}")
    assert sum(counts) == total, (
        f"doc/02's category list sums to {sum(counts)}, but there are {total} "
        f"units — a category is missing or miscounted")


def test_the_derivation_set_covers_the_self_defined_units(factors):
    """The point of this module is the units pint cannot check independently.
    If a new one is added without a derivation here, say so.

    THE SET IS COMPUTED, NOT LISTED. It used to be a literal of 39 symbols, and
    that is the same shape of defect as the profile gate's native index: a
    hand-written roster can only report on what someone remembered to put in it,
    so a newly added self-defined unit was invisible to the very check meant to
    catch it. The real set is 147, and the 108 the literal omitted were not
    deliberate exemptions -- nobody had looked at them. Deriving the roster from
    the bridge is what makes the guard's silence mean something.

    A unit needs an independent derivation here when pint has no definition of
    its own, i.e. when the bridge points it at a `bvnr_`-prefixed definition
    generated FROM bovnar -- checking that against bovnar is circular. Two
    escapes are legitimate and both are checked rather than assumed: a unit some
    profile maps is compared against that publisher's own published value by
    check_profile_factors, and a unit that is primitive or instrument-defined has
    no arithmetic to be derived from.
    """
    import re
    import sys
    sys.path.insert(0, os.path.join(_ROOT, "python"))
    from bovnar import _pint_units

    unchecked = {
        # primitive or instrument-defined, not derivable by arithmetic
        'b', 'B', 'Bd', 'pH', 'NTU', 'FNU', 'FTU', 'FAU', 'JTU', 'PSU',
        'var', 'VA', 'dB', 'Np', 'slug', 'tex', 'den', 'sc', 'gi',
    }
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        units = bvnr_data.load(f.read())["units"]
    by_id = {u["id"]: u for u in units}
    self_defined = {by_id[i]["symbol"]
                    for i, expr in _pint_units.BASE_UNIT_TO_PINT.items()
                    if i in by_id and "bvnr_" in str(expr)}
    assert len(self_defined) > 100, (
        "only %d self-defined units found; the bridge's shape has changed and "
        "this guard is no longer looking at anything" % len(self_defined))

    # Units a profile maps: check_profile_factors compares each against the
    # publisher's own factor, which is an authority outside bovnar.
    upstream = set()
    for ns in ("ucum", "udunits", "om", "qudt", "unece", "cf"):
        with open(os.path.join(_ROOT, "src", "gendata", ns + ".bvnr"),
                  encoding="utf-8") as f:
            for row in bvnr_data.load(f.read()).get("mapped", []):
                for token in re.split(r"[^A-Za-z0-9_µμΩΩÅÅ°Δ‰‱%]+",
                                      row.get("bovnar", "")):
                    if token:
                        upstream.add(token)

    missing = sorted(self_defined - set(DERIVED) - unchecked - upstream)
    assert not missing, (
        "these units are validated against a pint definition derived from "
        "bovnar itself, no profile maps them to an outside authority, and they "
        "have no independent derivation here: %s" % missing)
