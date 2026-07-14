#!/usr/bin/env -S ccraft
// texproc - faithful TeX macro processor (expansion engine only)
//
// ============================================================================
// OVERVIEW
// ============================================================================
//
// This is a complete implementation of the TeX macro expansion engine. It
// processes TeX's macro language without the typesetting - think of it as the
// "programmable preprocessor" component of TeX extracted into a standalone tool.
//
// TeX processes text in two phases: expansion and execution. This tool handles
// the expansion phase, where macros are replaced with their definitions,
// conditionals are evaluated, and arithmetic is computed. The output is the
// expanded token stream, with non-expandable tokens (like text) passed through.
//
// WHAT THIS IS:
//   - Complete TeX macro language (definitions, conditionals, expansion)
//   - All e-TeX macro extensions (\expanded, \ifdefined, introspection, etc.)
//   - File I/O primitives (\input, \read, \write, etc.)
//   - Arithmetic (\numexpr, \advance, etc.)
//   - Category code manipulation (\catcode)
//
// WHAT THIS IS NOT:
//   - Not a typesetter (no \hbox, \vbox, \font, dimensions, etc.)
//   - Not rendering-aware (no \ifvmode, \ifhmode, glue, penalties, etc.)
//
// ============================================================================
// KEY CONCEPTS
// ============================================================================
//
// TOKENS
// ------
// TeX reads characters and converts them to tokens based on "category codes".
// Each character has a category that determines its role:
//
//   0  escape     (\)  Starts a control sequence name
//   1  begin      ({)  Starts a group
//   2  end        (})  Ends a group
//   3  math       ($)  Math mode (passed through)
//   4  align      (&)  Alignment (passed through)
//   5  eol        (\n) End of line
//   6  param      (#)  Macro parameter marker
//   7  superscript(^)  Superscript (passed through)
//   8  subscript  (_)  Subscript (passed through)
//   9  ignored          Ignored characters
//   10 space      ( )  Space token
//   11 letter     (A-Z,a-z) Letter for control sequence names
//   12 other      (0-9,etc) Other characters
//   13 active     (~)  Active character (acts like a control sequence)
//   14 comment    (%)  Comment start (rest of line ignored)
//   15 invalid          Invalid character
//
// Control sequences are names like \foo formed by \ followed by letters.
// A single non-letter after \ creates a control sequence too: \$ \# etc.
//
// MACROS
// ------
// Macros are user-defined commands. Define them with \def:
//
//   \def\hello{Hello, World}     % \hello expands to "Hello, World"
//   \def\greet#1{Hello, #1!}     % #1 is a parameter: \greet{Alice} -> "Hello, Alice!"
//   \def\swap#1#2{#2#1}          % Multiple params: \swap{A}{B} -> "BA"
//
// The #1, #2, etc. are parameter markers (up to #9).
// Arguments can be:
//   - A single token: \swap AB -> "BA"
//   - A braced group: \swap{ABC}{DEF} -> "DEFABC"
//
// EXPANSION VS EXECUTION
// ----------------------
// Some commands EXPAND (are replaced by something else):
//   \foo              -> replacement text (if \foo is a macro)
//   \the\count0       -> the value of count register 0
//   \number123        -> "123" (expands to digit tokens)
//   \ifnum1<2 yes\fi  -> "yes" (conditional expansion)
//
// Some commands EXECUTE (have side effects, don't expand):
//   \def\foo{bar}     -> defines \foo (no expansion output)
//   \count0=42        -> sets register 0 to 42
//   \global\def...    -> affects global scope
//
// GROUPING AND SCOPE
// ------------------
// Braces { } create groups. Assignments inside a group are LOCAL by default:
//
//   \def\x{outer}          % \x = "outer"
//   {\def\x{inner}}        % \x = "inner" inside braces
//   \x                     % \x = "outer" again (reverted)
//
// Use \global for permanent changes:
//
//   {\global\def\x{global}}  % \x stays "global" after }
//
// \begingroup and \endgroup work like { } for scoping but don't affect
// grouping depth for macro arguments.
//
// ============================================================================
// PRIMITIVE REFERENCE
// ============================================================================
//
// --- DEFINITION COMMANDS ---
//
// \def\cs<parameter-text>{<replacement-text>}
//     Define a macro. The parameter text specifies how arguments are parsed.
//     Example: \def\bold#1{**#1**}  ->  \bold{text} becomes **text**
//
// \gdef\cs...{...}
//     Like \def but always global (ignores local scope).
//
// \edef\cs...{...}
//     "Expanded def" - the replacement text is fully expanded before storing.
//     Example: \count0=5 \edef\five{\the\count0}  -> \five is literally "5"
//
// \xdef\cs...{...}
//     Global + expanded (same as \global\edef).
//
// \let\cs1=\cs2
//     Make \cs1 mean exactly what \cs2 currently means.
//     Example: \let\x=\relax  -> \x now acts like \relax
//
// \futurelet\cs\a\b
//     Like \let\cs=\b but then executes \a\b (doesn't consume \b).
//     Used for lookahead in macro programming.
//
// --- PREFIX COMMANDS ---
//
// \global
//     Makes the next assignment global (survives group end).
//
// \long
//     Allows the next macro definition to accept \par in arguments.
//
// \outer
//     Marks the next macro as "outer" (cannot appear inside other macros).
//
// \protected
//     (e-TeX) Prevents macro from expanding in \edef context.
//
// --- REGISTER DEFINITION ---
//
// \chardef\cs=<number>
//     Define \cs to produce character <number>.
//     Example: \chardef\percent=`\%  -> \percent outputs %
//
// \countdef\cs=<number>
//     Define \cs as shorthand for \count<number>.
//     Example: \countdef\mycount=0  -> \mycount means \count0
//
// \toksdef\cs=<number>
//     Define \cs as shorthand for \toks<number>.
//
// --- REGISTERS ---
//
// \count<number>
//     Integer register (0-255). Use for arithmetic.
//     \count0=42        Set to 42
//     \advance\count0 by 1   Add 1
//     \the\count0       Get value as tokens
//
// \toks<number>
//     Token register. Stores arbitrary token lists.
//     \toks0={hello \world}   Store tokens
//     \the\toks0              Retrieve tokens
//
// \catcode<char>=<number>
//     Category code of character. Changes how character is tokenized.
//     \catcode`\@=11    Make @ a letter (can be in control sequences)
//
// \lccode<char>=<char>
//     Lowercase mapping for \lowercase.
//
// \uccode<char>=<char>
//     Uppercase mapping for \uppercase.
//
// --- ARITHMETIC ---
//
// \advance<register> by <number>
//     Add to a register. "by" is optional.
//     \advance\count0 by 10
//
// \multiply<register> by <number>
//     Multiply register.
//
// \divide<register> by <number>
//     Integer divide register.
//
// \numexpr <expression> \relax
//     (e-TeX) Inline arithmetic expression.
//     \the\numexpr 2+3*4\relax  -> "14"
//     Supports +, -, *, / with standard precedence.
//
// --- NUMBER FORMATS ---
//
// Numbers can be written as:
//   123      Decimal
//   "FF      Hexadecimal (prefix ")
//   '77      Octal (prefix ')
//   `A       Character code (prefix `)
//
// --- CONDITIONALS ---
//
// All conditionals have the form: \ifTYPE <test> <true-text> \else <false-text> \fi
// The \else part is optional.
//
// \if<token1><token2>
//     True if both tokens have the same character code.
//     \if AB false\fi  (A != B)
//     \if AA true\fi   (A == A)
//
// \ifx<token1><token2>
//     True if both tokens have exactly the same meaning.
//     Useful for checking if macros are defined the same way.
//
// \ifnum<number1><relation><number2>
//     Compare numbers. Relation is <, =, or >.
//     \ifnum\count0>5 big\fi
//
// \ifcat<token1><token2>
//     True if both tokens have the same category code.
//
// \ifodd<number>
//     True if number is odd.
//
// \iftrue
//     Always true.
//
// \iffalse
//     Always false. Useful for commenting out code:
//     \iffalse This is ignored \fi
//
// \ifdefined\cs
//     (e-TeX) True if \cs is defined.
//
// \ifcsname <name>\endcsname
//     (e-TeX) True if control sequence with given name exists.
//
// \ifcase<number> <case0> \or <case1> \or <case2> ... \else <default> \fi
//     Multi-way branch. Executes case N for value N.
//     \ifcase\count0 zero\or one\or two\else many\fi
//
// \ifeof<number>
//     True if read stream <number> is at end of file.
//
// \unless\ifTYPE...
//     (e-TeX) Negates the following conditional.
//     \unless\ifnum\count0=0 nonzero\fi
//
// --- EXPANSION CONTROL ---
//
// \expandafter<token1><token2>
//     Expand <token2> first, then process <token1>.
//     Essential for macro programming tricks.
//
// \noexpand<token>
//     Prevents <token> from expanding (once).
//     In \edef: \edef\x{\noexpand\foo} stores \foo unexpanded.
//
// \the<register>
//     Expand to the value of a register or parameter.
//     \the\count0    -> register value
//     \the\toks0     -> token list contents
//     \the\catcode`A -> category code
//
// \number<number>
//     Expand to decimal representation.
//     \number`A -> "65"
//
// \romannumeral<number>
//     Expand to lowercase roman numerals.
//     \romannumeral 42 -> "xlii"
//
// \string<token>
//     Convert token to character tokens.
//     \string\foo -> "\foo" as characters
//
// \meaning<token>
//     Expand to human-readable description of meaning.
//     \meaning\def -> "def"
//     \meaning\mymacro -> "macro:->replacement"
//
// \csname <characters> \endcsname
//     Build control sequence from character tokens.
//     \csname foo\endcsname  -> \foo
//     Useful for computed control sequence names.
//
// \uppercase{<tokens>}
//     Convert to uppercase using \uccode mappings.
//
// \lowercase{<tokens>}
//     Convert to lowercase using \lccode mappings.
//
// \detokenize{<tokens>}
//     (e-TeX) Convert tokens to characters (category 12/10).
//     Control sequences become their string form.
//
// \unexpanded{<tokens>}
//     (e-TeX) In \edef, prevents expansion of tokens.
//     \edef\x{\unexpanded{\foo}}  -> \x contains \foo
//
// \scantokens{<tokens>}
//     (e-TeX) Re-tokenize token list as if reading from file.
//
// \expanded{<tokens>}
//     (e-TeX) Fully expand tokens and insert result.
//
// --- GROUPING ---
//
// { }
//     Begin/end group. Creates local scope.
//
// \begingroup \endgroup
//     Same as { } but doesn't affect argument parsing.
//
// \bgroup \egroup
//     Implicit braces. Act like { } for grouping but are tokens.
//
// --- SPECIAL COMMANDS ---
//
// \relax
//     Does nothing. Useful as delimiter or terminator.
//
// \aftergroup<token>
//     Execute <token> after current group ends.
//     {\aftergroup\foo ...} -> \foo runs after }
//
// \afterassignment<token>
//     Execute <token> after the next assignment completes.
//
// \ignorespaces
//     Skip following space tokens.
//
// \end
//     Terminate processing immediately.
//
// --- INPUT/OUTPUT ---
//
// \input <filename>
//     Read and process file. Extension optional.
//
// \endinput
//     Stop reading current file after current line.
//
// \jobname
//     Expand to the base name of the input file.
//
// \inputlineno
//     (e-TeX) Expand to current line number.
//
// --- FILE I/O ---
//
// \openin<number>=<filename>
//     Open file for reading on stream <number> (0-15).
//
// \closein<number>
//     Close read stream.
//
// \read<number> to \cs
//     Read one line from stream into macro \cs.
//     Stream -1 reads from terminal (stdin).
//
// \readline<number> to \cs
//     (e-TeX) Read line without tokenization (all catcode 12).
//
// \openout<number>=<filename>
//     Open file for writing on stream <number> (0-15).
//
// \closeout<number>
//     Close write stream.
//
// \write<number>{<tokens>}
//     Write tokens to stream. Tokens are expanded first.
//     Stream -1 writes to terminal (stderr).
//     Prefix with \immediate for immediate output.
//
// \immediate
//     Execute following \write/\openout/\closeout immediately.
//
// --- PARAMETERS ---
//
// \escapechar
//     Character code to print before control sequence names.
//     Default: `\\ (92)
//
// \endlinechar
//     Character appended to each input line.
//     Default: `\\r (13)
//
// \newlinechar
//     Character that represents newline in output.
//     Default: -1 (disabled)
//
// \globaldefs
//     If positive, all assignments are global.
//     If negative, all assignments are local.
//     Default: 0 (respect \global prefix)
//
// --- DATE/TIME ---
//
// \year  \month  \day
//     Current date at startup.
//
// \time
//     Minutes since midnight at startup.
//
// --- DEBUGGING ---
//
// \show<token>
//     Print meaning of token to terminal/log.
//
// \showthe<register>
//     Print value of register to terminal/log.
//
// \message{<text>}
//     Print text to terminal.
//
// \errmessage{<text>}
//     Print error message.
//
// \tracingmacros
//     If positive, trace macro expansions to stderr.
//     Shows macro name and arguments for each expansion.
//
// \tracingcommands
//     If positive, trace command execution. (Reserved for future use)
//
// --- e-TeX INTROSPECTION ---
//
// \currentgrouplevel
//     Current grouping depth.
//
// \currentgrouptype
//     Type of current group (0=none, 1=simple, etc.)
//
// \currentiflevel
//     Nesting depth of conditionals.
//
// \currentiftype
//     Type number of innermost conditional.
//     1=\if, 2=\ifcat, 3=\ifnum, 4=\ifodd, 12=\ifx, 13=\ifeof,
//     14=\iftrue, 15=\iffalse, 16=\ifcase, 17=\ifdefined, 18=\ifcsname
//
// \currentifbranch
//     Which branch of innermost conditional: 1=then, -1=else, 0=case
//
// ============================================================================
// USAGE
// ============================================================================
//
// Read from stdin:
//   echo '\def\hello{Hello}\hello' | texproc
//
// Process file:
//   texproc < input.tex > output.txt
//
// With jobname:
//   texproc myfile.tex < myfile.tex > output.txt
//
// ============================================================================
// IMPLEMENTATION NOTES
// ============================================================================
//
// - Token-based: Input is tokenized once, then processed.
// - Left-to-right: Expansion proceeds left to right.
// - Recursive: Macros can call other macros, conditionals nest.
// - State: Global state includes registers, meanings, category codes.
// - Scope: Local assignments are saved on a stack and restored at group end.
//
// ============================================================================
// OPTIONS
// ============================================================================
//
// Positional arguments:
//   [filename]  Set jobname (affects \jobname expansion). No file reading.
//               Input is always from stdin.
//
// ============================================================================
// EXIT CODES
// ============================================================================
//
//   0   Success (input processed without fatal error)
//   1   Fatal error (die() called — runaway conditional, memory, syntax)
//
// ============================================================================
// DEPENDENCIES
// ============================================================================
//
//   stdlib: stdio.h, stdlib.h, string.h, ctype.h, time.h
//   Build:  ccraft, gcc (any C99 compiler)
//
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

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
} catcode_t;

