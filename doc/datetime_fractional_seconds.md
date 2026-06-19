# Datetime fractional seconds — design note (spec 1.1)

As-built reference for the ISO-8601 fractional-second feature. The published
behaviour lives in `1_bovnar_spec.md` (§ datetime) and the FAQ; this note is for
maintainers: the design decision, the data flow, the invariants, and the
non-obvious gotchas that bit during development.

## The decision

ISO 8601 puts no bound on the number of fractional-second digits, and a
`datetime` value's carrier is a **whole-second** signed integer (epoch seconds,
validated exactly like `sint`). So a literal such as `2026-06-15T12:00:00.5Z`
cannot store its fraction in the carrier.

Rather than truncate-and-discard (the original behaviour) or widen the carrier,
the fraction is **preserved verbatim as an out-of-band string** and is
*informational only*:

- The carrier is unchanged — the value floors to the written second and takes
  part in all arithmetic/comparison/array-homogeneity as a plain integer.
- The verbatim digits (no leading `.`, no trailing-zero trimming, any length)
  ride alongside the carrier so a consumer can *see* sub-second precision and so
  the value *round-trips*.
- Round-trip happens by **re-emitting the ISO literal**, not by storing a
  fraction on the carrier. A datetime that carries a fraction pretty-prints as
  `<datetime:64> 2026-06-15T12:00:00.5Z`; one written as a bare integer carrier
  still pretty-prints as the integer. Both forms are idempotent.

For sub-second values you actually compute on, use a finer integer carrier
(milliseconds/microseconds since the epoch). The fraction here is metadata.

## Data flow

```
ISO literal text
  └─ lexer (dtlit_* states) accumulates the whole literal into str_data
       └─ validator bvn_iso_parse_fields(): parses fields, captures the
          fraction span [frac, frac_len) into raw->str_data; converts the
          civil UTC instant to the whole-second carrier on the value's epoch
            └─ value event carries:  data = "<carrier int>"        (decimal)
                                     frac_data/frac_length = "5"   (verbatim)
                 ├─ streaming consumer reads bvnr_data_t.frac_data
                 ├─ DOM builder strdup()s it into node->dt_frac
                 │     └─ bvn_dom_get_datetime_fraction()
                 └─ writer ev_data: bvn_ser_datetime_to_civil() reconstructs the
                    civil time from the carrier and re-emits  ...:SS.<frac>Z
```

The reader→writer path (CLI `pretty-print`, the canon observer) is what makes
round-trip work; it re-serialises the **event stream**, so `frac_data` flows
straight through. The DOM is a separate consumer surface (it stores the
fraction for programmatic access); it has no re-serialiser of its own.

## Invariants

1. **`frac_data` is NULL / `frac_length` 0 for every value except a
   `vt_datetime` written as an ISO literal with a `.frac` part.** Every
   `bvnr_data_t` is zero-initialised so non-value / non-datetime events never
   carry stale frac.
2. **The carrier is the single source of truth for the value.** The fraction is
   excluded from equality, comparison, and array homogeneity (a same-carrier /
   different-fraction pair is "equal" and may share an array). Each element
   keeps its own fraction on output.
3. **The fraction never crosses a second boundary.** Tz-offset folding and the
   leap-second lookup operate on whole seconds *before* the fraction is
   attached, so they are unaffected by it. The writer always emits `Z` (the
   offset has already been folded to UTC at read time).
4. **Lifetime:** streaming `frac_data` points into the lexer's scratch buffer —
   valid only for the duration of the callback, exactly like `data`. The DOM's
   `dt_frac` is node-owned and NUL-terminated.

## Touch points

