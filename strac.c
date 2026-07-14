#!/usr/bin/env -S ccraft
// strac — original dialect: TRAC primitives + Scheme bracket calls
//
// Takes function names from canonical TRAC (ds, ss, cl, ps, rs, cc,
// cn, cs, eq, ad, su, ml, dv; Mooers, 1960s) but replaces TRAC's
// `#(fn,args)` syntax with Scheme-style `[fn,args]` brackets and
// `;[...]` for literal quoting. Commas as separators (rather than
// spaces) are a pragmatic concession — keeps parsing simple without
// a tokenizer. Not a faithful implementation of any standard.
//
// LANGUAGE — `[name,args...]` calls a function (active), `;[...]`
// emits literal text (neutral). First arg is the function name
// (2-letter primitive or user-defined form). Args are comma-separated
// and evaluated recursively. Characters outside brackets pass through
// literally. The `ds` value arg is NOT evaluated (passive).
//
// Canonical TRAC uses `#(fn,..)` — see mint.c for that syntax.
//
// PRIMITIVES (all called inside brackets: `[ps,hello]`)
//   I/O:
//     [ps,text]       — print text to output
//     [rs]            — read stdin until EOF, emit it
//   Forms (definitions):
//     [ds,name,val]   — define or replace a form
//     [cl,name,a..]   — call form: fill gaps with args, eval result
//     [dd,name]       — delete one form
//     [da]            — delete all forms
//     [ss,name,p..]   — segment: replace pattern strings with gap markers
//   Arithmetic (integer):
//     [ad,a,b]  [su,a,b]  [ml,a,b]  [dv,a,b]   — add/sub/mul/div
//   Compare:
//     [eq,a,b,then,else] — if a==b eval "then", else eval "else"
//   Character reading (walks a form body via internal pointer):
//     [cr,name]       — reset pointer to start
//     [cc,name]       — read one char, advance pointer
//     [cn,name,n]     — read n chars
//     [cs,name]       — read until gap marker (0x01..), skip it
//     [in,name,pat]   — read until pattern matches, advance past it
//
// GAP MARKERS (user-defined forms with parameters)
// Forms can contain gap markers (bytes 0x01..0xFF). To create them,
// define a form containing literal placeholder text, then use ss to
// replace those placeholders with gap markers:
//   [ds,greet,Hello $ world]  [ss,greet,$]   -- $ becomes gap #1
//   [cl,greet,dear]  -- replaces gap #1 with "dear" and evaluates
// This yields "Hello dear world" emitted by ps/text in the form body.
// Each ss argument produces the next gap (#1, #2, ...).
// cl fills gap #N with argument N (1-based).
//
// HOW IT WORKS
// Recursive descent evaluator reads text left-to-right. Active calls
// extract brackets, parse comma args, eval each recursively, then
// dispatch 2-char primitive. Forms store gap markers (0x01..)
// filled from call arguments. Output accumulates in emit buffer,
// captured per-argument during evaluation, flushed at top level.
// Recursion limit: 200 (MAX_DEPTH).
//
// EXAMPLES
//   echo '[ps,hello]' | strac               # hello
//   echo '[ad,3,4]' | strac                 # 7
//   echo '[eq,ab,ab,=,ne]' | strac          # =
//   echo '[ds,G,hi][cl,G]' | strac          # hi
//   echo '[ps,foo][ps,bar]' | strac         # foobar
//
// USAGE
//   strac [file]
//
// EXIT CODES
//   0  success  1  file open error
//
// DEPENDENCIES
//   ccraft — shebang-based build
//   libc
//
// REFERENCE
//   TRAC — https://en.wikipedia.org/wiki/TRAC_(programming_language)
//          Mooers, Calvin (1960s). Primitive names (ds, ss, cl, ps,
//          rs, cc, cn, cs, eq, ad, su, ml, dv) taken from here.
//
// ============================================================================
// STRAC TUTORIAL
// ============================================================================
//
// WHAT IS STRAC?
// ============================================================================
//
// strac is Scheme-style brackets `[...]` with TRAC function names.
// Characters outside brackets pass through literally.
// `;[literal]` prints its contents without evaluating.
//
// ============================================================================
// THE TWO RULES
// ============================================================================
//
// 1. `[name,args...]` calls a function (active) — args are evaluated
// 2. `;[literal]` prints literally (neutral) — no evaluation
//
// Everything else is copied to output as-is.
//
// ============================================================================
// SIMPLE MATH
// ============================================================================
//
//     [ps,hello]             outputs: hello
//     [ps,hello][ps,world]   outputs: helloworld
//     [ad,3,4]               outputs: 7
//     [su,10,3]              outputs: 7
//     [ml,3,4]               outputs: 12
//     [dv,10,3]              outputs: 3
//
// ============================================================================
// FORMS: NAMED TEXT
// ============================================================================
//
// Forms are like variables that hold text. Once defined, you can
// call them with `cl`.
//
//     [ds,g,Hello World]     -- define form "g" = "Hello World"
//     [cl,g]                 -- call "g", evaluates "Hello World"
//                              -> outputs: Hello World
//
// If the form body contains `[ps,...]`, it gets executed:
//
//     [ds,g,[ps,hello]]      -- define form with executable content
//     [cl,g]                 -- evaluates [ps,hello] -> outputs: hello
//
// ============================================================================
// GAP MARKERS (THE TRICKY PART)
// ============================================================================
//
// Forms can have "gaps" — slots filled from arguments.
//
// Step 1: Define with placeholder text:
//
//     [ds,greet,Hello $ world]
//     -- form "greet" = "Hello $ world"
//
// Step 2: Replace placeholder with gap marker:
//
//     [ss,greet,$]
//     -- finds $" and replaces it with gap marker byte 0x01
//     -- form "greet" now = "Hello " + gap_01 + " world"
//
// Step 3: Call with actual text:
//
//     [cl,greet,dear]
//     -- fills gap #1 with "dear"
//     -- evaluates "Hello dear world"
//     -> outputs: Hello dear world
//
// Multiple gaps:
//
//     [ds,fmt,X $ Y $ Z][ss,fmt,$,$][cl,fmt,A,B]
//     -- gap #1 = A, gap #2 = B
//     -> outputs: X A Y B Z
//
// ============================================================================
// COMPARISON: CONDITIONAL EXECUTION
// ============================================================================
//
//     [eq,a,b,then,else]     -- if a==b, eval "then", else eval "else"
//     [eq,abc,abc,[ps,yes],[ps,no]]   -> outputs: yes
//     [eq,abc,xyz,[ps,yes],[ps,no]]   -> outputs: no
//
// ============================================================================
// CHARACTER READING
// ============================================================================
//
// Each form has a pointer for sequential character access:
//
//     [ds,x,hello]
//     [cr,x]          -- reset pointer
//     [cc,x]          -- read one char: 'h'
//     [cc,x]          -- read one char: 'e'
//     [cn,x,2]        -- read 2 chars: "ll"
//     [cs,x]          -- read until gap marker (or end): "o"
//     [in,x,l]        -- read until pattern "l": "" (already past)
//
// ============================================================================
// USER-DEFINED FORMS WITH GAPS
// ============================================================================
//
// Combine ds + ss + cl to make reusable templates:
//
//     [ds,double,[ps,$$ doubled is $]]
//     [ss,double,$,$$]
//     [cl,double,5]
//     -> outputs: 5 doubled is 5
//
// (Gap #1 = first arg, gap #2 = second arg, etc.)
//
// ============================================================================
// QUICK REFERENCE
// ============================================================================
//
// I/O:      [ps,text]  [rs]
// FORMS:    [ds,n,v]   [cl,n,a..]  [dd,n]  [da]  [ss,n,p..]
// ARITH:    [ad,a,b]   [su,a,b]    [ml,a,b]   [dv,a,b]
// COMPARE:  [eq,a,b,t,e]
// READ:     [cr,n]     [cc,n]      [cn,n,c]   [cs,n]  [in,n,p]
//
// USAGE: echo '[ps,hello]' | strac   or   strac file.trc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_DEFS  256
#define MAX_NAME  64
#define MAX_VAL   1024
#define MAX_ARGS  32
#define MAX_GAPS  32
#define GAP_BASE  '\x01'