// --- Token Types ---

typedef enum {
    CMD_CHAR, CMD_CS, CMD_PARAM_REF, CMD_MATCH
} token_cmd_t;

typedef struct {
    token_cmd_t cmd;
    int val;
} token_t;

typedef struct {
    token_t *tokens;
    size_t len;
} token_list_t;

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
} meaning_type_t;

typedef struct {
    meaning_type_t type;
    int val;
} meaning_t;

// --- Macro Storage ---

typedef struct {
    token_list_t pattern;
    token_list_t body;
    int nparams;
    int is_long;
    int is_outer;
    int is_protected;
} macro_t;

// --- Save Stack ---

typedef enum {
    SAVE_MEANING, SAVE_COUNT, SAVE_TOKS, SAVE_CATCODE, SAVE_LCCODE, SAVE_UCCODE
} save_type_t;

typedef struct {
    save_type_t type;
    int idx;
    meaning_t meaning;
    token_list_t toks;
    int level;
} save_entry_t;

// --- Input State ---

typedef struct input_state {
    token_t *tokens;
    size_t count;
    size_t pos;
    struct input_state *prev;
    FILE *file;
    int is_file;
    int owns_tokens;
    int endinput;
    int lineno;
} input_state_t;

// --- Globals ---

static catcode_t catcodes[NUM_CHARS];
static int lccode[NUM_CHARS];
static int uccode[NUM_CHARS];

static char string_pool[MAX_STRINGS];
static size_t string_used = 0;

static char *names[MAX_NAMES];
static size_t nnames = 0;

static macro_t macros[MAX_NAMES];
static int nmacros = 0;

static meaning_t eqtb[MAX_NAMES];
static int count_regs[MAX_REGISTERS];
static token_list_t toks_regs[MAX_REGISTERS];

static save_entry_t save_stack[MAX_SAVE];
static int save_ptr = 0;
static int level = 0;

static input_state_t *input = NULL;
static int ungot = EOF;

static token_t build_buf[MAX_LIST_BUILD];
static size_t build_len = 0;

static token_t aftergroup_stack[MAX_AFTERGROUP];
static int aftergroup_ptr = 0;
static int aftergroup_level[MAX_AFTERGROUP];

static token_t afterassignment_token;
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

static token_t get_token(void);
static token_t get_raw_token(void);
static void push_tokens(token_list_t *list, int own);
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

