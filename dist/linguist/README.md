# GitHub Linguist submission for Bovnar

Getting GitHub to recognise and syntax-highlight `.bvnr` files means adding
Bovnar to [github/linguist](https://github.com/github-linguist/linguist). This
directory holds everything needed for that pull request.

## What Linguist needs (and where it already exists)

1. **A language entry** — add `languages.yml.fragment` (this directory) to
   `lib/linguist/languages.yml`, then run `script/update-ids` to fill in a
   unique `language_id`.

2. **A TextMate grammar** at scope `source.bovnar`. One already exists in this
   repo: `highlighter/vscode/bovnar-highlight/syntaxes/Bovnar.tmLanguage.json`
   (`scopeName: source.bovnar`). Register it by adding an entry to Linguist's
   `grammars.yml` and `vendor/grammars` (a git submodule pointing at a public
   repo that contains the grammar). The simplest path is to publish the VS Code
   extension's grammar in its own repo, or point at this one.

3. **Sample files** under `samples/Bovnar/` in the Linguist PR. Copy a few from
   this project's `examples/` (e.g. `structs.bvnr`, `arrays.bvnr`,
   `financial.bvnr`, `units.bvnr`).

## Acceptance bar (read this first)

Linguist only adds a language once it is **used in a meaningful number of unique
public repositories** (historically ~200, judged by maintainers). A brand-new
format will be declined until there is real-world usage on GitHub. Practical
sequence:

1. Land this project and downstream `.bvnr` files on GitHub; encourage adoption.
2. Meanwhile, the repo-local `.gitattributes` (`linguist-language=Bovnar`) is in
   place — it activates highlighting automatically once the language ships, and
   is a harmless no-op until then.
3. When usage is demonstrable, open the Linguist PR with items 1–3 above and a
   link to the specification (`doc/03_bovnar_spec.md`) and this project.

## Checklist for the PR

- [ ] `languages.yml` entry added; `script/update-ids` run.
- [ ] Grammar registered in `grammars.yml` + `vendor/grammars` submodule.
- [ ] `samples/Bovnar/*.bvnr` added.
- [ ] `bundle exec rake test` / `script/licensed` pass.
- [ ] PR description links the spec, the PyPI package, and usage evidence.
