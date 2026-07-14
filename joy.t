#!/usr/bin/env bash
# joy.t — tests for Joy concatenative language interpreter
#
# WHAT IT DOES
#   Feeds Joy expressions through joy.c via ccraft, checks stack output.
#   Covers: stack ops (push, pop, dup, swap), arithmetic, quotations,
#   combinators (i, dip, map), conditionals (ifte), booleans,
#   comparison, list operations.
#
# USAGE
#   ./joy.t                  run all tests
#   ./joy.t -v               verbose (show each assertion)
#   ./joy.t /ifte            run only tests matching "ifte"
#
# DEPENDENCIES
#   ccraft — builds and runs joy.c

set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
JOY="$SCRIPT_DIR/joy.c"

# ---------------------------------------------------------------------------
# Runner — run command, capture everything
# ---------------------------------------------------------------------------

RUN_OUT=""
RUN_ERR=""
RUN_EXIT=0

run() {
    local out
    local err

    out=$(mktemp)
    err=$(mktemp)

    RUN_EXIT=0
    "$@" > "$out" 2> "$err" || RUN_EXIT=$?

    RUN_OUT=$(< "$out")
    RUN_ERR=$(< "$err")
    rm -f "$out" "$err"
}

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

PASS=0
FAIL=0
TEST_PASS=0
TEST_FAIL=0
CURRENT_TEST=""
TEST_HAD_FAILURE=0
VERBOSE=false

GREEN=$'\033[32m'
RED=$'\033[31m'
RESET=$'\033[0m'
WIDTH=90

# ---------------------------------------------------------------------------
# Predicates
# ---------------------------------------------------------------------------

no_failures()   { [[ $TEST_HAD_FAILURE -eq 0 ]]; }
all_passed()    { [[ $FAIL -eq 0 ]]; }
should_skip()   { [[ -n "${FILTER:-}" && "$1" != *"$FILTER"* ]]; }

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