static int has_catcode(int c, catcode_t cat) {
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

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// --- Token Predicates ---

static int is_char_token(token_t t)  { return t.cmd == CMD_CHAR; }
static int is_cs_token(token_t t)    { return t.cmd == CMD_CS; }
static int is_param_ref(token_t t)   { return t.cmd == CMD_PARAM_REF; }
static int is_match_token(token_t t) { return t.cmd == CMD_MATCH; }
static int is_eof_token(token_t t)   { return is_char_token(t) && t.val == EOF; }

static int is_space_token(token_t t) {
    return is_char_token(t) && is_space(t.val);
}

static int is_digit_token(token_t t) {
    return is_char_token(t) && is_digit(t.val);
}

static int is_begin_group_token(token_t t) {
    return is_char_token(t) && is_begin_group(t.val);
}

static int is_param_token(token_t t) {
    return is_char_token(t) && is_param(t.val);
}

static int is_hex_digit_token(token_t t) {
    return is_char_token(t) && is_hex_digit(t.val);
}

static int is_octal_digit_token(token_t t) {
    return is_char_token(t) && is_octal_digit(t.val);
}

// Number syntax markers
static int is_minus_token(token_t t)       { return is_char_token(t) && t.val == '-'; }
static int is_plus_token(token_t t)        { return is_char_token(t) && t.val == '+'; }
static int is_backtick_token(token_t t)    { return is_char_token(t) && t.val == '`'; }
static int is_hex_prefix_token(token_t t)  { return is_char_token(t) && t.val == '"'; }
static int is_octal_prefix_token(token_t t){ return is_char_token(t) && t.val == '\''; }

// Expression tokens
static int is_lparen_token(token_t t) { return is_char_token(t) && t.val == '('; }
static int is_rparen_token(token_t t) { return is_char_token(t) && t.val == ')'; }
static int is_star_token(token_t t)   { return is_char_token(t) && t.val == '*'; }
static int is_slash_token(token_t t)  { return is_char_token(t) && t.val == '/'; }

static int tokens_equal(token_t a, token_t b) {
    return a.cmd == b.cmd && a.val == b.val;
}

static int meanings_equal(meaning_t a, meaning_t b) {
    return a.type == b.type && a.val == b.val;
}

// --- Meaning Predicates ---

static int is_undefined(meaning_t m)  { return m.type == M_UNDEFINED; }
static int is_macro(meaning_t m)      { return m.type == M_MACRO; }
static int is_chardef(meaning_t m)    { return m.type == M_CHARDEF; }
static int is_catcode_cmd(meaning_t m){ return m.type == M_CATCODE; }
static int is_lccode_cmd(meaning_t m) { return m.type == M_LCCODE; }
static int is_uccode_cmd(meaning_t m) { return m.type == M_UCCODE; }
static int is_count(meaning_t m)      { return m.type == M_COUNT; }
static int is_toks(meaning_t m)       { return m.type == M_TOKS; }
static int is_escapechar(meaning_t m) { return m.type == M_ESCAPECHAR; }
static int is_endlinechar(meaning_t m){ return m.type == M_ENDLINECHAR; }
static int is_newlinechar(meaning_t m){ return m.type == M_NEWLINECHAR; }
static int is_inputlineno(meaning_t m){ return m.type == M_INPUTLINENO; }
static int is_year(meaning_t m)       { return m.type == M_YEAR; }
static int is_month(meaning_t m)      { return m.type == M_MONTH; }
static int is_day(meaning_t m)        { return m.type == M_DAY; }
static int is_time_cmd(meaning_t m)   { return m.type == M_TIME; }
static int is_tracingmacros(meaning_t m)     { return m.type == M_TRACINGMACROS; }
static int is_tracingcommands(meaning_t m)   { return m.type == M_TRACINGCOMMANDS; }
static int is_globaldefs(meaning_t m) { return m.type == M_GLOBALDEFS; }
static int is_currentgrouplevel(meaning_t m) { return m.type == M_CURRENTGROUPLEVEL; }
static int is_currentgrouptype(meaning_t m)  { return m.type == M_CURRENTGROUPTYPE; }
static int is_currentiflevel(meaning_t m)    { return m.type == M_CURRENTIFLEVEL; }
static int is_currentiftype(meaning_t m)     { return m.type == M_CURRENTIFTYPE; }
static int is_currentifbranch(meaning_t m)   { return m.type == M_CURRENTIFBRANCH; }
static int is_numexpr(meaning_t m)    { return m.type == M_NUMEXPR; }
static int is_endcsname(meaning_t m)  { return m.type == M_ENDCSNAME; }
static int is_relax(meaning_t m)      { return m.type == M_RELAX; }
static int is_or(meaning_t m)         { return m.type == M_OR; }
static int is_else(meaning_t m)       { return m.type == M_ELSE; }
static int is_fi(meaning_t m)         { return m.type == M_FI; }
static int is_unexpanded(meaning_t m) { return m.type == M_UNEXPANDED; }
static int is_conditional(meaning_t m) {
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

// Stream validity and state
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
    if (string_used + len >= MAX_STRINGS) die("String pool full");
    char *p = &string_pool[string_used];
    strcpy(p, s);
    string_used += len;
    return p;
}

// --- Name Table ---

static int
intern(const char *name) {
    for (size_t i = 0; i < nnames; i++) {
        if (strcmp(names[i], name) == 0) return (int)i;
    }
    if (nnames >= MAX_NAMES) die("Name table full");
    names[nnames] = store_string(name);
    return (int)nnames++;
}

// --- Token List Operations ---

static token_t *
clone_tokens(token_t *src, size_t len) {
    if (len == 0) return NULL;
    token_t *dest = malloc(len * sizeof(token_t));
    if (!dest) die("Out of memory");
    memcpy(dest, src, len * sizeof(token_t));
    return dest;
}

static token_t
make_char_token(int c) {
    token_t t = {CMD_CHAR, c};
    return t;
}

static token_t
make_cs_token(int idx) {
    token_t t = {CMD_CS, idx};
    return t;
}

static void
push_single_token(token_t t) {
    token_t *p = malloc(sizeof(token_t));
    *p = t;
    token_list_t list = {p, 1};
    push_tokens(&list, 1);
}

static void
push_two_tokens(token_t a, token_t b) {
    token_t *p = malloc(2 * sizeof(token_t));
    p[0] = a;
    p[1] = b;
    token_list_t list = {p, 2};
    push_tokens(&list, 1);
}

// --- Token List Building ---

static void build_reset(void) { build_len = 0; }

static void
build_append(token_t t) {
    if (build_len >= MAX_LIST_BUILD) die("Token list too long");
    build_buf[build_len++] = t;
}

static token_list_t
build_finish(void) {
    token_list_t list;
    list.tokens = clone_tokens(build_buf, build_len);
    list.len = build_len;
    return list;
}

// --- Scoping ---

static int at_global_level(void) { return level == 0; }

static void
save_meaning(int idx) {
    if (at_global_level()) return;
    if (save_ptr >= MAX_SAVE) die("Save stack full");
    save_stack[save_ptr].type = SAVE_MEANING;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].meaning = eqtb[idx];
    save_stack[save_ptr].level = level;
    save_ptr++;
}

static void
save_count(int idx) {
    if (at_global_level()) return;
    if (save_ptr >= MAX_SAVE) die("Save stack full");
    save_stack[save_ptr].type = SAVE_COUNT;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].meaning.val = count_regs[idx];
    save_stack[save_ptr].level = level;
    save_ptr++;
}

static void
save_toks(int idx) {
    if (at_global_level()) return;
    if (save_ptr >= MAX_SAVE) die("Save stack full");
    save_stack[save_ptr].type = SAVE_TOKS;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].toks = toks_regs[idx];
    save_stack[save_ptr].level = level;
    save_ptr++;
}

static void
save_catcode_entry(int idx) {
    if (at_global_level()) return;
    if (save_ptr >= MAX_SAVE) die("Save stack full");
    save_stack[save_ptr].type = SAVE_CATCODE;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].meaning.val = catcodes[idx];
    save_stack[save_ptr].level = level;
    save_ptr++;
}

static void
save_lccode_entry(int idx) {
    if (at_global_level()) return;
    if (save_ptr >= MAX_SAVE) die("Save stack full");
    save_stack[save_ptr].type = SAVE_LCCODE;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].meaning.val = lccode[idx];
    save_stack[save_ptr].level = level;
    save_ptr++;
}

static void
save_uccode_entry(int idx) {
    if (at_global_level()) return;
    if (save_ptr >= MAX_SAVE) die("Save stack full");
    save_stack[save_ptr].type = SAVE_UCCODE;
    save_stack[save_ptr].idx = idx;
    save_stack[save_ptr].meaning.val = uccode[idx];
    save_stack[save_ptr].level = level;
    save_ptr++;
}

static void
pop_aftergroup_tokens(void) {
    while (aftergroup_ptr > 0 && aftergroup_level[aftergroup_ptr - 1] == level) {
        aftergroup_ptr--;
        push_single_token(aftergroup_stack[aftergroup_ptr]);
    }
}

static void
restore_scope(void) {
    while (save_ptr > 0 && save_stack[save_ptr - 1].level == level) {
        save_ptr--;
        save_entry_t *e = &save_stack[save_ptr];
        switch (e->type) {
            case SAVE_MEANING: eqtb[e->idx] = e->meaning; break;
            case SAVE_COUNT:   count_regs[e->idx] = e->meaning.val; break;
            case SAVE_TOKS:    toks_regs[e->idx] = e->toks; break;
            case SAVE_CATCODE: catcodes[e->idx] = (catcode_t)e->meaning.val; break;
            case SAVE_LCCODE:  lccode[e->idx] = e->meaning.val; break;
            case SAVE_UCCODE:  uccode[e->idx] = e->meaning.val; break;
        }
    }
    pop_aftergroup_tokens();
    level--;
}

static void
invalidate_saved(int idx, save_type_t type) {
    for (int i = save_ptr - 1; i >= 0; i--) {
        if (save_stack[i].type == type && save_stack[i].idx == idx) {
            save_stack[i].level = -1;
        }
    }
}

