# Changelog

All notable changes to Bovnar are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning is **semantic
versioning of the format (spec)**; the reference implementation
(`BVNR_VERSION_STRING`, `bovnar.__version__`, the CMake project version) tracks
it in lockstep. The highest spec a build understands is reported by
`bvnr_spec_version()` / `BVNR_SPEC_VERSION_MAJOR`·`MINOR`.

## [Unreleased]

The on-the-wire format grows only additively *except in one place*: the unit
parser accepts one new input spelling (the compact prefix form, below), the
canonical output form is unchanged, and every existing document parses to exactly
the same values — **unless it has whitespace inside a type-annotation parameter**,
which used to be deleted and is now an error. See the first entry below; those
documents carried a unit their author did not write. **The ABI breaks**:
`bvnr_data_t` and `bvnr_read_flags_t.want_unit` changed shape (see below) —
rebuild consumers against the new headers. **SOVERSION is bumped 1 → 2**
(`libbvnr.so.2`), so a binary built against 1.x headers fails to load rather than
reading the grown by-value structs at the wrong size.

### Fixed — a temperature difference that cancelled to itself came out a temperature

`ΔK·m/m` was **compatible with `K`** — a temperature *scale* — and **incompatible
with `ΔK`**, the interval it literally spells. So was `ΔK²/ΔK`, and every other
compound that cancels down to a lone interval.

The interval kind is scoped to "a lone unit at exponent 1", because that is the
only shape where an affine offset could ever have been applied; inside a compound
`ΔK` and `K` are the same unit, which is what keeps `W/(m²·ΔK)` a U-value. That
rule was being asked of the components **as written** rather than of the unit
after cancellation, so three components that reduce to one counted as a compound
and the kind went uncounted.

The consequence went further than a compatibility answer. `bvn_unit_reduce` turns
`ΔK·m/m` *into* a lone `ΔK`, so reduction produced a unit its own input could not
convert to — `bvn_unit_convert_value` refused the pair — and `BVN_UNIT_REDUCE`
would have rewritten a difference into a reading. Exactly the substitution the Δ
units exist to prevent, reached through the compound door.

`bvni_kind_exponents` now folds repeated bases into a net exponent before asking
whether the unit is lone. Every row of the documented table in doc/07 §9 is
unchanged — `ΔK` vs `K` still incompatible, `W/(m²·ΔK)` still a U-value,
`1/ΔK` still an expansion coefficient — and the fold is neutral for the other
kinds, which count `weight × exponent` either way. Found by a randomised sweep
over compound units, not by reading: every shape in that table was already right.

### Fixed — a unit conversion lost 5 digits, or refused, when an intermediate went subnormal

A compound's per-component SI factors can each sit far outside a double's range
while the **product** sits comfortably inside it. `bvn_unit_to_si_factor`
accumulated in a plain double, so the running value walked through the subnormal
range and lost mantissa bits it never got back:

| unit | true factor | was | now |
|---|---|---|---|
| `n~m·r~Da⁴·r~tn_l⁴` | 9.9999999999999996e-226 | 5.3e-5 relative error | within 1 ulp |
| `z~fl_oz_uk⁴·y~var·r~barn⁴/µ~qt_uk³` | 9.9999999999999991e-199 | **refused** | within 1 ulp |
| `r~chUS⁴/R~ha⁴·R~tn_sh³` | 1e-297 | 4.6e-8 relative error | within 1 ulp |

The middle row is the sharper failure: the factor is an ordinary 10⁻¹⁹⁸, both
sides underflowed to zero on the way, and the conversion came back refused —
while `bvn_unit_convert_rational` performed it exactly. `bvn_unit_reduce` had the
same defect in its `*scale`, which underflowed to **zero** with `*overflow` left
`false`, so a caller multiplying by it lost every value it touched. (Both
`bovnar.h` and doc/08 already said a scale "out of float range" sets the flag;
only the `isinf` half was implemented.)

The unit engine now accumulates through `bvni_scaled_t` — a mantissa and a
separate binary exponent, renormalised after every multiply. Powers of two are
exact in binary floating point, so the split costs nothing, and overflow and
underflow are decided once at the end, where they are a property of the answer
rather than of the order the components happened to be written in.
`bvn_unit_convert_factor` divides in that representation too, so a representable
**ratio** is no longer refused because neither operand is representable alone.

Found by a randomised sweep of 600 000 compound units checked against the exact
rational path; `test_extreme_compounds_keep_full_precision` pins all three.

### Added — `pptr` and `ppq`, the two ratios below `ppb`

The dimensionless ratio family stopped at `ppb`, and five profile codes were
refused for that one reason: `ucum:[pptr]`, and `udunits:ppt` / `pptv` / `ppq` /
`ppqv`. Both publishers state them exactly (10⁻¹² and 10⁻¹⁵) and agree with each
other. All five now map.

**The symbol is `pptr`, not `ppt`**, for two independent reasons. `ppt` already
resolves — as the compact form of `p~pt`, the picopint — and `units.bvnr` forbids
a new alias that changes what an existing spelling means (`gen_units.py` refuses
it at build time). And `ppt` is ambiguous in the field: parts per **thousand** in
some industries, parts per **trillion** in atmospheric chemistry, 10⁹ apart. UCUM
splits the same ambiguity the same way (`[ppth]`, `[pptr]`), so the symbol is
borrowed rather than invented.

The bare token `ppt` joins `.compact_exceptions` and is now `error_unit_illegal`:
reading a dimensionless ratio as a *volume* is the failure this format exists to
prevent, and no table lookup can settle which ratio was meant. `p~pt` still means
the picopint, `‰` still means per thousand.

### Fixed — five profile refusals the catalogue had outgrown, and one that named a unit it has

A refusal is written once and nothing re-asks it, so it outlives the reason. Six
did:

- `ucum:[mil_us]`, `[srd_us]`, `[smi_us]` and `[sct]` were refused under a
  comment reading "bovnar carries the survey foot and nothing else built on it" —
  after the nine US survey units above them had landed. Each is exact:
  `m~inUS`, `rdUS²`, `miUS²`, `miUS²`.
- `qudt:AC-FT` matches native `ac·ft` **exactly** and was refused as "no native
  unit of this dimension is a decade away", which is a statement about single
  units; an acre-foot is a product of two.
- `udunits:EC_therm` and `om:therm-EC` were refused as "native thm is the US
  therm" and "bovnar has no unit of this magnitude" — both written before
  `thm_ec` existed. Both publishers state the EC therm rounded to six digits
  (1.05506e8 J against the exact 105 505 585.262 J), and both state the US therm
  exactly in the same file, which is what a rounding rather than a different unit
  looks like. Waived by name in `check_profile_factors.py`.

`qudt:AC-FT_US` stays refused, with a reason that now says why: QUDT's 1233.484266
is neither the international acre-foot (1233.48183755) nor the survey one
(1233.48923847), i.e. an acre and a foot from different systems.

### Fixed — `°C/h` was reported as the value being out of range

Reading `°C/h` and asking `want_unit` for `K/h` raised
`error_value_out_of_range`, about a document whose value is `1.0`. doc/05 §12.4
and doc/08 §1.10 both promise `error_unit_mismatch`, and the reader has to tell
two failures apart that the conversion engine reports identically: a pair whose
units genuinely disagree, and a pair it agrees about whose exact factor outgrows
`bvn_int_t` (`Q~m¹⁰⁰·Q~g¹⁰⁰` needs 10^12000).

It told them apart by asking `bvn_units_convertible` — which is a **screen**, and
passes an affine scale in a compound by design: `s/°C` is dimensionally `s/K`.
So the affine case fell into the capacity branch and came back as a complaint
about the number, sending a caller to look at their value instead of their unit.

`bvni_unit_affine_misplaced` now asks only *where* the affine component sits — at
an exponent other than 1, or beside another component — which no magnitude can
confuse. `°C/h`, `°C·m`, `°C²` and `s/°C` are `error_unit_mismatch` in either
direction; the capacity refusal keeps `error_value_out_of_range`; and a lone `°C`
at exponent 1 still converts. Seven cases pinned in `bovnar_unit_policy_test.c`,
which gained a `want_unit` driver: the hook's contract differs from a policy's
(a policy names a preference and delivers the value untouched, a hook names a
demand and the parse stops) and only the policy half was covered.

### Fixed — three claims in doc/09's `UnitFlags.REDUCE` section, and a gate for the rest

Reading doc/09 line by line against the library:

- §5.2 called `unit_reduce` "reduce a compound unit to its **canonical named SI
  unit**". It never produces one: `unit_reduce(parse_unit("k~g·m/s²")).unit` is
  `m·g/s²`, and `N` is what `unit_to_str_ex(…, UnitFlags.REDUCE)` gives. The
  named-SI collapse is the formatter's, exclusively.
- §5.3 told a caller to apply `unit_reduce(vu).scale` to a value it is also going
  to serialise — the same double-application fixed in doc/05 and doc/08 last
  release: `k~N` emits `"k~N"` with nothing to rescale while `unit_reduce`
  reports `1000.0`. The section now carries the three-line Python recipe, run
  verbatim to produce this entry.
- §5.3 said a reduction overflows the **±9** exponent range. It is ±100:
  `m⁵⁰·m⁵⁰` reduces to `m¹⁰⁰`, `m⁵⁰·m⁵¹` raises.

Everything else in §5, §5.1, §5.5, §5.6 and §8 was verified correct, including
the asymmetry §5.1 calls out — `unece:KMH` parses to `k~m/h`, and
`unit_to_profile("unece", k~m/h)` refuses it, because a flat vocabulary spells one
whole code and has no operator to build a compound from.

**`check_doc_python_examples.py`** now evaluates doc/09's `# →` expectations
against the library. Nothing had ever run them: 76 KB of Python was the copy no
gate executed, and all three defects above lived in it. Blocks needing a file, a
handler or an optional import are skipped, and the skipped count is printed on
every run so that "nothing is checked any more" cannot look like success.

### Fixed — the tutorial and the FAQ described the wrong dimensionless encoding

doc/03 §11.8 has it right and doc/01 §6.4 and doc/02 §4 had it backwards.
Omitting the unit parameter **inside an annotation** yields the same
`BVN_UNIT_NONE` (`num_components == 0`) that writing `no_unit` does —
`bvn_parse_type_annotation` initialises the unit to that and only overwrites it
when a dimensioned parameter is present. It is a **fully untyped** value, with no
annotation at all, that gets the one-component `bu_none` form from default-type
synthesis.

Both documents said the opposite, which reads as harmless until somebody
switches on `num_components`. The difference is invisible from Python (the
binding exposes no `num_components`) and invisible from a document (all three
serialise to `"no_unit"` and compare compatible), so nothing else could have
caught it; `test_the_three_dimensionless_encodings` asks
`bvn_parse_type_annotation` directly, and reproducing the old claim makes it
fail.

The tutorial also said a compound unit may hold **8** components. It is 32
(`BVNR_MAX_UNIT_COMPONENTS`), as the spec, the FAQ and the constraints table all
say, and as 32 parsing and 33 refusing confirm.

### Fixed — the unit-string bound explained itself with numbers two catalogues old

`include/bovnar.h` said the worst case is "798 bytes + NUL … the longest
prefixable canonical symbol (`fl_oz_uk`, 8 bytes)". `footlambert` (11 bytes)
overtook it, and the real figure is 862; the generator's own bound is 895. The
macro `BVNR_UNIT_STRING_MAX` (1088) was never wrong — the generators recompute
the bound from `src/gendata` and fail the build if it is exceeded — but the
comment a reader learns the shape from had outlived two catalogue growths.

`test_longest_unit_fits_the_declared_bound` had gone stale the same way: it built
its worst case from `bu_fluid_ounce_uk` and kept passing while no longer
exercising the extreme. It now **sweeps every native unit** for the longest
emission, asserts the maximum fits with its NUL and that one byte short refuses,
and reports which symbol produced it — so a catalogue change that moves the
extreme is visible in review instead of silently un-tested. The header comment
now states the shape and points at the generators for the figure, rather than
carrying a number that ages.

### Fixed — three documents named the wrong error for an over-long unit

doc/05 §8 and §14, doc/03 §12.8 and doc/08 §5 all said an over-long unit string
raises `error_unit_too_long`. It does not, in the place a reader is most likely
to hit: a unit written in an **annotation** reaches the type-annotation body's
own 255-byte cap first — that cap counts the family name, the width and the
commas as well, so a unit parameter can never be the only thing over the line —
and raises `error_type_too_long` (21). `error_unit_too_long` (22) is the
**inline** suffix's buffer, which has one to itself.

doc/11 §2.5 tabulates all three caps and had it right; the other three documents
were restating it from memory. All four now agree, and `check_doc_unit_factors.py`
provokes eleven unit error codes against the reader — including both halves of
this pair, so neither can be restated as the other again. Confirmed by
reproducing the old claim and watching the gate fail.

### Changed — `qudt:HP_Brake` is refused, as a QUDT modelling error

It was mapped to `hp_B` on the strength of QUDT's own multiplier. QUDT's code and
its multiplier describe different units: brake horsepower is the mechanical
horsepower measured at the shaft — 745.7 W, which QUDT already carries as `HP` —
and QUDT gives `HP_Brake` 9809.5 W, which is its own `HP_Boiler` to the digit.
One of the two is a mistake and nothing in the vocabulary says which, so
following the number carries a boiler rating under a shaft-power name and
following the name contradicts the only value QUDT states. Refusing is the third
answer, and the one that costs a producer an error message instead of a factor of
thirteen.

### Fixed — both documents told a direct caller to multiply by a thousand twice over

`bvn_unit_to_string_ex(…, BVN_UNIT_REDUCE)` folds a compound into the named unit
it spells out, and the value has to move with it. doc/05 §12.2 and doc/08 §3.3
both said:

> A direct caller must apply `bvn_unit_reduce`'s `scale` to its own value.

That is the scale to the **fully reduced** unit, and the formatter does not
always emit the fully reduced unit: where the reduction lands on a named SI unit
it re-attaches the prefix. So `k~N` comes back `"k~N"` with nothing to rescale
while `bvn_unit_reduce` still reports 1000, and `k~g` comes back `"g"` where the
1000 must be applied. The two are indistinguishable from outside — both a lone
unit carrying a kilo prefix — so a caller following the sentence is out by 1000
on the first of them.

