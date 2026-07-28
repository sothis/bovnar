# Distribution & integration assets

Everything needed to integrate the Bovnar media type and file extension with
desktop environments, OS tooling, and developer platforms. The media type
`text/vnd.bovnar` is registered with IANA:
<https://www.iana.org/assignments/media-types/text/vnd.bovnar>.

| Identity       | Value                          |
|----------------|--------------------------------|
| Media type     | `text/vnd.bovnar`              |
| File extension | `.bvnr`                        |
| Encoding       | UTF-8 (8-bit text)             |
| TextMate scope | `source.bovnar`                |

## Contents

### `mime/` — desktop / OS integration
- **`text-vnd.bovnar.xml`** — freedesktop [shared-mime-info] database entry.
  Detection is by the `*.bvnr` glob (the reliable mechanism for a text format).
  Installed by `make install` into `<datadir>/mime/packages/` (toggle with
  `-DBVNR_INSTALL_MIME=OFF`); then run `update-mime-database <datadir>/mime`.
- **`bovnar.magic`** — `file(1)` content magic (best-effort fallback for
  extension-less files; the extension is authoritative). Compile/test with
  `file -C -m bovnar.magic`. Verified to content-match the `examples/*.bvnr`
  documents that lead with a comment or blank line, and to reject ordinary
  source code; a file that opens directly with an assignment is recognised by
  extension only (see the heuristic notes in the file).

### `linguist/` — GitHub syntax highlighting
- **`languages.yml.fragment`** + **`README.md`** — the github/linguist
  submission. The required TextMate grammar already exists at
  `highlighter/vscode/bovnar-highlight/syntaxes/Bovnar.tmLanguage.json`
  (`source.bovnar`). Note Linguist's real-world-usage acceptance bar. The
  repo-root `.gitattributes` already carries the `linguist-language=Bovnar`
  override, which activates once the language ships upstream.

### `wasm/` — the `bovnar-wasm` npm package
- **`package.json`**, **`index.mjs`**, **`index.single.mjs`**, **`bovnar.mjs`**,
  **`bovnar.single.mjs`**, **`bovnar.wasm`**, **`README.md`** — the C reference
  parser built for WebAssembly, laid out as a publishable npm package exposing
  `validate`, `toJSON`, `events` and `version`. The `/single` entry point inlines
  the `.wasm` as base64 for `file://`, strict-CSP, and no-bundler use.
- These are **build products**, not hand-edited sources: `wasm/build_wasm.sh`
  (emscripten) writes the whole directory, and `wasm/index.mjs` is the source the
  wrapper is copied from. `wasm/built_from.sha256` records a hash of the sources
  they were built from (the amalgamated `bovnar.c`/`bovnar.h` plus
  `wasm/bvnr_wasm.c`), and the `bvnr_wasm_freshness` CTest gate fails when the
  library has moved on and the artifacts have not been rebuilt.
- The package is **not published to npm yet** — it is versioned and staged here.
  Consume it from a checkout, or use the copy the site serves under `web/`.

## Quick verification

```sh
# desktop magic
file -C -m dist/mime/bovnar.magic
file --mime-type -m dist/mime/bovnar.magic examples/financial.bvnr   # -> text/vnd.bovnar

# shared-mime-info validity (if shared-mime-info tools are installed)
xmllint --noout dist/mime/text-vnd.bovnar.xml
```

[shared-mime-info]: https://specifications.freedesktop.org/shared-mime-info-spec/latest/
