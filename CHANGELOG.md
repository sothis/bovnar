# Changelog

All notable changes to Bovnar are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning is **semantic
versioning of the format (spec)**; the reference implementation
(`BVNR_VERSION_STRING`, `bovnar.__version__`, the CMake project version) tracks
it in lockstep. The highest spec a build understands is reported by
`bvnr_spec_version()` / `BVNR_SPEC_VERSION_MAJOR`·`MINOR`.

## [Unreleased]

The on-the-wire format grows only additively: the unit parser accepts one new
input spelling (the compact prefix form, below), every existing document parses
to exactly the same values, and the canonical output form is unchanged. **The ABI
breaks**: `bvnr_data_t` and `bvnr_read_flags_t.want_unit` changed shape (see
below) — rebuild consumers against the new headers. **SOVERSION is bumped 1 → 2**
(`libbvnr.so.2`), so a binary built against 1.x headers fails to load rather than
reading the grown by-value structs at the wrong size.

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

  A **cross-vocabulary suite** (53 concepts, 2847 assertions) checks every
  spelling of a concept against every other one, pairwise: equality, coherent-SI
  factor, dimension and round-trip, plus a negative table for the pairs that look
  interchangeable and are not. It found the ampere missing from the UCUM table on
  its first run — one of the seven SI base units had no UCUM spelling, and no
  single-vocabulary test had asked.

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
