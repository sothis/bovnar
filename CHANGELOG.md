# Changelog

All notable changes to Bovnar are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning is **semantic
versioning of the format (spec)**; the reference implementation
(`BVNR_VERSION_STRING`, `bovnar.__version__`, the CMake project version) tracks
it in lockstep. The highest spec a build understands is reported by
`bvnr_spec_version()` / `BVNR_SPEC_VERSION_MAJOR`·`MINOR`.

## [Unreleased]

Reference-implementation only; the on-the-wire format is unchanged. **The ABI
breaks**: `bvnr_data_t` and `bvnr_read_flags_t.want_unit` changed shape (see
below) — rebuild consumers against the new headers. **SOVERSION is bumped 1 → 2**
(`libbvnr.so.2`), so a binary built against 1.x headers fails to load rather than
reading the grown by-value structs at the wrong size.

### Added

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

### Fixed

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

## [1.0.1]

Implementation maintenance over 1.0.0 — no format change; every 1.0.0 document
is unaffected.

## [1.0.0]

First stable release and the **format freeze**: a document valid under spec 1.0
stays valid, and decodes to the same values, under every 1.x release. This
covers the lexical grammar, the seven type families and their annotations,
arrays (including the homogeneity rules), structs, octet streams, references,
and the error-code values. See [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md)
for the full notes.

[1.1.0]: https://github.com/sothis/bovnar/releases/tag/v1.1.0
[1.0.1]: https://github.com/sothis/bovnar/releases/tag/v1.0.1
[1.0.0]: https://github.com/sothis/bovnar/releases/tag/v1.0.0