// ---------------------------------------------------------------------------
// Character predicates - test single character properties
// ---------------------------------------------------------------------------

static int is_open(char c)   { return c == '['; }
static int is_close(char c)  { return c == ']'; }
static int is_comma(char c)  { return c == ','; }
static int is_semi(char c)   { return c == ';'; }
static int is_digit(char c)  { return c >= '0' && c <= '9'; }
static int is_gap(char c)    { return c >= GAP_BASE && c < GAP_BASE + MAX_GAPS; }

// String predicates - domain questions about text
static int eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

// ---------------------------------------------------------------------------
// Output buffer - accumulates text to print
//
// Functions emit characters to this buffer. At top level, it's flushed
// to stdout. During argument evaluation, it's captured and restored.
// ---------------------------------------------------------------------------

static char out[MAX_VAL];
static int olen;

// emit - add single character to output
static void emit(char c)          { if (olen < MAX_VAL - 1) out[olen++] = c; }

// emit_str - add string to output
static void emit_str(const char *s) { while (*s) emit(*s++); }

// emit_flush - write output to stdout and clear
static void emit_flush(void)      { out[olen] = '\0'; printf("%s", out); olen = 0; }

// emit_save - remember current position for later capture
static int  emit_save(void)       { return olen; }

// emit_capture - extract output since 'from' into buf, then restore
static void
emit_capture(int from, char *buf, int sz) {
    int len = olen - from;
    if (len >= sz) len = sz - 1;
    if (len > 0) memcpy(buf, out + from, len);
    buf[len] = '\0';
    olen = from;
}

