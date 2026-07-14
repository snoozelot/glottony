#!/usr/bin/env bash
# mint.t — tests for MINT macro processor
#
# WHAT IT DOES
#   Tests every MINT builtin by feeding expressions through mint.c
#   via ccraft and checking stdout against expected output.
#
# USAGE
#   ./mint.t                run all tests
#   ./mint.t -v             verbose (show each assertion)
#   ./mint.t /arith         run only tests matching "arith"
#
# DEPENDENCIES
#   ccraft — builds and runs mint.c

set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
MINT="$SCRIPT_DIR/mint.c"
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

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

assert_not_contains() {
    local name=$1
    local haystack=$2
    local needle=$3

    if [[ "$haystack" != *"$needle"* ]]; then
        record_pass "$name"
    else
        record_fail "$name"
        printf '  unexpected: %s\n' "$needle" >&2
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

assert_empty() {
    local name=$1
    local value=$2

    if [[ -z "$value" ]]; then
        record_pass "$name"
    else
        record_fail "$name"
        printf '  expected empty, got: %s\n' "$value" >&2
    fi
}

assert() {
    local name=$1
    shift
    local negate=false

    if [[ "$1" == "!" ]]; then
        negate=true
        shift
    fi

    local result=0
    "$@" 2>/dev/null || result=$?

    if $negate; then
        result=$((1 - result))
    fi

    if [[ $result -eq 0 ]]; then
        record_pass "$name"
    else
        record_fail "$name"
    fi
}

# ---------------------------------------------------------------------------
# Test helpers — invoke MINT, check output
# ---------------------------------------------------------------------------

mint() { ccraft "$MINT" "$@"; }

mint_ok() {
    local name=$1
    local input=$2
    local expected=$3

    run mint <<< "$input"
    assert_exit "$name/exit" 0
    assert_eq "$name/output" "$expected" "$RUN_OUT"
}

# ============================================================================
# Tests — arithmetic
# ============================================================================

test_arith_add() {
    mint_ok "add" '#(++,3,4)' 7
}

test_arith_sub() {
    mint_ok "sub" '#(--,10,3)' 7
}

test_arith_mul() {
    mint_ok "mul" '#(**,3,4)' 12
}

test_arith_div() {
    mint_ok "div" '#(//,10,3)' 3
}

test_arith_mod() {
    mint_ok "mod" '#(%%,10,3)' 1
}

test_arith_prefix() {
    mint_ok "prefix" '#(++,a10,5)' a15
}

test_arith_zero() {
    mint_ok "zero" '#(//

,5,0)' 0
}

test_arith_neg() {
    mint_ok "neg" '#(++,x-5,3)' x-2
}

# ============================================================================
# Tests — bitwise
# ============================================================================

test_bit_or() {
    mint_ok "or" '#(||,6,3)' 7
}

test_bit_and() {
    mint_ok "and" '#(&&,6,3)' 2
}

test_bit_xor() {
    mint_ok "xor" '#(^^,6,3)' 5
}

# ============================================================================
# Tests — comparison
# ============================================================================

test_cmp_gt_true() {
    mint_ok "gt_true" '#(g?,5,3,yes,no)' yes
}

test_cmp_gt_false() {
    mint_ok "gt_false" '#(g?,3,5,yes,no)' no
}

test_cmp_eq_true() {
    mint_ok "eq_true" '#(==,abc,abc,yes,no)' yes
}

test_cmp_eq_false() {
    mint_ok "eq_false" '#(==,abc,xyz,yes,no)' no
}

test_cmp_lt_true() {
    mint_ok "lt_true" '#(a?,abc,xyz,yes,no)' yes
}

test_cmp_lt_false() {
    mint_ok "lt_false" '#(a?,xyz,abc,yes,no)' no
}

# ============================================================================
# Tests — string operations
# ============================================================================

test_str_nc() {
    mint_ok "nc" '#(nc,hello)' 5
}

test_str_nc_empty() {
    mint_ok "nc_empty" '#(nc,)' 0
}

