#!/usr/bin/env bash
# strac.t — tests for TRAC dialect interpreter
#
# WHAT IT DOES
#   Feeds TRAC expressions through strac.c via ccraft, checks output.
#   Covers: ps, ds/cl, dd/da, ss (gap markers), arithmetic,
#   equality, character reading, literal quoting, nested calls.
#
# USAGE
#   ./strac.t                run all tests
#   ./strac.t -v             verbose (show each assertion)
#   ./strac.t /arith         run only tests matching "arith"
#
# DEPENDENCIES
#   ccraft — builds and runs strac.c

set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
STRAC="$SCRIPT_DIR/strac.c"

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
WIDTH=95

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

assert_contains() {
    local name=$1
    local haystack=$2
    local needle=$3

    if [[ "$haystack" == *"$needle"* ]]; then
        record_pass "$name"
    else
        record_fail "$name"
        printf '  missing: %s\n' "$needle" >&2
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
# Test helper — invoke strac, check output
# ---------------------------------------------------------------------------

strac_ok() {
    local name=$1
    local input=$2
    local expected=$3

    run ccraft "$STRAC" <<< "$input"
    assert_exit "$name/exit" 0
    assert_eq "$name/output" "$expected" "$RUN_OUT"
}

# ============================================================================
# Tests — print string
# ============================================================================

test_ps_simple() {
    strac_ok "ps_simple" '[ps,hello]' "hello"
}

test_ps_multiple() {
    strac_ok "ps_multiple" '[ps,foo][ps,bar]' "foobar"
}

test_ps_empty() {
    strac_ok "ps_empty" '[ps,]' ""
}

test_ps_text_between_calls() {
    strac_ok "ps_with_text" 'before[ps,in]after' "beforeinafter"
}

# ============================================================================
# Tests — literal quoting
# ============================================================================

test_literal_preserves_brackets() {
    strac_ok "literal_brackets" ';[ps,literal]' "[ps,literal]"
}

test_literal_mixed() {
    strac_ok "literal_mixed" \
        ';[literal text] here [ps,out]' \
        "[literal text] here out"
}

# ============================================================================
# Tests — arithmetic
# ============================================================================

test_ad_simple() {
    strac_ok "ad_simple" '[ad,3,4]' "7"
}

test_su_simple() {
    strac_ok "su_simple" '[su,10,3]' "7"
}

test_ml_simple() {
    strac_ok "ml_simple" '[ml,3,4]' "12"
}

test_dv_simple() {
    strac_ok "dv_simple" '[dv,10,3]' "3"
}

test_ad_negative() {
    strac_ok "ad_negative" '[ad,5,-3]' "2"
}

test_dv_by_zero() {
    strac_ok "dv_by_zero" '[dv,5,0]' ""
}

# ============================================================================
# Tests — forms (define and call)
# ============================================================================

test_ds_cl_simple() {
    strac_ok "ds_cl" '[ds,g,hello][cl,g]' "hello"
}

test_ds_cl_returns_body() {
    strac_ok "ds_cl_return" \
        '[ds,name,Alice][ps,Hello ][cl,name]' \
        "Hello Alice"
}

test_ds_overwrite() {
    strac_ok "ds_overwrite" '[ds,x,one][ds,x,two][cl,x]' "two"
}

# ============================================================================
# Tests — segment (gap markers)
# ============================================================================

test_ss_single_gap() {
    strac_ok "ss_single" \
        '[ds,greet,Hello $ world][ss,greet,$][cl,greet,dear]' \
        "Hello dear world"
}

test_ss_two_gaps() {
    strac_ok "ss_two_gaps" \
        '[ds,f,$1 and $2][ss,f,$1,$2][cl,f,A,B]' \
        "A and B"
}

# ============================================================================
# Tests — delete forms
# ============================================================================

test_dd_makes_form_undefined() {
    strac_ok "dd_makes_undefined" '[ds,x,val][dd,x][cl,x]' ""
}

test_da_makes_all_undefined() {
    strac_ok "da_all" '[ds,x,val][ds,y,val][da][cl,x][cl,y]' ""
}

# ============================================================================
# Tests — equality
# ============================================================================

test_eq_equal() {
    strac_ok "eq_equal" '[eq,ab,ab,=,ne]' "="
}

test_eq_not_equal() {
    strac_ok "eq_ne" '[eq,ab,xy,=,ne]' "ne"
}

test_eq_nested() {
    strac_ok "eq_nested" '[eq,3,3,[ps,yes],[ps,no]]' "yes"
}

# ============================================================================
# Tests — character reading
# ============================================================================

test_cr_cc_single() {
    strac_ok "cr_cc" '[ds,x,abc][cr,x][cc,x]' "a"
}

test_cn_multiple_chars() {
    strac_ok "cn_multiple" '[ds,x,abcde][cr,x][cn,x,3]' "abc"
}

test_cn_beyond_end() {
    strac_ok "cn_beyond" '[ds,x,ab][cr,x][cn,x,5]' "ab"
}

# ============================================================================
# Tests — nested calls
# ============================================================================

test_nested_arithmetic() {
    strac_ok "nested_arith" '[ad,[ad,1,2],3]' "6"
}

test_nested_deep() {
    strac_ok "nested_deep" '[ml,[ad,2,3],[su,10,3]]' "35"
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
