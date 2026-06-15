#!/bin/bash
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
set -euo pipefail
shopt -s failglob

mkdir -p ./build/merged

# Recreate the archive from scratch (no `zip -u`): a fresh archive can never
# return zip's benign "nothing to do" exit code 12, which set -e would treat as
# a failure, and it can never retain stale entries from a previous run.
rm -f build/merged/bovnar.zip
find . \( -type d \( -name .git -o -name build -o -name cmake-build-debug -o -name __pycache__ -o -name .cache -o -name .pytest_cache -o -name .github -o -name .claude -o -name .idea -o -name pdf \) -o -name .gitignore \) -prune -o -print | zip -q9 build/merged/bovnar.zip -@

tail -n +1 CMake* src/dom/* src/io/* src/lexer/* src/stream/* src/utils/* src/validator/* src/writer/* src/*.c include/* tests/*.c tests/json/* > build/merged/bvnr_src.txt
tail -n +1 python/bovnar/*.py python/tests/*.py pyproject.toml build/merged/bvnr_src.txt > build/merged/bvnr_py_src.txt
tail -n +1 examples/* build/merged/bvnr_py_src.txt > build/merged/bvnr_py_src_exmpl.txt
tail -n +1 doc/* build/merged/bvnr_py_src_exmpl.txt > build/merged/bvnr_py_src_exmpl_doc.txt
tail -n +1 web/*.html build/merged/bvnr_py_src_exmpl_doc.txt > build/merged/bvnr_py_src_exmpl_doc_web.txt
