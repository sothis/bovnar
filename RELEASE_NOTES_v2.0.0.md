# Bovnar 2.0.0 — units from other vocabularies, and a reader that converts

**The 2 is the library's, not the format's.** Bovnar 2.0.0 implements **spec
1.2**, which is additive over 1.1: no accepted document changes meaning and the
version directive stays in the 1.x line. The major moved because programs built against
1.1 have to change — the C ABI breaks and so does the Python API. From this
release on, the library version and the spec version are separate numbers.

**Spec 1.2 lets a document write its units in the vocabulary the data came
from.** A unit parameter of the form `namespace:code` is read as a code of UCUM,
UN/ECE Rec 20/21, QUDT, UDUNITS-2, OM 2 or the CF standard-name table and
translated, at parse time, into the same unit a native spelling produces.

```bovnar
#!bovnar 1.2
.systolic = <float:64,ucum:mm[Hg]>        120.0;   # UCUM          → mmHg
.weight   = <float_dec:64,unece:KGM>       12.5;   # UN/ECE Rec 20 → k~g
.speed    = <float:64,qudt:M-PER-SEC>       9.81;  # QUDT          → m/s
.flux     = <float:64,udunits:kg m-2 s-1>   0.5;   # UDUNITS / CF
.volume   = <float:64,ucum:mL{total}>       1.5;   # the annotation is kept
```

Like every construct added after 1.0 it is **opt-in per document**: it needs the
`#!bovnar 1.2` directive, and without one `ucum:mm[Hg]` is simply not a unit.
Native units are unaffected in every version, and every document a 1.1 reader
accepted and this one still accepts means exactly what it did. Two corrections
refuse documents that 1.1 let through; both are under *Compatibility* below.

For C consumers: rebuild against the new headers. `libbvnr.so` moves to
SOVERSION 2, so a binary built against 1.1 fails to load rather than misreading
a grown struct. For Python consumers: see *Breaking changes*; a requirement
pinned below 2 keeps the 1.1 API.

## Install

```bash
pip install --upgrade bovnar     # pure-ctypes bindings + bundled shared library
```

Prebuilt archives for Linux and Windows (MinGW64 and MSVC), the single-file
amalgamation and the WebAssembly bundle are attached to the GitHub release.

## What's new

- **Unit profiles (spec 1.2)** — seven namespaces: `ucum`, `unece`, `qudt`,
  `qudt-qk`, `udunits`, `om` and `cf`. A code either becomes a real unit or is
  an error; there is no state in which a value carries a unit the parser cannot
  reason about. `ucum:kg`, `unece:KGM`, `qudt:KiloGM`, `udunits:kg` and
  `om:kilogram` are one and the same unit, and a cross-vocabulary suite checks
  every spelling against every other. Units with no SI definition — UCUM's
  `[IU]`, a UN/ECE package count — are carried as opaque units: equal to
  themselves, convertible to nothing. `cf:` is read-only. Each vocabulary has
  its own build switch (`-DBVNR_WITH_<NAME>_PROFILE=OFF`).
- **A unit keeps its spelling** — `bvnr_data_t.unit_source` hands the reader's
  consumer the unit exactly as it was written, beside what it means, and the
  writer writes it back (`bvnr_writer_set_unit_source`, or the same field on
  `bvnr_write_event`). A spelling that disagrees with its unit is refused, not
  written.
- **Read-time unit policy and lossless conversion** —
  `bvnr_reader_set_unit_policy` and the `want_unit` hook convert values as they
  are read, in exact rational arithmetic: a conversion that cannot be exact is
  refused (`error_unit_inexact`), never rounded. A policy can also *assert* —
  require a unit on every number, require a dimension, or pin one field by path —
  and the writer enforces the validation half on what it emits. On the CLI:
  `--unit`, `--field`, `--si`, `--require-unit`, `--require-dimension`,
  `--require-field`.
- **A temperature difference is a unit** — `ΔK`, `Δ°C` and four siblings, in
  their own quantity kind, so a difference no longer converts as a scale reading.
