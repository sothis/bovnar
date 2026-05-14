#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"

static int failures = 0;
static int tests    = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg)); \
        failures++; \
    } \
} while (0)

#define ASSERT_FALSE(cond, msg)  ASSERT_TRUE(!(cond), msg)
#define ASSERT_EQ_INT(a, b, msg) ASSERT_TRUE((int64_t)(a) == (int64_t)(b), msg)
#define ASSERT_EQ_DBL(a, b, tol, msg) do { \
    tests++; \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) > (tol)) { \
        fprintf(stderr, "FAIL line %d: %s\n  got %.15g, expected %.15g\n", \
                __LINE__, (msg), _a, _b); \
        failures++; \
    } \
} while (0)

static void test_new_units_in_enum(void)
{
    printf("  new enum values...\n");
    ASSERT_TRUE(bu_radian    > bu_celsius,                 "bu_radian after bu_celsius");
    ASSERT_TRUE(bu_steradian > bu_radian,                  "bu_steradian after bu_radian");
    ASSERT_TRUE(bu_steradian < BVN_VALUE_BASE_UNIT_COUNT,  "bu_steradian before sentinel");
}

static void test_radian_steradian_si_factor(void)
{
    printf("  radian/steradian SI factor...\n");
    bool aff; double off; bool ok = true;

    double f_rad = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_radian),
                                          &aff, &off, &ok);
    ASSERT_EQ_DBL(f_rad, 1.0, 1e-15, "rad → 1.0");
    ASSERT_FALSE(aff, "rad not affine");
    ASSERT_TRUE(ok,   "rad ok");

    double f_sr  = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_steradian),
                                          &aff, &off, &ok);
    ASSERT_EQ_DBL(f_sr, 1.0, 1e-15, "sr → 1.0");
    ASSERT_FALSE(aff, "sr not affine");
    ASSERT_TRUE(ok,   "sr ok");

    double f_mrad = bvn_unit_to_si_factor(BVN_UNIT_SI(bu_radian, si_milli),
                                           &aff, &off, &ok);
    ASSERT_EQ_DBL(f_mrad, 1e-3, 1e-15, "m-rad → 1e-3");

    double f_ksr  = bvn_unit_to_si_factor(BVN_UNIT_SI(bu_steradian, si_kilo),
                                           &aff, &off, &ok);
    ASSERT_EQ_DBL(f_ksr, 1e3, 1e-10, "k-sr → 1e3");
}

static void test_radian_steradian_dim_vector(void)
{
    printf("  radian/steradian dimension vector...\n");
    int32_t dims[7];
    ASSERT_TRUE(bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_radian), dims),
                "rad dimvec ok");
    for (int i = 0; i < 7; i++)
        ASSERT_EQ_INT(dims[i], 0, "rad dim[i] == 0");

    ASSERT_TRUE(bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_steradian), dims),
                "sr dimvec ok");
    for (int i = 0; i < 7; i++)
        ASSERT_EQ_INT(dims[i], 0, "sr dim[i] == 0");
}

