#!/usr/bin/env bash
# cortex.t — tests for TeX macro expansion engine
#
# WHAT IT DOES
#   Feeds TeX input through cortex.c via ccraft, checks output.
#   Covers: def/gdef/edef, parameters, arithmetic, conditionals,
#   scoping, expansion control, case conversion, edge cases.
#
# USAGE
#   ./cortex.t                run all tests
#   ./cortex.t -v             verbose (show each assertion)
#   ./cortex.t /ifnum         run only tests matching "ifnum"
#
# DEPENDENCIES
#   ccraft — builds and runs cortex.c

set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
CORTEX="$SCRIPT_DIR/cortex.c"

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
WIDTH=105

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
# Test helpers — run cortex, trim whitespace, check output
# ---------------------------------------------------------------------------

trim_ws() {
    sed 's/^[[:space:]]*//; s/[[:space:]]*$//'
}

cortex_ok() {
    local name=$1
    local input=$2
    local expected=$3

    run ccraft "$CORTEX" <<< "$input"
    assert_exit "$name/exit" 0

    local trimmed
    trimmed=$(printf '%s' "$RUN_OUT" | trim_ws)
    assert_eq "$name/output" "$expected" "$trimmed"
}

cortex_nocrash() {
    local name=$1
    local input=$2

    run ccraft "$CORTEX" <<< "$input"
    assert_exit "$name/exit" 0
}

assert_stderr_has() {
    local name=$1
    local needle=$2

    assert_contains "$name" "$RUN_ERR" "$needle"
}

# ============================================================================
# Tests — definitions (def/gdef/edef)
# ============================================================================

test_def_expands_to_body() {
    cortex_ok "def_expands_to_body" \
        '\def\hello{Hello, World}\hello\end' \
        "Hello, World"
}

test_def_empty_body_expands_to_nothing() {
    cortex_ok "def_empty_body" '\def\x{}\x\end' ""
}

test_gdef_survives_group_boundary() {
    cortex_ok "gdef_survives" \
        '{\gdef\x{global}}\x\end' \
        "global"
}

test_edef_expands_replacement_at_definition_time() {
    cortex_ok "edef_expands" \
        '\count0=42 \edef\five{\the\count0}\count0=0 \five\end' \
        "42"
}

test_def_with_macro_parameter_expands_argument() {
    cortex_ok "single_param" \
        '\def\greet#1{Hello, #1!}\greet{World}\end' \
        "Hello, World!"
}

test_def_with_two_parameters_swaps_them() {
    cortex_ok "two_param_swap" \
        '\def\swap#1#2{#2#1}\swap{A}{B}\end' \
        "BA"
}

test_def_with_unbraced_argument_consumes_single_token() {
    cortex_ok "unbraced_arg" \
        '\def\double#1{#1#1}\double X\end' \
        "XX"
}

# ============================================================================
# Tests — scoping
# ============================================================================

test_local_count_assignment_reverts_after_group() {
    cortex_ok "local_count_reverts" \
        '\count0=1 {\count0=2 \the\count0 } \the\count0\end' \
        "2  1"
}

test_local_def_reverts_after_group() {
    cortex_ok "local_def_reverts" \
        '\def\x{outer}{\def\x{inner}\x}\x\end' \
        "innerouter"
}

test_global_def_persists_across_group_boundary() {
    cortex_ok "global_def_persists" \
        '{\global\def\x{survived}}\x\end' \
        "survived"
}

# ============================================================================
# Tests — arithmetic
# ============================================================================

test_count_register_assignment_stores_integer() {
    cortex_ok "count_assign" '\count0=42 \the\count0\end' "42"
}

test_advance_adds_to_count_register() {
    cortex_ok "advance_add" \
        '\count0=10 \advance\count0 by 5 \the\count0\end' \
        "15"
}

test_multiply_scales_count_register() {
    cortex_ok "multiply" \
        '\count0=6 \multiply\count0 by 7 \the\count0\end' \
        "42"
}

test_divide_divides_count_register() {
    cortex_ok "divide" \
        '\count0=42 \divide\count0 by 6 \the\count0\end' \
        "7"
}

test_numexpr_respects_multiplication_before_addition() {
    cortex_ok "numexpr_precedence" \
        '\number\numexpr 2+3*4\relax\end' \
        "14"
}

test_numexpr_parentheses_override_precedence() {
    cortex_ok "numexpr_parens" \
        '\number\numexpr (2+3)*4\relax\end' \
        "20"
}

