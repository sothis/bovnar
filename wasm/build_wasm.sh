#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# Build the Bovnar WebAssembly module from the single-file amalgamation.
#
# Produces, under dist/wasm/:
#   bovnar.mjs + bovnar.wasm   ESM module (separate .wasm) — npm / bundlers / node
#   bovnar.single.mjs          ESM module with the wasm inlined as base64
#                              (no second fetch; convenient for file:// and the
#                              web playground)
#
# Requires the emscripten SDK on PATH (source ~/emsdk/emsdk_env.sh first), and
# python3 for the amalgamation step.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

AMALG="$ROOT/build/amalgamate"
OUT="$ROOT/dist/wasm"
mkdir -p "$OUT"

# In-place ERE substitution that means the same thing on GNU and BSD sed.
# Plain "sed -i -E" is not portable: GNU sed takes -E as an option, while
# FreeBSD's sed takes it as the -i backup SUFFIX and then evaluates the
# expression as a BRE -- the groups/alternations silently match nothing, and a
# stray "<file>-E" is left behind. A temp file plus cat preserves the target's
# mode on both and leaves no backup.
sed_in_place() {
	local expr="$1" file="$2" tmp
	tmp="$(mktemp "${file}.XXXXXX")" || return 1
	if ! sed -E "$expr" "$file" > "$tmp"; then
		rm -f "$tmp"
		return 1
	fi
	cat "$tmp" > "$file"
	rm -f "$tmp"
}

if ! command -v emcc >/dev/null 2>&1; then
	echo "error: emcc not found. Run:  source ~/emsdk/emsdk_env.sh" >&2
	exit 1
fi

echo ">> regenerating amalgamation"
python3 "$ROOT/amalgamate.py" "$AMALG"

# Symbols the JS glue calls. The leading underscore is the C name mangling
# emscripten applies; _malloc/_free let the JS side pass byte buffers in.
# The _ex twins carry the reader options + unit policy; the small helpers mirror
# the stateless reader-side C API (versions, error names, unit introspection).
EXPORTS='_bvnr_wasm_validate,_bvnr_wasm_validate_ex,_bvnr_wasm_errors,_bvnr_wasm_errors_ex,_bvnr_wasm_to_json,_bvnr_wasm_to_json_ex,_bvnr_wasm_events,_bvnr_wasm_events_ex,_bvnr_wasm_events_convert,_bvnr_wasm_events_convert_ex,_bvnr_wasm_parse,_bvnr_wasm_parse_ex,_bvnr_wasm_doc,_bvnr_wasm_doc_ex,_bvnr_wasm_version,_bvnr_wasm_version_number,_bvnr_wasm_spec_version,_bvnr_wasm_peek_version,_bvnr_wasm_error_name,_bvnr_wasm_unit,_bvnr_wasm_units_equal,_bvnr_wasm_units_convertible,_bvnr_wasm_convert_unit,_bvnr_wasm_unit_to_profile,_bvnr_wasm_unit_profiles,_bvnr_wasm_parse_type_annotation,_bvnr_wasm_policy_create,_bvnr_wasm_policy_destroy,_bvnr_wasm_policy_require_unit,_bvnr_wasm_policy_target,_bvnr_wasm_policy_require_dimension,_bvnr_wasm_policy_rule,_bvnr_wasm_policy_normalise,_bvnr_wasm_free,_malloc,_free'
RUNTIME='ccall,cwrap,UTF8ToString,lengthBytesUTF8,stringToUTF8,HEAPU8'

