# TODO

Open items, bucketed by the release that can take them.

## 2.2.1 — patch (bug fixes, no API change)

- [ ] `bvnr_write_bvni_unit` validates the width inside the annotation, after
  `emit_key`, so an illegal width leaves a dangling `.k=` in the output. Check
  it before `emit_key`, as `bvnr_write_bvnf_base_unit` does
  (`src/writer/bovnar_write_utils.c:623`).
- [ ] A bad byte in the leading comment sets `ver_checked` and skips `#!bovnar`
  enforcement: with `strict_version` set, `#!bovnar 9.9 \xff` reports
  `invalid_utf8_byte`, not `unsupported_spec_version`
  (`src/lexer/bovnar_lexer.c:363`; verified with a live probe).
- [ ] The DOM builder's 256 MiB octet cap returns false without setting
  `last_error`, so an oversized stream reports `error_scanner_callback_failed`
  (`src/dom/bovnar_dom_builder.c:248`).
- [ ] `bvn_float_alloc` / `bvn_float_init_buf` leave `_reserved[4]`
  uninitialised; zero it as the other initializers do
  (`src/utils/bvn_float.c:228`).
- [ ] `bvn_ser_reduced_number` returns -1 with `ser_error` unset on the
  `malloc` failure and labels later allocation failures
  `error_value_out_of_range`; set one error consistently
  (`src/writer/bovnar_writer.c:2069`).
- [ ] Rust `Writer::open_mem` allocates `vec![0; cap]` with the caller's `cap`;
  a huge `cap` aborts instead of returning `Error::Argument`
  (`rust/src/ffi.rs:1549`).
- [ ] Rust `Decimal::terminating` loops over the denominator's 2s and 5s
  without the `FRACTION_EXPONENT_CAP` `Quantity::fraction` uses; a hand-built
  `Value::Fraction` can spin (`rust/src/decimal.rs:139`).
- [ ] Python `Unit(5)` raises `BovnarArgumentError` for a wrong *type*,
  against the TypeError-for-wrong-type rule; `compatible_with` /
  `convert_factor` inherit it (`python/bovnar/unit.py:88`).
- [ ] `DomDoc.parse` / `dom_parse` surface raw `ctypes`/`len()` `TypeError`s
  (e.g. `dom_parse(5)`), and disagree on `str` (`dom_parse` accepts it,
  `DomDoc.parse` does not); route both through the same argument checks
  (`python/bovnar/dom.py:437`).

## 2.3.0 — minor (additive)

- [ ] Python feed parity: per-call `want_unit` / `on_error` / `on_verified` /
  `on_unverified` and `reset()` on `DemuxFeed` / `DocumentFeed`;
  `parse_embedded` from a reader; a recovery count (or callback) on
  `loads` / `load`.
- [ ] Rust reader API: `iter_mem_with_options` / `iter_reader`;
  `canonicalize_to(writer)`; a path-based reader (`read_file`);
  `Quantity: Display`; `literals::looks_like_double`.
- [ ] Bind the remaining `bvn_float` entry points in Python:
  `bvn_float_is_regular`, `bvn_float_from_double`, `bvn_float_set_zero`.
- [ ] Make `BovnarArgumentError` / `BovnarParseError` also inherit
  `ValueError` (additive; `except ValueError` keeps working).
- [ ] `Reader.iter_mem` builds the whole event list before yielding, so it is
  not lazy and a late failure yields nothing; yield incrementally
  (`python/bovnar/reader.py:732`).
- [ ] One rule for `str`: `Reader.read_mem` rejects it while `loads`, the
  feeds and `parse_embedded` encode it. Widen `read_mem` or narrow the rest.
- [ ] Surface a datetime's fractional seconds in the plain (non-typed) tier, so
  `loads` → `dumps` does not lose them
  (`doc/datetime_fractional_seconds.md` §5).
- [ ] Comment may start after any token (v2 §14; a pure 1.x loosening — spec 1.3 /
  library 2.3.0, not a spec 2.0 break).
- [ ] Measurement uncertainty `±` (`doc/measurement_uncertainty.md`; spec 1.3
  proposal) — additive grammar; confirm the ABI/struct-growth policy first.
- [ ] Either implement `error_expected_string_in_array` /
  `error_expected_number_in_array` / `error_recovered`, or mark them
  non-normative (`include/bovnar.h:232`).

## 3.0.0 — major (breaking)

- [ ] DOM builder refusal model: align Rust's by-value `DomDoc::add` (node
  consumed, `DuplicateStructKey`) with Python's (node left with the caller,
  `BovnarArgumentError`).
- [ ] `bvnr_on_error_fn` returns `void`; add a `bool` return, or a
  `stop_on_error` flag in `bvnr_read_flags_t._reserved[2]`.
- [ ] Rust `_with` naming: `_with` = loose scalars/handlers, `_with_options`
  = options struct, `_with_policy[_capped]` = `UnitPolicy`; fix
  `parse_reader_capped_with_policy`'s word order.
- [ ] Rust API consolidation: fold the six `parse*` functions into
  `parse_with_options(data, &ReadOptions{typed,..})`; the eight `read_*`
  behind one `_with`; the eight `load_documents*` behind one options struct.
- [ ] Rust `to_string(&Value, bool) -> Result<Vec<u8>>` → `to_bytes`; split
  `Error`'s `code: i32` from `ErrorKind`; make `UnitFlags` a real type.
- [ ] `float:16` / `float:32` subnormal shortest form (spec question; the
  current form round-trips).
- [ ] `max_file_size` finite default on untrusted input (today unlimited).
- [ ] Base-unit id space: tags 10–90 are taken; a new vocabulary needs a tag
  outside that range (ABI).

## Spec 2.0 — format major (proposals, not scheduled)

- [ ] `doc/bovnar_v2_proposals.md`: one conformance tier (§3), remove the `/`
  dimension row (§4), quoted keys (§5), inline unit as sugar (§6), named type
  parameters (§7), the symbol sigil (§8), `null` written out (§9), a real
  `%bovnar` directive (§10), radix literals (§11), group exponents (§12),
  octet streams declared/checked (§13). Priority if cut: §3, §6, §8.
- [ ] §3 also adds a `reject_duplicate_keys` reader flag, defaulting on
  (opt out), so duplicate keys stop being a DOM-only rule. The structural
  checks (homogeneity, rectangularity, shape) are unconditional.
- [ ] §14 (comments after any token) is **not** here: it is a pure 1.x
  loosening, tracked under 2.3.0.