The library was never wrong; `bvn_ser_reduced_number` converts to the unit
actually emitted and its own comment names the trap ("using `bvn_unit_reduce`'s
raw scale there would multiply by 1000 twice over"). Only the public contract was
wrong, and only in the dangerous direction. Both sections now give the eight-line
recipe the writer runs, and two tests pin it: one tabulating the five cases where
the two scales differ, and a 20 000-unit randomised sweep asserting that a
reduced spelling denotes the same quantity it was written from.

### Added — 17 more profile codes the 35 new units unblocked

A second sweep after the units landed found refusals whose reason had become
false — including some the additions themselves invalidated. `om:franklin` and
`qudt:FR` are the statcoulomb; `om:point-TeX` and `om:pica-TeX` are the printer's
point and pica to a part in 10⁸; `qudt:LA_FT` is the foot-lambert; `qudt:HP_Brake`
carries QUDT's own 9809.5 W, which is `hp_B` exactly.

The tropical and Gregorian years unblocked the rest: `cf:age_of_sea_ice` and
`cf:sea_water_age_since_surface_contact` are `yr_trop`, the two sea-level
tendencies are `m/yr_trop`, `udunits:eon` is `G~yr_trop`, and OM's `gigayear`,
`reciprocalYear`, `cubicMetrePerYear` and `millisecond-AnglePerYear` build on
`yr_greg`. Four tests that asserted those refusals now assert the translation.

`qudt:MI_US2` maps to `miUS²` — its name says US survey and its value agrees.
Plain `MI2` stays refused, with a reason that now records why it cannot be
decided: QUDT's 2 589 997.766 sits 2.7e-7 from the survey square mile and 3.7e-6
from the international one, so the value points one way and a trade-code "square
mile" conventionally the other.

### Added — the other 35 evidenced units, and 87 more profile codes that map

The corroboration sweep found 35 refusal groups **two or more vocabularies
define**; the nine in the entry below were the first pass and these are the rest
that name a real unit. What is still refused is a *constant* rather than a unit
(UCUM's `[pi]`), a value only one publisher states (`[Btu_39]`), or a
dimensionless "relative permeability" that is a number.

| Group | Units |
|---|---|
| Calendar and sidereal time | `yr_trop` `yr_greg` `yr_sid` `yr_com` `mo_syn` `mo_trop` `mo_sid` `d_sid` `h_sid` `min_sid` `s_sid` |
| CGS electrostatic | `statV` `statA` `statC` `statF` `statΩ` `statH` `statS` |
| Luminance | `Lmb` `apostilb` `footlambert` |
| Water-vapour permeance | `perm_0C` `perm_23C` `perm_m` |
| Heat conventions | `cal_m` `cal_15` `cal_20` `Btu_59` `Btu_60` `Btu_m` |
| Singles | `e` `sph` `cml` `unit_pole` `hp_W` |

87 profile codes across UCUM, UDUNITS-2, QUDT and OM now map instead of being
refused — the catalogue's named refusals fall from 2013 to 1926 — with 0
mismatch against the publishers' own files.

**Where the publishers disagree, the exact definition decides.** The CGS-ESU
units are built on `c`, which the 2019 SI fixed, so every one is an exact
rational and the publishers' five- to seven-digit decimals are roundings of it:
`statΩ` is 22468879468420441/25000 here against 898 755 400 000, 898 755 200 000
and 898 760 000 000 in UDUNITS, OM and QUDT. Three QUDT rows round past the
7.5e-7 tolerance and are waived by name, each with two other publishers stating
the precise value as the evidence. The sidereal hour, minute and second are
derived from the sidereal **day** rather than taken from the three slightly
inconsistent decimals the publishers state, so the four stay consistent with each
other. The π-based rows carry `.exact = false`, as the oersted and the parsec do.

**`yr` is still the Julian year and `mo` still a twelfth of it.** Naming the
other calendars does not make the short spellings ambiguous — it gives them
somewhere to go. A UDUNITS or CF document that says `year`, and they say it
constantly, could not be read **at all** before; it now reads as `yr_trop` and is
still not `yr`. Four tests that asserted those refusals now assert the
distinction instead, which is the thing that must not be lost.

`sph` is 4π steradian, so it joins the steradian's quantity kind in
`bvni_kind_table` and in the pint bridge — without that a solid angle would
convert to a plain number, and the bridge's own agreement test said so.

### Added — nine units two and three vocabularies define, and doc/07's own gate

A refusal reading "bovnar has no unit of this magnitude" is a request when one
publisher makes it and **evidence** when three make it independently at the same
value. Grouping every profile refusal by (dimension, value) turned up 35 such
groups; these nine are the ones whose publishers agree to the last digit they
state and whose value is exactly representable:

| Symbol | Unit | Factor | Named by |
|---|---|---|---|
| `pnt_pr` | printer's point | 0.0003514598 m (= 0.013837 `in`) | UCUM, UDUNITS, OM |
| `pca_pr` | printer's pica | 0.0042175176 m | UCUM, UDUNITS, OM |
| `hp_E` | electric horsepower | 746 W exactly | UDUNITS, QUDT, OM |
| `hp_B` | boiler horsepower | 9809.5 W | UDUNITS, QUDT, OM |
| `abV` | abvolt | 10⁻⁸ V exactly | UDUNITS, QUDT, OM |
| `AT` | assay ton (short) | 175/6 g | UDUNITS, QUDT, OM |
| `bsh_uk` | imperial bushel | 0.03636872 m³ (= 8 `gal_uk`) | UCUM, QUDT |
| `clo` | clo | 0.155 K·m²/W exactly | UDUNITS, QUDT |
| `debye` | debye | 10⁻²¹/c C·m, exact since 2019 | QUDT, OM |

26 profile codes across four vocabularies now map instead of being refused, and
`check_profile_factors.py` reports 0 mismatch against the publishers' own files.

Each is a **near neighbour** of a unit already present, which is why each needed a
row rather than an alias: `pnt_pr` is 0.37 % off `pnt` (the DTP point), `hp_E`
0.04 % off `hp`, `bsh_uk` 3.2 % off `bsh` (the US *dry* bushel), and `hp_B`
thirteen times any horsepower. Folding one onto its neighbour is the error doc/11
§6.3 warns about — dimensionally perfect, inside anything a later check would
notice, and wrong. `doc/05` had said the printer's point "is still refused"; that
was a statement about it being a different unit, which is an argument for giving
it a symbol rather than for having none, and it is the argument the US survey
lengths already won.

**`check_doc_ambiguities.py`** puts every claim doc/07 makes to the reference
parser — §2 and §3's two-readings rows, §4's and §13's refusals, §5's
prefix-vs-unit symbols, and §9's accept-list, refuse-list and worked conversions.
doc/07's header has always promised "every row here was checked against the
reference parser"; nothing re-asked, and a table of REFUSALS is the one thing
that cannot keep that promise on its own, because a refusal rots the moment the
catalogue grows. §9's "families, in full" sentence is checked for **closure**, and
that check earned itself immediately: adding `hp_E` and `hp_B` above put two new
members in the kg·m²·s⁻³ family, and the build failed until the sentence said so.
The factor gate caught a second one the same day — a `9806.5` typed for `9809.5`
in a table this commit was adding.

### Fixed — doc/07 §9 said "(factor 1)" about a rule that is not about factors

The section listing the units Bovnar converts between despite their meaning
different things — `Gy`/`Sv`, `Hz`/`Bq`/`Bd`, `W`/`VA`/`var`, `J`/`N·m` —
introduced them as converting "(factor 1)". That is true of the pairs it names
and not of what the compatibility check accepts, which is the whole **dimensional
family**. A reader took away "the worst case is a mislabelling"; the worst case
is a different number:

| Written | Read as | Returns |
|---|---|---|
| `1 Ci` | `Hz` | 3.7×10¹⁰ |
| `1 rem` | `Gy` | 0.01 |
| `1 hp` | `var` | 745.6998715822702 |
| `1 cal` | `N·m` | 4.184 |
| `1 rpm` | `Bq` | 1/60 |

§9 now states the families in full — `Hz` `Bq` `Bd` `Ci` `rpm`; `Gy` `Sv` `rem`;
`W` `var` `VA` `PS` `hp` `ton_ref`; the ten energy units — and says where the
line is actually crossed: only on an explicit conversion request, since
`BVN_UNIT_REDUCE` never substitutes one named unit for another. It also records
why these get no quantity kind when `b`/`B` and `lm`/`cd` do: a kind needs a
*unit* to carry it (`sr` does the work for photometry), and a dose weighting
factor is dimensionless and 1 by convention — so a kind on `Sv` would have to
refuse `Sv` ↔ `J/kg`, and one on `VA` would refuse `VA` ↔ `V·A`, while leaving
them accepted would make `Gy` ≡ `J/kg` ≡ `Sv` with `Gy` ≢ `Sv`, which callers
screening on `bvn_units_convertible` cannot rely on.

doc/07's header promises "every row here was checked against the reference
parser", so `check_doc_unit_factors.py` now performs all five conversions and
checks each family is both mutually convertible and **closed** — a new native
unit of one of those dimensions joins the family whether or not anybody updates
the sentence, and that is now a build failure rather than a silent one.

### Added — three gates over the unit documentation, and the drift they found

**`check_doc_unit_factors.py` — the Factor column.**
`check_doc_unit_tables.py` compares roughly 1150 documented rows and says in its
own header that it does not compare the Factor column, "prose as much as data".
That is the one column a reader *uses*, and it turned out to be a small grammar
rather than prose. All 381 cells are now evaluated in exact rational arithmetic
(with π carried symbolically) and compared against `units.bvnr` — value,
**dimension**, and the `(exact)` claim, since calling a rounded decimal exact is
wrong even when the digits agree. It found five truncated factors (`slug`,
`ft_lb`, `hp`, `prln`, `prz` — `hp` was documented as 745.69987158227, a
different double from the catalogue's 745.6998715822702) and a section of doc/05
written in ASCII-degraded notation (`m2`, `4.462e-4 mol.m-2`, `` `acUS`.`ftUS` ``,
and a mangled `` `ftUS`squared ``) while both documents use `m²`, `·` and `×10⁻³`
everywhere else. The same gate now also requires every `bovnar_si_units.h` export
to be *named* in doc/05: four were not, including `bvn_units_convertible` and
`bvn_unit_si_normal_form` — the two the reader's unit policy is built on, and the
two whose header comments say every hand-written `want_unit` hook screens its
targets wrongly without them. Both are now documented in §12.4.

**`check_doc_profile_atoms.py` — doc/11 §6.1's completeness.**
§6.1 opens with "What follows is the whole mapped list" and it was not: 188 UCUM
codes were mapped and 155 appeared, so a third of the profile was invisible in
the one place that promises completeness. Worse, the same section said the US
survey series "is **refused**" while §6.3, a hundred lines down, correctly listed
all nine as mapped — the document contradicting itself about a capability the
library has had for some time. Every row's target and factor is now checked
against the reader, and a mapped code missing from §6.1 is a failure.

**`check_doc_counts.py` — the phrasings and the id-space bounds it missed.**
doc/05 §3 opened with "Bovnar supports 180 named physical base units" against an
actual 215; the gate's five fixed phrasings covered four of the five adjective
orderings and missed that one. The claim is now an optional adjective run.
Added alongside: the id-space **bounds**, which are count claims wearing a
different hat and had been left behind by a growing catalogue —
doc/04's "Native units 100000–100179 (180)" and doc/05's "`bu_bit` = 100000 to
`bu_long_hundredweight` = 100191". `check_doc_unit_tables.py` gained the matching
check for doc/09 §18, whose BaseUnit range table stopped at 100179 and omitted 35
members outright, and whose `CUP` note cited enum values 81 and 167 — two
generations of id space ago, against today's 100080 and 900033.

### Changed — the licence now says what it can and cannot grant

**Read this if you redistribute Bovnar.** `LICENSE` gains clauses 4 and 5, and
clauses 1–3 become subject to them. The MIT grant covers this project's own
contribution — the native unit registry, the translation targets, the refusal
rationales, the generators, the code and the prose. It does **not** cover the
identifier strings the unit-profile tables carry from UCUM, QUDT, OM 2,
UDUNITS-2, the CF standard name table and UN/ECE Recommendations 20 and 21, nor
the further material Part 2 of the notices inventories — the website's fonts and
libraries, the imagery, the committed toolchain output, an algorithm the date
routines derive from, the code of conduct's adapted text. It never did;
the file said otherwise, which offered recipients a right to sublicense and sell
material this project has never held a right in.

The new `THIRD_PARTY_NOTICES.md` records, per source: publisher, the exact
published version each table is verified against, the licence, what was
extracted, and the modification statement CC BY 4.0 requires. **It must
accompany any redistribution** — `pack_artifacts.cmake` copies it into every
binary archive and the amalgamation drop, the Python wheel carries it under
`dist-info/licenses/`, and `dist/wasm/README.md` (the README npm publishes)
points at it. Two of these obligations bind binary distributions specifically:
UDUNITS-2's BSD-3 clause 2, and CC BY 4.0's attribution, whose §6(a) terminates
the grant on breach. Nothing about the library's own MIT terms changed, and no
vocabulary has a ShareAlike clause, so this reaches no downstream code.

Two questions are open and are recorded as open rather than settled. UCUM's
licence is revocable and bars derivative works of its table; written permission
for the derived mapping is being sought, and `-DBVNR_WITH_UCUM_PROFILE=OFF`
drops it meanwhile. The CF standard name table states no licence at all — the
conventions *document* is CC0, the table is maintained separately and carries no
rights statement — and a declaration has been requested.

Alongside: the webfonts now ship the SIL Open Font License text the OFL requires
to accompany every copy (`web/fonts/OFL.txt`), the Impressum gains a
*Bildnachweis*, `doc/11` gains §18, and `CITATION.cff` gains a `references:`
block naming all six vocabularies with their licences.

### Fixed — two documents said an irrational factor still aborts; it does not

`bvnr_inexact_leave` delivers a value untouched instead of aborting when the
conversion cannot be exact. Both places it is described — `bovnar.h`'s
`bvnr_unit_inexact_policy_t` and doc/06 §2.4 — said it

> Applies only to a result that is exact as a rational but has no terminating
> expansion in the output base; **a genuinely irrational factor still aborts**,
> since there is nothing exact to hand over either way.

It does not abort. With the mode set, a π-based angle is left in its native unit
like any other value the conversion cannot deliver:

```
--unit rad                    on 90 °   ->  error_unit_inexact
--unit rad --leave-inexact    on 90 °   ->  delivered untouched
```

The implementation is deliberate and `test_irrational_is_left_under_leave`
already pinned it — with a comment naming the confusion exactly: *"The
alternative reading — that an irrational factor is special because there is no
rational to hand over — belongs to `want_unit_allow_nonterminating`, whose
fallback IS the rational. This flag's fallback is the native value, and that
works for an irrational factor exactly as well as for a non-terminating one."*
The stale sentence describes a different knob.

It also contradicted the mode's own stated purpose. `bvnr_inexact_leave` exists
for `bvnr_normalise_si`, "where every value is a conversion candidate and one
5/18 factor would otherwise reject an ordinary document" — and a document
carrying a heading in degrees is entirely ordinary. Under the strict reading
`--si --leave-inexact` would reject it; it does not.

Both sites corrected, with a transcript (doc/06 promises its transcripts are
output rather than illustration, and this one is), and both now also state the
half neither did: only a **policy-chosen** target takes this path, since a target
named by the `want_unit` hook keeps its strict all-or-nothing contract whatever
`on_inexact` says.

### Added — `available_profiles()`, and the namespace count five documents got wrong

**The bindings could not report which profiles a build carries.** Every unit
profile is a compile-time switch, so which of the seven a given `libbvnr` has is
a property of *that build*. The C side exposes `bvn_unit_profile_count` /
`bvn_unit_profile_name` for exactly this, and doc/11 §9.4 presents them as how a
consumer tells "this build has no ucum" (`error_unit_profile_unknown`) from "that
is not a unit" (`error_unit_illegal`). Nothing in Python called them, and a
caller who installed a wheel had no way to ask.

`bovnar.available_profiles()` returns them in registry order:

```python
>>> bovnar.available_profiles()
('ucum', 'unece', 'qudt', 'qudt-qk', 'udunits', 'om', 'cf')
```

**And the count was wrong in five places.** `om` and `cf` shipped, and the
sentence that names the namespaces did not follow:

| | said | |
|---|---|---|
| doc/05 §2 | **one** — "One namespace is defined, `ucum`" | |
| doc/09 §5.1 | five | the Python reference |
| doc/11 §2.1 | five | while doc/11's own header says **seven**, 160 lines above |
| IETF draft (.md, .txt, .xml) | five | the normative document |

Every one understated what the library does, and doc/11 managed to contradict
itself within one file. The draft's `om` citation needed a bibliography entry
that did not exist, so `OM2` is added to all three renderings that carry
references.

`check_doc_counts.py` gained the gate. The count is spelled as a **word** in
every one of those sentences, which is why 46 numeral-based claims sailed past
it; it now reads the word forms too, and checks 53 claims. Mutation-checked.

### Fixed — the Python bindings could not format a unit the library had just written

Every unit formatter in the bindings allocated **256 bytes** and passed **256**
as the capacity — two separate literals, neither of them a bound on anything. The
C side allows `BVNR_UNIT_STRING_MAX` (1088), and a perfectly legal 32-component
unit reaches 597 bytes once its exponents render as superscripts:

```python
"·".join(["da~ton_ref^-100", "da~cal_IT^100",
          "da~Btu_th^-100",  "da~fath^100"] * 8)
```

C formats that. Python raised `BovnarArgumentError: unit_to_str: output buffer
overflow` — on a unit its own library had produced, with no way for a caller to
work around it. Six call sites were affected: `unit_to_str`, `unit_to_profile`,
`unit_to_ucum`, `units.unit_to_str_ex`, `dom.unit_str` and the writer's
annotation path.

`UNIT_STRING_MAX` now lives in `bovnar.structs` beside `MAX_UNIT_COMPONENTS`,
carrying the same "must match include/bovnar.h" note, and all six sites take both
the buffer and the capacity from it. Pinned by a test that builds the unit from
long symbols at three-digit exponents rather than asserting a fixed string, so it
keeps testing the bound rather than a spelling; mutation-checked by putting the
constant back to 256.

### Fixed — doc/11's UCUM refusal tables listed ten codes that now map

§6.4 tabulates what a UCUM code does and §6.3 explains the near-misses in prose.
Both are hand-maintained copies of what `src/gendata/ucum.bvnr` decides, and both
went stale as the registry grew — during **this** release, from the units added
for exactly these codes:

| documented | actually |
|---|---|
| `[Btu]`, `[Btu_th]` refused | map to native `Btu_th` |
| `cal_IT` refused | maps to native `cal_IT` |
| `[ch_us]`, `[acr_us]` refused | map to `chUS`, `acUS` |
| `[dr_ap]`, `[lb_ap]` refused | map to `dr_ap`, `lb_t` |
| `[ch_br]`, `[ft_br]`, `[yd_br]` are `error_unit_illegal` | named refusals — `error_unit_profile_unsupported` |

Nothing was broken by it: the docs **understated** the profile, which is the
direction nothing notices, because no test fails when a capability goes
unclaimed. A reader planning a clinical or survey corpus would have concluded
those codes were unusable and written their own translation.

§6.3's four near-miss paragraphs are rewritten against the implementation. Two of
them now describe a better outcome than they used to: the BTU and calorie traps
are closed by *carrying both conventions* (`Btu` is the IT one, `Btu_th` the
thermochemical, and UCUM's unqualified `[Btu]` is thermochemical so it lands on
the latter) rather than by refusing, and the whole US survey series maps to its
own native units instead of being fenced off at 2 ppm from the international one.

§6.4's table gains the rule its rows had stopped illustrating: **`error_unit_illegal`
is the last row and only the last row.** Every atom UCUM defines is mapped,
profile-only, or named in the refusal list, so a real code never falls through as
"not a code" — that outcome is reserved for a typo. This is what §15 already
claimed and what §6.4's examples had quietly stopped showing.

`check_profile_factors.py` gained the gate: it extracts every "`code` …
`error_unit_*`" claim in §6 — 40 of them — and checks each against the reader.
Table rows are read as a list, since one outcome governs every example in the row
and taking only the token nearest the error code is how the stale rows survived a
reading in the first place. Mutation-checked against the original defect.

### Added — every embedded example is checked, not a tenth of them

`check_doc_examples.py` parsed only the fences opening with `#!bovnar` — **24 of
212**. The directive is optional, so the tutorial and the spec mostly omit it and
those 188 blocks were never looked at. All of them are classified and checked
now:

```
157 embedded document(s) parse, 46 refuse as marked, 9 illustrative
```

Classifying them turned up **nothing broken**, which is the useful result: 136 of
the 188 already parsed, and every one of the 52 that did not was a deliberate
negative example (`# error: starts with a digit`, `# WRONG: annotation must come
after '='`) or a depiction rather than a document. Two of them were rejected
*design alternatives* in `doc/temperature_difference.md` — syntax considered and
not adopted — which must not parse for the document to make its point.

Two things the exercise established, recorded in the tool for whoever adds the
next example:

- **A profile unit needs the directive.** `<float:64,udunits:m s-1>` is
  `error_unit_illegal` in a document declaring nothing, because spec 1.2 is where
  profile units exist (doc/03 §11.7 says so). Several spec fragments show one
  without a directive.
- **`\xNN` in the docs depicts bytes**, and is not syntax an octet stream
  accepts. Those blocks were never files.

Also worth preferring: five blocks show a refusal with the offending line
**commented out** (`# .bad = <float:64,m> 1.5 s;   # ERROR`). Those parse, stay
unmarked, and keep the block runnable while still showing the reader the bad
form. Where it fits, it beats a `rejected` marker.

Mutation-checked both ways — removing a `rejected` marker from an invalid block
fails, and adding one to a valid block fails.

### Fixed — the unitless fence was documented in one direction only

`bvn_policy_selects` fences unitless values off from the ratio units **both
ways**, and its source comment says so:

> a policy naming "%" would otherwise convert a bare `0.25` into `25 %`, **and
> one naming `no_unit` would turn `35 %` into `0.35`**

The implementation is symmetric (`BVN_UNIT_IS_UNITLESS(native) !=
BVN_UNIT_IS_UNITLESS(target)`), and the test asserts both directions. Only the
two places a *user* reads stated half of it: `bovnar.h`'s `targets` field and
doc/06 §4.2, whose heading was literally "A bare number matches only no_unit".

The missing half is the surprising one. `no_unit` as a **target** does not strip
units off dimensionless values — there is no policy setting that turns `35 %`
into a bare number, by design, because that is the same silent
factor-of-a-hundred from the other side. A reader wanting "deliver everything as
plain numbers" would reach for `--unit no_unit`, get a document back unchanged,
and find nothing in either document explaining why.

Both sites now state the fence in both directions, with a transcript of what each
target does to a bare number, a `%` and a `ppm`, and the reminder that `ppm -> %`
is unaffected — the fence is about values carrying no unit at all, not about
converting dimensionless things.

No code or test change: the behaviour was right and already covered by
`test_unitless_is_fenced_from_the_ratio_units`. This was documentation catching
up with an implementation that had been careful first.

### Fixed — a unit rule that matches nothing was silently satisfied, and undocumented

`bvnr_rule_require` / `--require-field` **does not require the field to exist**. A
rule whose path no value sits at makes no claim, and the document passes:

```
--require-field .inlet.speed=m/s   on a document with .inlet.speed   asserted
--require-field .inlet.sped=m/s    on the same document              ACCEPTED, checks nothing
--require-field .nope.*=m          on a document with no .nope       ACCEPTED, checks nothing
```

So a typo in a path — or a rename the policy was not updated for — turns that rule
off and nothing says so. The behaviour itself is defensible and is left alone;
what was wrong is that it was **unwritten**, in a contract that goes to unusual
lengths over the case next door. The header already spent two paragraphs
explaining that a value the reader cannot *locate* is `error_unit_mismatch`
because *"I could not check it" is not "it holds"* — and never said what happens
when the field is simply absent, which is the case a user actually hits, from a
typo, and which behaves the opposite way.

Both are now stated together in `bovnar.h` and doc/06 §2.1, with the distinction
named: a rule that *found nothing to check* is fine; a rule that *could not check
what it found* is an error. The consequence is spelled out, because it runs
against what the name suggests.

Pinned by a test that asserts the pair — a hit both ways round, then four ways of
naming nothing (a typo'd leaf, an absent top-level key, a path deeper than any
value, an absent subtree), each with a unit the document's one value would
*fail*, so a pass can only mean the rule never fired rather than fired and agreed.
This is a silent behaviour: nothing breaks when a rule stops applying, so without
this nothing would report a change in either direction.

### Fixed — a quoted number literal lost its unit, and was not rescaled

Two defects in the writer, both from the same cause: a **quoted number literal**
is a `token_is_string` carrying a numeric annotation, and two things that belong
to every numeric value lived only under `token_is_number`.

**The inline unit was dropped.** With no flags set at all, on an ordinary write:

```
.a = <uint:64,_16> "7" k~m;    ->    .a = <uint:64,_16> "7";
```

Seven kilometres rewritten as a bare, dimensionless seven. A value losing its
unit is the one outcome this format exists to make impossible. The append was the
last statement of the number branch and nothing else called it; it is now a
helper (`bvn_ser_append_inline_unit`) that both branches use.

**`BVN_UNIT_REDUCE` reduced the unit without moving the value.** The annotation
is written by the common path and was reduced regardless, so:

```
<uint:64,_10,k~m> "7"       ->   <uint:64,_10,m> "7"
<float:64,_16,k~m> "1p0"    ->   <float:64,_16,m> "1p0"
```

Seven kilometres written back as seven metres, silently, with a successful
return — exactly what `bvn_ser_reduced_number`'s own comment says it exists to
prevent (*"the annotation said one thing and the digits another"*). It is now
called on the string path too, and the rescale is exact in the value's own base:

```
<uint:64,_2,k~m>  "111"  ->  "1101101011000" m      (7000 in binary)
<uint:64,_36,k~m> "z"    ->  "r08" m                (35000 in base 36)
<float:64,_16,k~m> "1p0" ->  "3e8" m                (1000 in hex)
```

**Why this shape and not an exotic one:** the spec *requires* the quoted form for
every non-decimal base (§4.6 — `e` is a hex digit, so a base-16 float must be
written `"1.8p+2"`). So the quoted literal was not a corner case; it was the only
way to write a base-16 value at all, and every one of them carrying a prefixed
unit was exposed — the first defect on any write, the second through
`pretty-print --canonical`.

Both are now tested across bases 2, 10, 16 and 36 for `uint` and `float`, with
the inline-unit case asserted separately (unit on the value, no annotation — an
annotated unit takes the other path and never showed the bug), and with a plain
`utf8` string asserted untouched so the new call sites cannot start rewriting
text. Each test was mutation-checked against its own fix.

### Fixed — `W/(m²·ΔK)` and `W/(m²·K)` were not the same unit, though four documents said so

The temperature-interval kind is scoped to **a lone component at exponent 1** —
the one place `bvn_unit_to_si_factor` applies an affine offset, and so the only
place a difference could ever be read as a scale. Everywhere else an affine scale
cannot appear at all, so a `K` in a compound was already an interval and there is
nothing to separate. `doc/temperature_difference.md` §4.2 states the rule, tabulates
the cases, and records that the alternative was **considered and rejected**:

```
ΔK        vs  K          incompatible    the hazard, and the whole point
ΔK/k~m    vs  K/k~m      the SAME unit   a lapse rate
W/(m²·ΔK) vs  W/(m²·K)   the SAME unit   a U-value
```

`bvni_kind_exponents` applied the scoping, so `bvn_units_convertible` agreed —
those pairs convert with factor 1. **Equality did not.** It compares bases
structurally and `bu_delta_kelvin` is not `bu_kelvin`, so the two spellings the
design calls one unit were separated one layer up from the kind scoping. Since
the annotation/inline agreement rule compares with `bvn_unit_equal`, a document
was refused for writing both:

```
.u_value = <float:64,W/(m²·ΔK)> 0.25 W/(m²·K);   ->  error_unit_mismatch
.lapse   = <float:64,ΔK/k~m>    6.5  K/k~m;       ->  error_unit_mismatch
```

Exactly the U-value and lapse rate the scoping rule exists to keep working. Both
now parse. The hazard is untouched: a lone `ΔK` at exponent 1 is still neither
equal nor compatible with `K`.

Only `ΔK` folds. The other five interval units are not spellings of a kelvin —
`Δ°F` is 5/9 K, and its scale counterpart is affine and unusable in a compound
anyway — so there is no pair to merge, and the test asserts that too.

The existing test is why this survived: it asserted `bvn_units_compatible` under
the message *"ΔK and K are the same unit inside a compound"*. The claim was in
the message and the weaker predicate was in the assertion, so the gap was
invisible. It now asserts `bvn_unit_equal` beside it, and asserts the lone case
stays separated — a fix that folded `ΔK` into `K` unconditionally would have
satisfied every other line and destroyed the reason the unit exists.

### Fixed — doc/11's profile spelling table understated the writer

Two hand-maintained claims about `bvn_unit_to_profile`, neither compared to it,
both stale in the direction nothing notices — they claimed *less* capability than
exists, and no test fails when a capability goes unclaimed:

- the §5.3 table showed `Mi~B` with no UN/ECE spelling (it is `E63`) and `mph`
  with none at all (UN/ECE `HM`, QUDT `MI-PER-HR`);
- the prose listed `mph` among the units that "have nowhere to go", beside `kph`
  and `rpm`, which genuinely have none.

A reader takes that as the answer and writes their own translation, or concludes
the profile is thinner than it is. The table gains a `kph` row so the contrast is
explicit — `mph` has two spellings, `kph` has none — and the prose now says
membership is per-unit rather than per-family.

`check_profile_factors.py` gained the gate: it parses that table and that
sentence and checks all 28 claims against the built writer.

### Fixed — a conversion too large to compute was reported as a unit mismatch

`want_unit` refused a pair the library itself calls **convertible**, and blamed
the units for it:

```
Q~m¹⁰⁰·Q~g¹⁰⁰  ->  q~m¹⁰⁰·q~g¹⁰⁰    convertible=YES   reader said: unit_mismatch
```

Same two bases, same two exponents; only the prefixes differ. The conversion is
defined and the factor is 10¹²⁰⁰⁰ — past `BVN_INT_MAX_BITS`, so
`bvn_unit_convert_rational` returns false. The reader mapped *every* false
return to `error_unit_mismatch`, which tells a consumer their units are
incompatible when they are not. That is the one diagnosis a consumer acts on
differently: a mismatch means fix the units, a range error means the value
cannot be carried.

Capacity now reports `error_value_out_of_range`, the code already documented for
a literal too extreme to build a rational from. A genuinely incompatible pair
still reports `error_unit_mismatch`, and the test asserts both halves — a fix
that collapsed them into one code would have destroyed the distinction while
passing the new check. Nothing was ever computed with truncated digits: the
arbitrary-precision helpers report their own overflow and the engine refuses on
it. `bvn_unit_convert_rational` and the `want_unit` contract now name capacity as
a failure mode; they had listed only "incompatible" and "structurally invalid".

### Fixed — a profile row no grammar could reach, and the constant it widened

`udunits:astronomical_unit_BIPM_2006` sat in `.mapped` and could never be read.
UDUNITS is an **expression** profile, where a trailing run of digits is an
exponent — `udunits:m2` is m², `udunits:m100` is m¹⁰⁰ — so the scanner took
`2006` as the exponent and asked the atom table for `astronomical_unit_BIPM_`,
which is not a code.

It cost more than itself. At 27 bytes it was the longest code in the profile, and
it drove the worst-case emitted string to 1032 bytes — over the then-current
`BVNR_UNIT_STRING_MAX` of 1024, which is why that constant was raised to **1088**.
Every buffer sized from it grew to hold a mapping that could not happen. With the
row out of the emitted set the profile needs 936. The constant **stays at 1088**:
a maximum larger than the tables need costs a caller nothing, and shrinking a
published constant is churn no defect asks for.

The row is now a named refusal rather than deleted, so a producer who sends it is
told "known, and not carryable" instead of "that is not a code". UDUNITS spells
the same unit `au`, `ua` and `astronomical_unit`, and all three work.

Two gates, because the failure has a general shape and a specific one.
`gen_profiles.py` refuses an expression-profile code that ends in a digit, at
generation time, before a table exists. `check_profile_factors.py` round-trips
**every** code in every profile against the built library — 10 762 mapped, 66
opaque — which catches a row that reads back as the *wrong* unit too, something
a shape rule cannot see. Neither existed: `check_targets_parse` proved a row's
`.bovnar` target parses and nothing ever asked whether its `.code` did.

### Fixed — two named refusals delivered the wrong diagnosis

`ucum:10*` and `ucum:10^` are `.unsupported` rows reading "the number ten — a
numeric base for UCUM exponents, not a unit", and reported `error_unit_illegal`.
The rest of the family reported `error_unit_profile_unsupported`:

```
ucum:10      unsupported        ucum:10*   ILLEGAL
ucum:10*3    unsupported        ucum:10^   ILLEGAL
ucum:10^-6   unsupported
```

A truncated numeric base hit the decade path and failed before the unsupported
table was consulted, so a producer got "that is not a code" for the truncation
and "known, not carryable" for the complete form. The whole family now answers
with one voice, and the read-back gate above checks all **2050** named refusals
report the code they declare.

### Fixed — the pint bridge's mapping was complete but ungated

Nothing asserted that `BASE_UNIT_TO_PINT` covers every native unit. The parity
sweep iterates the **map**, so a unit added to `src/gendata` and wired into
`BaseUnit` but never given a pint token is simply absent from it and every
assertion still passes; the bridge ships incomplete and the hole surfaces at a
user's runtime as `no pint mapping for bovnar base unit code NNNNNN`. Two
closures added — the map against the enum, and the bridge's hand-written
`_AFFINE` set against what the library actually reports as affine — completing
the chain `test_enums.py` starts at `src/gendata` → `BaseUnit`.

The sweep itself was looser than it looked. `checked > 100` was its floor against
a real total of 209, and three `except: continue` arms could drop a unit out of
the comparison unseen. It now asserts every row is either compared or named as
affine, and that the two account for all of them. A dead exclusion set
(`{'byte', 'bit', 'decibel', 'neper'}`, which never matched because the map
spells them `bvnr_byte` and friends) is gone: all four were being compared and
agreeing the whole time.

### Fixed — the spec's own version-directive examples were all rejected

doc/03 states that a directive followed by "trailing junk" is
`error_invalid_spec_version`, and four lines later showed:

```
#!bovnar 1.1        # current — accepted
```

The annotation *is* the trailing junk. All four lines it labelled "accepted" are
`error_invalid_spec_version` as written; strip the comment and all four parse as
labelled. Conformance case **VER-013** asserts the rejection, so the reader, the
suite and the prose agreed and the example disagreed with all three. The
outcomes are now a table, and the directive lines stand alone.

The same error opened **README.md** and **web/index.md** — the first bovnar
document most readers ever see, propagating into `web/index.html` and
`web/llms-full.txt`. Every other line in that block carries a legal trailing
comment, so the construct looked legal by induction; it is not, on line 1 only.

`check_doc_examples.py` is new: it hands every ```bovnar fence that holds a whole
document to the reference reader — 21 of them — and checks the directive line of
every fence, including ones exempted from parsing. Blocks that are sketches
(README's `.payload = \x00 … binary stream … \x00` is a placeholder, not syntax)
or deliberate refusals are marked with an HTML comment rather than guessed at,
and a block marked as a refusal that starts parsing cleanly is also a failure.

### Fixed — the IETF draft's grammar rejected units the library emits

The draft's **prose** was current: Table 8 says "one to three digits",
`^[+-]?[0-9]{1,3}`, and gives `m¹⁰⁰` as an example. Its **Appendix A ABNF** — the
machine-readable half an implementer builds from — still had the pre-widening
grammar:

```abnf
caret-exp = "^" [ "+" / "-" ] %x31-39     ; one digit, 1-9
sup-exp   = [ sup-sign ] sup-digit         ; one superscript digit
sup-digit = ... U+00B9, U+00B2, U+00B3, U+2074-U+2079   ; U+2070 absent
```

So the document disagreed with itself, with the wrong half being the normative
one. `m^10`, `m^100`, `m^-100` and `m^12` are all outside it, and the library
does not merely accept them — it **emits** them: exponent 100 is written back as
`m¹⁰⁰`.

Two further holes in the same block, both from the draft being the only document
that **enumerates** the characters a symbol may use (doc/12 defers to the
registry, which is why doc/12 never rotted):

- `base-unit-char` omitted **U+0394 Δ**, excluding all eight temperature-interval
  spellings — `ΔK`, `Δ°C`, `Δ°F`, `Δ°De`, `Δ°N`, `Δ°Ra`;
- it had no **DIGIT** rule at all, excluding `mH2O`.

Nine spellings the library accepts and emits, rejected by its own specification.

`check_ietf_draft.py` gained the ABNF spellings of the exponent mistake — it
matched only the three *prose* phrasings, which is exactly how the fix landed in
Table 8 and missed Appendix A — and now checks `base-unit-char` against all
**646** registry spellings from `src/gendata`, so the enumeration cannot drift
behind the registry again.

Also reworded: §8.2's *"m/s/s is therefore m·s⁻², not m"*. The "not m" half is
right — they are not even convertible — but `m/s/s` carries three components to
`m·s⁻²`'s two and the two compare **unequal**, while every neighbouring "is" in
that section is an exact equality (`k~g/(m·s²)` **is** `k~g·m⁻¹·s⁻²` **is**
`k~g/m·s²`, all verified).

### Fixed — the writer spelled the dimensionless unit in five namespaces that cannot read it

`bvn_unit_to_profile(ns, u, …)` with a **dimensionless** `u` returned success and
wrote `"1"` for all seven namespaces. Only two can read that back:

```
ucum:1  udunits:1   -> the dimensionless unit
unece:1  qudt:1  qudt-qk:1  om:1  cf:1   -> error_unit_illegal
```

`1` is a production of the **expression** grammar — a bare integer factor of 10⁰,
which is why UCUM and UDUNITS round-trip it. A flat vocabulary has no
integer-factor production and none of the five defines `1` as a code. Two of them
go further and refuse their own unity code *on purpose*: QUDT's `UNITLESS` and
`NUM` and OM's `one` all sit in `.unsupported` reading "the absence of a unit —
write no unit at all". The writer contradicted the policy those tables state.

The sharpest case is `cf`, which is **read-only** and generates no reverse table
at all — `src/gendata/cf.bvnr` says of itself that `bvn_unit_to_profile("cf", u)`
"returns -1 for every unit", and it did, for every unit except this one: the
`num_components == 0` branch returned above the writability check rather than
below it, so exactly one unit escaped the guarantee.

The branch now asks whether the profile can spell unity at all — expression
grammar, and a reverse table that is not just its sentinel. Nothing that had a
spelling lost one. `test_the_dimensionless_unit_writes_only_where_it_reads`
asserts the contract per namespace rather than as "if it writes, it reads back",
because here the interesting half is the refusal. The existing sweep could not
have caught it: it walks the native registry, so every unit it builds has exactly
one component and the empty one is never constructed.

### Fixed — `CITATION.cff` failed schema validation, silently

One line — `abbreviation: BVNR` — sat at the **top level**, where CFF 1.2 sets
`additionalProperties: false`. The key is real and legal *inside* a `references:`
entry; at the top level it invalidated the whole document. Nothing reported it,
because that file's only readers are GitHub's citation widget, Zenodo and
`cffconvert`, and they degrade quietly rather than raising an error the
repository can see. BVNR is named in the abstract instead.

`check_citation.py` gates the key spelling now — dependency-free, because no gate
here may need pip or the network, and explicit about being a spelling check
rather than a schema validator (`cffconvert --validate` stays the full one).

### Added — a PARTIAL profile build is tested, not only all-off

`bvnr_profiles_off_build` drove one of the 128 configurations the seven
compile-time switches describe: all of them at `0`. That is the configuration
where the *compile* is at risk — an empty registry array, a table that loses its
last reference under `-Werror` — and it was well covered.

The mistakes that need a **mixed** build were not reachable there, because none
of them can happen when the answer is uniformly "none":

* `bvn_unit_profile_name(i)` must skip absent profiles rather than walk them.
  With every profile off the count is 0 and the walk is vacuous.
* The blocked id space must not **compact** when a block is unused:
  `bu_unece_one` stays `300000` with `unece` absent, or two builds disagree
  about what an id means — the one thing the block layout exists to prevent.
* An opaque unit whose profile is absent is still in the opaque range, so still
  incommensurable, and has no spelling — the writer must refuse it rather than
  reach for another namespace.
* An absent namespace is `error_unit_profile_unknown` while a bad code in a
  *present* one is `error_unit_illegal`. A mixed build is the only place both
  are observable at once.

`tests/profiles_partial_smoke.c` pins all four, compiled against the
amalgamation under the same strict flags, with `ucum` (an expression profile
that owns opaque units) and `qudt-qk` (a flat one that owns none) on. The test
driving both configurations is renamed `bvnr_profile_configs`, since it no
longer drives only the off one. Each new assertion was mutation-tested.

### Fixed — the pint bridge refused units the library it bridges represents

`from_pint` carried the literals **8** and **9** — `value_unit_t`'s component
count and `unit_exponent_t`'s range as they were when it was written. Both grew,
to **32** and **±100**, and the literals did not. `python/bovnar/structs.py` had
the right count the whole time; the two Python files simply disagreed, and
nothing compared them.

The consequence was an **asymmetry**: `to_pint` exported `m¹⁰⁰` and
nine-component units happily, and `from_pint` then refused them, so
`bovnar → pint → bovnar` failed for anything past the old limits — with a
message that stated the old numbers as "bovnar's range". A test even pinned
`meter**12` as unrepresentable, which was the bug rather than the rule.

Both bounds are now imported from `bovnar.enums` and `bovnar.structs` rather
than restated. The test pins the **round trip** instead of either number, so a
future widening cannot break it and a future narrowing of the bridge alone
cannot pass it.

### Fixed — two normative documents described an exponent form the library does not use

doc/03 §11.5 stated the ASCII caret form as `^[+-]?[1-9]`, "single digit", and
omitted `⁰` (U+2070) from the superscript table; the IETF draft said "One digit
1-9 only" in all three renderings. The range has been `[-100, 100]` for some
time, and the library does not merely accept `m^100` — it **emits `m¹⁰⁰`** as
the canonical spelling of exponent 100. So a conforming parser built from either
document rejects documents this implementation writes, which is the sharpest
form this kind of drift takes. doc/12's EBNF and doc/05 §6 had it right
throughout.

Three worked examples were wrong the same way: doc/01, doc/03 and doc/05 each
showed a **nine-component** unit annotated `error_unit_illegal (9 > 8)`. All
three parse — the limit is 32, which doc/03 itself states correctly three
sections later. A reader copying any of them got a valid document the comment
called invalid.

### Added — the exponent range is gated, and the gate covers six documents

`check_ietf_draft.py` already compared the error-code enum and the component
limit against `include/bovnar.h`. It did not compare the **exponent range**,
which is exactly why `[1-9]` outlived the widening, and its component matcher
only recognised the limit stated as a sentence — never as the right-hand side of
`9 > 8`, which is how all three stale examples wrote it.

Both are fixed, and the document set grew from four files to nine: the tutorial
and the bindings reference restate these constants too and were both wrong. The
presence requirement stays on only for the normative documents — a tutorial need
not state a limit — while any figure a document does state is checked. Each of
the six original mistakes was mutation-tested against the new gate.

### Added — coverage for seven unit-carrying entry points that had none

`bvnr_write_uint_unit` and `bvnr_write_float_unit` were exercised in four files.
The other six unit-carrying writers were public API with **no caller anywhere in
the tree** — not a test, not the Python bindings, not the fuzz harnesses:

```
bvnr_write_sint_unit        signed integers
bvnr_write_float_fix_unit   fixed point
bvnr_write_float_dec_unit   decimal floats
bvnr_write_bvnf_unit        arbitrary-precision floats
bvnr_write_bvnf_base_unit   ... in a chosen base
bvnr_write_bvni_unit        arbitrary-precision integers
```

Those are the value families this format exists for, so "it compiles" was the
whole of the guarantee on the path that attaches a unit to a 128-bit integer.
They turn out to work; nothing said so, and nothing would have said otherwise.
Each is now written, read back, and checked on the two things that fail
independently — the value survives, and the unit survives *as a
`value_unit_t`*, not as text that merely looks similar.

`bvn_dom_node_from_bigint` was in the same state, and it is worse to leave
there: it is the only DOM constructor that takes a unit, and its header states
an **asymmetric ownership contract** — on success the node takes the
`bvn_int_t`, on failure the caller keeps it. That is exactly the shape that
leaks or double-frees when nobody checks. Both halves are pinned now, with the
refusals sharing one integer the test frees itself, so a leak checker has
something to say if a refusal ever starts consuming it.

### Fixed — the writer emitted a code that read back as a different unit

`bvn_unit_to_profile` joins a prefix to an atom with **nothing between them** —
there is no `~` in UCUM or UDUNITS — and the reader then resolves that one token
by its own rules, whole atom before prefix+atom. Where the concatenation happens
to *be* an atom, the code means something else, and the writer reported success:

```
k~t (kilotonne)  -> udunits:kt   -> the KNOT, a speed
p~H (picohenry)  -> udunits:pH   -> the ACIDITY scale
f~t (femtotonne) -> udunits:ft   -> the FOOT
p~t (picotonne)  -> udunits:pt   -> the PINT
n~t (nanotonne)  -> udunits:nt   -> the NIT, cd/m²
a~t (attotonne)  -> udunits:at   -> the TECHNICAL ATMOSPHERE
```

Twenty-four of the nine thousand codes the writer can produce. `kt` is the sharp
one: `units.bvnr` refuses that exact token on **input**, in `compact_exceptions`,
because "reading a speed as a mass is exactly the failure this format exists to
prevent" — and the writer was emitting it.

A code is now assembled only if it reads back, checked against the parser's own
resolution order rather than against a restatement of it. When the shortest
prefix spelling collides the next is tried — UDUNITS spells kilo both `k` and
`kilo`, and `kilot` is unambiguous — so nothing that had a spelling lost one:
`k~t` writes as `kilot`, `p~H` as `picoH`, while `km`, `mg` and `mt` keep their
short forms. `test_written_codes_read_back` sweeps the whole registry at every
SI prefix and four exponents through all seven namespaces and asserts the
property directly; a table of cases would not have found this, because the
collisions are an accident of two vocabularies' spellings meeting.

### Added — 23 units, and the ~90 publisher codes they unblock

Each was the **sole** reason a run of UCUM, UDUNITS-2, QUDT, OM or UN/ECE codes
had to be refused. All are exact; the ones whose value is not a terminating
decimal in SI state a rational rather than the repr of a double.

* **The US survey lengths** — `inUS`, `ydUS`, `fathUS`, `rdUS`, `chUS`, `lkUS`,
  `furUS`, `miUS`, `acUS`. `ftUS` had been in the registry all along and nothing
  was built on it, so UDUNITS' `chain`, `rod`, `pole`, `perch`, `furlong`,
  `fathom` and `acre` — which that vocabulary builds on the survey foot — had to
  be refused rather than mapped onto the international lengths of the same name.
  The survey foot is 2 ppm longer and the survey acre 4 ppm larger: small enough
  to ignore and never small enough to be right.
* **The typographic point, pica and line** — and *not* under the symbols `pt`
  and `ln`, because `pt` is the pint. A length answering to `pt` would have been
  the same collision as `kt` for the knot.
* **The US dry gallon, quart and pint**, 16 % larger than the liquid ones the
  registry already had; the peck and bushel here were always the dry ones.
* **Board foot, cord and the survey acre-foot**, the trade measures whose
  factors (144 in³, 128 ft³) are not a decade off anything.
* **darcy, EC therm, ton of refrigeration, Dobson unit and shake** — the last
  because there is no SI prefix at 10⁻⁸, and the darcy because it is exact:
  every unit in its definition is.

Every one of the 90 new profile mappings was checked against its publisher's own
file; the run reports zero mismatches, zero quantity-kind disagreements and zero
cross-reference disagreements.

### Added — a symbol may not redefine a spelling that already exists

`RT` was proposed for the ton of refrigeration. `R` is the ronna prefix and `T`
the tesla, so `RT` had been the ronnatesla since prefixes and units first met,
and the longest-alias rule would have handed the token silently to the new unit.
`TR` is no better: `T` is tera and `R` the roentgen.

`gen_units.py` now refuses any alias that is a prefix followed by another unit's
spelling. The nineteen that predate the check are listed as what they are — the
rule's other half, where the whole token wins deliberately and always has, which
is why `min` is the minute and `cd` the candela. What must not happen is a *new*
alias joining that set, because then a spelling changes hands. The proposal was
caught by diffing 40 000 spellings before and after; the gate makes that a build
failure with both readings named.

### Fixed — a stray brace in a data file hung every generator

An unmatched `}` inside an array made `bvnr_data.py` spin forever: a closing
delimiter is one of the characters `_value()` reports as the empty slot in
`[1, , 3]`, so it returned None without consuming anything and the loop asked
again. One duplicated line in `units.bvnr` turned every generator into a hang —
no message, no exit, and in CI a timeout rather than a diagnosis. An array is
closed by `]` and by nothing else, and it says so now.

### Fixed — three more profile mappings, found by comparing two of our own tables

The quantity-kind gate added last pass compares a QUDT unit against the QUDT
quantity kind it is filed under. A defect survives that by being consistent
*within* its own vocabulary, and three were:

```
qudt:NP-PER-SEC       -> s^-1        a neper per second is not a reciprocal second.
                                     QUDT files it under a kind literally named
                                     "Unknown", so there was nothing to compare it to.
qudt:IU-PER-MilliGM   -> µ~mol/k~g   QUDT models the international unit as an amount of
                                     substance THROUGHOUT, so nothing internal to QUDT
                                     objected. UCUM declares [IU] arbitrary and bovnar
                                     carries it as an opaque unit commensurable with
                                     nothing — one library, two answers, and an IU is
                                     assay-defined (0.3 µg retinol, 0.025 µg vitamin D).
qudt:S_Ab             -> a conductivity   QUDT gives the absiemens the dimension vector
                                     of the siemens per METRE, and a matching quantity
                                     kind, so both internal checks agreed with it.
```

What settled all three was QUDT's own `ucumCode`: `Np/s`, `[IU].mg-1`, `GS`.
`check_cross_reference` makes that a gate — 2640 published cross-references
compared, each asking whether bovnar's translation of a QUDT code and of the
UCUM or UDUNITS code QUDT gives for it are the same unit. It is the first check
here that compares two of *our* tables rather than one of ours against a
publisher, which is exactly the axis a self-consistent publisher error hides in.

Eighteen disagreements are QUDT's own and waived by name: a terawatt-hour whose
code reads `TW/h`, a gram per degree Celsius written `d/Cel` (the day), a thermal
conductivity whose UCUM form has lost its foot, `fl oz` for the fluid ounce —
where a space multiplies in UDUNITS and `fl` is a femtolitre. It then caught
three more the moment new rows were added, all publisher slips: a mass unit
cross-referenced to `fldr` (a volume), and two codes with a cubed or missing
minute.

### Fixed — a per-field rule stopped asserting when a document got deep

A key path is recorded to a bounded depth and length. Past either, the reader
cannot say where a value sits — and a policy rule naming that field silently
stopped applying. `targets` on the same document kept working, so nothing looked
wrong.

The path machinery is careful never to report a position it is unsure of, which
leaves two honest answers, and this took the quieter one. It is the silence the
mechanism refuses everywhere else: the header says a value a rule cannot be
applied to is `error_unit_mismatch` because "silence would defeat the point of
naming it", and a rule that stops asserting because a document nested past 32
structs is that defeat with nothing to notice it by. Both the reader and the
writer now refuse an undecidable rule. `targets`, `normalise`, `require_unit`
and `require_dimension_of` ask about the value rather than about where it sits
and are unaffected, so a document of any depth still reads normally under those.

### Added — 100+ refusals an existing native expression already covered

A refusal is written once and nothing re-asks it, so a reason that was true
becomes a reason that is stale — most obviously when the unit it named as
missing is later added. Sweeping every profile's `.unsupported` list against the
native registry found:

* **a degree Celsius inside a compound**, refused in `om` and in half of `qudt`
  while `qudt:DEG_C-PER-SEC` had always read `K/s`. K in a compound *is* the
  interval, so `om:degreeCelsiusPerHour`, `qudt:DEG_C-PER-HR`, `PER-DEG_C`,
  `BAR-PER-DEG_C` and the Fahrenheit and BTU_IT families now read K and Δ°F, as
  their already-mapped siblings do;
* **a ratio of two of the same unit** — `om:metrePerMetre`, `gramPerGram`,
  `kilogramPerKilogram` — refused as "dimension one" while `unece:3H` spells
  exactly that as `k~g/k~g`. The compound keeps *a ratio of what*;
* **units the registry had under another spelling**: `om:are` is a
  centihectare, `om:poundApothecaries` the troy pound, `om:degreeReaumur` the
  Réaumur degree that was there all along, `qudt:DeciM3-PER-MIN` a litre per
  minute;
* **compounds the registry can build**: a poundal is `lb·ft/s²`, a footcandle a
  lumen per square foot — *not* `cd/ft²`, which is what matching on dimension
  alone picks and is a luminance;
* **`unece:DRA`, `unece:LBT`, `qudt:DRAM_US`, `LB_T`, `Hundredweight_UK`**,
  which the apothecary dram, troy pound and long hundredweight unblocked.

The sweep is now part of `check_profile_factors.py` as a `--verbose` advisory,
so a refusal that goes stale is visible rather than permanent. Advisory rather
than fatal, and for a stronger reason than the coverage suggestion beside it:
matching on value is not proof. A dimensionless refusal matches the first
dimensionless native unit by construction, and `om:shake` "matches" `c~P/bar`
because the dimensions happen to agree. It is a list to read, not to apply.

### Fixed — seven profile mappings turned an information quantity into a frequency

`qudt-qk:BitRate` translated to **`Hz`**. `ByteRate` to `Hz`. `BitTransmissionRate` and
`ByteTransmissionRate` to `s⁻¹`. `LinearBitDensity` to `m⁻¹`. `DataRate` to `Hz`. And
`qudt:ExaBIT-PER-SEC` — reached from `unece:E58` as well — to `E~Hz`, with `qudt:NAT-PER-SEC`
(`unece:Q19`) to `s⁻¹` beside it. A document annotated `qudt-qk:BitRate` came out of the parser
carrying a unit the library itself calls **incompatible** with the `b/s` it meant.

The cause is one blind spot, and it is the same one `bvni_kind_table` exists to cover: a bit per
second and a hertz have the same dimension vector (T⁻¹) and the same factor (1). The derivation
that filled these tables compared exactly those two things, found the coherent unit of that
dimension, and wrote down the hertz. Two more of the same shape sat in the photometric rows —
`PowerPerAreaAngle` and `TotalRadiance` came out as **W/m²**, irradiance, beside a `Radiance` that
correctly carried the steradian.

Every one is now the unit its code names, or refused:

```
qudt-qk:BitRate                  b/s ... refused: QUDT names the bit and lists only OCTET-PER-SEC
qudt-qk:ByteRate                 B/s
qudt-qk:LinearBitDensity         b/m
qudt-qk:DataRate                 b/s
qudt-qk:PowerPerAreaAngle        W/m²·sr        (agreeing with Radiance)
qudt:ExaBIT-PER-SEC              E~b/s
qudt:NAT-PER-SEC                 refused — a nat has no native unit
```

`BitRate`, `BitTransmissionRate`, `InformationFlowRate`, `RotationalFrequency` and
`RotationalVelocity` are **refused** rather than mapped, because QUDT pools quantities bovnar keeps
apart: it gives `BitRate` one applicable unit and that unit is the octet per second, and it lists
the hertz and the revolution per second under one rotational kind although they differ by exactly
2π. Where a vocabulary does not say which of two incommensurable units it means, an error message
is the honest answer.

### Added — a gate on the one axis the factor comparison cannot see

`check_profile_factors.py` compares a publisher's factor and dimension vector against the native
target's, ten thousand rows of it. A dimension vector has **no room for a quantity kind** — a bit, a
radian, a steradian, a decibel and a pure ratio are all `[0,0,0,0,0,0,0]` — so every one of the
seven mappings above scored perfectly for as long as it existed.

`check_quantity_kinds` closes it, and needs no new source: QUDT already states, for 2798 of the
units it defines, the quantity kind that unit measures, and bovnar maps both vocabularies. A kind
translates to the coherent unit of the kind, so a unit **of** that kind must be convertible with it
— and `bvn_units_convertible` is the predicate that compares kinds as well as dimensions. 2567
unit/quantity-kind pairs are checked on every run; a disagreement fails the run like a factor
mismatch. Three links are waived by name, each because QUDT files a unit under the wrong kind (a
lumen per square metre is a lux, not a luminance), and each is printed rather than skipped.

### Added — the information families the profiles refused, and 26 UN/ECE codes with them

`unece:AD` is the byte. `unece:J63` is the bit. Both are among the most-used codes in Rec 20, and
both were refused — along with fifty-five siblings covering every decimal and binary prefix, every
rate and every linear density. QUDT was refused the same way: `BIT-PER-SEC`, `KiloBIT`, `MebiBIT`,
`SHANNON`, `OCTET`, `PetaBYTE` and forty-six more.

One number caused all of it. QUDT models information as **entropy**, so its bit is ln 2 = 0.693 and
its byte 8·ln 2, the coherent unit of entropy being the nat; bovnar's bit is 1. That is a modelling
difference and not a wrong conversion, and it had already been accepted for `BIT`, `BYTE` and eight
prefixed bytes — the rest of the family is the same difference at another decade or binary power.
Ratios were taken against QUDT's own `BIT` and `BYTE` rather than against 1, so every row is an
exact power of ten or of two off the atom it names and the ln 2 cancels.

Four rows are waived because QUDT does not follow its own model there: it states the pebibit,
exbibit, pebibyte and exbibyte at values 8.9 and 7.5 out from 2⁵⁰ and 2⁶⁰ of its own bit. The names
are unambiguous, so those map and the disagreement is recorded, exactly as UCUM's inverted phot is.

`unece:E86` and `E87` stay refused: the cross-reference attaches the tebibit per square and per
cubic metre the other way round from the way every other family in the block runs, and a secondary
source contradicting its own pattern is evidence rather than proof.

### Added — six native units the profiles needed, and the 100+ codes they unblock

Each of these was the **sole** reason a run of publisher codes had to be refused. The publisher
defines the unit, the value is exactly stateable, and there was simply no native unit to translate
onto.

| Unit | Exactly | Unblocks |
|---|---|---|
| `mH2O` | 9806.65 Pa (conventional column) | UCUM `m[H2O]`, `[in_i'H2O]`; the whole UDUNITS water-column family; `qudt:FT_H2O`, `MilliM_H2O`; `unece:K24` |
| `cal_IT` | 4.1868 J | UCUM `cal_IT`; UDUNITS `calorie`/`cal`/`IT_calorie`; nine QUDT rows; `unece:D70`, `D71`, `D72`, `D75`, `E14`, `K52`, `L14` |
| `Btu_th` | 23722880951/22500000 J | UCUM `[Btu]`, `[Btu_th]`; twenty QUDT rows; eleven UN/ECE codes |
| `lb_t` | 0.3732417216 kg | UCUM `[lb_tr]`, `[lb_ap]`; UDUNITS `troy_pound`, `apothecary_pound`, `appound` |
| `dr_ap` | 0.0038879346 kg | UCUM `[dr_ap]`; UDUNITS `apdram` |
| `cwt_l` | 50.80234544 kg | UCUM `[lcwt_av]`; UDUNITS `long_hundredweight`; `qudt:CWT_LONG`; `unece:CWI`; `om:hundredweight-British` |

**Two calories and two BTUs, and the pairs cross over.** `cal` is the thermochemical calorie and
`Btu` the international-table BTU — not an inconsistency but the vocabularies' own defaults, since
UCUM's unqualified `cal` is thermochemical while UDUNITS' unqualified `calorie` is the IT one. Each
pair is 0.067 % apart, dimensionally identical and numerically wrong if confused, which is why the
missing halves used to be refused rather than rounded onto the halves that existed. Both members of
both pairs are carried now and every code lands on the one its own vocabulary says it means.

`mH2O` takes prefixes, so `c~mH2O` (compactly `cmH2O`) is the centimetre of water a ventilator is
set in. UDUNITS states both its water and its mercury column as pressure **gradients** and builds
every pressure as a length times one, which bovnar can now say exactly — `mH2O/m` and `mmHg/m~m` —
so the expressions built on them fall out of the profile's own grammar. A column measured at a
stated temperature (`water_4C`, `qudt:IN_H2O`) is a *different* unit and stays refused.

The short hundredweight has no unit: it is exactly 100 lb, which the prefix mechanism already
spells `h~lb`.

### Fixed — the Python `BaseUnit` enum was six units short of the C one

`BaseUnit.DELTA_KELVIN` did not exist. Nor did the other five temperature intervals, so a Python
caller could not name a unit the C enum, the parser and the writer had all carried since they were
added — and `UNIT_NATIVE_LAST` reported the wrong end of the block. Nothing noticed, because the
only check was `len(BaseUnit) == 1 + 180 + 216` and the count had gone stale in exactly the same
direction as the enum. That is the failure this file already documents for the error codes: a
hardcoded number and the thing it counts go stale together and agree with each other.

The count assertion is replaced by four that close both blocks against `src/gendata` — every id,
every member name, the bounds, and nothing outside them. The pint bridge was missing the same six
units and gained a `[temperature_interval]` dimension for them, so pint now refuses `ΔK → K` as
bovnar does; its test's list of kind-carrying units, which had already been widened once when the
photometric units joined the angle kind, now asks the registry for *any* non-SI dimension rather
than for `[angle]` by name.

### Added — the catalogue counts stated in prose are gated against `src/gendata`

The tables were checked thoroughly — roughly 1150 documented rows, the generated C factors, the
profiles against their publishers. The **sentences** were not, so a stale unit count sat in the
README, the cheatsheet's scope line, the EBNF commentary, the IETF draft and the JOSS paper, and
the spelling total was out by more than fifty. A count in prose is invisible to a table check.

`check_doc_counts.py` reads the headline numbers back out of every document, comment header and
generator and compares them to what the data files hold — 46 stated counts across fifteen files.
`CHANGELOG.md` and the release notes are exempt: they record what was true at a version.

### Added — a temperature difference is a unit now: `ΔK` and five siblings

`25 °C` is 298.15 K. A *rise* of 25 degrees is 25 K — and there was no way to write that, so a
producer wrote `°C` and every consumer converted it as a scale reading. The two documents were
byte-identical, nothing in the pipeline could tell them apart, and the number was wrong by 273.15.
doc/11 §10.3 called it "the format's most concrete gap, worth more than this whole profile"; it also
said the fix belonged in the **native registry**, because importing it through a foreign notation
would put it somewhere no native document could reach. That is where it went.

Six base units (ids 100180–100185), eight spellings, one new quantity kind:

| Unit | Exactly | Also spelled |
|---|---|---|
| `ΔK` | 1 K | `delta_K`, `deltaK`, `delta_kelvin` — **and `Δ°C`**, `delta_degC`, `delta_celsius` |
| `Δ°F` | 5/9 K | `delta_degF` — **and `Δ°Ra`**, `delta_degRa`, `delta_rankine` |
| `Δ°De` | −2/3 K | `delta_degDe`, `delta_delisle` (Delisle runs backwards) |
| `Δ°N` | 100/33 K | `delta_degN`, `delta_newton_temperature` |
| `Δ°Re` | 5/4 K | `delta_degRe`, `delta_reaumur` |
| `Δ°Ro` | 40/21 K | `delta_degRo`, `delta_romer` |

`Δ°C` **is** `ΔK` and `Δ°Ra` **is** `Δ°F` — the Celsius interval is the kelvin (SI Brochure 9th ed.
§2.3.1) and the Rankine degree is the Fahrenheit degree, so those are aliases rather than rows of
their own: two units that must compare equal and convert by exactly 1 would be a distinction with no
content. There is deliberately no bare `delta_C` or `delta_F`, which read as a delta coulomb and a
delta farad.

Each is a **ratio** scale — `.affine = false`, `.offset = 0.0` — carrying a new
`BVNI_KIND_TEMP_INTERVAL` quantity kind, which is the mechanism that already keeps `M~b/s` out of
`M~B/s` and `lm` out of `cd`:

```
ΔK   → K       error_unit_mismatch    the hazard, and the whole point
°C   → Δ°C     error_unit_mismatch    only the author knows which was meant
Δ°F  → ΔK      factor 5/9             exact; lossless for multiples of 9, else unit_inexact
Δ°De → ΔK      factor −2/3            no new arithmetic: the registry already had negative factors
--si on 25 Δ°C                        25 ΔK, not 298.15 K
[<float:64,K> 1.0, <float:64,ΔK> 2.0] error_array_element_type_mismatch, from §7.4 alone
```

Being ratio scales is also what lets them into a compound at all: `Δ°F/mi` is a lapse rate, `Δ°Re^-1`
an expansion coefficient, where `°F/mi` and `°Re^-1` have no SI meaning whatever.

**The one judgement call, and it breaks nothing.** The interval kind counts only for a **lone unit at
exponent 1**, so `W/(m²·ΔK)` and `W/(m²·K)` are the *same unit*, as are `ΔK/k~m` and `K/k~m`, and
`ΔK^-1` and `K^-1`. The scope follows the hazard exactly: an affine offset is only ever applied to a
lone component at exponent 1, so that is the only place a difference could have been read as a scale
— a `K` inside a compound was already an interval. Counting the kind there would have separated two
spellings of one quantity and broken every U-value and lapse rate written to date. This asymmetry is
the only rule in the change that was not already somewhere in the tree, and `bvni_kind_exponents`
carries the reasoning beside it.

**No grammar change, no new error code, no ABI break** — the ids append inside the native block,
which is what its spare space is for, and `error_unit_mismatch` already says what a delta-to-scale
conversion is. `bvn_unit_si_normal_form` gained the one line it needed: an interval reduces to `ΔK`,
never to `K`, or the form would be screened out as incompatible and a normalising pass would silently
leave every temperature difference in a document alone.

**One profile row was wrong and is corrected.** `qudt-qk:TemperatureDifference` mapped to `K`, making
it the same unit as `qudt-qk:ThermodynamicTemperature` — the confusion the code's own name rules out,
and one that converts to °F wrong by 255.37. It maps to `ΔK`, and since no other quantity kind claims
`ΔK` it is the one row in that file's tail that reverses. A quantity kind states a quantity and no
unit, so nothing published is being diverged from. **The four CF standard names that are differences
by name** (`air_temperature_anomaly` and siblings) are deliberately left at `K`: CF *states*
`canonical_units = "K"` for them, and overriding a publisher's stated unit from the sense of its name
is a different decision — and one that would put `cf:air_temperature_anomaly` in disagreement with
`udunits:K` for the same variable. CF 1.12's own answer, a `units_metadata` attribute, is not in the
unit slot and still cannot be read by a profile; a converter is where that call belongs.

Thirteen conformance cases (UNT-065…UNT-077) and
`test_temperature_difference_is_its_own_quantity_kind`. The design record, including the two
mechanisms that were rejected — a `delta` type-annotation parameter and a general `Δ(…)` unit
operator — is [doc/temperature_difference.md](doc/temperature_difference.md).

### Fixed — a space inside a type annotation produced a different unit, silently

`<float:64,k g>` was accepted as `k~g`. `<uint:6 4>` was accepted as a 64-bit
width. `<float:64,udunits:m s-1>` — the space-multiplied spelling UDUNITS and CF
use, and the commonest one in netCDF metadata — was accepted as `udunits:ms-1`,
**reciprocal milliseconds**, for a value its author wrote as a speed. This was
the one place in the format where a wrong unit was produced silently rather than
refused, and the format's entire argument is that a wrong unit stops the parse.

The lexer skipped whitespace anywhere inside an annotation body, because
whitespace between parameters (`<uint:8, _16>`) is legal and by the time a
parameter was scanned the two positions were no longer distinguishable. They are
distinguished now, by state rather than after the fact:

* **legal, unchanged** — beside a separator: after the family keyword, either
  side of the family `:` or of a `,` between parameters, and before the closing
  `>`. A comment counts as whitespace, as it always did. `<float : 64>`,
  `<uint:8 , _16>` and `<float:64,m/s >` all still parse;
* **`error_type_param_whitespace` (52)**, new — inside a parameter, reported at
  the first byte after the whitespace;
* a `,` or `:` nested inside a UCUM annotation or bracketed atom is part of the
  code and not a separator, so it licenses nothing: `ucum:mL{cells,tot}` parses
  and `ucum:mL{cells, tot}` does not, which is what makes §3.4's promise to keep
  annotation text verbatim true.

**For a native parameter this is not a grammar change.** doc/12 has always placed
every `ws` inside an annotation beside a separator and never derived one in the
middle of a parameter; so does the IETF draft's ABNF. The implementation was
leniently wrong and now agrees with its own normative grammar, which is why the
fix is unconditional rather than gated on a spec version — gating it would leave
the hole open for every document that exists. An **inline** unit suffix was never
affected: whitespace ends that token, so `1.0 k g` has always been
`error_unexpected_input_byte`.

### Added — a space multiplies in `udunits:`, as UDUNITS and CF write it

Once the lexer could tell whitespace *between* parameters from whitespace *inside*
one, a third answer became available for the case that wanted it. Inside a
parameter carrying a profile **namespace**, whitespace is neither deleted nor
refused: it is kept **verbatim** and handed to the vocabulary.

```
udunits:kg m-2 s-1   →  k~g/m²·s   CF's commonest spelling of a flux, and the
                                   same unit as udunits:kg*m-2*s-1
udunits:m s-1        →  m/s        a speed
udunits:ms-1         →  m~s⁻¹      a reciprocal MILLISECOND — also valid UDUNITS,
                                   and a different unit. The space is the whole
                                   difference, and it is the reading this used to
                                   silently produce for the line above
ucum:mL{cells, tot}  →  m~L        an annotation is inert text and keeps its
                                   spacing, which makes doc/11 §3.4's promise
                                   true for the first time
ucum:[in i]          →  error_unit_illegal   from UCUM's grammar, not the lexer
qudt:Kilo GM         →  error_unit_illegal   a flat vocabulary has no operators
<float:64,k g>       →  error_type_param_whitespace   a NATIVE parameter, unchanged
```

`' '` joins `.` and `*` in the UDUNITS profile's multiplication set. It could not
have before: while the space was being deleted, the parser never saw one — it saw
`ms-1`, a perfectly good UDUNITS expression — so listing space as an operator
would only have made the wrong reading look supported. doc/11 §13.2, which
explained why the spelling could not be carried, now explains how it is.

Three limits, each stated in §13.2:

* **a run of spaces is not collapsed** — `kg  m-2` is two operators in a row and
  fails as `error_unit_illegal`. Collapsing in the lexer would have cost a UCUM
  annotation the spacing it now keeps;
* **the inline unit form cannot carry a space and never will** — whitespace is
  what terminates that token, so the space-separated spelling is available in a
  type annotation only;
* **a line break is still an error**, even in a namespaced parameter: no
  vocabulary spells a unit across a line, and admitting one would let a malformed
  code consume a document's layout.

The family `:` deliberately does *not* open a namespace — only a later `:` at
bracket depth 0 does — which is what keeps `<uint:6 4>` an error. Trailing
whitespace inside a namespaced parameter is trimmed, since it sits beside the
separator that ends the parameter.

**It also restores a diagnostic.** `udunits:days since 1970-01-01` reaches the
profile again and is refused as `error_unit_profile_unsupported` — "you wrote
valid UDUNITS and bovnar cannot carry a reference time" — rather than as a
whitespace error, which was the right refusal for the wrong reason.

Twenty-three conformance cases pin the whole boundary (TYP-042…TYP-055 and
UPR-043…UPR-043f), including the annotation/inline agreement between
`udunits:kg m-2 s-1` and `udunits:kg*m-2*s-1`, which passes only if both spellings
produce the same `value_unit_t`, and §14.2's pairing of `udunits:ms-1` against
`udunits:m*s-1` as two units that must not compare equal — a check that matters
more now, not less.

### Changed — a bare array is homogeneous in its UNIT, not in its dimension

§7.4 said numeric array elements must share the same physical dimension, and
closed by telling consumers that "a bare array of measurements is therefore
uniform — a consumer may treat its elements identically". Those two statements
were not compatible. Dimensional homogeneity admitted

```bovnar
.a = [<float:64,m> 1.0, <float:64,ft> 2.0];              # 0.3048 apart
.b = [<float_dec:64,$USD> 1.0, <float_dec:64,k~$USD> 2.0];   # 1000 apart
.c = [<float:64,°C> 1.0, <float:64,K> 2.0];              # 273.15 apart — an OFFSET
```

and a consumer that did what §7.4 permitted — read the block under one unit —
computed wrong numbers from all three. `.c` is what settles it: an affine offset
between two neighbouring cells is not a scale error anything downstream can
notice.

The check is now `bvn_unit_equal`, so §7.4's promise is true as written rather
than deleted. It is order-insensitive (`[m*s, s*m]` stays valid — unit
multiplication commutes) and an explicit `no_unit` still matches an omitted unit,
which the component count alone would have called different. **Struct fields are
untouched**: "shape uniform, fields free" still lets a multi-currency ledger carry
`$USD` in one record and `$EUR` in the next, because a record's fields are named
and read one at a time. Seven conformance cases were added (HOM-017…HOM-023).

Documents affected are documents where a consumer reading the array as a block was
already getting wrong answers; there is no spelling that has to change, only an
array that has to become a struct or gain a common unit.

### Added — one build switch per unit-profile vocabulary

`BVNR_WITH_UCUM_PROFILE`, `..._UNECE_...`, `..._QUDT_...`, `..._QUDT_QK_...`,
`..._UDUNITS_...`, `..._OM_...`, `..._CF_...` — all `ON`, so the default build is
the build every consumer already has. doc/11 §9.4 used to say the profiles were
unconditional and the switch did not exist; §15.3 then measured the price (the
binary grew 65 %, and the CF names added 780 KB more), and §10.4 carried the
switch as specified-and-not-built. It is built.

| all seven ON | all seven OFF |
|---|---|
| `libbvnr.so` 1.96 MB | **524 KB** |

**Nothing about the ABI varies with them.** The generators still run whole: every
`bu_*` id keeps its value, the dense unit tables and opaque blocks are unchanged,
and no struct or signature moves — so two builds with different switches are
ABI-compatible and differ only in which namespaces they translate. A namespace
that is compiled out is `error_unit_profile_unknown`, which is the answer §9.4
reserved that code for, and the same one an invented namespace gets: the document
is not wrong for naming `ucum`, and a build without `ucum` cannot read it either
way. The cost is symmetrical — an absent namespace is unwritable too, so
`bvn_unit_to_profile` and `bvn_unit_to_ucum` return −1 and a unit carrying that
vocabulary's opaque units has no spelling at all.

Two new entry points report what a build carries, so a consumer can ask instead of
inferring it from a failed parse:

```c
uint32_t     bvn_unit_profile_count(void);
const char*  bvn_unit_profile_name(uint32_t index);   /* NULL past the end */
```

`bovnar version` prints the same list. Because the switches are compile-time,
nothing in an ordinary build can reach the off state, so `bvnr_profiles_off_build`
compiles the **amalgamation** with all seven at `0` under an integrator's strict
`-Werror` flags and runs `tests/profiles_off_smoke.c` against it — which is what
catches the two things that actually break there: a registry array left empty (C99
has no empty initialiser; the table carries a sentinel row) and a table that loses
its last reference and trips `-Wunused-const-variable`. Each per-vocabulary test
suite is registered only when its own profile is on; the whole-corpus gates only
when all seven are.

### Fixed — `run_tests.sh` ran 81 of the 157 registered tests

The wrapper kept its own hand-written list of what to run, which is a second
copy of the CTest registry maintained by remembering to, and it had drifted:
every one of the seven unit-profile suites, every documentation and web gate,
the ABI dump, the amalgamation checks, the WASM freshness stamp and two thirds
of the CLI corpus ran only under `ctest` — while the wrapper printed "All tests
passed".

It keeps no list now. It builds, runs the whole registry through `ctest`, and
then does the two things CTest does not: sweep the fuzz harnesses at the
iteration count `--fuzz-iter` asks for (the registered fuzz tests are pinned to
fixed ones), and drive a cross-built tree through Wine. It also **asserts that
the number of tests run equals the number registered**, minus any marked
DISABLED, so a filter — including its own `--no-fuzz` — cannot quietly shrink a
run again.

### Added — two more unit profiles: `om:` (OM 2) and `cf:` (CF standard names)

Two vocabularies bovnar could not read now have namespaces of their own, taking
the last two free tags of the base-unit id space (70 and 80). Both are **flat**
profiles, both are gated on `#!bovnar 1.2` like every other profile, and both
contribute no opaque units — a document that does not use them is byte-for-byte
unaffected.

- **`om:`** — OM 2, the Ontology of units of Measure: 1255 local names mapped,
  205 refused with a reason. The vocabulary agrifood, food science, LCA and
  FAIR/ELN data use, and the one Wikidata's unit items align to. Its targets
  were **derived from OM's own composition** rather than read off the local
  name: OM states a prefix and a base, a numerator and a denominator, a term and
  a term, so `om:gramPerPetalitre` is `g/P~L` because the ontology says so.
- **`cf:`** — the CF conventions' standard names, all 5071 of standard name
  table v94: 4450 mapped, 621 refused. A standard name is a *quantity*, so it
  translates to the `canonical_units` CF itself states for it —
  `cf:air_temperature` is the kelvin because the table says `K`. This makes a
  netCDF/CF producer that holds a standard name and no units string able to
  write the value down at all.

**`cf:` is read-only, which is new.** `bvn_unit_to_profile("cf", u)` returns -1
for every unit and always will: sixty-nine standard names state the kelvin, and
writing a kelvin back as one of them would assert a quantity the unit does not
know. `gen_profiles.py` marks such a profile unwritable and emits no reverse
table, so the C writer reaches the refusal through its ordinary "no row names
this base" path — there is no special case, and a `.reverse` flag in such a data
file is now a build error.

Both tables are checked against their publishers by `check_profile_factors.py`,
which grew two resolvers of new shapes: OM states no multiplier, so its value is
computed by walking the composition down to the SI base units, and CF states no
units, so each name's `canonical_units` is evaluated with the same UDUNITS
evaluator the `udunits` profile is checked against. Across all seven profiles the
tool now compares **10263 rows with no mismatch**.

What the two vocabularies disagreed with bovnar about is refused rather than
rounded: OM's `year` is the **Gregorian** 31 556 952 s against native `yr`'s
Julian 31 557 600 s, and everything OM builds on it falls with it; CF's year is
UDUNITS' tropical one. OM's arbitrary units (`InternationalUnit`,
`colonyFormingUnit`) are refused so that they cannot become a second,
incommensurable identity for the `ucum:[IU]` bovnar already carries.

**The cost is size, and it is the largest this project has paid**: the stripped
release binary goes from 1015 KB to 1798 KB, three quarters of that being CF's
5071 standard names, which average 54 bytes each. The per-profile build switch
doc/11 §15.3 called for does not exist yet and now matters more.

Cross-vocabulary coverage grew with it: `om:` joins 62 of the 64 concepts in
`bovnar_crossvocab_test.c` (4776 assertions, up from 3551), and the conformance
corpus gains six `unit_profile` cases (425 total). `cf:` is deliberately not in
the concept table — a standard name that states the kelvin is not a *spelling*
of it. Two cases that used `cf:m` to stand for an unknown namespace now use
`zz:m`, since `cf` is a namespace of this build.

### Changed — every unit out-parameter is optional

Passing NULL for an out-parameter of a unit function now means "do not report
this one". The function behaves identically otherwise and its return value is
unchanged, in every function, in `bovnar.h` and `bovnar_si_units.h` alike.

It used to be three rules at once, and no way to tell which you had except by
trying:

- **crash** — `bvn_unit_to_si_factor` (all four), `bvn_unit_convert_factor`
  (both), `bvn_unit_dimension_vector`, `bvn_parse_unit` / `_n` (which checked
  their *input* pointer and not `ok`);
- **don't report** — `bvn_unit_reduce`'s `overflow`, `bvn_unit_convert_rational`'s
  `exact`, `bvn_currency_minor_unit`'s `ok`;
- **refuse the call** — `bvn_unit_si_normal_form`'s `out` and
  `bvn_unit_convert_value`'s `out`, neither of which appeared in those
  functions' own documented lists of failure reasons.

`bvn_unit_reduce` managed two of the three by itself: it guarded `overflow` and
dereferenced `scale`. A caller with no use for `requires_affine`, or one reading
a factor it means to validate itself, had to declare a variable to throw away.

The two that refused now compute and discard, which leaves each of them a useful
predicate — "does this unit have an SI normal form", "can this conversion be
done" — and brings them into line with what their headers already said. NULL for
an argument the answer is made *of* (a `bvn_int_t` in
`bvn_unit_convert_rational`, a buffer to a formatter) is still a refused call.

### Fixed — three wrong rationales for one right refusal

`Np` and `dB` do not convert into each other, which is correct and deliberate.
The reason given for it was wrong in three places, two of them contradicting
each other: doc/05 §3.15 stated `1 Np = 20/ln(10) dB ≈ 8.686 dB` as a plain
fact, in a table of units the library refuses to relate; the source comment in
`bovnar_si_units.c` said the figure is "8.686 or 4.343 depending on whether the
quantity is a power or a field"; and the test comment said the same thing with
power and field swapped. Under consistent definitions ISO 80000-3 gives one
figure, 8.685889 dB, for both.

The real reason is that relating two logarithmic scales is a change of *base*,
which the multiply-by-a-factor model every conversion entry point is built on
cannot express — and that ISO's relation holds between *levels* referred
consistently to one kind of quantity, which is not what a `value_unit_t`
carries. All four places now say that.

### Added — documentation for what was undocumented

- **`b` → `B` is refused, not divided by eight**, and now says so. Bit and byte
  are separate quantity kinds — a link rate in `M~b/s` and a file size in `M~B`
  are not the same measurement — and this was implemented, tested and reasoned
  about in the source without appearing in doc/04 or doc/05 at all. It is the
  one pair every reader assumes converts.
- **`bvn_unit_valid` had no documentation.** It is *structural* validity, and
  the header now says what that does and does not cover — in particular that a
  valid unit need not be writable.
- **The three shapes with no spelling**, in `bvn_unit_to_string`'s contract and
  doc/11 §5.1. A unit mixing opaque bases from two profiles was already
  documented; the other two were not, and both follow from what a *flat*
  vocabulary is: `unece:XBX` writes, `bu_unece_box²` and `bu_unece_box·m` have
  no UN/ECE notation and no native one. All three are `-1` from the formatter
  and `error_unit_illegal` from the writer, none of them reachable from a
  document, and none of them making the unit invalid.
- **`CF` (the hydroponic conductivity factor) had no unit-table row in doc/05** —
  the only real unit without one, mentioned in prose in §3.29 and tabulated
  everywhere else. It has a row now, with its conversions (1 CF = 0.1 mS/cm =
  100 µS/cm) verified against the library.

### Added — gates for what nothing checked

- **`check_doc_unit_tables.py`** (`bvnr_doc_unit_tables`). doc/04 and doc/05
  carry every unit and every currency, twice over, as hand-written markdown —
  about 1150 rows of symbols, accepted spellings, ISO numeric codes and
  minor-unit counts. Everything else derived from `src/gendata` is generated or
  gated; these two documents were the copy nothing compared. It checks that each
  documented canonical symbol IS the unit's symbol, that every spelling offered
  to a reader is one the parser accepts, that ISO codes and minor units match
  the catalogue, and that both documents cover all 180 units and all 216
  currencies. `minor_unit` is the field an application formats money with.
- **Both conversion engines are swept against each other.** `bvn_unit_convert_value`
  works in doubles off `si_conv_table`'s `.to_si_factor`; `bvn_unit_convert_rational`
  works in bignums off the `.factor_num`/`.factor_den` columns of the same rows.
  Two hand-edited numbers per unit, previously only spot-checked against each
  other. The new sweep compares every convertible pair in the registry, then
  every pair again across five prefixes and four exponents — 14 000 comparisons
  — and requires the two to agree on refusing as well as on answering. A factor
  whose decimal and whose rational disagree is the worst defect this library can
  have: both paths answer confidently, and which one a caller gets depends on
  whether it asked for losslessness.
- **Every opaque unit round-trips**, all 66, through text and back, and each must
  serialise into *its own* namespace — a single hardcoded `"ucum:"` in the
  writer would print a UN/ECE package code as `ucum:XBX`, which re-parses as
  nothing.

### Changed — BREAKING (`value_base_unit_t` is renumbered)

Every base unit id changed. **No document changed**: an id is an API number, not
a wire number — a unit is written by its spelling (`m/s`, `$USD`, `ucum:[IU]`)
and every existing `.bvnr` file parses to exactly the same values. What changes
is source and binaries that name a base unit numerically. Rebuild against the new
headers; a `bu_*` enumerator used by name needs no edit.

**The id space is now blocked.** The leading two decimal digits of an id name the
vocabulary it comes from and the four after them its position within that
vocabulary, so each block holds 10 000 ids:

| Block | Ids | Vocabulary |
|-------|-----|------------|
| 10 | 100000–100179 | native bovnar units |
| 20 | 200000–200040 | UCUM opaque units |
| 30 | 300000–300024 | UN/ECE opaque units |
| 40 / 50 / 60 | reserved | QUDT, QUDT quantity kinds, UDUNITS |
| 90 | 900000–900215 | currencies |

`bu_none` is still 0 and belongs to no block; 70 and 80 are free for a further
profile. See doc/05 §12.1.

*Why.* One flat counter made every vocabulary's ids a function of every other
vocabulary's size, and the space had the scars: physical units at 1–133 **and
again** at 348–396 because the currencies had been dropped in between; two
currencies (`ZWG`, `XCG`) stranded at 378–379 outside the currency range because
that range had been frozen; the profiles' units pinned above all of it, so
adding a native unit moved them. Adding a currency shifted units. Blocks end
that — a vocabulary grows into its own 10 000 ids and nothing outside it moves —
and they make an id self-describing: 200017 is UCUM's, whatever else the build
contains.

Consequences, all of which the review of the unit subsystem turned up as
existing hazards rather than new ones:

- **A base unit id is no longer an array index.** The space is sparse by design.
  The library's own tables (`si_conv_table`, `bu_prefix_policy`) are dense — one
  row per defined unit — and indexed by `bvni_unit_slot()`. `BVN_UNIT_SLOT_COUNT`
  is that row count and replaces `BVN_VALUE_BASE_UNIT_COUNT`, which was a bound
  on the enum and can no longer be one.
- **A bounds check is no longer a membership test.** `bvn_unit_valid` on the unit
  or `bvn_unit_is_currency` on the base is the way to ask.
- **`bu_zwg` and `bu_xcg` are gone**, and so is the whole `BVN_CURRENCY_EXT_*`
  extension segment. They existed only because the currency range had no room
  left. The two currencies are ordinary catalogue rows now, like the other 214,
  and like them have no `bu_*` enumerator.
- **`bvn_unit_is_fiat` / `bvn_unit_is_crypto` read the catalogue row**, not a
  sub-range of the ids. The order of `currencies.bvnr` therefore carries no
  meaning and a new currency of either kind appends without renumbering
  anything. `BVN_CURRENCY_FIAT_FIRST` / `_LAST` / `CRYPTO_FIRST` / `_LAST` are
  replaced by `BVN_CURRENCY_FIRST` / `_LAST` / `_COUNT`, which are now
  **generated** from the catalogue (`include/bovnar_currency.gen.h`) rather than
  hand-written beside it — a bound can no longer claim more rows than exist.
- **Each unit profile owns a block** instead of sharing one appended run, so a
  profile that grows a row shifts no other profile's ids. `gen_profiles.py`
  refuses two profiles claiming one block tag, or a profile overflowing its
  10 000.
- Python: `BaseUnit` members are renumbered to match, `BaseUnit._SENTINEL` is
  removed (a "one past the end" over a sparse space is exactly the bounds check
  that no longer works), and `CURRENCY_FIAT_FIRST` and friends are replaced by
  `CURRENCY_FIRST` / `CURRENCY_LAST` / `CURRENCY_COUNT` / `UNIT_NATIVE_FIRST` /
  `UNIT_NATIVE_LAST` / `UNIT_BLOCK_SIZE`.

### Fixed — the unit subsystem (three defects from the same stale bound)

`unit_exponent_t`'s range grew from ±9 to ±100 some releases ago. Three places
kept the old limit, and none of them reported an error — each produced a wrong
answer quietly:

- **`bvn_unit_reduce` dropped the component** for any summed exponent past ±9,
  folding its SI factor into `*scale` instead. For metre, whose factor is 1.0,
  the fold left no trace: `m¹⁰` reduced to the **dimensionless** unit with scale
  1.0. Everything from `m¹⁰` to `m¹⁰⁰` — the whole range the exponent type had
  gained — was affected. The bound is now `BVN_EXPONENT_MAX`, and past *that*
  the component genuinely cannot be represented, which is what `*overflow` has
  always been for.
- **`bvn_unit_si_normal_form` refused any dimension exponent past ±9**, so
  `k~m¹⁰` reported having no SI form at all and a normalising policy silently
  left it as written — although its SI form is simply `m¹⁰` and its factor
  exactly 10³⁰.
- **The profile writer emitted a multi-digit exponent as one byte.**
  `(char)('0' + v)` is correct only to 9; `m¹⁰` came out of `bvn_unit_to_ucum`
  as `"m:"` — a colon, which is the profile namespace separator — and `m¹⁰⁰` as
  `"m"` plus a raw `0x94`. The profile *parser* had accepted two- and
  three-digit exponents all along, so these were units the library could read
  and could not write back.

Also fixed:

- **`bvn_unit_to_si_factor` accepted underflow to zero.** The guard tested
  `isfinite`, and `0.0` is finite: `q~m¹⁰⁰` is 10⁻³⁰⁰⁰, the product underflowed
  to exactly zero, and the function reported success — so every value in that
  unit converted to 0 with nothing to say it had not. No real unit has an SI
  factor of zero, so zero can only mean the product fell off the bottom, and it
  is now a failure like `inf` is.
- **`bovnar_profiles.gen.h` was missing from the install set.** `bovnar.h`
  includes it, so `make install` produced an include directory whose `bovnar.h`
  could not even preprocess. Nothing caught it because nothing in the tree ever
  compiled against an install; a new `bvnr_install_headers` gate walks the
  include graph and fails when the list is short.

### Changed — BREAKING (Python bindings)

The Python package is **1.2.0** and two of its API shapes changed. Both were
defects the review named, and neither could be fixed additively.

- **`Quantity.unit` is now a `Unit` object, not a raw `ValueUnit`.** The struct
  it used to return had its operations scattered across module-level functions
  the caller had to know to look for (`bovnar.unit_to_str(q.unit)`,
  `bovnar.units_compatible(q.unit, other.unit)`), and — worse — it compared by
  ctypes identity, so two units that meant the same thing were never `==`.
  `Unit` carries the same struct (`q.unit.raw`, so anything that took a
  `ValueUnit` still works) and adds `str()`, `.factor`, `.is_dimensionless`,
  `.is_profile_only`, `.compatible_with()`, `.convert_factor()`,
  `.to_profile()`, and an `__eq__`/`__hash__` built on `bvn_unit_equal`. A
  native `mmHg` and a `ucum:mm[Hg]` now compare equal, which is what the format
  promises and what the old type denied.

  *Migration:* `str(q.unit)` for the spelling, `q.unit.raw` where a `ValueUnit`
  is genuinely wanted.

- **The value accessors are all properties.** `decimal`, `fraction`,
  `fixed_point`, `stored_value`, `ieee_bits` and `unit_str` were methods while
  `value`, `epoch_name`, `epoch_mjd` and `datetime_fraction` were properties,
  with no rule anyone could state — so callers guessed and got a `TypeError`
  either way. They are now uniformly properties.

  *Migration:* drop the parentheses — `q.decimal()` becomes `q.decimal`.

- **`Quantity.__init__` validates.** It used to store whatever it was given, so
  `Quantity("1", "not-a-valuetypespec")` constructed happily and every consumer
  of `.vtype` had to defend itself against a value that could never be right.
  `raw`, `vtype`, `tok_type` and `frac` are now type-checked and a bad one
  raises `BovnarArgumentError` at the call that built it, rather than three
  frames later. `unit` additionally accepts a `str` (`Quantity("1.0", vt,
  "m/s")`) and a `Unit`, not only a `ValueUnit`.


### Added

- **`--text-only` / `bvnr_read_flags_t.text_only`** — refuse a document that
  contains an octet stream, with the new `error_octet_stream_forbidden` (51).
  The format is a text/binary hybrid on purpose: length-prefixed chunks mean no
  escaping, no expansion, no forbidden byte, and a region a reader can skip
  without looking at. The cost is that such a document is not transport-safe
  through anything that rewrites bytes — line-ending normalisation inside a
  payload desynchronises the length prefixes, unrecoverably, and the result
  reads as a malformed document rather than a mangled one.

  This is the same move as `--require-unit`: the format permits something, and a
  consumer that cannot accept it says so where the parser can enforce it instead
  of hoping. A producer can use it to guarantee a channel stays transport-safe.
  The refusal fires at the stream's opening `0x00`, before the payload is read.
  On the CLI for `validate` and `events`; `query` refuses the option rather than
  ignoring it, since that path goes through the DOM, which takes no read flags.

- **A unit notation under implementation** (`ucum:`, `unece:`, `qudt:`,
  `qudt-qk:`, `udunits:` — below). It is **not** a new
  specification version: `BVNR_SPEC_VERSION_MINOR` is unchanged and
  `bovnar version` still reports spec 1.1. A document reaches the notation only
  by opting in to a version this build does not advertise, which keeps it out of
  every document that does not ask for it and leaves the number it will finally
  ship under open.

- **The UCUM unit profile** — `ucum:<code>` in a unit slot, translated at parse
  time into the same `value_unit_t` a native spelling produces, so nothing
  downstream can tell the two apart: `<float_dec:64,ucum:mm[Hg]> 120.00` and
  `<float_dec:64,mmHg> 120.00` compare equal, convert identically, and satisfy
  the same `--require-field` rule. The DOM, the writer, the streaming reader,
  the policy engine and the CLI needed no changes at all. A profile expression
  either becomes a real unit or becomes an error — there is no passthrough
  that would let an unchecked string reach a value — and the refusals split
  three ways so a producer can tell them apart: `error_unit_illegal` ("not a
  UCUM atom"), the new `error_unit_profile_unsupported` ("valid UCUM, no
  representation here") and the new `error_unit_profile_unknown` ("no such
  profile"). UCUM's 32 arbitrary units ([IU], [PFU], …) get one
  `value_base_unit_t` id each at 397..428 — one shared id would have made them
  compare equal — and are commensurable with nothing, the way currencies
  already are. New API: `bvn_unit_error_code`, `bvn_unit_is_profile_only`,
  `bvn_unit_to_profile`, `bvn_unit_to_ucum` (`unit_error_code`,
  `unit_is_profile_only`, `unit_to_profile`, `unit_to_ucum` in Python), and
  `ErrorCode.UNIT_PROFILE_UNKNOWN` /
  `.UNIT_PROFILE_UNSUPPORTED` in the Python enum. Requires the opt-in directive
  above; the writer
  refuses to emit a unit that has no native spelling without one, rather than
  producing a document it cannot read back. The table is
  `src/gendata/ucum.bvnr`; the
  specification, the transliteration table and the list of what has NO
  representation are in
  [doc/11_bovnar_unit_profiles.md](doc/11_bovnar_unit_profiles.md).

- **Four more unit profiles, and a cross-vocabulary conformance suite.** The
  profile mechanism became a registry of namespaces rather than a special case
  for one: `unece:` (UN/ECE Rec 20 units, plus Rec 21 packages and Rec 20 counts
  as incommensurable opaque units), `qudt:` (QUDT unit local names), `qudt-qk:`
  (QUDT quantity kinds, each translated to the **coherent SI unit** of the kind)
  and `udunits:` (UDUNITS-2, the CF/netCDF units syntax). Two grammars cover all
  five: an *expression* profile (`ucum`, `udunits`) parses operators, prefixes
  and exponents, while a *flat* profile (`unece`, `qudt`, `qudt-qk`) matches one
  whole token and never decomposes it — `unece:KGM` is the kilogram, not a `k`
  prefix on a `GM` Rec 20 never defined.

  Profile-only units from every namespace now share one **opaque block**, whose
  ids `gen_profiles.py` assigns rather than the data files hand-numbering them,
  and each is written back in the namespace that owns it. `gen_ucum.py` becomes
  `gen_profiles.py` and `bovnar_ucum.c` becomes `bovnar_profiles.c`; the refactor
  was checked by confirming all seven generated tables came out byte-identical
  with only `ucum` registered.

  A **cross-vocabulary suite** (64 concepts, 3551 assertions) checks every
  spelling of a concept against every other one, pairwise: equality, coherent-SI
  factor, dimension and round-trip, plus a negative table for the pairs that look
  interchangeable and are not. It found the ampere missing from the UCUM table on
  its first run — one of the seven SI base units had no UCUM spelling, and no
  single-vocabulary test had asked.

  **The five tables were then synchronised against each other, and against what
  each writes back.** Three faults, in falling order of how quietly they went
  wrong:

  *A flat profile wrote back the wrong code.* The reverse table picked the
  shortest code and broke ties alphabetically, which is right for an expression
  profile — its competing rows are spellings of one atom, and the vocabulary's
  own abbreviation is the short one — and meaningless for a flat one, where
  every Rec 20 code is two or three bytes and the rule collapses into
  "alphabetically first". So a joule was emitted as `unece:J55` (Rec 20's *watt
  second*), a pascal as `C55` (*newton per square metre*), a mole as `C34`, a
  tonne as `2U` (*megagram*), and a short ton as QUDT's bare, ambiguous `TON`.
  Each is worth the right number and none is the code a reader of that
  vocabulary expects. A flat profile now writes the FIRST code its data file
  lists, which is the canonical one; and a new `.reverse = false` covers what no
  ordering can see — `ucum:eq` is a byte shorter than `mol` and means the
  *equivalent*, so every mole was leaving the UCUM writer as a different
  quantity. Nothing caught any of this, because every write-back assertion in
  the tree was about a unit with only one code.

  *Three vocabularies disagreed about the reciprocal minute.* `unece:C94` and
  `qudt:PER-MIN` both mean *reciprocal minute* and were mapped onto native
  `rpm`, which counts revolutions — a claim about rotation the code does not
  make, and 2π from `rev/min`, which is what `unece:M46` and `udunits:rpm`
  actually mean and what neither table carried. All four now agree, and the
  precedent was already in the same file: `unece:C97` maps to `s^-1` and not to
  the hertz.

  *The coverage check could not see the gaps a flat grammar makes.* It matched a
  publisher's code against the native registry's SYMBOLS, so it could only ever
  propose a code worth a bare unprefixed atom — while a flat vocabulary spells
  every prefixed and every compound unit as one whole token. It now also indexes
  every target the five tables already use, which surfaced 392 more candidates
  and closed 71: `unece` 201 → 252 and `qudt` 244 → 263, most of them the
  coherent SI unit of a kind `qudt-qk` already mapped, so that the two QUDT
  namespaces had been disagreeing about their own publisher. Reading by label
  rather than by number mattered as before — the value alone proposed the *watt*
  for `KVA` and the *hertz* for the becquerel codes.

  The suite gained rows for all of it, including the six concepts (`tesla`,
  `sievert`, `katal`, `radian`, `steradian`, `newton metre`) whose UNECE code
  had been in the table for a release while the row that would have checked it
  left the vocabulary out.

  Two things are deliberately not carried. UDUNITS **reference time**
  (`days since 1970-01-01`) is refused as `error_unit_profile_unsupported`: a
  bovnar timestamp is `<datetime:width,epoch>`, whose epoch lives in the type
  spec's base slot where a unit-slot expression cannot reach it, and whose
  carrier is defined as a count of seconds. And **space does not multiply** in
  `udunits:`, although UDUNITS multiplies with one — a type annotation does not
  preserve whitespace, so `m s-1` would arrive as `ms-1`, which is valid UDUNITS
  meaning *reciprocal milliseconds*. Both are recorded in
  [doc/11 §10.2, §13.2 and §13.3](doc/11_bovnar_unit_profiles.md).

  The lexer accepts five new bytes in a unit — `'`, `[`, `]`, `{`, `}` — which
  is the profile's one visible effect on a document that never uses it:
  `<float:64,m[s]>` was `error_unexpected_input_byte` and is now
  `error_unit_illegal`. Both refuse the same document.

- **`bvnr_reader_get_skipped_bytes`** — how many bytes error recovery consumed
  and threw away, summed over the document (`Reader.skipped_bytes` in Python,
  and `bovnar events -c` now prints it). `recovery_count` says how OFTEN the
  parser recovered and cannot distinguish one skipped assignment from a whole
  discarded struct — both report a single recovery. The skipped bytes were
  never parsed, so no callback ever mentions them either: before this there was
  no way at all for a consumer of `continue_on_error` to learn that values had
  gone missing. A non-zero total means the document the callbacks saw is not
  the whole document. Recovery that runs to end-of-input counts everything to
  the end.
- **Fixed: error recovery discarded the whole statement after the error** — with
  `continue_on_error`, resync ran to the next `;` at the recovery-relative top
  level and resumed there. That is right when the error is INSIDE a statement:
  that statement's own `;` is the next one, so exactly the broken statement is
  lost and nothing else. When the error is BETWEEN statements — a stray byte in
  the whitespace separating two assignments — the next `;` belongs to the
  following, perfectly good statement, and recovery swallowed it whole. A single
  stray byte in front of a two-hundred-member struct discarded the entire
  struct: 202 values became 2, `bvnr_read` still returned true, and
  `recovery_count` still read 1, so nothing told the caller that two hundred
  values had gone.
  Recovery now ends at the start of the next assignment as well — a `.` at the
  recovery-relative top level followed by a byte that can begin an identifier.
  The `;` remains a boundary in every case it was one before, so this can never
  recover LESS; what it can do is stop early, at the first point the document
  plausibly becomes readable again. The same stray byte now costs only itself.
  It does not make it newly possible to read an assignment out of corruption: a
  `;` inside a corrupt region already ended recovery and resumed parsing inside
  it. A `.` that leads nowhere (`1.5`, `.5`, a `.` in binary junk) is just
  another skipped byte, and a `.` inside a bracket opened since recovery began
  opens no assignment either. An error inside a statement still discards that
  statement and no more. See specification §13.2.
- **Reader-side unit policy (`bvnr_reader_set_unit_policy`)** — what
  `bvnr_read_flags_t.want_unit` does through a C callback, stated as data: which
  units the consumer wants values delivered in, and what the document must carry
  to be accepted at all. Everything is expressed as unit **text**, so a consumer
  that cannot take the address of a C function can now ask for the feature the
  callback was the only route to. New symbols and one new struct only — no
  existing struct changed shape, so this costs nothing in ABI beyond what the
  release already spends.
  Three things share the one policy object. `targets` names the units to convert
  to, first validly-convertible match winning, so order is significant.
  `normalise = bvnr_normalise_si` catches whatever the targets did not and
  delivers it in coherent SI base units with prefixes folded out. `require_unit`
  and `require_dimension_of` reject a document instead of changing it — "every
  numeric value must carry a unit", "every value must be a length, in whatever
  unit it chose to write it" — and are evaluated on the unit the document wrote,
  before any conversion: validate what you were sent, convert for the consumer.
  The `want_unit` hook still wins where it is installed, so a caller can
  normalise a document and hand-handle one field.
  Two refusals are deliberate and are the reason the feature is safe. A value
  with no unit only ever matches a target (or a requirement) that is itself
  `no_unit`: a bare number is dimensionally compatible with `%` and `ppm`, so
  without that fence a policy naming `"%"` would deliver `0.25` as `25` — the
  silent rescale the format exists to prevent, arrived at through the machinery
  meant to prevent it. And SI normalisation refuses every *dimensionless* unit
  (`%`, `ppm`, `dB`, `pH`, `rad`, `°`, the turbidity scales) along with the
  currencies, rather than restating `35 %` as `0.35` or demanding the irrational
  factor between `°` and `rad`.
  Exactness is unchanged — the same exact arbitrary-precision path, nothing
  approximate ever delivered — with one mode added for the blanket case:
  `on_inexact = bvnr_inexact_leave` hands over a value whose exact result has no
  terminating expansion in the output base (`42 km/h` is `35/3 m/s`) in its
  native unit, visible as `converted == false`, instead of aborting a document
  nobody had a complaint about. An irrational factor still aborts. See
  read/write API §1.12, and `doc/06_bovnar_unit_policy.md` for the policy
  reference.
  **Per-field rules** name one field by the key path it sits at
  (`.inlet.temperature`, or `.inlet.*` for a subtree), and are consulted before
  everything else — the most specific thing a policy can say. This is what the
  streaming validator never had: it tracked no key context at all, so a value
  arrived knowing its type and unit and nothing about where it came from. Unlike
  a whole-document target, a rule is an ASSERTION — the caller named this field,
  so a value that cannot satisfy it is `error_unit_mismatch` rather than one
  passed through quietly, and a bare number does not satisfy ".speed is m/s"
  either. A prefix matches only at a component boundary, so `.in.*` never claims
  `.inlet.a`, and a value nested deeper than the path tracker can describe
  matches NO rule rather than the wrong one.
  **The DOM tier takes the policy too** (`bvn_dom_parse_policy`,
  `bvn_dom_parse_fd_policy`, `dom_parse(..., policy)`), so `bovnar query` can
  assert what it expects and ask for the unit it wants back. A converted value
  is STORED converted — digits, unit and base — because a caller who asked for
  metres and got the document's feet back would have no way to notice; an
  integer that converts to a fraction is stored as a float, which is what it now
  is. A policy the library refuses is `error_invalid_argument`, never a parse
  error against the document.
  **The writer takes the same policy** (`bvnr_writer_set_unit_policy`,
  `Writer.set_unit_policy`), and that is the half the format's promise actually
  rests on: a reader can only reject a document somebody already wrote, so only
  the writer can stop a bare number reaching a file at all. `bvnr_write_event`
  and every helper above it then fail with `error_unit_mismatch`. It checks
  whichever place the unit came from — inline on the value or as a parameter of
  its type annotation, one annotation covering every element of the array under
  it — and it deliberately does NOT let an annotated value vouch for the next
  bare one. Validation only: a policy carrying the conversion fields is refused
  rather than half-honoured, because the writer already rewrites values under
  `BVN_UNIT_REDUCE` and a second rewriting mode with different rules about
  exactness is how two features end up disagreeing about what a document says.
  Reachable from every consumer, which is the point of stating it as data:
  Python gets `Reader.set_unit_policy(UnitPolicy(...))` with no per-value ctypes
  trampoline (python bindings §5.5), and the CLI gets `--unit`, `--si`,
  `--base`, `--leave-inexact`, `--require-unit` and `--require-dimension` on
  `events`, plus the two validation flags on `validate`. `bovnar events` now
  prints a conversion next to the value it belongs to.
- **`bvn_units_convertible` and `bvn_unit_si_normal_form`** — the two predicates
  the policy is built on, exposed because every hand-written `want_unit` hook
  that screens its targets needs the first one and most get it wrong.
  `bvn_units_convertible` is "can the library actually convert a to b":
  dimensionally compatible, **or** the same unit apart from its prefixes. That
  second clause is not pedantry — a currency carries no dimension by design, so
  `bvn_units_compatible` reports `k~$USD → $USD` (and `$USD → $USD`)
  incompatible although both convert exactly. `bvn_unit_convert_factor` is not a
  substitute either: it reports failure for `°F → °C`, since an affine
  conversion has no single multiplicative factor, so screening on it drops every
  temperature in the format. `bvn_unit_si_normal_form` returns the coherent SI
  form of a unit, or false where there is none to name.
- **Compact prefix form for physical units** — a prefix may now be written
  without the `~` separator: `kg`, `km`, `ms`, `MHz`, `hPa`, `mmol`, `MeV`,
  `KiB`, `MiB`, `kg·m/s²` all parse, each to exactly the `value_unit_t` its
  separated spelling produces. The barrier this removes is the first thing every
  newcomer hit; the safety property that made it possible is that the base
  symbol is matched as the **longest alias suffix**, so a bare unit alias always
  outranks a prefixed reading of the same token (`min` is the minute, never
  milli-inch; `cd` the candela, never centi-day) and a compact spelling is only
  ever accepted where the parser previously raised `error_unit_illegal` — no
  existing document decodes differently. Prefixes still cannot be stacked
  (`kkg`, `k~kg` → `error_unit_illegal`), per-unit prefix policy is unchanged
  (`Kim` fails exactly as `Ki~m` does, as do `mB`, `kPfd`, `kppm`), and **the
  canonical output form is unaffected** — `bvn_unit_to_string` and the writers emit `k~g` for
  either spelling, so a round-tripped document stays readable to an older
  reader. Two compact spellings are refused by name, because accepting them
  would turn a parse error into a quietly wrong unit: `usb` (not the
  microstilb) and `kt` — which abbreviates two units bovnar *does* model, the
  kilotonne and the knot, so the author picks `k~t` or `kn`. The list is data,
  in `.compact_exceptions` in `src/gendata/units.bvnr`, and the separated forms
  `u~sb` and `k~t` are untouched. See unit-system reference §4.3.
- **Fixed: the Prussian Elle was 3.3×10⁻⁴ off its own definition** — 0.66716 m, which is 25.5085
  Zoll, a number no definition produces. The Elle is 25½ Zoll = 0.666937625 m from the 1816 Fuß, and
  published tables give 66.694 cm; every other Prussian unit already derived to within 1.1×10⁻⁶.
  A value stored in `elle` now converts correctly. This is the one class of change a 1.x revision
  cannot make silently — the stored number is unchanged, but what it converts to moves by 0.03 % —
  so it is called out here rather than buried: anything that round-tripped an Elle through SI before
  this release carries the old factor.
- **Fixed: six factors were rounded before being stored, and the lossless path believed them** —
  `klafter`, `rute` and `morgen` are exact multiples of the 1816 Prussian Fuß (6, 12, and 180 square
  Ruten) but were stored to six digits, so `1 klafter` was 6.000006 Fuß; `ft_lb` and `slug` are exact
  products of the international pound, foot and standard gravity but were stored 11 digits in, and
  `inHg` is exactly 25.4 conventional mmHg but was stored as 3386.388645 rather than
  3386.388640341. None of them tripped `gen_units`' exactness guard, which only refuses a decimal of
  16+ significant digits — the repr of a double — so the exact-rational engine reported all six as
  *exact* while being wrong in the 7th to 12th digit. The visible symptoms: `1 klafter → prf` failed
  with `error_unit_inexact` (6.000006 has no terminating expansion) instead of returning 6, and
  `1 ft_lb → J` returned a losslessly-labelled value 2.3×10⁻¹¹ short of its own definition. All six
  now carry the exact value, three of them via `.factor_num`/`.factor_den`. As with the Elle above,
  what a stored number converts to moves — by at most 1.6×10⁻⁶, and only for those six units.
  `test_exactly_defined_factors_are_exactly_right` now compares fifteen exactly-defined factors as
  **rationals**, with no tolerance, which is the check that would have caught all of them; the three
  Prussian entries no longer carry the 5×10⁻⁶ tolerance that had been accommodating the rounding.
- **Fixed: `lm` converted to `cd`, while `lm ↔ cd·sr` was refused** — the steradian carries a
  quantity kind (it *is* `rad²`), but the photometric units defined through it did not, so the table
  contradicted itself: it refused luminous flux against candela-steradian on the kind rule and
  converted it to a bare candela at factor 1, which is the same claim with the `sr` dropped. `lm`,
  `lx` and `ph` now carry the steradian's weight; `cd` and `sb` deliberately do not. Luminous flux
  no longer converts into luminous intensity, and illuminance (`lx`, `ph`) no longer into luminance
  (`cd/m²`, `sb`) — a distinction the SI dimension vector cannot express, since every photometric
  unit reduces to candela in base dimensions. `lm ↔ cd·sr`, `lx ↔ lm/m²`, `ph ↔ lx` and `sb ↔ cd/m²`
  all work, and `cd·sr` now reduces to `lm` under `BVN_UNIT_REDUCE`. The pint bridge follows:
  `bvnr_lumen`/`bvnr_lux`/`bvnr_phot` are defined from `candela · bvnr_steradian`.
- **Fixed: `BVN_UNIT_REDUCE` rewrote one named unit as another** — the named-SI collapse matched on
  the dimension vector and took the first hit, so serialising with `BVN_UNIT_REDUCE` turned `Sv`
  into `Gy`, `rem` into `c~Gy`, `Bq` and `Bd` into `Hz`, `var` and `VA` into `W`, and `cd` into `lm`.
  Offering those conversions when a caller asks is deliberate and documented; rewriting the
  annotation *in the document* is a stronger act, and it changed what a stored value claimed to be.
  The collapse now never substitutes a different base unit for a single-component unit. What it is
  for is untouched: `A·s` → `C`, `mol/s` → `kat`, `k~g·m/s²` → `N`, and a prefix folded into the
  scale still comes back (`k~N` → `k~N`).
- **Fixed: an affine scale inside a product produced a meaningless number** — `bvn_unit_to_si_factor`
  accepted `°C` at exponent 1 anywhere in a compound and added its offset unscaled, so
  `20 °C/h → K/h` came out as 983360 and `20 °C·m → K·m` as 293.15 regardless of the metres. The
  offset is a number of kelvin and a product whose SI unit is `K·s⁻¹` or `K·m` has nowhere to put
  it. An affine unit now has an SI value only when it is the whole unit: `°C/h`, `°C·m`, `°C²` and
  `°C·°F` all set `*ok = false`, and the reader reports `error_unit_mismatch` rather than a number.
  `°C/h` still **parses** and remains a legal annotation — a consumer meaning a temperature
  *difference* can read the components and apply its own semantics. A lone `°C` is unaffected, and
  all eight temperature scales still inter-convert exactly. This matches pint, which forbids an
  offset unit in a product outright; the bridge's own refusal is no longer stricter than the
  reference it is bridging to.
- **Hardened: "kilo and up" on `b`/`B` is now a magnitude test** — `bvn_prefix_unit_valid` compared
  prefix *enum ids* (`>= si_kilo`), which agreed with magnitude only because `prefixes.bvnr` happens
  to list prefixes in ascending order — while that same file requires ids to be append-only, so the
  first sub-kilo prefix ever added would have been given a high id and silently become legal on bit
  and byte. It now reads the exponent, and `gen_prefixes.py` additionally refuses a list that is not
  in ascending exponent order.
- **Conversion factors are now checked against their own definitions** — a new pure-Python test
  (`test_unit_factors_derived.py`, registered as `bvnr_py_unit_factors`) re-derives 119 factors from
  the relations that define them: a furlong is 660 international feet, an acre 43560 square feet, an
  oersted 1000/4π A/m, a Prussian Zoll a twelfth of the 1816 Fuß, a German hardness degree 10 mg CaO
  per litre over the molar mass of CaO. This closes a gap the review exposed: the pint bridge
  validates bovnar against pint, but for the ~40 units bovnar defines itself the pint definition is
  *generated from bovnar's factor*, so that check was circular and a wrong number would have agreed
  with itself. Units defined by arithmetic on exact constants must match to 1e-9; historical ones
  carry the tolerance their sources actually publish.
- **The pint bridge now enforces bovnar's quantity kinds** — it did not, and the gap ran the wrong
  way: pint would convert an `NTU` into an `FNU`, a byte into 8 bits, an angle into a plain number,
  a `pH` into a percentage. Fourteen such conversions were possible in pint that bovnar refuses.
  Each kind now gets its own pint **dimension**, the same device the currency table already uses to
  stop `100 USD` becoming `100 EUR`, so pint raises `DimensionalityError` exactly where bovnar
  raises. Conversions within a kind are untouched — `m~NTU → NTU`, `° → rad`, `sr = rad²` — as are
  all dimensioned conversions. `is_kind_scale(unit)` reports the isolation, as `is_currency_unit`
  does for money. The cost is interop with pint's *natives*: a bovnar byte no longer converts to
  pint's `megabyte`, so `build_registry(isolate_kinds=False)` restores the aliasing, and pint's
  permissiveness with it. `SEMANTIC_CAVEATS` shrinks to what isolation cannot fix — `DECIBEL`,
  `NEPER`, `PH_SCALE` (pint will still *add* two of them, which the scales do not support) and
  `VAL` (the divalent convention). The invariant is now a test:
  `TestPintAgreesWithBovnarOnWhatConverts` asserts pint refuses everything bovnar refuses, over
  every kind plus dimensioned controls.
- **Three more turbidity scales: `FTU`, `FAU`, `JTU`** — completing the set, each as its own
  quantity kind for a different reason. `FTU` is formazin turbidity with the optical geometry
  **unstated** (ISO 7027's original 1984 name), which is precisely why it cannot be an alias of NTU
  or FNU: not saying which optics were used is its entire content. `FAU` is not a nephelometric
  measurement at all — it reads attenuation in the transmitted beam at 0° rather than sideways
  scatter, and is the instrument of choice above ~40 FNU where nephelometry saturates. `JTU` is the
  visual candle turbidimeter, whose published "1 JTU ≈ 1 NTU" holds near 40 units and nowhere else,
  being nonlinear and sample-dependent; it takes no prefix, the method being unable to resolve
  below roughly 25 JTU. With NTU and FNU that makes five turbidity scales, no two of which convert.
  Case still matters: `fau` is the femto-astronomical-unit. Enum values 394-396;
  `BVN_VALUE_BASE_UNIT_COUNT` grows to 397.
- **Water-quality scales: `NTU`, `FNU`, `PSU` and `CF`** — three instrument scales and one rescaled
  conductivity. NTU (white light, EPA 180.1) and FNU (near-infrared, ISO 7027) are both
  formazin-calibrated and numerically equal *on a formazin standard*, which is exactly why they get
  separate quantity kinds: on real water the two optics disagree by an amount that depends on
  particle size and colour, so no factor relates them and every standard demands the method be
  reported. PSU (PSS-78 practical salinity) is a conductivity ratio, dimensionless by construction
  and **not** a mass fraction — *S*_P 35 is about 35.165 g/kg, so letting it convert to `‰` or
  `g/kg` would be wrong by half a percent; write `g/k~g` for absolute salinity. All three convert
  only to themselves. `CF` is the opposite case: the hydroponic conductivity factor really is a
  conductivity, EC in mS/cm × 10, so it carries siemens-per-metre dimensions and converts exactly —
  1 CF = 0.1 mS/cm = 100 µS/cm. Uppercase only: `cF` is the centifarad. `NTU`/`FNU` take prefixes
  (`m~NTU` is real in ultrapure-water work); `PSU` and `CF` do not. Enum values 390-393;
  `BVN_VALUE_BASE_UNIT_COUNT` grows to 394. The pint bridge carries a caveat for each of the three
  scales, because pint has no notion of quantity kinds and would happily trade an NTU for an FNU.
- **Conductivity, dissolved solids and two more hardness spellings** — a review of what water data
  actually writes. **EC and TDS need no new units**: `µS/cm`, `mS/cm`, `dS/m`, `S/m` and `mg/L`
  already say them exactly, and `MΩ·cm` covers resistivity. What was missing were spellings:
  `mho`, `mhos` and `℧` (U+2127) are now accepted names for the siemens, so the `µmho/cm` in older
  US water and soil data parses; `eq` is accepted for `val`, so `meq/L` and `mval/L` are one unit;
  and `gpg` (grains of CaCO₃ per US gallon, the US water-softener scale, 0.171034 mmol/L) joins the
  hardness family as `bu_grains_per_gallon` — note it is an *amount* concentration and so converts
  with the other scales, where the compound `gr/gal` is a *mass* concentration and does not.
  Documented alongside: "TDS in ppm" means mg/L, while bovnar's `ppm` is the dimensionless 10⁻⁶ —
  numerically the same for dilute water at 1 kg/L, but a different dimension, so they never convert.
- **Water hardness: `°dH`, `°e`/`°Clark`, `°fH`, `°rH`, `°aH`, and `val`** — six scales for one
  quantity, the concentration of dissolved alkaline-earth ions. Each is defined as a mass of a
  reference compound per litre, but the scales count *different* compounds (CaO, CaCO₃, Ca), so
  mass concentration is not their common ground and comparing them that way is wrong by the ratio
  of two molar masses. They are modelled as amount concentration (mol·m⁻³ = mmol·L⁻¹), which is
  what the published conversion tables tabulate and what makes every scale convert into every
  other and into `m~mol/L` — the latter needs no unit of its own. Factors are derived from IUPAC
  2021 molar masses and therefore carry `.exact = false`: a lossless conversion reports
  `error_unit_inexact` rather than inventing precision. None of the degrees takes a prefix. `val`
  is the equivalent **as water analysis uses it** — the ions are divalent, so 1 val = ½ mol and
  `m~val/L` = 0.5 mmol/L; for a monovalent species an equivalent is 1 mol, which no unit can
  convey, so there is deliberately no generic `eq`/`equivalent` alias. Two spellings matter:
  `dH` without the degree sign is the decihenry and stays that way, and water chemistry's "ppm"
  is `°aH`, not Bovnar's dimensionless `ppm`. Enum values 383-388;
  `BVN_VALUE_BASE_UNIT_COUNT` grows to 389.
- **Three units: `pH`, `mph`, `kph`** — the acidity scale (dimensionless, no
  prefix, modelled for the same reason `dB` and `Np` are) and two named speeds
  (`mph` = 0.44704 m/s exactly; `kph` = 5/18 m/s, stated as an exact rational
  because the decimal does not terminate; `kmh` is an accepted spelling). All
  three were compact spellings the parser had to refuse, because `pH` reads as
  the picohenry and `mph`/`kph` as milli- and kilophot; naming the quantities
  resolves that properly, since a bare alias always outranks a prefixed reading.
  The compound forms `mi/h` and `k~m/h` are unaffected and still mean the same
  quantities — but a named speed is its own component, so an annotation of
  `mph` does not reconcile with an inline `mi/h` (the same structural rule that
  has always applied to `kn`). Enum values are appended past the currency
  extension at **380-382**, so nothing shifts; `BVN_VALUE_BASE_UNIT_COUNT` grows
  to 383 and its static assert now tracks the highest enumerator
  (`bu_kilometer_per_hour`) rather than the last currency. The Python `BaseUnit`
  enum and the pint bridge gain the three: `mph`/`kph` map to pint's native
  `mile_per_hour`/`kilometer_per_hour`, while pH is defined as `bvnr_ph_scale`
  (pint parses "pH" as the picohenry too) and carries a semantic caveat, as `dB`
  and `Np` do, because the scale is logarithmic.
- **Compact prefix form for currencies** — the same spelling rule applied to
  money: `k$USD`, `M$EUR` and `G$ETH` parse as the prefixed currencies they
  look like. The `$` sigil separates the prefix from the code on its own —
  no prefix symbol and no currency code contains a `$` — so the compact form is
  unambiguous by construction. The sigil rule itself is unchanged and the sigil
  stays mandatory: `kUSD` is `error_unit_illegal` exactly as bare `USD` is, IEC
  prefixes remain forbidden on money in either spelling (`Ki$USD`, `Ki~$USD`),
  and the writer still emits `k~$USD`. See unit-system reference §9.4.
- **Lossless read-time unit / base conversion** — an optional `want_unit` hook on
  `bvnr_read_flags_t`. When set, the reader calls it for every numeric value and,
  if the caller names a target unit and output base, delivers the value converted
  into it **exactly**, in arbitrary-precision rational arithmetic — a 1056-bit
  float or a 512-bit integer converts with no loss beyond the library's declared
  factor. `bvnr_data_t` gains `converted` (bool) and a `conv` carrier
  (`bvnr_converted_t`: target unit, exact positional `text` in the requested
  base, and the reduced rational `num`/`den`); `data`/`value_unit` keep the
  original. Requesting the native unit with a different base is a pure base
  conversion. Once the hook asks for a conversion the value either arrives
  converted or the parse stops — nothing approximate is delivered and nothing is
  silently skipped. A dimensionally incompatible target is `error_unit_mismatch`;
  an irrational factor (π-based angle) is the new `error_unit_inexact`; a finite
  literal too extreme to build a rational from is `error_value_out_of_range`; an
  unusable output base is `error_invalid_argument`. Only `nan`/`inf` pass through
  unconverted. See read/write API §7c. The Python `Reader` `want_unit=` callback
  returns a unit or `(unit, base)`, and `BvnrData.converted_str()` /
  `EventPayload.converted_text` expose the exact string; the WASM event JSON
  gains `"converted"`, `"converted_base"`, `"converted_unit"`.
- **`want_unit_allow_nonterminating`** (`bvnr_read_flags_t`, Python
  `Reader.read_mem`/`read_fd`) — many everyday conversions are exact as a
  rational but have no finite positional expansion in the output base (`km/h →
  m/s` is 5/18, `°F → °C`, `m → km` in base 2). By default those abort with
  `error_unit_inexact` rather than round; set this flag and they arrive with
  `conv.num`/`conv.den` exact and `conv.text == NULL`. An irrational factor still
  aborts — there is no exact rational to hand over.
- **`bvn_unit_convert_rational` / `bvn_rational_to_str`** (`bovnar_si_units.h`) —
  exact unit conversion of an arbitrary-precision rational, and rendering of a
  rational in any base bvnr can write — `2..62` plus `64` and `85`
  (`bvn_rational_base_valid`) — with terminating-expansion detection. The engine
  behind `want_unit`. `bvn_rational_to_str` never truncates: a buffer too small
  is `-1`, a non-terminating expansion is the distinct
  `BVN_RATIONAL_NONTERMINATING`, and `bvn_rational_str_bufsize` gives the bound.
  `bvn_float_parse_rational` (`bvn_float.h`) parses a wire literal into an exact
  rational.
- **Exact rational factor table** — every non-irrational unit now carries an
  exact rational `to_si` factor, stated in `src/gendata/units.bvnr` rather than
  recovered from a rounded decimal. `gen_units.py` now refuses to generate a
  table in which a 16+ digit decimal (i.e. the repr of a double) is passed off as
  exact: such a unit must supply `.factor_num`/`.factor_den` or `.exact = false`.
- **`bvn_int_mul` / `bvn_int_add` / `bvn_int_gcd`** (`bvn_int.h`) —
  arbitrary-precision multiply, add, and gcd, underpinning exact-rational
  arithmetic.
- **`bvn_unit_convert_value`** (`bovnar_si_units.h`) — the `double`, convenience
  counterpart (lossy for wide values): multiplicative + affine conversion in one
  call. The C equivalent of Python `convert_value` (which delegates to it).

- **§18 Security Considerations in the specification.** The format's threat
  surface was written down in the Internet-Draft and in the IANA registration
  but not in the document both of those cite as the published specification.
  §18 states it there: the resource-exhaustion defaults that are chosen for
  trusted pipelines, recovery mode delivering a subset with nothing in the data
  saying so, version leniency, transport corruption of length-prefixed chunks,
  the hazards a reference resolver inherits, special numbers bypassing range
  validation, the difference between a unit being *validated* and being *right*
  (including a conversion request that is not a guarantee), leap-second table
  drift between builds, byte-compared keys, and the absence of any integrity or
  authenticity property. Nothing here changes what parses; it is a description
  of behaviour that was already specified piecemeal.

- **`SECURITY.md`** — the reporting policy, the supported versions, and, above
  all, an explicit out-of-scope list, so a report does not have to guess which
  of these are defects and which are documented properties of the format.

### Fixed

- **Both formal grammars referenced rules they never defined.** `doc/12_bovnar.ebnf`
  wrote the seven datetime-literal productions over a lowercase `digit` that has
  no definition anywhere — the terminal is `DIGIT`, as every other production
  spells it — so the one document whose stated purpose is to be machine-readable
  did not resolve. The draft's collected ABNF referenced `base-unit` and
  `currency-code` with no productions at all; both now have rules that bound a
  token's shape (verified to admit all 529 registered spellings and all 216
  currency codes, and nothing else), leaving membership where it belongs, in the
  registry. Both grammars are now closed: every referenced rule is defined, and
  the only unreferenced ones are the two documented entry points.

- **doc/11 §5.3's round-trip sweep was stale and unpinned.** It reported 592
  spellings surviving native → UCUM → native; the registry has since grown and
  the figure is 594. `test_sweep_round_trip` now pins all three counts and the
  invariant that matters — that nothing round-trips to a *different* unit —
  and the document states the prefix set the measurement uses, which it did not
  before.

- **A copy-paste failure in the Python bindings reference.** The helper example
  in §7 imports `units_convertible` and then calls `units_compatible`, which the
  import line omits: the block as printed raises `NameError`.

- **Struct listings in the documentation had fallen behind their headers.** The
  `bvnr_unit_policy_t` listing in the read/write API omitted `rules` and
  `num_rules` — the two fields the example immediately below it assigns, and the
  first two in the header — along with `bvnr_unit_rule_t` itself, so the sample
  used a type the document never showed. The specification's `bvnr_read_flags_t`
  listing, which is otherwise complete down to `_reserved`, was missing
  `text_only`, though §16.10 already names that flag as what raises code 51. The
  `bvnr_data_t` listing in the unit reference dropped `converted` and `conv`,
  which are precisely the unit-facing fields a conversion reports through. A
  check now compares every struct listing in `doc/` against the header it
  restates.

- **Stale numbers in the conformance and gendata documents.** doc/13's
  illustrative TAP block still showed `1..21` groups and an `encoding` plan of
  `1..9`, against a suite that has run 23 groups and 14 encoding cases for some
  time; the coverage-table gate did not reach into an example. It does now, and
  it also holds the prose group count beside it. `src/gendata/README.md` still
  said 163 physical units (the 1.0 figure; there are 180) and documented neither
  `ucum.bvnr` nor `gen_profiles.py` nor any of the seven tables they generate.

- **Both sets of release notes pointed at an amalgamation that was never
  there.** They told a C consumer to vendor `dist/bovnar.h` + `dist/bovnar.c`;
  no such paths have existed at any tag. The amalgamation is regenerated into
  `build/amalgamate/` by every build and ships as the `…-amalgamate.tar.xz`
  release asset. `dist/README.md` in turn described only `mime/` and
  `linguist/`, omitting the tracked `wasm/` npm package entirely.

- **`bovnar query` and `bovnar convert` printed short round numbers in
  scientific notation.** `120.0` came out as `1.2e+02` and `250.0` as
  `2.5e+02`, while `123456789.12345679` printed plainly — the same command
  rendering comparable values two different ways, and the short ones worst. The
  cause was using `%g`'s own notation rule as a side effect of searching for the
  shortest round-tripping precision: `%g` switches to scientific once the
  decimal exponent reaches the precision, and 120.0 round-trips at precision 2.
  Precision and notation are now decided separately. Values below `1e-4` and at
  or above `1e17` keep scientific notation, as every shortest-round-trip printer
  does. No value changed: round-tripping was verified over three million random
  doubles.

- **A bad first byte in a string discarded every assignment after it.** With
  `continue_on_error`, recovery picks the string sub-machine when the error fired
  inside a string, so the string's own closing `"` is consumed as the close
  rather than read as the opening quote of a new string to skip. That switch
  listed `copy_string_byte` and the escape states but not `string_intro` — the
  state the FIRST byte of a string body is read in, whose transition row is
  byte-for-byte identical. So recovery worked or failed depending on whether the
  string had one good character in front of the bad byte:
  `.a = "x<bad>"; .b = 2;` recovered and kept `.b`, while `.a = "<bad>"; .b = 2;`
  read the closing quote as an opening one, swallowed everything to the next
  quote or end of input, and then failed the whole parse with
  `error_got_incomplete_bvnr_stream` — losing every later assignment in a
  document that was perfectly recoverable. Reachable from any string whose first
  byte is malformed UTF-8 or a rejected ASCII control byte, including the first
  byte of a continuation string (`"a" "<bad>"`) and of an array element. The
  first byte of a string body is now treated as the string-body state it is.
- **A datetime literal could be read but never written back.** Sub-second digits
  survive a round-trip only as an ISO literal — the whole-second epoch carrier
  has nowhere to put them — so a literal whose UTC civil year falls outside the
  `0000`..`9999` a four-digit year can spell was accepted by the reader and
  refused by the writer, which is the one asymmetry the format cannot afford:
  `.t = 0000-01-01T00:00:00.5+23:59;` validated fine and then failed every
  `pretty-print`, `convert` and canonicalisation of the document that held it,
  reported as a writer error against a document nothing had complained about. A
  timezone offset is what makes it reachable, at either end — the LOCAL year is
  in range and only the fold to UTC pushes it out. The reader now rejects such a
  literal with `error_invalid_datetime_literal`, applying exactly the writer's
  check, so anything it accepts the writer can emit. Only when a fraction is
  actually present: without one the integer carrier round-trips at any year, and
  those instants stay legal.
- **A prefix ID past the end of its table let the writer emit a document it
  could not read back.** `bvn_prefix_unit_valid` range-checked the prefix ID in
  exactly one branch — SI prefixes on `bit`/`byte`, where "kilo and up" has to
  index the table to compare magnitudes. Every other unit skipped the check, and
  since this function is the single gatekeeper the parser, the writer, the
  validator and the conversion helpers all share, an out-of-range ID was
  "valid" everywhere at once. `si_prefix_str()` has no symbol for such an ID and
  returns `""`, so `bvn_write_unit_component` wrote the `~` separator with
  nothing in front of it: a unit built with `prefix.id.si = 99` on `bu_meter`
  formatted as `~m`, `bvnr_write_float_unit` accepted it and reported success,
  and the resulting `.len=<float:64,~m>1.5e+0;` came back
  `error_unit_illegal` on the next read — a silently unreadable document out of
  a writer that said it had succeeded. `bvn_unit_prefix_factor` meanwhile scored
  the same unit as 1, quietly dropping the scale, so the three disagreed three
  ways. The ID is now range-checked for both prefix systems before anything
  looks at it, which makes `bvn_unit_valid` false, `bvn_unit_to_string` return
  -1, and the writer fail with `error_unit_illegal` at the call instead of in
  someone else's parser. In-range prefixes are untouched.
- **`bvn_unit_equal` called the two spellings of "no prefix" different units.**
  An absent prefix can be written `(prefix_si, si_none)` — what the parser always
  produces — or `(prefix_iec, iec_none)`, which is the natural thing to reach for
  when building an unprefixed `bit`/`byte` unit by hand. The two are
  indistinguishable everywhere else: both render to the same text, both score a
  factor of 1. But the component comparison tested the prefix *system* before the
  ID, so a hand-built `B` compared unequal to the `B` the parser hands back for
  the very text it prints — against the documented contract that logically
  equivalent notations compare equal, and a spurious unit mismatch for any caller
  that assembles its expected unit rather than parsing it. The absent-prefix case
  is now settled before the system is allowed to matter; a real prefix still
  separates units, so `km` and `m` compare unequal as before.
- **`bvn_int_mul`/`bvn_int_add`/`bvn_int_gcd` now screen their operands.** These
  size their scratch from the operand widths — `na + nb`, `maxn + 1u` — and
  `bvn_int_t` is a public struct, so an operand carrying a nonsensical `nused`
  made those sums wrap. A wrapped `need` then satisfied `bigint_ensure_cap`'s
  `need <= nlimbs` early return *without allocating*, and the multiply/add loops
  walked limbs that were never there. Nothing the library builds looks like that
  (capacity is capped at `BVN_INT_MAX_LIMBS` and `nused` never passes `nlimbs`),
  so this was only reachable by handing in a hand-built or uninitialised struct
  rather than one from `bvn_int_alloc` — but these are `BVN_API`. The three
  entry points now return false for such an operand, and `bigint_ensure_cap` no
  longer promises capacity for a buffer that does not exist. Well-formed
  bignums are unaffected.

- **Clarified when `want_unit` runs relative to the two value callbacks.** The
  header and read/write API said "just before `on_verified`", which is
  imprecise: the hook runs after validation but ahead of **both** callbacks —
  it can abort the parse, and the two views of one value must not disagree — so
  an `on_unverified` consumer also sees a populated `converted`/`conv` on
  `ev_data`. That is a deliberate property, not an accident of emission order,
  and is now stated in `bovnar.h`, in the API doc's callback section and §7c, and
  pinned by a test. Behaviour is unchanged.
- **`max_conversion_length`** (`bvnr_read_flags_t`, Python
  `Reader.read_mem`/`read_fd`/`read_file`) — bounds the characters a `want_unit`
  conversion may produce; `0` selects `BVNR_DEFAULT_MAX_CONVERSION_LENGTH`
  (1024). Rendering an exact expansion is quadratic in its digit count and the
  count follows the value's *exponent* rather than its length, so `1e-9800` — seven
  characters — expanded to 9800 digits and cost a tenth of a second. A 9.9 KB
  document of them took **70 seconds**; through the WASM playground a 1.3 KB
  paste froze the tab for 16 s. Now 0.000 s and 8 ms respectively: the exponent is
  checked before the rational is built, and `bvn_rational_to_str` refuses on
  length before generating any digits (new `BVN_RATIONAL_TOO_LONG`, distinct from
  the -1 failures). Legitimate conversions are unaffected.
- **Angles are their own quantity kind, and so are logarithmic units.** Units
  that carry no SI dimension are now tracked by net exponent per *kind*, the way
  `bit`/`byte` already were, and two units must agree on every kind to be
  compatible.
  - **Angle** is one *shared* kind — `degree → radian` is a conversion people
    legitimately want, so giving each angle unit its own kind would have broken
    it. What the kind stops is an angle drifting into a plain count: `rev/min` is
    an angular rate and `rpm` a cycle rate, and with a revolution defined as 2π
    radians the two used to "convert" into each other off by exactly 2π. Same for
    `rad/s` against `Hz`, which are angular frequency and frequency. Steradian
    carries weight 2, because a steradian *is* rad², so `sr ↔ rad²` keeps
    working. `°→rad`, `rev→°` and `rpm→Hz` are unchanged.
  - **`dB` and `Np`** are separate kinds. Both carried factor 1.0 and no
    dimension, so they compared as compatible and `1 dB` converted to `1 Np` —
    wrong by a factor of 8.7, silently. They are logarithms of a ratio, not
    linear quantities (20 dB is a ratio of 100, not twice 10 dB), and `dB` is
    ambiguous on its own: 1 Np is 8.686 dB for a power quantity and 4.343 dB for
    a field quantity, which a document does not record. Each still converts to
    itself and across its own prefixes.
  - Percent, per-mille and ppm are deliberately *not* kinds: those are pure
    ratios, and converting 1 % to 0.01 is exactly right.
- **A datetime's sub-second digits were dropped when a timezone offset pushed
  its UTC year out of range.** The ISO literal can only carry a 4-digit year, so
  the writer falls back to the bare integer carrier when the UTC civil year is
  outside 0000–9999 — and that carrier has nowhere to put the fraction, which
  simply vanished. Spec §"ISO-8601 literals" promises those digits round-trip.
  Reachable when an offset crosses either end: `0000-01-01T00:00:00.5+23:59`
  parses (its *local* year is in range) but its UTC year is −1, and the `.5` was
  lost with no error. The writer now refuses with `error_invalid_datetime_literal`
  rather than emitting a value it knows is incomplete. A datetime *without* a
  fraction still falls back to the integer carrier, where nothing is lost.
  `bovnar query` prints a value rather than a document, so it still emits the
  carrier — but now warns on stderr which digits it left out, so a caller piping
  stdout is not misled into thinking it holds the whole instant.
- **UTC⇄TAI is now injective; an inserted leap second has its own value.**
  `23:59:60` used to collapse onto the following day's `00:00:00` on *every*
  epoch. On the civil epochs that is right — POSIX time runs a uniform 86 400 s
  day and has no second to spend — but on `tai` it discarded a real instant: the
  inserted second and the one after it shared a value, so `TAI→UTC→TAI` returned
  the later of the two and grew by one second, and the two distinct literals
  `2016-12-31T23:59:60Z` and `2017-01-01T00:00:00Z` were indistinguishable.
  `bvn_dt_tai_seconds_to_utc()` now renders an insertion as `23:59:60` of the
  preceding day and `bvn_dt_utc_to_tai_seconds()` maps that spelling back to the
  TAI second below the boundary, making the two mutual inverses over the whole
  int64 range. **This changes the value stored for a `tai` literal spelling a
  known leap second** (`2016-12-31T23:59:60Z` is now `1861920036`, was
  `1861920037`); conformance case DTLIT-127 is updated and DTLIT-129…132 pin the
  neighbouring rulings. The civil epochs are unchanged. A `:60` at an instant the
  leap table does not record as an insertion also still collapses, so a build
  predating an IERS announcement does not reject a document spelling a genuine
  future leap second. The property is now enforced by an exhaustive round-trip
  sweep (±1 day around all 28 table boundaries, second by second) in the datetime
  test. Two supporting fixes: the tz offset is folded into `minute` rather than
  `second`, which used to erase the `:60` spelling for every offset but `Z`; and
  the header records that `second == 60` is the one field value in a
  `bvn_datetime_t` that is not its own arithmetic value.
- **The exact-rational division failed at one specific denominator size.**
  `bvnf_bitdiv`, the bounded-memory fallback, shifted its running remainder left
  *before* reducing it, which transiently needs `bitlen(den)+1` bits. For a
  denominator of exactly `BVN_INT_MAX_BITS` — `10^9864` has bitlen 32768 — that
  shift cannot succeed, so the whole division aborted: `bvn_float_from_str(f,
  "1e-9864", 10)` returned false while both neighbours worked. Worse, the caller
  commits `_sign`/`_exp` before that division, so the failure left a float
  claiming `bvn_float_is_regular()` with an all-zero mantissa — a state the rest
  of the library treats as impossible, which `bvn_float_to_str` rendered as the
  illegal `"0e-9884"` in base 10 and as `"1p-32768"` in base 16. The loop now
  decides against `den>>1` and rewrites the update as `2(R-H) + (b-dlow)`, so the
  value being doubled always stays below `den`; every failure path also leaves a
  valid signed zero. The rewritten step is verified exhaustively against the one
  it replaces (all denominators 1…4000 × every remainder × both input bits, 16 M
  evaluations, 0 disagreements) and the bignum implementation against an exact
  `Fraction` oracle over 2008 parses straddling the boundary.
- **`bvn_float_str_bufsize(prec, 0)` and `(prec, 1)` crashed with SIGFPE.** The
  log2-of-base loop yields 0 for `base < 2` and the following division trapped.
  Public API with no documented precondition; the sibling `bvn_int_str_bufsize`
  already screened it.
- **`bvn_float_from_str` accepted trailing garbage.** It stopped at the first
  unrecognised byte and returned a value for the prefix: `"1.2.3"` was 1.2,
  `"12 34"` was 12, `"1e2e3"` was 100, `"nanana"` a NaN and `"infinity"` an
  infinity. The header promises false for a malformed literal, and
  `bvn_int_from_str` already rejected the analogous `"12 3"`, so the two string
  APIs disagreed.
- **Documented two limits rather than pretending they are not there.**
  `bvn_float_strtoieee_bin` promised correct rounding "across the whole range";
  it inherits the exact-rational capacity cap, so a decimal exponent beyond
  ~1e9865 saturates regardless of what the target format could hold. Invisible
  for every standard format up to binary128, whose own range is narrower — it
  binds only for binary256 and wider, where `"1e50000"` yields infinity instead
  of a finite normal. Lifting it needs a different algorithm, not a bigger
  constant. Separately, `bvn_int_shl` and `bvn_int_mul_u32` leave a *truncated*
  value when they return false, which the header's "defined state" wording did
  not convey; both now say so, and the general note warns that ignoring their
  return means computing with wrong digits rather than merely missing an error.
- **A consumer that aborted was called again.** The read/write API documents a
  callback returning `false` as "abort", but with `continue_on_error` set the
  reader treated it as a document error: it entered resync and kept calling the
  consumer that had just said stop, counting each of those as a recovery. A
  consumer whose buffer is full was re-entered for the rest of the file, and
  `recovery_count` was inflated by aborts that were never parse failures.
- **Resync inverted string-quote parity and swallowed the rest of the file.**
  The string sub-machine was chosen only when the error happened *inside* a
  string. If the offending byte was itself a `"` — illegal after a number, say —
  that quote was consumed and plain resync began inside the string it opened, so
  the string's *closing* quote read as an opening one. `.a = 1 "x"; .b = 2;`
  recovered nothing after `.a` and failed with `incomplete_bvnr_stream`, while
  the same shape with a non-quote offending byte recovered correctly.
- **A UTF-8 BOM was accepted at a non-zero byte offset.** Spec §3.2 allows it
  only at offset 0; the guard tested a lexer state that self-loops on
  whitespace, so a BOM after a space or a newline slipped through.
- **A version directive terminated by EOF was never parsed.** A document that is
  nothing but the directive was accepted straight from the EOF handler, which
  bypasses the only place the directive is checked — so `#!bovnar bogus` with no
  trailing newline was accepted, and `#!bovnar 99.9` passed even under
  `strict_version`. Both now behave identically with and without the newline.
- **An unterminated array was accepted, and swallowed the next key.** A `;` (or
  `}`) ends the whole value, so every `[` opened for it must already have been
  closed — but the lexer just zeroed the nesting level instead of checking it. No
  `ev_array_row_end` was ever emitted, so the following assignment's value was
  absorbed into the array and its key vanished entirely:

  ```
  .a = [1;        validate: OK
  .b = 2;         convert:  { "a": [1, 2] }      <- ".b" is gone
  ```

  Inside a struct the level was not even reset, so a *top-level* key afterwards
  migrated into the struct: `.s = { .a = [1; .b = 2; }; .c = 3;` produced
  `{"s": {"a": [1,2], "c": 3}}`. Three tools gave three different answers for one
  input — `validate` said OK, `convert` corrupted, `pretty-print` failed. The EBNF
  makes `]` mandatory; it is now `error_got_incomplete_bvnr_stream`. A `;` inside
  a struct nested in an array is still legal, so each array frame records the
  struct level it was opened at.
- **A prefixed currency could not be converted to its unprefixed form.**
  Currencies deliberately carry no SI dimension, so `bvn_unit_dimension_vector`
  fails for them and `bvn_units_compatible` calls every currency incompatible
  even with itself. The identity short-circuit rescued `$USD → $USD`, but
  `k~$USD → $USD` is not an identity — the two differ by one prefix and nothing
  else, with a factor of 1000 between them. Every entry point refused it, so a
  prefixed currency could neither be read through `want_unit` nor written under
  `BVN_UNIT_REDUCE`; `examples/financial.bvnr` and `crypto_portfolio.bvnr` were
  unwritable with that flag for exactly this reason, and both now round-trip
  idempotently.
  The three conversion entry points now fall back — only where
  `bvn_units_compatible` has already said no, so a unit that *does* have an SI row
  keeps taking the ordinary path — to a match on the base units and exponents
  with the prefixes ignored, scaling by the exact prefix ratio. SI and IEC
  prefixes are kept apart as powers of ten and two, which is what lets the
  rational path stay lossless without consulting the conversion table at all.
  Two genuinely different currencies are still refused: only the prefix may
  differ.
- **`convert bvnr → json` now reports what it drops and exits 1.** JSON cannot
  carry a unit, a symbol, a reference, an octet stream or an integer wider than
  64 bits — and the converter dropped all of it silently with exit 0, while going
  out of its way to hard-error on a sub-second datetime, which is the same class
  of loss. Refusing outright would make the command useless for any document with
  a unit, so the JSON is still written; the exit status and a stderr summary say
  it is not a faithful copy:

  ```
  convert: doc.bvnr: JSON cannot carry everything in this document; the output above is LOSSY:
    3 value(s) lose their unit entirely, e.g. .dist_km (k~m)
    1 symbol(s) become plain strings, e.g. .state
  convert: exiting 1: this JSON does not convert back to the document it came from.
  ```

  A document JSON *can* represent still exits 0.
- **`convert json → bvnr` turned a float inside an array into an integer.** The
  scalar path uses `bvnr_write_float` and keeps the family, but an array element
  was rendered with `%.17g` and emitted bare — so `2.0` came back as a `uint`
  inside an array and a `float` outside it, in the same document. Array elements
  now carry an explicit `<float>` annotation when the rendered text has no `.` or
  exponent to mark it.
- **The pretty-print idempotence test could not handle octet streams.** The CTest
  helper routed each pass's output through a CMake variable, which cannot hold
  the NUL bytes an octet stream is made of, so a document containing one was
  reported as non-idempotent when it was not. Both passes now go straight to
  files and are compared with `cmake -E compare_files`.
- **The writer's event grammar is now enforced.** Its header claims it "cannot
  be coaxed into emitting a stream the reader would reject", but validation
  checked only struct balance and the two depth caps. A value bare at the top
  level wrote `1;`; two assignments in a row wrote `.k=.k=1;`; a lone annotation
  end wrote `>`; a dimension separator outside an array wrote `/`; a keyless
  value inside a struct wrote `.k={1;};` — every one reported as success, none of
  them readable. Fuzzing event sequences, **16 312 of 200 000** finished with no
  error and produced unparseable bytes.
  `bvn_writer_validate_event` now tracks whether an assignment is awaiting its
  value, whether an annotation is open, whether a row just closed or a `/` was
  just written, and whether the stream has ended — and rejects any event that
  cannot occur there with `error_unknown_token_type`. `bvnr_write_finish`
  additionally refuses a document with a dangling assignment, an unclosed
  annotation or an open octet stream, alongside the unclosed structs and arrays
  it already caught. A null value is still written by sending `ev_data` with
  `token_is_null_value`; leaving the assignment open is not the same thing.
  The test that matters is a property, not a list: everything the writer accepts
  must re-parse. Over 600 000 fuzzed sequences across ten seeds, **zero**
  violations remain, and a 40 000-sequence version of the same check now runs in
  the suite.
- **`on_event` was shown the value the caller passed, not the bytes that were
  written.** A `BVN_UNIT_REDUCE` rescale replaces the digits and the unit, so an
  observer used to mirror or checksum the stream described a value that never
  reached the sink. It now receives the emitted text and unit; the caller's own
  struct is left untouched.
- **The DOM's integer accessors returned wrong values instead of failing.**
  `include/bovnar_dom.h` promises they "return false (leaving \*out UNCHANGED —
  no clamping or truncation)" when a value does not fit, but `dom_raw_i64` handed
  back a width-64 `uint`'s bit pattern reinterpreted as signed, so
  `18446744073709551615` read as `-1` with a `true` return — and the fixed-width
  wrappers then range-checked the already-wrong number, so `get_i8` "succeeded"
  too. `dom_raw_u64` guarded only `vt_sint`, so a pre-epoch `datetime` (a signed
  carrier) came back as `18446744073393932416`. Both now check the family's
  signedness. `bvn_dom_get_float` shared the fault and additionally refused any
  bignum beyond `int64`; it now renders those through their decimal text, which
  also fixes `bvn_dom_get_value_in_base_units` returning an exact `0.0` for a
  `uint:128` — the same value it uses to mean "no SI mapping", so a caller could
  not tell 1e23 metres from an unconvertible unit.
- **A number written with a string carrier was not a number in the DOM.** The
  builder dispatched on the token type alone, so `token_is_string` became a
  `BVN_DOM_STRING` regardless of the declared family — but spec §6.1 lets
  `uint`/`sint`/`float` take "Number **or string**", and the quoted form is the
  *only* way to write a non-decimal base whose digits are letters and the
  canonical way to write a wide integer. `examples/integers.bvnr` ships twelve of
  them and `pretty-print` re-emits them quoted, yet `get_u64`, `get_bigint` and
  `bvn_dom_int_to_str` all failed on every one. The declared family now decides
  what the node is; the token only says how the value was spelled. `bovnar query
  .uint256_max` prints the number instead of a quoted string.
- **The DOM's currency dimension fallback compared unit components
  positionally**, so an array whose elements spell the same unit in a different
  order — `[$USD·$EUR, $EUR·$USD]` — was rejected as heterogeneous. Unit
  multiplication commutes, and `bvn_unit_equal` has always known that; the
  fallback now matches order-insensitively too. It only ever false-rejected.
- **The writer could emit documents its own reader rejects**, against its stated
  contract that it "cannot be coaxed into emitting a stream the reader would
  reject":
  - An identifier over 255 bytes, or a string/symbol over 65535, was written and
    reported as success. Those are the reader's *fixed* caps (`uint8_t` and
    `uint16_t`), so no reader configuration can accept more. Now
    `error_identifier_too_long` / `error_string_too_long` /
    `error_symbol_too_long`.
  - A bare number token whose digits the lexer cannot read — `<uint:64,_16> 18F`,
    where `F` is a digit in base 16 but not a bare-number character — was emitted
    happily; the reader answers `unexpected_input_byte`. Now
    `error_base_requires_string_literal`, which until this release was an enum
    member nothing ever set. `5` and `1e3` still write bare in any base, because
    the lexer does read those.
- **`bvnr_canon_observer_set_version`'s "ignored after output has begun" guard
  was dead code.** It tests `ser.stream_begun`, which only
  `bvn_writer_validate_event` sets — and the observer drives the serialiser
  directly, so the flag stayed false forever and a late call injected the
  `#!bovnar` directive between two already-written events, producing a document
  that no longer parses. The observer now marks the flag when bytes actually
  reach the sink, which is what "output has begun" means: `ev_stream_start` emits
  nothing, so a caller driving this from a reader can still supply the version it
  learns mid-stream.
- **`bvnr_write_datetime()` produced a document the library cannot read back.**
  `datetime` is a spec-1.1 construct and the reader gates the family on a
  *declared* version, but the writer accepted it with the zero-init default,
  which emits no directive. The directive has to precede every value so it cannot
  be added retroactively; the writer now refuses with
  `error_unsupported_spec_version` unless `bvnr_write_flags_t.emit_version` is
  set.
- **A writer whose `bvnr_open_write_*` failed crashed on the first flush.** The
  sink's `push` is NULL, and nothing checked it; events kept returning `true`
  because they only fill the internal buffer, so checking every return value did
  not help. It now fails cleanly with `error_writing_to_sink`.
- **`BVN_UNIT_REDUCE` wrote values that did not match their unit.** The flag
  folds every prefix out of the unit — `k~m` becomes `m` — and the formatter
  discarded the scale `bvn_unit_reduce` hands back, while the writer emitted the
  value unchanged beside it. `5 km` was written as `.d=5 m`, `2.5 kg` as
  `.d=2.5 g`: the annotation saying one thing and the digits another, in a format
  whose premise is that a unit confusion is the expensive failure. It only looked
  right on the nineteen named SI derived units, where the collapse re-attaches the
  prefix, so testing it on `kN` gave no hint.
  The writer now scales the value with the unit — `.d=5000 m` — converting to the
  unit that will actually be *emitted* rather than to `bvn_unit_reduce`'s raw
  output, so `kN` correctly stays `1 k~N`. The rescale runs in exact rational
  arithmetic, so a 128-bit value keeps every digit rather than going through a
  `double`. Where the scaled value has no exact representation in the value's own
  base — 1/100 in base 16 needs a factor of five — the write fails with
  `error_value_out_of_range` instead of rounding, and a unit whose exponent
  overflowed the representable range is refused outright. `nan`/`inf` carry no
  finite value and pass through unscaled.
  The reduction applies to the unit wherever it appears: on the reader-driven
  path (pretty-print, canonicalise) the annotation's unit arrives as a token
  carrying the text the reader saw, and that text goes through the same reducer,
  so the annotation and the value never disagree and repeated canonicalisation is
  idempotent. The rescaled value is re-validated against its declared type — the
  writer's own validation runs before the rescale — so a count that stops being
  an integer (20 ms is not a whole number of seconds) or that no longer fits its
  width (20 ZiB in bytes, `uint:128` max scaled by 1000) is refused rather than
  emitted as a document the library's own reader rejects.
- **`BVN_UNIT_REDUCE` collapsed a data rate onto a frequency.** The named-SI
  collapse matched on the SI dimension vector alone, which is all-zero for every
  dimensionless kind, so `B/s` reduced to `Hz` — a unit the library itself calls
  incompatible with the one it replaced. It now requires the kind vectors to
  agree too. (The other `BVN_UNIT_REDUCE` defect, dropping the prefix scale, is
  still open: `5 km` is written as `5 m`.)
- **A terminating expansion was refused in large bases.** The fractional digits
  were produced via `base^k`, an intermediate that overflows the big-int ceiling
  long before the answer does, so `1e-6000` rendered in base 40 and hard-failed in
  base 50 — reported as "unusable output base". The digits now come from a long
  division whose intermediates stay the size of the denominator.
- **`bvn_int_str_bufsize` wrapped for a bit width near `UINT32_MAX`** and
  returned 2, so a caller sizing a buffer from it would allocate two bytes for a
  value needing hundreds of megabytes. The round-up is now done in 64 bits and
  saturates at `SIZE_MAX` where the true answer does not fit `size_t` at all, so
  the caller's allocation fails honestly instead of succeeding at the wrong size;
  `bvn_rational_str_bufsize`, which adds to it, saturates too. Not reachable
  through the library — every internal call passes a `bvn_int_bitlen()` capped at
  `BVN_INT_MAX_BITS` — but both are `BVN_API` and a caller may pass any width.
- **`bvnr_open_read_mem(NULL, len > 0)` opened successfully and then crashed** in
  the lexer. `buf` and `len` arrive independently (the WASM entry points take them
  as separate arguments), so the pairing is now checked. A NULL buffer with length
  0 stays valid — that is an empty document.
- **The shipped Release library could `abort()` the host process.** The
  `CMAKE_C_FLAGS_*` FORCE-overrides replace CMake's per-config defaults
  wholesale, and `-DNDEBUG` was not spelled back in, so `assert()` stayed live in
  the released `libbvnr`. A hand-filled `value_unit_t` with an exponent outside
  `unit_exponent_t` passed `bvn_unit_valid`, was silently dropped by the
  formatter, and then aborted the whole program inside `bvn_unit_prefix_factor`.
  Fixed on three levels: `bvn_unit_valid` now rejects any exponent
  `bvn_exponent_to_int` does not recognise (not just `exp_invalid`), the two
  `assert()`s in the unit helpers are replaced by the defensive return every
  caller already checks for, and the optimised configurations define `NDEBUG`.
- **`bvn_unit_to_string` produced text its own parser rejects.** A `bu_none`
  component past the plain `no_unit` shape contributes no symbol, so a prefixed
  or compound one formatted as `k~`, `²` or `m·k~` — which the writer would then
  embed in a document that no longer parses. Both formatters now refuse.
- **`bvn_unit_reduce` dropped a prefixed `bu_none`'s scale** — the same defect
  fixed earlier in `bvn_unit_to_si_rational`, of which this was the last holdout.
  The component still does not survive reduction (it carries no dimension), but
  its prefix is folded into `*scale` instead of vanishing.
- **`bvn_unit_convert_factor` still refused an identity conversion** for a unit
  with no SI row, so `$USD → $USD` failed there — and Python's `convert_factor`
  with it — after `bvn_unit_convert_value`/`_rational` had been fixed. Added as a
  *fallback* rather than a short-circuit, so every pair the normal path already
  handled keeps its exact previous result including the `requires_affine` signal.
- **A nonzero literal could be delivered as exactly `"0"`.**
  `bvn_float_parse_rational` reported `BVNF_RK_ZERO` both for a literal that *is*
  zero and for one whose exact rational will not fit the big-int budget, and the
  reader believed it: `.d = <float:64> 1.5e-20000 m;` arrived with
  `converted == true` and `conv.text == "0"`, no error — wrong by every digit,
  from the path that advertises exactness. Not an extreme-exponent corner either;
  it is a total-precision limit, so a 2000-digit mantissa at `e-9000` tripped it
  too. Underflow is now its own `BVNF_RK_UNDERFLOW`, refused by the exact parser
  (`error_value_out_of_range`, matching what the symmetric overflow side has
  always done) and still rounded to ±0 by the float parsers, whose behaviour is
  unchanged.
- **Converting a unit to itself was refused for angles and every currency.**
  There was no identity short-circuit, so `bvn_unit_convert_rational` /
  `bvn_unit_convert_value` consulted both units' exactness and SI-table presence
  for what is arithmetically a no-op: `90° → 90°` came back `error_unit_inexact`
  (the π factor never enters an identity) and `$USD → $USD`
  `error_unit_mismatch` (currencies have no SI row). That broke the documented
  pure base conversion, where the caller names the value's own unit precisely
  because it wants no unit change — so the natural generic hook, "every number in
  base 16, keep its unit", could not read a document containing one angle or one
  price. Identity now passes the value through exactly, for any *well-formed*
  unit: the short-circuit still rejects a malformed one (`exp_invalid`, a prefix
  the base disallows, a base index off the table), so it does not become the one
  place in the library that accepts a unit nothing else will.
- **`want_unit` handed Python a live pointer into reader memory.** The Python
  binding's `want_unit` wrapper passed `data_ptr.contents` — a ctypes *view* over
  the reader's buffer — where the event-callback wrapper deliberately builds a
  snapshot. A callback that kept the object segfaulted the interpreter once the
  reader was destroyed. Both wrappers now share one `Reader._snapshot`.
- **The ABI test did not field-check `bvnr_converted_t`.** `bvnr_abi_dump` emits
  all six of its field offsets, but `test_abi.py` compared only the total size,
  so a same-size field reorder in the ctypes mirror passed silently — for the one
  struct this release added.
- **`Reader.read_file()` dropped `want_unit`, `want_unit_allow_nonterminating`
  and `strict_version`**, making a conversion or strict-version read from a path
  impossible. It now forwards every `read_fd` option.
- **The WASM `converted` JSON fields were unreachable.** `evt_cb` emitted
  `converted`/`converted_base`/`converted_unit`, but no entry point ever set
  `flags.want_unit`, so the branch was dead code and the claim that the event
  JSON gained those fields did not hold. A new `bvnr_wasm_events_convert(ptr,
  len, unit, base, allow_nonterminating)` export arms the conversion, wrapped as
  `eventsConvert()` in the JS module. The checked-in `web/` and `dist/wasm/`
  artifacts, stale since before this release's ABI changes, are rebuilt.
- **The exact rational was unreachable from Python.** `conv.num`/`conv.den` were
  exposed as opaque pointers with nothing bound to read them, so the
  `want_unit_allow_nonterminating` fallback — where the rational is the *only*
  carrier of the value — was a dead end. `bvn_rational_to_str`,
  `bvn_rational_str_bufsize`, `bvn_int_to_str`, `bvn_int_bitlen` and
  `bvn_int_str_bufsize` are now bound, behind `BvnrData.converted_rational()`
  (an exact `(num, den)` pair) and `BvnrData.converted_in_base(base)`.
- **`token_type_t` was hand-copied as bare integers** in `writer.py` and
  `__init__.py` with no mirror in `enums.py` and no test, while every other
  public enum had both. It is now `enums.TokenType`, used at all sites, and
  `test_enums.py` parses `token_type_e` and `error_code_e` out of the header and
  compares them.
- **Prefixes on a component with an exponent were assembled wrongly** in the
  exact-rational factor builder (never released; the `double` path was always
  right). `bvni_prefix_exp_int` already folds in both `|exp|` and the sign of the
  component's exponent, but `bvn_unit_to_si_rational` multiplied by `|exp|` a
  second time and then inverted the result again for a negative exponent. So
  `1 km²` converted to `10¹²` m² instead of `10⁶`, and `6 m/km` came out `6000`
  instead of `0.006` — a factor of 10⁶. A prefixed `bu_none` (a dimensionless
  kilo) was dropped entirely, making this the one function in the library that
  disagreed with `bvn_prefix_unit_valid`, `bvn_unit_dimension_vector`,
  `bvn_units_compatible` and `bvn_unit_to_si_factor`; it is reachable through the
  `want_unit` hook, whose target unit comes from the caller rather than the
  parser. Both are covered by a sweep test that cross-checks the exact and
  `double` factor paths over unit × prefix × exponent, which catches the class
  rather than the instance.
- **Unit factors that were rounded doubles masquerading as exact rationals.**
  Sixteen units whose true SI factor is a non-terminating rational carried a
  17-significant-digit decimal, from which the table recovered a wrong "exact"
  value: `760 Torr` converted to `101324.9999999999988 Pa` instead of exactly
  `101325`, `1 rpm` to `0.016666666666666666 Hz` instead of `1/60`. They now
  carry their true rationals (torr `101325/760`, rankine `5/9`, rpm `1/60`, knot
  `463/900`, survey_foot `1200/3937`, delisle `−2/3`, newton_temp `100/33`, romer
  `40/21` + offset `36241/140`, denier `1/9000000`, the Prussian units, psi,
  horsepower), so an exact conversion is either right or refused — previously
  `°F`, alone in having been hand-overridden, refused while `°R` with the same
  5/9 slope returned a wrong answer flagged exact. `parsec` (648000/π au) and
  `oersted` (1000/4π A/m) are now flagged irrational alongside the angle units.
  The `double` factors for `psi` and `horsepower` were also truncated short of
  their correctly-rounded values and are corrected, which slightly changes
  `bvn_unit_to_si_factor` / `bvn_unit_convert_value` results for those two.
- **Reader reuse leaked the conversion scratch.** `bvnr_open_read_source` — which
  documents that one reader serves many documents — re-armed the validator with a
  `memset` that orphaned the `want_unit` bignums and text buffer. It now releases
  them first.
- **`bvn_rational_to_str` truncated silently.** A buffer too small produced a
  partial digit string reported as exact — a different number under an exact
  contract. It now refuses with `-1`, matching `bvn_int_to_str`.
- **Output bases above 36 were silently rewritten to 10.** `want_base` accepted
  only `2..36` even though bvnr writes `2..62`, `64` and `85`; a base-62 value
  asking to keep its own base got decimal instead. The full range now works, and
  an unusable base is `error_invalid_argument` rather than a substitution.
- **A value the rational builder could not represent was silently left
  unconverted.** `1e1000000` came back with `converted == false` and no error, so
  a consumer that trusted the hook read the original unit's digits — the exact
  misread `want_unit` exists to prevent. It is now `error_value_out_of_range`.
- **Editing a gendata document did not regenerate its table.** CMake only ran the
  generators when an output was missing or `BVNR_REGEN_TABLES` was set, so in an
  existing build directory a change to `src/gendata/*.bvnr` — correcting a
  conversion factor, adding a unit — silently never reached the library. The
  generator inputs are now `CMAKE_CONFIGURE_DEPENDS` and the tables are
  regenerated whenever an input is newer than an output.

## [1.1.0] - 2026-06-21

Spec 1.1 is **purely additive** over the frozen 1.0 baseline: every spec-1.0
document parses unchanged and decodes to the same values. The new constructs are
**opt-in** — a document enables them with a leading `#!bovnar 1.1` directive; a
document with no directive is treated as spec 1.0, and a 1.1-only construct in
such a document is an error, exactly as a 1.0 reader reports.

The one reserved-prefix exception to "parses unchanged": the `#!bovnar ` prefix
on a first-line comment is now read as a version directive, so a 1.0 document
whose first line is a comment beginning literally with `#!bovnar ` followed by a
non-version remainder is reported as a malformed directive rather than ignored.
`#!bovnar` was never a 1.0 convention, so this affects no realistic document.

### Added

- **Version directive** — an optional first-line `#!bovnar <major>.<minor>`
  comment declaring the spec version a document targets. Lexically it is a
  comment, so a 1.0 reader skips it; a 1.1+ reader records it
  (`bvnr_reader_get_declared_version`, surfaced by `bovnar validate` and
  the CLI `bovnar version`) and, with the `strict_version` read flag, rejects a
  version it does not support (`error_unsupported_spec_version`). A malformed
  directive is `error_invalid_spec_version`.
- **Richer string escapes** — `\u{1–6 hex}` (a Unicode scalar, UTF-8 encoded)
  and `\xHH` (one byte). A `\u{…}` surrogate or value above `U+10FFFF` is
  `error_invalid_codepoint`; `\x` must leave the string valid UTF-8
  (`\xC3\xA9` = "é", a lone `\xFF` is `error_invalid_utf8_byte`), and neither may
  smuggle an ASCII control byte past the rule that rejects raw control bytes
  (the whitespace controls remain available via the named escapes).
- **Native time family** — `<datetime:width,epoch>`: a timestamp carried as a
  **signed integer count of seconds** since a named epoch (`unix` default, `tai`,
  `gps`, `mjd`, `ntp`, `galileo`, `glonass`, `y2000`, `beidou`), distinct from a
  *duration* (a number with a time unit, e.g. `<float:64,s>`). The carrier
  validates like `sint`; the epoch is recovered with `bvnr_datetime_epoch_name`/
  `bvnr_datetime_epoch_mjd` and converted to civil time with the `bvn_datetime.h`
  helpers. In an array, datetime is its own kind and its epoch is a dimension
  (mixing epochs, or datetime with a plain number, is `error_array_element_type_mismatch`).
- **ISO-8601 datetime literals** — a value may be written as `YYYY-MM-DD`,
  `YYYY-MM-DDTHH:MM:SS`, with an optional trailing `Z`, a numeric `±HH:MM`
  time-zone offset, and/or a fractional second, instead of a raw integer; it is
  converted to the epoch-seconds carrier at parse time (the integer is what is
  stored and re-emitted, so round-trips stay idempotent). A bare literal with no
  annotation infers `<datetime:64,unix>`. A `±HH:MM` offset folds the written
  civil time to true UTC before the conversion (and before `tai`'s leap-second
  lookup, so atomic values stay correct). A fractional second (any digit count —
  ISO 8601 sets no limit) leaves the whole-second carrier unchanged, but the
  verbatim digits are preserved: consumers read them as a string
  (`bvnr_data_t.frac_data`/`frac_length`, or `bvn_dom_get_datetime_fraction()`),
  and a value carrying a fraction is pretty-printed back as an ISO literal
  (`<datetime:64> 2026-06-15T12:00:00.5Z`) so it round-trips idempotently. The UTC→epoch
  conversion is leap-second correct: civil epochs (`unix`/`mjd`/`ntp`/`y2000`)
  use the uniform scale and `tai` applies the IERS leap-second table; the atomic
  GNSS epochs (`gps`/`galileo`/`glonass`/`beidou`) reject a literal
  (`error_datetime_literal_unsupported_epoch`) because there is no
  round-trippable civil⇄seconds inverse for them — use an integer carrier there.
  A second of `60` is accepted as a UTC leap second; since the carrier is whole
  epoch-seconds it normalises onto the following second (`2016-12-31T23:59:60Z`
  and `2017-01-01T00:00:00Z` store the same `unix` value), the correct POSIX
  reading. A malformed or out-of-range literal is `error_invalid_datetime_literal`.
- **Reference array indexing** — a reference path may index arrays,
  `&.matrix[0][1]`. The index is stored verbatim/unresolved at the byte layer and
  interpreted by `bvn_dom_lookup` (which also backs `bovnar query`): a flat
  `/`-row matrix is addressed `[row][col]`, a 1-D array `[i]`, and nested arrays
  descend one index per level.
- **C API** — `bvnr_version()`, `bvnr_version_string()`, `bvnr_spec_version()`,
  `bvnr_reader_get_declared_version()`, `bvnr_peek_version()`,
  `bvnr_write_version()`, `bvnr_write_datetime()`, `bvnr_datetime_epoch_name()`,
  `bvnr_datetime_epoch_mjd()`, `bvnr_datetime_epoch_index()`,
  `bvnr_canon_observer_set_version()` (so a canonicalising observer can re-emit
  the source's version directive); the
  `bvnr_read_flags_t.strict_version` and `bvnr_write_flags_t.emit_version` flags;
  the `BVNR_SPEC_VERSION_MAJOR`/`MINOR` macros; the `vt_datetime` family; and
  error codes `error_invalid_spec_version` (42), `error_unsupported_spec_version`
  (43), `error_invalid_codepoint` (44), `error_invalid_datetime_literal` (45),
  `error_datetime_literal_unsupported_epoch` (46).
- **Python bindings** — `bovnar.version()`, `spec_version()`, `peek_version()`,
  `Reader.declared_version`, `Quantity.epoch_name`/`epoch_mjd`,
  `ValueTypeFamily.DATETIME`; `dumps()` automatically prepends `#!bovnar 1.1`
  (and emits the `<datetime>` annotation) when the value tree needs it, so typed
  round-trips are lossless. The numpy bridge maps a unix-epoch datetime array
  to/from `datetime64[s]` (`to_numpy`/`from_numpy`/`array_to_bvnr`); a non-unix
  epoch is refused with a pointer to `dtype='int64'` for the raw seconds, NaT is
  rejected, and `array_to_bvnr` prepends the `#!bovnar 1.1` directive.
- **Python — lossless access to decimal, fixed-point and wide binary floats.**
  `Quantity` gains `.decimal()` / `.fraction()` (exact value from the verbatim
  literal), `.stored_value()` / `.ieee_bits()` / `.fixed_point()` (bit-exact
  IEEE materialisation for the 16/32/64/128/256 encodings), and a `from_number`
  constructor; `dumps()` accepts `Decimal` / terminating `Fraction`. The NumPy
  bridge decodes `float_dec` / `float_fix` / `float:128`+ to exact `Decimal`
  object arrays (typed path; `dtype='float64'` is the lossy escape) and
  `from_numpy` gains `float_format=` to write them. The arbitrary-precision
  `bvn_float` API is exposed as `bovnar.BvnFloat`. Materialisation covers the
  full representable range via pure-Python encoders past the parser's cap.
- **Tooling** — `bovnar version` subcommand; `datetime` keyword in all five
  syntax highlighters and the web playground; the conformance suite grew to
  **306 cases** (groups `version`, `datetime` — including the ISO-literal
  `DTLIT` cases — plus escape/reference additions), passing in both self-test
  and `--iut` modes.
- **Windows build support (64-bit MinGW64 and MSVC).** The library (`bvnr.dll` +
  import lib, plus a static archive) and the `bovnar` CLI now build on Windows.
  A portability shim (`src/utils/bvn_port.h`) maps the fd I/O onto the CRT and —
  critically for a binary format — forces binary mode on opened files and on
  stdin/stdout/stderr so no CRLF translation can corrupt the byte stream; the
  bench timing uses `QueryPerformanceCounter`/`GetThreadTimes`. DLL symbols are
  exported via CMake `WINDOWS_EXPORT_ALL_SYMBOLS` (matching the export-all-extern
  behaviour on ELF). On MSVC the static archive is `bvnr_static.lib` (the DLL
  import lib takes `bvnr.lib`); MinGW keeps the unified `libbvnr` names. A new
  `Build & Package` CI workflow builds both Windows toolchains and a native Linux
  target, smoke-tests the CLI, and publishes the build + amalgamation artifacts.
  The POSIX test harness (fork/exec IUT, socketpair, sigaction) is not built on
  Windows.
  The CLI's decorative UTF-8 UI (events/bench tables, box-drawing rules) renders
  on a real Windows console via `WriteConsoleW` — the CRT's byte path garbles
  multi-byte UTF-8 even with the console code page set to UTF-8 — while piped or
  redirected output stays byte-exact raw UTF-8.
- **Streaming — octet-demux key scoping.** `bvnr_demux_set_key()` (Python:
  `stream.mux_load(..., key=…)`) restricts the demultiplexer to octet streams
  opened under a given key, so a document may mix one multiplexed stream with
  ordinary binary octet payloads (those under other keys are ignored rather than
  misread as channel/length framing). With no key set the demux behaves as before
  (every octet stream is demuxed).
- **Currency catalogue — ZWG and XCG.** The Zimbabwe Gold (`$ZWG`) and Caribbean
  guilder (`$XCG`) join the fiat catalogue as an **ABI-stable extension segment**
  (`bu_zwg = 378`, `bu_xcg`), appended after the unit block rather than inside the
  frozen 134–347 currency region so no existing enum value shifts. They are
  recognised as currencies via the `BVN_CURRENCY_EXT_*` range; XCG carries ISO
  numeric 532 (inherited from ANG, which it replaces).
- **Unit aliases and symbol fixes.** The dalton accepts `amu` and `u` as input
  aliases (canonical output stays `Da`); the micro SI prefix accepts an ASCII `u`
  on input (canonical output stays `µ`); and the Rankine symbol is normalised
  (absolute scale, factor 5⁄9, no affine offset). These are additive input
  conveniences — canonical serialisation is unchanged.

### Changed

- Library/package version bumped to **1.1.0**; spec 1.1 ships as the additive
  successor to the frozen 1.0 baseline, which remains the stable floor every 1.x
  release must honour.
- **Dropped the `libm` dependency.** The library never used a transcendental
  function — every `pow()`/`log10()` call raised to or recovered an *integer*
  power, so they are replaced with integer exponentiation helpers (`bvni_ipow`,
  `bvni_pow10`) that are bit-exact for powers of two and for powers of ten in the
  exactly-representable range. The remaining `<math.h>` uses (`fabs`, `isfinite`,
  `isinf`, `isnan`, `signbit`) are compiler builtins that need no runtime library.
  Consumers no longer have to add `-lm` when linking `libbvnr`.
- **Dropped the optional `libgmp` build-time dependency.** It was used only by an
  `#ifdef WITH_GMP` cross-check in `bvnr_int_test`; the bignum tests already cover
  parsing, formatting, and a 32768-bit round-trip without it. The cross-check, the
  `FindGMP.cmake` module, and its CMake wiring are removed, so the project now has
  no external library dependencies at build time or runtime.
- **Unified library artifact name.** Both libraries now share the base name
  `bvnr`, differentiated only by extension: the static archive is `libbvnr.a`
  and the shared object is `libbvnr.so` (was `libbvnr_static.a` /
  `libbvnr_shared.so`). Link with `-lbvnr` (this resolves to the shared object
  when both are present; force the archive with `-l:libbvnr.a` or static-link
  flags). The CMake target names `bvnr_static`/`bvnr_shared` are unchanged.
- **Versioned soname for the shared library.** A system/distro build now installs
  `libbvnr.so.1` (SOVERSION 1) so the dynamic loader can detect an ABI
  mismatch — relevant because public by-value structs grew this release
  (`bvnr_data_t` gained `frac_*`, `bvnr_read_flags_t` gained `strict_version`),
  appended so offsets are preserved but requiring consumers to recompile against
  the 1.1 headers rather than mixing a 1.0-compiled object with the new library.
  The Python wheel keeps the unversioned `libbvnr.so` it loads by name.
- **Installable build (`cmake --install`).** A non-wheel build now installs the
  shared and static libraries (with the `libbvnr.so.1` soname chain), the `bovnar`
  CLI, and all public headers, plus a **pkg-config** file (`pkg-config --cflags
  --libs bvnr`) and a **CMake package** (`find_package(bovnar)` →
  `bovnar::bvnr` / `bovnar::bvnr_static`). Previously only the freedesktop MIME
  entry was installed, so the soname/headers were built but never laid down.
- **Conformance output upgraded to TAP version 14.** The 306 cases are now grouped
  into TAP 14 *subtests* — one indented child stream per case group (21 groups),
  each with its own plan and rolled up into a parent test point — so the report is
  hierarchical instead of a flat list. The parent plan counts groups (`1..21`);
  individual case points and YAML failure diagnostics are emitted inside each
  subtest. Self-test and `--iut` modes and the exit-code contract are unchanged.
- **Website publishing is now scripted, and the docs/PDFs are no longer
  duplicated or committed as build artifacts.** The Markdown documents are
  canonical under `doc/`; `web/doc` is a symlink to `../doc` (previously the real
  files lived under `web/doc/` and `doc/` held symlinks). `gen_doc_pdfs.py`
  renders the per-document PDFs and the `bovnar-docs-pdf.zip` bundle under
  `build/doc/pdf/` (git-ignored) instead of committing them into the tree. A new
  `publish_web.sh` deploys everything under `web/` to the live web root with
  `rsync`, resolving the `web/doc` symlink so the docs land as real files under
  `<webroot>/doc`; `--pdf` (re)builds the PDFs first and `--delete` prunes files
  removed locally.

### Fixed

Hardening uncovered while developing 1.1:

- CLI `query` now prints every byte of an octet stream as `\xHH` instead of only
  the first byte (it was silently truncating the value).
- CLI `convert` now reports a usage error when `--from`/`--to` is given without a
  value (previously the flag was misread as the input filename), and refuses to
  emit output larger than 4 GiB rather than skipping the homogeneity/unique-key
  re-validation that backs its "no unrepresentable output" contract.
- CLI `bench --size` rejects a zero or out-of-range value instead of letting the
  benchmark buffer's size arithmetic overflow.
- Thread-safety: the lexer's run-LUT initialisation now runs before `main()` on
  MSVC too (via a `.CRT$XCU` registration), matching the GCC/Clang constructor.
  Previously the MSVC build initialised the tables lazily on first reader use,
  which could race if two threads created their first reader concurrently.
- The streaming demux now rejects a declared message length exceeding `SIZE_MAX`
  on 32-bit hosts, mirroring the document-stream path.
- The streaming demux now reassembles a message-length varint that is split
  across two octet chunks (it previously treated a partial length varint as a
  desync); this makes the consumer fully general for the documented varint-
  prefixed wire convention — data *and* length varints may span chunks, with only
  the per-chunk channel-routing varint required to be whole. `bvnr_mux_send` was
  already conformant, so this changes no produced byte stream.
- Documented the ownership and error contracts in `bovnar_dom.h` and `bvn_int.h`
  (which functions consume/borrow/own their arguments and results, and the
  "returns false, leaves out-param unchanged" convention) — the previous headers
  left several leak/double-free traps undocumented.
- Python `dumps()` spec-1.1 directive detection (`_uses_spec_1_1`) now reads a
  Quantity's `vtype.family` defensively, so a Quantity built (via the low-level
  constructor) with an unexpected `vtype` reports "not a datetime" instead of
  raising `AttributeError` mid-serialisation.
- The datetime epoch index was misread as a numeric base in the DOM/parse layer
  (`<datetime:gps> 1010` decoded as base-2); now always decimal.
- Reader/writer validation symmetry for datetime: the writer now emits the
  width and epoch, and range-checks the value and width exactly as the reader
  does (no longer emits a datetime the reader would reject).
- An empty reference index `&.m[]` is rejected by the lexer, matching DOM
  resolution and `bvn_validate_reference`.
- Restored the `-DBVNR_WERROR=ON` (`-fanalyzer`) build.
- Python `dumps()` no longer drops a datetime annotation or crashes on a typed
  array; exposes the epoch helpers.
- `to_dec*` lost precision when encoding a decimal float near a format's Emax:
  `bvnf_dec_render_roundodd` estimated the decimal exponent with a coarse
  `log10(2)` approximation (`0.302`) whose error, past ~2000 bits of binary
  exponent, exceeded the digit-count safety margin and truncated the rendered
  coefficient — decimal128's 34-digit maximum encoded to ~20 significant digits.
  Now uses `0.30103`, exact across the whole exponent range.
- Python NumPy/pint bridge hardening: reject `null` elements that a `bool`/`str`
  dtype would silently coerce; key the pint reverse-map cache on the registry
  (`WeakKeyDictionary`) to avoid `id()`-reuse aliasing and a leak; `to_pint_array`
  rejects datetime arrays; raw Python ints past int64 widen to uint64/object
  instead of raising a bare `OverflowError`; `from_numpy` rejects masked arrays;
  clearer errors for invalid `unit=` / non-pint / mixed unit-and-dimensionless
  inputs.
- **Base-10 rendering of a wide-magnitude float looped forever.** The same coarse
  `log10(2)` (`0.302`) estimate also drove the sibling renderer `bvnf_to_str_dec`;
  once the binary exponent dwarfed the precision it placed the whole digit window
  above the value, and the leading-zero strip then rotated zeros without
  terminating — an infinite loop reachable through `bvn_float_to_str` /
  `bvnr_write_bvnf` (e.g. `to_str(1e4900)` at 16-bit precision). Now uses
  `0.30103` and bounds the strip loop.
- **A `datetime` wrongly accepted an inline unit.** `<datetime:64> 100 m` parsed
  (the "no unit" check was gated on the ISO-literal form only) and the unit was
  then silently dropped on emit; an inline unit on any datetime carrier is now
  `error_unit_illegal`.
- **Non-rectangular sibling arrays of equal cell count were accepted.** A 2×3 and
  a 3×2 block (both six cells) compared equal because the DOM shape check used
  only the flattened cell count; it now also compares the row geometry, so the
  mismatch is caught (`error_array_row_size_mismatch`).
- **An inline unit was dropped when the value also had an explicit (unit-less)
  annotation.** `<float:64> 9.81 m/s` canonicalised to `<float:64> 9.81`, silently
  changing the decoded value; the serializer now re-appends the inline unit.
- **An unbalanced `ev_struct_end` on the canon-observer path** underflowed the
  indent depth, amplifying the next indent to ~4 billion tab bytes; it is now
  rejected, matching the array-depth guard.
- **The JSON converter silently dropped a datetime's sub-second fraction.**
  `bovnar convert` to JSON now refuses a fractional datetime (the integer carrier
  cannot represent it) with a diagnostic rather than truncating, and `bovnar
  query` emits the faithful ISO literal instead of the bare carrier.
- **Python — lossless typed datetime/symbol/reference round-trips.**
  `loads(typed=True)`→`dumps()` dropped a datetime's sub-second fraction, emitted
  an illegal `<datetime:…,no_unit>` annotation for a bare ISO literal, and
  downgraded a symbol or reference to a quoted string; all now round-trip, and
  `dumps()` prepends `#!bovnar 1.1` for a reference that indexes an array
  (`&.a[0][1]`). The numpy bridge no longer silently truncates a DOM
  decimal/wide-binary float under `dtype=object` — it errors, pointing at
  `loads(typed=True)` for exact `Decimal`s (the lossy `dtype='float64'` opt-in is
  unchanged).
- **Tooling** — the Vim highlighter now colours ISO-8601 datetime literals (it
  mis-tokenised them as `YYYY`-dash-`MM` integers, and not at all inside arrays
  or structs); the cibuildwheel test phase now hard-fails if a built wheel cannot
  load its bundled library (the lib-dependent tests are skipif, so they would
  otherwise pass-as-skipped).
- **The canonicalising observer dropped the version directive.**
  `bvnr_canon_observer_*` re-emitted only the value stream, so canonicalising a
  spec-1.1 document (datetime, the new escapes, …) produced output a reader then
  rejected as 1.0. It can now be told the source's declared version
  (`bvnr_canon_observer_set_version`, wired into the `events -d` debug dump), and
  emits the directive ahead of the first event so the canonical form re-reads.
- **CLI `validate` reopened the input file for its second (DOM homogeneity)
  pass.** It now rewinds and reuses the one descriptor, so both passes see the
  same bytes (closing a reopen TOCTOU window) and there is a single fd to manage.
- **`bvn_int_getbit`/`bvn_int_setbit`** now reject a negative bit index up front
  instead of relying on word-index bounds to mask the otherwise-undefined shift.
- **Python** — `make_data_key`'s return annotation was `BvnrData` though it
  returns a `(data, raw)` tuple; corrected, and an unused writer import dropped.

Second release-review pass:

- **Error recovery dropped a value's unit serial when unwinding an array.** The
  `]` and `}` resync paths restored the parent frame's type and unit but not its
  `parsed_unit_serial`, so a later element boundary could mis-decide unit
  restoration and emit wrong unit metadata on recovered (`continue_on_error`)
  output; both paths now restore it, matching the `;` resync and normal
  array-outro paths.
- **MinGW64 file I/O used a 32-bit `off_t`/`lseek`.** Only the MSVC shim widened
  them, so the CLI's 4 GiB file-size guard could never fire on MinGW (and a
  >2 GiB seek wrapped); both Windows toolchains now share the 64-bit `off_t` /
  `_lseeki64` remap.