// ---------------------------------------------------------------------------
// Definition storage - named forms with gaps and read pointer
//
// TRAC stores "forms" (definitions) that can contain "gaps" (parameter
// markers). When called, gaps are filled with arguments.
// ---------------------------------------------------------------------------

struct def {
    char name[MAX_NAME];    // form identifier
    char val[MAX_VAL];      // form body (may contain gap markers)
    int gaps;               // number of gaps created by ss
    int ptr;                // read pointer for cc/cn/cs/in
    int used;               // 1 if slot is in use
};

static struct def defs[MAX_DEFS];

// def_find - look up definition by name, return NULL if not found
static struct def *
def_find(const char *name) {
    for (int i = 0; i < MAX_DEFS; i++) {
        if (defs[i].used && eq(defs[i].name, name)) {
            return &defs[i];
        }
    }
    return NULL;
}

// def_alloc - find an unused slot for a new definition
static struct def *
def_alloc(void) {
    for (int i = 0; i < MAX_DEFS; i++) {
        if (!defs[i].used) return &defs[i];
    }
    return NULL;
}

// def_store - create or replace a definition
static void
def_store(const char *name, const char *val) {
    struct def *d = def_find(name);
    if (d == NULL) {
        d = def_alloc();
        if (d == NULL) return;
        strncpy(d->name, name, MAX_NAME - 1);
        d->name[MAX_NAME - 1] = '\0';
        d->used = 1;
    }
    strncpy(d->val, val, MAX_VAL - 1);
    d->val[MAX_VAL - 1] = '\0';
    d->gaps = 0;
    d->ptr = 0;
}

// Definition predicates and operations
static void def_delete(const char *name) { struct def *d = def_find(name); if (d) d->used = 0; }
static void def_clear(void)              { for (int i = 0; i < MAX_DEFS; i++) defs[i].used = 0; }
static void def_reset(struct def *d)     { d->ptr = 0; }
static int  def_exhausted(struct def *d) { return d->val[d->ptr] == '\0'; }
static char def_read(struct def *d)      { return d->val[d->ptr++]; }
static char def_peek(struct def *d)      { return d->val[d->ptr]; }
static int  has_no_gaps(struct def *d)   { return d->gaps == 0; }

// ---------------------------------------------------------------------------
// Number conversion - simple integer parsing and formatting
// ---------------------------------------------------------------------------

// str_to_int - parse integer from string (handles negative)
static int
str_to_int(const char *s) {
    int n = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (is_digit(*s)) { n = n * 10 + (*s - '0'); s++; }
    return neg ? -n : n;
}

// int_to_str - format integer to string
static void
int_to_str(char *buf, int n) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int neg = n < 0;
    if (neg) { *buf++ = '-'; n = -n; }
    char tmp[32]; int i = 0;
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) *buf++ = tmp[--i];
    *buf = '\0';
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