test_numexpr_division_by_zero_returns_zero() {
    cortex_ok "numexpr_div_zero" \
        '\number\numexpr 5/0\relax\end' \
        "0"
}

# ============================================================================
# Tests — conditionals
# ============================================================================

test_ifnum_true_condition_expands_then_branch() {
    cortex_ok "ifnum_true" \
        '\ifnum 1<2 yes\else no\fi\end' \
        "yes"
}

test_ifnum_false_condition_expands_else_branch() {
    cortex_ok "ifnum_false" \
        '\ifnum 2<1 yes\else no\fi\end' \
        "no"
}

test_ifnum_without_else_still_skips_false_branch() {
    cortex_ok "ifnum_no_else" \
        '\ifnum 1<2 yes\fi\end' \
        "yes"
}

test_iffalse_skips_all_tokens_until_fi() {
    cortex_ok "iffalse_skips" \
        '\iffalse hidden\fi visible\end' \
        "visible"
}

test_ifx_self_comparison_returns_true() {
    cortex_ok "ifx_self" \
        '\def\a{A}\ifx\a\a equal\else diff\fi\end' \
        "equal"
}

test_ifx_different_macros_returns_false() {
    cortex_ok "ifx_different" \
        '\def\a{A}\def\b{B}\ifx\a\b equal\else diff\fi\end' \
        "diff"
}

test_ifdefined_returns_true_for_existing_macro() {
    cortex_ok "ifdefined_true" \
        '\def\x{}\ifdefined\x yes\else no\fi\end' \
        "yes"
}

test_ifdefined_returns_false_for_undefined() {
    cortex_ok "ifdefined_false" \
        '\ifdefined\undefined yes\else no\fi\end' \
        "no"
}

test_unless_negates_conditional_result() {
    cortex_ok "unless_negates" \
        '\unless\ifnum 1<2 no\else yes\fi\end' \
        "yes"
}

test_currentiftype_returns_14_for_iftrue() {
    cortex_ok "currentiftype" \
        '\iftrue\the\currentiftype\fi\end' \
        "14"
}

# ============================================================================
# Tests — expansion control
# ============================================================================

test_expandafter_expands_second_token_before_first() {
    cortex_ok "expandafter_order" \
        '\def\a{A}\def\b{B}\expandafter\a\b\end' \
        "BA"
}

test_csname_builds_control_sequence_from_characters() {
    cortex_ok "csname_builds" \
        '\def\foo{bar}\csname foo\endcsname\end' \
        "bar"
}

test_csname_ignores_spaces_in_name() {
    cortex_ok "csname_ignores_spaces" \
        '\def\foobar{baz}\csname foo bar\endcsname\end' \
        "baz"
}

test_ifcsname_ignores_spaces_in_name() {
    cortex_ok "ifcsname_ignores_spaces" \
        '\def\foobar{defined}\ifcsname foo bar\endcsname yes\else no\fi\end' \
        "yes"
}

test_string_primitive_emits_backslash_and_name() {
    cortex_ok "string_emits" '\string\foo\end' "\\foo"
}

test_meaning_of_def_emits_backslash_def() {
    cortex_ok "meaning_of_def" '\meaning\def\end' "\\def"
}

test_number_primitive_emits_decimal_representation() {
    cortex_ok "number_emits" '\number 42\end' "42"
}

test_romannumeral_emits_lowercase_roman() {
    cortex_ok "romannumeral_42" '\romannumeral 42\end' "xlii"
}

# ============================================================================
# Tests — case conversion
# ============================================================================

test_uppercase_converts_lowercase_to_upper() {
    cortex_ok "uppercase" '\uppercase{hello}\end' "HELLO"
}

test_lowercase_converts_uppercase_to_lower() {
    cortex_ok "lowercase" '\lowercase{HELLO}\end' "hello"
}

# ============================================================================
# Tests — job control and edge cases
# ============================================================================

test_end_stops_processing_immediately() {
    cortex_ok "end_stops" 'Hello\end Ignored\end' "Hello"
}

test_stray_rbrace_at_level_zero_does_not_crash() {
    cortex_nocrash "stray_rbrace" '}\end'
}

test_stray_rbraces_output_remaining_text() {
    cortex_ok "stray_rbraces" '}}hello\end' "hello"
}

test_stray_endgroup_at_level_zero_does_not_crash() {
    cortex_nocrash "stray_endgroup" '\endgroup\end'
}

test_unmatched_brace_in_def_does_not_hang() {
    run ccraft "$CORTEX" <<< '\def\x{unclosed\end'
    assert_stderr_has "unmatched_brace/stderr" "Error"
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
