#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="./build"
TESTS_DIR="${BUILD_DIR}/tests"
FUZZ_ITER=5000
RUN_FUZZ=1
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; TESTS_DIR="${BUILD_DIR}/tests"; shift 2 ;;
        --fuzz-iter) FUZZ_ITER="$2"; shift 2 ;;
        --no-fuzz)   RUN_FUZZ=0;     shift   ;;
        -j)          JOBS="$2";       shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

PASS=0
FAIL=0
SKIP=0
FAILED_TESTS=()

_green()  { printf '\033[0;32m%s\033[0m\n' "$*"; }
_red()    { printf '\033[0;31m%s\033[0m\n' "$*"; }
_yellow() { printf '\033[0;33m%s\033[0m\n' "$*"; }
_bold()   { printf '\033[1m%s\033[0m\n'    "$*"; }

run_test() {

    local label="$1"; shift
    local exe="$1";   shift

    if [[ ! -x "$exe" ]]; then
        _yellow "  SKIP  $label  (not built: $exe)"
        (( SKIP++ )) || true
        return
    fi

    printf '  %-52s ' "$label"
    if "$exe" "$@"; then
        _green "PASS"
        (( PASS++ )) || true
    else
        _red "FAIL"
        (( FAIL++ )) || true
        FAILED_TESTS+=("$label")
    fi
}

_bold "=== Building ==="
cmake --build "$BUILD_DIR" --parallel "$JOBS"
echo

_bold "=== Unit tests ==="

run_test "bvnr_dom_test"                  "$TESTS_DIR/bvnr_dom_test"
run_test "bvnr_reader_test"               "$TESTS_DIR/bvnr_reader_test"
run_test "bvnr_extended_reader_test"      "$TESTS_DIR/bvnr_extended_reader_test"
run_test "bvnr_writer_test"               "$TESTS_DIR/bvnr_writer_test"
run_test "bvnr_si_test"                   "$TESTS_DIR/bvnr_si_test"
run_test "bvnr_unit_ext_test"             "$TESTS_DIR/bvnr_unit_ext_test"
run_test "bvnr_utils_test"                "$TESTS_DIR/bvnr_utils_test"
run_test "bvnr_socketpair_roundtrip_test" "$TESTS_DIR/bvnr_socketpair_roundtrip_test"
run_test "bvnr_high_severity_test"        "$TESTS_DIR/bvnr_high_severity_test"
run_test "bvnr_int_test"                  "$TESTS_DIR/bvnr_int_test"
run_test "bvnr_float_test"                "$TESTS_DIR/bvnr_float_test"
run_test "bvnr_float_fix_dec_test"        "$TESTS_DIR/bvnr_float_fix_dec_test"

echo

if [[ $RUN_FUZZ -eq 1 ]]; then
    _bold "=== Standalone fuzz tests (--fuzz-iter $FUZZ_ITER) ==="

    run_test "bvnr_fuzz_test --harness reader" \
        "$TESTS_DIR/bvnr_fuzz_test" \
        --harness reader --iterations "$FUZZ_ITER" --seed 42

    run_test "bvnr_fuzz_test --harness dom" \
        "$TESTS_DIR/bvnr_fuzz_test" \
        --harness dom --iterations "$FUZZ_ITER" --seed 42

    run_test "bvnr_fuzz_test --harness utils" \
        "$TESTS_DIR/bvnr_fuzz_test" \
        --harness utils --iterations "$FUZZ_ITER" --seed 42

    SEED_FILE=$(mktemp /tmp/bvnr_fuzz_writer_seed_XXXXXX.bin)
    trap 'rm -f "$SEED_FILE"' EXIT
    printf 'X' > "$SEED_FILE"

    run_test "bvnr_fuzz_writer_test (smoke)" \
        "$TESTS_DIR/bvnr_fuzz_writer_test" "$SEED_FILE"

    echo
else
    _yellow "=== Standalone fuzz tests skipped (--no-fuzz) ==="
    echo
fi

_bold "=== CLI example smoke tests ==="

BOVNAR_BIN="${BUILD_DIR}/bovnar"
EXAMPLES_DIR="./examples"

run_cli_test() {
    local label="$1"; shift

    if [[ ! -x "${BOVNAR_BIN}" ]]; then
        _yellow "  SKIP  $label  (not built: ${BOVNAR_BIN})"
        (( SKIP++ )) || true
        return
    fi

    printf '  %-52s ' "$label"
    if "$@" > /dev/null 2>&1; then
        _green "PASS"
        (( PASS++ )) || true
    else
        _red "FAIL"
        (( FAIL++ )) || true
        FAILED_TESTS+=("$label")
    fi
}

for bvnr_file in "${EXAMPLES_DIR}"/*.bvnr; do
    stem=$(basename "${bvnr_file}" .bvnr)
    run_cli_test "bovnar events   ${stem}.bvnr" \
        "${BOVNAR_BIN}" events "${bvnr_file}"
    run_cli_test "bovnar validate ${stem}.bvnr" \
        "${BOVNAR_BIN}" validate "${bvnr_file}"
done

echo

_bold "=== Results ==="
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo "  Skipped: $SKIP"

if [[ $FAIL -gt 0 ]]; then
    echo
    _red "Failed tests:"
    for t in "${FAILED_TESTS[@]}"; do
        _red "  • $t"
    done
    exit 1
fi

echo
_green "All tests passed."
