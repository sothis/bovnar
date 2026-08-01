# Bovnar — Temperature Difference (Design Note)

> **Spec version:** 1.2 (native registry; no grammar change)
> **Status:** **Built.** The six Δ units and the temperature-interval quantity kind are in the
> registry; the published behaviour is in doc/05 §3.4 and doc/04 §4.6.
> **Scope:** Why a temperature *difference* is its own unit, which mechanisms were rejected, and
> what closing the gap does and does not fix.

`doc/11_bovnar_unit_profiles.md` §10.3 called this "the format's most concrete gap, and it is worth
more than this whole profile". This note is the design record for closing it: the three candidate
mechanisms, the one chosen, the single judgement call it turned on, and the failure modes that
remain open.

It was written as a proposal before any code and is kept as the argument behind the change, because
the change adds base units and a quantity kind to the **native** registry — the one part of the unit
system a document cannot opt out of.

---

## Table of Contents

1. [The gap](#1-the-gap)
2. [Why the profiles cannot close it](#2-why-the-profiles-cannot-close-it)
3. [Three mechanisms](#3-three-mechanisms)
    - 3.1 [A type-annotation parameter](#31-a-type-annotation-parameter)
    - 3.2 [A general `Δ` operator in the unit grammar](#32-a-general-δ-operator-in-the-unit-grammar)
    - 3.3 [Delta units, in their own quantity kind](#33-delta-units-in-their-own-quantity-kind)
4. [Recommendation: delta units, in their own quantity kind](#4-recommendation-delta-units-in-their-own-quantity-kind)
    - 4.1 [What the rules then produce, for free](#41-what-the-rules-then-produce-for-free)
    - 4.2 [The one judgement call](#42-the-one-judgement-call)
5. [The registry rows](#5-the-registry-rows)
    - 5.1 [Spelling, and the ambiguity check `doc/07` will want](#51-spelling-and-the-ambiguity-check-doc07-will-want)
6. [What each code site has to do](#6-what-each-code-site-has-to-do)
7. [Interoperability](#7-interoperability)
8. [What this does not close](#8-what-this-does-not-close)
9. [Cost, and how to stage it](#9-cost-and-how-to-stage-it)

- [See also](#see-also)

---

## 1. The gap

`°C` is a **scale**: a point on it is 273.15 K above the same number of kelvin, and Bovnar converts
it that way, correctly.

```
.t  = <float:64,°C> 25.0;      #  --si -->  298.15 K     right: a temperature
.dt = <float:64,°C> 25.0;      #  --si -->  298.15 K     WRONG: a rise of 25 degrees
.dt = <float:64,Δ°C> 25.0;     #  --si -->   25 ΔK       what the second line now says
```

Before the third line existed there was no spelling for a difference. A temperature difference of
25 degrees is 25 K, not 298.15 K, and the format had no way to say so — so a producer wrote `°C` and
a consumer converted it as a scale reading. The number that came out was wrong by 273.15, and
nothing in the pipeline could tell, because the two documents were byte-identical.

**This is not a corner.** A temperature *rise* across a heat exchanger, a *hysteresis* band, a
*setpoint deviation*, a *lapse rate* (K/km), a *coefficient of thermal expansion* (1/K) and a
*temperature ripple* are among the commonest quantities in the industrial telemetry and process
control that the README names as target domains. Every one of them is a difference.

**One thing already works, and it is worth being precise about why.** A compound unit does *not*
have this problem:

```
.u_value = <float:64,W/(m²·K)> 0.28;      # correct today
```

An affine scale has an SI meaning only alone and at exponent 1 —
`bvn_unit_to_si_factor` sets `*ok = false` for an affine component anywhere else — so `°C` can never
appear inside a compound at all, and the `K` in `W/(m²·K)` is unambiguously an interval because a
ratio scale is all a compound can hold. The gap is exactly and only the **bare scalar**: one
component, exponent 1, an affine scale or a kelvin that means an interval.

That is a much smaller hole than it first looks, and it is why the mechanism below can be narrow.

---

## 2. Why the profiles cannot close it

UCUM has the same gap by the same choice: `Cel` is the scale. CF 1.12 closed it *outside* the unit
string, with a variable attribute:

```
temperature_rise:units = "K" ;
temperature_rise:units_metadata = "temperature: difference" ;
```

That is the right shape for CF and the wrong shape to import. `udunits:K` would have to mean
"interval" or "scale" depending on a sibling attribute that is not in the unit slot and that the
profile parser never sees — so a profile could only ever guess. The rule this note follows is
[Unit Profiles §10.3](11_bovnar_unit_profiles.md#103-deliberately-not-attempted): **a delta scale is a native registry change, and importing it through a foreign notation
would put the fix somewhere no native document could reach.**

The consequence for interop is in §7: a CF reader that wants to preserve the distinction needs a
converter that reads `units_metadata`, not a profile that reads a unit string.

---

## 3. Three mechanisms

### 3.1 A type-annotation parameter

<!-- bovnar-example: rejected -->
```bovnar
.dt = <float:64,K,delta> 25.0;        # a new parameter class
```

**Rejected.** It puts a temperature-specific word in the type system, where nothing else about
physical quantities lives; it needs a grammar change and a new parameter class in
`bvn_parse_type_annotation`; and it does not compose — `<float:64,K/km,delta>` would have to say
*which* K the marker applies to. The unit is where a unit distinction belongs.

### 3.2 A general `Δ` operator in the unit grammar

<!-- bovnar-example: rejected -->
```bovnar
.dt = <float:64,Δ(°C)> 25.0;          # "difference of" as a unit-expression operator
```

**Rejected, but for a reason worth recording.** It is the most general answer and generality is
the problem: `Δ(m)` and `Δ(Pa)` are grammatical and meaningless, because on a **ratio** scale a
difference has the same unit as the quantity — only an *affine* scale distinguishes them. So the
operator would be a no-op on 186 of the registry's 262 units, and a no-op that parses is a thing
producers will write and readers will have to strip. It also doubles the notional unit space for
equality and normalisation (`Δ(m)` must compare equal to `m`, which is a special case in
`bvn_unit_equal`, the formatter and the reverse profile tables).

### 3.3 Delta units, in their own quantity kind

```bovnar
.dt = <float:64,ΔK> 25.0;             # a base unit of its own
```

**Chosen.** It reuses two mechanisms the unit system already has and needs neither a grammar
change nor a new concept:

* **quantity kinds** (`bvni_kind_table`) already make `bvn_units_compatible` refuse two units that
  share a dimension and mean different things — it is what keeps `M~b/s` out of `M~B/s`, `NTU` out
  of `FNU` and `lm` out of `cd`. A temperature interval and a temperature scale reading are exactly
  that relationship: same dimension Θ, different quantity;
* **the registry** already carries negative and non-terminating exact factors (Delisle is
  −2/3, Rankine 5/9), so no new arithmetic is needed for any delta slope.

---

## 4. Recommendation: delta units, in their own quantity kind

**One quantity kind and six base units**, as built.

```
BVNI_KIND_TEMP_INTERVAL        one new kind, weight 1, exponent 1
```

| Unit | Symbol | Exactly | Kind |
|---|---|---|---|
| delta kelvin | `ΔK` | 1 K | `TEMP_INTERVAL` |
| delta Celsius | `Δ°C` | **the same unit as `ΔK`** — an alias, not a row | — |
| delta Fahrenheit | `Δ°F` | 5/9 K | `TEMP_INTERVAL` |
| delta Rankine | `Δ°Ra` | **the same unit as `Δ°F`** — an alias | — |
| delta Delisle | `Δ°De` | −2/3 K | `TEMP_INTERVAL` |
| delta Newton | `Δ°N` | 100/33 K | `TEMP_INTERVAL` |
| delta Réaumur | `Δ°Re` | 5/4 K | `TEMP_INTERVAL` |
| delta Rømer | `Δ°Ro` | 40/21 K | `TEMP_INTERVAL` |

So six new `value_base_unit_t` ids, not eight: **`Δ°C` is an alias of `ΔK`** and **`Δ°Ra` is an
alias of `Δ°F`**, because the degree Celsius interval *is* the kelvin by SI definition and the
Rankine degree *is* the Fahrenheit degree. Spelling them as separate rows would create two units
that must compare equal and convert by exactly 1 — a distinction with no content, and one more
pair for `check_profile_factors.py` and the cross-vocabulary suite to keep in step.

Every delta row has `.affine = false` and `.offset = 0.0`. That is the point of them: **a delta
unit is a ratio scale**, so it composes, it exponentiates, and it needs none of the affine
special-casing the scale units need.

### 4.1 What the rules then produce, for free

| | |
|---|---|
| `ΔK` → `Δ°C` | factor 1 — the same unit |
| `Δ°C` → `Δ°F` | 9/5 exactly. The lossless path succeeds when the value is a multiple of 5 and reports `error_unit_inexact` otherwise, which is the per-value exactness the system already has |
| `ΔK` → `K` | **`error_unit_mismatch`.** Different kinds. This is the whole feature |
| `°C` → `Δ°C` | `error_unit_mismatch`. Also the feature: converting a reading into an interval is a decision only the author can make |
| `Δ°C` → `Δ°De` | −3/2 exactly. Negative, and already representable |
| `--si` on `25.0 Δ°C` | `25 ΔK` — exact, and it does not become 298.15 |
| `m~ΔK` | works; delta units take prefixes like any other |
| `ΔK/k~m` (lapse rate), `ΔK^-1` (expansion coefficient) | work — and are **the same unit as** `K/k~m` and `K^-1`, which §4.2 explains and which is what keeps every existing U-value and lapse rate meaning what it meant |
| `Δ°F/mi`, `Δ°Re^-1` | work, and have no equivalent in the scales at all: `°F/mi` and `°Re^-1` have no SI meaning, because an affine scale is meaningful only alone at exponent 1 |
| `[<float:64,K> 1.0, <float:64,ΔK> 2.0]` | `error_array_element_type_mismatch` — the array rule of spec §7.4 gets this without being told |

The last row is worth noticing: closing this gap composes with the array-homogeneity rule rather
than needing a clause in it.

### 4.2 The one judgement call

`W/(m²·K)` was correct before any of this (§1) and had to stay correct — `K` in a compound is
already an interval, because that is all a compound can hold. Should it also be *spellable* as
`W/(m²·ΔK)`, and if so, are the two the same unit?

**They are the same unit.** The rule, as built: **the temperature-interval kind counts only for a
lone unit at exponent 1.** So

```
ΔK        vs  K          incompatible    the hazard, and the whole point
Δ°F       vs  °F         incompatible    likewise
ΔK/k~m    vs  K/k~m      the SAME unit   a lapse rate
ΔK^-1     vs  K^-1       the SAME unit   an expansion coefficient
W/(m²·ΔK) vs  W/(m²·K)   the SAME unit   a U-value
```

The scope follows the hazard exactly. `bvn_unit_to_si_factor` applies an affine offset for a single
component at exponent 1 and refuses it everywhere else, so a lone bare temperature is the only place
a difference could ever have been read as a scale. Counting the kind in a compound would separate two
spellings of one quantity and break every U-value and lapse rate written to date, buying nothing.

The alternative — an unconditional kind, so `W/(m²·ΔK)` ≠ `W/(m²·K)` — is more uniform in the code
and worse in the field. It was rejected. This asymmetry is the only rule in the design that was not
already somewhere in the tree, and `bvni_kind_exponents` carries the explanation beside it.

---

## 5. The registry rows

`src/gendata/units.bvnr`, in the native block (ids continue at `100180`). Sketch, in the file's own
shape:

<!-- bovnar-example: illustrative -->
```bovnar
  {
    .id      = <uint:32> 100180;
    .name    = "delta_kelvin";
    .symbol  = "ΔK";
    .factor  = 1.0;
    .dims    = { .temperature = 1; };
    .affine  = false;              # a difference is a RATIO scale
    .offset  = 0.0;
    .prefix  = default;            # m~ΔK is a millikelvin interval
    # The degree Celsius interval IS the kelvin (SI Brochure, 9th ed. §2.3.1),
    # so Δ°C is an alias here rather than a row of its own.
    .aliases = ["delta_kelvin", "deltaK", "delta_K", "ΔK",
                "delta_celsius", "deltaC", "delta_degC", "Δ°C"];
  },
  {
    .id      = <uint:32> 100181;
    .name    = "delta_fahrenheit";
    .symbol  = "Δ°F";
    .factor  = 0.5555555555555556;
    .dims    = { .temperature = 1; };
    .affine  = false;
    .offset  = 0.0;
    # Exact rational: 1 Δ°F = 5/9 K. The Rankine degree is the same interval.
    .factor_num = <sint:64> 5;
    .factor_den = <sint:64> 9;
    .prefix  = default;
    .aliases = ["delta_fahrenheit", "deltaF", "delta_degF", "Δ°F",
                "delta_rankine", "deltaRa", "delta_degRa", "Δ°Ra"];
  },
```

…and four more for Delisle, Newton, Réaumur and Rømer, each mirroring its scale row's
`factor_num`/`factor_den` with `.affine = false` and `.offset = 0.0`. Ids `100180`–`100185`; the
native block's last unit is now `bu_delta_romer`.

**Include the historic four, do not stop at kelvin and Fahrenheit.** The argument is the one doc/11
§17.4 already made for the CF names and settled the same way: a unit that is absent is
indistinguishable, to a producer, from one bovnar has never heard of. The four cost six lines each,
their exact factors are already in the file beside their scale rows, and their negative-slope case
(Delisle) needs no new arithmetic. A registry that carries `°De` for reading an eighteenth-century
record and refuses `Δ°De` has an asymmetry that would have to be documented and defended; carrying
both has none.

### 5.1 Spelling, and the ambiguity check `doc/07` will want

* `Δ` is U+0394, `0xCE 0x94` in UTF-8 — inside the `BVN_UTF8_LEADER` class the unit token already
  accepts, so no lexer change.
* Every symbol has ASCII aliases (`deltaK`, `delta_K`, `delta_degF`), because a unit only reachable
  through a character that is awkward to type is a unit producers will avoid.
* **Compact-prefix safety.** `deltaK` must resolve as the bare alias and not as `d~eltaK`; it does,
  under the existing longest-alias-suffix rule with bare-alias precedence, because `eltaK` is not a
  unit. `ΔK` cannot be read as a prefix at all — `Δ` is not one. No new entry in the compact-prefix
  exception table is needed, and `doc/07` should say so explicitly rather than leave a reader to
  work it out.
* `dK` is **not** proposed as an alias for anything. It reads as decikelvin, and that is what it
  should keep meaning.

---

## 6. What each code site has to do

| Site | Change |
|---|---|
| `src/gendata/units.bvnr` | six rows, ids `100180`–`100185` |
| `bvni_kind_table` (`bovnar_si_units.c`) | six entries at the new kind, weight 1 |
| `BVNI_KIND_COUNT` | 12 → 13 |
| `bvn_internal_dims.h` | the dense-slot assertions move with the unit count, as for any new native unit |
| `bvni_kind_exponents` | **the one real change**: the temperature-interval kind counts only for a lone unit at exponent 1, so `W/(m²·ΔK)` equals `W/(m²·K)` (§4.2) |
| `bvn_unit_si_normal_form` | a delta unit normalises to `ΔK`, never to `K` |
| `qudt-qk.bvnr` | **one row corrected.** `TemperatureDifference` mapped to `K`, which made it the same unit as `ThermodynamicTemperature` — the confusion the code's own name rules out. It maps to `ΔK` and, since no other quantity kind claims `ΔK`, it is the one row in that file's tail that reverses |
| the other profiles | nothing. No unit *string* in any vocabulary is a delta, so no row gains a target and `check_profile_factors.py` is unaffected — a quantity KIND is the exception because it states a quantity and no unit, so there is no published unit string to diverge from |
| `doc/05`, `doc/06`, `doc/07` | the registry table, the policy chapter, the ambiguity note above |
| conformance corpus | 13 cases in the `units` group (UNT-065…UNT-077): every spelling, prefixed and inline forms, the compounds, the array case, and the two aliases that deliberately do not exist |
| `bovnar_si_units_test.c` | `test_temperature_difference_is_its_own_quantity_kind` — the refusals, `Δ°C` ≡ `ΔK`, `Δ°Ra` ≡ `Δ°F`, the exact slopes including Delisle's negative one, the SI normal form, and the compound equality at three shapes |

**No grammar change, no new error code, no ABI break.** New base-unit ids append inside the native
block, which is what that block's spare space is for; `error_unit_mismatch` already says what a
delta-to-scale conversion is.

---

## 7. Interoperability

| Vocabulary | What it has | What a bovnar reader should do |
|---|---|---|
| **CF ≥ 1.12** | `units = "K"` plus `units_metadata = "temperature: difference"` | A converter reads the attribute and emits `ΔK`. **Not** the `udunits:`/`cf:` profiles — the information is not in the unit string, so a profile could only guess (§2) |
| **CF standard names** | four are differences by name (`air_temperature_anomaly`, `brightness_temperature_anomaly`, `sea_water_temperature_anomaly`, `surface_temperature_anomaly`), and CF states `canonical_units = "K"` for each | **Left as `K`.** Overriding a publisher's *stated* unit from the sense of its name is a different decision from reading a quantity kind, and it would put `cf:air_temperature_anomaly` in disagreement with `udunits:K` for the same variable. The place to make that call is the converter above, which sees both halves |
| **CF < 1.12** | `units = "K"`, nothing else | Indistinguishable. A converter must ask the caller, and should refuse rather than guess |
| **UCUM** | `Cel` is the scale; no delta atom | `ucum:Cel` stays the scale. A UCUM writer given `ΔK` has no code for it: `bvn_unit_to_profile("ucum", …)` returns −1, which is the existing answer for a native unit outside the transliteration table |
| **QUDT** | has `DEG_C` and quantity kinds including `TemperatureDifference` | **Corrected in the same change.** `qudt-qk:TemperatureDifference` mapped to `K`, making it indistinguishable from `qudt-qk:ThermodynamicTemperature`; it maps to `ΔK`. A quantity kind states a quantity and no unit, so unlike the CF row above there is no published unit string being overridden — `ΔK` is simply the faithful reading of the name |
| **UN/ECE Rec 20** | `KEL`, `CEL`, `FAH` are scales | unchanged |

QUDT was the only vocabulary that already modelled the distinction, and it is what exposed the one
row this change had to correct rather than add.

---

## 8. What this does not close

**A producer who keeps writing `°C` for a difference.** This is the honest limit. The proposal
gives that producer a right answer; it cannot make them use it, and a reader still cannot tell a
correctly-written scale reading from an incorrectly-written difference. What it changes is that the
mistake becomes *expressible as a defect* — a consumer can write

```
bovnar validate --require-field '.exchanger.dt=ΔK' plant.bvnr
```

and get a refusal instead of a plausible number. That is the same shape as every other unit
assertion in the format, and it is the strongest thing a format can do about a producer who writes
the wrong unit correctly.

**Existing documents.** Every document that means a difference and says `°C` keeps parsing and
keeps converting the way it does now. There is no migration and no detection; the gap closes for
new documents only. An alternative — making a bare affine scale require an explicit
scale-or-difference choice — would be a breaking change to every temperature document in existence
for the benefit of the subset that is wrong, and it is not recommended.

**Arithmetic.** Bovnar does not compute, so `25 °C − 20 °C = 5 ΔK` is not something the library
will do. It will refuse to *convert* between the two, which is the part that belongs to a format.

**The other affine scales in the format.** A `datetime` epoch is affine in exactly the same way — a
duration is not a timestamp — and it is handled by an entirely different mechanism (the epoch is a
type parameter, and `datetime` is its own kind for array homogeneity). This proposal does not
unify the two, and probably nothing should: a duration already has a unit (`s`) that is not a
timestamp, so the gap this note is about does not exist there.

---

## 9. Cost, and how to stage it

| | |
|---|---|
| Registry | six rows, ~40 lines of `units.bvnr` |
| Code | one kind, six `bvni_kind_table` entries, one exponent rule in `bvni_kind_exponents`, one line in `bvn_unit_si_normal_form` |
| Binary | six dense-table rows and their symbol strings — under 1 KB |
| Tests | one conformance block, one `bovnar_si_units_test.c` block, one QUDT row if §7 is taken |
| Risk | concentrated in one place: the lone-scalar rule of §4.2. Everything else is a registry row of a kind the file had 180 examples of |

**Landed as one change, not two.** A half-landed version — delta units without the compound equality
rule, or with the kind but not the SI normal form — leaves two spellings of the U-value that do not
compare equal, which is a worse state than the gap was. The pieces are small and they are only
correct together.

What actually moved, beyond the registry: the native block's last id (`bu_delta_romer` = 100185),
the UCUM round-trip sweep's "no code" count (1178 → 1250, since six units gained no UCUM spelling),
the FAQ's base-unit count (180 → 186), and one `qudt-qk` row that was wrong before these units
existed and could not have been right without them.

---

## See also

- [Unit System](05_bovnar_unit_system.md) — the native registry these rows join
- [Unit Policy](06_bovnar_unit_policy.md) — `--require-field`, the enforcement in §8
- [Unit Ambiguities](07_bovnar_unit_ambiguities.md) — where the spelling check of §5 belongs
- [Unit Profiles §10.3](11_bovnar_unit_profiles.md#103-deliberately-not-attempted) — the entry this note answers
- [Specification §7.4](03_bovnar_spec.md#74-element-homogeneity) — the array rule this composes with

---

*End of Bovnar — Temperature Difference (Design Note) (Bovnar spec 1.1).*