// find_close - find matching ] accounting for nesting, returns index or -1
static int
find_close(const char *s) {
    int depth = 1, i = 0;
    while (s[i] && depth > 0) {
        if (is_open(s[i])) depth++;
        if (is_close(s[i])) depth--;
        i++;
    }
    return depth == 0 ? i - 1 : -1;
}

// parse_args - split string by commas (respecting bracket nesting)
static void
parse_args(const char *s, char args[][MAX_VAL], int *n) {
    *n = 0;
    int i = 0, depth = 0, start = 0;

    while (s[i]) {
        if (is_open(s[i])) depth++;
        if (is_close(s[i])) depth--;

        // Comma at depth 0 = argument separator
        if (depth == 0 && is_comma(s[i])) {
            int len = i - start;
            if (*n < MAX_ARGS) {
                if (len > 0) { strncpy(args[*n], s + start, len); args[*n][len] = '\0'; }
                else args[*n][0] = '\0';
                (*n)++;
            }
            start = i + 1;
        }
        i++;
    }

    // Final argument
    int len = i - start;
    if (*n < MAX_ARGS) {
        if (len > 0) { strncpy(args[*n], s + start, len); args[*n][len] = '\0'; }
        else args[*n][0] = '\0';
        (*n)++;
    }
}

// ---------------------------------------------------------------------------
// Forward declaration for recursive evaluation
// ---------------------------------------------------------------------------

static void eval(const char *text);

// ---------------------------------------------------------------------------
// Primitives - built-in TRAC functions
//
// All primitives have signature: (args, nargs)
// - args: array of evaluated argument strings
// - nargs: number of arguments
// ---------------------------------------------------------------------------

typedef void (*prim_fn)(char[][MAX_VAL], int);

// p_ps - [ps,text] - print string
// Emits text to output.
static void p_ps(char a[][MAX_VAL], int n) { if (n > 0) emit_str(a[0]); }

// p_rs - [rs] - read string
// Reads from stdin until EOF and emits it.
static void
p_rs(char a[][MAX_VAL], int n) {
    (void)a; (void)n;
    char buf[MAX_VAL];
    int i = 0, c;
    while ((c = getchar()) != EOF && i < MAX_VAL - 1) buf[i++] = c;
    buf[i] = '\0';
    emit_str(buf);
}

// p_ds - [ds,name,value] - define string
// Creates a form with given name and value.
static void p_ds(char a[][MAX_VAL], int n) { if (n >= 2) def_store(a[0], a[1]); }

// p_dd - [dd,name] - delete definition
static void p_dd(char a[][MAX_VAL], int n) { if (n > 0) def_delete(a[0]); }

// p_da - [da] - delete all definitions
static void p_da(char a[][MAX_VAL], int n) { (void)a; (void)n; def_clear(); }

// p_cl - [cl,name,a1,a2,...] - call form
// Retrieves form, fills gaps with arguments, evaluates result.
static void
p_cl(char a[][MAX_VAL], int n) {
    if (n == 0) return;
    struct def *d = def_find(a[0]);
    if (d == NULL) return;

    // No gaps: just evaluate the value
    if (has_no_gaps(d)) {
        char buf[MAX_VAL];
        strncpy(buf, d->val, MAX_VAL - 1);
        buf[MAX_VAL - 1] = '\0';
        eval(buf);
        return;
    }

    // Fill gaps with arguments
    // Gap marker 0x01 = arg 1, 0x02 = arg 2, etc.
    char result[MAX_VAL];
    strncpy(result, d->val, MAX_VAL - 1);
    result[MAX_VAL - 1] = '\0';

    for (int gi = 1; gi < n && gi <= d->gaps; gi++) {
        char gap = GAP_BASE + (gi - 1);
        const char *rep = a[gi];
        int rlen = strlen(rep);

        char tmp[MAX_VAL];
        int ti = 0, i = 0;
        while (result[i] && ti < MAX_VAL - rlen) {
            if (result[i] == gap) {
                strncpy(tmp + ti, rep, MAX_VAL - ti - 1);
                ti += rlen;
                i++;
            } else {
                tmp[ti++] = result[i++];
            }
        }
        tmp[ti] = '\0';
        strncpy(result, tmp, MAX_VAL - 1);
    }

    eval(result);
}

