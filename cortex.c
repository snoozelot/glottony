#!/usr/bin/env -S ccraft
// cortex — faithful TeX macro processor (expansion engine only)
//
// Processes TeX macro language without the typesetting: the "programmable
// preprocessor" component of TeX. Reads from stdin, outputs expanded token
// stream. Handles definitions, conditionals, arithmetic, file I/O, and all
// e-TeX macro extensions.
//
// HOW IT WORKS:
//   1. Tokenize input characters by category code (escape, letter, etc.)
//   2. Expand macros left-to-right by dispatch table
//   3. Execute non-expandable commands (definitions, assignments, I/O)
//   4. Output resulting token stream to stdout
//
// USAGE: cortex [filename] < input.tex > output.txt
//        [filename] sets \jobname; no file reading, input always stdin
//
// OPTIONS: none (positional arg sets jobname)
//
// EXIT CODES:
//   0 — Success
//   1 — Fatal error (memory, runaway conditional, syntax)
//
// DEPENDENCIES: stdlib (stdio, stdlib, string, ctype, time, limits.h)
//               ccraft, gcc (any C99)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>

// --- Configuration ---

#define NUM_CHARS       256   // Character code range (0-255)
#define MAX_NAMES       20000
#define MAX_STRINGS     200000
#define MAX_SAVE        20000
#define MAX_REGISTERS   256
#define MAX_LIST_BUILD  200000
#define MAX_CS_NAME     256
#define MAX_INT_DIGITS  32
#define MAX_AFTERGROUP  1000
#define MAX_FILENAME    256

// --- Category Codes ---

typedef enum {
    CAT_ESCAPE      = 0,
    CAT_BEGIN_GROUP = 1,
    CAT_END_GROUP   = 2,
    CAT_MATH        = 3,
    CAT_ALIGN       = 4,
    CAT_EOL         = 5,
    CAT_PARAM       = 6,
    CAT_SUP         = 7,
    CAT_SUB         = 8,
    CAT_IGNORE      = 9,
    CAT_SPACE       = 10,
    CAT_LETTER      = 11,
    CAT_OTHER       = 12,
    CAT_ACTIVE      = 13,
    CAT_COMMENT     = 14,
    CAT_INVALID     = 15
} catcode;

// --- Token Types ---

typedef enum {
    CMD_CHAR, CMD_CS, CMD_PARAM_REF, CMD_MATCH
} token_cmd;

struct token {
    token_cmd cmd;
    int val;
};

struct token_list {
    struct token *tokens;
    size_t len;
};

// --- Meaning Types ---

typedef enum {
    M_UNDEFINED, M_MACRO, M_CHAR, M_CHARDEF,
    M_DEF, M_GDEF, M_EDEF, M_XDEF,
    M_LET, M_FUTURELET, M_GLOBAL, M_LONG, M_OUTER, M_PROTECTED,
    M_CHARDEF_CMD, M_COUNTDEF, M_TOKSDEF,
    M_CATCODE, M_LCCODE, M_UCCODE,
    M_COUNT, M_TOKS, M_ADVANCE, M_MULTIPLY, M_DIVIDE,
    M_IF, M_IFX, M_IFNUM, M_IFCAT, M_IFDEFINED, M_IFCSNAME,
    M_IFCASE, M_IFTRUE, M_IFFALSE, M_IFODD, M_OR, M_ELSE, M_FI, M_UNLESS,
    M_IFEOF,
    M_EXP_AFTER, M_NOEXPAND, M_THE, M_NUMBER, M_ROMANNUMERAL,
    M_STRING, M_MEANING, M_CSNAME, M_ENDCSNAME,
    M_UPPERCASE, M_LOWERCASE, M_DETOKENIZE, M_UNEXPANDED,
    M_SCANTOKENS, M_EXPANDED,
    M_INPUT, M_ENDINPUT, M_JOBNAME, M_INPUTLINENO,
    M_BEGINGROUP, M_ENDGROUP,
    M_AFTERGROUP, M_AFTERASSIGNMENT,
    M_MESSAGE, M_ERRMESSAGE, M_SHOW, M_SHOWTHE,
    M_LBRACE, M_RBRACE, M_RELAX,
    M_NUMEXPR,
    M_ESCAPECHAR, M_ENDLINECHAR, M_NEWLINECHAR,
    M_IGNORESPACES,
    M_READ, M_WRITE, M_OPENIN, M_CLOSEIN, M_OPENOUT, M_CLOSEOUT, M_IMMEDIATE,
    M_READLINE,
    M_YEAR, M_MONTH, M_DAY, M_TIME,
    M_END,
    M_GLOBALDEFS,
    M_CURRENTGROUPLEVEL, M_CURRENTGROUPTYPE,
    M_CURRENTIFLEVEL, M_CURRENTIFTYPE, M_CURRENTIFBRANCH,
    M_TRACINGMACROS, M_TRACINGCOMMANDS,
    M_TYPE_COUNT
} meaning_type;

struct meaning {
    meaning_type type;
    int val;
};

// --- Macro Storage ---

struct macro {
    struct token_list pattern;
    struct token_list body;
    int nparams;
    int is_long;
    int is_outer;
    int is_protected;
};

// --- Save Stack ---

typedef enum {
    SAVE_MEANING, SAVE_COUNT, SAVE_TOKS, SAVE_CATCODE, SAVE_LCCODE, SAVE_UCCODE
} save_type;

struct save_entry {
    save_type type;
    int idx;
    struct meaning meaning;
    struct token_list toks;
    int level;
};

// --- Input State ---

struct input_state {
    struct token *tokens;
    size_t count;
    size_t pos;
    struct input_state *prev;
    FILE *file;
    int is_file;
    int owns_tokens;
    int endinput;
    int lineno;
};

// --- Globals ---

static catcode catcodes[NUM_CHARS];
static int lccode[NUM_CHARS];
static int uccode[NUM_CHARS];

static char string_pool[MAX_STRINGS];
static size_t string_used = 0;

static char *names[MAX_NAMES];
static size_t nnames = 0;

static struct macro macros[MAX_NAMES];
static int nmacros = 0;

static struct meaning eqtb[MAX_NAMES];
static int count_regs[MAX_REGISTERS];
static struct token_list toks_regs[MAX_REGISTERS];

static struct save_entry save_stack[MAX_SAVE];
static int save_ptr = 0;
static int level = 0;

static struct input_state *input = NULL;
static int ungot = EOF;

static struct token build_buf[MAX_LIST_BUILD];
static size_t build_len = 0;

static struct token aftergroup_stack[MAX_AFTERGROUP];
static int aftergroup_ptr = 0;
static int aftergroup_level[MAX_AFTERGROUP];

static struct token afterassignment_token;
static int afterassignment_pending = 0;

static int global_flag = 0;
static int long_flag = 0;
static int outer_flag = 0;
static int protected_flag = 0;
static int noexpand_next = 0;
static int unless_flag = 0;

static char jobname[MAX_FILENAME] = "texput";

// Character parameters
static int escapechar = '\\';
static int endlinechar = '\r';
static int newlinechar = -1;

// File I/O streams (0-15 for \openin/\read, 0-15 for \openout/\write)
#define MAX_STREAMS 16
static FILE *read_files[MAX_STREAMS];
static FILE *write_files[MAX_STREAMS];
static int read_eof[MAX_STREAMS];

// \immediate parsed as no-op (no deferred-write queue)

// Date/time (set at startup)
static int tex_year, tex_month, tex_day, tex_time;

// Global assignment control
static int globaldefs = 0;

// Primitive indices
static int count_primitive_idx;

// Tracing
static int tracingmacros = 0;
static int tracingcommands = 0;

// Conditional stack for introspection (e-TeX)
#define MAX_COND_STACK 256
static int cond_type_stack[MAX_COND_STACK];
static int cond_branch_stack[MAX_COND_STACK];  // 1=then, -1=else, 0=not yet decided
static int cond_ptr = 0;

// Conditional type numbers (e-TeX currentiftype)
#define COND_IF       1
#define COND_IFCAT    2
#define COND_IFNUM    3
#define COND_IFODD    4
#define COND_IFX      12
#define COND_IFEOF    13
#define COND_IFTRUE   14
#define COND_IFFALSE  15
#define COND_IFCASE   16
#define COND_IFDEFINED 17
#define COND_IFCSNAME  18

// --- Forward Declarations ---

static struct token get_token(void);
static struct token get_raw_token(void);
static void push_tokens(struct token_list *list, int own);
static int intern(const char *name);
static int scan_int(void);
static void skip_conditional(void);
static void skip_optional_equals(void);
static void skip_keyword_to(void);
static void skip_spaces_raw(void);

// --- Error Handling ---

static void
die(const char *msg) {
    fprintf(stderr, "! Error: %s\n", msg);
    exit(1);
}

// --- Catcode Predicates ---

static int
has_catcode(int c, catcode cat) {
    return c >= 0 && c < NUM_CHARS && catcodes[c] == cat;
}

static int is_escape(int c)      { return has_catcode(c, CAT_ESCAPE); }
static int is_begin_group(int c) { return has_catcode(c, CAT_BEGIN_GROUP); }
static int is_end_group(int c)   { return has_catcode(c, CAT_END_GROUP); }
static int is_param(int c)       { return has_catcode(c, CAT_PARAM); }
static int is_space(int c)       { return has_catcode(c, CAT_SPACE); }
static int is_letter(int c)      { return has_catcode(c, CAT_LETTER); }
static int is_comment(int c)     { return has_catcode(c, CAT_COMMENT); }
static int is_active(int c)      { return has_catcode(c, CAT_ACTIVE); }
static int is_eol(int c)         { return has_catcode(c, CAT_EOL); }

// --- Character Predicates ---

