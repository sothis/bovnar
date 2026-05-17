# Bovnar Unit System — Reference Documentation

> **Applies to:** Bovnar (BVNR) specification version 1.0  
> **Scope:** This document covers the unit system exclusively — syntax, data model, C API, and validation rules.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Syntax — Unit as a Type Parameter](#2-syntax--unit-as-a-type-parameter)
3. [Base Units](#3-base-units)
4. [Prefixes](#4-prefixes)
   - 4.1 [SI Prefixes](#41-si-prefixes)
   - 4.2 [IEC Binary Prefixes](#42-iec-binary-prefixes)
5. [Unit Notation Grammar](#5-unit-notation-grammar)
   - 5.1 [Simple Units](#51-simple-units)
   - 5.2 [Compound Units](#52-compound-units)
   - 5.3 [Separators](#53-separators)
   - 5.4 [Denominator Semantics](#54-denominator-semantics)
6. [Exponents](#6-exponents)
   - 6.1 [Unicode Superscript Form](#61-unicode-superscript-form)
   - 6.2 [ASCII Caret Form](#62-ascii-caret-form)
   - 6.3 [Exponent Edge Cases](#63-exponent-edge-cases)
7. [The `no_unit` Keyword](#7-the-no_unit-keyword)
8. [Constraints and Limits](#8-constraints-and-limits)
9. [C Data Model](#9-c-data-model)
   - 9.1 [Enumerations](#91-enumerations)
   - 9.2 [Structures](#92-structures)
   - 9.3 [Convenience Macros](#93-convenience-macros)
10. [C API Functions](#10-c-api-functions)
    - 10.1 [Parsing a Unit String](#101-parsing-a-unit-string)
    - 10.2 [Serializing a Unit](#102-serializing-a~unit)
    - 10.3 [Prefix Factor and Exponent Queries](#103-prefix-factor-and-exponent-queries)
    - 10.4 [SI Conversion API](#104-si-conversion-api)
11. [Integration with the Parser Event Stream](#11-integration-with-the-parser-event-stream)
12. [Validation Errors](#12-validation-errors)
13. [Annotated Examples](#13-annotated-examples)
    - 13.1 [Physical Quantities](#131-physical-quantities)
    - 13.2 [Digital Storage](#132-digital-storage)
    - 13.3 [Compound SI Quantities](#133-compound-si-quantities)
    - 13.4 [Error Cases](#134-error-cases)

---

## 1. Overview

The Bovnar unit system is an **optional, per-value annotation** that attaches a physical or digital unit to any numeric field. It is part of the broader type annotation (`<family:width,_base,unit>`) and applies to the `uint`, `sint`, and `float` type families. The unit is purely descriptive from the serialization standpoint — Bovnar does not perform dimensional analysis or unit conversion — but it is fully parsed, validated, and made available to the consuming application through a structured C API.

### Design Principles

- **SI-first.** All standard SI base units, all 21 SI-named derived units listed in section 3 (BIPM 2019, excluding °C which is treated as a non-SI unit accepted for use with SI), and all current SI prefixes (quecto … quetta) are supported.
- **Binary-prefix aware.** IEC 80000-13 binary prefixes (kibi … quebi) are supported for digital storage and data-rate quantities.
- **Compound units.** Derived quantities such as velocity (m/s), force (kg·m/s²), or energy (kg·m²/s²) are expressed inline without separate schema definitions, using product and division separators.
- **Two exponent notations.** Both the visually concise Unicode superscript form (`m²`, `s⁻²`) and the ASCII-safe caret form (`m^2`, `s^-2`) are accepted equivalently.
- **Dimensionless values.** The keyword `no_unit` is the canonical representation of a dimensionless quantity; omitting the unit parameter produces the same internal state.

---

## 2. Syntax — Unit as a Type Parameter

The unit occupies the **third positional parameter class** of a type annotation, after the optional bit-width and optional base. The three parameter classes are identified by their content, not by position, and at most one of each class may appear:

```
type-spec       = param-type [ ":" type-param-list ]
type-param-list = type-param { "," type-param }
type-param      = width-param   (* plain decimal integer, e.g. 32    *)
                | base-param    (* "_" + decimal integer,  e.g. _16  *)
                | unit-param    (* everything else,        e.g. m/s  *)
```

The parser classifies each comma-separated token: if it begins with `_` and is followed by digits it is a base parameter; if it is all decimal digits it is a width parameter; otherwise it is treated as a unit string and handed to `bvn_parse_unit` for semantic validation.

### 2.1 Examples of parameter ordering flexibility

All three of the following annotations are equivalent:

```bovnar
.val = <uint:32,_10,no_unit> 42;
.val = <uint:_10,no_unit,32> 42;
.val = <uint:no_unit,_10,32> 42;
```

The unit participates alongside width and base:

```bovnar
.speed     = <float:64,m/s>         9.81;    # 64-bit float, base 10 (default), m/s
.storage   = <uint:64,Ti~B>         2;       # 64-bit uint, tebibytes
.hex_count = <uint:32,_16,no_unit>  "FF";    # 32-bit uint, hex, dimensionless
```

### 2.2 Inline Unit Suffix

As an alternative (or redundant complement) to the type-annotation unit, a unit may be written **directly after a scalar value**, between the value literal and the terminating `;`.  This is called the **inline unit suffix**.

```bovnar
.distance = 1500 m;            # no annotation; inline unit supplies m
.speed    = 9.81 m/s;          # compound inline unit
.mass     = 70.5 k~g;          # SI-prefix inline unit
.storage  = 4 Gi~B;            # IEC-prefix inline unit
.ratio    = 3.14 no_unit;      # explicit dimensionless via inline suffix
```

The inline unit uses the **same character set** and the **same semantic parser** (`bvn_parse_unit`) as the type-annotation unit parameter.  The only syntactic difference is the delimiter: an inline unit is terminated by ASCII whitespace, `#` (comment), or `;`, whereas an annotation unit is terminated by `,` or `>`.

#### Constraints

| Situation | Result |
|-----------|--------|
| No annotation unit; inline unit present | Inline unit becomes the effective unit |
| Annotation has no unit; inline unit present | Inline unit becomes the effective unit |
| Annotation unit present; no inline unit | Annotation unit is the effective unit |
| Annotation unit **equals** inline unit | Valid; the common unit is used |
| Annotation unit **differs** from inline unit | `error_unit_mismatch` |
| Inline unit inside an array element | `error_unexpected_input_byte` |

The inline unit suffix is forbidden inside `[ … ]` array elements.  The lexer detects alphabetic characters or `_` immediately following an array-element value and reports `error_unexpected_input_byte`.

#### Interaction with the type-annotation unit

When both are present, equality is checked **after parsing** via `memcmp` on the complete `value_unit_t` structure (`memcmp(&annotation_unit, &inline_unit, sizeof(value_unit_t))`).  Two unit strings match if and only if `bvn_parse_unit` produces bit-for-bit identical `value_unit_t` values for both.  In practice this means that logically equivalent strings written in different but semantically identical notations (e.g. `m·s⁻¹` vs `m/s`) compare as equal, because both parse to the same internal representation and `bvn_parse_unit` fully initialises every field of every component it writes.

```bovnar
# Annotation and inline agree (different notation, same unit)
.v = <float:64,m/s> 9.81 m·s⁻¹;   # OK: both parse to m/s

# Annotation and inline disagree
.v = <float:64,m> 1.0 s;           # ERROR: error_unit_mismatch
```

### Applicable type families

| Type family | Unit parameter |
|-------------|---------------|
| `uint`       | Supported      |
| `sint`       | Supported      |
| `float`      | Supported      |
| `float_fix`  | Supported      |
| `float_dec`  | Supported      |
| `utf8`       | Lexically accepted and stored, but semantically ignored (no error raised) |

---

## 3. Base Units

Bovnar supports 133 named base units, covering SI base units, all named SI-derived units, non-SI units accepted for use with SI (BIPM Table 8/9/10), Imperial and US customary units, CGS electromagnetic and mechanical units, radiation units, electrical power units, and surveying and culinary measure units.

### SI Base Units

| Symbol | Name     | Enum value   | Notes |
|--------|----------|--------------|-------|
| `s`    | second   | `bu_second`  | SI base unit of time |
| `m`    | meter    | `bu_meter`   | SI base unit of length |
| `g`    | gram     | `bu_gram`    | SI base unit of mass is kg; `g` allows the prefix to carry the `k` |
| `A`    | ampere   | `bu_ampere`  | SI base unit of electric current |
| `K`    | kelvin   | `bu_kelvin`  | SI base unit of thermodynamic temperature |
| `mol`  | mole     | `bu_mol`     | SI base unit of amount of substance |
| `cd`   | candela  | `bu_candela` | SI base unit of luminous intensity |

> **Note on the kilogram:** The SI base unit of mass is the kilogram, but Bovnar uses `g` (gram) as the base unit symbol so that the `k~` (kilo) SI prefix can be attached explicitly: `k~g` = kilogram. This is consistent with how the SI formally defines kilogram as a prefixed gram.

### Named SI-Derived Units

| Symbol | Name       | Enum value      | SI Definition |
|--------|------------|-----------------|---------------|
| `Hz`   | hertz      | `bu_hertz`      | s⁻¹ |
| `N`    | newton     | `bu_newton`     | kg·m·s⁻² |
| `Pa`   | pascal     | `bu_pascal`     | kg·m⁻¹·s⁻² |
| `J`    | joule      | `bu_joule`      | kg·m²·s⁻² |
| `W`    | watt       | `bu_watt`       | kg·m²·s⁻³ |
| `V`    | volt       | `bu_volt`       | kg·m²·A⁻¹·s⁻³ |
| `Ω`    | ohm        | `bu_ohm`        | kg·m²·A⁻²·s⁻³ — U+2126, UTF-8: `0xE2 0x84 0xA6` |
| `F`    | farad      | `bu_farad`      | kg⁻¹·m⁻²·A²·s⁴ |
| `C`    | coulomb    | `bu_coulomb`    | A·s |
| `S`    | siemens    | `bu_siemens`    | kg⁻¹·m⁻²·A²·s³ |
| `Wb`   | weber      | `bu_weber`      | kg·m²·A⁻¹·s⁻² |
| `T`    | tesla      | `bu_tesla`      | kg·A⁻¹·s⁻² |
| `H`    | henry      | `bu_henry`      | kg·m²·A⁻²·s⁻² |
| `lm`   | lumen      | `bu_lumen`      | cd·sr |
| `lx`   | lux        | `bu_lux`        | cd·sr·m⁻² |
| `Bq`   | becquerel  | `bu_becquerel`  | s⁻¹ |
| `Gy`   | gray       | `bu_gray`       | m²·s⁻² |
| `Sv`   | sievert    | `bu_sievert`    | m²·s⁻² |
| `kat`  | katal      | `bu_katal`      | mol·s⁻¹ |
| `rad`  | radian     | `bu_radian`     | dimensionless (plane angle; m/m) |
| `sr`   | steradian  | `bu_steradian`  | dimensionless (solid angle; m²/m²) |

### Non-SI Units Accepted for Use with SI

| Symbol | Name              | Enum value    | Notes |
|--------|-------------------|---------------|-------|
| `L`, `l` | liter           | `bu_liter`    | 10⁻³ m³ |
| `min`  | minute            | `bu_minute`   | 60 s |
| `h`    | hour              | `bu_hour`     | 3600 s |
| `d`    | day               | `bu_day`      | 86400 s |
| `wk`   | week              | `bu_week`     | 604800 s |
| `yr`   | year              | `bu_year`     | 31557600 s (Julian year) |
| `°`, `deg`, `degr`, `degree`, `degrees` | degree (angle) | `bu_degree` | π/180 rad — U+00B0, UTF-8: `0xC2 0xB0` |
| `°C`, `degC`, `degrC` | degree Celsius | `bu_celsius` | affine offset to kelvin: K = °C + 273.15 |
| `t`    | tonne             | `bu_tonne`    | 10³ kg |
| `bar`  | bar               | `bu_bar`      | 10⁵ Pa |
| `eV`   | electronvolt      | `bu_electronvolt` | 1.602176634×10⁻¹⁹ J |
| `Da`   | dalton            | `bu_dalton`   | 1.66053906660×10⁻²⁷ kg (unified atomic mass unit) |
| `au`   | astronomical unit | `bu_astronomical_unit` | 1.495978707×10¹¹ m |
| `ha`   | hectare           | `bu_hectare`  | 10⁴ m² |

### Imperial and US Customary Units — Length

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `in`   | `inch`, `inches` | inch | `bu_inch` | 0.0254 m (exact) |
| `ft`   | `foot`, `feet`   | foot | `bu_foot` | 0.3048 m (exact) |
| `yd`   | `yard`, `yards`  | yard | `bu_yard` | 0.9144 m (exact) |
| `mi`   | `mile`, `miles`  | statute mile | `bu_mile` | 1609.344 m (exact) |
| `nmi`  | `nautical_mile`, `nautical_miles` | nautical mile | `bu_nautical_mile` | 1852 m (exact) |
| `Å` (U+212B) | `angstrom`, `angstroms`, Å (U+00C5) | ångström | `bu_angstrom` | 10⁻¹⁰ m |
| `ly`   | `light_year`, `light_years` | light-year | `bu_light_year` | 9.4607304725808×10¹⁵ m |
| `pc`   | `parsec`, `parsecs` | parsec | `bu_parsec` | 3.085677581491367×10¹⁶ m |
| `fur`  | `furlong`, `furlongs` | furlong | `bu_furlong` | 201.168 m (exact) |
| `fath` | `fathom`, `fathoms` | fathom | `bu_fathom` | 1.8288 m (exact) |

### Imperial and US Customary Units — Mass

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `lb`   | `lbs`, `pound`, `pounds` | pound (avoirdupois) | `bu_pound` | 0.45359237 kg (exact) |
| `oz`   | `ounce`, `ounces` | ounce (avoirdupois) | `bu_ounce` | 0.028349523125 kg (exact) |
| `gr`   | `grain`, `grains` | grain | `bu_grain` | 6.479891×10⁻⁵ kg (exact) |
| `st`   | `stone`, `stones` | stone | `bu_stone` | 6.35029318 kg (exact) |
| `tn_sh`| `short_ton`, `short_tons` | short ton (US ton) | `bu_short_ton` | 907.18474 kg (exact) |
| `tn_l` | `long_ton`, `long_tons` | long ton (UK ton) | `bu_long_ton` | 1016.0469088 kg (exact) |
| `oz_t` | `troy_ounce`, `troy_ounces` | troy ounce | `bu_troy_ounce` | 0.0311034768 kg (exact) |
| `ct`   | `carat`, `carats` | metric carat | `bu_carat` | 2×10⁻⁴ kg (exact) |

### Imperial and US Customary Units — Temperature

| Symbol | Long forms | Name | Enum value | Conversion |
|--------|-----------|------|------------|------------|
| `°F`, `degF`, `degrF` | `fahrenheit` | degree Fahrenheit | `bu_fahrenheit` | affine: K = (°F + 459.67) × 5/9 |

### Pressure Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `atm`  | `atmosphere`, `atmospheres` | standard atmosphere | `bu_atmosphere` | 101325 Pa (exact) |
| `mmHg` | — | millimetre of mercury | `bu_mmhg` | 133.322387415 Pa |
| `Torr` | `torr` | torr | `bu_torr` | 101325/760 Pa ≈ 133.322368 Pa |
| `psi`  | — | pound-force per square inch | `bu_psi` | 6894.757293168361 Pa |

### Energy Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `cal`  | `calorie`, `calories` | thermochemical calorie | `bu_calorie` | 4.184 J (exact) |
| `Btu`  | `BTU`, `btu` | International Table BTU | `bu_btu` | 1055.05585262 J |
| `erg`  | `ergs` | erg | `bu_erg` | 10⁻⁷ J (exact) |
| `thm`  | `therm`, `therms` | US therm | `bu_therm` | 1.05480400×10⁸ J (exact) |

### Power Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `hp`   | `horsepower` | mechanical horsepower | `bu_horsepower` | 745.69987158227 W |

### Force Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `lbf`  | `pound_force` | pound-force | `bu_pound_force` | 4.4482216152605 N |
| `dyn`  | `dyne`, `dynes` | dyne | `bu_dyne` | 10⁻⁵ N (exact) |
| `kip`  | `kips` | kip (kilopound-force) | `bu_kip` | 4448.2216152605 N |

### Speed Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `kn`   | `knot`, `knots` | knot | `bu_knot` | 1852/3600 m/s ≈ 0.514444 m/s |

### Volume Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `gal`  | `gallon`, `gallons` | US liquid gallon | `bu_gallon` | 3.785411784×10⁻³ m³ (exact) |
| `gal_uk` | `gallon_uk`, `gallons_uk` | Imperial gallon | `bu_gallon_uk` | 4.54609×10⁻³ m³ (exact) |
| `qt`   | `quart`, `quarts` | US liquid quart | `bu_quart` | 9.46352946×10⁻⁴ m³ |
| `pt`   | `pint`, `pints` | US liquid pint | `bu_pint` | 4.73176473×10⁻⁴ m³ |
| `cup`  | `cups` | US cup | `bu_cup` | 2.365882365×10⁻⁴ m³ |
| `fl_oz`| `fluid_ounce`, `fluid_ounces` | US fluid ounce | `bu_fluid_ounce` | 2.95735296875×10⁻⁵ m³ |
| `tbsp` | `tablespoon`, `tablespoons` | US tablespoon | `bu_tablespoon` | 1.47867648×10⁻⁵ m³ |
| `tsp`  | `teaspoon`, `teaspoons` | US teaspoon | `bu_teaspoon` | 4.92892159375×10⁻⁶ m³ |
| `bbl`  | `barrel`, `barrels` | petroleum barrel (42 US gal) | `bu_barrel` | 0.158987294928 m³ |

### Area Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `ac`   | `acre`, `acres` | acre | `bu_acre` | 4046.8564224 m² (exact) |
| `barn` | `barns` | barn | `bu_barn` | 10⁻²⁸ m² (exact) |

### Angle Units

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `arcmin` | `arcminute`, `arcminutes` | arcminute | `bu_arcminute` | π/10800 rad |
| `arcsec` | `arcsecond`, `arcseconds` | arcsecond | `bu_arcsecond` | π/648000 rad |
| `grad` | `gradian`, `gradians`, `gon` | gradian | `bu_grad` | π/200 rad |

### CGS Units

| Symbol | Long forms | Name | Enum value | SI equivalent |
|--------|-----------|------|------------|---------------|
| `P`    | `poise`, `poises` | poise (dynamic viscosity) | `bu_poise` | 0.1 Pa·s |
| `St`   | `stokes`, `stoke` | stokes (kinematic viscosity) | `bu_stokes` | 10⁻⁴ m²·s⁻¹ |
| `G`    | `gauss` | gauss (magnetic flux density) | `bu_gauss` | 10⁻⁴ T |
| `Mx`   | `maxwell`, `maxwells` | maxwell (magnetic flux) | `bu_maxwell` | 10⁻⁸ Wb |
| `Oe`   | `oersted`, `oersteds` | oersted (magnetic field strength) | `bu_oersted` | 1000/(4π) A/m ≈ 79.577 A/m |
| `sb`   | `stilb`, `stilbs` | stilb (luminance) | `bu_stilb` | 10⁴ cd/m² |
| `ph`   | `phot`, `phots` | phot (illuminance) | `bu_phot` | 10⁴ lx |
| `Gal`  | `galileo`, `galileos` | galileo (acceleration) | `bu_galileo` | 10⁻² m/s² |

### Radiation Units

| Symbol | Long forms | Name | Enum value | SI equivalent |
|--------|-----------|------|------------|---------------|
| `Ci`   | `curie`, `curies` | curie (radioactivity) | `bu_curie` | 3.7×10¹⁰ Bq |
| `R`    | `roentgen`, `roentgens` | röntgen (radiation exposure) | `bu_roentgen` | 2.58×10⁻⁴ C/kg |
| `rem`  | `rems` | rem (dose equivalent) | `bu_rem` | 10⁻² Sv |

### Logarithmic Units

| Symbol | Long forms | Name | Enum value | Notes |
|--------|-----------|------|------------|-------|
| `Np`   | `neper`, `nepers` | neper | `bu_neper` | dimensionless logarithmic ratio; SI factor 1.0 |
| `dB`   | `decibel`, `decibels` | decibel | `bu_decibel` | dimensionless logarithmic ratio; SI factor 1.0; 1 Np = 20/ln(10) dB ≈ 8.686 dB |

### Imperial Temperature — Absolute Scale

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `Ra`   | `rankine` | degree Rankine | `bu_rankine` | 5/9 K per °Ra — absolute, no affine offset |

> **Rankine vs Fahrenheit:** Rankine is the absolute temperature scale corresponding to Fahrenheit, analogous to how Kelvin relates to Celsius. 0 °Ra = 0 K; 459.67 °Ra = 0 °F = 273.15 K. Rankine is a **linear** unit (not affine): `K = °Ra × 5/9`. Note that the canonical symbol `Ra` is used because the natural symbol `R` is reserved for the röntgen (`bu_roentgen`).

### Imperial and US Customary Units — Mass (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `slug` | `slug`, `slugs` | slug | `bu_slug` | 14.593902937 kg — defined as lbf·s²/ft |

### Length — Precision Engineering

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `thou` | `thou`, `mil`, `mils` | thou (thousandth of an inch) | `bu_thou` | 25.4×10⁻⁶ m (exact) |

> **Thou vs mil:** Both `thou` and `mil` are accepted spellings for 1/1000 of an inch (25.4 µm). The term "thou" is standard in UK engineering; "mil" is common in US manufacturing and PCB layout. The canonical output form is `thou`. Note that `mil` does **not** mean milliradian here; milliradians are written `m~rad`.

### Imperial Volume Units (UK)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `pt_uk`    | `pint_uk`, `pints_uk` | imperial pint | `bu_pint_uk` | 568.26125×10⁻⁶ m³ (= gallon_uk / 8, exact) |
| `fl_oz_uk` | `fluid_ounce_uk`, `fluid_ounces_uk` | imperial fluid ounce | `bu_fluid_ounce_uk` | 28.4130625×10⁻⁶ m³ (= gallon_uk / 160, exact) |
| `qt_uk`    | `quart_uk`, `quarts_uk` | imperial quart | `bu_quart_uk` | 1136.5225×10⁻⁶ m³ (= gallon_uk / 4, exact) |

> **US vs UK volume units:** The existing `pt` (US liquid pint, 473.2 mL), `fl_oz` (US fluid ounce, 29.57 mL), and `qt` (US liquid quart, 946.4 mL) differ substantially from their UK imperial counterparts. The `_uk` suffix variants are added for unambiguous use in UK and Commonwealth contexts.

### Electrical Power Units

| Symbol | Long forms | Name | Enum value | Factor | Notes |
|--------|-----------|------|------------|--------|-------|
| `var`  | `var`, `vars` | var (volt-ampere reactive) | `bu_var` | 1.0 W equivalent | reactive power; same dimensions as W |
| `VA`   | `volt_ampere`, `volt_amperes` | volt-ampere | `bu_volt_ampere` | 1.0 W equivalent | apparent power; same dimensions as W |

> **Watt, var, and VA:** All three units carry the same SI dimensional signature (kg·m²·s⁻³), so `bvn_units_compatible` returns `true` when comparing them. They are kept as distinct base units because they represent physically distinct interpretations of AC power: active power (W), reactive power (var), and apparent power (VA). A Bovnar-aware application can inspect `value_unit_t.components[0].base` to distinguish them after a compatibility check confirms the shared dimension.

### Force Units (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `kgf`  | `kilogram_force` | kilogram-force | `bu_kilogram_force` | 9.80665 N (exact) |

> **Kilogram-force:** 1 kgf is the force exerted by one kilogram of mass under standard gravity (g = 9.80665 m/s²). It is widely used in mechanical engineering, machine ratings, and spring constants. `bvn_units_compatible` treats `kgf` as compatible with `N`, `lbf`, `dyn`, and `kip`.

### Pressure Units (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `inHg` | `inch_hg`, `inch_mercury` | inch of mercury (conventional, 0 °C) | `bu_inch_hg` | 3386.388645 Pa |

> **Inch of mercury:** The conventional inch of mercury is defined at 0 °C (ice point) where mercury has density 13595.1 kg/m³ and g = 9.80665 m/s². It equals exactly 25.4 × 1 mmHg ≈ 3386.389 Pa. Used in US aviation barometric altimetry and weather reporting.

### Rotational Frequency

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `rpm`  | `rpm` | revolutions per minute | `bu_rpm` | 1/60 s⁻¹ |

> **rpm:** Revolutions per minute is the standard unit for rotational speed in engines, motors, and turbines. Since revolutions are dimensionless, rpm has SI dimension s⁻¹ (same as Hz and Bq). The SI conversion factor is 1/60: 1 rpm = 1/60 Hz. `bvn_units_compatible` returns `true` when comparing `rpm` with `Hz` or `Bq`.

### Energy Units (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `ft_lb` | `foot_pound`, `foot_pounds` | foot-pound | `bu_foot_pound` | 1.3558179483 J |

> **Foot-pound:** 1 ft·lbf = 0.3048 m × 4.4482216152605 N = 1.3558179483 J. Commonly used in US engineering for both mechanical energy and torque. Since joule and newton-metre share the same SI dimension vector (kg·m²·s⁻²), `bvn_units_compatible` returns `true` when comparing `ft_lb` with `J`, `cal`, `eV`, and similar energy units.

### Imperial and US Customary Units — Mass (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `dr`   | `dram`, `drams` | dram (avoirdupois) | `bu_dram` | 1.7718451953125×10⁻³ kg (exact) |
| `dwt`  | `pennyweight`, `pennyweights` | pennyweight (troy) | `bu_pennyweight` | 1.55517384×10⁻³ kg (exact) |

> **Dram:** 1 dram = 1/16 ounce (avoirdupois) = 0.028349523125 / 16 kg. Used in pharmaceutical compounding and US culinary contexts.
>
> **Pennyweight:** 1 dwt = 1/20 troy ounce = 0.0311034768 / 20 kg. The troy mass system is used in precious metals trading (gold, silver, platinum).

### Imperial and US Customary Units — Length (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `ch`   | `chain`, `chains` | chain (Gunter's) | `bu_chain` | 20.1168 m (exact) |
| `rd`   | `rod`, `rods` | rod (pole, perch) | `bu_rod` | 5.0292 m (exact) |

> **Chain and rod:** Gunter's chain (1 ch = 66 ft = 20.1168 m) and rod (1 rd = 16.5 ft = 5.0292 m) are the canonical land-survey units in the US public-lands system. One acre = 10 chains × 1 chain = 10 ch² and one chain = 4 rods. Both factors are exact under the international foot definition (1 ft = 0.3048 m exactly).

### Volume Units (Additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `gi`      | `gill`, `gills` | US gill | `bu_gill` | 1.18294118250×10⁻⁴ m³ |
| `gi_uk`   | `gill_uk`, `gills_uk` | imperial gill | `bu_gill_uk` | 1.420653125×10⁻⁴ m³ (exact) |

> **Gill:** The US gill is 4 US fluid ounces (= 1/4 US liquid pint). The imperial gill is 5 imperial fluid ounces (= 1/4 imperial pint = gallon_uk / 32). Both are exact fractions of their respective gallon definitions. As with other US/UK pairs, `gi` and `gi_uk` are dimensionally compatible but numerically distinct.

### Acceleration

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `gn`   | `standard_gravity` | standard gravity | `bu_standard_gravity` | 9.80665 m·s⁻² (exact, BIPM 1901) |

> **Standard gravity** (`gn`): The conventional standard acceleration of free fall, g₀ = 9.80665 m/s² (exact by BIPM/CIPM definition 1901). Dimension vector: m¹·s⁻². Used for g-force notation, specific impulse (Isp), and accelerometer calibration. Dimensionally compatible with the galileo (`Gal`). Prefixes are valid (e.g. `m~gn` = milli-g = 9.80665×10⁻³ m/s²).

### Power

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `PS`   | `CV`, `metric_horsepower` | metric horsepower | `bu_metric_horsepower` | 735.49875 W (exact) |

> **Metric horsepower** (`PS`): Defined as 75 kgf·m/s = 75 × 9.80665 W = 735.49875 W. Also spelled CV (French/Spanish *cheval vapeur*) and pk (Norwegian/Danish). Distinct from the mechanical/imperial horsepower (`hp` = 745.69987… W). Dimensionally compatible with `W`, `hp`, `VA`, and `var`.

### Angle (additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `rev`  | `turn`, `revolution`, `revolutions`, `turns` | revolution | `bu_revolution` | 2π rad ≈ 6.28318530718 |

> **Revolution** (`rev`): One full angular turn = 2π radians = 360° = 400 grad. Dimensionless. Used in shaft-angle notation, encoder counts, and rotational kinematics. Dimensionally compatible with `rad`, `deg`, `grad`. Do not confuse with `rpm`, which is a frequency unit (revolutions per minute = s⁻¹/60).

### Time (additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `mo`   | `month`, `months` | month (Julian) | `bu_month` | 2 629 800 s (= 365.25 d / 12, exact) |
| `fn`   | `fortnight`, `fortnights` | fortnight | `bu_fortnight` | 1 209 600 s (= 14 d, exact) |

> **Month** (`mo`): The Julian month, defined as 365.25 × 86 400 / 12 = 2 629 800 s. This is a fixed-length approximation; calendar months vary between 28 and 31 days. Compatible with `s`, `min`, `h`, `d`, `wk`, `yr`. Note that 12 `mo` equals exactly 1 `yr` by this definition.
>
> **Fortnight** (`fn`): Exactly 14 days = 2 weeks = 1 209 600 s. Common in British English for pay periods, agricultural cycles, and some legal contexts.

### Pressure (additional)

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `at`   | `atmosphere_technical` | atmosphere technical | `bu_atmosphere_technical` | 98 066.5 Pa (= 1 kgf/cm², exact) |

> **Atmosphere technical** (`at`): Defined as the pressure exerted by 1 kilogram-force per square centimetre: 1 kgf/cm² = 9.80665 × 10⁴ Pa = 98 066.5 Pa. Distinct from the standard atmosphere (`atm` = 101 325 Pa). Found in older European engineering literature and legacy pressure gauges. Dimensionally compatible with `Pa`, `bar`, `atm`, `mmHg`, `psi`.

### Textile Linear Density

| Symbol | Long forms | Name | Enum value | Factor |
|--------|-----------|------|------------|--------|
| `tex`  | — | tex | `bu_tex` | 1×10⁻⁶ kg/m (= 1 g/km, ISO 1144) |
| `den`  | `denier`, `deniers` | denier | `bu_denier` | 1/9 000 000 kg/m (= 1 g/9 000 m) |

> **Tex** (`tex`): The SI-coherent unit of linear mass density for fibres and yarns, defined as 1 gram per kilometre = 10⁻⁶ kg/m (ISO 1144:2021). Dimension vector: m⁻¹·kg. SI prefixes are valid (e.g. `m~tex` = millitex = 10⁻⁹ kg/m).
>
> **Denier** (`den`): Traditional unit defined as 1 gram per 9 000 metres ≈ 1.111×10⁻⁷ kg/m. The conversion is exactly 9 den = 1 tex. Smaller denier values indicate finer fibres. Dimensionally compatible with `tex`.

### US Apothecary / Dry Volume (additional)

| Symbol   | Long forms | Name | Enum value | Factor |
|----------|-----------|------|------------|--------|
| `fl_dr`  | `fluid_dram`, `fluid_drams`, `fl_drams` | US fluid dram | `bu_fluid_dram` | 3.6966911953125×10⁻⁶ m³ |
| `minim`  | `minims` | US minim | `bu_minim` | 6.16115199218750×10⁻⁸ m³ |
| `pk`     | `peck`, `pecks` | US dry peck | `bu_peck` | 8.80976754172×10⁻³ m³ |
| `bsh`    | `bushel`, `bushels` | US bushel | `bu_bushel` | 3.523907016688×10⁻² m³ |

> **Fluid dram** (`fl_dr`): Exactly 1/8 US fluid ounce = 3.6966911953125 mL. Used in pharmacy and apothecary measures. Relationships: 60 minim = 1 fl_dr; 8 fl_dr = 1 fl_oz; 256 fl_dr = 1 US pint.
>
> **Minim** (`minim`): The smallest traditional apothecary volume, exactly 1/60 fluid dram ≈ 61.6 µL. Note the symbol `minim` (not `min`, which is the minute) avoids ambiguity.
>
> **Peck** (`pk`): US dry peck = 8.80976754172 L. Used for dry agricultural commodities (apples, grain). 4 pecks = 1 bushel.
>
> **Bushel** (`bsh`): US bushel = 4 pecks ≈ 35.24 L. The fundamental US dry measure for grain; CBOT/CME futures contracts for corn, wheat, and soybeans are denominated in bushels.

### Digital Units

| Symbol | Name | Enum value  |
|--------|------|-------------|
| `b`    | bit  | `bu_bit`    |
| `B`    | byte | `bu_byte`   |

### Sentinel Value

`bu_none` (value `0`) is the internal representation of "no base unit", used for the `no_unit` keyword and as the default when no unit annotation is present.

---

## 4. Prefixes

Prefixes are attached to a base unit symbol with a mandatory `~` separator: `prefix~baseunit`. A bare base unit with no prefix requires no separator.

### 4.1 SI Prefixes

All 24 current SI prefixes are supported, from quecto (10⁻³⁰) to quetta (10³⁰).

| Prefix | Symbol | Factor  | Enum value   |
|--------|--------|---------|--------------|
| quetta | `Q`    | 10³⁰    | `si_quetta`  |
| ronna  | `R`    | 10²⁷    | `si_ronna`   |
| yotta  | `Y`    | 10²⁴    | `si_yotta`   |
| zetta  | `Z`    | 10²¹    | `si_zetta`   |
| exa    | `E`    | 10¹⁸    | `si_exa`     |
| peta   | `P`    | 10¹⁵    | `si_peta`    |
| tera   | `T`    | 10¹²    | `si_tera`    |
| giga   | `G`    | 10⁹     | `si_giga`    |
| mega   | `M`    | 10⁶     | `si_mega`    |
| kilo   | `k`    | 10³     | `si_kilo`    |
| hecto  | `h`    | 10²     | `si_hecto`   |
| deca   | `da`   | 10¹     | `si_deca`    |
| *(no prefix)* | — | 10⁰ | `si_none` |
| deci   | `d`    | 10⁻¹    | `si_deci`    |
| centi  | `c`    | 10⁻²    | `si_centi`   |
| milli  | `m`    | 10⁻³    | `si_milli`   |
| micro  | `µ`    | 10⁻⁶    | `si_micro`   |
| nano   | `n`    | 10⁻⁹    | `si_nano`    |
| pico   | `p`    | 10⁻¹²   | `si_pico`    |
| femto  | `f`    | 10⁻¹⁵   | `si_femto`   |
| atto   | `a`    | 10⁻¹⁸   | `si_atto`    |
| zepto  | `z`    | 10⁻²¹   | `si_zepto`   |
| yocto  | `y`    | 10⁻²⁴   | `si_yocto`   |
| ronto  | `r`    | 10⁻²⁷   | `si_ronto`   |
| quecto | `q`    | 10⁻³⁰   | `si_quecto`  |

> **Encoding note:** `µ` is U+00B5 (MICRO SIGN), UTF-8: `0xC2 0xB5`. Note that U+03BC (GREEK SMALL LETTER MU) is a distinct code point and is **not** accepted as an SI micro prefix.

#### Prefix–symbol ambiguities

Several prefix symbols overlap with base unit symbols. The parser resolves the ambiguity by the required `~` separator: `m~` introduces a prefix (milli), while `m` alone (or `m` followed by a separator such as `/`, `*`, `·`, or end-of-string) is the meter base unit.

| Symbol | As prefix | As base unit |
|--------|-----------|--------------|
| `m`    | milli     | meter        |
| `d`    | deci      | day          |
| `h`    | hecto     | hour         |
| `T`    | tera      | tesla        |
| `f`    | femto     | farad        |
| `a`    | atto      | *(none)*     |
| `S`    | *(none)*  | siemens      |

The `~` is the disambiguator: `d~s` = decisecond; `d` alone = day.

### 4.2 IEC Binary Prefixes

IEC 80000-13 binary prefixes are used for digital quantities (bits and bytes). All 10 current IEC binary prefixes are supported.

| Prefix | Symbol | Factor | Enum value   |
|--------|--------|--------|--------------|
| kibi   | `Ki`   | 2¹⁰    | `iec_kibi`   |
| mebi   | `Mi`   | 2²⁰    | `iec_mebi`   |
| gibi   | `Gi`   | 2³⁰    | `iec_gibi`   |
| tebi   | `Ti`   | 2⁴⁰    | `iec_tebi`   |
| pebi   | `Pi`   | 2⁵⁰    | `iec_pebi`   |
| exbi   | `Ei`   | 2⁶⁰    | `iec_exbi`   |
| zebi   | `Zi`   | 2⁷⁰    | `iec_zebi`   |
| yobi   | `Yi`   | 2⁸⁰    | `iec_yobi`   |
| robi   | `Ri`   | 2⁹⁰    | `iec_robi`   |
| quebi  | `Qi`   | 2¹⁰⁰   | `iec_quebi`  |

IEC prefixes are recognised by their two-character `Xi` suffix pattern and carry `iec_none` as the "no prefix" sentinel (value `0`). They follow the same `~` convention as SI prefixes:

```bovnar
.ram   = <uint:64,Gi~B> 8;       # gibibytes
.cache = <uint:32,Mi~b> 256;     # mibibits
.drive = <uint:64,Ti~B> 2;       # tebibytes
```

#### Prefix–unit validity constraints

Not every prefix may be combined with every base unit. The following rules are enforced by `bvn_prefix_unit_valid` and cause `error_unit_illegal` on violation:

- **IEC prefixes** (`Ki`…`Qi`) are only permitted on `b` (bit) and `B` (byte). Any attempt to attach an IEC prefix to a physical unit (e.g. `Ki~m`) is rejected.
- **SI sub-kilo prefixes** (`da`, `h`, `d`, `c`, `m`, `µ`, `n`, `p`, `f`, `a`, `z`, `y`, `r`, `q`) are forbidden on `b` and `B`. This includes `da` (deca, ×10¹) and `h` (hecto, ×10²), which are positive but still below `k`. Bits and bytes may carry `si_none` or any SI prefix from `k` (kilo) upward.

```bovnar
.valid1   = <uint:64,Ki~B>  8;     # OK: IEC prefix on byte
.valid2   = <uint:32,M~b>   100;   # OK: SI mega on bit
.invalid1 = <uint:64,Ki~m>  1;     # ERROR: IEC prefix on meter
.invalid2 = <uint:32,m~B>   512;   # ERROR: SI milli on byte
```

---

## 5. Unit Notation Grammar

### 5.1 Simple Units

A simple unit consists of an optional prefix, the base unit symbol, and an optional exponent. No separator is required between the prefix and the base unit — the `~` **is** the separator:

```
unit-component = [ prefix "~" ] base-unit [ unit-exponent ]
```

```bovnar
.temperature = <float:64,K>     300.0;  # kelvin (no prefix)
.distance    = <float:64,k~m>   1.5;    # kilometer (kilo + meter)
.frequency   = <float:64,M~Hz>  2400;   # megahertz
.storage     = <uint:64,Ki~B>   1024;   # kibibytes
```

### 5.2 Compound Units

A compound unit is a sequence of unit-components joined by separator characters. The full grammar of a unit string (the **unit sub-grammar**, enforced semantically by `bvn_parse_unit`) is:

```ebnf
compound-unit  = "no_unit"
               | unit-component { unit-sep unit-component }

unit-sep       = "*" | "/" | "·"        (* · = U+00B7 MIDDLE DOT *)

unit-component = [ prefix "~" ] base-unit [ unit-exponent ]

unit-exponent  = [ exp-sign ] exp-digit
               | "^" [ "-" | "+" ] ASCII-digit

exp-sign       = "⁺"   (* U+207A, positive, no-op *)
               | "⁻"   (* U+207B, negate exponent *)

exp-digit      = "¹" | "²" | "³" | "⁴" | "⁵"
               | "⁶" | "⁷" | "⁸" | "⁹"

si-prefix      = "Q"|"R"|"Y"|"Z"|"E"|"P"|"T"|"G"|"M"|"k"|"h"|"da"
               | "d"|"c"|"m"|"µ"|"n"|"p"|"f"|"a"|"z"|"y"|"r"|"q"

iec-prefix     = "Ki"|"Mi"|"Gi"|"Ti"|"Pi"|"Ei"|"Zi"|"Yi"|"Ri"|"Qi"

base-unit      = "b"|"B"|"s"|"m"|"g"|"A"|"K"|"mol"|"cd"|"Hz"|"N"
               | "Pa"|"J"|"W"|"V"|"Ω"|"F"|"C"|"S"|"Wb"|"T"|"H"
               | "lm"|"lx"|"Bq"|"Gy"|"Sv"|"kat"|"rad"|"sr"
               | "L"|"l"|"min"|"h"|"d"|"wk"|"yr"
               | "°C"|"°"|"degC"|"degrC"
               | "degrees"|"degree"|"degr"|"deg"
               | "t"|"bar"|"eV"|"Da"|"au"|"ha"
```

> This sub-grammar is **semantic**, not lexical. The outer lexer captures the entire type annotation body as a raw byte sequence; `bvn_parse_unit` parses the unit string portion after the lexer has finished.

### 5.3 Separators

Three separator characters are defined:

| Character | Code point | UTF-8 bytes | Meaning |
|-----------|-----------|-------------|---------|
| `*`       | U+002A    | `0x2A`      | Multiplication — numerator stays numerator |
| `·`       | U+00B7    | `0xC2 0xB7` | Multiplication — visually preferred form of `*` |
| `/`       | U+002F    | `0x2F`      | Division — all components after the first `/` are denominator |

`·` (MIDDLE DOT) and `*` (ASTERISK) are **semantically identical**. They can be mixed freely within the same unit string.

### 5.4 Denominator Semantics

The division semantics are **non-reversing**: the first `/` flips a "in-denominator" flag to `true`, and that flag remains `true` for all subsequent components regardless of whether additional `/` separators appear. Every component after the first `/` is in the denominator; additional `/` separators do not toggle back to the numerator.

When a component is placed in the denominator, `bvn_parse_unit` negates its exponent. So `k~g·m/s²` is stored as:

```
component[0]: { base=bu_gram,   exponent=exp_linear,       prefix=si_kilo }   ← numerator
component[1]: { base=bu_meter,  exponent=exp_linear,     prefix=si_none }   ← numerator
component[2]: { base=bu_second, exponent=exp_neg_square, prefix=si_none }   ← denominator (negated)
```

This means the string form `k~g·m/s²` and the form `k~g·m·s⁻²` produce the **identical** `value_unit_t` representation; the second form uses an explicit negative exponent in the numerator instead of relying on the `/` switch.

---

## 6. Exponents

Exponents can appear on any `unit-component` and are limited to integer values in the range **−9 … +9**. Two syntactic forms are accepted and map to the same internal `unit_exponent_t` enumeration.

### 6.1 Unicode Superscript Form

| Glyph | Code point | UTF-8 bytes             | Maps to         |
|-------|-----------|-------------------------|-----------------|
| `¹`   | U+00B9    | `0xC2 0xB9`             | `exp_linear`    |
| `²`   | U+00B2    | `0xC2 0xB2`             | `exp_square`    |
| `³`   | U+00B3    | `0xC2 0xB3`             | `exp_cubic`     |
| `⁴`   | U+2074    | `0xE2 0x81 0xB4`        | `exp_quartic`   |
| `⁵`   | U+2075    | `0xE2 0x81 0xB5`        | `exp_quintic`   |
| `⁶`   | U+2076    | `0xE2 0x81 0xB6`        | `exp_sextic`    |
| `⁷`   | U+2077    | `0xE2 0x81 0xB7`        | `exp_septic`    |
| `⁸`   | U+2078    | `0xE2 0x81 0xB8`        | `exp_octic`     |
| `⁹`   | U+2079    | `0xE2 0x81 0xB9`        | `exp_nonic`     |
| `⁺`   | U+207A    | `0xE2 0x81 0xBA`        | positive sign (no-op) |
| `⁻`   | U+207B    | `0xE2 0x81 0xBB`        | negate exponent |

The sign glyphs (`⁺`, `⁻`) immediately precede the digit glyph: `s⁻²` means s to the power of −2. `⁺` is accepted but has no effect (positive exponent is the default).

### 6.2 ASCII Caret Form

The caret form `^[+-]?[0-9]` is accepted as an equivalent alternative:

| ASCII form | Equivalent Unicode | Parsed as |
|------------|---------------------|-----------|
| `m^2`      | `m²`                | `exp_square` |
| `s^-2`     | `s⁻²`               | `exp_neg_square` |
| `m^+2`     | `m⁺²` (= `m²`)      | `exp_square` |
| `kg^1`     | `kg¹`               | `exp_linear` |

Only a **single ASCII digit** is permitted after the caret; multi-digit exponents (e.g. `m^10`) are not supported.

### 6.3 Exponent Edge Cases

- **`exp_invalid`:** The zero-initialized sentinel (value `0`). A component whose exponent field was never explicitly set will compare equal to `exp_invalid`. API functions that have an error output path (`bvn_unit_to_si_factor`, `bvn_unit_dimension_vector`, `bvn_unit_convert_factor`, `bvn_units_compatible`, and the serialization functions) reject `exp_invalid` and signal an error through their output parameter or return value. The two prefix query functions (`bvn_unit_prefix_factor` and `bvn_unit_prefix_exponent`) have no error output parameter; they silently skip components whose exponent is `exp_invalid` and return a result based on the remaining components only. Callers must never pass `exp_invalid` to `bvni_exp_abs` or `bvni_prefix_exp_int`.
- **`exp_linear`:** Value `1`. Represents both an explicit `¹` / `^1` and any component written without an exponent suffix. The convenience macros (`BVN_UNIT_NO_PREFIX`, `BVN_UNIT_SI`, `BVN_UNIT_IEC`) store `exp_linear` for single-component units where no exponent is specified, consistent with parsed output.
---

## 7. The `no_unit` Keyword

The literal string `no_unit` in the unit parameter position declares a value as **explicitly dimensionless**:

```bovnar
.ratio       = <float:64,no_unit> 0.95;
.count       = <uint:32,no_unit>  1000;
.phase_angle = <sint:16,no_unit>  -90;
```

`no_unit` is handled as a special case by `bvn_parse_unit`: it is detected by a `memcmp` before any component parsing begins, and it returns the value:

```c
BVN_UNIT_NONE
```

which expands to:

```c
(value_unit_t){ .num_components = 0 }
```

**Omitting the unit parameter** (writing `<float:64>` or `<uint:32>`) produces a different internal representation: `bvn_parse_type_annotation` initialises its output unit to `BVN_UNIT_NO_PREFIX(bu_none)` and leaves it unchanged when no unit parameter is present, yielding `num_components = 1` with `base == bu_none`. Both forms are semantically equivalent — they compare as compatible via `bvn_units_compatible`, both serialize to `"no_unit"` via `bvn_unit_to_string`, and both are treated as dimensionless — but they are structurally distinct internal states.

> **Implementation note:** `bvn_parse_unit("no_unit")` and an explicit `no_unit` parameter in a type annotation both yield `BVN_UNIT_NONE` (`num_components = 0`). An absent unit parameter in an explicit type annotation (e.g. `<float:64>`) yields `BVN_UNIT_NO_PREFIX(bu_none)` (`num_components = 1`, `base == bu_none`). The `BVN_UNIT_NONE` macro is used wherever the unit has been explicitly declared dimensionless; `BVN_UNIT_NO_PREFIX(bu_none)` is the default for absent-annotation or synthesised contexts.

---

## 8. Constraints and Limits

| Constraint | Value | Error on violation |
|------------|-------|--------------------|
| Maximum components per compound unit | 8 (`BVNR_MAX_UNIT_COMPONENTS`) | `error_unit_illegal` |
| Empty component between separators (e.g. `m//s`, `m*·s`) | Not allowed | `error_unit_illegal` |
| Maximum length of the raw unit string | Enforced by `max_type_length` / type-buffer limit | `error_unit_too_long` |
| Null or empty unit string | Rejected by `bvn_parse_unit` | `ok = false` |

The 8-component limit covers the most complex physically meaningful compound units. For reference, the SI unit for dynamic viscosity (Pa·s = kg/(m·s)) has 3 components after expansion. A unit such as `k~g·m²/(A²·s³)` (henry) has 4.

The "no toggle back" rule for `/` means that constructs like `a/b/c` are parsed as `a / (b·c)`, not as `(a/b)/c`. Both produce the same mathematical result, but the internal representation is always "all post-`/` components negated", so `a/b/c` → components `[a, b⁻¹, c⁻¹]`.

---

## 9. C Data Model

### 9.1 Enumerations

#### `prefix_system_t` — Which prefix family applies

```c
typedef enum prefix_system_e {
    prefix_si,                  /* SI decimal prefixes (or no prefix) */
    prefix_iec                  /* IEC binary prefixes                */
} prefix_system_t;
```

#### `si_prefix_id_t` — SI prefix identity

```c
typedef enum si_prefix_id_e {
    si_none = 0,            /* no SI prefix (×10⁰)          */
    si_quecto, si_ronto, si_yocto, si_zepto, si_atto,
    si_femto,  si_pico,  si_nano,  si_micro, si_milli,
    si_centi,  si_deci,
    si_deca,   si_hecto, si_kilo,  si_mega,  si_giga,
    si_tera,   si_peta,  si_exa,   si_zetta, si_yotta,
    si_ronna,  si_quetta
} si_prefix_id_t;
```

Values are ordered from smallest (quecto) to largest (quetta). `si_none` is 0, so a zero-initialized component has no prefix.

#### `iec_prefix_id_t` — IEC binary prefix identity

```c
typedef enum iec_prefix_id_e {
    iec_none = 0,           /* no IEC prefix (×2⁰)           */
    iec_kibi, iec_mebi, iec_gibi, iec_tebi, iec_pebi,
    iec_exbi, iec_zebi, iec_yobi, iec_robi, iec_quebi
} iec_prefix_id_t;
```

#### `value_base_unit_t` — Base unit identity

```c
typedef enum value_base_unit_e {
    bu_none = 0,            /* dimensionless / no unit        */
    bu_bit, bu_byte,
    bu_second, bu_meter, bu_gram, bu_ampere, bu_kelvin,
    bu_mol, bu_candela,
    bu_hertz, bu_newton, bu_pascal, bu_joule, bu_watt,
    bu_volt,  bu_ohm,   bu_farad,  bu_coulomb, bu_siemens,
    bu_weber, bu_tesla, bu_henry,  bu_lumen,   bu_lux,
    bu_becquerel, bu_gray, bu_sievert, bu_katal,
    bu_liter, bu_minute, bu_hour, bu_day, bu_degree, bu_celsius,
    bu_radian, bu_steradian,
    bu_tonne, bu_bar,
    bu_electronvolt, bu_dalton, bu_astronomical_unit,
    bu_hectare,
    bu_week, bu_year,
    /* Imperial/US customary — length */
    bu_inch, bu_foot, bu_yard, bu_mile, bu_nautical_mile,
    bu_angstrom, bu_light_year, bu_parsec, bu_furlong, bu_fathom,
    bu_thou,
    /* Imperial/US customary — mass */
    bu_pound, bu_ounce, bu_grain, bu_stone, bu_short_ton,
    bu_long_ton, bu_troy_ounce, bu_carat, bu_slug,
    /* Temperature */
    bu_fahrenheit, bu_rankine,
    /* Pressure */
    bu_atmosphere, bu_mmhg, bu_torr, bu_psi,
    /* Energy */
    bu_calorie, bu_btu, bu_erg, bu_therm,
    /* Power */
    bu_horsepower,
    /* Force */
    bu_pound_force, bu_dyne, bu_kip,
    /* Speed */
    bu_knot,
    /* Volume — US */
    bu_gallon, bu_gallon_uk, bu_quart, bu_pint, bu_cup,
    bu_fluid_ounce, bu_tablespoon, bu_teaspoon, bu_barrel,
    /* Volume — UK imperial */
    bu_pint_uk, bu_fluid_ounce_uk, bu_quart_uk,
    /* Area */
    bu_acre, bu_barn,
    /* Angle */
    bu_arcminute, bu_arcsecond, bu_grad,
    /* CGS */
    bu_poise, bu_stokes, bu_gauss, bu_maxwell, bu_oersted,
    bu_stilb, bu_phot, bu_galileo,
    /* Radiation */
    bu_curie, bu_roentgen, bu_rem,
    /* Logarithmic */
    bu_neper, bu_decibel,
    /* Electrical power */
    bu_var, bu_volt_ampere
} value_base_unit_t;
```

#### `unit_exponent_t` — Exponent of a unit component

```c
typedef enum unit_exponent_e {
    exp_invalid    =   0,   /* sentinel — invalid / uninitialized    */
    exp_linear     =   1,   /* ¹                                     */
    exp_square     =   2,   /* ²                                     */
    exp_cubic      =   3,   /* ³                                     */
    exp_quartic    =   4,   /* ⁴                                     */
    exp_quintic    =   5,   /* ⁵                                     */
    exp_sextic     =   6,   /* ⁶                                     */
    exp_septic     =   7,   /* ⁷                                     */
    exp_octic      =   8,   /* ⁸                                     */
    exp_nonic      =   9,   /* ⁹                                     */
    exp_neg_linear =  -1,   /* ⁻¹                                    */
    exp_neg_square =  -2,   /* ⁻²                                    */
    exp_neg_cubic  =  -3,   /* ⁻³                                    */
    exp_neg_quartic=  -4,   /* ⁻⁴                                    */
    exp_neg_quintic=  -5,   /* ⁻⁵                                    */
    exp_neg_sextic =  -6,   /* ⁻⁶                                    */
    exp_neg_septic =  -7,   /* ⁻⁷                                    */
    exp_neg_octic  =  -8,   /* ⁻⁸                                    */
    exp_neg_nonic  =  -9,   /* ⁻⁹                                    */
} unit_exponent_t;
```

`exp_invalid` (value `0`) is the zero-initialization sentinel; a component whose exponent field has not been explicitly set will read as `exp_invalid` and is treated as malformed by all API functions. Always initialize components before use.

`exp_linear` (value `1`) is the stored representation for both an explicit `¹` / `^1` and any component written without an exponent suffix.

### 9.2 Structures

#### `value_unit_component_t` — A single factor in a compound unit

```c
typedef struct value_unit_component_s {
    value_base_unit_t   base;        /* which physical quantity         */
    unit_exponent_t     exponent;    /* power to which this unit is raised */
    value_unit_prefix_t prefix;      /* prefix system and id            */
} value_unit_component_t;
```

where `value_unit_prefix_t` is:

```c
typedef struct value_unit_prefix_s {
    prefix_system_t system;    /* prefix_si or prefix_iec         */
    union {
        si_prefix_id_t   si;   /* used when system == prefix_si   */
        iec_prefix_id_t  iec;  /* used when system == prefix_iec  */
    } id;
} value_unit_prefix_t;
```

Access the prefix as `component.prefix.id.si` (for SI) or `component.prefix.id.iec` (for IEC), guarded by a check on `component.prefix.system`.

#### `value_unit_t` — A complete unit (simple or compound)

```c
#define BVNR_MAX_UNIT_COMPONENTS  8

typedef struct value_unit_s {
    uint32_t               num_components;
    value_unit_component_t components[BVNR_MAX_UNIT_COMPONENTS];
} value_unit_t;
```

`num_components` is the number of valid entries in `components`. For an explicit `no_unit` annotation or when `bvn_parse_unit` is called with the string `"no_unit"`, `num_components == 0` (= `BVN_UNIT_NONE`). For an absent unit parameter in an explicit type annotation (e.g. `<float:64>`), `num_components == 1` with `base == bu_none` (= `BVN_UNIT_NO_PREFIX(bu_none)`). `BVN_UNIT_NONE` is also used as an internal sentinel where no unit has yet been set.

#### `bvnr_data_t` — Parser event payload (unit field)

The unit is delivered to the application as part of the `bvnr_data_t` structure emitted by `ev_type_annotation_type_family_parameter` events:

```c
typedef struct bvnr_data_s {
    token_type_t       type;        /* token classification            */
    value_type_spec_t  value_type;  /* family, width, base             */
    value_unit_t       value_unit;  /* parsed unit — focus of this doc */
    const void*        data;        /* raw bytes of the token          */
    uint32_t           length;      /* byte length of data             */
} bvnr_data_t;
```

### 9.3 Convenience Macros

Five macros cover the most common construction patterns:

```c
/* Dimensionless or single-component without prefix */
#define BVN_UNIT_NO_PREFIX(b)          \
    ((value_unit_t){                   \
        .num_components = 1,           \
        .components = {{               \
            .base           = (b),     \
            .exponent       = exp_linear,\
            .prefix.system  = prefix_si,\
            .prefix.id.si   = si_none  \
        }}                             \
    })

/* Single-component with SI prefix */
#define BVN_UNIT_SI(b, p)              \
    ((value_unit_t){                   \
        .num_components = 1,           \
        .components = {{               \
            .base           = (b),     \
            .exponent       = exp_linear,\
            .prefix.system  = prefix_si,\
            .prefix.id.si   = (p)      \
        }}                             \
    })

/* Single-component with IEC prefix */
#define BVN_UNIT_IEC(b, p)             \
    ((value_unit_t){                   \
        .num_components = 1,           \
        .components = {{               \
            .base           = (b),     \
            .exponent       = exp_linear,\
            .prefix.system  = prefix_iec,\
            .prefix.id.iec  = (p)      \
        }}                             \
    })

/* Single-component with SI prefix and explicit exponent */
#define BVN_UNIT_SI_EXP(b, p, e)      \
    ((value_unit_t){                   \
        .num_components = 1,           \
        .components = {{               \
            .base           = (b),     \
            .exponent       = (e),     \
            .prefix.system  = prefix_si,\
            .prefix.id.si   = (p)      \
        }}                             \
    })

/* Empty unit (num_components == 0) — internal sentinel */
#define BVN_UNIT_NONE                  \
    ((value_unit_t){ .num_components = 0 })

/* Two-component compound unit, both SI-prefixed */
#define BVN_UNIT_COMPOUND2(b1,p1,e1, b2,p2,e2) \
    ((value_unit_t){                             \
        .num_components = 2,                     \
        .components = {                          \
            { .base=(b1), .exponent=(e1),        \
              .prefix.system=prefix_si, .prefix.id.si=(p1) }, \
            { .base=(b2), .exponent=(e2),        \
              .prefix.system=prefix_si, .prefix.id.si=(p2) }  \
        }                                        \
    })
```

Usage examples:

```c
/* kilogram: k~g */
value_unit_t kg = BVN_UNIT_SI(bu_gram, si_kilo);

/* gibibytes: Gi~B */
value_unit_t gib = BVN_UNIT_IEC(bu_byte, iec_gibi);

/* square meter: m² */
value_unit_t m2 = BVN_UNIT_SI_EXP(bu_meter, si_none, exp_square);

/* dimensionless */
value_unit_t none = BVN_UNIT_NO_PREFIX(bu_none);
```

---

## 10. C API Functions

### 10.1 Parsing a Unit String

```c
value_unit_t bvn_parse_unit(const uint8_t* unit, bool* ok);
value_unit_t bvn_parse_unit_n(const uint8_t* unit, uint32_t len, bool* ok);
```

`bvn_parse_unit` parses the NUL-terminated UTF-8 unit string `unit` and returns a `value_unit_t`. Sets `*ok = false` on any parse error (unknown prefix, unknown base unit, too many components, empty component, empty or NULL input).

`bvn_parse_unit_n` is a length-bounded variant that reads exactly `len` bytes; it does not require a NUL terminator and is used internally when the unit string is a slice of a larger type-annotation buffer.

Both functions implement the unit sub-grammar in a single pass:

1. Checks for the literal `"no_unit"` via `memcmp` and returns `BVN_UNIT_NONE` (num_components = 0) immediately.
2. Scans for the presence of any separator character to decide between simple and compound parsing paths.
3. For compound units, splits on separator bytes (`0x2A` for `*`, `0x2F` for `/`, `0xC2 0xB7` for `·`), parses each slice as a `unit-component`, negates the exponent of any denominator component, and appends it to `result.components`.
4. Returns an empty `value_unit_t` (with `num_components = 0`) and `*ok = false` for any error.

```c
bool ok;
value_unit_t u = bvn_parse_unit((const uint8_t *)"k~g·m/s²", &ok);
if (!ok) {
    /* invalid unit string */
}
/* u.num_components == 3:
   [0]: gram,   exp_linear,     si_kilo
   [1]: meter,  exp_linear,     si_none
   [2]: second, exp_neg_square, si_none  ← negated by '/' */
```

### 10.2 Serializing a Unit

```c
int32_t bvn_unit_to_string(value_unit_t u, char* buf, size_t bufsize);
int32_t bvn_unit_to_string_ex(value_unit_t u, char* buf, size_t bufsize,
                               bvn_unit_flags_t flags);
```

Both functions serialize `u` to a canonical UTF-8 string in `buf`. They return the number of bytes written (excluding the NUL terminator), or `-1` on buffer overflow or if any component carries `exp_invalid`.

`bvn_unit_to_string` is equivalent to `bvn_unit_to_string_ex(u, buf, bufsize, BVN_UNIT_FLAGS_NONE)`.

`bvn_unit_to_string_ex` accepts a bitmask of `bvn_unit_flags_t` flags:

| Flag | Effect |
|------|--------|
| `BVN_UNIT_FLAGS_NONE` | Default: Unicode superscript exponents, no reduction |
| `BVN_UNIT_ASCII_EXP` | Use ASCII caret form (`^N`) for all exponents instead of Unicode superscripts |
| `BVN_UNIT_REDUCE` | Reduce the unit via `bvn_unit_reduce` before serializing (folds repeated base units and prefix exponents) |

The canonical form places numerator components first, joined by `·` (U+00B7), followed by `/` and denominator components (those with negative exponents) joined by `·`. `exp_linear` is suppressed in output (the base unit symbol is written with no exponent suffix).

Both functions call `bvn_unit_valid` internally before writing. If `bvn_unit_valid` returns `false` they return `-1` immediately without modifying `buf`.

```c
char buf[64];
value_unit_t u = /* k~g·m/s² */;
int32_t n = bvn_unit_to_string(u, buf, sizeof(buf));
/* buf == "k~g·m/s²", n == byte length */

/* ASCII exponent form */
n = bvn_unit_to_string_ex(u, buf, sizeof(buf), BVN_UNIT_ASCII_EXP);
/* buf == "k~g*m/s^2" */
```

#### Validation predicate

```c
bool bvn_unit_valid(value_unit_t u);
```

Returns `true` if every component in `u` has a valid exponent (not `exp_invalid`), a known base unit (within the `value_base_unit_t` range), and a prefix that is legal for that base unit per `bvn_prefix_unit_valid`. Returns `false` on the first violated condition. Both serialization functions call this predicate before writing; callers may also use it directly to validate hand-constructed `value_unit_t` values before passing them to any other API.

### 10.3 Prefix Factor and Exponent Queries

These functions compute the multiplicative scale factor contributed by the unit's **prefixes**, ignoring the dimensional identity of the base units themselves. They are useful for normalizing values to un-prefixed base units.

```c
double  bvn_unit_prefix_factor(value_unit_t u);
```
Iterates over all components, computes `prefix_factor ^ |exponent|` for each, inverts the result if the exponent is negative, and multiplies all results together. For `si_none`/`iec_none` prefixes the factor is 1.0. This function does **not** apply the base-unit conversion factor (e.g. gram → kilogram); use `bvn_unit_to_si_factor` when full SI normalization is needed.

```c
int32_t bvn_unit_prefix_exponent(value_unit_t u);
```
Returns the sum of `(prefix_base_exponent × |unit_exponent|)` across all components (negated for denominator components). For SI prefixes the base exponent is the power of ten (e.g. `si_kilo` → 3). For IEC prefixes the base exponent is the power of two (e.g. `iec_kibi` → 10, `iec_mebi` → 20). This gives the net prefix offset relative to un-prefixed base units.

#### Normalization example

```c
bool ok;
value_unit_t u = bvn_parse_unit((const uint8_t *)"k~m/s", &ok);
double value = 1.5;
double factor = bvn_unit_prefix_factor(u);  /* == 1000.0 (kilo in numerator) */
double si_value = value * factor;           /* == 1500.0 m/s */

value_unit_t u2 = bvn_parse_unit((const uint8_t *)"k~g/m³", &ok);
/* bvn_unit_prefix_factor(u2) == 1000.0:
   kilo in numerator contributes ×1000, m³ in denominator has si_none → ×1 */
```

### 10.4 SI Conversion API

These functions live in `bovnar_si_units.h` and provide dimensional analysis, unit compatibility checking, and value conversion between compatible units.

#### Exponent integer conversion

```c
int32_t        bvn_exponent_to_int(unit_exponent_t e);
unit_exponent_t bvn_int_to_exponent(int32_t n);
```

`bvn_exponent_to_int` maps a `unit_exponent_t` enum value to its integer equivalent: `exp_linear` → 1, `exp_neg_square` → −2, `exp_invalid` → 0. `bvn_int_to_exponent` is a partial inverse: it returns `exp_invalid` for integers outside −9…+9 and for input 0.

#### Full SI factor

```c
double bvn_unit_to_si_factor(value_unit_t u,
                              bool        *is_affine,
                              double      *affine_offset,
                              bool        *ok);
```

Returns the multiplicative factor that converts a value in unit `u` to the corresponding SI base unit (e.g. `k~g` → 1.0, since the gram-to-kilogram factor of 10⁻³ is absorbed by the `si_kilo` prefix contribution of 10³ giving a net factor of 1.0; `k~J` → 1000.0). Both the prefix factor and the base-unit-to-SI factor are applied.

For affine units (`bu_celsius`), `*is_affine` is set to `true` and `*affine_offset` is set to the additive offset that must be applied **after** multiplying by the returned factor (273.15 for Celsius). An affine unit is valid at exponent 1 only. At exponent 1, `*is_affine` is set to `true` and `*affine_offset` is populated. Any other exponent (negative, or greater than 1) sets `*ok = false`. Only one affine component per compound unit is permitted; a second affine component at exponent 1 also sets `*ok = false`.

`*ok` is set to `false` for invalid prefixes, unknown base units, or `exp_invalid` exponents.

```c
bool ok, affine;
double offset;
value_unit_t u = bvn_parse_unit((const uint8_t *)"°C", &ok);
double f = bvn_unit_to_si_factor(u, &affine, &offset, &ok);
/* f == 1.0, affine == true, offset == 273.15 */
/* kelvin = celsius * f + offset */
```

#### Dimension vector

```c
bool bvn_unit_dimension_vector(value_unit_t u, int32_t dims[bvn_si_dim_count]);
```

Fills `dims` with the SI dimension exponents for unit `u`. The indices correspond to the `bvn_si_dim_idx_t` enumeration:

```c
typedef enum bvn_si_dim_idx_e {
    bvn_si_dim_meter    = 0,
    bvn_si_dim_kilogram = 1,
    bvn_si_dim_second   = 2,
    bvn_si_dim_ampere   = 3,
    bvn_si_dim_kelvin   = 4,
    bvn_si_dim_mol      = 5,
    bvn_si_dim_candela  = 6,
    bvn_si_dim_count    = 7
} bvn_si_dim_idx_t;
```

Returns `false` if any component carries `exp_invalid` or an invalid prefix, `true` otherwise. Digital units (`bu_bit`, `bu_byte`) have all-zero dimension vectors and are tracked separately by `bvn_units_compatible`.

#### Unit compatibility

```c
bool bvn_units_compatible(value_unit_t a, value_unit_t b);
```

Returns `true` if units `a` and `b` are dimensionally compatible (i.e. measure the same physical quantity and can be converted into each other by a scalar factor). Compatibility is determined by comparing:

1. The net exponent of `bu_bit` components in `a` and `b`.
2. The net exponent of `bu_byte` components in `a` and `b`.
3. The full SI dimension vector of `a` and `b`.

All three must match. Returns `false` on any parse error in either unit.

#### Conversion factor

```c
double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
                                bool *ok, bool *requires_affine);
```

Returns the multiplicative factor `k` such that `value_in_b = value_in_a * k`, provided units `a` and `b` are compatible and neither is affine. Sets `*ok = false` and returns 0.0 if the units are incompatible or the conversion cannot be expressed as a pure scale factor.

`*requires_affine` is set to `true` whenever at least one of the units is affine (e.g. `bu_celsius`). If both units are affine with the same offset (converting `°C` to `°C`), the function returns the ratio of their scale factors and leaves `*ok = true`. For mixed affine/non-affine conversions (e.g. `°C` to `K`) it sets `*ok = false` to signal that the caller must apply the offset manually using `bvn_unit_to_si_factor`.

```c
bool ok, needs_affine;
value_unit_t km = bvn_parse_unit((const uint8_t *)"k~m",  &ok);
value_unit_t m  = bvn_parse_unit((const uint8_t *)"m",    &ok);
double k = bvn_unit_convert_factor(km, m, &ok, &needs_affine);
/* k == 1000.0, ok == true, needs_affine == false */
```

#### Unit reduction

```c
value_unit_t bvn_unit_reduce(value_unit_t u, double *scale, bool *overflow);
```

Reduces a compound unit by accumulating the net exponent and net prefix contribution for each distinct base unit across all components. Returns a canonical `value_unit_t` where each base unit appears at most once, with `si_none` prefix and the accumulated net exponent. All prefix contributions are folded into `*scale`.

Components with a net exponent of zero are dropped. Components whose net exponent magnitude exceeds 9 (outside the expressible range of `unit_exponent_t`) are also dropped; their base-unit-to-SI contribution is folded into `*scale` and `*overflow` is set to `true`.

The output components are sorted: positive-exponent components first, ordered by absolute exponent descending, then by `value_base_unit_t` enum value ascending; negative-exponent components follow in the same order.

```c
double scale;
bool overflow;
value_unit_t u  = bvn_parse_unit((const uint8_t *)"k~m·k~m", &ok);
value_unit_t r  = bvn_unit_reduce(u, &scale, &overflow);
/* r: single component { bu_meter, exp_square, si_none }
   scale == 1e6  (two kilo prefixes → 10³ × 10³) */
```

#### Prefix–unit validity

```c
bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base);
```

Returns `true` if the prefix carried by the first component of `u` is a legal modifier for `base`. The rules are:
- IEC prefixes (other than `iec_none`) are only valid on `bu_bit` and `bu_byte`.
- SI prefixes below `si_kilo` are invalid on `bu_bit` and `bu_byte`.
- Out-of-range `prefix.system` or `base` values return `false`.

All higher-level parsing and conversion functions call `bvn_prefix_unit_valid` internally and propagate the error via their `ok` output.

---

## 11. Integration with the Parser Event Stream

Unit information flows into the application through the `ev_type_annotation_type_family_parameter` event and through `ev_data`. There are two paths by which a unit reaches the event stream:

1. **Type-annotation unit** — parsed from `<family:…,unit-param>` by the lexer, validated by the validator, and delivered in the `ev_type_annotation_type_family_parameter` unit event.
2. **Inline unit suffix** — parsed from the suffix that follows a scalar value literal (number or string) before the terminating `;`, validated by the validator after the value is checked for type compatibility.

In both cases the effective unit is reported in the `bvnr_data_t.value_unit` field of the `ev_data` event for the value.

The full event sequence for an assignment with a compound unit annotation is:

```
Input: .force = <float:64,k~g·m/s²> 9.81;

ev_assignment_start
    data = "force"

ev_type_annotation_start
    data = "float:64,k~g·m/s²"

ev_type_annotation_type_family
    data = "float"

ev_type_annotation_type_family_parameter    ← width
    value_type.width = 64

ev_type_annotation_type_family_parameter    ← unit
    value_unit = {
        num_components = 3,
        components = [
            { base=bu_gram,   exponent=exp_linear,       prefix={prefix_si, si_kilo} },
            { base=bu_meter,  exponent=exp_linear,     prefix={prefix_si, si_none} },
            { base=bu_second, exponent=exp_neg_square, prefix={prefix_si, si_none} }
        ]
    }

ev_type_annotation_end

ev_data
    data = "9.81"
```

Both `on_unverified` and `on_verified` callbacks receive this event stream. The validator confirms that the unit string is valid before emitting `on_verified`; invalid units are reported via `on_error` and produce `error_unit_illegal` or `error_unit_too_long`.

For events with an explicit type annotation but no unit parameter (e.g. `<float:64>`), the `ev_type_annotation_type_family_parameter` event for the unit position is **not emitted** — the validator skips the unit event when `ulen == 0`. For events with an explicit `no_unit` parameter, the unit event IS emitted with:

```c
value_unit = BVN_UNIT_NONE
/* i.e. num_components=0 */
```

For synthesised (default) type annotations generated for plain values with no explicit annotation, the unit event IS emitted with:

```c
value_unit = BVN_UNIT_NO_PREFIX(bu_none)
/* i.e. num_components=1, components[0].base=bu_none */
```

### Inline unit suffix — event stream view

When a value carries an **inline unit suffix** (and no explicit type-annotation unit), the validator synthesises a default type annotation, then applies the inline unit.  The effective unit is always reflected in the `value_unit` field of the final `ev_data` event:

```
Input: .distance = 1500 m;

ev_assignment_start
    data = "distance"

ev_type_annotation_start      ← synthesised for plain integer
    data = "uint"

ev_type_annotation_type_family
    data = "uint"

ev_type_annotation_type_family_parameter    ← width (synthesised: 64)
    value_type.width = 64

ev_type_annotation_type_family_parameter    ← base (synthesised: 10)
    value_type.base = 10

ev_type_annotation_type_family_parameter    ← unit (synthesised: no_unit)
    value_unit = BVN_UNIT_NO_PREFIX(bu_none)

ev_type_annotation_end

ev_data                                     ← inline unit applied here
    data   = "1500"
    value_unit = { num_components=1,
                   components[0] = { base=bu_meter, prefix.si=si_none,
                                     exponent=exp_linear } }
```

The `value_unit` field of `ev_data` always reflects the final, reconciled unit — whether it came from the annotation, from synthesis, or from an inline suffix.

A practical callback for inspecting unit data:

```c
bool my_verified_handler(void* userdata, bvnr_event_t ev, bvnr_data_t* d)
{
    if (ev != ev_type_annotation_type_family_parameter)
        return true;

    value_unit_t u = d->value_unit;
    if (u.num_components == 0 ||
        (u.num_components == 1 && u.components[0].base == bu_none))
        return true;  /* dimensionless, nothing to do */

    char unit_str[128];
    bvn_unit_to_string(u, unit_str, sizeof(unit_str));
    printf("unit: %s  (factor: %g)\n", unit_str, bvn_unit_prefix_factor(u));

    for (uint32_t i = 0; i < u.num_components; i++) {
        value_unit_component_t *c = &u.components[i];
        printf("  [%u] base=%d  exp=%d  prefix_sys=%s\n",
               i, c->base, c->exponent,
               c->prefix.system == prefix_si ? "SI" : "IEC");
    }
    return true;
}
```

---

## 12. Validation Errors

The validator raises the following unit-specific errors:

| Error code | Value | Trigger condition |
|------------|-------|-------------------|
| `error_unit_illegal` | 32 | Unparseable unit string: unknown prefix, unknown base unit, invalid prefix–unit combination, empty component between separators (e.g. `m//s`), or more than `BVNR_MAX_UNIT_COMPONENTS` (8) components |
| `error_unit_too_long` | 22 | Unit string exceeds the internal type-buffer size limit |
| `error_unit_mismatch` | 38 | An inline unit suffix is present and an explicit type-annotation unit is also present, but the two do not parse to the same `value_unit_t` representation |
| `error_unexpected_input_byte` | 15 | An inline unit suffix appears inside an array element (only scalar context is permitted) |

All four errors are raised during the `on_unverified` → validator phase and cause the validated (`on_verified`) callback to not be invoked for that event.

In `continue_on_error` mode the parser invokes `on_error` with the error code and then enters the resync state machine, which skips to the next `;` at the current nesting depth. The `recovery_count` (accessible via `bvnr_reader_get_recovery_count`) is incremented immediately when an error triggers entry into resync mode.

`bvnr_reader_get_error`, `bvnr_reader_get_error_line`, `bvnr_reader_get_error_column`, `bvnr_reader_get_error_byte`, and `bvnr_reader_get_error_offset` all report the location of the offending unit string within the stream.

---

## 13. Annotated Examples

### 13.1 Physical Quantities

```bovnar
# Thermodynamic temperature
.ambient_temp = <float:64,K>         293.15;    # kelvin

# Temperature in Celsius (affine: K = °C + 273.15)
.room_temp    = <float:32,°C>        20.0;

# Velocity
.wind_speed   = <float:64,m/s>       12.5;

# Acceleration (using Unicode exponent)
.gravity      = <float:64,m/s²>      9.80665;

# Acceleration (using ASCII caret — identical result)
.gravity_asc  = <float:64,m/s^2>     9.80665;

# Pressure in kilopascals
.tire_pressure = <float:32,k~Pa>     250.0;

# Energy in kilojoules
.heat_energy  = <float:64,k~J>       5400.0;

# Flow rate (liters per minute)
.pump_flow    = <float:32,L/min>     15.0;

# Angle
.bearing      = <float:64,°>         270.0;

# Plane angle in radians
.phase        = <float:64,rad>        1.5708;

# Solid angle
.beam_solid   = <float:64,sr>         0.05;

# Mass in tonnes
.cargo_mass   = <float:64,t>          14.5;

# Pressure in bar
.tank_pressure = <float:32,bar>       2.5;

# Energy in electronvolts
.photon_energy = <float:64,eV>        2.4;

# Mass in daltons (unified atomic mass)
.atomic_mass  = <float:64,Da>         12.0;

# Distance in astronomical units
.orbit_radius = <float:64,au>         1.524;

# Area in hectares
.field_area   = <float:64,ha>         3.7;

# Duration in weeks and years
.shelf_life   = <uint:32,wk>          52;
.service_life = <float:64,yr>         10.0;
```

### 13.2 Digital Storage

```bovnar
# Plain bytes
.packet_size = <uint:32,B>      1500;

# Kibibytes (IEC binary)
.cache_size  = <uint:64,Ki~B>   512;

# Mebibytes
.ram_size    = <uint:64,Mi~B>   4096;

# Gibibytes
.disk_size   = <uint:64,Gi~B>   500;

# Tebibytes
.array_size  = <uint:64,Ti~B>   2;

# Megabits (SI decimal — different from Mebi!)
.link_rate   = <uint:32,M~b>    1000;

# Gigabits per second (compound: data-rate)
.nic_speed   = <float:64,G~b/s> 10.0;
```

### 13.3 Compound SI Quantities

```bovnar
# Force: Newton = kg·m·s⁻² = k~g·m/s²
.force          = <float:64,k~g·m/s²>    9.81;

# Alternative: explicit negative exponent in numerator
.force_alt      = <float:64,k~g·m·s⁻²>  9.81;   # identical internal form

# Energy: Joule = kg·m²·s⁻² = k~g·m²/s²
.kinetic_energy = <float:64,k~g·m²/s²>  1000.0;

# Momentum: kg·m/s
.momentum       = <float:64,k~g·m/s>    5.0;

# Mass density: kg/m³
.steel_density  = <float:64,k~g/m³>     7800.0;

# Area density: kg/m²
.surface_load   = <float:64,k~g/m²>     200.0;

# Pressure via explicit components: kg/(m·s²)
.atm_pressure   = <float:64,k~g/(m·s²)> 101325.0;

# Electric field strength: V/m
.field_strength = <float:64,V/m>         150.0;

# Magnetic flux density: T (named SI unit, no compound needed)
.b_field        = <float:64,m~T>         50.0;     # millitesla

# Torque: N·m
.torque         = <float:64,N·m>         25.0;

# Product form using asterisk separator (same as ·)
.moment         = <float:64,m*s>         1.0;
```

### 13.4 Error Cases

```bovnar
# Empty component between two slashes → error_unit_illegal
.bad1 = <float:64,m//s>      1.0;

# Two middle-dots with nothing between → error_unit_illegal
.bad2 = <float:64,m*·s>      1.0;

# Too many components (9 > BVNR_MAX_UNIT_COMPONENTS=8) → error_unit_illegal
.bad3 = <float:64,m*s*k~g*A*K*mol*cd*b*B> 1.0;

# Unknown prefix → error_unit_illegal
# ('x' is not an SI or IEC prefix symbol)
.bad4 = <float:64,x-m>       1.0;

# Unknown base unit symbol → error_unit_illegal
.bad5 = <float:64,XYZ>       1.0;

# Exponent digit out of supported range (multi-digit not allowed)
.bad6 = <float:64,m^10>      1.0;    # only single ASCII digit after ^

# Correct: dimensionless explicit
.ok1  = <uint:32,no_unit>    42;

# Correct: omitted unit (same internal state as no_unit)
.ok2  = <uint:32>            42;
```

---

*End of Bovnar Unit System Reference Documentation v1.0.*





