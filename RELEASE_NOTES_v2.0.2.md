# Bovnar 2.0.2 — a crate that names its revision

**A patch release of the 2.0 line: same format, same ABI, same APIs, same
library code.** It implements spec 1.2, as 2.0.0 and 2.0.1 do, and the C
sources are 2.0.1's apart from the version number. What changes is what the
Rust crate says about itself.

## Install

```bash
pip install --upgrade bovnar     # pure-ctypes bindings + bundled shared library
```

```toml
[dependencies]
bovnar = "2.0.2"                 # Rust; compiles the C library in (needs a C compiler)
```

Prebuilt archives for Linux, macOS (universal, arm64 and x86_64) and Windows
(MinGW and MSVC), the single-file amalgamation and the source tarball are
attached to the GitHub release, with a `SHA256SUMS` file.

## Added — provenance in the crate

The crate bundles the whole C library under `c/`, and programs that vendor it
from there, to compile it and to generate their license notices from it, need
to say which upstream revision they ship. Up to 2.0.1 the crate named none: it
is staged outside git, so cargo records no VCS information.

From 2.0.2 the crate carries **`PROVENANCE.json`**:

- the repository, the full commit and the release tag it was staged from;
- for every file in the crate (`c/include`, `c/src`, `c/generated`, `LICENSE`,
  `THIRD_PARTY_NOTICES.md`, the Rust sources) its SHA-256 and the repository
  path it was copied from, `"generated"` for the generated tables.

The release checks it before anything is published: `rust/package.py` stages
the named commit a second time and requires the same files byte for byte
(`check_crate_provenance.py`), CI checks the packed `.crate` against the
release tag, and a publish refuses uncommitted changes and an untagged commit.
A consumer can verify its vendored copy against the hashes, and anyone can
check a published crate against the repository:

```bash
python3 check_crate_provenance.py bovnar-2.0.2.crate --tag v2.0.2
```

The full list is in [CHANGELOG.md](CHANGELOG.md) under 2.0.2.