static int is_digit(int c)    { return c >= '0' && c <= '9'; }
static int is_hex_digit(int c){ return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static int is_octal_digit(int c) { return c >= '0' && c <= '7'; }

static int
hex_value(int c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return 0;
}

// --- Token Predicates ---

static int is_char_token(struct token t)  { return t.cmd == CMD_CHAR; }
static int is_cs_token(struct token t)    { return t.cmd == CMD_CS; }
static int is_param_ref(struct token t)   { return t.cmd == CMD_PARAM_REF; }
static int is_match_token(struct token t) { return t.cmd == CMD_MATCH; }
static int is_eof_token(struct token t)   { return is_char_token(t) && t.val == EOF; }

static int
is_space_token(struct token t) {
    return is_char_token(t) && is_space(t.val);
}

static int
is_digit_token(struct token t) {
    return is_char_token(t) && is_digit(t.val);
}

static int
is_begin_group_token(struct token t) {
    return is_char_token(t) && is_begin_group(t.val);
}

static int
is_param_token(struct token t) {
    return is_char_token(t) && is_param(t.val);
}

static int
is_hex_digit_token(struct token t) {
    return is_char_token(t) && is_hex_digit(t.val);
}

static int
is_octal_digit_token(struct token t) {
    return is_char_token(t) && is_octal_digit(t.val);
}

// Number syntax markers
static int is_minus_token(struct token t)       { return is_char_token(t) && t.val == '-'; }
static int is_plus_token(struct token t)        { return is_char_token(t) && t.val == '+'; }
static int is_backtick_token(struct token t)    { return is_char_token(t) && t.val == '`'; }
static int is_hex_prefix_token(struct token t)  { return is_char_token(t) && t.val == '"'; }
static int is_octal_prefix_token(struct token t){ return is_char_token(t) && t.val == '\''; }

// Expression tokens
static int is_lparen_token(struct token t) { return is_char_token(t) && t.val == '('; }
static int is_rparen_token(struct token t) { return is_char_token(t) && t.val == ')'; }
static int is_star_token(struct token t)   { return is_char_token(t) && t.val == '*'; }
static int is_slash_token(struct token t)  { return is_char_token(t) && t.val == '/'; }

static int
tokens_equal(struct token a, struct token b) {
    return a.cmd == b.cmd && a.val == b.val;
}

static int
meanings_equal(struct meaning a, struct meaning b) {
    return a.type == b.type && a.val == b.val;
}

// --- Meaning Predicates ---

static int is_undefined(struct meaning m)  { return m.type == M_UNDEFINED; }
static int is_macro(struct meaning m)      { return m.type == M_MACRO; }
static int is_chardef(struct meaning m)    { return m.type == M_CHARDEF; }
static int is_catcode_cmd(struct meaning m){ return m.type == M_CATCODE; }
static int is_lccode_cmd(struct meaning m) { return m.type == M_LCCODE; }
static int is_uccode_cmd(struct meaning m) { return m.type == M_UCCODE; }
static int is_count(struct meaning m)      { return m.type == M_COUNT; }
static int is_toks(struct meaning m)       { return m.type == M_TOKS; }
static int is_escapechar(struct meaning m) { return m.type == M_ESCAPECHAR; }
static int is_endlinechar(struct meaning m){ return m.type == M_ENDLINECHAR; }
static int is_newlinechar(struct meaning m){ return m.type == M_NEWLINECHAR; }
static int is_inputlineno(struct meaning m){ return m.type == M_INPUTLINENO; }
static int is_year(struct meaning m)       { return m.type == M_YEAR; }
static int is_month(struct meaning m)      { return m.type == M_MONTH; }
static int is_day(struct meaning m)        { return m.type == M_DAY; }
static int is_time_cmd(struct meaning m)   { return m.type == M_TIME; }
static int is_tracingmacros(struct meaning m)     { return m.type == M_TRACINGMACROS; }
static int is_tracingcommands(struct meaning m)   { return m.type == M_TRACINGCOMMANDS; }
static int is_globaldefs(struct meaning m) { return m.type == M_GLOBALDEFS; }
static int is_currentgrouplevel(struct meaning m) { return m.type == M_CURRENTGROUPLEVEL; }
static int is_currentgrouptype(struct meaning m)  { return m.type == M_CURRENTGROUPTYPE; }
static int is_currentiflevel(struct meaning m)    { return m.type == M_CURRENTIFLEVEL; }
static int is_currentiftype(struct meaning m)     { return m.type == M_CURRENTIFTYPE; }
static int is_currentifbranch(struct meaning m)   { return m.type == M_CURRENTIFBRANCH; }
static int is_numexpr(struct meaning m)    { return m.type == M_NUMEXPR; }
static int is_endcsname(struct meaning m)  { return m.type == M_ENDCSNAME; }
static int is_relax(struct meaning m)      { return m.type == M_RELAX; }
static int is_or(struct meaning m)         { return m.type == M_OR; }
static int is_else(struct meaning m)       { return m.type == M_ELSE; }
static int is_fi(struct meaning m)         { return m.type == M_FI; }
static int
is_count_primitive(struct token t) {
    return is_cs_token(t) && t.val == count_primitive_idx;
}

static int is_unexpanded(struct meaning m) { return m.type == M_UNEXPANDED; }

static int
is_valid_type(struct meaning m) {
    return m.type >= 0 && m.type < M_TYPE_COUNT;
}

static int
is_conditional(struct meaning m) {
    return (m.type >= M_IF && m.type <= M_IFODD) || m.type == M_IFEOF;
}

// Domain state predicates
static int in_group(void)       { return level > 0; }
static int in_conditional(void) { return cond_ptr > 0; }

// Character parameter validity
static int charcode_valid(int c) { return c >= 0 && c < NUM_CHARS; }
static int catcode_valid(int cat) { return cat >= 0 && cat <= 15; }
static int has_uccode(int c) { return uccode[c] != 0; }
static int has_lccode(int c) { return lccode[c] != 0; }

// Register and stream validity
static int is_valid_register(int n) { return n >= 0 && n < MAX_REGISTERS; }
static int stream_valid(int n) { return n >= 0 && n < MAX_STREAMS; }
static int read_stream_open(int n)  { return stream_valid(n) && read_files[n] != NULL; }
static int write_stream_open(int n) { return stream_valid(n) && write_files[n] != NULL; }
static int read_stream_eof(int n)   { return !read_stream_open(n) || read_eof[n]; }
static int is_terminal_stream(int n) { return n < 0; }

// Character parameter state
static int escapechar_enabled(void) { return charcode_valid(escapechar); }
static int newlinechar_active(void) { return newlinechar >= 0; }
static int matches_newlinechar(int c) { return newlinechar_active() && c == newlinechar; }

// Tracing state
static int tracing_macros(void) { return tracingmacros > 0; }



// --- String Pool ---

static char *
store_string(const char *s) {
    size_t len = strlen(s) + 1;
    if (string_used + len >= MAX_STRINGS) { die("String pool full"); }

    char *p = &string_pool[string_used];
    strcpy(p, s);
    string_used += len;
    return p;
}

// --- Name Table ---

static int
intern(const char *name) {
    for (size_t i = 0; i < nnames; i++) {
        if (strcmp(names[i], name) == 0) { return (int)i; }
    }

    if (nnames >= MAX_NAMES) { die("Name table full"); }
    names[nnames] = store_string(name);
    return (int)nnames++;
}

// --- Growable Token Array ---
// Replaces fixed-size buffers that silently truncated.

typedef struct {
    struct token *tokens;
    size_t len;
    size_t cap;
} token_array_t;

static void
token_array_grow(token_array_t *a) {
    a->cap = a->cap ? a->cap * 2 : 1024;
    struct token *nt = realloc(a->tokens, a->cap * sizeof(struct token));
    if (!nt) { die("Out of memory"); }
    a->tokens = nt;
}

static void
token_array_append(token_array_t *a, struct token t) {
    if (a->len >= a->cap) { token_array_grow(a); }
    a->tokens[a->len++] = t;
}

static struct token_list
token_array_finish(token_array_t *a) {
    struct token_list list;
    list.tokens = a->tokens;
    list.len = a->len;
    a->tokens = NULL;  // ownership transferred
    return list;
}

// --- Token List Operations ---

static struct token *
clone_tokens(struct token *src, size_t len) {
    if (len == 0) { return NULL; }

    struct token *dest = malloc(len * sizeof(struct token));
    if (!dest) { die("Out of memory"); }
    memcpy(dest, src, len * sizeof(struct token));
    return dest;
}

static struct token *
alloc_tokens(size_t n) {
    struct token *p = malloc(n * sizeof(struct token));
    if (!p) { die("Out of memory"); }
    return p;
}

static char *
alloc_str(size_t n) {
    char *p = malloc(n);
    if (!p) { die("Out of memory"); }
    return p;
}

static struct token
make_char_token(int c) {
    struct token t = {CMD_CHAR, c};
    return t;
}

static struct token
make_cs_token(int idx) {
    struct token t = {CMD_CS, idx};
    return t;
}

static void
push_single_token(struct token t) {
    struct token *p = alloc_tokens(1);
    *p = t;
    struct token_list list = {p, 1};
    push_tokens(&list, 1);
}

static void
push_two_tokens(struct token a, struct token b) {
    struct token *p = alloc_tokens(2);
    p[0] = a;
    p[1] = b;
    struct token_list list = {p, 2};
    push_tokens(&list, 1);
}

// --- Token List Building ---

static void build_reset(void) { build_len = 0; }

static void
build_append(struct token t) {
    if (build_len >= MAX_LIST_BUILD) { die("Token list too long"); }
    build_buf[build_len++] = t;
}

static struct token_list
build_finish(void) {
    struct token_list list;
    list.tokens = clone_tokens(build_buf, build_len);
    list.len = build_len;
    return list;
}

// --- Scoping ---

static int at_global_level(void) { return level == 0; }

static void
save_entity(save_type type, int idx) {
    if (at_global_level()) { return; }
    if (save_ptr >= MAX_SAVE) { die("Save stack full"); }

    save_stack[save_ptr].type = type;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].level = level;
    switch (type) {
        case SAVE_MEANING: save_stack[save_ptr].meaning = eqtb[idx]; break;
        case SAVE_COUNT:   save_stack[save_ptr].meaning.val = count_regs[idx]; break;
        case SAVE_TOKS:    save_stack[save_ptr].toks = toks_regs[idx]; break;
        case SAVE_CATCODE: save_stack[save_ptr].meaning.val = catcodes[idx]; break;
        case SAVE_LCCODE:  save_stack[save_ptr].meaning.val = lccode[idx]; break;
        case SAVE_UCCODE:  save_stack[save_ptr].meaning.val = uccode[idx]; break;
    }
    save_ptr++;
}

static void
pop_aftergroup_tokens(void) {
    int start = aftergroup_ptr;
    while (start > 0 && aftergroup_level[start - 1] == level) { start--; }
    for (int i = start; i < aftergroup_ptr; i++) {
        push_single_token(aftergroup_stack[i]);
    }
    aftergroup_ptr = start;
}

static void
restore_scope(void) {
    while (save_ptr > 0 && save_stack[save_ptr - 1].level == level) {
        save_ptr--;
        struct save_entry *e = &save_stack[save_ptr];
        switch (e->type) {
            case SAVE_MEANING: eqtb[e->idx] = e->meaning; break;
            case SAVE_COUNT:   count_regs[e->idx] = e->meaning.val; break;
            case SAVE_TOKS:    toks_regs[e->idx] = e->toks; break;
            case SAVE_CATCODE: catcodes[e->idx] = (catcode)e->meaning.val; break;
            case SAVE_LCCODE:  lccode[e->idx] = e->meaning.val; break;
            case SAVE_UCCODE:  uccode[e->idx] = e->meaning.val; break;
        }
    }
    pop_aftergroup_tokens();
    level--;
}

static void
invalidate_saved(int idx, save_type type) {
    for (int i = save_ptr - 1; i >= 0; i--) {
        if (save_stack[i].type == type && save_stack[i].idx == idx) {
            save_stack[i].level = -1;
        }
    }
}

// Resolve global flag with globaldefs
static int
effective_global(int global) {
    if (globaldefs > 0) { return 1; }
    if (globaldefs < 0) { return 0; }
    return global;
}

// Complete an assignment: clear prefix flags, fire afterassignment
static void
complete_assignment(void) {
    global_flag = 0;
    long_flag = 0;
    outer_flag = 0;
    protected_flag = 0;
    if (afterassignment_pending) {
        afterassignment_pending = 0;
        push_single_token(afterassignment_token);
    }
}

static void
save_or_invalidate(int idx, save_type type, int global) {
    global = effective_global(global);
    if (!global) { save_entity(type, idx); return; }
    invalidate_saved(idx, type);
}

