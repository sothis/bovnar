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

# Single scratch dir for all temp artifacts (fuzz seed, idempotency / converter
# round-trip outputs); cleaned up on exit.
SCRATCH_DIR="$(mktemp -d "${TMPDIR:-/tmp}/bvnr_run_tests.XXXXXX")"
trap 'rm -rf "$SCRATCH_DIR"' EXIT

# Repo root (this script assumes it is invoked from there) and the cmake helper
# scripts that CTest also drives, so the shell runner and CTest share one
# source of truth for the converter / canonical-form assertions.
SRC_DIR="$(pwd)"
CMAKE_BIN="${CMAKE:-cmake}"

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
run_test "bvnr_stream_test"               "$TESTS_DIR/bvnr_stream_test"
run_test "bvnr_high_severity_test"        "$TESTS_DIR/bvnr_high_severity_test"
run_test "bvnr_int_test"                  "$TESTS_DIR/bvnr_int_test"
run_test "bvnr_float_test"                "$TESTS_DIR/bvnr_float_test"
run_test "bvnr_float_fix_dec_test"        "$TESTS_DIR/bvnr_float_fix_dec_test"
run_test "bvnr_currency_test"             "$TESTS_DIR/bvnr_currency_test"

echo

_bold "=== Conformance tests ==="

run_test "bvnr_conformance (self)"        "$TESTS_DIR/bvnr_conformance"
run_test "bvnr_conformance (iut self)"    "$TESTS_DIR/bvnr_conformance" \
    --iut "$TESTS_DIR/bvnr_conformance_iut"

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

    SEED_FILE="${SCRATCH_DIR}/bvnr_fuzz_writer_seed.bin"
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

# Drive one of the shared cmake -P helper scripts (the same ones CTest uses)
# and treat a zero exit as PASS.  Skipped when the bovnar binary is missing.
run_cmake_check() {
    local label="$1"; shift

    if [[ ! -x "${BOVNAR_BIN}" ]]; then
        _yellow "  SKIP  $label  (not built: ${BOVNAR_BIN})"
        (( SKIP++ )) || true
        return
    fi

    printf '  %-52s ' "$label"
    if "${CMAKE_BIN}" "$@" > /dev/null 2>&1; then
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
    run_cmake_check "pretty-print idempotent ${stem}.bvnr" \
        -DBOVNAR="${BOVNAR_BIN}" \
        -DBVNR_FILE="${bvnr_file}" \
        -DTMP_FILE="${SCRATCH_DIR}/idem_${stem}.bvnr" \
        -P "${SRC_DIR}/cmake/pretty_print_idempotent.cmake"
done

echo

_bold "=== Canonical-form preservation ==="

run_cmake_check "inline units preserved (units.bvnr)" \
    -DBOVNAR="${BOVNAR_BIN}" \
    -DBVNR_FILE="${SRC_DIR}/examples/units.bvnr" \
    "-DREQUIRE=<float:64,_10,m/s> 9.81|<float:64,_10,k~g> 70.5|<uint:64,_10,Gi~B> 4|<uint:64,_10,B> 1500|<sint:64,_10,°C> -40|<float:64,_10,Pa> 101325.0" \
    -P "${SRC_DIR}/cmake/pretty_print_contains.cmake"

run_cmake_check "inline currency preserved (financial.bvnr)" \
    -DBOVNAR="${BOVNAR_BIN}" \
    -DBVNR_FILE="${SRC_DIR}/examples/financial.bvnr" \
    "-DREQUIRE=<float_dec:64,\$USD> 29.95|<float_dec:64,\$EUR> 149.00" \
    -P "${SRC_DIR}/cmake/pretty_print_contains.cmake"

echo

_bold "=== JSON <-> bvnr converter ==="

run_cmake_check "convert round-trip (roundtrip.json)" \
    -DBOVNAR="${BOVNAR_BIN}" \
    -DJSON_FILE="${SRC_DIR}/tests/json/roundtrip.json" \
    -DTMP_FILE="${SCRATCH_DIR}/convert_roundtrip.bvnr" \
    -P "${SRC_DIR}/cmake/convert_json_roundtrip.cmake"

run_convert_fail() {
    local label="$1" file="$2" needle="$3"
    run_cmake_check "$label" \
        -DBOVNAR="${BOVNAR_BIN}" \
        -DJSON_FILE="${SRC_DIR}/tests/json/${file}" \
        "-DNEEDLE=${needle}" \
        -P "${SRC_DIR}/cmake/convert_expect_fail.cmake"
}

run_convert_fail "convert rejects bad_key.json"      bad_key.json      "not a valid bovnar identifier"
run_convert_fail "convert rejects bad_overflow.json" bad_overflow.json "out of range"
run_convert_fail "convert rejects bad_trailing.json" bad_trailing.json "trailing content"
run_convert_fail "convert rejects bad_nul.json"      bad_nul.json      "NUL"

# bvnr -> json must refuse a datetime carrying sub-second digits (the integer
# carrier cannot hold the fraction) rather than silently truncate it.
run_cmake_check "convert rejects bvnr->json datetime fraction" \
    -DBOVNAR="${BOVNAR_BIN}" \
    -DINPUT_FILE="${SRC_DIR}/tests/frac_datetime.bvnr" \
    -DFROM=bvnr -DTO=json \
    "-DNEEDLE=sub-second" \
    -P "${SRC_DIR}/cmake/convert_expect_fail.cmake"

# query must print a sub-second datetime as the faithful ISO literal rather than
# the fraction-dropping integer carrier.
run_cmake_check "query shows datetime fraction (ISO literal)" \
    -DBOVNAR="${BOVNAR_BIN}" \
    -DFILE="${SRC_DIR}/tests/frac_datetime.bvnr" \
    -DQPATH=.t \
    "-DNEEDLE=2026-06-15T12:00:00.123Z" \
    -P "${SRC_DIR}/cmake/query_expect.cmake"

echo

_bold "=== Streaming CLI (frames + mux) ==="

run_cmake_check "frames pack/list + mux pack/list round-trip" \
    -DBOVNAR="${BOVNAR_BIN}" \
    -DEX_DIR="${SRC_DIR}/examples" \
    -DTMP_DIR="${SCRATCH_DIR}" \
    -P "${SRC_DIR}/cmake/stream_cli_roundtrip.cmake"

echo

_bold "=== Python binding tests ==="

PY_BIN="${PYTHON:-python3}"
PY_SRC="${SRC_DIR}/python"
PY_TESTS="${PY_SRC}/tests"
SHARED_LIB="$(ls "${BUILD_DIR}"/libbvnr_shared.* 2>/dev/null | head -1 || true)"

if ! command -v "${PY_BIN}" > /dev/null 2>&1; then
    _yellow "  SKIP  python suite  (no ${PY_BIN})"
    (( SKIP++ )) || true
elif ! "${PY_BIN}" -m pytest --version > /dev/null 2>&1; then
    _yellow "  SKIP  python suite  (pytest not installed)"
    (( SKIP++ )) || true
else
    printf '  %-52s ' "pytest python/tests"
    # The integration tests find the shared lib via LIBBOVNAR_PATH; pure tests
    # ignore it.  Tests needing the lib self-skip when it is absent.
    if ( cd "${PY_SRC}" && LIBBOVNAR_PATH="${SHARED_LIB}" \
            "${PY_BIN}" -m pytest "${PY_TESTS}" --tb=short -q > /dev/null 2>&1 ); then
        _green "PASS"
        (( PASS++ )) || true
    else
        _red "FAIL"
        (( FAIL++ )) || true
        FAILED_TESTS+=("pytest python/tests")
    fi
fi

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
