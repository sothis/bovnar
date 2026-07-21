#!/usr/bin/env bash
#
# Bundle the whole project into build/merged/: a zip of the tree plus a series
# of cumulative concatenated text dumps (C → +python → +examples → +doc → +web).
#
# Hardening:
#   set -e          abort on the first failing command (e.g. an unreadable file)
#   set -u          treat an unset variable as an error
#   set -o pipefail a failure anywhere in a pipeline fails the whole pipeline
#   failglob        a glob that matches nothing is a hard error rather than being
#                   passed through literally — so a moved/renamed source set
#                   stops the build loudly instead of silently dropping a section.
#
# The dumps used to be built with `tail -n +1 <glob>`, which had four problems:
#
#   * A directory in a glob is fatal. Adding doc/wiki/ broke the script outright
#     — `tail: error reading 'doc/wiki': Is a directory`, exit 1, and the final
#     bvnr_py_src_exmpl_doc_web.txt was never produced at all. clean.sh ends by
#     calling this script, so a clean run ended in failure too.
#   * Binary content landed in files advertised as text: examples/octet_streams
#     .bvnr embeds an octet stream, which put 932 NUL bytes into the dumps.
#   * `tail -n +1` prints the "==> name <==" header only when given MORE THAN ONE
#     file, so a source set that happened to match exactly one file lost its
#     filename marker.
#   * A set that failed left yesterday's output file in place, so a partial
#     bundle looked complete.
#
# Files are therefore expanded to regular files explicitly, binary ones are
# recorded by name and size instead of being inlined, every file gets a header,
# and the outputs are removed up front.
set -euo pipefail
shopt -s failglob

OUT=./build/merged
mkdir -p "$OUT"

# Recreate everything from scratch so a failed run can never leave a stale file
# looking like a current one.  (No `zip -u` either: a fresh archive can never
# return zip's benign "nothing to do" exit code 12, which set -e would treat as
# a failure, and it can never retain stale entries from a previous run.)
rm -f "$OUT"/bovnar.zip "$OUT"/bvnr_src.txt "$OUT"/bvnr_py_src.txt \
      "$OUT"/bvnr_py_src_exmpl.txt "$OUT"/bvnr_py_src_exmpl_doc.txt \
      "$OUT"/bvnr_py_src_exmpl_doc_web.txt

find . \( -type d \( -name .git -o -name build -o -name cmake-build-debug \
        -o -name __pycache__ -o -name .cache -o -name .pytest_cache \
        -o -name .github -o -name .claude -o -name .idea -o -name pdf \) \
        -o -name .gitignore \) -prune -o -print \
    | zip -q9 "$OUT"/bovnar.zip -@

# Expand the arguments to regular files, recursing into any directory, sorted so
# the dump is reproducible.
expand_files() {
    local p
    for p in "$@"; do
        if [ -d "$p" ]; then
            find "$p" -type f -print
        elif [ -f "$p" ]; then
            printf '%s\n' "$p"
        fi
    done | LC_ALL=C sort -u
}

# Concatenate with a "==> path <==" header per file, exactly as tail -n +1 does
# for two or more files. Binary files are listed rather than inlined: this is a
# TEXT dump, and inlining NULs corrupts it for every consumer downstream.
dump_files() {
    local f
    while IFS= read -r f; do
        printf '==> %s <==\n' "$f"
        if grep -Iq . "$f" 2>/dev/null; then
            cat -- "$f"
        else
            printf '[binary file, %s bytes — not inlined]\n' "$(wc -c < "$f")"
        fi
        printf '\n'
    done
}

expand_files CMakeLists.txt CMakeLists_tests.txt cmake \
             src include tests/*.c tests/json \
    | dump_files > "$OUT"/bvnr_src.txt

{ expand_files python/bovnar python/tests pyproject.toml | dump_files
  cat "$OUT"/bvnr_src.txt; } > "$OUT"/bvnr_py_src.txt

{ expand_files examples | dump_files
  cat "$OUT"/bvnr_py_src.txt; } > "$OUT"/bvnr_py_src_exmpl.txt

{ expand_files doc | dump_files
  cat "$OUT"/bvnr_py_src_exmpl.txt; } > "$OUT"/bvnr_py_src_exmpl_doc.txt

{ expand_files web/*.html | dump_files
  cat "$OUT"/bvnr_py_src_exmpl_doc.txt; } > "$OUT"/bvnr_py_src_exmpl_doc_web.txt

printf 'merged -> %s (%s)\n' "$OUT" \
       "$(ls -1 "$OUT" | tr '\n' ' ')"