static void
set_meaning(int idx, struct meaning m, int global) {
    save_or_invalidate(idx, SAVE_MEANING, global);
    eqtb[idx] = m;
}

static void
set_count(int idx, int val, int global) {
    if (!is_valid_register(idx)) { die("Count register out of range"); }
    save_or_invalidate(idx, SAVE_COUNT, global);
    count_regs[idx] = val;
}

static void
set_toks(int idx, struct token_list val, int global) {
    if (!is_valid_register(idx)) { die("Tokens register out of range"); }
    save_or_invalidate(idx, SAVE_TOKS, global);
    toks_regs[idx] = val;
}

static void
set_catcode(int idx, int val, int global) {
    save_or_invalidate(idx, SAVE_CATCODE, global);
    catcodes[idx] = (catcode)val;
}

static void
set_lccode_val(int idx, int val, int global) {
    save_or_invalidate(idx, SAVE_LCCODE, global);
    lccode[idx] = val;
}

static void
set_uccode_val(int idx, int val, int global) {
    save_or_invalidate(idx, SAVE_UCCODE, global);
    uccode[idx] = val;
}

// --- Initialization ---

static void
def_primitive(const char *name, meaning_type type) {
    int idx = intern(name);
    eqtb[idx].type = type;
}

static void
init_catcodes(void) {
    for (int i = 0; i < NUM_CHARS; i++) { catcodes[i] = CAT_OTHER; }
    for (int i = 'a'; i <= 'z'; i++) { catcodes[i] = CAT_LETTER; }
    for (int i = 'A'; i <= 'Z'; i++) { catcodes[i] = CAT_LETTER; }
    catcodes['\\'] = CAT_ESCAPE;
    catcodes['{']  = CAT_BEGIN_GROUP;
    catcodes['}']  = CAT_END_GROUP;
    catcodes['%']  = CAT_COMMENT;
    catcodes['#']  = CAT_PARAM;
    catcodes['\n'] = CAT_EOL;
    catcodes[' ']  = CAT_SPACE;
    catcodes['\t'] = CAT_SPACE;
    catcodes['~']  = CAT_ACTIVE;
    catcodes[0]    = CAT_IGNORE;
}

static void
init_case_codes(void) {
    for (int i = 0; i < NUM_CHARS; i++) {
        lccode[i] = 0;
        uccode[i] = 0;
    }
    for (int i = 'a'; i <= 'z'; i++) {
        lccode[i] = i;
        uccode[i] = i - 'a' + 'A';
    }
    for (int i = 'A'; i <= 'Z'; i++) {
        lccode[i] = i - 'A' + 'a';
        uccode[i] = i;
    }
}

static void
init_primitives(void) {
    def_primitive("def", M_DEF);
    def_primitive("gdef", M_GDEF);
    def_primitive("edef", M_EDEF);
    def_primitive("xdef", M_XDEF);
    def_primitive("let", M_LET);
    def_primitive("futurelet", M_FUTURELET);
    def_primitive("global", M_GLOBAL);
    def_primitive("long", M_LONG);
    def_primitive("outer", M_OUTER);
    def_primitive("protected", M_PROTECTED);
    def_primitive("chardef", M_CHARDEF_CMD);
    def_primitive("countdef", M_COUNTDEF);
    def_primitive("toksdef", M_TOKSDEF);
    def_primitive("catcode", M_CATCODE);
    def_primitive("lccode", M_LCCODE);
    def_primitive("uccode", M_UCCODE);
    def_primitive("count", M_COUNT);
    count_primitive_idx = intern("count");
    def_primitive("toks", M_TOKS);
    def_primitive("advance", M_ADVANCE);
    def_primitive("multiply", M_MULTIPLY);
    def_primitive("divide", M_DIVIDE);
    def_primitive("if", M_IF);
    def_primitive("ifx", M_IFX);
    def_primitive("ifnum", M_IFNUM);
    def_primitive("ifcat", M_IFCAT);
    def_primitive("ifdefined", M_IFDEFINED);
    def_primitive("ifcsname", M_IFCSNAME);
    def_primitive("ifcase", M_IFCASE);
    def_primitive("iftrue", M_IFTRUE);
    def_primitive("iffalse", M_IFFALSE);
    def_primitive("ifodd", M_IFODD);
    def_primitive("or", M_OR);
    def_primitive("else", M_ELSE);
    def_primitive("fi", M_FI);
    def_primitive("unless", M_UNLESS);
    def_primitive("expandafter", M_EXP_AFTER);
    def_primitive("noexpand", M_NOEXPAND);
    def_primitive("the", M_THE);
    def_primitive("number", M_NUMBER);
    def_primitive("romannumeral", M_ROMANNUMERAL);
    def_primitive("string", M_STRING);
    def_primitive("meaning", M_MEANING);
    def_primitive("csname", M_CSNAME);
    def_primitive("endcsname", M_ENDCSNAME);
    def_primitive("uppercase", M_UPPERCASE);
    def_primitive("lowercase", M_LOWERCASE);
    def_primitive("detokenize", M_DETOKENIZE);
    def_primitive("unexpanded", M_UNEXPANDED);
    def_primitive("input", M_INPUT);
    def_primitive("endinput", M_ENDINPUT);
    def_primitive("jobname", M_JOBNAME);
    def_primitive("begingroup", M_BEGINGROUP);
    def_primitive("endgroup", M_ENDGROUP);
    def_primitive("aftergroup", M_AFTERGROUP);
    def_primitive("afterassignment", M_AFTERASSIGNMENT);
    def_primitive("message", M_MESSAGE);
    def_primitive("errmessage", M_ERRMESSAGE);
    def_primitive("show", M_SHOW);
    def_primitive("showthe", M_SHOWTHE);
    def_primitive("relax", M_RELAX);
    def_primitive("numexpr", M_NUMEXPR);

    // Character parameters
    def_primitive("escapechar", M_ESCAPECHAR);
    def_primitive("endlinechar", M_ENDLINECHAR);
    def_primitive("newlinechar", M_NEWLINECHAR);

    // Token control
    def_primitive("ignorespaces", M_IGNORESPACES);

    // File I/O
    def_primitive("read", M_READ);
    def_primitive("write", M_WRITE);
    def_primitive("openin", M_OPENIN);
    def_primitive("closein", M_CLOSEIN);
    def_primitive("immediate", M_IMMEDIATE);
    def_primitive("ifeof", M_IFEOF);

    // e-TeX expansion
    def_primitive("scantokens", M_SCANTOKENS);
    def_primitive("expanded", M_EXPANDED);
    def_primitive("inputlineno", M_INPUTLINENO);

    // Output file I/O
    def_primitive("openout", M_OPENOUT);
    def_primitive("closeout", M_CLOSEOUT);
    def_primitive("readline", M_READLINE);

    // Date/time
    def_primitive("year", M_YEAR);
    def_primitive("month", M_MONTH);
    def_primitive("day", M_DAY);
    def_primitive("time", M_TIME);

    // Job control
    def_primitive("end", M_END);
    def_primitive("globaldefs", M_GLOBALDEFS);

    // e-TeX introspection
    def_primitive("currentgrouplevel", M_CURRENTGROUPLEVEL);
    def_primitive("currentgrouptype", M_CURRENTGROUPTYPE);
    def_primitive("currentiflevel", M_CURRENTIFLEVEL);
    def_primitive("currentiftype", M_CURRENTIFTYPE);
    def_primitive("currentifbranch", M_CURRENTIFBRANCH);

    // Tracing
    def_primitive("tracingmacros", M_TRACINGMACROS);
    def_primitive("tracingcommands", M_TRACINGCOMMANDS);

    // Define \bgroup and \egroup as implicit braces
    int bgroup_idx = intern("bgroup");
    eqtb[bgroup_idx].type = M_LBRACE;
    int egroup_idx = intern("egroup");
    eqtb[egroup_idx].type = M_RBRACE;
}

static void
init_datetime(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) { tex_year = 1970; tex_month = 1; tex_day = 1; tex_time = 0; return; }
    tex_year = t->tm_year + 1900;
    tex_month = t->tm_mon + 1;
    tex_day = t->tm_mday;
    tex_time = t->tm_hour * 60 + t->tm_min;
}

static void
init_system(void) {
    init_catcodes();
    init_case_codes();
    init_primitives();
    init_datetime();
}

// --- Input Management ---

static int
read_char(void) {
    if (ungot != EOF) {
        int c = ungot;
        ungot = EOF;
        return c;
    }

    if (!input || !input->is_file) { return EOF; }
    if (input->endinput) { return EOF; }

    int c = fgetc(input->file);
    if (c == '\n') {
        input->lineno++;
        if (charcode_valid(endlinechar)) { ungot = endlinechar; }
    }

    return c;
}

static void unread_char(int c) { ungot = c; }

static struct input_state *
find_file_input(void) {
    struct input_state *s = input;
    while (s && !s->is_file) { s = s->prev; }
    return s;
}

static int
get_input_lineno(void) {
    struct input_state *s = find_file_input();
    return s ? s->lineno : 0;
}

static struct input_state *
alloc_input_state(void) {
    struct input_state *s = malloc(sizeof(struct input_state));
    if (!s) { die("Out of memory"); }
    s->count = 0;
    s->pos = 0;
    s->prev = input;
    s->endinput = 0;
    return s;
}

static void
push_file(FILE *f) {
    struct input_state *s = alloc_input_state();
    s->tokens = NULL;
    s->is_file = 1;
    s->file = f;
    s->owns_tokens = 0;
    s->lineno = 1;
    input = s;
}

static void
push_tokens(struct token_list *list, int own) {
    if (list->len == 0) {
        if (own && list->tokens) { free(list->tokens); }
        return;
    }

    struct input_state *s = alloc_input_state();
    s->tokens = list->tokens;
    s->count = list->len;
    s->is_file = 0;
    s->owns_tokens = own;
    input = s;
}

static void
pop_input(void) {
    struct input_state *prev = input->prev;
    if (input->is_file && input->file != stdin) { fclose(input->file); }
    if (!input->is_file && input->owns_tokens && input->tokens) { free(input->tokens); }
    free(input);
    input = prev;
}

static int reading_from_tokens(void) { return input && !input->is_file; }

// --- Low-Level Tokenization ---

static struct token
scan_control_sequence(void) {
    char buf[MAX_CS_NAME];
    int len = 0;
    int c = read_char();

    buf[len++] = c;

    // Read letter name, then skip trailing space
    if (is_letter(c)) {
        while (1) {
            int next = read_char();
            if (!is_letter(next)) { unread_char(next); break; }
            if (len < MAX_CS_NAME - 1) { buf[len++] = next; }
        }

        while (1) {
            int next = read_char();
            if (!is_space(next)) { unread_char(next); break; }
        }
    }

    buf[len] = '\0';
    return make_cs_token(intern(buf));
}

static void
skip_comment(void) {
    while (1) {
        int c = read_char();
        if (c == EOF || is_eol(c)) { break; }
    }
}

