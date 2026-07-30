#!/usr/bin/env bash
#
# Bundle the whole project into build/merged/:
#
#   bovnar.zip                  the tree as it is, comments intact
#   bovnar-nocomments.zip       the same tree with the comments stripped out of
#                               every language in it: C, Bovnar, Python, CMake,
#                               HTML, XML, JavaScript, CSS and Markdown
#   bvnr_src.txt … bvnr_py_src_exmpl_doc_web.txt
#                               cumulative concatenated text dumps
#                               (C → +python → +examples → +doc → +web)
#
# Both zips are always written. The text dumps follow --strip: without it they
# carry comments and keep their historical names, with it they are built from
# the comment-free sources and are written as bvnr_*-nocomments.txt, so a
# stripped dump can never be mistaken for a full one.
#
# usage: ./merge.sh [-s|--strip] [--no-strip] [-h|--help]
#
# Comment stripping
# -----------------
# Two tools, because no one scanner knows nine languages:
#
#   strip_comments.sh       C sources and headers (ISO C99 translation phase 3,
#                           so a /* inside a string literal is left alone)
#   strip_lang_comments.py  everything else, each language scanned with
#                           something that knows its literals. That is not
#                           pedantry: `#` appears inside CMake quoted arguments
#                           in this repository's own CTest registry, `<!--`
#                           inside a <script> on the landing page, `//` inside
#                           strings and regexes in the vendored highlighter, and
#                           a Bovnar octet stream carries arbitrary bytes --
#                           `#`, LF, NUL -- in length-prefixed chunks.
#
# Everything happens in a throwaway staging copy, so the tree is never modified
# and the clean-tree guard strip.sh needs does not apply here.
#
# The leading SPDX/copyright block of every file survives: it IS a comment, so
# a stripper removes it, and the MIT licence requires the notice to be retained.
# strip.sh applies that rule to C by capturing the block and putting it back;
# the same dance is done below for C, while strip_lang_comments.py does it
# internally for the rest -- along with a vendored /*! @license */ banner, a
# `#!` shebang, a PEP 263 coding cookie, IE conditional comments, the
# `<!-- bovnar:retired-path -->` markers three doc checkers read, and the first
# comment of a Bovnar document, which is the only place `#!bovnar 1.1` can be.
#
# Nothing here is taken on trust. strip_lang_comments.py refuses to write a file
# whose syntax tree, token stream, canonical XML or parse events changed, and a
# build of the stripped tree is the check for C -- see the "Verifying" section at
# the bottom of this file.
#
# NOT stripped: JSON and TOML (pyproject.toml keeps its comments), plain text,
# *.min.js (no comments beyond the licence banner, and minified regex-versus-
# division is a real risk for no gain), and JavaScript or CSS comments that sit
# inside an HTML <script>/<style> element rather than in a .js/.css file.
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
#
# What goes into the zips
# -----------------------
# The prune list used to name `build` exactly, so a second build tree -- the
# build-asan/, build-ubsan/, cmake-build-release/ directories a sanitizer or
# IDE run leaves behind -- was packed into the archive: megabytes of object
# files and CMake caches shipped as "source". The prune list now covers those
# by pattern, and on top of that every path git ignores is dropped, which is
# the same rule .gitignore already states (`build*/*`) and needs no upkeep the
# next time a build directory gets a new name. The count of paths the git
# filter removed is printed, so that pass is never silent.
set -euo pipefail
shopt -s failglob

STRIP=0
while (($#)); do
    case "$1" in
        -s|--strip)   STRIP=1 ;;
        --no-strip)   STRIP=0 ;;
        -h|--help)
            sed -n '/^# usage:/{s/^# //;p;q}' "$0"
            printf '  -s, --strip   build the text dumps from the comment-free\n'
            printf '                sources (both zips are written either way)\n'
            exit 0 ;;
        *)
            printf 'merge.sh: unknown option: %s (try --help)\n' "$1" >&2
            exit 2 ;;
    esac
    shift
done

OUT=./build/merged
mkdir -p "$OUT"
OUTABS="$(cd "$OUT" && pwd)"