COMMON=(
	-O3
	# The library type-puns through opaque source/sink blobs; strict aliasing
	# (TBAA) miscompiles those accesses when the whole amalgamation is one TU,
	# as it is here. The native CMake build disables it for the same reason
	# (CMakeLists.txt). Without this, -O3 produces an infinite loop in the DOM
	# builder on type-annotated values.
	-fno-strict-aliasing
	# The library has one live assert(), and __FILE__ put the absolute path of
	# whoever built the bundle into the shipped .wasm -- which also made the
	# committed artifact differ byte-for-byte when rebuilt anywhere else.
	"-ffile-prefix-map=$ROOT=."
	# include/ is on the path for bvnr_wasm.c's own `#include
	# "bovnar_si_units.h"` (the exact-rational formatter and the base check).
	# The amalgamated bovnar.h is found first, so its merged copy of the same
	# header satisfies the include guard and nothing is defined twice.
	-I "$AMALG" -I "$ROOT/wasm" -I "$ROOT/include"
	-std=c11
	"$AMALG/bovnar.c" "$ROOT/wasm/bvnr_wasm.c"
	-sEXPORTED_FUNCTIONS="$EXPORTS"
	-sEXPORTED_RUNTIME_METHODS="$RUNTIME"
	# Emscripten's default stack is only 64 KiB. The lexer (bvn_lex_run) uses
	# deep frames — especially on the type-annotation path — and overflows that,
	# manifesting as a stack-overflow abort (or an infinite loop at -O3). The
	# native build runs with a multi-MB OS stack and never hits this. Reserve a
	# generous stack to match.
	-sSTACK_SIZE=8MB
	-sALLOW_MEMORY_GROWTH=1
	-sMODULARIZE=1
	-sEXPORT_ES6=1
	-sEXPORT_NAME=createBovnar
	-sENVIRONMENT=web,worker,node
	-sFILESYSTEM=0
	--no-entry
)

echo ">> building dist/wasm/bovnar.mjs (+ bovnar.wasm)"
emcc "${COMMON[@]}" -o "$OUT/bovnar.mjs"

echo ">> building dist/wasm/bovnar.single.mjs (inlined wasm)"
emcc "${COMMON[@]}" -sSINGLE_FILE=1 -o "$OUT/bovnar.single.mjs"

echo ">> assembling package (wrapper + metadata)"
cp "$ROOT/wasm/index.mjs"    "$OUT/index.mjs"
cp "$ROOT/wasm/package.json" "$OUT/package.json"
cp "$ROOT/wasm/README.md"    "$OUT/README.md"
# The .wasm embeds the unit-profile tables, and with them identifier strings
# that belong to their publishers. The notices have to travel with a binary
# (cmake/pack_artifacts.cmake says the same of the archives), so they travel
# with this one too.
cp "$ROOT/LICENSE"                  "$OUT/LICENSE"
cp "$ROOT/THIRD_PARTY_NOTICES.md"   "$OUT/THIRD_PARTY_NOTICES.md"
# Single-file wrapper: same wrapper, importing the inlined glue.
sed "s#'./bovnar.mjs'#'./bovnar.single.mjs'#" \
	"$ROOT/wasm/index.mjs" > "$OUT/index.single.mjs"

# Web playground integration: the inlined single-file build + a wrapper that
# imports it, dropped into web/ so the playground loads the real parser offline
# (no separate .wasm fetch, works over file:// and the static site).
#
# NOTE: the web copies use a .js extension, not .mjs. They are still ES modules
# (loaded via import()/modulepreload) — but the live site is served by nginx,
# whose default mime.types has no .mjs mapping, so it serves .mjs as
# application/octet-stream and the browser refuses to execute the module. .js is
# mapped to application/javascript everywhere, so this works with no server-side
# MIME config. (The npm package under dist/wasm keeps .mjs — that's for node /
# bundlers, where the extension is correct and MIME is irrelevant.)
# Record what this artifact was built FROM, so a later build can tell whether the
# committed module still matches the sources. The ?v= hashes below are
# cache-busting -- they key on the artifact's own bytes and cannot detect that it
# ought to be rebuilt. cmake/wasm_freshness.cmake recomputes this and fails the
# test suite when they diverge.
{
	# build_wasm.sh itself is an input: its emcc flags and EXPORTS shape the
	# module as much as the sources do.
	sha256sum "$AMALG/bovnar.c" "$AMALG/bovnar.h" "$ROOT/wasm/bvnr_wasm.c" \
		"$ROOT/wasm/build_wasm.sh" \
		| cut -d' ' -f1 | tr -d '\n'
} | sha256sum | cut -d' ' -f1 > "$ROOT/wasm/built_from.sha256"
echo ">> recorded source stamp $(cat "$ROOT/wasm/built_from.sha256" | cut -c1-12)"

echo ">> installing web/ playground module"
cp "$OUT/bovnar.single.mjs" "$ROOT/web/bovnar_wasm_core.js"