static struct token
get_raw_token(void) {
    while (reading_from_tokens()) {
        if (input->pos < input->count) {
            return input->tokens[input->pos++];
        }
        pop_input();
    }

    if (!input) { return make_char_token(EOF); }

    int c = read_char();

    if (c == EOF) {
        if (input->prev) { pop_input(); return get_raw_token(); }
        return make_char_token(EOF);
    }

    if (is_escape(c)) { return scan_control_sequence(); }

    if (is_comment(c)) {
        skip_comment();
        return get_raw_token();
    }

    if (is_active(c)) {
        char buf[2] = {(char)c, '\0'};
        return make_cs_token(intern(buf));
    }

    return make_char_token(c);
}

// --- Meaning Lookup ---

static struct meaning
get_meaning(struct token t) {
    if (is_cs_token(t)) { return eqtb[t.val]; }

    if (is_char_token(t)) {
        if (is_begin_group(t.val)) {
            struct meaning m = {M_LBRACE, 0};
            return m;
        }
        if (is_end_group(t.val)) {
            struct meaning m = {M_RBRACE, 0};
            return m;
        }
    }

    struct meaning m = {M_CHAR, t.val};
    return m;
}

// --- Conditional Stack (for e-TeX introspection) ---

static void
cond_push(int type, int branch) {
    if (cond_ptr >= MAX_COND_STACK) { die("Conditional stack overflow"); }

    cond_type_stack[cond_ptr] = type;
    cond_branch_stack[cond_ptr] = branch;
    cond_ptr++;
}

static void
cond_pop(void) {
    if (cond_ptr > 0) { cond_ptr--; }
}

static void
cond_set_branch(int branch) {
    if (cond_ptr > 0) { cond_branch_stack[cond_ptr - 1] = branch; }
}

// --- Conditional Skipping ---

static void
skip_to_fi(void) {
    int depth = 0;
    while (1) {
        struct token t = get_raw_token();
        if (is_eof_token(t)) { die("Runaway conditional"); }
        struct meaning m = get_meaning(t);
        if (is_conditional(m)) { depth++; }
        if (is_fi(m)) {
            if (depth == 0) { return; }
            depth--;
        }
    }
}

static void
skip_to_else_or_fi(void) {
    int depth = 0;
    while (1) {
        struct token t = get_raw_token();
        if (is_eof_token(t)) { die("Runaway conditional"); }
        struct meaning m = get_meaning(t);
        if (is_conditional(m)) { depth++; }
        if (is_fi(m)) {
            if (depth == 0) { return; }
            depth--;
        }
        if ((is_else(m) || is_or(m)) && depth == 0) { return; }
    }
}

static void
skip_conditional(void) {
    skip_to_else_or_fi();
}

// Skip to \or N or \else or \fi for \ifcase
static void
skip_to_or_n(int n) {
    int depth = 0;
    int current = 0;

    while (1) {
        struct token t = get_raw_token();
        if (is_eof_token(t)) { die("Runaway conditional"); }
        struct meaning m = get_meaning(t);
        if (is_conditional(m)) { depth++; }
        if (is_fi(m)) {
            if (depth == 0) { return; }
            depth--;
        }
        if (depth == 0) {
            if (is_or(m)) {
                current++;
                if (current == n) { return; }
            }
            if (is_else(m)) { return; }
        }
    }
}

// --- Number Scanning ---

#define NO_QUANTITY  (INT_MIN + 1)  // sentinel — never a valid quantity
#define NO_CHAR_CODE 256  // Char code sentinel — not a character token


static int
char_code_of(struct token t) {
    if (is_char_token(t)) { return t.val; }
    return NO_CHAR_CODE;
}

static int
cat_code_of(struct token t) {
    if (is_char_token(t)) { return catcodes[t.val]; }
    return -1;
}

// -- Quantity Resolution --

static int
is_indexed_quantity(struct meaning m) {
    // Meanings that resolve by scanning an additional index
    return is_count(m) || is_catcode_cmd(m) || is_lccode_cmd(m) || is_uccode_cmd(m);
}

static int
resolve_parameter_int(struct meaning m) {
    if (is_chardef(m)) { return m.val; }
    if (is_escapechar(m)) { return escapechar; }
    if (is_endlinechar(m)) { return endlinechar; }
    if (is_newlinechar(m)) { return newlinechar; }
    if (is_inputlineno(m)) { return get_input_lineno(); }
    if (is_year(m)) { return tex_year; }
    if (is_month(m)) { return tex_month; }
    if (is_day(m)) { return tex_day; }
    if (is_time_cmd(m)) { return tex_time; }
    if (is_globaldefs(m)) { return globaldefs; }
    if (is_tracingmacros(m)) { return tracingmacros; }
    if (is_tracingcommands(m)) { return tracingcommands; }
    if (is_currentgrouplevel(m)) { return level; }
    if (is_currentgrouptype(m)) { return in_group() ? 1 : 0; }
    if (is_currentiflevel(m)) { return cond_ptr; }
    if (is_currentiftype(m)) { return in_conditional() ? cond_type_stack[cond_ptr - 1] : 0; }
    if (is_currentifbranch(m)) { return in_conditional() ? cond_branch_stack[cond_ptr - 1] : 0; }
    return NO_QUANTITY;
}

static int
resolve_indexed_int(struct meaning m, int idx) {
    if (is_count(m)) {
        if (!is_valid_register(idx)) { die("Count register index out of range"); }
        return count_regs[idx];
    }
    if (is_catcode_cmd(m)) {
        if (!charcode_valid(idx)) { die("Char code out of range"); }
        return catcodes[idx];
    }
    if (is_lccode_cmd(m)) {
        if (!charcode_valid(idx)) { die("Char code out of range"); }
        return lccode[idx];
    }
    if (is_uccode_cmd(m)) {
        if (!charcode_valid(idx)) { die("Char code out of range"); }
        return uccode[idx];
    }
    die("Not an indexed quantity");
    return 0;
}

// Forward declare for mutual recursion
static int scan_numexpr(void);

static struct token
consume_space_tokens(void) {
    struct token t;
    while (1) {
        t = get_token();
        if (!is_space_token(t)) { return t; }
    }
}

static int
parse_radix_int(int base, struct token first, int (*is_digit)(struct token), int (*value)(struct token)) {
    int val = value(first);
    while (1) {
        struct token next = get_token();
        if (!is_digit(next)) { push_single_token(next); break; }
        val = val * base + value(next);
    }
    return val;
}

static int
hex_value_of(struct token t) { return hex_value(t.val); }

static int
octal_digit_val(struct token t) { return t.val - '0'; }

static int
decimal_digit_val(struct token t) { return t.val - '0'; }

static int
scan_char_constant(void) {
    struct token next = get_raw_token();
    if (is_char_token(next)) { return next.val; }
    if (is_cs_token(next)) { return (unsigned char)names[next.val][0]; }
    die("Expected character after `");
    return 0;
}

static int
scan_int(void) {
    struct token t = consume_space_tokens();

    if (is_minus_token(t)) { return -scan_int(); }
    if (is_plus_token(t))  { return scan_int(); }

    struct meaning m = get_meaning(t);

    if (is_numexpr(m)) { return scan_numexpr(); }

    int param = resolve_parameter_int(m);
    if (param != NO_QUANTITY) { return param; }

    if (is_indexed_quantity(m)) {
        int idx = scan_int();
        return resolve_indexed_int(m, idx);
    }

    if (is_backtick_token(t)) { return scan_char_constant(); }

    if (is_hex_prefix_token(t))   { return parse_radix_int(16, get_token(), is_hex_digit_token, hex_value_of); }
    if (is_octal_prefix_token(t)) { return parse_radix_int(8, get_token(), is_octal_digit_token, octal_digit_val); }
    if (is_digit_token(t))        { return parse_radix_int(10, t, is_digit_token, decimal_digit_val); }

    die("Expected number");
    return 0;
}

// \numexpr implementation with proper operator precedence
// Precedence: * / higher than + -

static int
is_numexpr_end(struct token t) {
    return is_rparen_token(t) || is_relax(get_meaning(t));
}

static int numexpr_atom(void);
static int numexpr_term(void);
static int numexpr_expr(void);

static int
numexpr_atom(void) {
    struct token t = consume_space_tokens();

    if (is_lparen_token(t)) {
        int val = numexpr_expr();
        struct token close = consume_space_tokens();
        if (!is_rparen_token(close)) { die("Unmatched ( in \\numexpr"); }
        return val;
    }

    push_single_token(t);
    return scan_int();
}

static int
numexpr_term(void) {
    int result = numexpr_atom();

    while (1) {
        struct token t = consume_space_tokens();

        if (is_numexpr_end(t)) { push_single_token(t); break; }
        if (is_star_token(t))  { result *= numexpr_atom(); continue; }
        if (is_slash_token(t)) {
            int d = numexpr_atom();
            result = (d != 0) ? result / d : 0;
            continue;
        }

        push_single_token(t);
        break;
    }

    return result;
}

static int
numexpr_expr(void) {
    int result = numexpr_term();

    while (1) {
        struct token t = consume_space_tokens();

        if (is_numexpr_end(t)) { push_single_token(t); break; }
        if (is_plus_token(t))  { result += numexpr_term(); continue; }
        if (is_minus_token(t)) { result -= numexpr_term(); continue; }

        push_single_token(t);
        break;
    }

    return result;
}

static int
scan_numexpr(void) {
    int result = numexpr_expr();
    struct token t = consume_space_tokens();
    // consume terminating \relax or )
    if (!is_numexpr_end(t)) { push_single_token(t); }
    return result;
}

// --- Output Helpers ---

static void
push_number(int n) {
    char buf[MAX_INT_DIGITS];
    sprintf(buf, "%d", n);
    size_t len = strlen(buf);
    struct token *toks = alloc_tokens(len);
    for (size_t i = 0; i < len; i++) {
        toks[i] = make_char_token(buf[i]);
    }
    struct token_list list = {toks, len};
    push_tokens(&list, 1);
}

static void
push_roman(int n) {
    if (n <= 0) { return; }

    static const struct { int val; const char *str; } romans[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
        {100, "c"}, {90, "xc"}, {50, "l"}, {40, "xl"},
        {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}
    };

    // Use local buffer to avoid corrupting global build_buf
    token_array_t a = {NULL, 0, 0};

    enum { N_ROMANS = sizeof(romans) / sizeof(romans[0]) };

    for (int i = 0; n > 0 && i < N_ROMANS; i++) {
        while (n >= romans[i].val) {
            for (const char *p = romans[i].str; *p; p++) {
                token_array_append(&a, make_char_token(*p));
            }
            n -= romans[i].val;
        }
    }

    struct token_list list = token_array_finish(&a);
    push_tokens(&list, 1);
}

static void
push_string(const char *s) {
    size_t len = strlen(s);
    if (len == 0) { return; }
    struct token *toks = alloc_tokens(len);
    for (size_t i = 0; i < len; i++) {
        toks[i] = make_char_token(s[i]);
    }
    struct token_list list = {toks, len};
    push_tokens(&list, 1);
}

// --- Meaning to String ---