static void test_parse_new_symbols(void)
{
    printf("  parse new unit symbols rad/sr...\n");
    bool ok;
    value_unit_t u;

    u = bvn_parse_unit((const uint8_t *)"rad", &ok);
    ASSERT_TRUE(ok,  "parse 'rad' ok");
    ASSERT_EQ_INT(u.num_components, 1, "rad 1 component");
    ASSERT_EQ_INT(u.components[0].base, bu_radian, "rad → bu_radian");

    u = bvn_parse_unit((const uint8_t *)"sr", &ok);
    ASSERT_TRUE(ok,  "parse 'sr' ok");
    ASSERT_EQ_INT(u.num_components, 1, "sr 1 component");
    ASSERT_EQ_INT(u.components[0].base, bu_steradian, "sr → bu_steradian");

    u = bvn_parse_unit((const uint8_t *)"m-rad", &ok);
    ASSERT_TRUE(ok,  "parse 'm-rad' ok");
    ASSERT_EQ_INT(u.components[0].base,        bu_radian, "m-rad base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli, "m-rad prefix");

    u = bvn_parse_unit((const uint8_t *)"rad/s", &ok);
    ASSERT_TRUE(ok, "parse 'rad/s' ok");
    ASSERT_EQ_INT(u.num_components, 2, "rad/s has 2 components");
}

static void test_parse_long_name_aliases(void)
{
    printf("  parse long-name aliases...\n");
    bool ok;
    value_unit_t u;

    struct { const char *s; value_base_unit_t exp; } cases[] = {
        { "sec",        bu_second    },
        { "second",     bu_second    },
        { "seconds",    bu_second    },
        { "meter",      bu_meter     },
        { "metre",      bu_meter     },
        { "meters",     bu_meter     },
        { "metres",     bu_meter     },
        { "gram",       bu_gram      },
        { "grams",      bu_gram      },
        { "ampere",     bu_ampere    },
        { "amperes",    bu_ampere    },
        { "amp",        bu_ampere    },
        { "amps",       bu_ampere    },
        { "kelvin",     bu_kelvin    },
        { "kelvins",    bu_kelvin    },
        { "mole",       bu_mol       },
        { "moles",      bu_mol       },
        { "candela",    bu_candela   },
        { "candelas",   bu_candela   },
        { "hertz",      bu_hertz     },
        { "newton",     bu_newton    },
        { "newtons",    bu_newton    },
        { "pascal",     bu_pascal    },
        { "pascals",    bu_pascal    },
        { "joule",      bu_joule     },
        { "joules",     bu_joule     },
        { "watt",       bu_watt      },
        { "watts",      bu_watt      },
        { "volt",       bu_volt      },
        { "volts",      bu_volt      },
        { "ohm",        bu_ohm       },
        { "ohms",       bu_ohm       },
        { "farad",      bu_farad     },
        { "farads",     bu_farad     },
        { "coulomb",    bu_coulomb   },
        { "coulombs",   bu_coulomb   },
        { "siemens",    bu_siemens   },
        { "weber",      bu_weber     },
        { "webers",     bu_weber     },
        { "tesla",      bu_tesla     },
        { "teslas",     bu_tesla     },
        { "henry",      bu_henry     },
        { "henrys",     bu_henry     },
        { "henries",    bu_henry     },
        { "lumen",      bu_lumen     },
        { "lumens",     bu_lumen     },
        { "lux",        bu_lux       },
        { "becquerel",  bu_becquerel },
        { "becquerels", bu_becquerel },
        { "gray",       bu_gray      },
        { "grays",      bu_gray      },
        { "sievert",    bu_sievert   },
        { "sieverts",   bu_sievert   },
        { "katal",      bu_katal     },
        { "katals",     bu_katal     },
        { "liter",      bu_liter     },
        { "litre",      bu_liter     },
        { "liters",     bu_liter     },
        { "litres",     bu_liter     },
        { "l",          bu_liter     },
        { "celsius",    bu_celsius   },
        { "degree",     bu_degree    },
        { "degrees",    bu_degree    },
        { "degr",       bu_degree    },
        { "deg",        bu_degree    },
        { "degC",       bu_celsius   },
        { "degrC",      bu_celsius   },
        { "hour",       bu_hour      },
        { "hours",      bu_hour      },
        { "minute",     bu_minute    },
        { "minutes",    bu_minute    },
        { "day",        bu_day       },
        { "days",       bu_day       },
        { "bit",        bu_bit       },
        { "bits",       bu_bit       },
        { "byte",       bu_byte      },
        { "bytes",      bu_byte      },
        { "Byte",       bu_byte      },
        { "Bytes",      bu_byte      },
        { "radian",     bu_radian    },
        { "radians",    bu_radian    },
        { "steradian",  bu_steradian },
        { "steradians", bu_steradian },
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < n; i++) {
        u = bvn_parse_unit((const uint8_t *)cases[i].s, &ok);
        char msg[128];
        snprintf(msg, sizeof(msg), "parse '%s' ok", cases[i].s);
        ASSERT_TRUE(ok, msg);
        snprintf(msg, sizeof(msg), "'%s' base unit", cases[i].s);
        ASSERT_EQ_INT(u.components[0].base, (int64_t)cases[i].exp, msg);
    }
}

static void test_prefix_unit_valid_function(void)
{
    printf("  bvn_prefix_unit_valid...\n");

#define PFX_SI(p)  ((value_unit_prefix_t){prefix_si,  .id.si  = (p)})
#define PFX_IEC(p) ((value_unit_prefix_t){prefix_iec, .id.iec = (p)})

    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_none),  bu_meter),
                "si_none on meter OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_kilo),  bu_meter),
                "si_kilo on meter OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_mega),  bu_hertz),
                "si_mega on hertz OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_none),  bu_bit),
                "si_none on bit OK (no-prefix sentinel)");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_none),  bu_byte),
                "si_none on byte OK");

    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_kilo), bu_bit),
                "si_kilo on bit OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_mega), bu_byte),
                "si_mega on byte OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_giga), bu_byte),
                "si_giga on byte OK");

    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_none), bu_bit),
                "iec_none on bit OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_kibi), bu_bit),
                "iec_kibi on bit OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_gibi), bu_byte),
                "iec_gibi on byte OK");
    ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_none), bu_meter),
                "iec_none on meter OK (no-prefix sentinel)");

    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC(iec_kibi), bu_meter),
                 "iec_kibi on meter INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC(iec_mebi), bu_second),
                 "iec_mebi on second INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC(iec_gibi), bu_hertz),
                 "iec_gibi on hertz INVALID");

    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_milli), bu_bit),
                 "si_milli on bit INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_milli), bu_byte),
                 "si_milli on byte INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_micro), bu_bit),
                 "si_micro on bit INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_micro), bu_byte),
                 "si_micro on byte INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_nano),  bu_byte),
                 "si_nano on byte INVALID");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_hecto), bu_bit),
                 "si_hecto on bit INVALID (positive but below kilo)");
    ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_deca),  bu_byte),
                 "si_deca on byte INVALID (positive but below kilo)");

