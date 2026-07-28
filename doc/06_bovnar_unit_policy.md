# Bovnar — Parser-Level Unit Policy

> **Spec version:** 1.1
> **Status:** Design note — the evaluation behind the reader, writer and DOM unit policies. Options 1, 2, 3 and 4 shipped; 0, 5 and 6 did not. Section 7.5 records why option 2 was built after all. Nothing here is normative.
> **Scope:** How the reader could validate and convert units without an application-supplied callback, whether the options combine, and (section 7.4) the producer-side half the evaluation missed.

Companion to [Unit & Currency Reference](05_bovnar_unit_system.md) (the registry) and
[Read/Write API](08_bovnar_readwrite_api.md) (the `want_unit` hook this note is measured against).
Every behavioural claim in section 5 was produced by running the reference implementation
built from this tree; the numbers are transcripts, not estimates.

**What shipped.** Options 4, 1 and 3 were built, as one `bvnr_unit_policy_t` behind
`bvnr_reader_set_unit_policy` — the single-object form section 6.1 argues for, rather than the
separate setters sketched per-option in section 3. The same policy object also drives the WRITER
(`bvnr_writer_set_unit_policy`, validation half only), which this note did not consider at all and
which section 7.4 argues is the half the format's promise actually rests on. The composition is section 4's ladder exactly:
hook, then targets, then normalisation, with the assertions evaluated on the native unit.
Details the evaluation did not anticipate are recorded in sections 7.3 to 7.5, two of them defects
found only by sweeping the whole unit registry against the implementation. Options 0, 2, 5 and 6
were not built; the reasoning in sections 3.1, 3.6, 3.7 and 7.2 stands as written. Option 2 WAS
built in the end — section 7.5 records what its "defer" verdict got wrong, and what it got right.
The shipped API is documented in [Read/Write API 1.12](08_bovnar_readwrite_api.md#112-reader-side-unit-policy-bvnr_reader_set_unit_policy),
the Python form in [Python Bindings 5.6](09_bovnar_python_bindings.md#56-unitpolicy--validation-and-conversion-without-a-callback),
and the CLI flags in the README. Section 3.2's claim that every non-C consumer gets the feature
for free held: the Python binding is a dataclass and one setter, with no per-value trampoline.

## Table of Contents

1. [Overview](#1-overview)
    - 1.1 [What the parser already enforces](#11-what-the-parser-already-enforces)
    - 1.2 [What the want_unit hook costs](#12-what-the-want_unit-hook-costs)
2. [The design space](#2-the-design-space)
    - 2.1 [Two axes](#21-two-axes)
    - 2.2 [The options at a glance](#22-the-options-at-a-glance)
3. [The options](#3-the-options)
    - 3.1 [Option 0 — prebuilt hooks in the library](#31-option-0--prebuilt-hooks-in-the-library)
    - 3.2 [Option 1 — an opaque target list set at open time](#32-option-1--an-opaque-target-list-set-at-open-time)
    - 3.3 [Option 2 — key-path rules](#33-option-2--key-path-rules)
    - 3.4 [Option 3 — a normalisation mode](#34-option-3--a-normalisation-mode)
    - 3.5 [Option 4 — validation-only assertions](#35-option-4--validation-only-assertions)
    - 3.6 [Option 5 — an in-document declaration](#36-option-5--an-in-document-declaration)
    - 3.7 [Option 6 — a DOM-tier post-pass](#37-option-6--a-dom-tier-post-pass)
4. [Composing 4, 1, 3 and 5](#4-composing-4-1-3-and-5)
    - 4.1 [The target-resolution ladder](#41-the-target-resolution-ladder)
    - 4.2 [Assertions evaluate on the native unit](#42-assertions-evaluate-on-the-native-unit)
    - 4.3 [Why the in-document form must be declarative](#43-why-the-in-document-form-must-be-declarative)
    - 4.4 [Precedence with the want_unit hook](#44-precedence-with-the-want_unit-hook)
5. [Measured behaviour the design has to respect](#5-measured-behaviour-the-design-has-to-respect)
    - 5.1 [Compatible is not the same as convertible](#51-compatible-is-not-the-same-as-convertible)
    - 5.2 [no_unit is compatible with the ratio units](#52-no_unit-is-compatible-with-the-ratio-units)
    - 5.3 [What terminates and what does not](#53-what-terminates-and-what-does-not)
    - 5.4 [Quantity kinds already fence off the dimensionless units](#54-quantity-kinds-already-fence-off-the-dimensionless-units)
6. [Cost](#6-cost)
    - 6.1 [ABI and configuration surface](#61-abi-and-configuration-surface)
    - 6.2 [Fan-out across bindings and documentation](#62-fan-out-across-bindings-and-documentation)
7. [Recommendation](#7-recommendation)
    - 7.1 [Sequencing](#71-sequencing)
    - 7.2 [What not to build](#72-what-not-to-build)
    - 7.3 [What the evaluation missed](#73-what-the-evaluation-missed)
    - 7.4 [The half this note never considered](#74-the-half-this-note-never-considered)
    - 7.5 [Option 2, reconsidered](#75-option-2-reconsidered)
- [See also](#see-also)


---

## 1. Overview

Bovnar validates units in the parser today, and converts them only through an
application-supplied C callback. Those are two different tiers of service for
one feature, and the gap between them is what this note evaluates: what it would
take for the reader itself to hold a unit policy — a statement of what the
document must contain, and what the consumer wants to receive — with no function
pointer involved.

The motivation is not C ergonomics. `bvn_parse_unit` is public, so a C caller can
already turn `"m/s"` into a `value_unit_t` in one line. The motivation is that a
callback is the one part of the read API that does not survive a language
boundary cleanly, and unit conversion is the feature most likely to be wanted by
exactly the consumers that cannot express it.

### 1.1 What the parser already enforces

Unit checking is not absent from the reader — it is present and strict, but it
only ever checks the document against **itself**:

* an inline unit suffix that disagrees with the annotation unit is
  `error_unit_mismatch`;
* a unit that does not parse, or a prefix the base unit does not accept, is
  `error_unit_illegal`;
* a unit string longer than the cap is `error_unit_too_long`.

What the reader cannot be told is anything about the document's relationship to
the **consumer's** expectations. "Every numeric value in this document must carry
a unit", "this field must be a length, whatever length unit it is written in",
"deliver everything to me in SI" — none of these are expressible without writing
code that runs per value.

### 1.2 What the want_unit hook costs

`bvnr_read_flags_t.want_unit` is a per-value callback returning a target
`value_unit_t` and an output base. Underneath it sits the part that is genuinely
hard and is already finished: exact arbitrary-precision conversion, affine
handling, work limits, and an all-or-nothing exactness contract. Nothing in this
note proposes to touch that engine — every option below reuses it.

The costs are all in the interface:

**A function pointer is hostile to bindings.** The Python binding pays a ctypes
trampoline for every numeric value in the document. The WASM shim has one
`userdata` slot and has to share a single struct between the event serialiser and
the conversion hook. The CLI does not expose conversion at all — there is no
`--unit` flag, because wiring one up means writing a callback and threading state
through it.

**There is no "convert what fits" mode.** The hook decides per value, and once it
has named a target, an incompatible unit stops the parse. Deciding "convert this
one only if it is a length" means calling `bvn_units_compatible` yourself inside
the callback — which works, and which every caller who wants blanket conversion
has to reinvent. The WASM shim demonstrates the failure mode: its hook returns a
target unconditionally, so asking the playground to show a document in `m/s`
aborts on the first temperature it meets.

**Validation without conversion needs a callback that never converts.** Checking
that a field is dimensionally a length means writing a hook, returning `false`
from it every time, and doing the dimension comparison by hand on the side.

---

## 2. The design space

### 2.1 Two axes

The options below look like six variations on one feature. They are not — they
sit on two axes, and that is precisely what makes some of them combinable:

* A **predicate** accepts or rejects the document. It never changes what the
  consumer sees. Option 4 is the only pure predicate.
* A **target selector** answers "what unit should this value be delivered in".
  Options 1, 3 and 5 are all target selectors, which is why they compete with one
  another and why combining them requires a precedence order.

Option 0 and option 6 are neither: they are packaging (a convenience wrapper, and
a post-pass over the materialised DOM).

### 2.2 The options at a glance

| # | Mechanism | Axis | Spec change | Needs key tracking |
|---|-----------|------|-------------|--------------------|
| 0 | Prebuilt `want_unit` implementations | packaging | no | no |
| 1 | Opaque target list, set at open time | selector | no | no |
| 2 | Key-path rules | selector | no | **yes** |
| 3 | Normalisation mode (no target named) | selector | no | no |
| 4 | Validation-only assertions | predicate | no | no |
| 5 | In-document declaration | selector or predicate | **yes** | no |
| 6 | DOM-tier post-pass | packaging | no | no |

---

## 3. The options

Sections 3.1 to 3.7 use one running document. It is deliberately awkward: mixed
dimensions, an affine scale, a bare ratio, a prefixed currency.

```bovnar
#!bovnar 1.1
.wind_speed     = <float:64,km/h>    42.0;
.inlet_temp     = <float:64,°F>      212.0;
.tank_level     = <float:64,in>      12.0;
.duty_cycle     = <float:64,%>       35.0;
.spare_capacity = <float:64>         0.25;
.unit_price     = <float:64,k~$USD>  5.0;
```

### 3.1 Option 0 — prebuilt hooks in the library

Ship ready-made `want_unit` implementations plus a caller-allocated context, so
the common cases need no callback of the caller's own:

```c
typedef struct {
	value_unit_t target;
	uint32_t     base;
	bool         only_if_compatible;
} bvnr_want_fixed_ctx_t;

BVN_API bool bvnr_want_unit_fixed(void* userdata, const bvnr_data_t* data,
                                  value_unit_t* want, uint32_t* want_base);
```

New symbols only: no ABI risk, no parser semantics, no documentation beyond the
function's own comment. It also fixes the WASM shim's unconditional-target bug in
the one place rather than in each embedder.

It does nothing for the consumers that hurt. Python still pays the trampoline;
the CLI still has no flag; a caller in any language that cannot take the address
of a C function is exactly where it was. Worth doing as a by-product of option 1,
not as an answer to it.

### 3.2 Option 1 — an opaque target list set at open time

The caller hands over unit **text**, not a struct, plus an output base and a
policy for the values that do not match. The reader parses the targets once and,
per numeric value, converts to the first dimensionally compatible one.

```c
typedef struct bvnr_unit_target_s {
	const char* unit;   /* unit text, e.g. "m/s"; parsed once when set */
	uint32_t    base;   /* output base, 0 = keep the value's own */
} bvnr_unit_target_t;

static const bvnr_unit_target_t targets[] = {
	{ "m/s", 0 },
	{ "°C",  0 },
	{ "m",   0 },
};
bvnr_reader_set_unit_targets(r, targets, 3, bvnr_unit_leave_incompatible);
```

Against the running document, with `.spare_capacity` and `.unit_price` left alone
because nothing in the list is compatible with them:

| key | native | target chosen | delivered |
|-----|--------|---------------|-----------|
| `wind_speed` | `km/h` | `m/s` | exact `35/3`, no terminating text (see 5.3) |
| `inlet_temp` | `°F` | `°C` | `100` |
| `tank_level` | `in` | `m` | `0.3048` |
| `duty_cycle` | `%` | — | untouched |
| `spare_capacity` | none | — | untouched |
| `unit_price` | `k~$USD` | — | untouched (but see 5.1) |

This is the smallest change with the largest reach. It needs no key tracking, no
spec change, and no new conversion machinery — `bvn_apply_want_unit` in the
validator already parses the value to an exact rational, converts, renders, and
enforces the work limits. Option 1 replaces one step of that function, "ask the
callback", with "walk a table". The selector is `bvn_units_compatible`, which is
already public and already used for exactly this test inside careful callbacks.

Every non-C consumer gets the feature for free, because there is nothing to
marshal: an array of strings and an integer crosses ctypes, WASM and any FFI
without a trampoline. The CLI gets `bovnar events --unit m/s` for the price of an
argument parse.

Two properties have to be documented rather than left to be discovered. Rule
order is semantically significant, because "first compatible wins" is the whole
selection algorithm — a list of `{"m", "km"}` never selects `km`. And a value
that was left alone is only distinguishable from one that was converted by
`data->converted`, which makes that flag part of the contract rather than a
convenience.

### 3.3 Option 2 — key-path rules

The same as option 1, but selecting by `.sensor.inlet_temp` rather than by
dimension. Strictly more expressive: it is the only option that can say "convert
this field and not that one" when both are temperatures.

It is also the only option that requires new state on the hot path. The streaming
validator tracks **no key context at all** — not a current key, not a stack, not a
depth. Even the CLI's `query` command sidesteps this by materialising the DOM and
walking that instead of tracking keys during the parse. Adding a key stack means a
depth cap, an allocation strategy for long keys, and a decision about what a path
means inside an array — and the result is a schema language living inside the
parser, overlapping the DOM tier that already exists for whole-document questions.

Defer. It is a different feature wearing this one's clothes, and option 1 plus the
existing `want_unit` hook covers its use cases at a lower price.

### 3.4 Option 3 — a normalisation mode

No target named at all. One enum, and every value that has an SI meaning is
delivered in coherent SI base units with prefixes folded out.

```c
bvnr_reader_set_unit_normalise(r, bvnr_normalise_si);
```

| key | native | delivered |
|-----|--------|-----------|
| `wind_speed` | `km/h` | `m/s`, exact `35/3` |
| `inlet_temp` | `°F` | `K`, `373.15` |
| `tank_level` | `in` | `m`, `0.3048` |
| `duty_cycle` | `%` | dimensionless — see below |
| `spare_capacity` | none | untouched |
| `unit_price` | `k~$USD` | no SI meaning; untouched |

This is the most opaque option — zero configuration, deterministic, and it pairs
conceptually with the writer's existing `BVN_UNIT_REDUCE` flag: reduce on write,
normalise on read. Half the machinery is already there, since `bvn_unit_reduce`
is implemented and the writer uses it. It is the right answer for canonical
storage and for diffing two documents that disagree only about units.

The cost is that it turns every value into a conversion candidate, which
multiplies the exactness problem described in 5.3 across the whole document
instead of confining it to the values the caller explicitly targeted. A
normalisation mode almost certainly cannot ship with the current
all-or-nothing default; it needs either `want_unit_allow_nonterminating` on by
construction, or its own "leave it alone if it is not exact in this base" policy,
which reintroduces silent skipping and leans even harder on `data->converted`.

The second cost is the definition of "normalise". `%` and `ppm` are dimensionless
with a real factor, so normalising them means multiplying by 1/100 and 1/1000000
and delivering a bare number — defensible, and not obviously what a caller
expects. That question has to be answered in the specification of the mode, not
discovered by its users.

### 3.5 Option 4 — validation-only assertions

No conversion, no rational arithmetic, no work limits, no exactness pitfalls, and
nothing that can fail on untrusted input except the check itself:

```c
static const char* lengths[] = { "m" };
bvnr_reader_require_unit_on_numeric(r, true);   /* reject no_unit */
bvnr_reader_require_dimension_of(r, lengths, 1);/* every value must be a length */
```

Against the running document, `require_unit_on_numeric` rejects it at
`.spare_capacity` with a unit error, because a bare `0.25` is exactly the thing a
fully-annotated document is not supposed to contain.

This is the cheapest option here by a wide margin — a dimension check is a
comparison of two seven-element vectors — and it is the only one that answers
"is this document what I was promised" rather than "convert it for me". It is
orthogonal to every other option on the list, and worth shipping regardless of
which conversion mechanism wins.

### 3.6 Option 5 — an in-document declaration

A second directive alongside the version directive, declaring the document's own
unit policy:

```bovnar
#!bovnar 1.1
#!units si
```

Mechanically this is the cheapest part: the lexer already has a first-line,
first-comment-only capture path for `#!bovnar <major>.<minor>`, and a second
directive form slots into it. Everything else about it is expensive, and section
4.3 argues that its obvious reading — an instruction to the reader — is the wrong
one.

The unavoidable costs are a spec version bump to 1.2, the interaction with
`strict_version`, writer support for emitting it, and the fan-out through the
conformance document and the published documentation set.

### 3.7 Option 6 — a DOM-tier post-pass

```c
bvn_dom_normalise(doc, "m/s");
```

Operates on the materialised DOM rather than the streaming path, so it touches
neither the hot path nor the read-flags ABI, and it can be built and changed
freely. It cannot abort a parse, and it does nothing for a streaming consumer —
which is most of the consumers that care about units. A complement to whatever
else is chosen, not a substitute for it.

---

## 4. Composing 4, 1, 3 and 5

Options 4, 1, 3 and 5 do combine, under one precedence ladder and one change to
what option 5 means. They combine because of the axis split in 2.1: option 4 is a
predicate and the other three are selectors, so the only genuine conflict is
among the selectors, and it is resolved by ordering them.

### 4.1 The target-resolution ladder

Per numeric value, in the slot the `want_unit` hook occupies today — after unit
validation, before either value callback, so that an abort still happens before
the consumer has seen anything:

```text
resolve target:   want_unit hook  →  target list (1)  →  normalise mode (3)
                  most specific ...................................... catch-all
convert:          the existing exact-rational engine, unchanged
assert:           the predicates (4), evaluated on the NATIVE unit
```

First match wins, specificity descending. Option 3 sits at the bottom as the
terminal fallback that catches whatever option 1's list did not match, which
makes the combination useful rather than merely legal: "deliver speeds in `m/s`
and temperatures in `°C`, and normalise anything else to SI" is one target list
plus one enum.

Applied to the running document with `targets = {"m/s", "°C"}` and
`normalise = si`:

| key | native | resolved by | delivered |
|-----|--------|-------------|-----------|
| `wind_speed` | `km/h` | target list | `m/s` |
| `inlet_temp` | `°F` | target list | `°C`, `100` |
| `tank_level` | `in` | normalise fallback | `m`, `0.3048` |
| `duty_cycle` | `%` | normalise fallback | per the mode's ratio rule (3.4) |
| `spare_capacity` | none | nothing matches | untouched |
| `unit_price` | `k~$USD` | nothing matches | untouched |

### 4.2 Assertions evaluate on the native unit

This is the rule that keeps option 4 from colliding with the selectors:
**validate the document as written, convert for the consumer.**

If assertions ran after conversion they would be very nearly tautological — the
value is in the unit the caller just asked for, so "must be a length" passes
because the caller said `m`, not because the document said anything. Dimension
requirements and `require_unit_on_numeric` are statements about what the document
contains, so they must see what the document contains.

The concrete consequence, on the running document with both a length requirement
and a normalise-to-SI mode: `.spare_capacity` fails the assertion, and it fails
whether or not normalisation would have left it alone. The predicate does not
care what the selector decided.

### 4.3 Why the in-document form must be declarative

Option 5 as first framed — a directive that tells the reader to convert — does
not compose, for two independent reasons.

**Interop.** A transformative directive breaks reader equivalence. A spec-1.1
reader ignores `#!units si` and delivers native units; a 1.2 reader delivers
converted ones. Two conforming readers then disagree about the value the consumer
receives, in a format whose entire premise is that a document means one thing to
everyone holding it.

**Trust.** A transformative directive is input-controlled behaviour. A document
would decide what an application's callbacks receive, which is a strange power to
hand to a file whose contents are being validated precisely because they are not
trusted.

Reframe it as a declaration **about the content** — "the values in this document
are in SI" — and both problems dissolve. A 1.1 reader that ignores it loses
nothing, because the values were already whatever they were; the directive only
adds a claim that a 1.2 reader can check. The parser's job becomes verifying that
the document keeps its own promise, which is option 4's machinery pointed at a
target the document supplied instead of one the caller supplied.

That reframing removes option 5 from the ladder in 4.1 entirely. It stops
competing with options 1 and 3, the precedence question disappears, and what
remains is a cheap dimension check with a well-defined failure.

If a transformative reading is wanted later, it should be gated behind an
explicit application opt-in and placed **below** option 1 in the ladder — the
application is the consumer and knows what it needs — but the declarative form is
what should ship first.

### 4.4 Precedence with the want_unit hook

The hook stays, at the top of the ladder. Making it the most specific selector
costs nothing once a ladder exists, and it enables the pattern that a table alone
cannot express: normalise the whole document, and hand-handle the one field that
needs different treatment.

The alternative — refusing to let the hook coexist with a policy, with
`error_invalid_argument` — is defensible when there are only two mechanisms and no
ordering to appeal to. Once there are three, the ordering exists anyway and the
hook may as well sit in it.

---

## 5. Measured behaviour the design has to respect

Every result in this section was produced by linking against `libbvnr.a` built
from this tree and calling the public unit API directly. They are the constraints
that decide whether an opaque selector is safe, and three of the four are not
what a reasonable designer would assume.

### 5.1 Compatible is not the same as convertible

`bvn_units_compatible` is the obvious selector for "convert this value if the
target fits". It is not sufficient, because currencies deliberately carry no
dimension vector and therefore fail the compatibility test even against
themselves:

```text
k~$USD -> $USD    compatible=false   convert_factor_ok=true   f=1000
$USD   -> $EUR    compatible=false   convert_factor_ok=false
```

The exact-rational path performs `5 k~$USD -> $USD` correctly, yielding `5000`. A
selector built on `bvn_units_compatible` alone silently declines a conversion the
engine can do, which is why `.unit_price` is listed as untouched throughout
section 3. Making it work needs the prefix-only-delta path that
`bovnar_si_units.c` currently keeps internal, exposed or mirrored in the selector.

Note also that `bvn_unit_convert_factor` is the wrong predicate for a different
reason: it reports `ok=false` for `°F -> °C` and `°C -> K`, because an affine
conversion has no single multiplicative factor. Those conversions are perfectly
well supported by the value and rational entry points. A selector that screens on
`convert_factor_ok` would drop every temperature in the format.

### 5.2 no_unit is compatible with the ratio units

The sharpest hazard, and the least visible:

```text
no_unit -> %      compatible=true   f=100
no_unit -> ppm    compatible=true   f=1000000
ppm     -> %      compatible=true   f=0.0001
```

A bare number carries no dimension, and neither does a percentage, so they are
dimensionally compatible and the engine will happily convert between them. On the
running document, a policy containing `%` therefore converts

```text
.spare_capacity = <float:64> 0.25;
```

into `25 %` — exactly the kind of silent factor-of-a-hundred misread the format
exists to prevent, produced by the machinery meant to prevent it.

Any opaque selector must exclude unitless values explicitly rather than relying on
compatibility. The test already exists internally as `BVN_UNIT_IS_NO_UNIT`. The
`ppm -> %` row is included to show why the rule cannot simply be "never convert
dimensionless things": that one is a real, wanted conversion.

### 5.3 What terminates and what does not

The library's contract is that a conversion is delivered exactly or not at all.
An exact **rational** and a terminating **positional expansion** are different
things, and only the second one can be handed over as digits:

```text
100 km/h -> m/s   base 10   exact=1   250/9      text=<nonterminating>
100 mph  -> m/s   base 10   exact=1   5588/125   text=44.704
100 °F   -> °C    base 10   exact=1   340/9      text=<nonterminating>
212 °F   -> °C    base 10   exact=1   100/1      text=100
 25 °C   -> K     base 10   exact=1   5963/20    text=298.15
 12 in   -> m     base 10   exact=1   381/1250   text=0.3048
  1 m    -> km    base 10   exact=1   1/1000     text=0.001
  1 m    -> km    base  2   exact=1   1/1000     text=<nonterminating>
```

Three things follow. Metric-to-metric is where the trouble is, not
customary-to-metric: `km/h -> m/s` carries a factor of 5/18 and does not
terminate, while `mph -> m/s` is exactly 0.44704 and does. Whether a conversion
terminates depends on the value as well as the units — 212 °F converts cleanly and
100 °F does not, through the same 5/9 slope. And the output base matters
independently: `1 m -> km` is `0.001` in base 10 and non-terminating in base 2.

Under the current default, every `<nonterminating>` row above aborts the parse
with `error_unit_inexact`. That is a defensible default for a caller who named one
target deliberately. It is a poor default for a blanket mode, which is the main
reason option 3 needs an exactness policy of its own.

### 5.4 Quantity kinds already fence off the dimensionless units

The one pleasant surprise. Dimensionless does not collapse into one bucket,
because the library carries a quantity-kind vector alongside the dimension vector:

```text
dB  -> %         compatible=false
pH  -> no_unit   compatible=false
rad -> °         compatible=true    f=57.2958
B/s -> Hz        compatible=false
```

Decibels, the pH scale, angles, turbidity scales and practical salinity are each
their own kind, so a normalisation mode does not need a hand-maintained skip list
to keep them apart — the existing compatibility test already refuses them. The
concern is narrower than it looks from the unit registry: currencies (5.1) and
unitless values (5.2), not the logarithmic scales.

---

## 6. Cost

### 6.1 ABI and configuration surface

`bvnr_read_flags_t` has exactly `uint64_t _reserved[2]` left — sixteen bytes. A
target pointer plus a count plus two policy enums consumes all of it, and the
struct is mirrored by hand in the Python bindings, the WASM shim and the ABI dump
test, so growing it is a five-file change before any behaviour exists.

Prefer setter functions on the reader — `bvnr_reader_set_unit_targets`,
`bvnr_reader_set_unit_normalise`, `bvnr_reader_require_dimension_of` — called
after create and before read. New symbols do not perturb the struct layout, do not
consume the reserved words, and are the FFI-friendly shape anyway: a binding can
call a function without describing a struct. If the four options ship together
they should share one policy object behind one setter rather than four
independent knobs.

### 6.2 Fan-out across bindings and documentation

Any public surface added here lands in more places than the implementation:
`include/bovnar.h`, the validator, the ABI dump test, the Python binding structs
and reader wrapper, the WASM shim, the CLI, plus the specification, the unit
reference, the read/write API document, the regenerated HTML documentation and
the LLM text bundle, and the changelog. This is the argument for choosing one
mechanism and doing it thoroughly rather than shipping three partial ones.

The design-note tier this document belongs to is deliberately outside that set:
unnumbered files in `doc/` are not part of the published documentation series and
are not required to appear in the PDF bundle.

---

## 7. Recommendation

Build option 4 first, then option 1, then option 3 if normalisation demand turns
out to be real, and option 5 last if at all — in its declarative form only.

Option 4 is nearly free, has no exactness pitfalls, cannot fail on untrusted
input, and closes the one gap that no amount of callback cleverness makes
pleasant: asserting that a document is fully annotated. Option 1 is the smallest
change with the largest reach, because it reuses the finished conversion engine
and gives every binding and the CLI a feature they currently cannot express.

Together they answer both halves of the original question — validation against
caller expectation, and conversion without a callback — and they leave the
`want_unit` hook in place as the escape hatch for anything more specific.

### 7.1 Sequencing

The order matters beyond effort. A format-level directive has to **name** a
policy, and once that name is in the specification it is frozen. Shipping option 5
before option 3's normalisation semantics have settled — in particular the ratio
rule in 3.4 and the exactness policy in 5.3 — commits the format to a definition
that is still under discussion. The first three options are library work with no
interoperability consequences and can be revised freely; option 5 is the only one
that cannot be taken back.

### 7.2 What not to build

Option 2 should not be built. It is the only option requiring key tracking in the
streaming validator, and what it produces is a schema layer inside a parser that
already has a DOM tier for whole-document questions.

Option 0 should not be built on its own. As packaging shipped alongside option 1
it is nearly free; as an answer to the problem it leaves every non-C consumer
exactly where it was.

### 7.3 What the evaluation missed

Two decisions the implementation forced that this note had not anticipated, kept
here because both are the kind of question an evaluation is supposed to surface
and did not.

**A value already in the target unit.** Section 3.2 described selection as "first
compatible target wins" and stopped there, which quietly makes a policy of
`{"m"}` convert every value that is already in metres to itself: `converted`
comes back true, the digits come back identical, and each one has paid a full
exact parse-convert-render round trip. The shipped behaviour skips it, so
`converted` means "the policy restated this value" rather than "the policy looked
at it". A target that names an output base is exempt, because that is a pure base
conversion and the value really is being restated.

**The unitless fence is needed on the validation side too.** Section 5.2 framed
`no_unit` being dimensionally compatible with `%` and `ppm` as a hazard for
conversion, and it is — but the same hole is in `require_dimension_of`, where a
requirement of `{"%"}` would quietly accept an unannotated bare number as a
ratio. Both halves of the policy now go through one predicate, so the fence
cannot be present in one and missing in the other. The lesson generalises past
this feature: a rule that says "compatible" is doing dimensional analysis, and
dimensional analysis has nothing to say about whether a number was annotated.

**A dimension vector does not determine a unit.** Section 3.4 assumed SI
normalisation could be built by reading a unit's dimension vector and spelling
the SI base units back out. Sweeping the registry through the implementation
proved otherwise in two ways, neither visible from the design.

The photometric units — `lm`, `lx`, `ph` — each carry the steradian's quantity
kind, which no SI dimension vector can express, because the steradian is
dimensionless. Rebuilt from dimensions alone they come back as `cd` and `cd/m²`:
luminous intensity where the value was luminous flux. The conversion engine
refuses those pairs, correctly, so a document containing a lumen aborted the
moment normalisation was switched on. And an affine scale inside a compound —
`s/°C` — has the dimensions of `s/K` and is still unconvertible, because °C
means nothing at an exponent other than 1: 1812 such compounds exist in the
registry.

Both are fixed by making the normal form check its own answer rather than trust
the dimension vector, and section 5's conclusion needs the same qualification:
`bvn_units_convertible` is a screen, not a guarantee. That is why a policy target
the engine ultimately refuses now leaves the value in its native unit instead of
aborting — a policy names a preference, and a preference that cannot be applied
is a non-match, not an error.

**`leave_inexact` had borrowed its reasoning from the wrong flag.** The note
argued that an irrational factor must always abort because "there is nothing
exact to hand over either way". That is true of `want_unit_allow_nonterminating`,
whose fallback *is* the exact rational — and false of the leave policy, whose
fallback is the value in its own unit, which exists whatever the factor is. The
shipped flag therefore steps over irrational factors and work-limit refusals as
well as non-terminating expansions: one meaning, "deliver what you can, keep the
rest as written", rather than three cases with different answers.

### 7.4 The half this note never considered

Every option in section 2 is a READER option. The whole evaluation asked what a
consumer could demand of a document, and never asked what a producer could be
held to — which, on reflection, is the more load-bearing question.

A reader policy can only reject a document that already exists. If the producer
wrote a bare number, the damage is done at the point the file was created: the
reader's refusal is a late detection of a fault that was preventable, and every
consumer of that file has to detect it separately. The format's claim is that a
document carries everything needed to interpret it, and only the writer is in a
position to make that true.

So the same policy object drives `bvnr_writer_set_unit_policy`, in its
validation half. `require_unit` and `require_dimension_of` mean exactly what
they mean on the reader — deliberately the same predicate, so a producer and a
consumer configured alike cannot disagree — and `bvnr_write_event` fails with
`error_unit_mismatch` rather than emitting the value.

The conversion half is refused there rather than ignored. The writer already has
a value-rewriting mode in `BVN_UNIT_REDUCE`, which folds prefixes out and
rescales exactly; a second one arriving through a different door, with its own
rules about exactness and its own opinion about dimensionless units, is how two
features end up disagreeing about what a document says. A caller who sets the
conversion fields on a writer has assumed something untrue and is told so.

The interesting implementation detail is where the unit comes from. On the
reader a value's unit is settled by the time it is delivered; on the writer it
may arrive on the value event or as a parameter of the type annotation that
preceded it, and in the second case the value event carries no unit at all. The
annotation's unit is left in the writer's `parsed_unit`, which **outlives the
value it belonged to** — so reading it unguarded would let one annotated value
vouch for the next bare one. The serialiser's existing `emitted_unit` flag,
reset per assignment and per annotation, is what makes the fallback safe. That
flag existed for an unrelated reason (not double-emitting an inline unit under
an annotation that already carried one), which is the sort of luck worth
recording.

### 7.5 Option 2, reconsidered

Section 3.3 deferred key-path rules and section 7.2 said not to build them. They
were built. The verdict was wrong in its conclusion and right in its facts,
which is worth separating.

**What it got right.** Key tracking really was absent — the streaming validator
had no current key, no stack, no depth, and the CLI's `query` really did
sidestep the problem by materialising the DOM. Adding it really did mean a depth
cap, a byte cap, and a decision about arrays. None of that was overstated.

**What it got wrong** was calling the result "a schema language living inside
the parser". A rule is one path and one unit; it says nothing about types,
cardinality, required-ness or ordering, and it has no vocabulary in which to.
The cost was a flat buffer, a mark per open struct, and a rule that once a push
is lost every deeper one is too — perhaps eighty lines, no allocation, and no
new tier. Weighed against the feature it enables, that was not a close call, and
the note reached the opposite conclusion largely because it had bundled option 2
with the much larger thing it feared it would become.

**The part the evaluation could not have guessed** is what "and it has no
vocabulary in which to" costs. A path either is known or is not, and an unknown
path must match NOTHING. Nesting deeper than the tracker can describe, a key
longer than a component may be — in every such case the honest answer is that
the position is unknown, and the dangerous answer is to guess. Hence the rule
that a lost push makes every deeper push lost too, regardless of whether it
would have fitted: it keeps pushes and pops balanced, and it is what stops a
rule for `.a.b` from ever firing on something else. Matching the wrong field is
the one failure mode a per-field rule must not have, and it is a failure mode
the whole-document options simply do not possess.

**And the rule it forced was not enough on its own.** "An unknown path must
match nothing" is stated above as though it were the whole invariant, and the
first implementation satisfied it and was still wrong: an array of structs has
ONE assignment and one key for every row, so the first row's push consumed the
key and every later row pushed whatever key that row had ended on. The path was
never unknown — it was confidently wrong, and a rule naming `.holdings.amount`
quietly stopped applying after the first row of every table in the document.
Silently not firing is the same defect as firing on the wrong field, and it
survived the first round of testing because every test document had one struct
per key. The fix is that a close RESTORES the key the open consumed, which costs
nothing because that key is the path component being removed. The general
lesson: a per-field rule needs a test corpus of document SHAPES, not of units.
The suite now generates them — nested structs, arrays of scalars, arrays of
structs, structs inside rows, to a random depth — recording each value's true
path as it builds each document and then asserting that a rule naming that path
fires on exactly the values living there. Sixteen thousand such assertions over
three thousand generated documents found nothing further, which is the only kind
of evidence worth having about a defect that a hand-written table missed.

Two AGREEMENT properties are checked the same way, because a policy is now read
by three separate pieces of code and nothing else would notice them drifting.
Whatever the writer accepts under a policy, the reader must accept under that
same policy — otherwise a producer emits a file its own consumer rejects, which
is the failure the producer-side check exists to prevent, arrived at through the
check itself. And for one document under one policy, the DOM must report the
same value and the same unit as the streaming reader for every value, and reach
the same verdict about the document. Neither has disagreed over five thousand
generated documents.

The registry itself is swept through a real PARSE rather than through the
conversion functions, which is a different question and was for a while the last
place a gap could have hidden: section 5's sweeps prove what the engine does,
and nothing about what a document does, since a unit reaches the policy only by
being written into an annotation, lexed back out and matched. All 396 units
survive that round trip; 124 of them normalise, each to exactly the value and
unit the engine gives in isolation; the other 272 are left alone, and every one
of those is a case the engine genuinely cannot deliver — no SI form, already
normal, an irrational factor, or no terminating expansion. "Left alone" is not
allowed to be a shrug. The same table also goes through the DOM, through the
writer under two policy shapes, and back through a target naming each value's
own unit, which must be a no-op rather than a pointless rewrite.

**And the DOM followed for free.** Section 3.7 treated a DOM-tier pass as a
separate option with its own machinery. Once the reader had the policy the DOM
needed thirty lines: it is built on the reader, so it inherits everything, and
the only real decision was that a converted value is STORED converted — with one
wrinkle the note could not have anticipated, that an integer converting to a
fraction has to be stored as a float, because the DOM decides int-versus-float
from the declared family and would otherwise hand `0.005` to the integer
builder.

---

## See also

- [Unit & Currency Reference](05_bovnar_unit_system.md) — the unit registry and the notation grammar
- [Read/Write API](08_bovnar_readwrite_api.md) — the `want_unit` hook and the read flags this note extends
- [Unit Ambiguities](07_bovnar_unit_ambiguities.md) — how a unit token is resolved, and the pairs that look interchangeable
- [Streaming](10_bovnar_streaming.md) — why the reader holds no key context during a parse

---

*End of Bovnar — Parser-Level Unit Policy (Bovnar spec 1.1).*