static const char *
meaning_to_string(struct meaning m) {
    static char buf[MAX_CS_NAME];

    switch (m.type) {
        case M_UNDEFINED: return "undefined";
        case M_CHAR: sprintf(buf, "the character %c", m.val); return buf;
        case M_CHARDEF: sprintf(buf, "\\char\"%02X", m.val); return buf;
        case M_MACRO: return "macro";
        case M_RELAX: return "\\relax";
        case M_DEF: return "\\def";
        case M_GDEF: return "\\gdef";
        case M_EDEF: return "\\edef";
        case M_XDEF: return "\\xdef";
        case M_LET: return "\\let";
        case M_COUNT: return "\\count";
        case M_TOKS: return "\\toks";
        case M_LBRACE: return "begin-group character {";
        case M_RBRACE: return "end-group character }";
        default: return "primitive";
    }
}

// --- Macro Expansion ---

static struct token_list
scan_braced_tokens(int expand) {
    token_array_t a = {NULL, 0, 0};
    int depth = 1;

    while (depth > 0) {
        struct token t = expand ? get_token() : get_raw_token();

        if (is_eof_token(t)) { free(a.tokens); die("Unmatched brace"); }

        if (is_char_token(t)) {
            if (is_begin_group(t.val)) { depth++; }
            if (is_end_group(t.val)) {
                depth--;
                if (depth == 0) { break; }
            }
        }
        token_array_append(&a, t);
    }

    return token_array_finish(&a);
}

// --- Macro Definition Helpers ---

static int
scan_parameter_pattern(struct macro *mac) {
    build_reset();
    int nparams = 0;

    while (1) {
        struct token t = get_raw_token();

        if (is_begin_group_token(t)) { break; }

        if (is_param_token(t)) {
            struct token p = get_raw_token();
            if (is_digit_token(p)) {
                nparams++;
                struct token match = {CMD_MATCH, nparams};
                build_append(match);
                continue;
            }
            build_append(p);
            continue;
        }

        build_append(t);
    }

    mac->pattern = build_finish();
    return nparams;
}

static void
process_body_params(struct token_list *body) {
    build_reset();

    for (size_t i = 0; i < body->len; i++) {
        struct token t = body->tokens[i];

        if (is_param_token(t) && i + 1 < body->len) {
            struct token p = body->tokens[++i];
            if (is_digit_token(p)) {
                struct token ref = {CMD_PARAM_REF, p.val - '0'};
                build_append(ref);
                continue;
            }
            build_append(p);
            continue;
        }
        build_append(t);
    }

    if (body->tokens) { free(body->tokens); }
    *body = build_finish();
}

static void
scan_macro_definition(int global, int expand_body) {
    struct token name = get_raw_token();
    if (!is_cs_token(name)) { die("\\def requires control sequence"); }

    if (nmacros >= MAX_NAMES) { die("Too many macros"); }
    int midx = nmacros++;
    struct macro *mac = &macros[midx];
    mac->is_long = long_flag;
    mac->is_outer = outer_flag;
    mac->is_protected = protected_flag;
    long_flag = outer_flag = protected_flag = 0;

    mac->nparams = scan_parameter_pattern(mac);
    mac->body = scan_braced_tokens(expand_body);

    if (!expand_body) { process_body_params(&mac->body); }

    struct meaning m = {M_MACRO, midx};
    set_meaning(name.val, m, global || global_flag);
    complete_assignment();
}

static const char *
find_macro_name(int midx) {
    for (size_t i = 0; i < nnames; i++) {
        if (eqtb[i].type == M_MACRO && eqtb[i].val == midx) {
            return names[i];
        }
    }
    return "?";
}

static void
trace_macro_expansion(int midx, struct token_list *args, struct macro *mac) {
    const char *name = find_macro_name(midx);
    fprintf(stderr, "\\%s", name);
    for (int i = 1; i <= mac->nparams; i++) {
        fprintf(stderr, " #%d<-", i);
        for (size_t j = 0; j < args[i].len; j++) {
            struct token t = args[i].tokens[j];
            if (is_cs_token(t)) {
                fprintf(stderr, "\\%s", names[t.val]);
            } else if (is_char_token(t)) {
                fputc(t.val, stderr);
            }
        }
    }
    fprintf(stderr, "\n");
}

// --- Macro Expansion Helpers ---

static struct token_list
collect_single_argument(void) {
    token_array_t a = {NULL, 0, 0};

    struct token t = get_raw_token();

    if (is_begin_group_token(t)) {
        int depth = 1;

        while (depth > 0) {
            struct token s = get_raw_token();
            if (is_eof_token(s)) { free(a.tokens); die("Unmatched brace in macro argument"); }
            if (is_char_token(s)) {
                if (is_begin_group(s.val)) { depth++; }
                if (is_end_group(s.val)) { depth--; }
            }
            if (depth > 0) { token_array_append(&a, s); }
        }
    } else {
        token_array_append(&a, t);
    }

    return token_array_finish(&a);
}

static void
collect_macro_arguments(struct macro *mac, struct token_list *args) {
    for (size_t i = 0; i < mac->pattern.len; i++) {
        struct token p = mac->pattern.tokens[i];

        if (is_match_token(p)) {
            struct token_list *arg = &args[p.val];

            struct token_list collected = collect_single_argument();
            arg->tokens = clone_tokens(collected.tokens, collected.len);
            arg->len = collected.len;
            free(collected.tokens);

            continue;
        }

        struct token t = get_raw_token();
        if (!tokens_equal(t, p)) { die("Macro pattern mismatch"); }
    }
}

static struct token_list
substitute_macro_body(struct macro *mac, struct token_list *args) {
    token_array_t result_a = {NULL, 0, 0};

    for (size_t i = 0; i < mac->body.len; i++) {
        struct token b = mac->body.tokens[i];

        if (is_param_ref(b)) {
            struct token_list *arg = &args[b.val];
            for (size_t j = 0; j < arg->len; j++) {
                token_array_append(&result_a, arg->tokens[j]);
            }
            continue;
        }

        token_array_append(&result_a, b);
    }

    return token_array_finish(&result_a);
}

static void
expand_macro(int midx) {
    struct macro *mac = &macros[midx];
    struct token_list args[10] = {0};

    collect_macro_arguments(mac, args);

    if (tracing_macros()) { trace_macro_expansion(midx, args, mac); }

    struct token_list result = substitute_macro_body(mac, args);

    for (int i = 1; i <= mac->nparams; i++) {
        if (args[i].tokens) { free(args[i].tokens); }
    }

    push_tokens(&result, 1);
}

// --- Conditional Handlers ---

static void
finalize_conditional(int cond_type, int result) {
    if (unless_flag) { result = !result; }
    unless_flag = 0;
    cond_push(cond_type, result ? 1 : -1);
    if (!result) { skip_conditional(); }
}

static void
do_if(void) {
    struct token a = get_token();
    struct token b = get_token();
    int result = (char_code_of(a) == char_code_of(b));
    finalize_conditional(COND_IF, result);
}

static int
evaluate_relation(int a, int b, int op) {
    if (op == '=') { return a == b; }
    if (op == '<') { return a < b; }
    if (op == '>') { return a > b; }
    die("Invalid relation in \\ifnum");
    return 0;
}

static void
do_ifx(void) {
    struct token a = get_raw_token();
    struct token b = get_raw_token();
    struct meaning ma = get_meaning(a);
    struct meaning mb = get_meaning(b);

    int result = meanings_equal(ma, mb);
    finalize_conditional(COND_IFX, result);
}

static void
do_ifnum(void) {
    int a = scan_int();
    struct token rel;

    while (1) { rel = get_token(); if (!is_space_token(rel)) break; }
    int b = scan_int();
    finalize_conditional(COND_IFNUM, evaluate_relation(a, b, rel.val));
}

static void
do_ifcat(void) {
    struct token a = get_token();
    struct token b = get_token();
    int result = (cat_code_of(a) == cat_code_of(b));
    finalize_conditional(COND_IFCAT, result);
}

static void
do_ifdefined(void) {
    struct token t = get_raw_token();
    struct meaning m = get_meaning(t);
    int result = !is_undefined(m);
    finalize_conditional(COND_IFDEFINED, result);
}

static void
build_csname(char *buf, int maxlen) {
    int len = 0;

    while (1) {
        struct token t = get_token();
        struct meaning m = get_meaning(t);
        if (is_endcsname(m)) { break; }
        if (!is_char_token(t)) { die("Non-character in control sequence name"); }
        if (is_space(t.val)) { continue; }
        if (len >= maxlen - 1) { die("Control sequence name too long"); }
        buf[len++] = t.val;
    }
    buf[len] = '\0';
}

static int
find_name_index(const char *name) {
    for (size_t i = 0; i < nnames; i++) {
        if (strcmp(names[i], name) == 0) { return (int)i; }
    }
    return -1;
}

static void
do_ifcsname(void) {
    char buf[MAX_CS_NAME];
    build_csname(buf, MAX_CS_NAME);

    int idx = find_name_index(buf);
    int result = (idx >= 0) && !is_undefined(eqtb[idx]);
    finalize_conditional(COND_IFCSNAME, result);
}

static void
do_ifcase(void) {
    int n = scan_int();
    cond_push(COND_IFCASE, 0);  // branch 0 for ifcase (or-selected)
    if (n < 0) { skip_to_fi(); cond_pop(); return; }
    if (n > 0) { skip_to_or_n(n); }
}

static void
do_iftrue(void) {
    int result = 1;
    finalize_conditional(COND_IFTRUE, result);
}

static void
do_iffalse(void) {
    int result = 0;
    finalize_conditional(COND_IFFALSE, result);
}

static void
do_ifodd(void) {
    int n = scan_int();
    int result = (n % 2 != 0);
    finalize_conditional(COND_IFODD, result);
}

// --- Expansion Primitives ---

static void
do_expandafter(void) {
    // Skip first token (raw), expand second, then push expanded first
    struct token first = get_raw_token();
    struct token second = get_token();
    push_two_tokens(second, first);
}

static void
do_number(void) {
    int n = scan_int();
    push_number(n);
}

static void
do_romannumeral(void) {
    int n = scan_int();
    push_roman(n);
}

static void
do_the(void) {
    struct token t = consume_space_tokens();
    struct meaning m = get_meaning(t);

    // \the\toks<N> push token list
    if (is_toks(m)) {
        int idx = scan_int();
        if (!is_valid_register(idx)) { die("Token register index out of range"); }
        if (toks_regs[idx].len > 0) {
            struct token_list copy;
            copy.tokens = clone_tokens(toks_regs[idx].tokens, toks_regs[idx].len);
            copy.len = toks_regs[idx].len;
            push_tokens(&copy, 1);
        }
        return;
    }

    // \the\count<N>, \the\catcode<C>, etc — push numeric value
    if (is_indexed_quantity(m)) {
        int idx = scan_int();
        push_number(resolve_indexed_int(m, idx));
        return;
    }

    // \the\numexpr <expr> — evaluate expression and push result
    if (is_numexpr(m)) {
        push_number(scan_numexpr());
        return;
    }

    int param = resolve_parameter_int(m);
    if (param != NO_QUANTITY) {
        push_number(param);
        return;
    }

    die("\\the requires register");
}