log_result() {
    local name=$1
    local status=$2
    local color=$3
    local extra=${4:-}
    local pad=$((WIDTH - ${#name}))

    [[ $pad -lt 1 ]] && pad=1
    printf '/%s%*s%s[%s]%s%s\n' \
        "$name" "$pad" "" "$color" "$status" "$RESET" "$extra"
}

log_pass() {
    if $VERBOSE; then
        log_result "$CURRENT_TEST/$1" " OK " "$GREEN"
    fi
}

record_pass() {
    PASS=$((PASS + 1))
    TEST_PASS=$((TEST_PASS + 1))
    log_pass "$1"
}

record_fail() {
    FAIL=$((FAIL + 1))
    TEST_FAIL=$((TEST_FAIL + 1))
    TEST_HAD_FAILURE=1
    log_result "$CURRENT_TEST/$1" "FAIL" "$RED" >&2
}

reset_test_state() {
    CURRENT_TEST=$1
    TEST_HAD_FAILURE=0
    TEST_PASS=0
    TEST_FAIL=0
}

# ---------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------

assert_eq() {
    local name=$1
    local expected=$2
    local actual=$3

    if [[ "$expected" == "$actual" ]]; then
        record_pass "$name"
    else
        record_fail "$name"
        printf '  expected: %s\n  actual:   %s\n' "$expected" "$actual" >&2
    fi
}

assert_exit() {
    local name=$1
    local expected=$2

    if [[ "$RUN_EXIT" -eq "$expected" ]]; then
        record_pass "$name"
    else
        record_fail "$name"
        printf '  expected exit %d, got %d\n' "$expected" "$RUN_EXIT" >&2
    fi
}

# ---------------------------------------------------------------------------
# Test helper — invoke joy, check output
# ---------------------------------------------------------------------------

joy_ok() {
    local name=$1
    local input=$2
    local expected=$3

    run ccraft "$JOY" <<< "$input"
    assert_exit "$name/exit" 0
    assert_eq "$name/output" "$expected" "$RUN_OUT"
}

# ============================================================================
# Tests — stack operations
# ============================================================================

test_number_push() {
    joy_ok "push" '5 .' "5"
}

test_number_add() {
    joy_ok "add" '3 5 + .' "8"
}

test_number_sub() {
    joy_ok "sub" '5 3 - .' "2"
}

test_number_mul() {
    joy_ok "mul" '3 4 * .' "12"
}

test_number_div() {
    joy_ok "div" '10 3 / .' "3"
}

test_expression_chain() {
    joy_ok "chain" '3 4 + 5 * .' "35"
}

test_dup_duplicates_top() {
    joy_ok "dup" '5 dup * .' "25"
}

test_pop_removes_top() {
    joy_ok "pop" '1 2 3 pop' "1 2"
}

test_swap_exchanges_top_two() {
    joy_ok "swap" '1 2 swap' "2 1"
}

test_stack_multiple_values() {
    joy_ok "stack" '1 2 3 4' "1 2 3 4"
}

# ============================================================================
# Tests — quotations and combinators
# ============================================================================

test_quotation_literal() {
    joy_ok "quotation" '[dup *]' "[dup *]"
}

test_i_executes_quotation() {
    joy_ok "i" '5 [dup *] i .' "25"
}

test_dip_runs_below_top() {
    joy_ok "dip" '1 2 [10 *] dip' "10 2"
}

# ============================================================================
# Tests — conditional (ifte)
# ============================================================================

test_ifte_true_branch() {
    joy_ok "ifte_true" \
        '5 [0 =] [pop 99] [dup *] ifte .' \
        "25"
}

test_ifte_false_branch() {
    joy_ok "ifte_false" \
        '0 [0 =] [pop 99] [dup *] ifte .' \
        "99"
}

# ============================================================================
# Tests — comparison
# ============================================================================

test_gt_true() {
    joy_ok "gt_true" '5 3 > .' "true"
}

test_gt_false() {
    joy_ok "gt_false" '3 5 > .' "false"
}

test_lt_true() {
    joy_ok "lt_true" '3 5 < .' "true"
}

test_lt_false() {
    joy_ok "lt_false" '5 3 < .' "false"
}

test_eq_true() {
    joy_ok "eq_true" '3 3 = .' "true"
}

test_eq_false() {
    joy_ok "eq_false" '3 5 = .' "false"
}

# ============================================================================
# Tests — booleans
# ============================================================================

test_true_literal() {
    joy_ok "true" 'true .' "true"
}

test_false_literal() {
    joy_ok "false" 'false .' "false"
}

test_and_true_true() {
    joy_ok "and_tt" 'true true and .' "true"
}

test_and_true_false() {
    joy_ok "and_tf" 'true false and .' "false"
}

test_or_true_false() {
    joy_ok "or_tf" 'true false or .' "true"
}

test_or_false_false() {
    joy_ok "or_ff" 'false false or .' "false"
}

test_not_true() {
    joy_ok "not_true" 'true not .' "false"
}

test_not_false() {
    joy_ok "not_false" 'false not .' "true"
}

# ============================================================================
# Tests — list operations
# ============================================================================

test_map_square() {
    joy_ok "map" '[1 2 3] [dup *] map .' "[1 4 9]"
}

test_size() {
    joy_ok "size" '[1 2 3] size .' "3"
}

# ============================================================================
# Tests — edge cases
# ============================================================================

test_empty_program_returns_nothing() {
    joy_ok "empty" '' ""
}

# ============================================================================
# Runner
# ============================================================================

setup()    { :; }
teardown() { :; }

run_test() {
    local name=$1
    local t0
    local t1
    local ms
    local total
    local status
    local color
    local extra

    reset_test_state "$name"
    setup

    t0=$(date +%s%N)
    "test_$name"
    t1=$(date +%s%N)

    teardown

    ms=$(( (t1 - t0) / 1000000 ))
    total=$((TEST_PASS + TEST_FAIL))

    if no_failures; then
        status=" OK "
        color=$GREEN
    else
        status="FAIL"
        color=$RED
    fi

    printf -v extra "  %7s %4dms" "[${TEST_PASS}/${total}]" "$ms"
    log_result "$name" "$status" "$color" "$extra"
}

main() {
    local filter=""
    local name

    while [[ $# -gt 0 ]]; do
        case $1 in
            -v|--verbose) VERBOSE=true ;;
            /*)           filter=${1#/} ;;
        esac
        shift
    done

    while read -r name; do
        if should_skip "$name"; then
            continue
        fi
        run_test "$name"
    done < <(declare -F | awk '$3 ~ /^test_/ {print substr($3, 6)}' | sort)

    echo
    printf '%s: %s%d passed%s, %s%d failed%s\n' \
        "${0##*/}" "$GREEN" "$PASS" "$RESET" "$RED" "$FAIL" "$RESET"

    all_passed
}

main "$@"
