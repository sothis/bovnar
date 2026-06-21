# Bovnar (BVNR) — Units & Currencies Reference

> Spec version 1.1 · 163 physical units · 166 fiat currencies · 50 cryptocurrencies

---

## Contents

1. [SI Prefixes](#1-si-prefixes)
2. [IEC Binary Prefixes](#2-iec-binary-prefixes)
3. [Prefix Validity Rules](#3-prefix-validity-rules)
4. [Physical Units](#4-physical-units)
   - 4.1 [SI Base Units](#41-si-base-units)
   - 4.2 [Named SI-Derived Units](#42-named-si-derived-units)
   - 4.3 [Non-SI Units Accepted with SI](#43-non-si-units-accepted-with-si)
   - 4.4 [Imperial & US Customary — Length](#44-imperial--us-customary--length)
   - 4.5 [Imperial & US Customary — Mass](#45-imperial--us-customary--mass)
   - 4.6 [Temperature](#46-temperature)
   - 4.7 [Pressure](#47-pressure)
   - 4.8 [Energy](#48-energy)
   - 4.9 [Power](#49-power)
   - 4.10 [Force](#410-force)
   - 4.11 [Speed & Rotation](#411-speed--rotation)
   - 4.12 [Acceleration](#412-acceleration)
   - 4.13 [Volume — US Liquid](#413-volume--us-liquid)
   - 4.14 [Volume — UK Imperial](#414-volume--uk-imperial)
   - 4.15 [Volume — US Apothecary & Dry](#415-volume--us-apothecary--dry)
   - 4.16 [Area](#416-area)
   - 4.17 [Angle](#417-angle)
   - 4.18 [Digital](#418-digital)
   - 4.19 [CGS Units](#419-cgs-units)
   - 4.20 [Radiation](#420-radiation)
   - 4.21 [Logarithmic](#421-logarithmic)
   - 4.22 [Electrical Power](#422-electrical-power)
   - 4.23 [Textile Linear Density](#423-textile-linear-density)
   - 4.24 [Old German Units](#424-old-german-units)
   - 4.25 [Additional Physical Units](#425-additional-physical-units-361367)
   - 4.26 [Ratio and Proportion](#426-ratio-and-proportion-372377)
5. [Currencies](#5-currencies)
   - 5.1 [The Mandatory Currency Sigil](#51-the-mandatory-currency-sigil)
   - 5.2 [ISO 4217 Fiat Currencies](#52-iso-4217-fiat-currencies)
   - 5.3 [Cryptocurrencies](#53-cryptocurrencies)
   - 5.4 [Currency Prefix Rules](#54-currency-prefix-rules)
6. [Symbol Disambiguation](#6-symbol-disambiguation)

---

## 1. SI Prefixes

Written as `prefix~base` (mandatory `~` separator). Example: `k~m` = kilometre.

| Name   | Symbol | Factor | Enum (`si_prefix_id_t`) |
|--------|--------|--------|--------------------------|
| quetta | `Q`    | 10³⁰   | `si_quetta` |
| ronna  | `R`    | 10²⁷   | `si_ronna`  |
| yotta  | `Y`    | 10²⁴   | `si_yotta`  |
| zetta  | `Z`    | 10²¹   | `si_zetta`  |
| exa    | `E`    | 10¹⁸   | `si_exa`    |
| peta   | `P`    | 10¹⁵   | `si_peta`   |
| tera   | `T`    | 10¹²   | `si_tera`   |
| giga   | `G`    | 10⁹    | `si_giga`   |
| mega   | `M`    | 10⁶    | `si_mega`   |
| kilo   | `k`    | 10³    | `si_kilo`   |
| hecto  | `h`    | 10²    | `si_hecto`  |
| deca   | `da`   | 10¹    | `si_deca`   |
| *(none)* | —    | 10⁰    | `si_none`   |
| deci   | `d`    | 10⁻¹   | `si_deci`   |
| centi  | `c`    | 10⁻²   | `si_centi`  |
| milli  | `m`    | 10⁻³   | `si_milli`  |
| micro  | `µ`    | 10⁻⁶   | `si_micro`  |
| nano   | `n`    | 10⁻⁹   | `si_nano`   |
| pico   | `p`    | 10⁻¹²  | `si_pico`   |
| femto  | `f`    | 10⁻¹⁵  | `si_femto`  |
| atto   | `a`    | 10⁻¹⁸  | `si_atto`   |
| zepto  | `z`    | 10⁻²¹  | `si_zepto`  |
| yocto  | `y`    | 10⁻²⁴  | `si_yocto`  |
| ronto  | `r`    | 10⁻²⁷  | `si_ronto`  |
| quecto | `q`    | 10⁻³⁰  | `si_quecto` |

> `µ` is U+00B5 MICRO SIGN (UTF-8 `0xC2 0xB5`). U+03BC (Greek small letter mu) is **not** accepted.
> `da` is a two-character prefix: `da~m` = decametre.

**Prefix–base ambiguities** — resolved by the mandatory `~`:

| Bare token | Is a base unit | With `~` becomes prefix |
|------------|----------------|--------------------------|
| `m`  | meter (`bu_meter`)    | milli  |
| `d`  | day (`bu_day`)        | deci   |
| `h`  | hour (`bu_hour`)      | hecto  |
| `T`  | tesla (`bu_tesla`)    | tera   |
| `G`  | gauss (`bu_gauss`)    | giga   |
| `P`  | poise (`bu_poise`)    | peta   |
| `R`  | röntgen (`bu_roentgen`) | ronna |
| `f`  | farad (`bu_farad`)    | femto  |
| `S`  | siemens (`bu_siemens`) | *(not a prefix — `S` has no prefix role)* |

Examples: bare `m` = metre; `m~s` = millisecond. Bare `d` = day; `d~s` = decisecond.

---

## 2. IEC Binary Prefixes

Used **only** on `b` (bit) and `B` (byte). Written as `prefix~base`: `Ki~B` = kibibyte.

| Name  | Symbol | Factor   | Enum (`iec_prefix_id_t`) |
|-------|--------|----------|--------------------------|
| kibi  | `Ki`   | 2¹⁰      | `iec_kibi`  |
| mebi  | `Mi`   | 2²⁰      | `iec_mebi`  |
| gibi  | `Gi`   | 2³⁰      | `iec_gibi`  |
| tebi  | `Ti`   | 2⁴⁰      | `iec_tebi`  |
| pebi  | `Pi`   | 2⁵⁰      | `iec_pebi`  |
| exbi  | `Ei`   | 2⁶⁰      | `iec_exbi`  |
| zebi  | `Zi`   | 2⁷⁰      | `iec_zebi`  |
| yobi  | `Yi`   | 2⁸⁰      | `iec_yobi`  |
| robi  | `Ri`   | 2⁹⁰      | `iec_robi`  |
| quebi | `Qi`   | 2¹⁰⁰     | `iec_quebi` |

---

## 3. Prefix Validity Rules

| Unit category | SI prefixes | IEC prefixes |
|---------------|-------------|--------------|
| All physical units (default) | All 24 allowed | Forbidden |
| `b` (bit) and `B` (byte) | Only ≥ kilo (`k`, `M`, `G`, …, `Q`) | All 10 allowed |
| `b` and `B` with sub-kilo SI (`d`, `c`, `m`, `µ`, `n`, `p`, `f`, `a`, `z`, `y`, `r`, `q`, `da`, `h`) | **Forbidden** | — |
| Currency codes | All 24 allowed | **Forbidden** |
| Old German units (`bu_pfund` … `bu_scheffel`) | **None** (`si_none` only) | **Forbidden** |

---

## 4. Physical Units

> **Symbol** = canonical serialized form (produced on output; accepted on input).
> **Long forms** = accepted on input only; never produced on output.
> **Factor** = conversion factor to SI base units unless noted.

### 4.1 SI Base Units

| Symbol | Long forms | Name | Enum |
|--------|-----------|------|------|
| `s`   | `sec`, `second`, `seconds` | second | `bu_second` |
| `m`   | `meter`, `metre`, `meters`, `metres` | metre | `bu_meter` |
| `g`   | `gram`, `grams` | gram | `bu_gram` |
| `A`   | `amp`, `amps`, `ampere`, `amperes` | ampere | `bu_ampere` |
| `K`   | `kelvin`, `kelvins` | kelvin | `bu_kelvin` |
| `mol` | `mole`, `moles` | mole | `bu_mol` |
| `cd`  | `candela`, `candelas` | candela | `bu_candela` |

> `g` (gram) is the base symbol; `k~g` = kilogram.

### 4.2 Named SI-Derived Units

| Symbol | Long forms | Name | Enum | SI definition |
|--------|-----------|------|------|---------------|
| `Hz`  | `hertz` | hertz | `bu_hertz` | s⁻¹ |
| `N`   | `newton`, `newtons` | newton | `bu_newton` | kg·m·s⁻² |
| `Pa`  | `pascal`, `pascals` | pascal | `bu_pascal` | kg·m⁻¹·s⁻² |
| `J`   | `joule`, `joules` | joule | `bu_joule` | kg·m²·s⁻² |
| `W`   | `watt`, `watts` | watt | `bu_watt` | kg·m²·s⁻³ |
| `V`   | `volt`, `volts` | volt | `bu_volt` | kg·m²·A⁻¹·s⁻³ |
| `Ω`   | `ohm`, `ohms`, `Ohm` | ohm | `bu_ohm` | kg·m²·A⁻²·s⁻³ — U+2126 OHM SIGN; U+03A9 (Greek capital omega) **not** accepted |
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

### 4.3 Non-SI Units Accepted with SI

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
| `°`, `deg` | `degr`, `degree`, `degrees` | degree (angle) | `bu_degree` | π/180 rad — U+00B0 |
| `t`   | `tonne` | tonne | `bu_tonne` | 10³ kg |
| `bar` | — | bar | `bu_bar` | 10⁵ Pa |
| `eV`  | `electronvolt` | electronvolt | `bu_electronvolt` | 1.602176634×10⁻¹⁹ J |
| `Da`  | `dalton` | dalton | `bu_dalton` | 1.66053906660×10⁻²⁷ kg |
| `au`  | — | astronomical unit | `bu_astronomical_unit` | 1.495978707×10¹¹ m |
| `ha`  | `hectare` | hectare | `bu_hectare` | 10⁴ m² |

### 4.4 Imperial & US Customary — Length

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
| `thou` | `thou`, `mil`, `mils` | thou | `bu_thou` | 25.4×10⁻⁶ m (exact) |
| `ch`   | `chain`, `chains` | chain (Gunter's) | `bu_chain` | 20.1168 m (exact) |
| `rd`   | `rod`, `rods` | rod (pole, perch) | `bu_rod` | 5.0292 m (exact) |

> `thou` and `mil` are synonyms. Canonical output is `thou`. `mil` does **not** mean milliradian; milliradians are written `m~rad`.

### 4.5 Imperial & US Customary — Mass

| Symbol  | Long forms | Name | Enum | Factor |
|---------|-----------|------|------|--------|
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

### 4.6 Temperature

| Symbol | Long forms | Name | Enum | Conversion |
|--------|-----------|------|------|------------|
| `°C`, `degC` | `degrC`, `degreeC`, `degreesC`, `celsius` | degree Celsius | `bu_celsius` | K = °C + 273.15 **(affine)** |
| `°F`, `degF` | `degrF`, `degreeF`, `degreesF`, `fahrenheit` | degree Fahrenheit | `bu_fahrenheit` | K = (°F + 459.67) × 5/9 **(affine)** |
| `°Ra`, `degRa` | `degrRa`, `degreeRa`, `degreesRa`, `rankine` | degree Rankine | `bu_rankine` | K = °Ra × 5/9 (linear) |
| `°De`, `degDe` | `degrDe`, `degreeDe`, `degreesDe`, `delisle` | degree Delisle | `bu_delisle` | K = 373.15 − °De × 2/3 **(affine)** |
| `°N`, `degN` | `degrN`, `degreeN`, `degreesN`, `newton_temperature` | degree Newton | `bu_newton_temp` | K = °N × 100/33 + 273.15 **(affine)** |
| `°Re`, `degRe` | `degrRe`, `degreeRe`, `degreesRe`, `reaumur` | degree Réaumur | `bu_reaumur` | K = °Re × 5/4 + 273.15 **(affine)** |
| `°Ro`, `degRo` | `degrRo`, `degreeRo`, `degreesRo`, `romer` | degree Rømer | `bu_romer` | K = (°Ro − 7.5) × 40/21 + 273.15 **(affine)** |

> Kelvin (`K`) is in §4.1. `R` is reserved for röntgen (§4.20). `N` alone is newton (§4.2); use `°N` or `degN` for Newton temperature.

### 4.7 Pressure

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `atm`  | `atmosphere`, `atmospheres` | standard atmosphere | `bu_atmosphere` | 101 325 Pa (exact) |
| `at`   | `atmosphere_technical` | atmosphere technical | `bu_atmosphere_technical` | 98 066.5 Pa (= 1 kgf/cm²) |
| `mmHg` | — | millimetre of mercury | `bu_mmhg` | 133.322387415 Pa |
| `Torr` | `torr` | torr | `bu_torr` | 101 325/760 Pa |
| `psi`  | — | pound-force per square inch | `bu_psi` | 6894.757293168361 Pa |
| `inHg` | `inch_hg`, `inch_mercury` | inch of mercury | `bu_inch_hg` | 3386.388645 Pa |

> `at ≠ atm`: 1 at = 98 066.5 Pa; 1 atm = 101 325 Pa.

### 4.8 Energy

| Symbol  | Long forms | Name | Enum | Factor |
|---------|-----------|------|------|--------|
| `cal`   | `calorie`, `calories` | thermochemical calorie | `bu_calorie` | 4.184 J (exact) |
| `Btu`   | `btu`, `BTU` | International Table BTU | `bu_btu` | 1055.05585262 J |
| `erg`   | `ergs` | erg | `bu_erg` | 10⁻⁷ J (exact) |
| `thm`   | `therm`, `therms` | US therm | `bu_therm` | 1.05480400×10⁸ J (exact) |
| `ft_lb` | `foot_pound`, `foot_pounds` | foot-pound | `bu_foot_pound` | 1.3558179483 J |

> `BTU` (all-caps) is a valid alias: with no `$` sigil the token is a physical-unit lookup (the currency table is only consulted for `$`-prefixed tokens), and it matches `bu_btu`. `Btu` and `btu` are also accepted.

### 4.9 Power

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `hp`   | `horsepower` | mechanical horsepower | `bu_horsepower` | 745.69987158227 W |
| `PS`   | `CV`, `metric_horsepower` | metric horsepower | `bu_metric_horsepower` | 735.49875 W (exact) |

### 4.10 Force

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `lbf`  | `pound_force` | pound-force | `bu_pound_force` | 4.4482216152605 N |
| `dyn`  | `dyne`, `dynes` | dyne | `bu_dyne` | 10⁻⁵ N (exact) |
| `kip`  | `kips` | kip (kilopound-force) | `bu_kip` | 4448.2216152605 N |
| `kgf`  | `kilogram_force` | kilogram-force | `bu_kilogram_force` | 9.80665 N (exact) |

### 4.11 Speed & Rotation

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `kn`   | `knot`, `knots` | knot | `bu_knot` | 1852/3600 m/s |
| `rpm`  | — | revolutions per minute | `bu_rpm` | 1/60 s⁻¹ |

### 4.12 Acceleration

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `gn`   | `standard_gravity` | standard gravity | `bu_standard_gravity` | 9.80665 m·s⁻² (exact) |

### 4.13 Volume — US Liquid

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

> `cup` (lowercase) = US cup; `CUP` (uppercase) = Cuban Peso. See §6.

### 4.14 Volume — UK Imperial

| Symbol     | Long forms | Name | Enum | Factor |
|------------|-----------|------|------|--------|
| `gal_uk`   | `gallon_uk`, `gallons_uk` | imperial gallon | `bu_gallon_uk` | 4.54609×10⁻³ m³ (exact) |
| `qt_uk`    | `quart_uk`, `quarts_uk` | imperial quart | `bu_quart_uk` | 1136.5225×10⁻⁶ m³ |
| `pt_uk`    | `pint_uk`, `pints_uk` | imperial pint | `bu_pint_uk` | 568.26125×10⁻⁶ m³ |
| `gi_uk`    | `gill_uk`, `gills_uk` | imperial gill | `bu_gill_uk` | 1.420653125×10⁻⁴ m³ (exact) |
| `fl_oz_uk` | `fluid_ounce_uk`, `fluid_ounces_uk` | imperial fluid ounce | `bu_fluid_ounce_uk` | 28.4130625×10⁻⁶ m³ |

### 4.15 Volume — US Apothecary & Dry

| Symbol  | Long forms | Name | Enum | Factor |
|---------|-----------|------|------|--------|
| `fl_dr` | `fluid_dram`, `fluid_drams` | US fluid dram | `bu_fluid_dram` | 3.6966911953125×10⁻⁶ m³ |
| `minim` | `minims` | US minim | `bu_minim` | 6.16115199218750×10⁻⁸ m³ |
| `pk`    | `peck`, `pecks` | US dry peck | `bu_peck` | 8.80976754172×10⁻³ m³ |
| `bsh`   | `bushel`, `bushels` | US bushel | `bu_bushel` | 3.523907016688×10⁻² m³ |

> `minim` not `min` (which is the minute).

### 4.16 Area

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `ac`   | `acre`, `acres` | acre | `bu_acre` | 4046.8564224 m² (exact) |
| `barn` | `barns` | barn | `bu_barn` | 10⁻²⁸ m² (exact) |

### 4.17 Angle

| Symbol   | Long forms | Name | Enum | Factor |
|----------|-----------|------|------|--------|
| `arcmin` | `arcminute`, `arcminutes` | arcminute | `bu_arcminute` | π/10800 rad |
| `arcsec` | `arcsecond`, `arcseconds` | arcsecond | `bu_arcsecond` | π/648000 rad |
| `grad`   | `gradian`, `gradians`, `gon` | gradian | `bu_grad` | π/200 rad |
| `rev`    | `turn`, `revolution`, `revolutions`, `turns` | revolution | `bu_revolution` | 2π rad |

### 4.18 Digital

| Symbol | Long forms | Name | Enum |
|--------|-----------|------|------|
| `b`    | `bit`, `bits` | bit | `bu_bit` |
| `B`    | `byte`, `bytes`, `Byte`, `Bytes` | byte | `bu_byte` |

### 4.19 CGS Units

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

### 4.20 Radiation

| Symbol | Long forms | Name | Enum | SI equivalent |
|--------|-----------|------|------|---------------|
| `Ci`   | `curie`, `curies` | curie (radioactivity) | `bu_curie` | 3.7×10¹⁰ Bq |
| `R`    | `roentgen`, `roentgens` | röntgen (radiation exposure) | `bu_roentgen` | 2.58×10⁻⁴ C/kg |
| `rem`  | `rems` | rem (dose equivalent) | `bu_rem` | 10⁻² Sv |

### 4.21 Logarithmic

| Symbol | Long forms | Name | Enum | Notes |
|--------|-----------|------|------|-------|
| `Np`   | `neper`, `nepers` | neper | `bu_neper` | dimensionless; 1 Np = 20/ln(10) dB ≈ 8.686 dB |
| `dB`   | `decibel`, `decibels` | decibel | `bu_decibel` | dimensionless |

### 4.22 Electrical Power

| Symbol | Long forms | Name | Enum | Notes |
|--------|-----------|------|------|-------|
| `var`  | `vars` | var (volt-ampere reactive) | `bu_var` | reactive power; same SI dim as W |
| `VA`   | `volt_ampere`, `volt_amperes` | volt-ampere | `bu_volt_ampere` | apparent power; same SI dim as W |

> `W`, `var`, and `VA` all have SI dimension kg·m²·s⁻³. `bvn_units_compatible` returns `true` across them; use `.components[0].base` to distinguish.

### 4.23 Textile Linear Density

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `tex`  | — | tex | `bu_tex` | 1×10⁻⁶ kg/m (ISO 1144) |
| `den`  | `denier`, `deniers` | denier | `bu_denier` | 1/9 000 000 kg/m |

> 9 den = 1 tex.

### 4.24 Old German Units

No Old German unit accepts any SI or IEC prefix (`bvn_prefix_unit_valid` rejects all non-`si_none` prefixes for `bu_pfund` … `bu_scheffel`). Enum values 348–360.

#### Mass (metric-compatible)

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `Pfd`  | `pfund`, `pfunds` | Pfund | `bu_pfund` | 0.5 kg (exact) |
| `Ztr`  | `zentner` | Zentner | `bu_zentner` | 50 kg (exact) |
| `dz`   | `doppelzentner` | Doppelzentner | `bu_doppelzentner` | 100 kg (exact) |
| `lot`  | `lots` | Lot | `bu_lot` | 15.625×10⁻³ kg (exact) |

#### Length (historical Prussian)

| Symbol    | Long forms | Name | Enum | Factor |
|-----------|-----------|------|------|--------|
| `prln`    | `prussian_line`, `linie` | Prussian line | `bu_prussian_line` | 2.17953×10⁻³ m |
| `prz`     | `prussian_zoll`, `zoll` | Prussian Zoll | `bu_prussian_zoll` | 2.61544×10⁻² m |
| `prf`     | `prussian_fuss`, `preussischer_fuss` | Prussian Fuß | `bu_prussian_fuss` | 3.13853×10⁻¹ m |
| `elle`    | `prussian_elle`, `preussische_elle` | Prussian Elle | `bu_prussian_elle` | 6.67160×10⁻¹ m |
| `rute`    | `prussian_rute`, `preussische_rute` | Prussian Rute | `bu_prussian_rute` | 3.76624 m |
| `klafter` | `prussian_klafter` | Klafter | `bu_klafter` | 1.88312 m |
| `dt_mi`   | `deutsche_meile`, `german_mile` | Geographische Meile | `bu_german_mile` | 7420.44 m |

#### Area (historical Prussian)

| Symbol   | Long forms | Name | Enum | Factor |
|----------|-----------|------|------|--------|
| `morgen` | `prussian_morgen` | Morgen (Prussian) | `bu_morgen` | 2553.22 m² |

#### Volume (historical Prussian)

| Symbol   | Long forms | Name | Enum | Factor |
|----------|-----------|------|------|--------|
| `schffl` | `scheffel`, `prussian_scheffel` | Scheffel (Prussian) | `bu_scheffel` | 54.961×10⁻³ m³ |

### 4.25 Additional Physical Units (361–367)

#### Length

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `ftUS` | `survey_foot` | US survey foot | `bu_survey_foot` | 1200/3937 m ≈ 0.30480061 m |
| `lea`  | `league`, `leagues` | League (US statute, 3 mi) | `bu_league` | 4828.032 m |
| `cbl`  | `cable`, `cables` | Cable (international, ¹⁄₁₀ nmi) | `bu_cable` | 185.2 m |
| `hand` | `hands` | Hand (4 in) | `bu_hand` | 0.1016 m |

#### Mass

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `qntl` | `quintal`, `quintals` | Metric quintal | `bu_quintal` | 100 kg |
| `sc`   | `scruple`, `scruples` | Apothecary scruple (20 gr) | `bu_scruple` | 1.2959782×10⁻³ kg |

#### Signal Rate

| Symbol | Long forms | Name | Enum | SI dimension |
|--------|-----------|------|------|--------------|
| `Bd`   | `baud`, `bauds` | Baud (symbol/s) | `bu_baud` | s⁻¹ |

SI prefixes are accepted on all units in this section. IEC prefixes are rejected for all non-digital units.

### 4.26 Ratio and Proportion (372–377)

Dimensionless scaling factors: `5 %` ≡ `0.05`, `250 ppm` ≡ `0.00025`. These do **not** accept SI or IEC prefixes.

| Symbol | Long forms | Name | Enum | Factor |
|--------|-----------|------|------|--------|
| `%`   | `percent` | per cent | `bu_percent` | 10⁻² |
| `‰`   | `per_mille` | per mille | `bu_per_mille` | 10⁻³ |
| `‱`   | `per_myriad` | per myriad | `bu_per_myriad` | 10⁻⁴ |
| `pcm` | `per_cent_mille` | per cent mille | `bu_per_cent_mille` | 10⁻⁵ |
| `ppm` | — | parts per million | `bu_ppm` | 10⁻⁶ |
| `ppb` | — | parts per billion | `bu_ppb` | 10⁻⁹ |

---

## 5. Currencies

### 5.1 The Mandatory Currency Sigil

As of spec 1.0 a currency code carries a **mandatory `$` sigil**: write `$USD`,
`$BTC`, `k~$EUR`, `$USD/oz_t`. The codes listed in the tables below are the bare
ISO 4217 / crypto identifiers — prefix each with `$` when you use it in a document.
A bare code (no `$`) is **not** a currency; it is matched against the physical-unit
table and raises `error_unit_illegal` if it is not a unit. This removes every
currency/unit namespace collision (e.g. `$CUP` the Cuban Peso vs `cup` the unit).

### 5.2 ISO 4217 Fiat Currencies

166 codes: 164 occupying `value_base_unit_t` slots **134 … 297** (`BVN_CURRENCY_FIAT_FIRST … BVN_CURRENCY_FIAT_LAST`), plus `ZWG` and `XCG` appended past the unit block at slots **378 … 379** (`BVN_CURRENCY_EXT_FIRST … BVN_CURRENCY_EXT_LAST`) so that adding them shifted no existing enum value. The 134–297 codes have no named `bu_*` enumerators — they are resolved from the `$`-sigil code by `bvn_parse_currency_str` and carried as the numeric `base` value; query them with `bvn_unit_is_fiat` / `bvn_currency_info`.

> **Min** = minor unit exponent N: 1 major unit = 10^N minor units (e.g. 1 USD = 100 cents, N=2).
> Minor units are **bold** when they differ from 2. `numeric_code` is the ISO 4217 numeric identifier.

| Code | Num | Min | Name |
|------|----:|----:|------|
| `AED` |  784 |   2 | UAE Dirham |
| `AFN` |  971 |   2 | Afghan Afghani |
| `ALL` |    8 |   2 | Albanian Lek |
| `AMD` |   51 |   2 | Armenian Dram |
| `ANG` |  532 |   2 | Netherlands Antillean Guilder |
| `AOA` |  973 |   2 | Angolan Kwanza |
| `ARS` |   32 |   2 | Argentine Peso |
| `AUD` |   36 |   2 | Australian Dollar |
| `AWG` |  533 |   2 | Aruban Florin |
| `AZN` |  944 |   2 | Azerbaijani Manat |
| `BAM` |  977 |   2 | Bosnia-Herzegovina Convertible Mark |
| `BBD` |   52 |   2 | Barbados Dollar |
| `BDT` |   50 |   2 | Bangladeshi Taka |
| `BGN` |  975 |   2 | Bulgarian Lev |
| `BHD` |   48 | **3** | Bahraini Dinar |
| `BIF` |  108 | **0** | Burundian Franc |
| `BMD` |   60 |   2 | Bermudian Dollar |
| `BND` |   96 |   2 | Brunei Dollar |
| `BOB` |   68 |   2 | Boliviano |
| `BRL` |  986 |   2 | Brazilian Real |
| `BSD` |   44 |   2 | Bahamian Dollar |
| `BTN` |   64 |   2 | Bhutanese Ngultrum |
| `BWP` |   72 |   2 | Botswana Pula |
| `BYN` |  933 |   2 | Belarusian Ruble |
| `BZD` |   84 |   2 | Belize Dollar |
| `CAD` |  124 |   2 | Canadian Dollar |
| `CDF` |  976 |   2 | Congolese Franc |
| `CHF` |  756 |   2 | Swiss Franc |
| `CLF` |  990 | **4** | Unidad de Fomento |
| `CLP` |  152 | **0** | Chilean Peso |
| `CNY` |  156 |   2 | Chinese Yuan |
| `COP` |  170 |   2 | Colombian Peso |
| `CRC` |  188 |   2 | Costa Rican Colon |
| `CUP` |  192 |   2 | Cuban Peso |
| `CVE` |  132 |   2 | Cape Verdean Escudo |
| `CZK` |  203 |   2 | Czech Koruna |
| `DJF` |  262 | **0** | Djiboutian Franc |
| `DKK` |  208 |   2 | Danish Krone |
| `DOP` |  214 |   2 | Dominican Peso |
| `DZD` |   12 |   2 | Algerian Dinar |
| `EGP` |  818 |   2 | Egyptian Pound |
| `ERN` |  232 |   2 | Eritrean Nakfa |
| `ETB` |  230 |   2 | Ethiopian Birr |
| `EUR` |  978 |   2 | Euro |
| `FJD` |  242 |   2 | Fijian Dollar |
| `FKP` |  238 |   2 | Falkland Islands Pound |
| `GBP` |  826 |   2 | Pound Sterling |
| `GEL` |  981 |   2 | Georgian Lari |
| `GHS` |  936 |   2 | Ghanaian Cedi |
| `GIP` |  292 |   2 | Gibraltar Pound |
| `GMD` |  270 |   2 | Gambian Dalasi |
| `GNF` |  324 | **0** | Guinean Franc |
| `GTQ` |  320 |   2 | Guatemalan Quetzal |
| `GYD` |  328 |   2 | Guyanese Dollar |
| `HKD` |  344 |   2 | Hong Kong Dollar |
| `HNL` |  340 |   2 | Honduran Lempira |
| `HRK` |  191 |   2 | Croatian Kuna |
| `HTG` |  332 |   2 | Haitian Gourde |
| `HUF` |  348 |   2 | Hungarian Forint |
| `IDR` |  360 |   2 | Indonesian Rupiah |
| `ILS` |  376 |   2 | Israeli New Shekel |
| `INR` |  356 |   2 | Indian Rupee |
| `IQD` |  368 | **3** | Iraqi Dinar |
| `IRR` |  364 |   2 | Iranian Rial |
| `ISK` |  352 | **0** | Icelandic Krona |
| `JMD` |  388 |   2 | Jamaican Dollar |
| `JOD` |  400 | **3** | Jordanian Dinar |
| `JPY` |  392 | **0** | Japanese Yen |
| `KES` |  404 |   2 | Kenyan Shilling |
| `KGS` |  417 |   2 | Kyrgyzstani Som |
| `KHR` |  116 |   2 | Cambodian Riel |
| `KMF` |  174 | **0** | Comorian Franc |
| `KPW` |  408 |   2 | North Korean Won |
| `KRW` |  410 | **0** | South Korean Won |
| `KWD` |  414 | **3** | Kuwaiti Dinar |
| `KYD` |  136 |   2 | Cayman Islands Dollar |
| `KZT` |  398 |   2 | Kazakhstani Tenge |
| `LAK` |  418 |   2 | Laotian Kip |
| `LBP` |  422 |   2 | Lebanese Pound |
| `LKR` |  144 |   2 | Sri Lankan Rupee |
| `LRD` |  430 |   2 | Liberian Dollar |
| `LSL` |  426 |   2 | Lesotho Loti |
| `LYD` |  434 | **3** | Libyan Dinar |
| `MAD` |  504 |   2 | Moroccan Dirham |
| `MDL` |  498 |   2 | Moldovan Leu |
| `MGA` |  969 |   2 | Malagasy Ariary |
| `MKD` |  807 |   2 | Macedonian Denar |
| `MMK` |  104 |   2 | Myanmar Kyat |
| `MNT` |  496 |   2 | Mongolian Togrog |
| `MOP` |  446 |   2 | Macanese Pataca |
| `MRU` |  929 |   2 | Mauritanian Ouguiya |
| `MUR` |  480 |   2 | Mauritian Rupee |
| `MVR` |  462 |   2 | Maldivian Rufiyaa |
| `MWK` |  454 |   2 | Malawian Kwacha |
| `MXN` |  484 |   2 | Mexican Peso |
| `MYR` |  458 |   2 | Malaysian Ringgit |
| `MZN` |  943 |   2 | Mozambican Metical |
| `NAD` |  516 |   2 | Namibian Dollar |
| `NGN` |  566 |   2 | Nigerian Naira |
| `NIO` |  558 |   2 | Nicaraguan Cordoba |
| `NOK` |  578 |   2 | Norwegian Krone |
| `NPR` |  524 |   2 | Nepalese Rupee |
| `NZD` |  554 |   2 | New Zealand Dollar |
| `OMR` |  512 | **3** | Omani Rial |
| `PAB` |  590 |   2 | Panamanian Balboa |
| `PEN` |  604 |   2 | Peruvian Sol |
| `PGK` |  598 |   2 | Papua New Guinean Kina |
| `PHP` |  608 |   2 | Philippine Peso |
| `PKR` |  586 |   2 | Pakistani Rupee |
| `PLN` |  985 |   2 | Polish Zloty |
| `PYG` |  600 | **0** | Paraguayan Guarani |
| `QAR` |  634 |   2 | Qatari Riyal |
| `RON` |  946 |   2 | Romanian Leu |
| `RSD` |  941 |   2 | Serbian Dinar |
| `RUB` |  643 |   2 | Russian Ruble |
| `RWF` |  646 | **0** | Rwandan Franc |
| `SAR` |  682 |   2 | Saudi Riyal |
| `SBD` |   90 |   2 | Solomon Islands Dollar |
| `SCR` |  690 |   2 | Seychellois Rupee |
| `SDG` |  938 |   2 | Sudanese Pound |
| `SEK` |  752 |   2 | Swedish Krona |
| `SGD` |  702 |   2 | Singapore Dollar |
| `SHP` |  654 |   2 | Saint Helena Pound |
| `SLE` |  925 |   2 | Sierra Leonean Leone |
| `SLL` |  694 |   2 | Sierra Leonean Leone (old) |
| `SOS` |  706 |   2 | Somali Shilling |
| `SSP` |  728 |   2 | South Sudanese Pound |
| `SRD` |  968 |   2 | Surinamese Dollar |
| `STN` |  930 |   2 | Sao Tome and Principe Dobra |
| `SVC` |  222 |   2 | Salvadoran Colon |
| `SYP` |  760 |   2 | Syrian Pound |
| `SZL` |  748 |   2 | Swazi Lilangeni |
| `THB` |  764 |   2 | Thai Baht |
| `TJS` |  972 |   2 | Tajikistani Somoni |
| `TMT` |  934 |   2 | Turkmenistan Manat |
| `TND` |  788 | **3** | Tunisian Dinar |
| `TOP` |  776 |   2 | Tongan Pa'anga |
| `TRY` |  949 |   2 | Turkish Lira |
| `TTD` |  780 |   2 | Trinidad and Tobago Dollar |
| `TWD` |  901 |   2 | New Taiwan Dollar |
| `TZS` |  834 |   2 | Tanzanian Shilling |
| `UAH` |  980 |   2 | Ukrainian Hryvnia |
| `UGX` |  800 | **0** | Ugandan Shilling |
| `USD` |  840 |   2 | US Dollar |
| `UYU` |  858 |   2 | Uruguayan Peso |
| `UZS` |  860 |   2 | Uzbekistani Som |
| `VES` |  928 |   2 | Venezuelan Bolivar Soberano |
| `VND` |  704 | **0** | Vietnamese Dong |
| `VUV` |  548 | **0** | Vanuatu Vatu |
| `WST` |  882 |   2 | Samoan Tala |
| `XAF` |  950 | **0** | CFA Franc BEAC |
| `XAG` |  961 | **0** | Silver |
| `XAU` |  959 | **0** | Gold |
| `XCD` |  951 |   2 | East Caribbean Dollar |
| `XCG` |  532 |   2 | Caribbean Guilder |
| `XDR` |  960 | **0** | Special Drawing Rights |
| `XOF` |  952 | **0** | CFA Franc BCEAO |
| `XPD` |  964 | **0** | Palladium |
| `XPF` |  953 | **0** | CFP Franc |
| `XPT` |  962 | **0** | Platinum |
| `XTS` |  963 | **0** | Test (ISO 4217 reserved) |
| `YER` |  886 |   2 | Yemeni Rial |
| `ZAR` |  710 |   2 | South African Rand |
| `ZMW` |  967 |   2 | Zambian Kwacha |
| `ZWG` |  924 |   2 | Zimbabwe Gold |
| `ZWL` |  932 |   2 | Zimbabwean Dollar |

> `CLF` is the only currency with 4 minor units.
> `XTS` is the ISO 4217 testing code; present in the table but should not appear in production data.

### 5.3 Cryptocurrencies

50 codes occupying `value_base_unit_t` slots **298 … 347** (`BVN_CURRENCY_CRYPTO_FIRST … BVN_CURRENCY_CRYPTO_LAST`). Like the fiat codes they have no named `bu_*` enumerators — resolved by `bvn_parse_currency_str`, queried with `bvn_unit_is_crypto` / `bvn_currency_info`. `numeric_code = 0` for all.

> **Min** = `minor_unit` = on-chain decimal places. E.g. `<uint:64,$BTC>` stores satoshis; divide by 10⁸ to obtain BTC.

| Code   | Min | Subunit | Name |
|--------|----:|---------|------|
| `BTC`  |   8 | satoshi | Bitcoin |
| `ETH`  |  18 | wei | Ethereum |
| `SOL`  |   9 | lamport | Solana |
| `XRP`  |   6 | drop | XRP |
| `BNB`  |  18 | — | BNB |
| `ADA`  |   6 | lovelace | Cardano |
| `LTC`  |   8 | — | Litecoin |
| `DOT`  |  10 | planck | Polkadot |
| `XMR`  |  12 | piconero | Monero |
| `ETC`  |  18 | — | Ethereum Classic |
| `BCH`  |   8 | — | Bitcoin Cash |
| `XLM`  |   7 | stroop | Stellar |
| `FIL`  |  18 | — | Filecoin |
| `ICP`  |   8 | — | Internet Computer |
| `TRX`  |   6 | — | TRON |
| `EOS`  |   4 | — | EOS |
| `VET`  |  18 | — | VeChain |
| `NEO`  |   0 | — | Neo |
| `ZEC`  |   8 | — | Zcash |
| `UNI`  |  18 | — | Uniswap |
| `ARB`  |  18 | — | Arbitrum |
| `SUI`  |   9 | — | Sui |
| `TON`  |   9 | — | Toncoin |
| `INJ`  |  18 | — | Injective |
| `SEI`  |   6 | — | Sei |
| `APT`  |   8 | — | Aptos |
| `TAO`  |   9 | — | Bittensor |
| `WIF`  |   6 | — | dogwifhat |
| `DOGE` |   8 | koinu | Dogecoin |
| `LINK` |  18 | — | Chainlink |
| `USDT` |   6 | — | Tether |
| `USDC` |   6 | — | USD Coin |
| `AVAX` |  18 | — | Avalanche |
| `ATOM` |   6 | — | Cosmos |
| `POL`  |  18 | — | Polygon |
| `NEAR` |  24 | — | NEAR Protocol |
| `ALGO` |   6 | — | Algorand |
| `HBAR` |   8 | — | Hedera |
| `AAVE` |  18 | — | Aave |
| `MKR`  |  18 | — | Maker |
| `DAI`  |  18 | — | Dai |
| `STX`  |   6 | — | Stacks |
| `GRT`  |  18 | — | The Graph |
| `LDO`  |  18 | — | Lido DAO |
| `BONK` |   5 | — | Bonk |
| `PEPE` |  18 | — | Pepe |
| `SHIB` |  18 | — | Shiba Inu |
| `JUP`  |   6 | — | Jupiter |
| `PYTH` |   6 | — | Pyth Network |
| `RUNE` |   8 | — | THORChain |

### 5.4 Currency Prefix Rules

All 24 SI prefixes are allowed on all currency units. IEC binary prefixes are forbidden on all currencies (`error_unit_illegal`).

| Example | Meaning |
|---------|---------|
| `k~USD` | thousands of USD (×10³) |
| `M~EUR` | millions of EUR (×10⁶) |
| `G~ETH` | giga-ETH = Gwei scale (×10⁹) |
| `m~USD` | milli-USD = one tenth of a cent (×10⁻³) |

---

## 6. Symbol Disambiguation

As of spec 1.0 a currency is written **only** with the `$` sigil (§5.1), so the bare form is always a physical-unit lookup and the namespaces never collide:

| Token | Bare form (no `$`) | `$`-sigil form |
|-------|--------------------|----------------|
| `cup` / `CUP` | `cup` → US cup (`bu_cup`); `CUP` → `error_unit_illegal` | `$CUP` → Cuban Peso (ISO 4217:192) |
| `BTU`  | `BTU` → BTU (`bu_btu`); `Btu`, `btu` also accepted | *(not ISO 4217)* |
| `SOL`  | `SOL` → `error_unit_illegal` | `$SOL` → Solana (crypto) |
| `BAR`  | `BAR` → `error_unit_illegal`; use lowercase `bar` | *(not ISO 4217)* |
| `ERG`  | `ERG` → `error_unit_illegal`; use lowercase `erg` | *(not ISO 4217)* |
| `CAD`, `XAU` | `error_unit_illegal` (no physical unit) | `$CAD` → Canadian Dollar; `$XAU` → Gold (X-code) |

No bare token is simultaneously a valid physical unit and a currency: currencies live entirely under `$`, physical units entirely without it.

---

*Physical unit enum range: 1–133, 348–367, 368–371, and 372–377 (163 total) · Fiat: 134–297 and 378–379 (166) · Crypto: 298–347 (50)*
*`BVN_VALUE_BASE_UNIT_COUNT` = 380 (`bu_xcg + 1`)*