static void
do_string(void) {
    struct token t = get_raw_token();

    if (is_cs_token(t)) {
        const char *name = names[t.val];
        size_t namelen = strlen(name);
        int has_escape = (escapechar_enabled());
        size_t len = namelen + (has_escape ? 1 : 0);
        struct token *toks = alloc_tokens(len);
        size_t j = 0;
        if (has_escape) { toks[j++] = make_char_token(escapechar); }
        for (size_t i = 0; i < namelen; i++) {
            toks[j++] = make_char_token(name[i]);
        }
        struct token_list list = {toks, len};
        push_tokens(&list, 1);
        return;
    }

    push_single_token(t);
}

static void
do_meaning(void) {
    struct token t = get_raw_token();
    struct meaning m = get_meaning(t);
    push_string(meaning_to_string(m));
}

static void
do_csname(void) {
    char buf[MAX_CS_NAME];
    build_csname(buf, MAX_CS_NAME);
    int idx = intern(buf);

    if (is_undefined(eqtb[idx])) {
        eqtb[idx].type = M_RELAX;
    }
    push_single_token(make_cs_token(idx));
}

static void
do_case_map(int *mapping, int (*has_map)(int), const char *errmsg) {
    struct token t = get_raw_token();
    if (!is_begin_group_token(t)) { die(errmsg); }

    struct token_list body = scan_braced_tokens(0);

    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i]) && has_map(body.tokens[i].val)) {
            body.tokens[i].val = mapping[body.tokens[i].val];
        }
    }

    push_tokens(&body, 1);
}

static void
do_uppercase(void) {
    do_case_map(uccode, has_uccode, "\\uppercase expects {");
}

static void
do_lowercase(void) {
    do_case_map(lccode, has_lccode, "\\lowercase expects {");
}

static void
do_detokenize(void) {
    struct token t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\detokenize expects {");

    struct token_list body = scan_braced_tokens(0);

    // Convert tokens to detokenized characters

    token_array_t a = {NULL, 0, 0};

    for (size_t i = 0; i < body.len; i++) {
        struct token tok = body.tokens[i];
        if (is_cs_token(tok)) {
            token_array_append(&a, make_char_token('\\'));
            const char *name = names[tok.val];
            for (size_t j = 0; name[j]; j++) {
                token_array_append(&a, make_char_token(name[j]));
            }
            token_array_append(&a, make_char_token(' '));
        } else {
            token_array_append(&a, tok);
        }
    }

    if (body.tokens) { free(body.tokens); }

    struct token_list result = token_array_finish(&a);
    push_tokens(&result, 1);
}

static void
do_jobname(void) {
    push_string(jobname);
}

static void
do_noexpand(void) {
    noexpand_next = 1;
}

static void
do_unless(void) {
    unless_flag = 1;
}

// --- New Primitives ---

static void
do_ignorespaces(void) {
    while (1) {
        struct token t = get_token();
        if (!is_space_token(t)) {
            push_single_token(t);
            return;
        }
    }
}

static void
do_ifeof(void) {
    int n = scan_int();
    int result = 1;
    if (stream_valid(n)) {
        result = read_stream_eof(n);
    }
    finalize_conditional(COND_IFEOF, result);
}

static void
read_filename(char *filename, int maxlen) {
    int len = 0;

    struct token t = get_raw_token();
    while (is_space_token(t)) { t = get_raw_token(); }

    while (!is_eof_token(t) && is_char_token(t)
        && t.val != '\n' && t.val != '\r' && !is_space_token(t)) {
        if (len >= maxlen - 1) { die("Filename too long"); }
        filename[len++] = t.val;
        t = get_raw_token();
    }

    push_single_token(t);
    filename[len] = '\0';
}

static void
do_openin(void) {
    int n = scan_int();
    skip_optional_equals();

    char filename[MAX_FILENAME];
    read_filename(filename, MAX_FILENAME);

    if (stream_valid(n)) {
        if (read_stream_open(n)) { fclose(read_files[n]); }
        read_files[n] = fopen(filename, "r");
        read_eof[n] = !read_stream_open(n);
    }
}

static void
do_closein(void) {
    int n = scan_int();
    if (read_stream_open(n)) {
        fclose(read_files[n]);
        read_files[n] = NULL;
        read_eof[n] = 1;
    }
}

// --- Stream I/O Helpers ---

static FILE *
stream_for_read(int n) {
    if (is_terminal_stream(n)) { return stdin; }
    if (stream_valid(n)) { return read_files[n]; }
    return NULL;
}

static void
read_line_balanced(token_array_t *a, FILE *f, int *eof_reached) {
    int depth = 0;
    int c;

    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            if (depth == 0) { break; }
            token_array_append(a, make_char_token(' '));
            continue;
        }
        if (c == '{') { depth++; }
        if (c == '}') { depth--; }
        token_array_append(a, make_char_token(c));
    }

    if (eof_reached) *eof_reached = (c == EOF);
}

static void
read_line_raw(token_array_t *a, FILE *f, int *eof_reached) {
    int c;

    while ((c = fgetc(f)) != EOF && c != '\n') {
        token_array_append(a, make_char_token(c));
    }

    if (eof_reached) *eof_reached = (c == EOF);
}

static void
read_line_into_array(token_array_t *a, int n, int tex_mode) {
    FILE *f = stream_for_read(n);
    if (!f) { return; }

    int eof_reached = 0;

    if (tex_mode) {
        read_line_balanced(a, f, &eof_reached);
    } else {
        read_line_raw(a, f, &eof_reached);
    }

    if (eof_reached && stream_valid(n)) { read_eof[n] = 1; }
}

static void
define_macro_from_array(struct token cs, token_array_t *a, int global) {
    if (nmacros >= MAX_NAMES) { die("Too many macros"); }
    int midx = nmacros++;
    struct macro *mac = &macros[midx];
    mac->nparams = 0;
    mac->pattern.tokens = NULL;
    mac->pattern.len = 0;
    mac->body = token_array_finish(a);
    mac->is_long = 0;
    mac->is_outer = 0;
    mac->is_protected = 0;

    struct meaning m = {M_MACRO, midx};
    set_meaning(cs.val, m, global);
    complete_assignment();
}

static void
read_line_to_macro(int tex_mode, const char *errmsg) {
    int n = scan_int();
    skip_keyword_to();
    skip_spaces_raw();
    struct token t = get_raw_token();
    if (!is_cs_token(t)) { die(errmsg); }

    token_array_t a = {NULL, 0, 0};
    read_line_into_array(&a, n, tex_mode);

    define_macro_from_array(t, &a, global_flag);
}

static void
do_read(void) {
    read_line_to_macro(1, "\\read requires control sequence");
}

static void
do_readline(void) {
    read_line_to_macro(0, "\\readline requires control sequence");
}

static void
write_tokens_to_stream(struct token_list *body, FILE *out) {
    for (size_t i = 0; i < body->len; i++) {
        struct token tok = body->tokens[i];

        if (is_char_token(tok)) {
            fputc(matches_newlinechar(tok.val) ? '\n' : tok.val, out);
        } else if (is_cs_token(tok)) {
            if (escapechar_enabled()) { fputc(escapechar, out); }
            fprintf(out, "%s ", names[tok.val]);
        }
    }

    fputc('\n', out);
}

static FILE *
write_stream_for(int n) {
    if (write_stream_open(n)) { return write_files[n]; }
    if (is_terminal_stream(n)) { return stderr; }
    return stdout;
}

static void
do_write(void) {
    int n = scan_int();

    struct token t = get_raw_token();
    while (is_space_token(t)) { t = get_raw_token(); }
    if (!is_begin_group_token(t)) die("\\write expects {");

    struct token_list body = scan_braced_tokens(1);

    write_tokens_to_stream(&body, write_stream_for(n));

    if (body.tokens) { free(body.tokens); }
}

static void
do_inputlineno(void) {
    push_number(get_input_lineno());
}

static char *
token_list_to_string(struct token_list *body, size_t *out_len) {
    token_array_t a = {NULL, 0, 0};

    // Build token array from input body

    for (size_t i = 0; i < body->len; i++) {
        struct token tok = body->tokens[i];

        if (is_char_token(tok)) {
            token_array_append(&a, tok);
        } else if (is_cs_token(tok)) {
            if (escapechar_enabled()) {
                struct token esc = make_char_token(escapechar);
                token_array_append(&a, esc);
            }
            const char *name = names[tok.val];
            for (size_t j = 0; name[j]; j++) {
                struct token ch = make_char_token(name[j]);
                token_array_append(&a, ch);
            }
            struct token sp = make_char_token(' ');
            token_array_append(&a, sp);
        }
    }

    // Convert token array to string
    struct token_list result = token_array_finish(&a);
    char *str = alloc_str(result.len + 1);

    for (size_t i = 0; i < result.len; i++) {
        str[i] = result.tokens[i].val;
    }
    str[result.len] = '\0';

    if (result.tokens) { free(result.tokens); }

    *out_len = result.len;
    return str;
}