for t in ./strip_comments.sh ./strip_lang_comments.py; do
    [[ -f "$t" ]] || {
        printf 'merge.sh: %s not found — run this from the repo root\n' "$t" >&2
        exit 1
    }
done

DUMPS=(bvnr_src bvnr_py_src bvnr_py_src_exmpl bvnr_py_src_exmpl_doc
       bvnr_py_src_exmpl_doc_web)
SUF=""
((STRIP)) && SUF="-nocomments"

# Recreate everything from scratch so a failed run can never leave a stale file
# looking like a current one.  Both dump name sets go, not just the one about to
# be written: a bvnr_src.txt left by yesterday's unstripped run says nothing
# about today's tree.  (No `zip -u` either: a fresh archive can never return
# zip's benign "nothing to do" exit code 12, which set -e would treat as a
# failure, and it can never retain stale entries from a previous run.)
rm -f "$OUT"/bovnar.zip "$OUT"/bovnar-nocomments.zip
for d in "${DUMPS[@]}"; do
    rm -f "$OUT/$d.txt" "$OUT/$d-nocomments.txt"
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---------------------------------------------------------------- file list --
# Everything that is not a VCS, editor, cache or build artefact.  Build trees
# are matched by pattern: build/, build-asan/, build_release/, cmake-build-*/.
find . \( -type d \( -name .git -o -name .github -o -name .claude \
        -o -name .idea -o -name .cache -o -name .pytest_cache \
        -o -name __pycache__ -o -name node_modules -o -name pdf \
        -o -name build -o -name 'build-*' -o -name 'build_*' \
        -o -name 'cmake-build*' \) \
        -o -name .gitignore \) -prune -o -print > "$TMP"/all

# Drop what git ignores.  This is the backstop for the prune list above: a
# build tree with a name nobody predicted is still ignored by `build*/*`, and
# generated content that lives among the sources (web/de/, doc/ietf/.refcache/,
# *.mgc) is ignored too and has no business in a source bundle.
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    set +e
    git check-ignore --stdin < "$TMP"/all > "$TMP"/ignored
    rc=$?
    set -e
    # 0 = some path is ignored, 1 = none is; anything else is a real failure.
    ((rc == 0 || rc == 1)) || {
        printf 'merge.sh: git check-ignore failed (exit %d)\n' "$rc" >&2
        exit 1
    }
    awk 'NR==FNR { skip[$0]=1; next } !($0 in skip)' "$TMP"/ignored "$TMP"/all \
        > "$TMP"/zip
else
    cp "$TMP"/all "$TMP"/zip
    : > "$TMP"/ignored
fi
IGNORED_COUNT="$(wc -l < "$TMP"/ignored)"

[[ -s "$TMP"/zip ]] || { printf 'merge.sh: nothing to archive\n' >&2; exit 1; }

# -y stores a symlink as a symlink instead of following it.  web/doc -> ../doc
# is the one link in the tree; following it would either duplicate all of doc/
# or, in the staging copy below where the target is out of tree, fail outright.
zip -q9y "$OUT"/bovnar.zip -@ < "$TMP"/zip

# ------------------------------------------------------- comment-free staging --
# Copy the tree (via tar, so the relative paths survive) and strip that copy.
# --no-recursion: the list already names every file, and letting tar recurse
# into the directory entries it also contains would pull back in exactly the
# build trees pruned above.
#
# The staged set is the UNfiltered list, a superset of what either zip holds, so
# the dumps below can always resolve a file from it.
STAGE="$TMP/stage"
mkdir -p "$STAGE"
tar --no-recursion -cf - -T "$TMP"/all | tar -xf - -C "$STAGE"

# C: capture each leading SPDX block, strip, then put the blocks back.  One
# strip_comments.sh call for the whole set: it starts a python interpreter per
# invocation, not per file.
mapfile -d '' -t CFILES < <(
    find "$STAGE" -type f \( -iname '*.c' -o -iname '*.h' \) -print0)
