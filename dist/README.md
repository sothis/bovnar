# Distribution & registration assets

Everything needed to register the Bovnar media type and file extension with the
relevant authorities and systems.

| Identity       | Value                          |
|----------------|--------------------------------|
| Media type     | `text/vnd.bovnar`              |
| Deprecated alias | `text/x-bovnar` (unofficial) |
| File extension | `.bvnr`                        |
| Encoding       | UTF-8 (8-bit text)             |
| TextMate scope | `source.bovnar`                |

## Contents

### `mime/` — desktop / OS integration
- **`text-vnd.bovnar.xml`** — freedesktop [shared-mime-info] database entry.
  Detection is by the `*.bvnr` glob (the reliable mechanism for a text format),
  with `text/x-bovnar` kept as an alias. Installed by `make install` into
  `<datadir>/mime/packages/` (toggle with `-DBVNR_INSTALL_MIME=OFF`); then run
  `update-mime-database <datadir>/mime`.
- **`bovnar.magic`** — `file(1)` content magic (best-effort fallback for
  extension-less files; the extension is authoritative). Compile/test with
  `file -C -m bovnar.magic`. Verified to match the `examples/*.bvnr` documents
  and to reject ordinary source code.

### `nginx/` — web server
- **`bovnar-mime.conf`** — maps `.bvnr` → `text/vnd.bovnar; charset=utf-8` for
  the nginx deploy. Include it from `http{}`, or use the per-location variant in
  the file's comments.

### `linguist/` — GitHub syntax highlighting
- **`languages.yml.fragment`** + **`README.md`** — the github/linguist
  submission. The required TextMate grammar already exists at
  `highlighter/vscode/bovnar-highlight/syntaxes/Bovnar.tmLanguage.json`
  (`source.bovnar`). Note Linguist's real-world-usage acceptance bar. The
  repo-root `.gitattributes` already carries the `linguist-language=Bovnar`
  override, which activates once the language ships upstream.

### IANA (formal media-type registration)
- The application is **`doc/9_iana_media_type.md`** — a complete RFC 6838
  vendor-tree template. Submit via <https://www.iana.org/form/media-types>.
  Confirm the contact e-mail before submitting; approval is an external
  ~2-week Expert Review.

## Quick verification

```sh
# desktop magic
file -C -m dist/mime/bovnar.magic
file --mime-type -m dist/mime/bovnar.magic examples/financial.bvnr   # -> text/vnd.bovnar

# shared-mime-info validity (if shared-mime-info tools are installed)
xmllint --noout dist/mime/text-vnd.bovnar.xml
```

[shared-mime-info]: https://specifications.freedesktop.org/shared-mime-info-spec/latest/
