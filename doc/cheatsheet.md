# Bovnar (BVNR) — Units & Currencies Cheat Sheet

> **Spec version:** 1.1 · **Unit count:** 146 physical + 161 fiat + 34 crypto = 341 total

---

## Contents

1. [Quick Syntax Reference](#1-quick-syntax-reference)
2. [Type Families](#2-type-families)
3. [Prefixes](#3-prefixes)
   - 3.1 [SI Prefixes](#31-si-prefixes)
   - 3.2 [IEC Binary Prefixes](#32-iec-binary-prefixes)
   - 3.3 [Prefix Validity Rules](#33-prefix-validity-rules)
4. [Exponent Notation](#4-exponent-notation)
5. [Compound Unit Grammar](#5-compound-unit-grammar)
6. [Physical Units](#6-physical-units)
   - 6.1 [SI Base Units](#61-si-base-units)
   - 6.2 [Named SI-Derived Units](#62-named-si-derived-units)
   - 6.3 [Non-SI Units Accepted with SI](#63-non-si-units-accepted-with-si)
   - 6.4 [Imperial & US Customary — Length](#64-imperial--us-customary--length)
   - 6.5 [Imperial & US Customary — Mass](#65-imperial--us-customary--mass)
   - 6.6 [Temperature](#66-temperature)
   - 6.7 [Pressure](#67-pressure)
   - 6.8 [Energy](#68-energy)
   - 6.9 [Power](#69-power)
   - 6.10 [Force](#610-force)
   - 6.11 [Speed](#611-speed)
   - 6.12 [Volume — US Liquid](#612-volume--us-liquid)
   - 6.13 [Volume — UK Imperial](#613-volume--uk-imperial)
   - 6.14 [Volume — US Apothecary & Dry](#614-volume--us-apothecary--dry)
   - 6.15 [Area](#615-area)
   - 6.16 [Angle](#616-angle)
   - 6.17 [Digital](#617-digital)
   - 6.18 [CGS Units](#618-cgs-units)
   - 6.19 [Radiation](#619-radiation)
   - 6.20 [Logarithmic](#620-logarithmic)
   - 6.21 [Electrical Power](#621-electrical-power)
   - 6.22 [Textile Linear Density](#622-textile-linear-density)
   - 6.23 [Old German Units](#623-old-german-units)
   - 6.24 [Miscellaneous](#624-miscellaneous)
7. [Currencies](#7-currencies)
   - 7.1 [Namespace Rule](#71-namespace-rule)
   - 7.2 [ISO 4217 Fiat Currencies](#72-iso-4217-fiat-currencies)
   - 7.3 [Cryptocurrencies](#73-cryptocurrencies)
   - 7.4 [Currency Prefix Rules](#74-currency-prefix-rules)
8. [Symbol Disambiguation](#8-symbol-disambiguation)
9. [Common Patterns](#9-common-patterns)
10. [Error Reference](#10-error-reference)

---

## 1. Quick Syntax Reference

```bovnar
# ── Assignments ─────────────────────────────────────────────────────────────
.key = value;
.key = <type-family:width,_base,unit> value;

# ── Simple units ─────────────────────────────────────────────────────────────
.a = <float:64,s>         2.5;        # seconds
.b = <float:64,k~m>       1.5;        # kilometres (SI prefix~unit)
.c = <uint:64,Ki~B>       512;        # kibibytes  (IEC prefix~unit)
.d = <float_dec:64,USD>  19.99;       # US dollars (currency code)

# ── Compound units ───────────────────────────────────────────────────────────
.e = <float:64,m/s>       9.81;       # m per second
.f = <float:64,m/s²>      9.81;       # Unicode superscript exponent
.g = <float:64,m/s^2>     9.81;       # ASCII caret exponent (same result)
.h = <float:64,k~g·m/s²>  9.81;       # kilogram·metre per second²
.i = <float_dec:64,USD/oz_t> 2351.40; # $/troy oz (currency in compound)

# ── Inline unit suffix (scalar context only) ─────────────────────────────────
.j = 9.81 m/s;
.k = 70.5 k~g;
.l = 19.99 USD;

# ── Explicitly dimensionless ─────────────────────────────────────────────────
.m = <uint:32,no_unit> 42;
```

**Key rules:**
- The `~` separator between prefix and base unit is **mandatory** (`k~m`, not `km`).
- `/` is a one-way denominator switch: `a/b/c` → `a · b⁻¹ · c⁻¹`.
- `·` (U+00B7) and `*` are interchangeable multiplication separators.
- Inline suffix is **forbidden** inside array elements `[…]`.
- All 3–4 uppercase-ASCII tokens are dispatched to the currency table first.

---

## 2. Type Families

| Keyword | Description | Unit param | Notes |
|---------|-------------|------------|-------|
| `uint` | Unsigned integer | Supported | Any positive bit-width |
| `sint` | Signed integer | Supported | Any positive bit-width |
| `float` | IEEE 754 binary floating-point | Supported | Widths: 16, 32×N, up to 32768 |
| `float_fix` | Q-format signed fixed-point | Supported | Widths: 0→64, 16, 32, 64, 128, 256; needs `qN` |
| `float_dec` | IEEE 754-2008 decimal float | Supported | Widths: 0→64, 16, 32, 64, 128, 256; **recommended for money** |
| `utf8` | UTF-8 string | Lexically accepted, semantically ignored | |

---

## 3. Prefixes

### 3.1 SI Prefixes

All 24 current SI prefixes. Written as `prefix~base` (e.g. `k~m` = kilometre).

| Name | Symbol | Factor | Enum (`si_prefix_id_t`) |
|------|--------|--------|--------------------------|
| quetta | `Q`  | 10³⁰  | `si_quetta` |
| ronna  | `R`  | 10²⁷  | `si_ronna`  |
| yotta  | `Y`  | 10²⁴  | `si_yotta`  |
| zetta  | `Z`  | 10²¹  | `si_zetta`  |
| exa    | `E`  | 10¹⁸  | `si_exa`    |
| peta   | `P`  | 10¹⁵  | `si_peta`   |
| tera   | `T`  | 10¹²  | `si_tera`   |
| giga   | `G`  | 10⁹   | `si_giga`   |
| mega   | `M`  | 10⁶   | `si_mega`   |
| kilo   | `k`  | 10³   | `si_kilo`   |
| hecto  | `h`  | 10²   | `si_hecto`  |
| deca   | `da` | 10¹   | `si_deca`   |
| *(none)* | — | 10⁰   | `si_none`   |
| deci   | `d`  | 10⁻¹  | `si_deci`   |
| centi  | `c`  | 10⁻²  | `si_centi`  |
| milli  | `m`  | 10⁻³  | `si_milli`  |
| micro  | `µ`  | 10⁻⁶  | `si_micro`  |
| nano   | `n`  | 10⁻⁹  | `si_nano`   |
| pico   | `p`  | 10⁻¹² | `si_pico`   |
| femto  | `f`  | 10⁻¹⁵ | `si_femto`  |
| atto   | `a`  | 10⁻¹⁸ | `si_atto`   |
| zepto  | `z`  | 10⁻²¹ | `si_zepto`  |
| yocto  | `y`  | 10⁻²⁴ | `si_yocto`  |
| ronto  | `r`  | 10⁻²⁷ | `si_ronto`  |
| quecto | `q`  | 10⁻³⁰ | `si_quecto` |

> `µ` is U+00B5 MICRO SIGN (UTF-8: `0xC2 0xB5`). U+03BC (Greek mu) is **not** accepted.
> `da` is a two-character prefix — `da~m` = decametre.

**Prefix–base ambiguities resolved by `~`:**

| Token | Without `~` → base unit | With `~` → prefix |
|-------|--------------------------|-------------------|
| `m`   | meter (`bu_meter`)       | milli             |
| `d`   | day (`bu_day`)           | deci              |
| `h`   | hour (`bu_hour`)         | hecto             |
| `T`   | tesla (`bu_tesla`)       | tera              |
| `f`   | farad (`bu_farad`)       | femto             |
| `S`   | siemens (`bu_siemens`)   | *(not a prefix)*  |

Examples: `m~s` = millisecond; bare `m` = metre. `d~s` = decisecond; bare `d` = day.

### 3.2 IEC Binary Prefixes

Used for digital quantities (`b` and `B`) only. Written as `prefix~base` (e.g. `Ki~B`).

| Name  | Symbol | Factor  | Enum (`iec_prefix_id_t`) |
|-------|--------|---------|--------------------------|
| kibi  | `Ki`   | 2¹⁰     | `iec_kibi`  |
| mebi  | `Mi`   | 2²⁰     | `iec_mebi`  |
| gibi  | `Gi`   | 2³⁰     | `iec_gibi`  |
| tebi  | `Ti`   | 2⁴⁰     | `iec_tebi`  |
| pebi  | `Pi`   | 2⁵⁰     | `iec_pebi`  |
| exbi  | `Ei`   | 2⁶⁰     | `iec_exbi`  |
| zebi  | `Zi`   | 2⁷⁰     | `iec_zebi`  |
| yobi  | `Yi`   | 2⁸⁰     | `iec_yobi`  |
| robi  | `Ri`   | 2⁹⁰     | `iec_robi`  |
| quebi | `Qi`   | 2¹⁰⁰    | `iec_quebi` |

### 3.3 Prefix Validity Rules

| Unit category | SI prefixes | IEC prefixes |
|---------------|-------------|--------------|
| All physical units (default) | All 24 allowed | Forbidden |
| `b` (bit) and `B` (byte) | Only ≥ kilo (`k`, `M`, `G`, …) | All 10 allowed |
| `b` and `B` sub-kilo SI (`d`, `c`, `m`, `µ`, …) | **Forbidden** | — |
| Currency codes | All 24 allowed | **Forbidden** |
| Old German units (`bu_pfund`…`bu_scheffel`) | **None** (only `si_none`) | **Forbidden** |

---

## 4. Exponent Notation

Exponents range from −9 to +9. Two equivalent forms are accepted:

### Unicode Superscript Form

| Glyph | Code point | UTF-8 bytes      | Enum value (`unit_exponent_t`) |
|-------|-----------|------------------|-------------------------------|
| `¹`   | U+00B9    | `0xC2 0xB9`      | `exp_linear`   (1) |
| `²`   | U+00B2    | `0xC2 0xB2`      | `exp_square`   (2) |
| `³`   | U+00B3    | `0xC2 0xB3`      | `exp_cubic`    (3) |
| `⁴`   | U+2074    | `0xE2 0x81 0xB4` | `exp_quartic`  (4) |
| `⁵`   | U+2075    | `0xE2 0x81 0xB5` | `exp_quintic`  (5) |
| `⁶`   | U+2076    | `0xE2 0x81 0xB6` | `exp_sextic`   (6) |
| `⁷`   | U+2077    | `0xE2 0x81 0xB7` | `exp_septic`   (7) |
| `⁸`   | U+2078    | `0xE2 0x81 0xB8` | `exp_octic`    (8) |
| `⁹`   | U+2079    | `0xE2 0x81 0xB9` | `exp_nonic`    (9) |
| `⁺`   | U+207A    | `0xE2 0x81 0xBA` | positive sign (no-op) |
| `⁻`   | U+207B    | `0xE2 0x81 0xBB` | negate the following digit |

Negative exponents: `m⁻²` = `exp_neg_square` (−2).

### ASCII Caret Form

```
m^2    ≡  m²         m^-2   ≡  m⁻²
s^-1   ≡  s⁻¹        m^+2   ≡  m²   (+ is no-op)
```

Only a **single ASCII digit** (1–9) is permitted after `^`. `^0` is not supported.

---

## 5. Compound Unit Grammar

```ebnf
compound-unit  = "no_unit"
               | unit-component { unit-sep unit-component }

unit-sep       = "*" | "·" | "/"      (* · = U+00B7 MIDDLE DOT *)

unit-component = [ prefix "~" ] base-unit [ unit-exponent ]

unit-exponent  = superscript-digit | "^" [ sign ] ASCII-digit
```

- Maximum **8 components** per compound unit (`BVNR_MAX_UNIT_COMPONENTS`).
- First `/` latches the "in-denominator" flag permanently: `a/b/c` → `[a, b⁻¹, c⁻¹]`.
- `·` and `*` are interchangeable; may be mixed in the same expression.
- `m/s²` and `m·s⁻²` produce **identical** `value_unit_t` representations.

```bovnar
.velocity     = <float:64,m/s>          # [m¹, s⁻¹]
.force        = <float:64,k~g·m/s²>     # [k~g¹, m¹, s⁻²]
.energy       = <float:64,k~g·m²/s²>    # [k~g¹, m², s⁻²]
.gold_rate    = <float_dec:64,USD/oz_t>  # [USD¹, oz_t⁻¹]
.exchange     = <float_dec:64,USD/EUR>   # [USD¹, EUR⁻¹]
.grav_const   = <float:64,m³/k~g/s²>    # [m³, k~g⁻¹, s⁻²]
```

---

## 6. Physical Units

> **Reading the tables:** *Symbol* = canonical output form (also accepted on input).
> *Long forms* are accepted on input but never produced on output.
> Factors are to SI base units unless otherwise noted.

### 6.1 SI Base Units

| Symbol | Long forms | Name | Enum | Notes |
|--------|-----------|------|------|-------|
| `s` | `sec`, `second`, `seconds` | second | `bu_second` | SI base unit of time |
| `m` | `meter`, `metre`, `meters`, `metres` | metre | `bu_meter` | SI base unit of length |
| `g` | `gram`, `grams` | gram | `bu_gram` | Base symbol; `k~g` = kilogram |
| `A` | `amp`, `amps`, `ampere`, `amperes` | ampere | `bu_ampere` | SI base unit of electric current |
| `K` | `kelvin`, `kelvins` | kelvin | `bu_kelvin` | SI base unit of temperature |
| `mol` | `mole`, `moles` | mole | `bu_mol` | SI base unit of amount of substance |
| `cd` | `candela`, `candelas` | candela | `bu_candela` | SI base unit of luminous intensity |

### 6.2 Named SI-Derived Units

| Symbol | Long forms | Name | Enum | SI definition |
|--------|-----------|------|------|---------------|
| `Hz`  | `hertz` | hertz | `bu_hertz` | s⁻¹ |
| `N`   | `newton`, `newtons` | newton | `bu_newton` | kg·m·s⁻² |
| `Pa`  | `pascal`, `pascals` | pascal | `bu_pascal` | kg·m⁻¹·s⁻² |
| `J`   | `joule`, `joules` | joule | `bu_joule` | kg·m²·s⁻² |
| `W`   | `watt`, `watts` | watt | `bu_watt` | kg·m²·s⁻³ |
| `V`   | `volt`, `volts` | volt | `bu_volt` | kg·m²·A⁻¹·s⁻³ |
| `Ω`   | `ohm`, `ohms` | ohm | `bu_ohm` | kg·m²·A⁻²·s⁻³ — U+2126 |
| `F`   | `farad`, `farads` | farad | `bu_farad` | kg⁻¹·m⁻²·A²·s⁴ |
| `C`   | `coulomb`, `coulombs` | coulomb | `bu_coulomb` | A·s |
| `S`   | `siemens` | siemens | `bu_siemens` | kg⁻¹·m⁻²·A²·s³ |
| `Wb`  | `weber`, `webers` | weber | `bu_weber` | kg·m²·A⁻¹·s⁻² |
| `T`   | `tesla`, `teslas` | tesla | `bu_tesla` | kg·A⁻¹·s⁻² |
| `H`   | `henry`, `henrys`, `henries` | henry | `bu_henry` | kg·m²·A⁻²·s⁻² |
| `lm`  | `lumen`, `lumens` | lumen | `bu_lumen` | cd·sr |
| `lx`  | `lux` | lux | `bu_lux` | cd·sr·m⁻² |
| `Bq`  | `becquerel`, `becquerels` | becquerel | `bu_becquerel` | s⁻¹ |
| `Gy`  | `gray`, `grays` | gray | `bu_gray` | m²·s⁻² |
| `Sv`  | `sievert`, `sieverts` | sievert | `bu_sievert` | m²·s⁻² |
| `kat` | `katal`, `katals` | katal | `bu_katal` | mol·s⁻¹ |
| `rad` | `radian`, `radians` | radian | `bu_radian` | dimensionless (m/m) |
| `sr`  | `steradian`, `steradians` | steradian | `bu_steradian` | dimensionless (m²/m²) |

### 6.3 Non-SI Units Accepted with SI

| Symbol | Long forms | Name | Enum | Factor / notes |
|--------|-----------|------|------|----------------|
| `L`, `l` | `liter`, `litre`, `liters`, `litres` | litre | `bu_liter` | 10⁻³ m³ |
| `min` | `minute`, `minutes` | minute | `bu_minute` | 60 s |
| `h`   | `hour`, `hours` | hour | `bu_hour` | 3600 s |
| `d`   | `day`, `days` | day | `bu_day` | 86400 s |
| `wk`  | `week`, `weeks` | week | `bu_week` | 604 800 s |
| `mo`  | `month`, `months` | month (Julian) | `bu_month` | 2 629 800 s (= 365.25 d / 12) |
| `fn`  | `fortnight`, `fortnights` | fortnight | `bu_fortnight` | 1 209 600 s (= 14 d) |
| `yr`  | `year`, `years` | year (Julian) | `bu_year` | 31 557 600 s |
| `°`, `deg`, `degr`, `degree`, `degrees` | — | degree (angle) | `bu_degree` | π/180 rad — U+00B0 |
| `°C`, `degC`, `degrC` | — | degree Celsius | `bu_celsius` | K = °C + 273.15 (affine) |
| `t`   | `tonne` | tonne | `bu_tonne` | 10³ kg |
| `bar` | — | bar | `bu_bar` | 10⁵ Pa |
| `eV`  | `electronvolt` | electronvolt | `bu_electronvolt` | 1.602176634×10⁻¹⁹ J |
| `Da`  | `dalton` | dalton | `bu_dalton` | 1.66053906660×10⁻²⁷ kg |
| `au`  | `astronomical_unit` | astronomical unit | `bu_astronomical_unit` | 1.495978707×10¹¹ m |
| `ha`  | `hectare` | hectare | `bu_hectare` | 10⁴ m² |

### 6.4 Imperial & US Customary — Length

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `in`   | `inch`, `inches` | inch | `bu_inch` | 0.0254 m (exact) |
| `ft`   | `foot`, `feet` | foot | `bu_foot` | 0.3048 m (exact) |
| `yd`   | `yard`, `yards` | yard | `bu_yard` | 0.9144 m (exact) |
| `mi`   | `mile`, `miles` | statute mile | `bu_mile` | 1609.344 m (exact) |
| `nmi`  | `nautical_mile`, `nautical_miles` | nautical mile | `bu_nautical_mile` | 1852 m (exact) |
| `Å` (U+212B) | `angstrom`, `angstroms`, Å (U+00C5) | ångström | `bu_angstrom` | 10⁻¹⁰ m |
| `ly`   | `light_year`, `light_years` | light-year | `bu_light_year` | 9.4607304725808×10¹⁵ m |
| `pc`   | `parsec`, `parsecs` | parsec | `bu_parsec` | 3.085677581491367×10¹⁶ m |
| `fur`  | `furlong`, `furlongs` | furlong | `bu_furlong` | 201.168 m (exact) |
| `fath` | `fathom`, `fathoms` | fathom | `bu_fathom` | 1.8288 m (exact) |
| `thou` | `thou`, `mil`, `mils` | thou (mil) | `bu_thou` | 25.4×10⁻⁶ m (exact) |
| `ch`   | `chain`, `chains` | chain (Gunter's) | `bu_chain` | 20.1168 m (exact) |
| `rd`   | `rod`, `rods` | rod (pole, perch) | `bu_rod` | 5.0292 m (exact) |

> `thou` and `mil` are synonyms for 1/1000 inch. The canonical output is `thou`. `mil` does **not** mean milliradian — use `m~rad` for milliradians.

### 6.5 Imperial & US Customary — Mass

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `lb`    | `lbs`, `pound`, `pounds` | pound (avoirdupois) | `bu_pound` | 0.45359237 kg (exact) |
| `oz`    | `ounce`, `ounces` | ounce (avoirdupois) | `bu_ounce` | 0.028349523125 kg (exact) |
| `gr`    | `grain`, `grains` | grain | `bu_grain` | 6.479891×10⁻⁵ kg (exact) |
| `st`    | `stone`, `stones` | stone | `bu_stone` | 6.35029318 kg (exact) |
| `tn_sh` | `short_ton`, `short_tons` | short ton (US) | `bu_short_ton` | 907.18474 kg (exact) |
| `tn_l`  | `long_ton`, `long_tons` | long ton (UK) | `bu_long_ton` | 1016.0469088 kg (exact) |
| `oz_t`  | `troy_ounce`, `troy_ounces` | troy ounce | `bu_troy_ounce` | 0.0311034768 kg (exact) |
| `ct`    | `carat`, `carats` | metric carat | `bu_carat` | 2×10⁻⁴ kg (exact) |
| `slug`  | `slugs` | slug | `bu_slug` | 14.593902937 kg |
| `dr`    | `dram`, `drams` | dram (avoirdupois) | `bu_dram` | 1.7718451953125×10⁻³ kg (exact) |
| `dwt`   | `pennyweight`, `pennyweights` | pennyweight (troy) | `bu_pennyweight` | 1.55517384×10⁻³ kg (exact) |

### 6.6 Temperature

| Symbol | Long forms | Name | Enum | Conversion |
|--------|-----------|------|------|------------|
| `°F`, `degF`, `degrF` | `fahrenheit` | degree Fahrenheit | `bu_fahrenheit` | K = (°F + 459.67) × 5/9 **(affine)** |
| `Ra`   | `rankine` | degree Rankine | `bu_rankine` | K = °Ra × 5/9 (linear) |

> Celsius (`°C`) is in §6.3. Kelvin (`K`) is an SI base unit in §6.1.
> `Ra` not `R` — `R` is reserved for röntgen.

### 6.7 Pressure

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `atm`  | `atmosphere`, `atmospheres` | standard atmosphere | `bu_atmosphere` | 101 325 Pa (exact) |
| `at`   | `atmosphere_technical` | atmosphere technical | `bu_atmosphere_technical` | 98 066.5 Pa (= 1 kgf/cm²) |
| `mmHg` | — | millimetre of mercury | `bu_mmhg` | 133.322387415 Pa |
| `Torr` | `torr` | torr | `bu_torr` | 101 325/760 Pa |
| `psi`  | — | pound-force per square inch | `bu_psi` | 6894.757293168361 Pa |
| `inHg` | `inch_hg`, `inch_mercury` | inch of mercury | `bu_inch_hg` | 3386.388645 Pa |

> `at ≠ atm`: 1 at = 98 066.5 Pa; 1 atm = 101 325 Pa.

### 6.8 Energy

| Symbol  | Long forms | Name | Enum | Factor |
|---------|-----------|------|------|--------|
| `cal`   | `calorie`, `calories` | thermochemical calorie | `bu_calorie` | 4.184 J (exact) |
| `Btu`   | `btu`, `BTU` | International Table BTU | `bu_btu` | 1055.05585262 J |
| `erg`   | `ergs` | erg | `bu_erg` | 10⁻⁷ J (exact) |
| `thm`   | `therm`, `therms` | US therm | `bu_therm` | 1.05480400×10⁸ J (exact) |
| `ft_lb` | `foot_pound`, `foot_pounds` | foot-pound | `bu_foot_pound` | 1.3558179483 J |

> `BTU` (all-caps) is a valid alias; currency lookup fails (no ISO 4217 entry), physical table matches. `Btu` and `btu` are also accepted.

### 6.9 Power

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `hp`   | `horsepower` | mechanical horsepower | `bu_horsepower` | 745.69987158227 W |
| `PS`   | `CV`, `metric_horsepower` | metric horsepower | `bu_metric_horsepower` | 735.49875 W (exact) |

### 6.10 Force

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `lbf`  | `pound_force` | pound-force | `bu_pound_force` | 4.4482216152605 N |
| `dyn`  | `dyne`, `dynes` | dyne | `bu_dyne` | 10⁻⁵ N (exact) |
| `kip`  | `kips` | kip (kilopound-force) | `bu_kip` | 4448.2216152605 N |
| `kgf`  | `kilogram_force` | kilogram-force | `bu_kilogram_force` | 9.80665 N (exact) |

### 6.11 Speed

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `kn`   | `knot`, `knots` | knot | `bu_knot` | 1852/3600 m/s |
| `rpm`  | — | revolutions per minute | `bu_rpm` | 1/60 s⁻¹ |

### 6.12 Volume — US Liquid

| Symbol  | Long forms | Name | Enum | Factor |
|---------|-----------|------|------|--------|
| `gal`   | `gallon`, `gallons` | US liquid gallon | `bu_gallon` | 3.785411784×10⁻³ m³ (exact) |
| `qt`    | `quart`, `quarts` | US liquid quart | `bu_quart` | 9.46352946×10⁻⁴ m³ |
| `pt`    | `pint`, `pints` | US liquid pint | `bu_pint` | 4.73176473×10⁻⁴ m³ |
| `cup`   | `cups` | US cup | `bu_cup` | 2.365882365×10⁻⁴ m³ |
| `gi`    | `gill`, `gills` | US gill | `bu_gill` | 1.18294118250×10⁻⁴ m³ |
| `fl_oz` | `fluid_ounce`, `fluid_ounces` | US fluid ounce | `bu_fluid_ounce` | 2.95735295625×10⁻⁵ m³ |
| `tbsp`  | `tablespoon`, `tablespoons` | US tablespoon | `bu_tablespoon` | 1.47867648×10⁻⁵ m³ |
| `tsp`   | `teaspoon`, `teaspoons` | US teaspoon | `bu_teaspoon` | 4.92892159375×10⁻⁶ m³ |
| `bbl`   | `barrel`, `barrels` | petroleum barrel | `bu_barrel` | 0.158987294928 m³ |

> `cup` (lowercase) = US cup. `CUP` (uppercase) = Cuban Peso. See §8.

### 6.13 Volume — UK Imperial

| Symbol     | Long forms | Name | Enum | Factor |
|------------|-----------|------|------|--------|
| `gal_uk`   | `gallon_uk`, `gallons_uk` | imperial gallon | `bu_gallon_uk` | 4.54609×10⁻³ m³ (exact) |
| `qt_uk`    | `quart_uk`, `quarts_uk` | imperial quart | `bu_quart_uk` | 1136.5225×10⁻⁶ m³ |
| `pt_uk`    | `pint_uk`, `pints_uk` | imperial pint | `bu_pint_uk` | 568.26125×10⁻⁶ m³ |
| `gi_uk`    | `gill_uk`, `gills_uk` | imperial gill | `bu_gill_uk` | 1.420653125×10⁻⁴ m³ (exact) |
| `fl_oz_uk` | `fluid_ounce_uk`, `fluid_ounces_uk` | imperial fluid ounce | `bu_fluid_ounce_uk` | 28.4130625×10⁻⁶ m³ |

### 6.14 Volume — US Apothecary & Dry

| Symbol  | Long forms | Name | Enum | Factor |
|---------|-----------|------|------|--------|
| `fl_dr` | `fluid_dram`, `fluid_drams` | US fluid dram | `bu_fluid_dram` | 3.6966911953125×10⁻⁶ m³ |
| `minim` | `minims` | US minim | `bu_minim` | 6.16115199218750×10⁻⁸ m³ |
| `pk`    | `peck`, `pecks` | US dry peck | `bu_peck` | 8.80976754172×10⁻³ m³ |
| `bsh`   | `bushel`, `bushels` | US bushel | `bu_bushel` | 3.523907016688×10⁻² m³ |

> `minim` not `min` (which is the minute).

### 6.15 Area

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `ac`   | `acre`, `acres` | acre | `bu_acre` | 4046.8564224 m² (exact) |
| `barn` | `barns` | barn | `bu_barn` | 10⁻²⁸ m² (exact) |

### 6.16 Angle

| Symbol   | Long forms | Name | Enum | Factor |
|----------|-----------|------|------|--------|
| `arcmin` | `arcminute`, `arcminutes` | arcminute | `bu_arcminute` | π/10800 rad |
| `arcsec` | `arcsecond`, `arcseconds` | arcsecond | `bu_arcsecond` | π/648000 rad |
| `grad`   | `gradian`, `gradians`, `gon` | gradian | `bu_grad` | π/200 rad |
| `rev`    | `turn`, `revolution`, `revolutions`, `turns` | revolution | `bu_revolution` | 2π rad |

### 6.17 Digital

| Symbol | Long forms | Name | Enum |
|--------|-----------|------|------|
| `b`    | `bit`, `bits` | bit | `bu_bit` |
| `B`    | `byte`, `bytes`, `Byte`, `Bytes` | byte | `bu_byte` |

### 6.18 CGS Units

| Symbol | Long forms | Name | Enum | SI equivalent |
|--------|-----------|------|------|---------------|
| `P`    | `poise`, `poises` | poise (dynamic viscosity) | `bu_poise` | 0.1 Pa·s |
| `St`   | `stokes`, `stoke` | stokes (kinematic viscosity) | `bu_stokes` | 10⁻⁴ m²·s⁻¹ |
| `G`    | `gauss` | gauss (magnetic flux density) | `bu_gauss` | 10⁻⁴ T |
| `Mx`   | `maxwell`, `maxwells` | maxwell (magnetic flux) | `bu_maxwell` | 10⁻⁸ Wb |
| `Oe`   | `oersted`, `oersteds` | oersted (magnetic field strength) | `bu_oersted` | 1000/(4π) A/m |
| `sb`   | `stilb`, `stilbs` | stilb (luminance) | `bu_stilb` | 10⁴ cd/m² |
| `ph`   | `phot`, `phots` | phot (illuminance) | `bu_phot` | 10⁴ lx |
| `Gal`  | `galileo`, `galileos` | galileo (acceleration) | `bu_galileo` | 10⁻² m/s² |

### 6.19 Radiation

| Symbol | Long forms | Name | Enum | SI equivalent |
|--------|-----------|------|------|---------------|
| `Ci`   | `curie`, `curies` | curie (radioactivity) | `bu_curie` | 3.7×10¹⁰ Bq |
| `R`    | `roentgen`, `roentgens` | röntgen (radiation exposure) | `bu_roentgen` | 2.58×10⁻⁴ C/kg |
| `rem`  | `rems` | rem (dose equivalent) | `bu_rem` | 10⁻² Sv |

### 6.20 Logarithmic

| Symbol | Long forms | Name | Enum | Notes |
|--------|-----------|------|------|-------|
| `Np`   | `neper`, `nepers` | neper | `bu_neper` | dimensionless; 1 Np = 20/ln(10) dB ≈ 8.686 dB |
| `dB`   | `decibel`, `decibels` | decibel | `bu_decibel` | dimensionless |

### 6.21 Electrical Power

| Symbol | Long forms | Name | Enum | Notes |
|--------|-----------|------|------|-------|
| `var`  | `vars` | var (volt-ampere reactive) | `bu_var` | reactive power; same SI dim as W |
| `VA`   | `volt_ampere`, `volt_amperes` | volt-ampere | `bu_volt_ampere` | apparent power; same SI dim as W |
| `gn`   | `standard_gravity` | standard gravity | `bu_standard_gravity` | 9.80665 m·s⁻² (exact) |

> `W`, `var`, and `VA` are dimensionally identical. `bvn_units_compatible` returns `true` across them; use `.components[0].base` to distinguish.

### 6.22 Textile Linear Density

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `tex`  | — | tex | `bu_tex` | 1×10⁻⁶ kg/m (ISO 1144); 9 den = 1 tex |
| `den`  | `denier`, `deniers` | denier | `bu_denier` | 1/9 000 000 kg/m |

### 6.23 Old German Units

No Old German unit accepts any SI or IEC prefix (`bvn_prefix_unit_valid` rejects all non-`si_none` prefixes). Enum values occupy positions 329–341 (after the full currency range).

#### Metric-Compatible German Units — Mass

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `Pfd`  | `pfund`, `pfunds` | Pfund | `bu_pfund` | 0.5 kg (exact) |
| `Ztr`  | `zentner` | Zentner | `bu_zentner` | 50 kg (exact) |
| `dz`   | `doppelzentner` | Doppelzentner | `bu_doppelzentner` | 100 kg (exact) |
| `lot`  | `lots` | Lot | `bu_lot` | 15.625×10⁻³ kg (exact) |

#### Historical German Units — Length (Prussian)

| Symbol    | Long forms | Name | Enum | Factor |
|-----------|-----------|------|------|--------|
| `prln`    | `prussian_line`, `linie` | Prussian line | `bu_prussian_line` | 2.17953×10⁻³ m |
| `prz`     | `prussian_zoll`, `zoll` | Prussian Zoll | `bu_prussian_zoll` | 2.61544×10⁻² m |
| `prf`     | `prussian_fuss`, `preussischer_fuss` | Prussian Fuß | `bu_prussian_fuss` | 3.13853×10⁻¹ m |
| `elle`    | `prussian_elle`, `preussische_elle` | Prussian Elle | `bu_prussian_elle` | 6.67160×10⁻¹ m |
| `rute`    | `prussian_rute`, `preussische_rute` | Prussian Rute | `bu_prussian_rute` | 3.76624 m |
| `klafter` | `prussian_klafter` | Klafter | `bu_klafter` | 1.88312 m |
| `dt_mi`   | `deutsche_meile`, `german_mile` | Geographische Meile | `bu_german_mile` | 7420.44 m |

#### Historical German Units — Area & Volume

| Symbol   | Long forms | Name | Enum | Factor |
|----------|-----------|------|------|--------|
| `morgen` | `prussian_morgen` | Morgen (Prussian) | `bu_morgen` | 2553.22 m² |
| `schffl` | `scheffel`, `prussian_scheffel` | Scheffel (Prussian) | `bu_scheffel` | 54.961×10⁻³ m³ |

### 6.24 Miscellaneous

Units that do not fit a single category above:

| Symbol | Category | Enum | Factor / notes |
|--------|----------|------|----------------|
| `°`, `deg` | Angle | `bu_degree` | π/180 rad (also in §6.3) |
| `°C` | Temperature | `bu_celsius` | Affine; K = °C + 273.15 |
| `°F` | Temperature | `bu_fahrenheit` | Affine; K = (°F + 459.67) × 5/9 |
| `au` | Length | `bu_astronomical_unit` | 1.495978707×10¹¹ m |
| `gn` | Acceleration | `bu_standard_gravity` | 9.80665 m·s⁻² |

---

## 7. Currencies

### 7.1 Namespace Rule

Any token consisting **exclusively of uppercase ASCII letters with length 3 or 4** is dispatched to the **currency table first**. If not found there, the physical unit table is tried. If found in neither, `error_unit_illegal` is raised.

```
"USD"  → currency table → US Dollar       ✓
"cup"  → physical table → US cup          ✓
"CUP"  → currency table → Cuban Peso      ✓  (not US cup)
"BTU"  → currency table → not found → physical table → bu_btu  ✓
"XYZ"  → currency table → not found → physical table → not found → ERROR
```

**Case is load-bearing.** `cup ≠ CUP`. `btu ≠ BTU ≠ Btu` (all valid, all `bu_btu`).

### 7.2 ISO 4217 Fiat Currencies

All 161 active ISO 4217 alphabetic codes. Enum values: `bu_aed` (134) … `bu_zwl` (294).

> **Minor unit:** exponent N such that 1 major unit = 10^N minor units.
> Call `bvn_currency_minor_unit(base, &ok)` to retrieve at runtime.

| Code | Num | Min | Name |
|------|-----|-----|------|
| `AED` | 784 | 2 | UAE Dirham |
| `AFN` | 971 | 2 | Afghan Afghani |
| `ALL` |   8 | 2 | Albanian Lek |
| `AMD` |  51 | 2 | Armenian Dram |
| `ANG` | 532 | 2 | Netherlands Antillean Guilder |
| `AOA` | 973 | 2 | Angolan Kwanza |
| `ARS` |  32 | 2 | Argentine Peso |
| `AUD` |  36 | 2 | Australian Dollar |
| `AWG` | 533 | 2 | Aruban Florin |
| `AZN` | 944 | 2 | Azerbaijani Manat |
| `BAM` | 977 | 2 | Bosnia-Herzegovina Convertible Mark |
| `BBD` |  52 | 2 | Barbados Dollar |
| `BDT` |  50 | 2 | Bangladeshi Taka |
| `BGN` | 975 | 2 | Bulgarian Lev |
| `BHD` |  48 | **3** | Bahraini Dinar |
| `BMD` |  60 | 2 | Bermudian Dollar |
| `BND` |  96 | 2 | Brunei Dollar |
| `BOB` |  68 | 2 | Boliviano |
| `BRL` | 986 | 2 | Brazilian Real |
| `BSD` |  44 | 2 | Bahamian Dollar |
| `BTN` |  64 | 2 | Bhutanese Ngultrum |
| `BWP` |  72 | 2 | Botswana Pula |
| `BYN` | 933 | 2 | Belarusian Ruble |
| `BZD` |  84 | 2 | Belize Dollar |
| `CAD` | 124 | 2 | Canadian Dollar |
| `CDF` | 976 | 2 | Congolese Franc |
| `CHF` | 756 | 2 | Swiss Franc |
| `CLF` | 990 | **4** | Unidad de Fomento |
| `CLP` | 152 | **0** | Chilean Peso |
| `CNY` | 156 | 2 | Chinese Yuan |
| `COP` | 170 | 2 | Colombian Peso |
| `CRC` | 188 | 2 | Costa Rican Colon |
| `CUP` | 192 | 2 | Cuban Peso |
| `CVE` | 132 | 2 | Cape Verdean Escudo |
| `CZK` | 203 | 2 | Czech Koruna |
| `DJF` | 262 | **0** | Djiboutian Franc |
| `DKK` | 208 | 2 | Danish Krone |
| `DOP` | 214 | 2 | Dominican Peso |
| `DZD` |  12 | 2 | Algerian Dinar |
| `EGP` | 818 | 2 | Egyptian Pound |
| `ERN` | 232 | 2 | Eritrean Nakfa |
| `ETB` | 230 | 2 | Ethiopian Birr |
| `EUR` | 978 | 2 | Euro |
| `FJD` | 242 | 2 | Fijian Dollar |
| `FKP` | 238 | 2 | Falkland Islands Pound |
| `GBP` | 826 | 2 | Pound Sterling |
| `GEL` | 981 | 2 | Georgian Lari |
| `GHS` | 936 | 2 | Ghanaian Cedi |
| `GIP` | 292 | 2 | Gibraltar Pound |
| `GMD` | 270 | 2 | Gambian Dalasi |
| `GNF` | 324 | **0** | Guinean Franc |
| `GTQ` | 320 | 2 | Guatemalan Quetzal |
| `GYD` | 328 | 2 | Guyanese Dollar |
| `HKD` | 344 | 2 | Hong Kong Dollar |
| `HNL` | 340 | 2 | Honduran Lempira |
| `HRK` | 191 | 2 | Croatian Kuna |
| `HTG` | 332 | 2 | Haitian Gourde |
| `HUF` | 348 | 2 | Hungarian Forint |
| `IDR` | 360 | 2 | Indonesian Rupiah |
| `ILS` | 376 | 2 | Israeli New Shekel |
| `INR` | 356 | 2 | Indian Rupee |
| `IQD` | 368 | **3** | Iraqi Dinar |
| `IRR` | 364 | 2 | Iranian Rial |
| `ISK` | 352 | **0** | Icelandic Krona |
| `JMD` | 388 | 2 | Jamaican Dollar |
| `JOD` | 400 | **3** | Jordanian Dinar |
| `JPY` | 392 | **0** | Japanese Yen |
| `KES` | 404 | 2 | Kenyan Shilling |
| `KGS` | 417 | 2 | Kyrgystani Som |
| `KHR` | 116 | 2 | Cambodian Riel |
| `KMF` | 174 | **0** | Comorian Franc |
| `KPW` | 408 | 2 | North Korean Won |
| `KRW` | 410 | **0** | South Korean Won |
| `KWD` | 414 | **3** | Kuwaiti Dinar |
| `KYD` | 136 | 2 | Cayman Islands Dollar |
| `KZT` | 398 | 2 | Kazakhstani Tenge |
| `LAK` | 418 | 2 | Laotian Kip |
| `LBP` | 422 | 2 | Lebanese Pound |
| `LKR` | 144 | 2 | Sri Lankan Rupee |
| `LRD` | 430 | 2 | Liberian Dollar |
| `LSL` | 426 | 2 | Lesotho Loti |
| `LYD` | 434 | **3** | Libyan Dinar |
| `MAD` | 504 | 2 | Moroccan Dirham |
| `MDL` | 498 | 2 | Moldovan Leu |
| `MGA` | 969 | 2 | Malagasy Ariary |
| `MKD` | 807 | 2 | Macedonian Denar |
| `MMK` | 104 | 2 | Myanmar Kyat |
| `MNT` | 496 | 2 | Mongolian Togrog |
| `MOP` | 446 | 2 | Macanese Pataca |
| `MRU` | 929 | 2 | Mauritanian Ouguiya |
| `MUR` | 480 | 2 | Mauritian Rupee |
| `MVR` | 462 | 2 | Maldivian Rufiyaa |
| `MWK` | 454 | 2 | Malawian Kwacha |
| `MXN` | 484 | 2 | Mexican Peso |
| `MYR` | 458 | 2 | Malaysian Ringgit |
| `MZN` | 943 | 2 | Mozambican Metical |
| `NAD` | 516 | 2 | Namibian Dollar |
| `NGN` | 566 | 2 | Nigerian Naira |
| `NIO` | 558 | 2 | Nicaraguan Cordoba |
| `NOK` | 578 | 2 | Norwegian Krone |
| `NPR` | 524 | 2 | Nepalese Rupee |
| `NZD` | 554 | 2 | New Zealand Dollar |
| `OMR` | 512 | **3** | Omani Rial |
| `PAB` | 590 | 2 | Panamanian Balboa |
| `PEN` | 604 | 2 | Peruvian Sol |
| `PGK` | 598 | 2 | Papua New Guinean Kina |
| `PHP` | 608 | 2 | Philippine Peso |
| `PKR` | 586 | 2 | Pakistani Rupee |
| `PLN` | 985 | 2 | Polish Zloty |
| `PYG` | 600 | **0** | Paraguayan Guarani |
| `QAR` | 634 | 2 | Qatari Riyal |
| `RON` | 946 | 2 | Romanian Leu |
| `RSD` | 941 | 2 | Serbian Dinar |
| `RUB` | 643 | 2 | Russian Ruble |
| `RWF` | 646 | **0** | Rwandan Franc |
| `SAR` | 682 | 2 | Saudi Riyal |
| `SBD` |  90 | 2 | Solomon Islands Dollar |
| `SCR` | 690 | 2 | Seychellois Rupee |
| `SDG` | 938 | 2 | Sudanese Pound |
| `SEK` | 752 | 2 | Swedish Krona |
| `SGD` | 702 | 2 | Singapore Dollar |
| `SHP` | 654 | 2 | Saint Helena Pound |
| `SLL` | 694 | 2 | Sierra Leonean Leone |
| `SOS` | 706 | 2 | Somali Shilling |
| `SRD` | 968 | 2 | Surinamese Dollar |
| `STN` | 930 | 2 | Sao Tome and Principe Dobra |
| `SVC` | 222 | 2 | Salvadoran Colon |
| `SYP` | 760 | 2 | Syrian Pound |
| `SZL` | 748 | 2 | Swazi Lilangeni |
| `THB` | 764 | 2 | Thai Baht |
| `TJS` | 972 | 2 | Tajikistani Somoni |
| `TMT` | 934 | 2 | Turkmenistan Manat |
| `TND` | 788 | **3** | Tunisian Dinar |
| `TOP` | 776 | 2 | Tongan Pa'anga |
| `TRY` | 949 | 2 | Turkish Lira |
| `TTD` | 780 | 2 | Trinidad and Tobago Dollar |
| `TWD` | 901 | 2 | New Taiwan Dollar |
| `TZS` | 834 | 2 | Tanzanian Shilling |
| `UAH` | 980 | 2 | Ukrainian Hryvnia |
| `UGX` | 800 | **0** | Ugandan Shilling |
| `USD` | 840 | 2 | US Dollar |
| `UYU` | 858 | 2 | Uruguayan Peso |
| `UZS` | 860 | 2 | Uzbekistani Som |
| `VES` | 928 | 2 | Venezuelan Bolivar Soberano |
| `VND` | 704 | **0** | Vietnamese Dong |
| `VUV` | 548 | **0** | Vanuatu Vatu |
| `WST` | 882 | 2 | Samoan Tala |
| `XAF` | 950 | **0** | CFA Franc BEAC |
| `XAG` | 961 | **0** | Silver |
| `XAU` | 959 | **0** | Gold |
| `XCD` | 951 | 2 | East Caribbean Dollar |
| `XDR` | 960 | **0** | Special Drawing Rights |
| `XOF` | 952 | **0** | CFA Franc BCEAO |
| `XPD` | 964 | **0** | Palladium |
| `XPF` | 953 | **0** | CFP Franc |
| `XPT` | 962 | **0** | Platinum |
| `XTS` | 963 | **0** | Test (ISO 4217 reserved) |
| `YER` | 886 | 2 | Yemeni Rial |
| `ZAR` | 710 | 2 | South African Rand |
| `ZMW` | 967 | 2 | Zambian Kwacha |
| `ZWL` | 932 | 2 | Zimbabwean Dollar |

> Minor units **bold** when ≠ 2. `CLF` is the only currency with 4 minor units.
> `XTS` is the ISO 4217 testing code; it is present but should not appear in production data.

### 7.3 Cryptocurrencies

34 cryptocurrencies. Enum values: `bu_btc` (295) … `bu_atom` (328). `numeric_code = 0` for all.

| Code   | Min | Subunit name | Name |
|--------|-----|-------------|------|
| `BTC`  |  8  | satoshi | Bitcoin |
| `ETH`  | 18  | wei | Ethereum |
| `SOL`  |  9  | lamport | Solana |
| `XRP`  |  6  | drop | XRP |
| `BNB`  | 18  | — | BNB |
| `ADA`  |  6  | lovelace | Cardano |
| `LTC`  |  8  | — | Litecoin |
| `DOT`  | 10  | planck | Polkadot |
| `XMR`  | 12  | piconero | Monero |
| `ETC`  | 18  | — | Ethereum Classic |
| `BCH`  |  8  | — | Bitcoin Cash |
| `XLM`  |  7  | stroop | Stellar |
| `FIL`  | 18  | — | Filecoin |
| `ICP`  |  8  | — | Internet Computer |
| `TRX`  |  6  | — | TRON |
| `EOS`  |  4  | — | EOS |
| `VET`  | 18  | — | VeChain |
| `NEO`  |  8  | — | Neo |
| `ZEC`  |  8  | — | Zcash |
| `UNI`  | 18  | — | Uniswap |
| `ARB`  | 18  | — | Arbitrum |
| `SUI`  |  9  | — | Sui |
| `TON`  |  9  | — | Toncoin |
| `INJ`  | 18  | — | Injective |
| `SEI`  |  6  | — | Sei |
| `APT`  |  8  | — | Aptos |
| `TAO`  |  9  | — | Bittensor |
| `WIF`  |  6  | — | dogwifhat |
| `DOGE` |  8  | koinu | Dogecoin |
| `LINK` | 18  | — | Chainlink |
| `USDT` |  6  | — | Tether |
| `USDC` |  6  | — | USD Coin |
| `AVAX` | 18  | — | Avalanche |
| `ATOM` |  6  | — | Cosmos |

> **Min** = `minor_unit` = decimal places in the canonical on-chain representation.
> E.g. `<uint:64,ETH>` stores wei; divide by 10¹⁸ to get ETH.

### 7.4 Currency Prefix Rules

```bovnar
# SI prefixes — all 24 allowed
.fund_nav  = <float_dec:64,k~USD>   250.0;   # $250,000 (k = ×1000)
.gdp       = <float_dec:64,M~EUR> 42800.0;   # €42.8 billion
.gas_price = <float_dec:64,G~ETH>    35.0;   # 35 Gwei

# IEC prefixes — forbidden on all currencies
.bad = <float_dec:64,Ki~USD> 1.0;   # → error_unit_illegal
```

---

## 8. Symbol Disambiguation

Complete conflict table — every case where a physical token could be confused with a currency:

| Token | Physical meaning | Currency meaning | Resolution |
|-------|-----------------|-----------------|------------|
| `cup`  | US cup (236.6 mL, `bu_cup`) | — | Unambiguous: no currency uses lowercase |
| `CUP`  | — | Cuban Peso (ISO 4217:192) | Unambiguous: dispatches to currency table |
| `BTU`  | International Table BTU (`bu_btu`) | *(not in ISO 4217)* | Currency lookup fails; physical table matches |
| `Btu`  | International Table BTU (`bu_btu`) | — | Mixed-case; unambiguous |
| `btu`  | International Table BTU (`bu_btu`) | — | All-lowercase; unambiguous |
| `SOL`  | — | Solana (crypto) | Unambiguous: no physical unit named `SOL` |
| `BAR`  | *(no uppercase alias)* | *(not in ISO 4217)* | `error_unit_illegal`; use lowercase `bar` |
| `ERG`  | *(no uppercase alias)* | *(not in ISO 4217)* | `error_unit_illegal`; use lowercase `erg` |
| `CAD`  | *(no alias)* | Canadian Dollar | Unambiguous: currency table |
| `XAU`  | *(no alias)* | Gold (ISO 4217 X-code) | Unambiguous: currency table |

**Key finding:** No token is simultaneously a valid physical unit symbol and a valid currency code. All conflicts involve either an all-uppercase alias of a unit whose canonical form is mixed/lowercase, or a token not in the currency table at all.

---

## 9. Common Patterns

### Physical Quantities

```bovnar
# SI base units
.distance     = <float:64,m>           384400000.0;  # metres
.mass         = <float:64,k~g>                 70.5;  # kilogram
.time         = <float:64,µ~s>             50000.0;  # microseconds
.temperature  = <float:32,°C>                 23.5;  # Celsius

# Derived quantities
.velocity     = <float:64,m/s>              9.81;
.acceleration = <float:64,m/s²>             9.81;   # = m·s⁻²
.force        = <float:64,k~g·m/s²>         9.81;   # = Newton
.pressure     = <float:64,k~Pa>           250.0;
.energy       = <float:64,k~J>           5400.0;
.power        = <float:64,k~W>              1.5;
.voltage      = <float:32,m~V>            330.0;
.resistance   = <float:32,k~Ω>              4.7;
.frequency    = <float:64,M~Hz>          2400.0;

# Constants (inline suffix form)
.gravity_G    = 6.674e-11 m³/k~g/s²;     # gravitational constant
.boltzmann    = 1.380649e-23 J/K;
.planck       = 6.62607015e-34 J·s;

# Digital storage
.packet       = <uint:16,B>               1500;   # bytes
.cache        = <uint:32,Ki~B>             512;   # kibibytes
.ram          = <uint:64,Gi~B>               8;   # gibibytes
.link         = <float:64,G~b/s>           10.0;  # gigabits/s
```

### Monetary Values

```bovnar
# Scalar amounts — recommended type: float_dec
.price        = <float_dec:64,USD>        19.99;
.balance      = <float_dec:64,EUR>       342.00;
.high_prec    = <float_dec:128,GBP>     3421.78;

# Zero-minor-unit currencies: integer only
.ticket       = <uint:64,JPY>              2500;   # yen
.in_fils      = <uint:64,KWD>             3500;   # 3.500 KWD

# Negative balance (minor units)
.overdraft    = <sint:64,EUR>             -4999;   # -€49.99

# Reporting scale
.fund_nav     = <float_dec:64,k~USD>     250.0;   # $250,000
.gdp          = <float_dec:64,M~EUR>   42800.0;   # €42.8 billion

# Commodity prices
.gold         = <float_dec:64,USD/oz_t> 2351.40;  # $/troy oz
.oil          = <float_dec:64,USD/bbl>    78.20;  # $/barrel
.wheat        = <float_dec:64,USD/bsh>     5.82;  # $/bushel
.rent         = <float_dec:64,EUR/m²>     12.50;  # €/m²
.billing      = <float_dec:64,EUR/h>     150.00;  # €/h

# Exchange rates
.eur_usd      = <float_dec:64,USD/EUR>   1.0842;
.usd_jpy      = <float_dec:64,JPY/USD>  149.32;

# Crypto (on-chain integer)
.btc_sat      = <uint:64,BTC>         54782000;   # satoshis
.eth_wei      = <uint:64,ETH>   2500000000000000000;  # wei
.gas_gwei     = <float_dec:64,G~ETH>      35.0;  # gas price

# Cross-crypto
.eth_btc      = <float_dec:64,BTC/ETH>  0.05610;
```

### Type Pairing for Currencies

| Use case | Recommended | Avoid |
|----------|-------------|-------|
| Decimal monetary amount | `<float_dec:64,USD>` | `<float:64,USD>` (binary rounding) |
| High-precision / actuarial | `<float_dec:128,USD>` | — |
| Integer minor-unit storage | `<uint:64,USD>` | — |
| Negative balances in minor units | `<sint:64,USD>` | — |
| Zero-minor-unit currency (JPY, KRW…) | `<uint:64,JPY>` | `<float_dec:64,JPY>` |
| On-chain satoshi balance | `<uint:64,BTC>` | — |
| Human-readable BTC amount | `<float_dec:64,BTC>` | `<float:64,BTC>` |
| `float_fix` for money | — | **Never** (Q-format ≠ decimal) |

---

## 10. Error Reference

Unit-specific errors raised during the `on_unverified` → validator phase:

| Error code | Value | Trigger |
|------------|-------|---------|
| `error_unit_illegal` | 32 | Unknown prefix; unknown base unit; unknown currency code; all-uppercase 3–4 char token found in neither table; IEC prefix on a non-digital unit or currency; sub-kilo SI prefix on `b`/`B`; empty component between separators (e.g. `m//s`); more than 8 components in a compound unit |
| `error_unit_too_long` | 22 | Unit string exceeds internal type-buffer limit |
| `error_unit_mismatch` | 38 | Inline unit suffix and explicit type-annotation unit both present but parse to different `value_unit_t` |
| `error_unexpected_input_byte` | 15 | Inline unit suffix appears inside an array element `[…]` |

### Correct vs Incorrect Examples

```bovnar
# ── Correct ──────────────────────────────────────────────────────────────────
.ok1  = <uint:32,no_unit>       42;      # explicit dimensionless
.ok2  = <uint:64,Ki~B>        1024;      # IEC prefix on byte
.ok3  = <float:64,BTU>          1.0;    # all-caps alias → bu_btu
.ok4  = <float_dec:64,m~USD>    0.001;  # sub-kilo SI prefix on currency
.ok5  = <float_dec:32,cup>      2.0;    # US cup (lowercase)
.ok6  = <float_dec:64,CUP>     25.00;   # Cuban Peso (uppercase)
.ok7  = <float:64,m/s>    9.81 m·s⁻¹;  # matching notation forms

# ── Incorrect ─────────────────────────────────────────────────────────────────
# .bad1 = <float:64,m//s>        1.0;   # error_unit_illegal — empty component
# .bad2 = <float:64,Ki~m>        1.0;   # error_unit_illegal — IEC prefix on metre
# .bad3 = <uint:32,m~B>        512;     # error_unit_illegal — milli on byte
# .bad4 = <float_dec:64,Ki~USD>  1.0;   # error_unit_illegal — IEC on currency
# .bad5 = <float:64,m>     9.81 s;      # error_unit_mismatch — m ≠ s
# .bad6 = [9.81 m/s, 3.14 m];           # error_unexpected_input_byte
# .bad7 = <float:64,m*s*k~g*A*K*mol*cd*b*B> 1.0; # error_unit_illegal — 9 > 8
```

---

*Bovnar cheat sheet — covers all units and currencies as of spec version 1.1.*
*Unit count: 146 physical (positions 1–133, 329–341) + 161 fiat (134–294) + 34 crypto (295–328) = 341.*
*`BVN_VALUE_BASE_UNIT_COUNT` = 342 (`bu_scheffel` + 1).*