# Cache-busting: stamp content hashes so a rebuilt/redeployed asset can never be
# served from a stale browser/CDN cache (the ?v query is the only thing these
# ES-module / script URLs are keyed on). One hash per file in the chain, because
# each link is edited independently of the others: CORE_HASH for the compiled
# core, WRAP_HASH for the generated wrapper (whose source wasm/index.mjs is
# hand-maintained), and SHIM_HASH for the hand-maintained FFI shim. Each hash
# changes only when its file's bytes change, so unchanged rebuilds stay byte-stable
# (no spurious diffs) and there's no version number to bump by hand. All seds are
# idempotent (they rewrite any existing ?v=).
CORE_HASH="$(sha256sum "$ROOT/web/bovnar_wasm_core.js" | cut -c1-12)"
sed "s#'./bovnar.mjs'#'./bovnar_wasm_core.js?v=${CORE_HASH}'#" \
	"$ROOT/wasm/index.mjs" > "$ROOT/web/bovnar_wasm.js"
# The wrapper is generated from wasm/index.mjs, which is hand-maintained and
# edited independently of the compiled core (the OOM handling and the error
# accounting both landed there with no core rebuild). Keying its ?v= on
# CORE_HASH left the URL unchanged after such an edit, so a returning visitor
# kept the old wrapper from cache against a correct core -- the same trap the
# shim and the highlighter were already pulled out of below. Key it on its own
# bytes, computed after the core import above is substituted in, so it also
# rolls whenever the core it loads changes.
WRAP_HASH="$(sha256sum "$ROOT/web/bovnar_wasm.js" | cut -c1-12)"
sed_in_place "s#(import\('\./bovnar_wasm\.js)(\?v=[0-9a-f]+)?('\))#\1?v=${WRAP_HASH}\3#" \
	"$ROOT/web/bovnar_parser_wasm.js"
# The shim is hand-maintained FFI logic, versioned independently of the compiled
# core — so key ITS cache-bust on its own content hash (computed after the import
# rewrite above, so it also rolls when the core it loads changes). Keying it on
# CORE_HASH would leave the ?v= unchanged after a shim-only edit and serve a stale
# shim from browser/CDN cache.
SHIM_HASH="$(sha256sum "$ROOT/web/bovnar_parser_wasm.js" | cut -c1-12)"
sed_in_place "s#(bovnar_parser_wasm\.js\?v=)[0-9a-zA-Z]+#\1${SHIM_HASH}#" \
	"$ROOT/web/index.html"
# ...and the two <link rel="modulepreload"> hrefs, which must match the dynamic-import
# URLs above or the browser would fetch the modules twice. The wrapper preload
# therefore carries WRAP_HASH (what bovnar_parser_wasm.js imports) and the core
# preload CORE_HASH (what the wrapper itself imports).
sed_in_place "s#(bovnar_wasm\.js\?v=)[0-9a-f]+#\1${WRAP_HASH}#g" "$ROOT/web/index.html"
sed_in_place "s#(bovnar_wasm_core\.js\?v=)[0-9a-f]+#\1${CORE_HASH}#g" "$ROOT/web/index.html"
# bovnar_highlight.js carried a HAND-WRITTEN date stamp (?v=20260720) that nobody
# remembered to bump, so every edit to the highlighter shipped stale to returning
# visitors from cache. Key it on its own content, like the other two.
HL_HASH="$(sha256sum "$ROOT/web/bovnar_highlight.js" | cut -c1-12)"
sed_in_place "s#(bovnar_highlight\.js\?v=)[0-9a-zA-Z]+#\1${HL_HASH}#" "$ROOT/web/index.html"
# fonts/fonts.css was the last hand-written stamp on the page -- a date, ?v=20260724,
# carried over from the same batch as the highlighter's. It survived on luck: the
# one commit that changed the file (pruning 30 webfont faces to 14) happened to
# bump the date too. Nothing enforced that, and the site now serves a ?v= URL as
# "max-age=31536000, immutable", so a single forgotten bump would pin the old
# stylesheet in returning visitors' caches for a year instead of revalidating.
# Key it on its own bytes like the other four; cache_stamps.cmake covers it now.
FONTS_HASH="$(sha256sum "$ROOT/web/fonts/fonts.css" | cut -c1-12)"
sed_in_place "s#(fonts/fonts\.css\?v=)[0-9a-zA-Z]+#\1${FONTS_HASH}#" "$ROOT/web/index.html"
echo ">> stamped web/ module chain: core v=${CORE_HASH}, wrapper v=${WRAP_HASH}, shim v=${SHIM_HASH}, highlighter v=${HL_HASH}, fonts v=${FONTS_HASH}"

echo ">> done:"
ls -la "$OUT"