// Resolve global flag with globaldefs
static int effective_global(int global) {
    if (globaldefs > 0) return 1;
    if (globaldefs < 0) return 0;
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
set_meaning(int idx, meaning_t m, int global) {
    global = effective_global(global);
    if (!global) save_meaning(idx);
    else invalidate_saved(idx, SAVE_MEANING);
    eqtb[idx] = m;
}

static void
set_count(int idx, int val, int global) {
    if (idx < 0 || idx >= MAX_REGISTERS) die("Count register out of range");
    global = effective_global(global);
    if (!global) save_count(idx);
    else invalidate_saved(idx, SAVE_COUNT);
    count_regs[idx] = val;
}

static void
set_toks(int idx, token_list_t val, int global) {
    if (idx < 0 || idx >= MAX_REGISTERS) die("Tokens register out of range");
    global = effective_global(global);
    if (!global) save_toks(idx);
    else invalidate_saved(idx, SAVE_TOKS);
    toks_regs[idx] = val;
}

static void
set_catcode(int idx, int val, int global) {
    global = effective_global(global);
    if (!global) save_catcode_entry(idx);
    else invalidate_saved(idx, SAVE_CATCODE);
    catcodes[idx] = (catcode_t)val;
}

static void
set_lccode_val(int idx, int val, int global) {
    if (!global) save_lccode_entry(idx);
    else invalidate_saved(idx, SAVE_LCCODE);
    lccode[idx] = val;
}

static void
set_uccode_val(int idx, int val, int global) {
    if (!global) save_uccode_entry(idx);
    else invalidate_saved(idx, SAVE_UCCODE);
    uccode[idx] = val;
}

// --- Initialization ---

static void
def_primitive(const char *name, meaning_type_t type) {
    int idx = intern(name);
    eqtb[idx].type = type;
}

static void
init_catcodes(void) {
    for (int i = 0; i < NUM_CHARS; i++) catcodes[i] = CAT_OTHER;
    for (int i = 'a'; i <= 'z'; i++) catcodes[i] = CAT_LETTER;
    for (int i = 'A'; i <= 'Z'; i++) catcodes[i] = CAT_LETTER;
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
    if (input && input->is_file) {
        if (input->endinput) return EOF;
        int c = fgetc(input->file);
        if (c == '\n') {
            input->lineno++;
            // Append endlinechar if set
            if (charcode_valid(endlinechar)) {
                ungot = endlinechar;
            }
        }
        return c;
    }
    return EOF;
}

static void unread_char(int c) { ungot = c; }

static input_state_t *
find_file_input(void) {
    input_state_t *s = input;
    while (s && !s->is_file) s = s->prev;
    return s;
}

static int
get_input_lineno(void) {
    input_state_t *s = find_file_input();
    return s ? s->lineno : 0;
}

static input_state_t *
alloc_input_state(void) {
    input_state_t *s = malloc(sizeof(input_state_t));
    s->count = 0;
    s->pos = 0;
    s->prev = input;
    s->endinput = 0;
    return s;
}

static void
push_file(FILE *f) {
    input_state_t *s = alloc_input_state();
    s->tokens = NULL;
    s->is_file = 1;
    s->file = f;
    s->owns_tokens = 0;
    s->lineno = 1;
    input = s;
}

static void
push_tokens(token_list_t *list, int own) {
    if (list->len == 0) {
        if (own && list->tokens) free(list->tokens);
        return;
    }
    input_state_t *s = alloc_input_state();
    s->tokens = list->tokens;
    s->count = list->len;
    s->is_file = 0;
    s->owns_tokens = own;
    input = s;
}

static void
pop_input(void) {
    input_state_t *prev = input->prev;
    if (input->is_file && input->file != stdin) fclose(input->file);
    if (!input->is_file && input->owns_tokens && input->tokens) free(input->tokens);
    free(input);
    input = prev;
}

static int reading_from_tokens(void) { return input && !input->is_file; }

// --- Low-Level Tokenization ---

static token_t
scan_control_sequence(void) {
    char buf[MAX_CS_NAME];
    int len = 0;
    int c = read_char();

    buf[len++] = c;

    if (is_letter(c)) {
        while (1) {
            int next = read_char();
            if (!is_letter(next)) { unread_char(next); break; }
            if (len < MAX_CS_NAME - 1) buf[len++] = next;
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
        if (c == EOF || is_eol(c)) break;
    }
}

static token_t
get_raw_token(void) {
    while (reading_from_tokens()) {
        if (input->pos < input->count) {
            return input->tokens[input->pos++];
        }
        pop_input();
    }

    if (!input) return make_char_token(EOF);

    int c = read_char();
    if (c == EOF) {
        if (input->prev) { pop_input(); return get_raw_token(); }
        return make_char_token(EOF);
    }

    if (is_escape(c)) return scan_control_sequence();

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

static meaning_t
get_meaning(token_t t) {
    if (is_cs_token(t)) return eqtb[t.val];

    if (is_char_token(t)) {
        if (is_begin_group(t.val)) {
            meaning_t m = {M_LBRACE, 0};
            return m;
        }
        if (is_end_group(t.val)) {
            meaning_t m = {M_RBRACE, 0};
            return m;
        }
    }

    meaning_t m = {M_CHAR, t.val};
    return m;
}

// --- Conditional Stack (for e-TeX introspection) ---

static void cond_push(int type, int branch) {
    if (cond_ptr >= MAX_COND_STACK) { return; }

    cond_type_stack[cond_ptr] = type;
    cond_branch_stack[cond_ptr] = branch;
    cond_ptr++;
}

static void cond_pop(void) {
    if (cond_ptr > 0) cond_ptr--;
}

static void cond_set_branch(int branch) {
    if (cond_ptr > 0) cond_branch_stack[cond_ptr - 1] = branch;
}

// --- Conditional Skipping ---

static void
skip_to_fi(void) {
    int depth = 0;
    while (1) {
        token_t t = get_raw_token();
        if (is_eof_token(t)) die("Runaway conditional");
        meaning_t m = get_meaning(t);
        if (is_conditional(m)) depth++;
        if (is_fi(m)) {
            if (depth == 0) return;
            depth--;
        }
    }
}

static void
skip_to_else_or_fi(void) {
    int depth = 0;
    while (1) {
        token_t t = get_raw_token();
        if (is_eof_token(t)) die("Runaway conditional");
        meaning_t m = get_meaning(t);
        if (is_conditional(m)) depth++;
        if (is_fi(m)) {
            if (depth == 0) return;
            depth--;
        }
        if ((is_else(m) || is_or(m)) && depth == 0) return;
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
        token_t t = get_raw_token();
        if (is_eof_token(t)) die("Runaway conditional");
        meaning_t m = get_meaning(t);
        if (is_conditional(m)) depth++;
        if (is_fi(m)) {
            if (depth == 0) return;
            depth--;
        }
        if (depth == 0) {
            if (is_or(m)) {
                current++;
                if (current == n) return;
            }
            if (is_else(m)) return;
        }
    }
}

// --- Number Scanning ---

#define NO_CHAR_CODE 256  // Invalid character code (for comparisons)


static int
char_code_of(token_t t) {
    if (is_char_token(t)) return t.val;
    return NO_CHAR_CODE;
}

static int
cat_code_of(token_t t) {
    if (is_char_token(t)) return catcodes[t.val];
    return -1;
}

// Forward declare for mutual recursion
static int scan_numexpr(void);

static int
scan_int(void) {
    token_t t;

    // Skip spaces
    while (1) {
        t = get_token();
        if (!is_space_token(t)) break;
    }

    // Handle signs
    if (is_minus_token(t)) return -scan_int();
    if (is_plus_token(t))  return scan_int();

    meaning_t m = get_meaning(t);

    // Handle \numexpr
    if (is_numexpr(m)) return scan_numexpr();

    // Handle internal quantities
    if (is_count(m)) {
        int idx = scan_int();
        return count_regs[idx];
    }
    if (is_chardef(m)) return m.val;
    if (is_catcode_cmd(m)) { int c = scan_int(); if (!charcode_valid(c)) die("Char code out of range"); return catcodes[c]; }
    if (is_lccode_cmd(m))  { int c = scan_int(); if (!charcode_valid(c)) die("Char code out of range"); return lccode[c]; }
    if (is_uccode_cmd(m))  { int c = scan_int(); if (!charcode_valid(c)) die("Char code out of range"); return uccode[c]; }
    if (is_escapechar(m))  return escapechar;
    if (is_endlinechar(m)) return endlinechar;
    if (is_newlinechar(m)) return newlinechar;
    if (is_inputlineno(m)) return get_input_lineno();
    if (is_year(m))  return tex_year;
    if (is_month(m)) return tex_month;
    if (is_day(m))   return tex_day;
    if (is_time_cmd(m)) return tex_time;
    if (is_globaldefs(m)) return globaldefs;
    if (is_tracingmacros(m)) return tracingmacros;
    if (is_tracingcommands(m)) return tracingcommands;
    if (is_currentgrouplevel(m)) return level;
    if (is_currentgrouptype(m)) return in_group() ? 1 : 0;
    if (is_currentiflevel(m)) return cond_ptr;
    if (is_currentiftype(m)) return in_conditional() ? cond_type_stack[cond_ptr - 1] : 0;
    if (is_currentifbranch(m)) return in_conditional() ? cond_branch_stack[cond_ptr - 1] : 0;

    // Handle backtick character constant
    if (is_backtick_token(t)) {
        token_t next = get_raw_token();
        if (is_char_token(next)) return next.val;
        if (is_cs_token(next)) return (unsigned char)names[next.val][0];
        die("Expected character after `");
    }

    // Handle hex number
    if (is_hex_prefix_token(t)) {
        int val = 0;
        while (1) {
            token_t next = get_token();
            if (!is_hex_digit_token(next)) { push_single_token(next); break; }
            val = val * 16 + hex_value(next.val);
        }
        return val;
    }

    // Handle octal number
    if (is_octal_prefix_token(t)) {
        int val = 0;
        while (1) {
            token_t next = get_token();
            if (!is_octal_digit_token(next)) { push_single_token(next); break; }
            val = val * 8 + (next.val - '0');
        }
        return val;
    }

    // Parse decimal digits
    if (is_digit_token(t)) {
        int val = t.val - '0';
        while (1) {
            token_t next = get_token();
            if (!is_digit_token(next)) { push_single_token(next); break; }
            val = val * 10 + (next.val - '0');
        }
        return val;
    }

    die("Expected number");
    return 0;
}

// \numexpr implementation with proper operator precedence
// Precedence: * / higher than + -

static token_t numexpr_skip_spaces(void) {
    token_t t;

    while (1) {
        t = get_token();
        if (!is_space_token(t)) return t;
    }
}

static int is_numexpr_end(token_t t) {
    return is_rparen_token(t) || is_relax(get_meaning(t));
}

static int numexpr_atom(void);
static int numexpr_term(void);
static int numexpr_expr(void);

static int
numexpr_atom(void) {
    token_t t = numexpr_skip_spaces();

    if (is_lparen_token(t)) {
        int val = numexpr_expr();
        numexpr_skip_spaces();  // consume closing paren
        return val;
    }

    push_single_token(t);
    return scan_int();
}

static int
numexpr_term(void) {
    int result = numexpr_atom();

    while (1) {
        token_t t = numexpr_skip_spaces();

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
        token_t t = numexpr_skip_spaces();

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
    token_t t = numexpr_skip_spaces();
    // consume terminating \relax or )
    if (!is_numexpr_end(t)) push_single_token(t);
    return result;
}

// --- Output Helpers ---

static void
push_number(int n) {
    char buf[MAX_INT_DIGITS];
    sprintf(buf, "%d", n);
    size_t len = strlen(buf);
    token_t *toks = malloc(len * sizeof(token_t));
    for (size_t i = 0; i < len; i++) {
        toks[i] = make_char_token(buf[i]);
    }
    token_list_t list = {toks, len};
    push_tokens(&list, 1);
}

static void
push_roman(int n) {
    if (n <= 0) return;

    static const struct { int val; const char *str; } romans[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
        {100, "c"}, {90, "xc"}, {50, "l"}, {40, "xl"},
        {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}
    };

    // Use local buffer to avoid corrupting global build_buf
    token_t buf[64];
    size_t len = 0;

    for (int i = 0; n > 0 && i < 13; i++) {
        while (n >= romans[i].val) {
            for (const char *p = romans[i].str; *p && len < 64; p++) {
                buf[len++] = make_char_token(*p);
            }
            n -= romans[i].val;
        }
    }

    token_list_t list;
    list.tokens = clone_tokens(buf, len);
    list.len = len;
    push_tokens(&list, 1);
}

static void
push_string(const char *s) {
    size_t len = strlen(s);
    if (len == 0) return;
    token_t *toks = malloc(len * sizeof(token_t));
    for (size_t i = 0; i < len; i++) {
        toks[i] = make_char_token(s[i]);
    }
    token_list_t list = {toks, len};
    push_tokens(&list, 1);
}

// --- Meaning to String ---

static const char *
meaning_to_string(meaning_t m) {
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

static token_list_t
scan_braced_tokens(int expand) {
    // Use local buffer to avoid corruption during nested expansion
    token_t *buf = malloc(MAX_LIST_BUILD * sizeof(token_t));
    size_t len = 0;
    int depth = 1;

    while (depth > 0) {
        token_t t = expand ? get_token() : get_raw_token();

        if (is_eof_token(t)) die("Unmatched brace");

        if (is_char_token(t)) {
            if (is_begin_group(t.val)) depth++;
            if (is_end_group(t.val)) {
                depth--;
                if (depth == 0) break;
            }
        }
        if (len < MAX_LIST_BUILD) buf[len++] = t;
    }

    token_list_t list;
    list.tokens = clone_tokens(buf, len);
    list.len = len;
    free(buf);
    return list;
}

static void
scan_macro_definition(int global, int expand_body) {
    token_t name = get_raw_token();
    if (!is_cs_token(name)) die("\\def requires control sequence");

    int midx = nmacros++;
    macro_t *mac = &macros[midx];
    mac->is_long = long_flag;
    mac->is_outer = outer_flag;
    mac->is_protected = protected_flag;
    long_flag = outer_flag = protected_flag = 0;

    // Scan parameter pattern
    build_reset();
    int nparams = 0;

    while (1) {
        token_t t = get_raw_token();

        if (is_begin_group_token(t)) break;

        if (is_param_token(t)) {
            token_t p = get_raw_token();
            if (is_digit_token(p)) {
                nparams++;
                token_t match = {CMD_MATCH, nparams};
                build_append(match);
                continue;
            }
            build_append(p);
            continue;
        }

        build_append(t);
    }

    mac->nparams = nparams;
    mac->pattern = build_finish();

    // Scan body
    mac->body = scan_braced_tokens(expand_body);

    // Process param refs in body
    if (!expand_body) {
        build_reset();
        for (size_t i = 0; i < mac->body.len; i++) {
            token_t t = mac->body.tokens[i];
            if (is_param_token(t) && i + 1 < mac->body.len) {
                token_t p = mac->body.tokens[++i];
                if (is_digit_token(p)) {
                    token_t ref = {CMD_PARAM_REF, p.val - '0'};
                    build_append(ref);
                    continue;
                }
                build_append(p);
                continue;
            }
            build_append(t);
        }
        if (mac->body.tokens) free(mac->body.tokens);
        mac->body = build_finish();
    }

    meaning_t m = {M_MACRO, midx};
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
trace_macro_expansion(int midx, token_list_t *args, macro_t *mac) {
    const char *name = find_macro_name(midx);
    fprintf(stderr, "\\%s", name);
    for (int i = 1; i <= mac->nparams; i++) {
        fprintf(stderr, " #%d<-", i);
        for (size_t j = 0; j < args[i].len; j++) {
            token_t t = args[i].tokens[j];
            if (is_cs_token(t)) {
                fprintf(stderr, "\\%s", names[t.val]);
            } else if (is_char_token(t)) {
                fputc(t.val, stderr);
            }
        }
    }
    fprintf(stderr, "\n");
}

static void
expand_macro(int midx) {
    macro_t *mac = &macros[midx];
    token_list_t args[10] = {0};

    // Use local buffers to avoid corrupting global build_buf during nested expansion
    token_t *arg_buf = malloc(MAX_LIST_BUILD * sizeof(token_t));
    size_t arg_len = 0;

    for (size_t i = 0; i < mac->pattern.len; i++) {
        token_t p = mac->pattern.tokens[i];

        if (is_match_token(p)) {
            token_list_t *arg = &args[p.val];
            arg_len = 0;

            token_t t = get_raw_token();

            if (is_begin_group_token(t)) {
                int depth = 1;
                while (depth > 0) {
                    token_t s = get_raw_token();
                    if (is_char_token(s)) {
                        if (is_begin_group(s.val)) depth++;
                        if (is_end_group(s.val)) depth--;
                    }
                    if (depth > 0 && arg_len < MAX_LIST_BUILD) {
                        arg_buf[arg_len++] = s;
                    }
                }
            } else {
                arg_buf[arg_len++] = t;
            }

            arg->tokens = clone_tokens(arg_buf, arg_len);
            arg->len = arg_len;
            continue;
        }

        token_t t = get_raw_token();
        if (!tokens_equal(t, p)) { free(arg_buf); die("Macro pattern mismatch"); }
    }

    if (tracing_macros()) trace_macro_expansion(midx, args, mac);

    // Build result using local buffer
    token_t *result_buf = malloc(MAX_LIST_BUILD * sizeof(token_t));
    size_t result_len = 0;

    for (size_t i = 0; i < mac->body.len; i++) {
        token_t b = mac->body.tokens[i];

        if (is_param_ref(b)) {
            token_list_t *arg = &args[b.val];
            for (size_t j = 0; j < arg->len && result_len < MAX_LIST_BUILD; j++) {
                result_buf[result_len++] = arg->tokens[j];
            }
            continue;
        }

        if (result_len < MAX_LIST_BUILD) {
            result_buf[result_len++] = b;
        }
    }

    for (int i = 1; i <= mac->nparams; i++) {
        if (args[i].tokens) free(args[i].tokens);
    }

    token_list_t result;
    result.tokens = clone_tokens(result_buf, result_len);
    result.len = result_len;
    free(arg_buf);
    free(result_buf);
    push_tokens(&result, 1);
}

// --- Conditional Handlers ---

static void
finalize_conditional(int cond_type, int result) {
    if (unless_flag) result = !result;
    unless_flag = 0;
    cond_push(cond_type, result ? 1 : -1);
    if (!result) skip_conditional();
}

static void do_if(void) {
    token_t a = get_token();
    token_t b = get_token();
    int result = (char_code_of(a) == char_code_of(b));
    finalize_conditional(COND_IF, result);
}

static int
evaluate_relation(int a, int b, int op) {
    if (op == '=')  return a == b;
    if (op == '<')  return a < b;
    if (op == '>')  return a > b;
    die("Invalid relation in \\ifnum");
    return 0;
}

static void do_ifx(void) {
    token_t a = get_raw_token();
    token_t b = get_raw_token();
    meaning_t ma = get_meaning(a);
    meaning_t mb = get_meaning(b);

    int result = meanings_equal(ma, mb);
    finalize_conditional(COND_IFX, result);
}

static void do_ifnum(void) {
    int a = scan_int();
    token_t rel;

    while (1) { rel = get_token(); if (!is_space_token(rel)) break; }
    int b = scan_int();
    finalize_conditional(COND_IFNUM, evaluate_relation(a, b, rel.val));
}

static void do_ifcat(void) {
    token_t a = get_token();
    token_t b = get_token();
    int result = (cat_code_of(a) == cat_code_of(b));
    finalize_conditional(COND_IFCAT, result);
}

static void do_ifdefined(void) {
    token_t t = get_raw_token();
    meaning_t m = get_meaning(t);
    int result = !is_undefined(m);
    finalize_conditional(COND_IFDEFINED, result);
}

static void
build_csname(char *buf, int maxlen) {
    int len = 0;

    while (1) {
        token_t t = get_token();
        meaning_t m = get_meaning(t);
        if (is_endcsname(m)) break;
        if (!is_char_token(t)) die("Non-character in control sequence name");
        if (is_space(t.val)) continue;
        if (len < maxlen - 1) buf[len++] = t.val;
    }
    buf[len] = '\0';
}

static void do_ifcsname(void) {
    char buf[MAX_CS_NAME];
    build_csname(buf, MAX_CS_NAME);

    int idx = -1;
    for (size_t i = 0; i < nnames; i++) {
        if (strcmp(names[i], buf) == 0) { idx = (int)i; break; }
    }

    int result = (idx >= 0) && !is_undefined(eqtb[idx]);
    finalize_conditional(COND_IFCSNAME, result);
}

static void do_ifcase(void) {
    int n = scan_int();
    cond_push(COND_IFCASE, 0);  // branch 0 for ifcase (or-selected)
    if (n < 0) { skip_to_fi(); cond_pop(); return; }
    if (n > 0) skip_to_or_n(n);
}

static void do_iftrue(void) {
    int result = 1;
    finalize_conditional(COND_IFTRUE, result);
}

static void do_iffalse(void) {
    int result = 0;
    finalize_conditional(COND_IFFALSE, result);
}

static void do_ifodd(void) {
    int n = scan_int();
    int result = (n % 2 != 0);
    finalize_conditional(COND_IFODD, result);
}

// --- Expansion Primitives ---

static void do_expandafter(void) {
    // Skip first token (raw), expand second, then push expanded first
    token_t first = get_raw_token();
    token_t second = get_token();
    push_two_tokens(second, first);
}

static void do_number(void) {
    int n = scan_int();
    push_number(n);
}

static void do_romannumeral(void) {
    int n = scan_int();
    push_roman(n);
}

static void do_the(void) {
    token_t t;
    while (1) { t = get_token(); if (!is_space_token(t)) break; }

    meaning_t m = get_meaning(t);

    if (is_count(m)) {
        int idx = scan_int();
        push_number(count_regs[idx]);
        return;
    }

    if (is_toks(m)) {
        int idx = scan_int();
        if (toks_regs[idx].len > 0) {
            token_list_t copy;
            copy.tokens = clone_tokens(toks_regs[idx].tokens, toks_regs[idx].len);
            copy.len = toks_regs[idx].len;
            push_tokens(&copy, 1);
        }
        return;
    }

    if (is_catcode_cmd(m)) { push_number(catcodes[scan_int()]); return; }
    if (is_lccode_cmd(m))  { push_number(lccode[scan_int()]); return; }
    if (is_uccode_cmd(m))  { push_number(uccode[scan_int()]); return; }
    if (is_escapechar(m))  { push_number(escapechar); return; }
    if (is_endlinechar(m)) { push_number(endlinechar); return; }
    if (is_newlinechar(m)) { push_number(newlinechar); return; }
    if (is_inputlineno(m)) { push_number(get_input_lineno()); return; }
    if (is_year(m))  { push_number(tex_year); return; }
    if (is_month(m)) { push_number(tex_month); return; }
    if (is_day(m))   { push_number(tex_day); return; }
    if (is_time_cmd(m)) { push_number(tex_time); return; }
    if (is_globaldefs(m)) { push_number(globaldefs); return; }
    if (is_tracingmacros(m)) { push_number(tracingmacros); return; }
    if (is_tracingcommands(m)) { push_number(tracingcommands); return; }
    if (is_currentgrouplevel(m)) { push_number(level); return; }
    if (is_currentgrouptype(m)) { push_number(level > 0 ? 1 : 0); return; }
    if (is_currentiflevel(m)) { push_number(cond_ptr); return; }
    if (is_currentiftype(m)) { push_number(cond_ptr > 0 ? cond_type_stack[cond_ptr - 1] : 0); return; }
    if (is_currentifbranch(m)) { push_number(cond_ptr > 0 ? cond_branch_stack[cond_ptr - 1] : 0); return; }

    die("\\the requires register");
}

static void do_string(void) {
    token_t t = get_raw_token();

    if (is_cs_token(t)) {
        const char *name = names[t.val];
        size_t namelen = strlen(name);
        int has_escape = (escapechar_enabled());
        size_t len = namelen + (has_escape ? 1 : 0);
        token_t *toks = malloc(len * sizeof(token_t));
        size_t j = 0;
        if (has_escape) toks[j++] = make_char_token(escapechar);
        for (size_t i = 0; i < namelen; i++) {
            toks[j++] = make_char_token(name[i]);
        }
        token_list_t list = {toks, len};
        push_tokens(&list, 1);
        return;
    }

    push_single_token(t);
}

static void do_meaning(void) {
    token_t t = get_raw_token();
    meaning_t m = get_meaning(t);
    push_string(meaning_to_string(m));
}

static void do_csname(void) {
    char buf[MAX_CS_NAME];
    build_csname(buf, MAX_CS_NAME);
    int idx = intern(buf);

    if (is_undefined(eqtb[idx])) {
        eqtb[idx].type = M_RELAX;
    }
    push_single_token(make_cs_token(idx));
}

static void do_uppercase(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\uppercase expects {");

    token_list_t body = scan_braced_tokens(0);

    // Transform in place - no extra buffer needed
    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i]) && has_uccode(body.tokens[i].val)) {
            body.tokens[i].val = uccode[body.tokens[i].val];
        }
    }

    push_tokens(&body, 1);
}

static void do_lowercase(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\lowercase expects {");

    token_list_t body = scan_braced_tokens(0);

    // Transform in place - no extra buffer needed
    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i]) && has_lccode(body.tokens[i].val)) {
            body.tokens[i].val = lccode[body.tokens[i].val];
        }
    }

    push_tokens(&body, 1);
}