#undef PFX_SI
#undef PFX_IEC
}

static void test_prefix_enforcement_via_parse(void)
{
    printf("  prefix enforcement via bvn_parse_unit...\n");
    bool ok;

    bvn_parse_unit((const uint8_t *)"Ki-B", &ok);
    ASSERT_TRUE(ok, "Ki-B (iec on byte) accepted");

    bvn_parse_unit((const uint8_t *)"Mi-b", &ok);
    ASSERT_TRUE(ok, "Mi-b (iec on bit) accepted");

    bvn_parse_unit((const uint8_t *)"Gi-B", &ok);
    ASSERT_TRUE(ok, "Gi-B (iec on byte) accepted");

    bvn_parse_unit((const uint8_t *)"k-m", &ok);
    ASSERT_TRUE(ok, "k-m (si on meter) accepted");

    bvn_parse_unit((const uint8_t *)"M-Hz", &ok);
    ASSERT_TRUE(ok, "M-Hz (si on hertz) accepted");

    bvn_parse_unit((const uint8_t *)"k-B", &ok);
    ASSERT_TRUE(ok, "k-B (si kilo on byte) accepted");

    bvn_parse_unit((const uint8_t *)"M-b", &ok);
    ASSERT_TRUE(ok, "M-b (si mega on bit) accepted");

    bvn_parse_unit((const uint8_t *)"G-B", &ok);
    ASSERT_TRUE(ok, "G-B (si giga on byte) accepted");

    bvn_parse_unit((const uint8_t *)"k-bit", &ok);
    ASSERT_TRUE(ok, "k-bit (si kilo on bit longform) accepted");

    bvn_parse_unit((const uint8_t *)"M-Byte", &ok);
    ASSERT_TRUE(ok, "M-Byte (si mega on Byte longform) accepted");

    bvn_parse_unit((const uint8_t *)"G-Bytes", &ok);
    ASSERT_TRUE(ok, "G-Bytes (si giga on Bytes longform) accepted");

    bvn_parse_unit((const uint8_t *)"Ki-m", &ok);
    ASSERT_FALSE(ok, "Ki-m (iec on meter) rejected");

    bvn_parse_unit((const uint8_t *)"Mi-s", &ok);
    ASSERT_FALSE(ok, "Mi-s (iec on second) rejected");

    bvn_parse_unit((const uint8_t *)"Gi-J", &ok);
    ASSERT_FALSE(ok, "Gi-J (iec on joule) rejected");

    bvn_parse_unit((const uint8_t *)"Ki-kg", &ok);
    ASSERT_FALSE(ok, "Ki-kg does not parse (no 'kg' symbol) → rejected");

    bvn_parse_unit((const uint8_t *)"B", &ok);
    ASSERT_TRUE(ok, "bare B (no prefix) accepted");

    bvn_parse_unit((const uint8_t *)"b", &ok);
    ASSERT_TRUE(ok, "bare b (no prefix) accepted");

    bvn_parse_unit((const uint8_t *)"m-B", &ok);
    ASSERT_FALSE(ok, "m-B (si milli on byte) rejected");

    bvn_parse_unit((const uint8_t *)"m-b", &ok);
    ASSERT_FALSE(ok, "m-b (si milli on bit) rejected");

    bvn_parse_unit((const uint8_t *)"n-B", &ok);
    ASSERT_FALSE(ok, "n-B (si nano on byte) rejected");

    bvn_parse_unit((const uint8_t *)"\xc2\xb5-b", &ok);
    ASSERT_FALSE(ok, "\xc2\xb5-b (si micro on bit) rejected");

    bvn_parse_unit((const uint8_t *)"m-bit", &ok);
    ASSERT_FALSE(ok, "m-bit (si milli on bit longform) rejected");

    bvn_parse_unit((const uint8_t *)"m-byte", &ok);
    ASSERT_FALSE(ok, "m-byte (si milli on byte longform) rejected");

    bvn_parse_unit((const uint8_t *)"h-B", &ok);
    ASSERT_FALSE(ok, "h-B (si hecto on byte) rejected (positive but below kilo)");

    bvn_parse_unit((const uint8_t *)"da-b", &ok);
    ASSERT_FALSE(ok, "da-b (si deca on bit) rejected (positive but below kilo)");
}

