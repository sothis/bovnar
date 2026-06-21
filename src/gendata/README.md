# src/gendata — catalogue data tables

Units, prefixes, and currencies are defined once, as **data**, in the `*.bvnr`
files here; the C tables are generated from them. So the enum, conversion table,
symbol map, parse/alias tables, prefix scale/policy tables, and currency
catalogue cannot drift out of sync. Covers 163 physical units, 34 SI/IEC
prefixes, and 216 currencies.

Each `.bvnr` file's comment header documents its fields and the editing rules
(stable, append-only ids). **To add or change an entry, edit a record there and
rebuild — never edit the generated `*.gen.{h,inc}`.**

## Layout

| File | Location | Role |
|------|----------|------|
| `units.bvnr` | `src/gendata/` | the data: all 163 physical units |
| `prefixes.bvnr` | `src/gendata/` | the data: 24 SI + 10 IEC prefixes |
| `currencies.bvnr` | `src/gendata/` | the data: all 216 currencies |
| `gen_units.py` / `gen_prefixes.py` / `gen_currencies.py` | repo root | the generators |
| `bvnr_data.py` | repo root | the small built-in `.bvnr` reader they use |

## Building

The generators parse the `.bvnr` files with `bvnr_data.py` (a small built-in
reader) and need only **Python 3** — no bovnar library. CMake runs them at
configure time, regenerating when the snippets are missing or always with
`-DBVNR_REGEN_TABLES=ON`, so a clean checkout builds with just Python 3 + a C
compiler. To regenerate by hand, from the repo root:

```
python3 gen_units.py && python3 gen_prefixes.py && python3 gen_currencies.py
```

Generated outputs:

- **`*.gen.h` enum headers → `include/`** — committed (public API / install set).
- **`*.gen.inc` snippets → `<build>/generated/`** — build artifacts, **not
  committed**. CMake adds that directory to the private include path;
  `amalgamate.py` inlines the snippets via `BVNR_GENERATED_DIR`.

## Wiring map

Each hand-written span was replaced with an `#include` of a generated fragment.

| Generated file | Included from |
|----------------|---------------|
| `include/bovnar_units.gen.h` | `value_base_unit_e` enum in `bovnar.h` |
| `include/bovnar_si_prefix.gen.h` / `bovnar_iec_prefix.gen.h` | the two prefix enums in `bovnar.h` |
| `<build>/generated/bovnar_si_conv_table.gen.inc` | `si_conv_table` in `bovnar_si_units.c` |
| `<build>/generated/bovnar_base_unit_str.gen.inc` | `base_unit_str()` in `bovnar_utils.c` |
| `<build>/generated/bovnar_bu_table.gen.inc` | `bu_table` in `bovnar_utils.c` |
| `<build>/generated/bovnar_bu_index.gen.inc` | `bu_first_for_len`/`bu_max_len` in `bovnar_utils.c` |
| `<build>/generated/bovnar_prefix_policy.gen.inc` | `bu_prefix_policy` in `bovnar_si_units.c` |
| `<build>/generated/bovnar_si_pfx_table.gen.inc` / `bovnar_iec_pfx_table.gen.inc` | scale tables in `bvn_unit_impl.h` |
| `<build>/generated/bovnar_si_prefix_str.gen.inc` / `bovnar_iec_prefix_str.gen.inc` | symbol maps in `bovnar_utils.c` |
| `<build>/generated/bovnar_si_table.gen.inc` / `bovnar_iec_table.gen.inc` | prefix parse tables in `bovnar_utils.c` |
| `<build>/generated/bovnar_currency_table.gen.inc` | `g_currency_table` in `bovnar_currency.c` |

The generated files carry **data only**. The conversion/parse/lookup functions,
the range predicates, `bvn_currency_index()`, and the rule body of
`bvn_prefix_unit_valid` stay hand-written (logic, not data).