static void do_detokenize(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\detokenize expects {");

    token_list_t body = scan_braced_tokens(0);

    // Use local buffer to avoid corrupting global build_buf
    token_t *buf = malloc(MAX_LIST_BUILD * sizeof(token_t));
    size_t len = 0;

    for (size_t i = 0; i < body.len; i++) {
        token_t tok = body.tokens[i];
        if (is_cs_token(tok)) {
            if (len < MAX_LIST_BUILD) buf[len++] = make_char_token('\\');
            const char *name = names[tok.val];
            for (size_t j = 0; name[j] && len < MAX_LIST_BUILD; j++) {
                buf[len++] = make_char_token(name[j]);
            }
            if (len < MAX_LIST_BUILD) buf[len++] = make_char_token(' ');
        } else {
            if (len < MAX_LIST_BUILD) buf[len++] = tok;
        }
    }

    if (body.tokens) free(body.tokens);

    token_list_t result;
    result.tokens = clone_tokens(buf, len);
    result.len = len;
    free(buf);
    push_tokens(&result, 1);
}

static void do_jobname(void) {
    push_string(jobname);
}

static void do_noexpand(void) {
    noexpand_next = 1;
}

static void do_unless(void) {
    unless_flag = 1;
}

// --- New Primitives ---

static void do_ignorespaces(void) {
    while (1) {
        token_t t = get_token();
        if (!is_space_token(t)) {
            push_single_token(t);
            return;
        }
    }
}

static void do_ifeof(void) {
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

    token_t t = get_raw_token();
    while (is_space_token(t)) t = get_raw_token();
    push_single_token(t);

    while (1) {
        t = get_raw_token();
        if (is_space_token(t) || is_eof_token(t)) break;
        if (!is_char_token(t)) break;
        if (t.val == '\n' || t.val == '\r') break;
        if (len < maxlen - 1) filename[len++] = t.val;
    }
    filename[len] = '\0';
}

static void do_openin(void) {
    int n = scan_int();
    skip_optional_equals();

    char filename[MAX_FILENAME];
    read_filename(filename, MAX_FILENAME);

    if (stream_valid(n)) {
        if (read_stream_open(n)) fclose(read_files[n]);
        read_files[n] = fopen(filename, "r");
        read_eof[n] = !read_stream_open(n);
    }
}

static void do_closein(void) {
    int n = scan_int();
    if (read_stream_open(n)) {
        fclose(read_files[n]);
        read_files[n] = NULL;
        read_eof[n] = 1;
    }
}

static void do_read(void) {
    int n = scan_int();
    skip_keyword_to();
    skip_spaces_raw();
    token_t t = get_raw_token();
    if (!is_cs_token(t)) die("\\read requires control sequence");

    // Read a line from the stream
    token_t *buf = malloc(MAX_LIST_BUILD * sizeof(token_t));
    size_t len = 0;

    FILE *f = NULL;
    if (is_terminal_stream(n)) {
        f = stdin;
    } else if (stream_valid(n)) {
        f = read_files[n];
    }

    if (f) {
        int c;
        int depth = 0;
        while ((c = fgetc(f)) != EOF) {
            if (c == '\n') {
                if (depth == 0) break;
                if (len < MAX_LIST_BUILD) buf[len++] = make_char_token(' ');
                continue;
            }
            if (c == '{') depth++;
            if (c == '}') depth--;
            if (len < MAX_LIST_BUILD) buf[len++] = make_char_token(c);
        }
        if (c == EOF && stream_valid(n)) read_eof[n] = 1;
    }

    // Define macro with the read content as body
    int midx = nmacros++;
    macro_t *mac = &macros[midx];
    mac->nparams = 0;
    mac->pattern.tokens = NULL;
    mac->pattern.len = 0;
    mac->body.tokens = clone_tokens(buf, len);
    mac->body.len = len;
    mac->is_long = 0;
    mac->is_outer = 0;
    mac->is_protected = 0;

    meaning_t m = {M_MACRO, midx};
    set_meaning(t.val, m, global_flag);
    complete_assignment();
    free(buf);
}

static void do_write(void) {
    int n = scan_int();

    // Get the token list
    token_t t = get_raw_token();
    while (is_space_token(t)) t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\write expects {");

    token_list_t body = scan_braced_tokens(1);  // expand

    // Determine output file
    FILE *out = stdout;
    if (write_stream_open(n)) {
        out = write_files[n];
    } else if (is_terminal_stream(n)) {
        out = stderr;
    }

    // Output tokens
    for (size_t i = 0; i < body.len; i++) {
        token_t tok = body.tokens[i];
        if (is_char_token(tok)) {
            if (matches_newlinechar(tok.val)) {
                fputc('\n', out);
            } else {
                fputc(tok.val, out);
            }
        } else if (is_cs_token(tok)) {
            if (escapechar_enabled()) {
                fputc(escapechar, out);
            }
            fprintf(out, "%s ", names[tok.val]);
        }
    }
    fputc('\n', out);

    if (body.tokens) free(body.tokens);
}

static void do_inputlineno(void) {
    push_number(get_input_lineno());
}

static void do_scantokens(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\scantokens expects {");

    token_list_t body = scan_braced_tokens(0);

    // First pass: compute exact output length
    size_t slen = 0;
    for (size_t i = 0; i < body.len; i++) {
        token_t tok = body.tokens[i];
        if (is_cs_token(tok)) {
            if (escapechar_enabled()) slen++;
            slen += strlen(names[tok.val]);
            slen++;  // trailing space
        } else if (is_char_token(tok)) {
            slen++;
        }
    }
    char *str = malloc(slen + 1);
    slen = 0;

    for (size_t i = 0; i < body.len; i++) {
        token_t tok = body.tokens[i];
        if (is_cs_token(tok)) {
            if (escapechar_enabled()) {
                str[slen++] = escapechar;
            }
            const char *name = names[tok.val];
            for (size_t j = 0; name[j]; j++) {
                str[slen++] = name[j];
            }
            str[slen++] = ' ';
        } else if (is_char_token(tok)) {
            str[slen++] = tok.val;
        }
    }
    str[slen] = '\0';

    if (body.tokens) free(body.tokens);

    // Write to temp file to avoid fmemopen buffer leak
    FILE *f = tmpfile();
    if (f) {
        fwrite(str, 1, slen, f);
        rewind(f);
        push_file(f);
    }
    free(str);
}

static void do_expanded(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\expanded expects {");

    token_list_t body = scan_braced_tokens(1);  // expand
    push_tokens(&body, 1);
}

static void do_openout(void) {
    int n = scan_int();
    skip_optional_equals();

    char filename[MAX_FILENAME];
    read_filename(filename, MAX_FILENAME);

    if (stream_valid(n)) {
        if (write_stream_open(n)) fclose(write_files[n]);
        write_files[n] = fopen(filename, "w");
    }
}

static void do_closeout(void) {
    int n = scan_int();
    if (write_stream_open(n)) {
        fclose(write_files[n]);
        write_files[n] = NULL;
    }
}

static void do_readline(void) {
    int n = scan_int();
    skip_keyword_to();
    skip_spaces_raw();
    token_t t = get_raw_token();
    if (!is_cs_token(t)) die("\\readline requires control sequence");

    // Read a line without special tokenization
    token_t *buf = malloc(MAX_LIST_BUILD * sizeof(token_t));
    size_t len = 0;

    FILE *f = NULL;
    if (is_terminal_stream(n)) {
        f = stdin;
    } else if (stream_valid(n)) {
        f = read_files[n];
    }

    if (f) {
        int c;
        while ((c = fgetc(f)) != EOF && c != '\n') {
            if (len < MAX_LIST_BUILD) buf[len++] = make_char_token(c);
        }
        if (c == EOF && stream_valid(n)) read_eof[n] = 1;
    }

    // Define macro with the read content
    int midx = nmacros++;
    macro_t *mac = &macros[midx];
    mac->nparams = 0;
    mac->pattern.tokens = NULL;
    mac->pattern.len = 0;
    mac->body.tokens = clone_tokens(buf, len);
    mac->body.len = len;
    mac->is_long = 0;
    mac->is_outer = 0;
    mac->is_protected = 0;

    meaning_t m = {M_MACRO, midx};
    set_meaning(t.val, m, global_flag || (globaldefs > 0));
    complete_assignment();
    free(buf);
}

static void do_globaldefs_assignment(void) {
    skip_optional_equals();
    globaldefs = scan_int();
    complete_assignment();
}

static void do_tracingmacros_assignment(void) {
    skip_optional_equals();
    tracingmacros = scan_int();
    complete_assignment();
}

static void do_tracingcommands_assignment(void) {
    skip_optional_equals();
    tracingcommands = scan_int();
    complete_assignment();
}

static void do_end(void) {
    // \end terminates processing — handled by break in process()
}

// --- Dispatch table for get_token() ---

typedef void (*expand_fn)(meaning_t);

static void ef_unless(meaning_t m)      { do_unless(); (void)m; }
static void ef_if(meaning_t m)          { do_if(); (void)m; }
static void ef_ifx(meaning_t m)         { do_ifx(); (void)m; }
static void ef_ifnum(meaning_t m)       { do_ifnum(); (void)m; }
static void ef_ifcat(meaning_t m)       { do_ifcat(); (void)m; }
static void ef_ifdefined(meaning_t m)   { do_ifdefined(); (void)m; }
static void ef_ifcsname(meaning_t m)    { do_ifcsname(); (void)m; }
static void ef_ifcase(meaning_t m)      { do_ifcase(); (void)m; }
static void ef_iftrue(meaning_t m)      { do_iftrue(); (void)m; }
static void ef_iffalse(meaning_t m)     { do_iffalse(); (void)m; }
static void ef_ifodd(meaning_t m)       { do_ifodd(); (void)m; }
static void ef_ifeof(meaning_t m)       { do_ifeof(); (void)m; }
static void ef_or(meaning_t m)          { skip_to_fi(); cond_pop(); (void)m; }
static void ef_else(meaning_t m)        { cond_set_branch(-1); skip_to_fi(); cond_pop(); (void)m; }
static void ef_fi(meaning_t m)          { cond_pop(); (void)m; }
static void ef_number(meaning_t m)      { do_number(); (void)m; }
static void ef_romannumeral(meaning_t m){ do_romannumeral(); (void)m; }
static void ef_the(meaning_t m)         { do_the(); (void)m; }
static void ef_string(meaning_t m)      { do_string(); (void)m; }
static void ef_meaning(meaning_t m)     { do_meaning(); (void)m; }
static void ef_csname(meaning_t m)      { do_csname(); (void)m; }
static void ef_expandafter(meaning_t m) { do_expandafter(); (void)m; }
static void ef_noexpand(meaning_t m)    { do_noexpand(); (void)m; }
static void ef_uppercase(meaning_t m)   { do_uppercase(); (void)m; }
static void ef_lowercase(meaning_t m)   { do_lowercase(); (void)m; }
static void ef_detokenize(meaning_t m)  { do_detokenize(); (void)m; }
static void ef_jobname(meaning_t m)     { do_jobname(); (void)m; }
static void ef_inputlineno(meaning_t m) { do_inputlineno(); (void)m; }
static void ef_scantokens(meaning_t m)  { do_scantokens(); (void)m; }
static void ef_expanded(meaning_t m)    { do_expanded(); (void)m; }

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
static token_t
expand_unexpanded(void) {
    token_t brace = get_raw_token();
    if (!is_begin_group_token(brace)) die("\\unexpanded expects {");
    token_list_t body = scan_braced_tokens(0);
    push_tokens(&body, 1);
    return get_token();
}

// --- Main Token Getter (with expansion) ---

static token_t
get_token(void) {
    token_t t = get_raw_token();

    if (is_eof_token(t)) return t;

    // noexpand_next bypasses all expansion for the next token
    if (noexpand_next) { noexpand_next = 0; return t; }

    meaning_t m = get_meaning(t);

    // Protected macros don't expand in expansion-only contexts
    if (is_macro(m)) {
        macro_t *mac = &macros[m.val];
        if (!mac->is_protected) {
            expand_macro(m.val);
            return get_token();
        }
    }

    // Dispatch table for expandable commands
    if (m.type >= 0 && m.type < M_TYPE_COUNT) {
        expand_fn fn = expand_dispatch[m.type];
        if (fn) {
            fn(m);
            return get_token();
        }
    }

    // \unexpanded returns its argument without expansion
    if (is_unexpanded(m)) return expand_unexpanded();

    return t;
}

// --- Skip Helpers ---

static void skip_spaces_raw(void) {
    while (1) {
        token_t t = get_raw_token();
        if (!is_space_token(t)) {
            push_single_token(t);
            return;
        }
    }
}

static void skip_optional_equals(void) {
    skip_spaces_raw();
    token_t t = get_raw_token();
    if (is_char_token(t) && t.val == '=') return;
    push_single_token(t);
}

static void skip_optional_by(void) {
    skip_spaces_raw();
    token_t t = get_raw_token();

    if (is_char_token(t) && (t.val == 'b' || t.val == 'B')) {
        token_t y = get_raw_token();
        if (is_char_token(y) && (y.val == 'y' || y.val == 'Y')) return;
        push_single_token(y);
    }

    push_single_token(t);
}

static void skip_keyword_to(void) {
    skip_spaces_raw();
    token_t t = get_raw_token();

    if (is_char_token(t) && (t.val == 't' || t.val == 'T')) {
        token_t o = get_raw_token();
        if (is_char_token(o) && (o.val == 'o' || o.val == 'O')) return;
        push_single_token(o);
    }

    push_single_token(t);
}

// --- Command Handlers ---

static void do_let(void) {
    token_t dest = get_raw_token();
    skip_optional_equals();
    token_t src = get_raw_token();
    set_meaning(dest.val, get_meaning(src), global_flag);
    complete_assignment();
}

static void do_futurelet(void) {
    token_t cs = get_raw_token();
    token_t first = get_raw_token();
    token_t second = get_raw_token();
    set_meaning(cs.val, get_meaning(second), global_flag);
    complete_assignment();
    push_two_tokens(first, second);
}

static void do_chardef(void) {
    token_t cs = get_raw_token();
    skip_optional_equals();
    int val = scan_int();
    meaning_t m = {M_CHARDEF, val};
    set_meaning(cs.val, m, global_flag);
    complete_assignment();
}

static void do_countdef(void) {
    token_t cs = get_raw_token();
    skip_optional_equals();
    int idx = scan_int();
    meaning_t m = {M_COUNT, idx};
    set_meaning(cs.val, m, global_flag);
    complete_assignment();
}

static void do_toksdef(void) {
    token_t cs = get_raw_token();
    skip_optional_equals();
    int idx = scan_int();
    meaning_t m = {M_TOKS, idx};
    set_meaning(cs.val, m, global_flag);
    complete_assignment();
}

static void do_count_assignment(void) {
    int idx = scan_int();
    skip_optional_equals();
    int val = scan_int();
    set_count(idx, val, global_flag);
    complete_assignment();
}

static void do_toks_assignment(void) {
    int idx = scan_int();
    skip_optional_equals();

    token_t t = get_raw_token();
    while (is_space_token(t)) t = get_raw_token();

    if (is_begin_group_token(t)) {
        token_list_t body = scan_braced_tokens(0);
        set_toks(idx, body, global_flag);
    } else {
        // \toks0=\toks1 style assignment
        meaning_t m = get_meaning(t);
        if (is_toks(m)) {
            int src_idx = scan_int();
            token_list_t copy;
            copy.tokens = clone_tokens(toks_regs[src_idx].tokens, toks_regs[src_idx].len);
            copy.len = toks_regs[src_idx].len;
            set_toks(idx, copy, global_flag);
        } else {
            die("Expected { or \\toks after \\toks N=");
        }
    }

    complete_assignment();
}

static void do_catcode_assignment(void) {
    int c = scan_int();
    skip_optional_equals();
    int cat = scan_int();
    if (charcode_valid(c) && catcode_valid(cat)) set_catcode(c, cat, global_flag);
    complete_assignment();
}

static void do_lccode_assignment(void) {
    int c = scan_int();
    skip_optional_equals();
    int val = scan_int();
    if (charcode_valid(c)) set_lccode_val(c, val, global_flag);
    complete_assignment();
}

static void do_uccode_assignment(void) {
    int c = scan_int();
    skip_optional_equals();
    int val = scan_int();
    if (charcode_valid(c)) set_uccode_val(c, val, global_flag);
    complete_assignment();
}

static void do_escapechar_assignment(void) {
    skip_optional_equals();
    escapechar = scan_int();
    complete_assignment();
}

static void do_endlinechar_assignment(void) {
    skip_optional_equals();
    endlinechar = scan_int();
    complete_assignment();
}

static void do_newlinechar_assignment(void) {
    skip_optional_equals();
    newlinechar = scan_int();
    complete_assignment();
}

static token_t skip_spaces_and_get_count(void) {
    token_t t = get_raw_token();
    while (is_space_token(t)) t = get_raw_token();
    if (!is_count(get_meaning(t))) die("Expected \\count");
    return t;
}

static void do_advance(void) {
    skip_spaces_and_get_count();
    int idx = scan_int();
    skip_optional_by();
    int val = scan_int();
    set_count(idx, count_regs[idx] + val, global_flag);
    complete_assignment();
}

static void do_multiply(void) {
    skip_spaces_and_get_count();
    int idx = scan_int();
    skip_optional_by();
    int val = scan_int();
    set_count(idx, count_regs[idx] * val, global_flag);
    complete_assignment();
}

static void do_divide(void) {
    skip_spaces_and_get_count();
    int idx = scan_int();
    skip_optional_by();
    int val = scan_int();
    if (val != 0) set_count(idx, count_regs[idx] / val, global_flag);
    complete_assignment();
}

static void do_input(void) {
    char filename[MAX_FILENAME];
    int len = 0;

    while (1) {
        token_t t = get_raw_token();
        if (!is_space_token(t)) { push_single_token(t); break; }
    }

    while (1) {
        token_t t = get_raw_token();
        if (is_space_token(t) || is_eof_token(t)) break;
        if (!is_char_token(t)) break;
        if (t.val == '\n' || t.val == '\r') break;
        if (len < MAX_FILENAME - 5) filename[len++] = t.val;
    }
    filename[len] = '\0';

    FILE *f = fopen(filename, "r");
    if (!f) {
        size_t flen = strlen(filename);
        if (flen + 4 < MAX_FILENAME) {
            strcat(filename, ".tex");
            f = fopen(filename, "r");
        }
    }
    if (f) push_file(f);
}

static void do_endinput(void) {
    input_state_t *s = find_file_input();
    if (s) s->endinput = 1;
}

static void do_aftergroup(void) {
    token_t t = get_raw_token();
    if (aftergroup_ptr >= MAX_AFTERGROUP) die("Aftergroup stack full");
    aftergroup_stack[aftergroup_ptr] = t;
    aftergroup_level[aftergroup_ptr] = level;
    aftergroup_ptr++;
}

static void do_afterassignment(void) {
    afterassignment_token = get_raw_token();
    afterassignment_pending = 1;
}

static void do_message(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\message expects {");
    token_list_t body = scan_braced_tokens(1);  // expand

    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i])) {
            putc(body.tokens[i].val, stderr);
        } else if (is_cs_token(body.tokens[i])) {
            fprintf(stderr, "\\%s ", names[body.tokens[i].val]);
        }
    }
    fprintf(stderr, "\n");

    if (body.tokens) free(body.tokens);
}

static void do_errmessage(void) {
    token_t t = get_raw_token();
    if (!is_begin_group_token(t)) die("\\errmessage expects {");
    token_list_t body = scan_braced_tokens(1);

    fprintf(stderr, "! ");
    for (size_t i = 0; i < body.len; i++) {
        if (is_char_token(body.tokens[i])) {
            putc(body.tokens[i].val, stderr);
        }
    }
    fprintf(stderr, "\n");

    if (body.tokens) free(body.tokens);
}

static void do_show(void) {
    token_t t = get_raw_token();
    meaning_t m = get_meaning(t);

    if (is_cs_token(t)) {
        fprintf(stderr, "> \\%s=%s.\n", names[t.val], meaning_to_string(m));
    } else {
        fprintf(stderr, "> %s.\n", meaning_to_string(m));
    }
}

static void do_showthe(void) {
    token_t t;
    while (1) { t = get_token(); if (!is_space_token(t)) break; }

    meaning_t m = get_meaning(t);

    if (is_count(m)) {
        int idx = scan_int();
        fprintf(stderr, "> %d.\n", count_regs[idx]);
    } else if (is_catcode_cmd(m)) {
        int c = scan_int();
        fprintf(stderr, "> %d.\n", catcodes[c]);
    } else {
        fprintf(stderr, "> (unknown).\n");
    }
}

// --- Dispatch table for process() ---

typedef void (*process_fn)(meaning_t);

static void ph_global(meaning_t m)      { global_flag = 1; (void)m; }
static void ph_long(meaning_t m)        { long_flag = 1; (void)m; }
static void ph_outer(meaning_t m)       { outer_flag = 1; (void)m; }
static void ph_protected(meaning_t m)   { protected_flag = 1; (void)m; }
static void ph_immediate(meaning_t m)   { (void)m; /* no-op */ }
static void ph_def(meaning_t m)         { scan_macro_definition(0, 0); (void)m; }
static void ph_gdef(meaning_t m)        { scan_macro_definition(1, 0); (void)m; }
static void ph_edef(meaning_t m)        { scan_macro_definition(0, 1); (void)m; }
static void ph_xdef(meaning_t m)        { scan_macro_definition(1, 1); (void)m; }
static void ph_let(meaning_t m)         { do_let(); (void)m; }
static void ph_futurelet(meaning_t m)   { do_futurelet(); (void)m; }
static void ph_chardef_cmd(meaning_t m) { do_chardef(); (void)m; }
static void ph_countdef(meaning_t m)    { do_countdef(); (void)m; }
static void ph_toksdef(meaning_t m)     { do_toksdef(); (void)m; }
static void ph_count(meaning_t m)       { do_count_assignment(); (void)m; }
static void ph_toks(meaning_t m)        { do_toks_assignment(); (void)m; }
static void ph_catcode(meaning_t m)     { do_catcode_assignment(); (void)m; }
static void ph_lccode(meaning_t m)      { do_lccode_assignment(); (void)m; }
static void ph_uccode(meaning_t m)      { do_uccode_assignment(); (void)m; }
static void ph_advance(meaning_t m)     { do_advance(); (void)m; }
static void ph_multiply(meaning_t m)    { do_multiply(); (void)m; }
static void ph_divide(meaning_t m)      { do_divide(); (void)m; }
static void ph_input(meaning_t m)       { do_input(); (void)m; }
static void ph_endinput(meaning_t m)    { do_endinput(); (void)m; }
static void ph_escapechar(meaning_t m)  { do_escapechar_assignment(); (void)m; }
static void ph_endlinechar(meaning_t m) { do_endlinechar_assignment(); (void)m; }
static void ph_newlinechar(meaning_t m) { do_newlinechar_assignment(); (void)m; }
static void ph_openin(meaning_t m)      { do_openin(); (void)m; }
static void ph_closein(meaning_t m)     { do_closein(); (void)m; }
static void ph_read(meaning_t m)        { do_read(); (void)m; }
static void ph_write(meaning_t m)       { do_write(); (void)m; }
static void ph_openout(meaning_t m)     { do_openout(); (void)m; }
static void ph_closeout(meaning_t m)    { do_closeout(); (void)m; }
static void ph_readline(meaning_t m)    { do_readline(); (void)m; }
static void ph_ignorespaces(meaning_t m){ do_ignorespaces(); (void)m; }
static void ph_globaldefs(meaning_t m)  { do_globaldefs_assignment(); (void)m; }
static void ph_tracingmacros(meaning_t m)   { do_tracingmacros_assignment(); (void)m; }
static void ph_tracingcommands(meaning_t m) { do_tracingcommands_assignment(); (void)m; }
static void ph_end(meaning_t m)         { do_end(); (void)m; }
static void ph_aftergroup(meaning_t m)  { do_aftergroup(); (void)m; }
static void ph_afterassignment(meaning_t m){ do_afterassignment(); (void)m; }
static void ph_message(meaning_t m)     { do_message(); (void)m; }
static void ph_errmessage(meaning_t m)  { do_errmessage(); (void)m; }
static void ph_show(meaning_t m)        { do_show(); (void)m; }
static void ph_showthe(meaning_t m)     { do_showthe(); (void)m; }
static void ph_lbrace(meaning_t m)      { level++; (void)m; }
static void ph_rbrace(meaning_t m)      { if (level > 0) restore_scope(); (void)m; }
static void ph_begingroup(meaning_t m)  { level++; (void)m; }
static void ph_endgroup(meaning_t m)    { if (level > 0) restore_scope(); (void)m; }
static void ph_relax(meaning_t m)       { (void)m; }
static void ph_chardef(meaning_t m)     { putchar(m.val); }
static void ph_macro(meaning_t m)       { expand_macro(m.val); }

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
        token_t t = get_token();
        if (is_eof_token(t)) break;
        meaning_t m = get_meaning(t);

        if (m.type >= 0 && m.type < M_TYPE_COUNT) {
            process_fn fn = process_dispatch[m.type];
            if (fn) {
                fn(m);
                if (m.type == M_END) break;
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