static void
do_scantokens(void) {
    struct token t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\scantokens expects {");

    struct token_list body = scan_braced_tokens(0);

    size_t slen;
    char *str = token_list_to_string(&body, &slen);

    if (body.tokens) { free(body.tokens); }

    FILE *f = tmpfile();
    if (!f) { die("Cannot create temporary file for \\scantokens"); }
    fwrite(str, 1, slen, f);
    rewind(f);
    push_file(f);
    free(str);
}

static void
do_expanded(void) {
    struct token t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\expanded expects {");

    struct token_list body = scan_braced_tokens(1);  // expand
    push_tokens(&body, 1);
}

static void
do_openout(void) {
    int n = scan_int();
    skip_optional_equals();

    char filename[MAX_FILENAME];
    read_filename(filename, MAX_FILENAME);

    if (stream_valid(n)) {
        if (write_stream_open(n)) { fclose(write_files[n]); }
        write_files[n] = fopen(filename, "w");
        if (!write_files[n]) { die("Cannot open file for writing"); }
    }
}

static void
do_closeout(void) {
    int n = scan_int();
    if (write_stream_open(n)) {
        fclose(write_files[n]);
        write_files[n] = NULL;
    }
}

static void
do_globaldefs_assignment(void) {
    skip_optional_equals();
    globaldefs = scan_int();
    complete_assignment();
}

static void
do_tracingmacros_assignment(void) {
    skip_optional_equals();
    tracingmacros = scan_int();
    complete_assignment();
}

static void
do_tracingcommands_assignment(void) {
    skip_optional_equals();
    tracingcommands = scan_int();
    complete_assignment();
}

static void
do_year_assignment(void) {
    skip_optional_equals();
    tex_year = scan_int();
    complete_assignment();
}

static void
do_month_assignment(void) {
    skip_optional_equals();
    tex_month = scan_int();
    complete_assignment();
}

static void
do_day_assignment(void) {
    skip_optional_equals();
    tex_day = scan_int();
    complete_assignment();
}

static void
do_time_assignment(void) {
    skip_optional_equals();
    tex_time = scan_int();
    complete_assignment();
}

static void
do_end(void) {
    // \end terminates processing — handled by break in process()
}

// --- Dispatch table for get_token() ---

typedef void (*expand_fn)(struct meaning);

static void ef_unless(struct meaning m)      { do_unless(); (void)m; }
static void ef_if(struct meaning m)          { do_if(); (void)m; }
static void ef_ifx(struct meaning m)         { do_ifx(); (void)m; }
static void ef_ifnum(struct meaning m)       { do_ifnum(); (void)m; }
static void ef_ifcat(struct meaning m)       { do_ifcat(); (void)m; }
static void ef_ifdefined(struct meaning m)   { do_ifdefined(); (void)m; }
static void ef_ifcsname(struct meaning m)    { do_ifcsname(); (void)m; }
static void ef_ifcase(struct meaning m)      { do_ifcase(); (void)m; }
static void ef_iftrue(struct meaning m)      { do_iftrue(); (void)m; }
static void ef_iffalse(struct meaning m)     { do_iffalse(); (void)m; }
static void ef_ifodd(struct meaning m)       { do_ifodd(); (void)m; }
static void ef_ifeof(struct meaning m)       { do_ifeof(); (void)m; }
static void ef_or(struct meaning m)          { skip_to_fi(); cond_pop(); (void)m; }
static void ef_else(struct meaning m)        { cond_set_branch(-1); skip_to_fi(); cond_pop(); (void)m; }
static void ef_fi(struct meaning m)          { cond_pop(); (void)m; }
static void ef_number(struct meaning m)      { do_number(); (void)m; }
static void ef_romannumeral(struct meaning m){ do_romannumeral(); (void)m; }
static void ef_the(struct meaning m)         { do_the(); (void)m; }
static void ef_string(struct meaning m)      { do_string(); (void)m; }
static void ef_meaning(struct meaning m)     { do_meaning(); (void)m; }
static void ef_csname(struct meaning m)      { do_csname(); (void)m; }
static void ef_expandafter(struct meaning m) { do_expandafter(); (void)m; }
static void ef_noexpand(struct meaning m)    { do_noexpand(); (void)m; }
static void ef_uppercase(struct meaning m)   { do_uppercase(); (void)m; }
static void ef_lowercase(struct meaning m)   { do_lowercase(); (void)m; }
static void ef_detokenize(struct meaning m)  { do_detokenize(); (void)m; }
static void ef_jobname(struct meaning m)     { do_jobname(); (void)m; }
static void ef_inputlineno(struct meaning m) { do_inputlineno(); (void)m; }
static void ef_scantokens(struct meaning m)  { do_scantokens(); (void)m; }
static void ef_expanded(struct meaning m)    { do_expanded(); (void)m; }

static const expand_fn expand_dispatch[M_TYPE_COUNT] = {
    [M_UNLESS]       = ef_unless,
    [M_IF]           = ef_if,
    [M_IFX]          = ef_ifx,
    [M_IFNUM]        = ef_ifnum,
    [M_IFCAT]        = ef_ifcat,
    [M_IFDEFINED]    = ef_ifdefined,
    [M_IFCSNAME]     = ef_ifcsname,
    [M_IFCASE]       = ef_ifcase,
    [M_IFTRUE]       = ef_iftrue,
    [M_IFFALSE]      = ef_iffalse,
    [M_IFODD]        = ef_ifodd,
    [M_IFEOF]        = ef_ifeof,
    [M_OR]           = ef_or,
    [M_ELSE]         = ef_else,
    [M_FI]           = ef_fi,
    [M_NUMBER]       = ef_number,
    [M_ROMANNUMERAL] = ef_romannumeral,
    [M_THE]          = ef_the,
    [M_STRING]       = ef_string,
    [M_MEANING]      = ef_meaning,
    [M_CSNAME]       = ef_csname,
    [M_EXP_AFTER]    = ef_expandafter,
    [M_NOEXPAND]     = ef_noexpand,
    [M_UPPERCASE]    = ef_uppercase,
    [M_LOWERCASE]    = ef_lowercase,
    [M_DETOKENIZE]   = ef_detokenize,
    [M_JOBNAME]      = ef_jobname,
    [M_INPUTLINENO]  = ef_inputlineno,
    [M_SCANTOKENS]   = ef_scantokens,
    [M_EXPANDED]     = ef_expanded,
};

// SIDEFF: pushes unexpanded body, then gets next token
static struct token
expand_unexpanded(void) {
    struct token brace = get_raw_token();
    if (!is_begin_group_token(brace)) die("\\unexpanded expects {");
    struct token_list body = scan_braced_tokens(0);
    push_tokens(&body, 1);
    return get_token();
}

// --- Main Token Getter (with expansion) ---
// SIDEFF: pushes expanded token sequences onto input stack, recurses

static struct token
get_token(void) {
    struct token t = get_raw_token();

    if (is_eof_token(t)) { return t; }

    // noexpand_next bypasses all expansion for the next token
    if (noexpand_next) { noexpand_next = 0; return t; }

    struct meaning m = get_meaning(t);

    // Protected macros don't expand in expansion-only contexts
    if (is_macro(m)) {
        struct macro *mac = &macros[m.val];
        if (!mac->is_protected) {
            expand_macro(m.val);
            return get_token();
        }
    }

    // Dispatch table for expandable commands
    if (is_valid_type(m)) {
        expand_fn fn = expand_dispatch[m.type];
        if (fn) {
            fn(m);
            return get_token();
        }
    }

    // \unexpanded returns its argument without expansion
    if (is_unexpanded(m)) { return expand_unexpanded(); }

    return t;
}

// --- Skip Helpers ---

static void
skip_spaces_raw(void) {
    while (1) {
        struct token t = get_raw_token();
        if (!is_space_token(t)) {
            push_single_token(t);
            return;
        }
    }
}

static void
skip_optional_equals(void) {
    skip_spaces_raw();
    struct token t = get_raw_token();
    if (is_char_token(t) && t.val == '=') { return; }
    push_single_token(t);
}

static void
skip_optional_by(void) {
    skip_spaces_raw();
    struct token t = get_raw_token();

    if (is_char_token(t) && (t.val == 'b' || t.val == 'B')) {
        struct token y = get_raw_token();
        if (is_char_token(y) && (y.val == 'y' || y.val == 'Y')) { return; }
        push_single_token(y);
    }

    push_single_token(t);
}

static void
skip_keyword_to(void) {
    skip_spaces_raw();
    struct token t = get_raw_token();

    if (is_char_token(t) && (t.val == 't' || t.val == 'T')) {
        struct token o = get_raw_token();
        if (is_char_token(o) && (o.val == 'o' || o.val == 'O')) { return; }
        push_single_token(o);
    }

    push_single_token(t);
}

// --- Command Handlers ---

static void
do_let(void) {
    struct token dest = get_raw_token();
    skip_optional_equals();
    struct token src = get_raw_token();
    set_meaning(dest.val, get_meaning(src), global_flag);
    complete_assignment();
}

static void
do_futurelet(void) {
    struct token cs = get_raw_token();
    struct token first = get_raw_token();
    struct token second = get_raw_token();
    set_meaning(cs.val, get_meaning(second), global_flag);
    complete_assignment();
    push_two_tokens(first, second);
}

static void
do_chardef(void) {
    struct token cs = get_raw_token();
    skip_optional_equals();
    int val = scan_int();
    struct meaning m = {M_CHARDEF, val};
    set_meaning(cs.val, m, global_flag);
    complete_assignment();
}

static void
do_countdef(void) {
    struct token cs = get_raw_token();
    skip_optional_equals();
    int idx = scan_int();
    struct meaning m = {M_COUNT, idx};
    set_meaning(cs.val, m, global_flag);
    complete_assignment();
}

static void
do_toksdef(void) {
    struct token cs = get_raw_token();
    skip_optional_equals();
    int idx = scan_int();
    struct meaning m = {M_TOKS, idx};
    set_meaning(cs.val, m, global_flag);
    complete_assignment();
}

static void
do_count_assignment(void) {
    int idx = scan_int();
    skip_optional_equals();
    int val = scan_int();
    set_count(idx, val, global_flag);
    complete_assignment();
}

static void
do_toks_assignment(void) {
    int idx = scan_int();
    skip_optional_equals();

    struct token t = get_raw_token();
    while (is_space_token(t)) { t = get_raw_token(); }

    // Braced assignment or toks-to-toks copy

    if (is_begin_group_token(t)) {
        struct token_list body = scan_braced_tokens(0);
        set_toks(idx, body, global_flag);
    } else {
        // \toks0=\toks1 style assignment
        struct meaning m = get_meaning(t);
        if (is_toks(m)) {
            int src_idx = scan_int();
            struct token_list copy;
            copy.tokens = clone_tokens(toks_regs[src_idx].tokens, toks_regs[src_idx].len);
            copy.len = toks_regs[src_idx].len;
            set_toks(idx, copy, global_flag);
        } else {
            die("Expected { or \\toks after \\toks N=");
        }
    }

    complete_assignment();
}

static void
do_catcode_assignment(void) {
    int c = scan_int();
    skip_optional_equals();
    int cat = scan_int();
    if (charcode_valid(c) && catcode_valid(cat)) { set_catcode(c, cat, global_flag); }
    complete_assignment();
}

static void
do_lccode_assignment(void) {
    int c = scan_int();
    skip_optional_equals();
    int val = scan_int();
    if (charcode_valid(c)) { set_lccode_val(c, val, global_flag); }
    complete_assignment();
}

static void
do_uccode_assignment(void) {
    int c = scan_int();
    skip_optional_equals();
    int val = scan_int();
    if (charcode_valid(c)) { set_uccode_val(c, val, global_flag); }
    complete_assignment();
}

static void
do_escapechar_assignment(void) {
    skip_optional_equals();
    escapechar = scan_int();
    complete_assignment();
}

static void
do_endlinechar_assignment(void) {
    skip_optional_equals();
    endlinechar = scan_int();
    complete_assignment();
}

static void
do_newlinechar_assignment(void) {
    skip_optional_equals();
    newlinechar = scan_int();
    complete_assignment();
}

static void
get_count_register(int *idx) {
    struct token t = get_raw_token();
    while (is_space_token(t)) { t = get_raw_token(); }

    struct meaning m = get_meaning(t);
    if (!is_count(m)) { die("Expected \\count"); }

    if (is_count_primitive(t)) {
        *idx = scan_int();
    } else {
        *idx = m.val;
    }
}

static void
do_advance(void) {
    int idx;
    get_count_register(&idx);
    skip_optional_by();
    int val = scan_int();

    set_count(idx, count_regs[idx] + val, global_flag);
    complete_assignment();
}

static void
do_multiply(void) {
    int idx;
    get_count_register(&idx);
    skip_optional_by();
    int val = scan_int();

    set_count(idx, count_regs[idx] * val, global_flag);
    complete_assignment();
}

static void
do_divide(void) {
    int idx;
    get_count_register(&idx);
    skip_optional_by();
    int val = scan_int();

    if (val == 0) { complete_assignment(); return; }
    set_count(idx, count_regs[idx] / val, global_flag);
    complete_assignment();
}

static void
do_input(void) {
    char filename[MAX_FILENAME];
    read_filename(filename, MAX_FILENAME);

    FILE *f = fopen(filename, "r");

    if (!f) {
        size_t flen = strlen(filename);
        if (flen + 5 <= MAX_FILENAME) {
            strcat(filename, ".tex");
            f = fopen(filename, "r");
        }
    }

    if (f) { push_file(f); }
}

static void
do_endinput(void) {
    struct input_state *s = find_file_input();
    if (s) { s->endinput = 1; }
}

static void
do_aftergroup(void) {
    struct token t = get_raw_token();
    if (aftergroup_ptr >= MAX_AFTERGROUP) { die("Aftergroup stack full"); }
    aftergroup_stack[aftergroup_ptr] = t;
    aftergroup_level[aftergroup_ptr] = level;
    aftergroup_ptr++;
}

static void
do_afterassignment(void) {
    afterassignment_token = get_raw_token();
    afterassignment_pending = 1;
}

static void
do_message(void) {
    struct token t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\message expects {");
    struct token_list body = scan_braced_tokens(1);  // expand

    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i])) {
            putc(body.tokens[i].val, stderr);
        } else if (is_cs_token(body.tokens[i])) {
            fprintf(stderr, "\\%s ", names[body.tokens[i].val]);
        }
    }
    fprintf(stderr, "\n");

    if (body.tokens) { free(body.tokens); }
}