- **A non-blocking socket source/sink failed the whole parse on `EAGAIN`.** The
  fd backend now waits for readiness via `poll()` (POSIX) on
  `EAGAIN`/`EWOULDBLOCK` instead of treating a would-block as a hard error.
- **Duplicate-key enforcement was skipped under memory pressure.** When the
  temporary key array could not be allocated, `bvn_dom_keys_unique` returned
  "OK"; it now falls back to an in-place O(n²) compare so a document with
  duplicate keys is rejected regardless of available memory.
- **A value-side inline unit ignored the writer's unit flags and was dropped
  silently when over-long.** The serializer now renders it with
  `bvn_unit_to_string_ex` (honouring reduce / ASCII-exponent, as the annotation
  path does) and treats an overflow as an error rather than emitting the value
  without its unit.
- **Hardening:** the streaming varint decoder now rejects an overlong 10th byte
  instead of truncating it; the DOM integer `strtoll`/`strtoull` fallback
  width-clamps its result so a node's tag and payload cannot disagree; the GNSS
  week→epoch helper takes the TAI offset as a parameter (no longer assumes
  GPS/Galileo's +19 for any future constellation); Easter-relative holiday dates
  near the maximum supported year no longer return a stale date with success; and
  the JSON converter guards its output-size multiply against overflow on a 32-bit
  `size_t`.

### Known gaps / deferred

- ISO-8601 datetime literals carry a whole-second integer (a fractional part is
  preserved verbatim and round-tripped, but does not participate in the value's
  arithmetic) and cover `Z` and numeric `±HH:MM` offsets; literals for the atomic
  GNSS epochs are still rejected (no round-trippable inverse).
- The browser playground parser (`web/bovnar_parser.js`) recognises ISO-8601
  literals, infers the `<datetime:64,unix>` default for a bare literal, and
  surfaces the epoch — but, by its lenient design, it still displays literals as
  written without converting them to epoch seconds or leap-second/calendar-
  validating them; the C reader remains the authority for both.

See [`RELEASE_NOTES_v1.1.0.md`](RELEASE_NOTES_v1.1.0.md) for the full notes.

## [1.0.1] - 2026-06-15

Implementation maintenance over 1.0.0 — no format change; every 1.0.0 document
is unaffected.

## [1.0.0] - 2026-06-15

First stable release and the **format freeze**: a document valid under spec 1.0
stays valid, and decodes to the same values, under every 1.x release. This
covers the lexical grammar, the seven type families and their annotations,
arrays (including the homogeneity rules), structs, octet streams, references,
and the error-code values. See [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md)
for the full notes.

[Unreleased]: https://github.com/sothis/bovnar/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/sothis/bovnar/releases/tag/v1.1.0
[1.0.1]: https://github.com/sothis/bovnar/releases/tag/v1.0.1
[1.0.0]: https://github.com/sothis/bovnar/releases/tag/v1.0.0