// p_ss - [ss,name,p1,p2,...] - segment string
// Replaces occurrences of p1,p2,... in form with gap markers.
// SIDEFF: modifies form body and gaps count in-place
static void
p_ss(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    struct def *d = def_find(a[0]);
    if (d == NULL) return;

    char result[MAX_VAL];
    strncpy(result, d->val, MAX_VAL - 1);
    result[MAX_VAL - 1] = '\0';

    for (int gi = 1; gi < n && gi <= MAX_GAPS; gi++) {
        const char *pat = a[gi];
        int plen = strlen(pat);
        char gap = GAP_BASE + (gi - 1);

        char tmp[MAX_VAL];
        int ti = 0, i = 0;
        while (result[i] && ti < MAX_VAL - 1) {
            if (starts(result + i, pat)) {
                tmp[ti++] = gap;
                i += plen;
            } else {
                tmp[ti++] = result[i++];
            }
        }
        tmp[ti] = '\0';
        strncpy(result, tmp, MAX_VAL - 1);
    }

    d->gaps = n - 1;
    strncpy(d->val, result, MAX_VAL - 1);
    d->val[MAX_VAL - 1] = '\0';
}

// p_eq - [eq,a,b,then,else] - string equality test
// Evaluates 'then' if a==b, else evaluates 'else'.
static void
p_eq(char a[][MAX_VAL], int n) {
    if (n < 4) return;
    eval(eq(a[0], a[1]) ? a[2] : a[3]);
}

// --- Arithmetic primitives ---

// p_ad - [ad,a,b] - add
static void
p_ad(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    char buf[32];
    int_to_str(buf, str_to_int(a[0]) + str_to_int(a[1]));
    emit_str(buf);
}

// p_su - [su,a,b] - subtract
static void
p_su(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    char buf[32];
    int_to_str(buf, str_to_int(a[0]) - str_to_int(a[1]));
    emit_str(buf);
}

// p_ml - [ml,a,b] - multiply
static void
p_ml(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    char buf[32];
    int_to_str(buf, str_to_int(a[0]) * str_to_int(a[1]));
    emit_str(buf);
}

// p_dv - [dv,a,b] - divide (no-op if b=0)
static void
p_dv(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    int b = str_to_int(a[1]);
    if (b == 0) return;
    char buf[32];
    int_to_str(buf, str_to_int(a[0]) / b);
    emit_str(buf);
}

// --- Character reading primitives ---
// These read from a form's value using its pointer.

// p_cr - [cr,name] - call restore (reset pointer to start)
static void
p_cr(char a[][MAX_VAL], int n) {
    if (n == 0) return;
    struct def *d = def_find(a[0]);
    if (d) def_reset(d);
}

// p_cc - [cc,name] - call character (read one char)
static void
p_cc(char a[][MAX_VAL], int n) {
    if (n == 0) return;
    struct def *d = def_find(a[0]);
    if (d && !def_exhausted(d)) emit(def_read(d));
}

// p_cn - [cn,name,count] - call n characters
static void
p_cn(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    struct def *d = def_find(a[0]);
    if (d == NULL) return;
    int count = str_to_int(a[1]);
    for (int i = 0; i < count && !def_exhausted(d); i++) {
        emit(def_read(d));
    }
}

// p_cs - [cs,name] - call segment (read until gap marker)
static void
p_cs(char a[][MAX_VAL], int n) {
    if (n == 0) return;
    struct def *d = def_find(a[0]);
    if (d == NULL) return;
    while (!def_exhausted(d)) {
        char c = def_peek(d);
        if (is_gap(c)) { d->ptr++; break; }
        emit(def_read(d));
    }
}

// p_in - [in,name,pattern] - read until pattern found
// Emits chars until pattern matches, then positions pointer after pattern.
static void
p_in(char a[][MAX_VAL], int n) {
    if (n < 2) return;
    struct def *d = def_find(a[0]);
    if (d == NULL) return;
    const char *pat = a[1];
    int plen = strlen(pat);
    while (!def_exhausted(d)) {
        if (starts(d->val + d->ptr, pat)) {
            d->ptr += plen;
            return;
        }
        emit(def_read(d));
    }
}

// ---------------------------------------------------------------------------
// Dispatch table - maps 2-char function names to implementations
// ---------------------------------------------------------------------------

struct dispatch {
    const char *name;
    prim_fn fn;
};