((${#CFILES[@]})) || { printf 'merge.sh: no C sources staged\n' >&2; exit 1; }

HDRS=()
for f in "${CFILES[@]}"; do
    hdr="$(awk '
        NR==1 && $0 !~ /^\/\*/ { exit }
        { print }
        /\*\// && NR>1 { exit }
    ' "$f")"
    [[ "$hdr" == *SPDX-License-Identifier* ]] || hdr=""
    HDRS+=("$hdr")
done

./strip_comments.sh -i "${CFILES[@]}"

for i in "${!CFILES[@]}"; do
    [[ -n "${HDRS[$i]}" ]] || continue
    printf '%s\n' "${HDRS[$i]}" | cat - "${CFILES[$i]}" > "${CFILES[$i]}.hdr$$"
    mv -- "${CFILES[$i]}.hdr$$" "${CFILES[$i]}"
done

# Everything else.  strip_lang_comments.py keeps the SPDX block itself and
# verifies each file against its own parser before writing it, so a file it
# cannot prove unchanged fails the run rather than shipping mangled.  It decides
# per file what it can handle, so passing it a *.min.js (which it declines) or a
# .cmake.in costs nothing.
mapfile -d '' -t LFILES < <(
    find "$STAGE" -type f \( -iname '*.py' -o -iname '*.cmake' \
        -o -iname '*.cmake.in' -o -iname 'CMakeLists*.txt' \
        -o -iname '*.html' -o -iname '*.htm' -o -iname '*.bvnr' \
        -o -iname '*.xml' -o -iname '*.js' -o -iname '*.mjs' \
        -o -iname '*.cjs' -o -iname '*.css' \
        -o -iname '*.md' \) -print0)
((${#LFILES[@]})) || {
    printf 'merge.sh: no non-C sources staged\n' >&2
    exit 1
}
python3 ./strip_lang_comments.py -i "${LFILES[@]}"

( cd "$STAGE" && zip -q9y "$OUTABS"/bovnar-nocomments.zip -@ ) < "$TMP"/zip

# ------------------------------------------------------------------- dumps --
# Where dump_files reads content from.  Names in the dump are always
# tree-relative, so the two variants differ in comments and in nothing else.
ROOT="."
((STRIP)) && ROOT="$STAGE"

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
    local f src
    while IFS= read -r f; do
        src="$ROOT/$f"
        if [ ! -f "$src" ]; then
            # Pruned or git-ignored, so absent from the staging copy: dump it
            # from the tree.  True only for files no stripper touches
            # (doc/pdf/*.pdf, __pycache__/*.pyc, doc/ietf/.refcache/*.xml) --
            # a strippable file reaching this point would be an unstripped file
            # in a dump that claims to have none, so it stops the run instead.
            case "$f" in
                *.c|*.h|*.C|*.H|*.py|*.cmake|*.cmake.in|*.html|*.htm \
                |*.bvnr|*.xml|*.js|*.mjs|*.cjs|*.css|*.md \
                |CMakeLists*.txt|*/CMakeLists*.txt)
                    printf 'merge.sh: %s is not in the staging copy\n' "$f" >&2
                    exit 1 ;;
            esac
            src="$f"
        fi
        printf '==> %s <==\n' "$f"
        if grep -Iq . "$src" 2>/dev/null; then
            cat -- "$src"
        else
            printf '[binary file, %s bytes — not inlined]\n' "$(wc -c < "$src")"
        fi
        printf '\n'
    done
}

expand_files CMakeLists.txt CMakeLists_tests.txt cmake \
             src include tests/*.c tests/json \
    | dump_files > "$OUT/bvnr_src$SUF.txt"

{ expand_files python/bovnar python/tests pyproject.toml | dump_files
  cat "$OUT/bvnr_src$SUF.txt"; } > "$OUT/bvnr_py_src$SUF.txt"

{ expand_files examples | dump_files
  cat "$OUT/bvnr_py_src$SUF.txt"; } > "$OUT/bvnr_py_src_exmpl$SUF.txt"

{ expand_files doc | dump_files
  cat "$OUT/bvnr_py_src_exmpl$SUF.txt"; } > "$OUT/bvnr_py_src_exmpl_doc$SUF.txt"

{ expand_files web/*.html | dump_files
  cat "$OUT/bvnr_py_src_exmpl_doc$SUF.txt"; } \
    > "$OUT/bvnr_py_src_exmpl_doc_web$SUF.txt"

printf 'merged -> %s\n' "$OUT"
printf '  zips  : bovnar.zip (comments intact)\n'
printf '          bovnar-nocomments.zip (%d C, %d other file(s) through the strippers)\n' \
       "${#CFILES[@]}" "${#LFILES[@]}"
printf '  dumps : bvnr_*%s.txt (%s)\n' "$SUF" \
       "$( ((STRIP)) && echo 'comments stripped' || echo 'comments intact')"
printf '  git-ignored paths left out of the zips: %s\n' "$IGNORED_COUNT"

# ---------------------------------------------------------------- verifying --
# strip_lang_comments.py proves each of its own eight languages -- a file whose
# syntax tree, token stream, canonical XML or parse events changed is refused
# rather than written -- but nothing above proves the C strip, and nothing proves
# the result still builds. The check for that is to build both zips and compare,
# and it is worth redoing after any change to the stripping step:
#
#   out=$PWD/build/merged
#   for z in bovnar bovnar-nocomments; do
#       d=$(mktemp -d) && ( cd "$d" && unzip -q "$out/$z.zip" \
#           && cmake -B build -DBVNR_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo . \
#           && cmake --build build -j"$(nproc)" \
#           && ctest --test-dir build -j4 )
#   done
#
# Both trees must register the same number of tests (159 + 15 example documents
# at the time of writing) and build clean, and every test must reach the same
# verdict in both -- except for the five below, which are the complete expected
# difference. Each one asserts something ABOUT comments or about a hash of a file
# whose comments were removed; none of them means the code stopped working, and
# the whole point of running both trees is that a SIXTH difference would.
#
#   bvnr_py_all, bvnr_py_pure_all  fail on one case of 2533:
#       test_units_example_derived_count_matches_its_own_header reads the comment
#       "(N named SI-derived units)" out of examples/units.bvnr and counts the
#       keys beneath it. Its subject is a comment, so a comment-free tree cannot
#       satisfy it by construction.
#   bvnr_wasm_freshness  hashes the library sources and compares against the
#       stamp wasm/build_wasm.sh recorded, so a tree whose sources differ by as
#       little as a comment is correctly reported as not the one the committed
#       module was built from. Rewriting the stamp to match would be a lie about
#       provenance; this zip is a copy to read, not one to ship a playground from.
#   bvnr_cache_stamps, bvnr_font_stamps  the ?v= cache-busting stamps in
#       web/index.html ARE content hashes of the .js and .css they label, so
#       stripping those files necessarily moves them. The page still loads -- a
#       stamp is a cache-busting hint, not a load requirement -- and re-deriving
#       the stamps here would mean reimplementing three generators
#       (build_wasm.sh, gen_font_stamps.py, gen_html_docs.py) inside a bundling
#       script. If a green run matters more than comment-free web assets, drop
#       -iname '*.js'/'*.mjs'/'*.css' from the staging find above.
#   bvnr_html_docs  fails in BOTH trees for a reason unrelated to stripping:
#       gen_html_docs.py stamps each page with its git commit date and falls back
#       to file mtimes when there is no history (see shallow_clone() there), and
#       an unzipped tree has no .git. It passes in the repository itself. In the
#       stripped tree it also lists web/docs/index.html, which carries the
#       ?v= stamp of docs.css -- the same cause as bvnr_cache_stamps.
#
# Two checks worth running when the Bovnar or Markdown scanners change, because
# no ctest gate covers exactly what they do:
#
#   * every .bvnr file through the CLI, before and after. Copy the two files to
#     paths of EQUAL length first: `bovnar events` derives its column width from
#     the path, so a longer name alone changes where the output wraps.
#         for c in events validate pretty-print "pretty-print --canonical"; do
#             build/bovnar $c a.bvnr; build/bovnar $c b.bvnr; done
#     All 32 documents in the tree, the 226 KB octet-stream example included,
#     produce byte-identical output for all four.
#   * `python3 gen_html_docs.py` in both trees, then diff web/docs/ with the
#     dates and ?v= stamps normalised. All 14 pages must come out identical,
#     which is what proves the Markdown strip changed nothing the generator sees.
