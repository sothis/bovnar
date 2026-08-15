# Bovnar — Measurement Uncertainty (Design Note)

> **Spec version:** 1.2 (Layer 0, no grammar change) → 1.3 (Layer 1, additive grammar) → 2.0 (Layer 2)
> **Status:** **Proposal.** Nothing here is built, scheduled, or decided.
> **Scope:** How a value states how well it is known. The mechanisms considered, the one recommended,
> the two traps it has to avoid, and what it deliberately leaves open.

Every claim marked *"today"* below was taken from the reference implementation — built from this tree
and exercised — rather than from the prose. Where the two disagreed, the implementation won.

---

## Table of Contents

1. [The gap](#1-the-gap)
2. [What the grammar allows](#2-what-the-grammar-allows)
    - 2.1 [Where a second magnitude can live](#21-where-a-second-magnitude-can-live)
    - 2.2 [What is lexically free today](#22-what-is-lexically-free-today)
3. [Two traps](#3-two-traps)
    - 3.1 [The affine trap: an uncertainty is a difference](#31-the-affine-trap-an-uncertainty-is-a-difference)
    - 3.2 [The coverage trap: `± 0.5` does not mean anything yet](#32-the-coverage-trap--05-does-not-mean-anything-yet)
4. [Five mechanisms](#4-five-mechanisms)
5. [Recommendation: three layers, each useful alone](#5-recommendation-three-layers-each-useful-alone)
    - 5.1 [Layer 0 — the measurand struct (spec 1.2, no grammar change)](#51-layer-0--the-measurand-struct-spec-12-no-grammar-change)
    - 5.2 [Layer 1 — the `±` clause (spec 1.3, additive)](#52-layer-1--the--clause-spec-13-additive)
    - 5.3 [Layer 2 — fold into named parameters (2.0)](#53-layer-2--fold-into-named-parameters-20)
6. [The interval rule](#6-the-interval-rule)
7. [Grammar changes for Layer 1](#7-grammar-changes-for-layer-1)
8. [What each code site has to do](#8-what-each-code-site-has-to-do)
9. [Error codes](#9-error-codes)
10. [What this does not close](#10-what-this-does-not-close)
11. [Cost, and how to stage it](#11-cost-and-how-to-stage-it)

- [See also](#see-also)

---

## 1. The gap

Bovnar's pitch is that a number carries its unit and the parser checks it. A measured number has a
second property the format cannot express at all: **how well it is known**.

```bovnar
.length = <float:64,m> 1.5;      # 1.5 m — but ±1 mm, or ±10 cm? The document cannot say.
```

`JCGM 100:2008` (the GUM) is blunt about it: a measurement result is *incomplete* without a statement
of uncertainty. Bovnar's own README names industrial telemetry, process control and scientific data
as target domains, and in all three a bare value is not a result. The gap is not decorative — it is
the same *class* of gap as the one [temperature_difference.md](temperature_difference.md) closed,
and by the same test: a producer who knows the uncertainty has nowhere to put it, so it is dropped at
the format boundary and reconstructed downstream by guesswork or convention.

**What exists today is a naming convention and nothing else.** A producer writes `.length` and
`.length_err`, or `.length_sigma`, or `.length_tol`, and no two producers agree. The parser sees two
unrelated numbers. `--require-unit` will happily pass a document whose uncertainty is in the wrong
unit by a factor of 273.15 (§3.1).

---

## 2. What the grammar allows

### 2.1 Where a second magnitude can live

The value production is the whole constraint:

```ebnf
value            = [type-annotation , ws] , ( scalar-with-unit | raw-value ) ;
scalar-with-unit = ( number | string ) , ws-mandatory , inline-unit ;
```

A value is **one** magnitude plus optional metadata. There is no position in the grammar where a
value is two numbers. So every mechanism must answer one question: *where does the second number go,
and what binds it to the first?* There are exactly four answers the grammar can give:

| Carrier | Position | Binds by | Varies per element? |
|---|---|---|---|
| Type annotation parameter | prefix, `<…>` | adjacency | no — constant per annotation |
| Inline suffix | postfix, after `ws-mandatory` | adjacency | yes |
| A sibling key | elsewhere in the struct | **name** | yes |
| A nested struct | the value *is* the pair | containment | yes |

The first two put the uncertainty *in the value*, which is the property
[v2 §2](bovnar_v2_proposals.md) calls load-bearing ("the unit living in the value rather than in a
schema beside it"). The last two put it beside the value, which is the shape
[temperature_difference.md §2](temperature_difference.md) rejected when CF did it with
`units_metadata` — but the objection there was specifically that a *profile parser* could never see
the sibling attribute. A **native** convention is seen by the native validator, so that objection
does not transfer intact. It weakens to a milder one: binding by name is binding by agreement.

### 2.2 What is lexically free today

Three facts decide what can be added additively, and all three were checked against a build of this
tree rather than read off the state table.

**The `±` suffix is an error today.** In `number_outro` the byte class `BVN_UTF8_LEADER` — which
begins at `0xc2`, and `±` is `U+00B1` = `0xC2 0xB1` — maps to `ACT_inline_unit_intro`. So `±` opens
the inline-unit machine and the token fails to resolve as a unit:

```console
$ .a = 20.0 ± 0.5;     → error_unexpected_input_byte (line 1, col 13)
$ .a = 20.0 ±0.5;      → error_unit_illegal          (line 1, col 15)
$ .a = <float:64,±0.5> 20.0;   → error_unit_illegal  (line 1, col 20)
```

Every spelling is refused. Nothing valid can change meaning, so **`±` is available additively** in
both the suffix and the annotation-parameter position. The ASCII digraph `+-` is likewise free: `+`
(`0x2b`) has no entry in `number_outro` at all.

**`=` is not available.** The named-parameter form `<float:64,u=0.5>` is
`error_unexpected_input_byte` — `=` is not among the bytes `copy_type_byte` accepts. Named parameters
need a lexer change, which is why [v2 §7](bovnar_v2_proposals.md) files them under 2.0. Any 1.x
proposal must live inside the shape-identified parameter space.

**The ratio units are already there, already dimensionless.** `%`, `‰`, `‱`, `pcm`, `ppm`, `ppb`,
`pptr` and `ppq` are eight registry rows with `.dims = {}` and a factor (`%` → `0.01`). A *relative*
uncertainty therefore needs no new syntax whatsoever — only a rule that says what a ratio unit means
in the uncertainty slot (§6).

---

## 3. Two traps

A proposal that ships `±` and stops walks into both of these. They are the reason this note is longer
than the syntax warrants.

### 3.1 The affine trap: an uncertainty is a difference

This is the same fault [temperature_difference.md](temperature_difference.md) closed, arriving one
level up. An uncertainty is always an **interval**, never a point on a scale. On the six affine
temperature scales those are different units, and the format already knows it:

```console
$ .scale = <float:64,°C>  25.0;   --si-->  "298.15" K     a temperature
$ .diff  = <float:64,Δ°C> 25.0;   --si-->   "25.0"  ΔK    a difference
```

So a naive rule — *"the uncertainty takes the value's unit"* — is wrong by 273.15 for every
temperature in the corpus, and wrong silently, because an SI-normalising pipeline converts it as a
scale reading and produces a plausible number. `20.0 °C ± 0.5 °C` normalises to `20.0 °C ± 273.65 K`.

The registry already carries the fix: six `delta_*` rows, of which `ΔK` is the normal form (`Δ°C`
canonicalises to `ΔK`, as the trace above shows). The rule in §6 is built on them.

**And the enforcement already ships.** The unit-policy flag `--require-field <path>=<unit>` does the
dimensional check today, on the Layer-0 struct form of §5.1, with no code change at all:

```console
$ bovnar validate --require-field '.t.u=ΔK' doc.bvnr
doc.bvnr: OK                                    # .t.u was Δ°C — an interval

$ bovnar validate --require-field '.bad.u=ΔK' doc.bvnr
Validation failed: unit_mismatch at line 2, col 61   # .bad.u was °C — a scale
```

That is a stronger starting position than the temperature gap had, and it is what makes Layer 0
worth shipping on its own.

### 3.2 The coverage trap: `± 0.5` does not mean anything yet

The GUM distinguishes three things that `±0.5` is used for interchangeably in the wild:

- **standard uncertainty** `u` — one standard deviation, *k* = 1, ≈68 % coverage;
- **expanded uncertainty** `U = k·u` — conventionally *k* = 2, ≈95 %;
- a **tolerance or bound** — "guaranteed inside", not a distribution at all.

A factor of two separates the first two, and the third is a different kind of claim. Two documents
that both say `± 0.5` can mean any of them, and **nothing in the pipeline can tell** — byte-identical
documents, different meanings. That is precisely the failure shape §3.1 describes, and shipping `±`
without addressing it would re-open at the statistical layer the hole the delta units just closed at
the dimensional one.

**The design principle that follows:** make the ambiguous case *unspellable*. `±` means standard
uncertainty, *k* = 1, normatively and always; anything else must say so. The dangerous reading is
then not a default a careless producer falls into — it has no spelling at all.

---

## 4. Five mechanisms

| | Mechanism | Grammar cost | Per-element? | In the value? | Verdict |
|---|---|---|---|---|---|
| M1 | Measurand struct `{.value; .u;}` | **none** | yes | no (containment) | **adopt as Layer 0** |
| M2 | Parallel/ancillary arrays | none | yes | no (**by name**) | rejected as normative |
| M3 | Annotation parameter `<…,±0.5>` | additive | **no** | yes | adopt for the array-wide case |
| M4 | Value suffix `20.0 ± 0.5` | additive | yes | yes | **adopt as Layer 1** |
| M5 | New family `<measured:64,m>` | breaking | yes | yes | rejected |

**M1 — the measurand struct.** `{ .value = <float:64,°C> 20.0; .u = <float:64,ΔK> 0.5; }` parses
today, converts to the natural JSON shape `{"value":…,"u":…}`, and — checked against the build — a
DOM parse *enforces* that every sibling in an array of them has the same shape
(`error_struct_shape_mismatch`), so an array of measurands cannot lose its `.u` on one element. Its
costs are real: roughly three times the bytes, the flat numeric buffer is gone, and the uncertainty
is not in the value.

**M2 — parallel arrays** (`.depth` beside `.depth_u`, the netCDF `ancillary_variables` shape) keeps
the flat buffer and streams perfectly, but binds by **name**. As a *permitted* pattern it is fine and
costs nothing. As the *normative* answer it makes the format's central claim — the parser validates
it — false for the one property this note exists to add, because no parser can know that `_u` is a
suffix and not a column called `depth_u`. Rejected as normative, kept as a documented pattern.

**M3 — an annotation parameter.** `<float:64,m,±0.001>` is constant for everything the annotation
covers, which is useless per-element and exactly right for the whole-array annotation: *this
instrument reads to ±1 mm*, stated once, inherited by every element, checkable at the opening `[`.
It is not a competitor to M4 but its complement.

**M4 — the value suffix.** `20.0 ± 0.5` is the notation every physicist and every datasheet already
writes, it puts the uncertainty in the value, it varies per element, and §2.2 shows it is free.
Its one structural problem is inherited, not created: the inline-unit path is **banned inside array
elements** (`in_array_element` → `error_unexpected_input_byte`), which
[v2 §6](bovnar_v2_proposals.md) already lists as a defect to fix on its own merits.

**M5 — a dedicated family.** `<measured:64,m> 20.0 0.5` would make the value two tokens, breaking the
one-magnitude value production for every consumer, and would need a parallel family for each of the
five numeric families. All cost, no capability M4 lacks. Rejected.

---

## 5. Recommendation: three layers, each useful alone

Ship in three stages that do not invalidate one another. A document written for Layer 0 stays valid
and keeps its meaning under Layers 1 and 2; Layer 1 is a shorter spelling of the same semantics, not
a different one.

### 5.1 Layer 0 — the measurand struct (spec 1.2, no grammar change)

Make the convention **normative** and give it one name, so that tooling has something to recognise:

```bovnar
.temperature = {
  .value = <float:64,°C> 20.0;     # the measurand
  .u     = <float:64,ΔK> 0.5;      # standard uncertainty, k=1, in the INTERVAL unit
  .k     = <float:64> 2.0;         # OPTIONAL coverage factor; absent means 1
};
```

Reserved member names: `.value` (required), `.u` (required), `.k`, `.u_lower` / `.u_upper`
(asymmetric), `.dist` (`normal` | `rectangular` | `triangular`), `.dof`. Any other member is the
application's and is ignored by the convention.

This is the [temperature_difference.md](temperature_difference.md) move — solve it in the registry
and the documentation, not in the grammar — and it buys three things immediately: the interval rule
of §6 becomes stateable and enforceable *today* via `--require-field` (§3.1), the coverage factor has
a home from day one, and Layers 1 and 2 gain a normative semantics to be sugar *for*.

### 5.2 Layer 1 — the `±` clause (spec 1.3, additive)

<!-- bovnar-example: illustrative -->
```bovnar
#!bovnar 1.3
.length = <float:64,m>  1.5 ± 0.002;        # 1.5 m,  u = 2 mm      (k=1)
.temp   = <float:64,°C> 20.0 ± 0.5;         # 20 °C,  u = 0.5 ΔK    (§6: interval unit)
.mass   = <float:64,kg> 12.0 ± 0.5 %;       # relative: u = 0.06 kg
.gain   = <float:64,dB> 3.0 ±k2 0.4;        # EXPANDED, k=2  →  u = 0.2 dB
.gap    = <float:64,mm> 5.00 ±tol 0.05;     # a hard bound, not a distribution
.batch  = <float:64,m,±0.001> [1.5, 1.6];   # M3: one instrument spec, whole array
```

`±` alone is *always* the standard uncertainty. The two markers `±k<factor>` and `±tol` are a **new,
closed keyword namespace opened by the `±` sigil** — which sidesteps
[v2 §8](bovnar_v2_proposals.md) entirely, because unlike the bare-word namespace this one is closed
from birth and can be extended in 1.4 without rewriting the meaning of any document.

### 5.3 Layer 2 — fold into named parameters (2.0)

When [v2 §7](bovnar_v2_proposals.md) lands, the annotation form becomes
`<float:64, unit=m, u=0.001, k=2>` and the shape-identified `±0.001` parameter retires into it. When
[v2 §6](bovnar_v2_proposals.md) removes the array-element exclusion, `[1.5 ± 0.1, 1.6 ± 0.1]` becomes
legal with no further work. Neither is a prerequisite; both are simplifications the design should not
foreclose, and this one does not.

---

## 6. The interval rule

One rule, three clauses. It is the whole semantic content of the proposal.

> **1. Default.** An uncertainty with no unit of its own takes the unit of its value — except where
> that unit is an **affine scale**, in which case it takes that scale's **delta unit** (`°C` → `ΔK`,
> `°F` → `Δ°F`, and so for the other four). A ratio-scale unit is its own interval unit and is
> unchanged.
>
> **2. Explicit.** An uncertainty may carry its own unit. It must be **commensurable with the unit
> clause 1 would have given** — not with the value's unit. `20.0 °C ± 0.5 °C` is
> `error_uncertainty_unit_mismatch`, because `°C` is a scale and the slot takes an interval.
>
> **3. Relative.** A **ratio unit** (`%`, `‰`, `‱`, `pcm`, `ppm`, `ppb`, `pptr`, `ppq`) is accepted
> in the uncertainty slot for *any* value unit, and denotes `|value| × factor` in the unit of
> clause 1.

Clause 2 is the one that earns its keep: it makes the 273.15 error a **parse error** rather than a
plausible wrong number, and it is the only clause a careless producer will meet.

What the rule produces for free, with no further machinery:

- **Compounds are already safe.** An affine scale has an SI meaning only alone and at exponent 1, so
  `W/(m²·K)` can only ever hold an interval `K`. Compound units need no special case.
- **Relative uncertainty costs nothing.** Clause 3 is a multiplication against a registry factor that
  is already there.
- **`--require-field` already enforces it** for Layer 0 (§3.1), and needs only a path convention for
  Layer 1.
- **Dimensionless values compose.** A `%` value with a `%` uncertainty is absolute; with a `ppm`
  uncertainty it is relative. Clause 3 orders them by the slot, not by the unit.

**One judgement call, stated plainly.** Clause 3 makes a ratio unit mean *relative* even when the
value is itself dimensionless, so `<float:64,%> 2.0 ± 1 %` is 2 % ± 0.02 %, not 2 % ± 1 %. The
alternative — ratio-means-absolute when the value is a ratio — was rejected because it makes the
meaning of the uncertainty depend on the value's dimension, which is exactly the kind of
context-sensitivity clause 2 exists to remove. It should be called out in the tutorial, because it is
the one place the rule surprises.

**And one thing the rule deliberately refuses to do: infer.** `20.0` does not imply `± 0.05`. Written
precision is a *rendering* property in Bovnar — the `base` parameter is already documented that way —
and a format that silently manufactured an uncertainty from decimal places would produce a number no
producer stated. An uncertainty is present only when written.

---

## 7. Grammar changes for Layer 1

Additive against `doc/12_bovnar.ebnf`. Two productions change; the rest is new.

```ebnf
(* UNCHANGED, for contrast: a scalar carrying only an inline unit.            *)
scalar-with-unit = ( number | string ) , ws-mandatory , inline-unit ;

(* NEW: a scalar may carry an inline unit, an uncertainty, or both.           *)
scalar-with-uncert
                 = number , [ ws-mandatory , inline-unit ] ,
                   ws-mandatory , uncertainty ;

(* CHANGED: the new alternative, tried first.                                 *)
value            = [type-annotation , ws] ,
                   ( scalar-with-uncert | scalar-with-unit | raw-value ) ;

(* NEW *)
uncertainty      = pm-sigil , [ uncert-kind ] , ws-mandatory ,
                   number , [ ws-mandatory , inline-unit ] ;

pm-sigil         = "±"        (* U+00B1 = 0xC2 0xB1 *)
                 | "+-" ;     (* ASCII digraph, as "^2" is for "²"           *)

(* A CLOSED keyword set, opened by the sigil. Extensible in a later 1.x       *)
(* revision without touching the open bare-word namespace.                    *)
uncert-kind      = "k" , number      (* expanded: U = k·u; k > 0             *)
                 | "tol" ;           (* a hard bound, not a distribution      *)

(* NEW type-param class. Recognised by its first code point, which no unit    *)
(* alias in the registry begins with -- so doc/12's shape-identified          *)
(* parameter space stays unambiguous.                                         *)
uncert-param     = pm-sigil , [ uncert-kind ] , number , [ unit-param ] ;

(* CHANGED: one alternative added.                                            *)
type-param       = width-param | base-param | q-param | unit-param
                 | uncert-param ;
```

Semantic constraints, in the tier `doc/12` §13 uses: <!-- bovnar:no-section-check: doc/12 is the .ebnf, which this checker indexes no sections of -->


- **(VALIDATOR)** The uncertainty magnitude must be non-negative and finite; `nan`, `inf` and `ninf`
  are `error_uncertainty_illegal`. A negative magnitude is `error_uncertainty_illegal`, not
  `error_value_out_of_range` — the value is in range, the *claim* is malformed.
- **(VALIDATOR)** `k` must be `> 0`.
- **(VALIDATOR)** The interval rule of §6, clauses 2 and 3.
- **(VALIDATOR)** **The family is consulted.** An uncertainty is accepted for `uint`, `sint`,
  `float`, `float_fix` and `float_dec` only. On `utf8` and `bool` it is `error_illegal_value_type`,
  from the *same* code path as the annotation form. This is not a detail: today
  `<utf8> "hello" m` is **accepted** and `<utf8:,m> "hello"` is refused, because the inline path
  never consults the family — the bypass [v2 §6](bovnar_v2_proposals.md) calls "a bypass in the one
  check the format exists to perform". Verified against the build; the `±` path must not reproduce
  it.
- **(VALIDATOR)** Both an annotation `±` parameter and a suffix on the same value: the suffix wins,
  as the more specific. This is *deliberately* the opposite of the unit rule, where a mismatch is
  `error_unit_mismatch` — a whole-array `±` is an instrument default meant to be overridden per
  element, whereas a unit is a fact about the quantity. The asymmetry needs stating in the spec
  rather than discovering.
- **(LEXER)** `±` is rejected inside an array element for as long as the `in_array_element` guard
  stands, with the same `error_unexpected_input_byte`. Lifting it is [v2 §6](bovnar_v2_proposals.md)'s
  job, not this note's.

**Streaming.** The clause is bounded — one number plus an optional unit, both already bounded by
`max_number_length` and the unit-component cap — so `ev_data` is simply deferred until the value
clause closes, exactly as it is already deferred until a number's terminator is seen. No new
buffering, no unbounded window. A new `ev_uncertainty` fires between `ev_type_annotation_end` and
`ev_data`, keeping `ev_data` last for every existing consumer.

---

## 8. What each code site has to do

| Site | Layer 0 | Layer 1 |
|---|---|---|
| `src/gendata/units.bvnr` | — | — (the delta and ratio rows already exist) |
| `bovnar_state_table.c` | — | `number_outro` + `inline_unit_outro`: `±`/`+-` → new `uncert_*` states |
| `bvn_parse_type_annotation` | — | new param class, recognised by first code point |
| `bvn_val_receive` | — | the interval rule; family check; magnitude and `k` checks |
| `value_type_spec_t` | — | **untouched** — see below |
| `bvnr_data_t` | — | `+ has_uncertainty, uncertainty, uncertainty_unit, coverage_k, uncert_kind` |
| DOM | — | `bvn_dom_get_uncertainty`, `bvn_dom_get_coverage` |
| Writer / canonical | — | canonical spelling: `±`, after the unit, `k` only when ≠ 1 |
| `--require-field` | **works today** | a path convention for the suffix form |
| JSON convert | shape survives, units already lossy | one more lossy-report line |
| doc/03 §5, doc/05, doc/06 | the convention, the interval rule | the grammar |

**`value_type_spec_t` must not be touched**, and this is the load-bearing implementation decision.
That struct is `{family, width, base}`, and `base` is already doing double duty as `float_fix`'s Q —
[v2 §7](bovnar_v2_proposals.md) names it as a defect ("one field and two meanings"). An uncertainty
is a property of the **value**, not of its type: two values of identical type differ in it. It
belongs in `bvnr_data_t` beside `value_unit`, and putting it there keeps `bvn_type_spec_equal`
meaningful and avoids adding a third meaning to a field that already has two.

---

## 9. Error codes

Appended after the current maximum (`error_type_param_whitespace` = 52), per spec §17. Existing values
never change, and doc/03 §16.10 remains the list.

| Code | Name | Fires when |
|---|---|---|
| 53 | `error_uncertainty_unit_mismatch` | the uncertainty's unit is not commensurable with the interval unit of §6 clause 1 — the 273.15 error |
| 54 | `error_uncertainty_illegal` | negative or non-finite magnitude, `k ≤ 0`, malformed clause |

Two codes, not four: a family violation is already `error_illegal_value_type` and must stay so, since
that is the code path the `utf8` check of §7 has to share.

---

## 10. What this does not close

- **Correlation and covariance.** Two uncertainties do not compose without their correlation, and a
  covariance matrix is a relationship *between* values, not a property of one. It has no place in a
  per-value notation and belongs to the application. This is the largest omission and it is
  deliberate — a format that let you write `±` per value and quietly implied independence would
  encourage exactly the wrong arithmetic downstream.
- **Asymmetric uncertainty.** `+0.5 / −0.3` is common in physics. The suffix does not express it;
  the Layer-0 struct does, via `.u_lower` / `.u_upper`. A `±0.5∓0.3` spelling was considered and
  rejected as unreadable for a case rare enough to afford three lines.
- **Distribution shape.** `±` asserts a standard uncertainty and nothing about its distribution.
  GUM Type B evaluation needs rectangular and triangular; those stay in the struct's `.dist`. A
  later 1.x may add `±rect` / `±tri` to the closed keyword set of §7 — which is precisely what
  keeping that namespace closed buys.
- **Type A versus Type B provenance**, and **degrees of freedom**: metadata about how the number was
  arrived at, not the number. Struct form.
- **Uncertainty on a `datetime`.** Meaningful — GNSS time transfer states it routinely — but
  `datetime` forbids a unit parameter, and the uncertainty of an instant is a *duration*, which would
  be the first unit that family admits. It is a coherent extension and it is a separate decision;
  this note does not take it.
- **Units with no interval unit.** Six affine scales have delta rows. A future affine unit added
  without one would have no interval unit, and §6 clause 1 would have nothing to resolve to. The
  registry's ambiguity check (doc/07) should grow a rule: *an affine row requires a delta row*.

---

## 11. Cost, and how to stage it

| | Layer 0 | Layer 1 |
|---|---|---|
| Grammar | none | two productions changed, four added |
| Registry | none | none |
| C code | none | lexer states, one param class, one validator rule, `bvnr_data_t` |
| Breaks | nothing | nothing — every spelling is an error today (§2.2) |
| Buys | a checkable convention, enforceable with shipped tooling | the notation people actually write |

**Layer 0 stands alone and should ship first,** regardless of whether Layer 1 ever does. It costs
documentation and a normative paragraph, it is enforceable today with `--require-field`, and it
settles the two questions that are genuinely hard — the interval rule and the coverage factor — while
they are still cheap to change. Layer 1 is then sugar with a defined meaning rather than a syntax in
search of one.

**The test [v2 §14](bovnar_v2_proposals.md) sets** — mechanical transcodability — is passed in both
directions here, which is unusual: Layer 1 → Layer 0 is a rewrite of the suffix into the struct, and
Layer 0 → Layer 1 is the same rewrite backwards for any struct that uses only `.value`, `.u` and
`.k`. That the two layers are inter-transcodable is the strongest evidence that they are one design.

**What should not be half-landed:** `±` without §6, or §6 without §3.2's coverage rule. Either half
produces a document that *looks* like it states an uncertainty and does not — which is worse than the
gap, because the gap is at least visible.

---

## See also

- [Temperature Difference (Design Note)](temperature_difference.md) — the delta units §6 is built on,
  and the model this note follows
- [Version 2 Grammar Proposals](bovnar_v2_proposals.md) — the inline-unit defect and the `utf8` bypass
  ([v2 §6](bovnar_v2_proposals.md)), named parameters ([v2 §7](bovnar_v2_proposals.md)), why a closed
  keyword namespace matters ([v2 §8](bovnar_v2_proposals.md)), and the transcode test
  ([v2 §14](bovnar_v2_proposals.md))
- [Specification](03_bovnar_spec.md) — spec §5 (annotations), spec §6.5 (the inline suffix),
  spec §16.10 (the error list), spec §17 (what may grow additively in 1.x)
- [EBNF Grammar](12_bovnar.ebnf) — the value, type-param and constraint-tier rules §7 amends
- [Unit System](05_bovnar_unit_system.md) — the affine scales, the delta rows and the ratio units
- [Unit Policy](06_bovnar_unit_policy.md) — `--require-field`, the enforcement §3.1 relies on

---

*End of Bovnar — Measurement Uncertainty (Design Note) (Bovnar spec 1.1).*