static struct dispatch prims[] = {
    // I/O
    {"ps", p_ps}, {"rs", p_rs},
    // Definition management
    {"ds", p_ds}, {"cl", p_cl}, {"dd", p_dd}, {"da", p_da}, {"ss", p_ss},
    // Control flow
    {"eq", p_eq},
    // Arithmetic
    {"ad", p_ad}, {"su", p_su}, {"ml", p_ml}, {"dv", p_dv},
    // Character reading
    {"cr", p_cr}, {"cc", p_cc}, {"cn", p_cn}, {"cs", p_cs}, {"in", p_in},
    {NULL, NULL}
};

// prim_find - look up primitive by name
static prim_fn
prim_find(const char *name) {
    for (struct dispatch *d = prims; d->name; d++) {
        if (eq(d->name, name)) return d->fn;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Evaluation - recursive TRAC interpreter
//
// Processes text character by character:
// - ;[...] neutral call: emit literal [...] without evaluation
// - [...] active call: evaluate contents, dispatch to primitive
// - other: emit literally
// ---------------------------------------------------------------------------

static int depth;   // recursion guard
#define MAX_DEPTH 200

// is_too_deep - recursion limit reached
static int is_too_deep(int d) { return d > MAX_DEPTH; }

// starts_neutral - check for ;[ sequence (neutral call prefix)
static int starts_neutral(const char *s) { return is_semi(s[0]) && is_open(s[1]); }

// handle_neutral - ;[...] emits literal [...] without evaluation
static void
handle_neutral(const char *text, int *i) {
    int end = find_close(text + *i + 2);
    if (end >= 0) {
        emit('[');
        for (int j = 0; j < end; j++) emit(text[*i + 2 + j]);
        emit(']');
        *i += end + 3;
    } else {
        emit(text[(*i)++]);
    }
}

// handle_call - [...] active call: eval args, dispatch primitive
static void
handle_call(const char *text, int *i) {
    int end = find_close(text + *i + 1);
    if (end < 0) { emit(text[(*i)++]); return; }

    char content[MAX_VAL];
    strncpy(content, text + *i + 1, end);
    content[end] = '\0';

    char args[MAX_ARGS][MAX_VAL];
    int nargs;
    parse_args(content, args, &nargs);

    if (nargs == 0) { *i += end + 2; return; }

    char evaled[MAX_ARGS][MAX_VAL];
    int saved = emit_save();
    eval(args[0]);
    emit_capture(saved, evaled[0], MAX_VAL);

    int is_ds = eq(evaled[0], "ds");

    for (int j = 1; j < nargs; j++) {
        if (is_ds && j == 2) {
            strncpy(evaled[j], args[j], MAX_VAL - 1);
            evaled[j][MAX_VAL - 1] = '\0';
        } else {
            saved = emit_save();
            eval(args[j]);
            emit_capture(saved, evaled[j], MAX_VAL);
        }
    }

    prim_fn fn = prim_find(evaled[0]);
    if (fn) fn(evaled + 1, nargs - 1);

    *i += end + 2;
}

// eval - evaluate TRAC text
static void
eval(const char *text) {
    int i = 0;

    depth++;
    if (is_too_deep(depth)) { depth--; return; }

    while (text[i]) {
        if (starts_neutral(text + i)) { handle_neutral(text, &i); continue; }
        if (is_open(text[i]))          { handle_call(text, &i);   continue; }
        emit(text[i++]);
    }

    depth--;
}

// ---------------------------------------------------------------------------
// Main - read input and run interpreter
// ---------------------------------------------------------------------------

// run - process file through interpreter
static void
run(FILE *f) {
    char buf[MAX_VAL];
    size_t n;
    int first = 1;

    while ((n = fread(buf, 1, sizeof(buf) - 1, f)) > 0) {
        buf[n] = '\0';
        // Skip shebang line if present
        if (first && n >= 2 && buf[0] == '#' && buf[1] == '!') {
            first = 0;
            char *nl = strchr(buf, '\n');
            if (nl) eval(nl + 1);
        } else {
            first = 0;
            eval(buf);
        }
    }

    emit_flush();
}

int
main(int argc, char **argv) {
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (f == NULL) { fprintf(stderr, "strac: cannot open %s\n", argv[1]); return 1; }
        run(f);
        fclose(f);
    } else {
        run(stdin);
    }
    return 0;
}