- **A larger native registry** — 264 physical units, up from 163, with their
  conversion factors checked against their own definitions.
- **Compact prefix form** — `km` beside `k~m`. The canonical form is unchanged
  (`k~m`); a document that is re-serialised keeps the spelling it was written in.
- **`text_only`** — a reader flag (and `--text-only`) that refuses a document
  containing an octet stream, for channels that are not byte-transparent.
- **Python** — `Unit` objects that compare by meaning, `UnitPolicy`,
  `available_profiles()`, `unit_to_profile()`, and a typed
  `loads()` → `dumps()` round trip that is byte-identical for a profile-spelled
  document.
- **Tooling** — every mapped profile row is checked against its publisher's own
  machine-readable definitions (UN/ECE through QUDT's cross-reference), on a
  weekly schedule; the
  conformance suite is at **470 cases**; the web, vim, VS Code and Sublime
  highlighters know the profile notation.

## Breaking changes

- **ABI.** `bvnr_data_t` and `bvnr_read_flags_t` grew, `value_unit_t` holds up to
  32 components, and every `value_base_unit_t` id was renumbered into
  per-vocabulary blocks. No document changed — a unit is written by its
  spelling, never by its id. Code that names units by enumerator needs only a
  rebuild.
- **Python.** `Quantity.unit` is a `Unit` object (use `str(q.unit)`, or
  `q.unit.raw` for the struct), the value accessors are uniformly properties
  (`q.decimal`, not `q.decimal()`), and `Quantity()` validates its arguments.

## New error codes (appended, non-breaking)

`error_unit_inexact` (47), `error_octet_stream_truncated` (48),
`error_unit_profile_unknown` (49), `error_unit_profile_unsupported` (50),
`error_octet_stream_forbidden` (51), `error_type_param_whitespace` (52).
Existing codes are unchanged.

## Compatibility

- **Two corrections refuse documents that 1.1 accepted.** Neither changes what
  any accepted document means, and both apply whatever version a document
  declares.
- **Whitespace inside a type-annotation parameter is now an error**
  (`error_type_param_whitespace`). `<float:64,k g>` used to be read as `k~g`
  with the space silently deleted; such a document carried a unit its author did
  not write. Whitespace beside a separator — `<float : 64 , m>` — is as legal as
  it always was.
- **A bare array is homogeneous in its unit**, not merely in its dimension: an
  array whose elements are annotated with different units of one dimension —
  metres beside feet — is refused by the document tier. A consumer that read
  such an array as one block, which the specification said it could, was
  computing wrong numbers.
- **1.2 constructs are opt-in** — they require the `#!bovnar 1.2` directive.

## Open questions this release does not close

The profile tables restate other publishers' vocabularies. QUDT and OM (CC BY
4.0) and UDUNITS-2 (BSD 3-clause) are attributed as their licences require.
**UCUM's licence conditions its grant on not creating derivative tables, and the
CF standard-name table carries no licence statement**; permission and a
declaration have been requested for those two and neither has arrived. UN/ECE
Recommendation 20 exists to be implemented and is universally implemented, but
the UN's website terms do not say so; written confirmation has been asked for
there too. Both
tables ship enabled, and `-DBVNR_WITH_UCUM_PROFILE=OFF` /
`-DBVNR_WITH_CF_PROFILE=OFF` remove them from a build entirely. The record is in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and doc/11 §18.

## Documentation

Full docs — tutorial, specification, unit/currency reference, unit profiles, C
and Python API, formal EBNF grammar, FAQ, conformance protocol, and cheat sheet —
are available at **https://www.bovnar.io** (each also downloadable as **PDF**),
and in the [`doc/`](https://github.com/sothis/bovnar/tree/main/doc) directory.
See [`CHANGELOG.md`](CHANGELOG.md) for the complete change list.

## License

MIT for the source, CC BY 4.0 for the documentation, CC0 1.0 for the examples.
The identifier strings in the unit-profile tables belong to their publishers and
are not covered by the MIT grant; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