test_str_ds_gs() {
    mint_ok "ds_gs" '#(ds,g,Hello )#(gs,g,)' "Hello "
}

test_str_si() {
    mint_ok "si" '#(ds,x,hello)#(si,x,2)' e
}

test_str_si_oob() {
    mint_ok "si_oob" '#(ds,x,hi)#(si,x,9)' ""
}

test_str_mp_gs() {
    run mint <<< '#(ds,g,X $ Y)#(mp,g,$)#(gs,g,foo)'
    assert_exit "mp_gs/exit" 0
    assert_eq "mp_gs/output" "X foo Y" "$RUN_OUT"
}

test_str_es() {
    run mint <<< '#(ds,a,one)#(ds,b,two)#(es,a)#(gs,a,)'
    assert_exit "es/exit" 0
    assert_eq "es/output" "" "$RUN_OUT"
}

# ============================================================================
# Tests — literal and protection
# ============================================================================

test_protect() {
    mint_ok "protect" '(1+1)' '1+1'
}

test_literal_hash() {
    mint_ok "lone_hash" '#x' '#x'
}

# ============================================================================
# Tests — sort and base conversion
# ============================================================================

test_sa() {
    mint_ok "sa" '##(sa,c,b,a)' 'a,b,c'
}

test_sa_single() {
    mint_ok "sa_single" '##(sa,foo)' foo
}

test_bc_ascii() {
    mint_ok "bc_ascii" '#(bc,A,A,D)' 65
}

test_bc_hex() {
    mint_ok "bc_hex" '#(bc,255,D,H)' FF
}

# ============================================================================
# Tests — name query, list strings
# ============================================================================

test_nq_yes() {
    mint_ok "nq_yes" '#(ds,x,val)#(n?,x,y,n)' y
}

test_nq_no() {
    mint_ok "nq_no" '#(n?,nonexist,y,n)' n
}

test_ls() {
    mint_ok "ls" '##(ls,,a)' ''
}

test_ls_multi() {
    mint_ok "ls_multi" '#(ds,a,1)##(ds,ab,2)##(ds,b,3)##(ls,,a)' aab
}

# ============================================================================
# Tests — halt
# ============================================================================

test_hl() {
    run mint <<< '#(hl,0)'
    assert_exit "hl" 0
}

# ============================================================================
# Tests — file I/O
# ============================================================================

test_rf_missing() {
    run mint <<< '#(rf,/nonexistent/foo)'
    assert_contains "rf_missing" "$RUN_OUT" "File not found"
}

test_rf_wf_roundtrip() {
    local content="Hello world line2"

    run mint <<< "#(wf,$WORK_DIR/test.txt,$content)"
    assert_exit "wf/exit" 0

    run mint <<< "#(rf,$WORK_DIR/test.txt)"
    assert_eq "rf/content" "$content" "$RUN_OUT"
}

test_rn() {
    touch "$WORK_DIR/old.txt"

    run mint <<< "#(rn,$WORK_DIR/old.txt,$WORK_DIR/new.txt)"
    assert_exit "rn/exit" 0
    assert "rn_exists" test -f "$WORK_DIR/new.txt"
    assert "rn_gone" ! test -f "$WORK_DIR/old.txt"
}

test_de() {
    touch "$WORK_DIR/tmp.txt"

    run mint <<< "#(de,$WORK_DIR/tmp.txt)"
    assert_exit "de/exit" 0
    assert "de_gone" ! test -f "$WORK_DIR/tmp.txt"
}

# ============================================================================
# Tests — library save/load
# ============================================================================

test_sl_ll_roundtrip() {
    local lib="$WORK_DIR/lib.mnt"

    run mint <<< "#(ds,greet,Hello)#(ds,target,World)#(sl,$lib,greet,target)"
    assert_exit "sl/exit" 0
    assert "sl_file" test -f "$lib"

    run mint <<< "#(ll,$lib)##(gs,greet,) ##(gs,target,)"
    assert_eq "ll/output" "Hello World" "$RUN_OUT"
}

# ============================================================================
# Tests — time, execute, environment
# ============================================================================