static void test_alias_with_prefix(void)
{
    printf("  aliases work with SI prefixes...\n");
    bool ok;
    value_unit_t u;

    u = bvn_parse_unit((const uint8_t *)"k-seconds", &ok);
    ASSERT_TRUE(ok, "k-seconds ok");
    ASSERT_EQ_INT(u.components[0].base,         bu_second, "k-seconds base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,   "k-seconds prefix");

    u = bvn_parse_unit((const uint8_t *)"m-meters", &ok);
    ASSERT_TRUE(ok, "m-meters ok");
    ASSERT_EQ_INT(u.components[0].base,         bu_meter, "m-meters base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli, "m-meters prefix");

    u = bvn_parse_unit((const uint8_t *)"k-joules", &ok);
    ASSERT_TRUE(ok, "k-joules ok");
    ASSERT_EQ_INT(u.components[0].base,         bu_joule, "k-joules base");

    u = bvn_parse_unit((const uint8_t *)"m-radians", &ok);
    ASSERT_TRUE(ok, "m-radians ok");
    ASSERT_EQ_INT(u.components[0].base,         bu_radian, "m-radians base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli,  "m-radians prefix");

    u = bvn_parse_unit((const uint8_t *)"Ki-bytes", &ok);
    ASSERT_TRUE(ok, "Ki-bytes ok");
    ASSERT_EQ_INT(u.components[0].base,          bu_byte,   "Ki-bytes base");
    ASSERT_EQ_INT(u.components[0].prefix.id.iec, iec_kibi,  "Ki-bytes prefix");

    u = bvn_parse_unit((const uint8_t *)"k-bytes", &ok);
    ASSERT_TRUE(ok, "k-bytes (si kilo on byte alias) accepted");
    ASSERT_EQ_INT(u.components[0].base,         bu_byte,  "k-bytes base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k-bytes prefix");

    u = bvn_parse_unit((const uint8_t *)"k-Bytes", &ok);
    ASSERT_TRUE(ok, "k-Bytes (si kilo on Bytes alias) accepted");
    ASSERT_EQ_INT(u.components[0].base,         bu_byte,  "k-Bytes base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k-Bytes prefix");

    u = bvn_parse_unit((const uint8_t *)"M-Byte", &ok);
    ASSERT_TRUE(ok, "M-Byte (si mega on Byte alias) accepted");
    ASSERT_EQ_INT(u.components[0].base,         bu_byte,  "M-Byte base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_mega,  "M-Byte prefix");

    u = bvn_parse_unit((const uint8_t *)"k-bit", &ok);
    ASSERT_TRUE(ok, "k-bit (si kilo on bit alias) accepted");
    ASSERT_EQ_INT(u.components[0].base,         bu_bit,   "k-bit base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k-bit prefix");

    u = bvn_parse_unit((const uint8_t *)"G-bits", &ok);
    ASSERT_TRUE(ok, "G-bits (si giga on bits alias) accepted");
    ASSERT_EQ_INT(u.components[0].base,         bu_bit,   "G-bits base");
    ASSERT_EQ_INT(u.components[0].prefix.id.si, si_giga,  "G-bits prefix");

    u = bvn_parse_unit((const uint8_t *)"Ki-seconds", &ok);
    ASSERT_FALSE(ok, "Ki-seconds (iec on second alias) rejected");
}

static void test_unit_to_string_new_units(void)
{
    printf("  bvn_unit_to_string for rad/sr...\n");
    char buf[64];
    int32_t r;
    bool ok;
    value_unit_t round;

    r = bvn_unit_to_string(BVN_UNIT_NO_PREFIX(bu_radian), buf, sizeof(buf));
    ASSERT_TRUE(r > 0, "rad to_string returns > 0");
    round = bvn_parse_unit((const uint8_t *)buf, &ok);
    ASSERT_TRUE(ok, "rad serialized string re-parses ok");
    ASSERT_EQ_INT(round.components[0].base, bu_radian, "rad round-trips to bu_radian");

    r = bvn_unit_to_string(BVN_UNIT_NO_PREFIX(bu_steradian), buf, sizeof(buf));
    ASSERT_TRUE(r > 0, "sr to_string returns > 0");
    round = bvn_parse_unit((const uint8_t *)buf, &ok);
    ASSERT_TRUE(ok, "sr serialized string re-parses ok");
    ASSERT_EQ_INT(round.components[0].base, bu_steradian, "sr round-trips to bu_steradian");
}

int main(void)
{
    printf("══════════════════════════════════════\n");
    printf("  Bovnar Unit Extension Test Suite\n");
    printf("══════════════════════════════════════\n\n");

    test_new_units_in_enum();
    test_radian_steradian_si_factor();
    test_radian_steradian_dim_vector();
    test_parse_new_symbols();
    test_parse_long_name_aliases();
    test_prefix_unit_valid_function();
    test_prefix_enforcement_via_parse();
    test_alias_with_prefix();
    test_unit_to_string_new_units();

    printf("\n──────────────────────────────────────\n");
    printf("  Results: %d tests, %d failures\n", tests, failures);
    printf("──────────────────────────────────────\n");
    return failures ? 1 : 0;
}


