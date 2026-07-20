#!/usr/bin/env bash
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

if ! command -v emcc >/dev/null 2>&1; then
	echo "error: emcc not found. Run:  source ~/emsdk/emsdk_env.sh" >&2
	exit 1
fi

echo ">> regenerating amalgamation"
python3 "$ROOT/amalgamate.py" "$AMALG"

# Symbols the JS glue calls. The leading underscore is the C name mangling
# emscripten applies; _malloc/_free let the JS side pass byte buffers in.
EXPORTS='_bvnr_wasm_validate,_bvnr_wasm_errors,_bvnr_wasm_to_json,_bvnr_wasm_events,_bvnr_wasm_doc,_bvnr_wasm_version,_bvnr_wasm_free,_malloc,_free'
RUNTIME='ccall,cwrap,UTF8ToString,lengthBytesUTF8,stringToUTF8,HEAPU8'

COMMON=(
	-O3
	# The library type-puns through opaque source/sink blobs; strict aliasing
	# (TBAA) miscompiles those accesses when the whole amalgamation is one TU,
	# as it is here. The native CMake build disables it for the same reason
	# (CMakeLists.txt). Without this, -O3 produces an infinite loop in the DOM
	# builder on type-annotated values.
	-fno-strict-aliasing
	-I "$AMALG" -I "$ROOT/wasm"
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
# Single-file wrapper: same wrapper, importing the inlined glue.
sed "s#'./bovnar.mjs'#'./bovnar.single.mjs'#" \
	"$ROOT/wasm/index.mjs" > "$OUT/index.single.mjs"

# Web playground integration: the inlined single-file build + a wrapper that
# imports it, dropped into web/ so the playground loads the real parser offline
# (no separate .wasm fetch, works over file:// and the static site).
echo ">> installing web/ playground module"
cp "$OUT/bovnar.single.mjs" "$ROOT/web/bovnar_wasm_core.mjs"

# Cache-busting: stamp content hashes so a rebuilt/redeployed asset can never be
# served from a stale browser/CDN cache (the ?v query is the only thing these
# ES-module / script URLs are keyed on). Two independent hashes: CORE_HASH covers
# the compiled core + the wrapper that imports it (the <script>/import hops that
# change only when the wasm rebuilds), and SHIM_HASH (below) covers the
# hand-maintained FFI shim, which is edited independently of the core. Each hash
# changes only when its file's bytes change, so unchanged rebuilds stay byte-stable
# (no spurious diffs) and there's no version number to bump by hand. All seds are
# idempotent (they rewrite any existing ?v=).
CORE_HASH="$(sha256sum "$ROOT/web/bovnar_wasm_core.mjs" | cut -c1-12)"
sed "s#'./bovnar.mjs'#'./bovnar_wasm_core.mjs?v=${CORE_HASH}'#" \
	"$ROOT/wasm/index.mjs" > "$ROOT/web/bovnar_wasm.mjs"
sed -i -E "s#(import\('\./bovnar_wasm\.mjs)(\?v=[0-9a-f]+)?('\))#\1?v=${CORE_HASH}\3#" \
	"$ROOT/web/bovnar_parser_wasm.js"
# The shim is hand-maintained FFI logic, versioned independently of the compiled
# core — so key ITS cache-bust on its own content hash (computed after the import
# rewrite above, so it also rolls when the core it loads changes). Keying it on
# CORE_HASH would leave the ?v= unchanged after a shim-only edit and serve a stale
# shim from browser/CDN cache.
SHIM_HASH="$(sha256sum "$ROOT/web/bovnar_parser_wasm.js" | cut -c1-12)"
sed -i -E "s#(bovnar_parser_wasm\.js\?v=)[0-9a-zA-Z]+#\1${SHIM_HASH}#" \
	"$ROOT/web/index.html"
# ...and the two <link rel="modulepreload"> hrefs, which must match the dynamic-import
# URLs above or the browser would fetch the modules twice.
sed -i -E "s#(bovnar_wasm\.mjs\?v=)[0-9a-f]+#\1${CORE_HASH}#g" "$ROOT/web/index.html"
sed -i -E "s#(bovnar_wasm_core\.mjs\?v=)[0-9a-f]+#\1${CORE_HASH}#g" "$ROOT/web/index.html"
echo ">> stamped web/ module chain: core v=${CORE_HASH}, shim v=${SHIM_HASH}"

echo ">> done:"
ls -la "$OUT"