test_ct_now() {
    run mint <<< '#(ct,)'
    assert_exit "ct_now/exit" 0
    assert_contains "ct_now/has_year" "$RUN_OUT" "202"
}

test_ex_echo() {
    run mint <<< '#(ex,echo,hello)'
    assert_exit "ex_echo/exit" 0
    assert_eq "ex_echo/output" "hello" "$RUN_OUT"
}

test_ev() {
    run mint <<< '#(ev)#(n?,env.HOME,y,n)'
    assert_exit "ev/exit" 0
    assert_eq "ev/found" "y" "$RUN_OUT"
}

# ============================================================================
# Tests — editor buffer
# ============================================================================

test_ba() {
    run mint <<< '#(ba,100)#(sp,5)#(pp)'
    assert_exit "ba_sp/exit" 0
    assert_eq "ba_sp/point" 5 "$RUN_OUT"
}

test_is() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,Hello)#(pp)'
    assert_exit "is/exit" 0
    assert_eq "is/point" 5 "$RUN_OUT"
}

test_sm() {
    run mint <<< '#(ba,100)#(sp,10)#(sm,0,5)#(pp)'
    assert_exit "sm/exit" 0
    assert_eq "sm/point" 10 "$RUN_OUT"
}

test_pp() {
    run mint <<< '#(ba,100)#(sp,42)#(pp)'
    assert_exit "pp/exit" 0
    assert_eq "pp/pos" 42 "$RUN_OUT"
}

test_dm() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,Hello)#(sm,0,5)#(sp,0)#(dm,,)'
    assert_exit "dm/exit" 0
    assert_eq "dm/text" "Hello" "$RUN_OUT"
}

test_mb() {
    run mint <<< '#(ba,100)#(sp,7)#(mb,,,)'
    assert_exit "mb/exit" 0
}

test_tr() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,startFINISHend)#(sp,0)#(sm,0,5)#(sp,5)#(tr,,REPLACED)'
    assert_exit "tr/exit" 0
}

# ============================================================================
# Tests — search
# ============================================================================

test_lp_forward() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,abcXdefXghi)#(sp,0)#(lp,X,,,0)'
    assert_exit "lp_fwd/exit" 0
    assert_eq "lp_fwd/pos" 3 "$RUN_OUT"
}

test_lp_backward() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,abcXdefXghi)#(sp,8)#(lp,X,,,1)'
    assert_exit "lp_bwd/exit" 0
    assert_eq "lp_bwd/pos" 7 "$RUN_OUT"
}

test_lk() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,Hello World)#(sp,0)#(lk,World,,,,)'
    assert_exit "lk/exit" 0
    assert_eq "lk/pos" 6 "$RUN_OUT"
}

test_lq_found() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,abc)#(sp,0)#(l?,b,,,,yes,no)'
    assert_exit "lq_found/exit" 0
    assert_eq "lq_found" "yes" "$RUN_OUT"
}

test_lq_notfound() {
    run mint <<< '#(ba,100)#(sp,0)#(is,,abc)#(l?,z,,,,yes,no)'
    assert_exit "lq_nfound/exit" 0
    assert_eq "lq_nfound" "no" "$RUN_OUT"
}

# ============================================================================
# Tests — misc (file find, debug, variable I/O, timed input)
# ============================================================================

test_ff() {
    run mint <<< '##(ff,/etc/host*,|)'
    assert_exit "ff/exit" 0
    assert_contains "ff/match" "$RUN_OUT" "host"
}

test_db() {
    run mint <<< '#(db)'
    assert_exit "db/exit" 0
}

test_lv_sv_roundtrip() {
    local vfile="$WORK_DIR/var.txt"

    run mint <<< "#(sv,$vfile,hello variable)"
    assert_exit "sv/exit" 0

    run mint <<< "#(lv,$vfile)#(n?,$vfile,y,n)"
    assert_eq "lv/found" "y" "$RUN_OUT"
}

test_it() {
    run mint <<< '#(it,0)'
    assert_exit "it/exit" 0
    assert_eq "it/timeout" "Timeout" "$RUN_OUT"
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