| Layer | File | What |
|---|---|---|
| Public API | `include/bovnar.h` | `bvnr_data_t.frac_data` / `frac_length` |
| Lexer | `src/lexer/bovnar_state_table.c` | `dtlit_frac` state (already scanned the digits) |
| Validator | `src/validator/bovnar_validator.c` | `bvn_iso_parse_fields` captures the span; value event sets frac |
| DOM | `src/dom/bvn_dom_impl.h`, `bovnar_dom_builder.c`, `bovnar_dom.c` | `dt_frac` field, strdup on build, free on destroy, `bvn_dom_get_datetime_fraction` |
| DOM API | `include/bovnar_dom.h` | `bvn_dom_get_datetime_fraction()` |
| Writer | `src/writer/bovnar_writer.c` | `bvn_ser_datetime_to_civil` + the ev_data number case |
| Python FFI | `python/bovnar/structs.py` | `BvnrData` ctypes mirror (frac fields), `frac_str()` |
| Python | `python/bovnar/reader.py`, `dom.py`, `_ffi.py` | snapshot frac in the callback; `DomNode.datetime_fraction` |
| JS playground | `web/bovnar_parser.js` | `frac` field on the datetime DATA event (both `parseBovnar` and `parseFaithful`) |

## Gotchas (the hard-won ones)

- **The ctypes mirror must match the C struct.** Appending `frac_data` /
  `frac_length` to `bvnr_data_t` left the Python `BvnrData` 16 bytes short; the
  Python writer passes `&BvnrData` to `bvnr_write_event`, and the C serializer
  read `frac_data` past the end of the Python allocation — a heap OOB read that
  SEGV'd when writing a datetime (it only "worked" while that memory happened to
  be zero). Fixed by syncing the mirror; **guarded permanently** by the ABI
  sweep (`tests/bvnr_abi_dump.c` is the C source of truth, `python/tests/test_abi.py`
  asserts every ctypes Structure matches it). Any future struct change that
  desyncs a binding now fails the build instead of corrupting memory at runtime.
- **The non-typed Python value drops the fraction.** Plain `loads()` returns an
  `int` holding only the whole-second carrier, so a non-typed `loads`→`dumps`
  drops the fraction. With `typed=True` the `Quantity` preserves the verbatim
  fraction and re-emits it, so `loads(typed=True)`→`dumps` round-trips it
  losslessly; the DOM accessor and the streaming reader expose it explicitly.
  (Documented in `4_bovnar_python_bindings.md`.)
- **The atomic GNSS epochs (`gps`/`galileo`/`glonass`/`beidou`) never carry a
  fraction** — they reject ISO literals at read time (no round-trippable
  civil⇄seconds inverse). `bvn_ser_datetime_to_civil` also refuses to
  reconstruct them, falling back to the integer carrier.
- **Writer reconstruction must be defensive.** `bvn_dt_tai_seconds_to_utc`
  leaves `*dt` untouched on the tai underflow near `INT64_MIN`, and
  `bvn_gregorian_date_from_mjd` leaves the date untouched for an out-of-range
  MJD. `bvn_ser_datetime_to_civil` therefore zero-inits `dt` and validates the
  full civil result (month 1–12, day 1–31, year 0000–9999, which an ISO literal
  requires) before emitting — otherwise it emits the plain integer carrier. It
  also rejects a non-digit `frac_data` (a public-API caller could supply one).

## Test map

- Streaming consumer sees frac: `tests/bovnar_extended_reader_test.c`
- DOM stores/exposes frac: `tests/bovnar_dom_test.c`
- Writer round-trip + the reconstruction guards (garbage frac, GNSS epoch,
  INT64_MIN tai): `tests/bovnar_writer_test.c`
- Streaming across read boundaries: `tests/bovnar_socketpair_roundtrip_test.c`
- Carrier still floors (value unchanged): `tests/bvnr_conformance.c` (DTLIT-114/115),
  `python/tests/test_datetime_oracle.py`
- Python visibility + the datetime-write path: `python/tests/test_datetime.py`
- ABI mirror guard: `python/tests/test_abi.py` (+ `tests/bvnr_abi_dump.c`)
- Idempotent pretty-print of a fractional literal: `examples/datetime.bvnr`
  via the CLI idempotency / DOM-canonical tests