static void
do_errmessage(void) {
    struct token t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\errmessage expects {");
    struct token_list body = scan_braced_tokens(1);

    fprintf(stderr, "! ");
    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i])) {
            putc(body.tokens[i].val, stderr);
        }
    }
    fprintf(stderr, "\n");

    if (body.tokens) { free(body.tokens); }
}

static void
do_show(void) {
    struct token t = get_raw_token();
    struct meaning m = get_meaning(t);

    if (is_cs_token(t)) {
        fprintf(stderr, "> \\%s=%s.\n", names[t.val], meaning_to_string(m));
    } else {
        fprintf(stderr, "> %s.\n", meaning_to_string(m));
    }
}

static void
do_showthe(void) {
    struct token t = consume_space_tokens();
    struct meaning m = get_meaning(t);

    if (is_indexed_quantity(m)) {
        int idx = scan_int();
        fprintf(stderr, "> %d.\n", resolve_indexed_int(m, idx));
        return;
    }

    if (is_toks(m)) {
        int idx = scan_int();
        if (idx >= 0 && idx < MAX_REGISTERS) {
            for (size_t i = 0; i < toks_regs[idx].len; i++) {
                struct token tok = toks_regs[idx].tokens[i];
                if (is_cs_token(tok)) {
                    fprintf(stderr, "\\%s ", names[tok.val]);
                } else if (is_char_token(tok)) {
                    fputc(tok.val, stderr);
                }
            }
        }
        fprintf(stderr, ".\n");
        return;
    }

    if (is_numexpr(m)) {
        fprintf(stderr, "> %d.\n", scan_numexpr());
        return;
    }

    int param = resolve_parameter_int(m);
    if (param != NO_QUANTITY) {
        fprintf(stderr, "> %d.\n", param);
        return;
    }

    fprintf(stderr, "> (unknown).\n");
}

// --- Dispatch table for process() ---

typedef void (*process_fn)(struct meaning);

static void ph_global(struct meaning m)      { global_flag = 1; (void)m; }
static void ph_long(struct meaning m)        { long_flag = 1; (void)m; }
static void ph_outer(struct meaning m)       { outer_flag = 1; (void)m; }
static void ph_protected(struct meaning m)   { protected_flag = 1; (void)m; }
static void ph_immediate(struct meaning m)   { (void)m; /* no-op */ }
static void ph_def(struct meaning m)         { scan_macro_definition(0, 0); (void)m; }
static void ph_gdef(struct meaning m)        { scan_macro_definition(1, 0); (void)m; }
static void ph_edef(struct meaning m)        { scan_macro_definition(0, 1); (void)m; }
static void ph_xdef(struct meaning m)        { scan_macro_definition(1, 1); (void)m; }
static void ph_let(struct meaning m)         { do_let(); (void)m; }
static void ph_futurelet(struct meaning m)   { do_futurelet(); (void)m; }
static void ph_chardef_cmd(struct meaning m) { do_chardef(); (void)m; }
static void ph_countdef(struct meaning m)    { do_countdef(); (void)m; }
static void ph_toksdef(struct meaning m)     { do_toksdef(); (void)m; }
static void ph_count(struct meaning m)       { do_count_assignment(); (void)m; }
static void ph_toks(struct meaning m)        { do_toks_assignment(); (void)m; }
static void ph_catcode(struct meaning m)     { do_catcode_assignment(); (void)m; }
static void ph_lccode(struct meaning m)      { do_lccode_assignment(); (void)m; }
static void ph_uccode(struct meaning m)      { do_uccode_assignment(); (void)m; }
static void ph_advance(struct meaning m)     { do_advance(); (void)m; }
static void ph_multiply(struct meaning m)    { do_multiply(); (void)m; }
static void ph_divide(struct meaning m)      { do_divide(); (void)m; }
static void ph_input(struct meaning m)       { do_input(); (void)m; }
static void ph_endinput(struct meaning m)    { do_endinput(); (void)m; }
static void ph_escapechar(struct meaning m)  { do_escapechar_assignment(); (void)m; }
static void ph_endlinechar(struct meaning m) { do_endlinechar_assignment(); (void)m; }
static void ph_newlinechar(struct meaning m) { do_newlinechar_assignment(); (void)m; }
static void ph_openin(struct meaning m)      { do_openin(); (void)m; }
static void ph_closein(struct meaning m)     { do_closein(); (void)m; }
static void ph_read(struct meaning m)        { do_read(); (void)m; }
static void ph_write(struct meaning m)       { do_write(); (void)m; }
static void ph_openout(struct meaning m)     { do_openout(); (void)m; }
static void ph_closeout(struct meaning m)    { do_closeout(); (void)m; }
static void ph_readline(struct meaning m)    { do_readline(); (void)m; }
static void ph_ignorespaces(struct meaning m){ do_ignorespaces(); (void)m; }
static void ph_globaldefs(struct meaning m)  { do_globaldefs_assignment(); (void)m; }
static void ph_tracingmacros(struct meaning m)   { do_tracingmacros_assignment(); (void)m; }
static void ph_tracingcommands(struct meaning m) { do_tracingcommands_assignment(); (void)m; }
static void ph_year(struct meaning m)        { do_year_assignment(); (void)m; }
static void ph_month(struct meaning m)       { do_month_assignment(); (void)m; }
static void ph_day(struct meaning m)         { do_day_assignment(); (void)m; }
static void ph_time(struct meaning m)        { do_time_assignment(); (void)m; }
static void ph_end(struct meaning m)         { do_end(); (void)m; }
static void ph_aftergroup(struct meaning m)  { do_aftergroup(); (void)m; }
static void ph_afterassignment(struct meaning m){ do_afterassignment(); (void)m; }
static void ph_message(struct meaning m)     { do_message(); (void)m; }
static void ph_errmessage(struct meaning m)  { do_errmessage(); (void)m; }
static void ph_show(struct meaning m)        { do_show(); (void)m; }
static void ph_showthe(struct meaning m)     { do_showthe(); (void)m; }
static void ph_lbrace(struct meaning m)      { level++; (void)m; }
static void ph_rbrace(struct meaning m)      { if (level > 0) restore_scope(); (void)m; }
static void ph_begingroup(struct meaning m)  { level++; (void)m; }
static void ph_endgroup(struct meaning m)    { if (level > 0) restore_scope(); (void)m; }
static void ph_relax(struct meaning m)       { (void)m; }
static void ph_chardef(struct meaning m)     { putchar(m.val); }
static void ph_macro(struct meaning m)       { expand_macro(m.val); }

static const process_fn process_dispatch[M_TYPE_COUNT] = {
    [M_GLOBAL]        = ph_global,
    [M_LONG]          = ph_long,
    [M_OUTER]         = ph_outer,
    [M_PROTECTED]     = ph_protected,
    [M_IMMEDIATE]     = ph_immediate,
    [M_DEF]           = ph_def,
    [M_GDEF]          = ph_gdef,
    [M_EDEF]          = ph_edef,
    [M_XDEF]          = ph_xdef,
    [M_LET]           = ph_let,
    [M_FUTURELET]     = ph_futurelet,
    [M_CHARDEF_CMD]   = ph_chardef_cmd,
    [M_COUNTDEF]      = ph_countdef,
    [M_TOKSDEF]       = ph_toksdef,
    [M_COUNT]         = ph_count,
    [M_TOKS]          = ph_toks,
    [M_CATCODE]       = ph_catcode,
    [M_LCCODE]        = ph_lccode,
    [M_UCCODE]        = ph_uccode,
    [M_ADVANCE]       = ph_advance,
    [M_MULTIPLY]      = ph_multiply,
    [M_DIVIDE]        = ph_divide,
    [M_INPUT]         = ph_input,
    [M_ENDINPUT]      = ph_endinput,
    [M_ESCAPECHAR]    = ph_escapechar,
    [M_ENDLINECHAR]   = ph_endlinechar,
    [M_NEWLINECHAR]   = ph_newlinechar,
    [M_OPENIN]        = ph_openin,
    [M_CLOSEIN]       = ph_closein,
    [M_READ]          = ph_read,
    [M_WRITE]         = ph_write,
    [M_OPENOUT]       = ph_openout,
    [M_CLOSEOUT]      = ph_closeout,
    [M_READLINE]      = ph_readline,
    [M_IGNORESPACES]  = ph_ignorespaces,
    [M_GLOBALDEFS]    = ph_globaldefs,
    [M_TRACINGMACROS] = ph_tracingmacros,
    [M_TRACINGCOMMANDS]= ph_tracingcommands,
    [M_YEAR]          = ph_year,
    [M_MONTH]         = ph_month,
    [M_DAY]           = ph_day,
    [M_TIME]          = ph_time,
    [M_END]           = ph_end,
    [M_AFTERGROUP]    = ph_aftergroup,
    [M_AFTERASSIGNMENT]= ph_afterassignment,
    [M_MESSAGE]       = ph_message,
    [M_ERRMESSAGE]    = ph_errmessage,
    [M_SHOW]          = ph_show,
    [M_SHOWTHE]       = ph_showthe,
    [M_LBRACE]        = ph_lbrace,
    [M_RBRACE]        = ph_rbrace,
    [M_BEGINGROUP]    = ph_begingroup,
    [M_ENDGROUP]      = ph_endgroup,
    [M_RELAX]         = ph_relax,
    [M_CHARDEF]       = ph_chardef,
    [M_MACRO]         = ph_macro,
};

// --- Main Processing Loop ---

static void
process(void) {
    while (1) {
        struct token t = get_token();
        if (is_eof_token(t)) { break; }
        struct meaning m = get_meaning(t);

        if (is_valid_type(m)) {
            process_fn fn = process_dispatch[m.type];
            if (fn) {
                fn(m);
                if (m.type == M_END) { break; }
                continue;
            }
        }

        if (is_cs_token(t)) {
            printf("\\%s ", names[t.val]);
            continue;
        }

        putchar(t.val);
    }
}

// --- Entry Point ---

int
main(int argc, char **argv) {
    init_system();

    if (argc > 1) {
        // Set jobname from first argument
        const char *p = strrchr(argv[1], '/');
        p = p ? p + 1 : argv[1];
        strncpy(jobname, p, MAX_FILENAME - 1);
        char *dot = strrchr(jobname, '.');
        if (dot) *dot = '\0';
    }

    push_file(stdin);
    process();
    return 0;
}
