#!/usr/bin/env -S ccraft
// mint - MINT — standalone string-processing core of Freemacs
//
// Implements the MINT macro language from Freemacs (Russ Nelson, 1988;
// https://en.wikipedia.org/wiki/Freemacs), an EMACS-like editor for
// MS-DOS whose extension language was also called MINT (MINT Is Not
// TRAC). This is the string-processing engine extracted and runnable
// standalone — same `#()`/`##()`/`()` syntax, same two-buffer scanner,
// same parameter marker system (bytes 129-255). Editor-specific
// builtins (buffer ops, file I/O, display, window management) are
// omitted; all string/arithmetic/bitwise/comparison/utility builtins
// are present.
//
// LANGUAGE — `#(name,args..)` calls a function, `##(...)` does not
// rescan the result, `(...)` protects from evaluation. First arg is the
// function name (2-letter builtin or user-defined string macro). Args
// are comma-separated, evaluated left-to-right. Numbers parse from the
// longest integer suffix of a string, preserving the non-numeric prefix
// in arithmetic results.
//
// BUILTINS (all called inside #(): `#(++,3,4)` = 7)
//   String:
//     #(ds,name,body)        — define or replace a named string
//     #(mp,name,pat..)       — replace patterns with parameter markers (129+)
//     #(gs,name,args..)      — get string, fill markers with args
//     #(go,name,alt)         — get one char from string pointer, alt if empty
//     #(gn,name,n,alt)       — get n chars, alt if not enough
//     #(fm,name,pat,alt)     — read from pointer to pattern, advance past it
//     #(rs,name)             — reset string pointer to start
//     #(es,name..)           — erase named strings
//     #(si,name,i)           — get character at position i (1-based)
//     #(nc,s)                — count characters in s
//     #(n?,name,y,n)         — does name exist? returns y or n
//     #(ls,sep,prefix)       — list defined strings matching prefix
//     #(sa,a,b,..)           — sort ascending, return comma-separated
//     #(bc,val,from,to)      — base conversion: A/D/H/O/B
//   Arithmetic (preserves first operand's prefix):
//     #(++,a,b)  #(--,a,b)  #(**,a,b)  #(//,a,b)  #(%%,a,b)
//   Bitwise:  #(||,a,b)  #(&&,a,b)  #(^^,a,b)
//   Compare (returns t/f or ""):
//     #(g?,a,b,t,f)  #(==,a,b,t,f)  #(a?,a,b,t,f)
//   File I/O:
//     #(rf,name)             — read file, return content
//     #(wf,name,content)     — write content to file
//     #(ff,pattern,sep)      — find files matching glob pattern
//     #(rn,old,new)          — rename file
//     #(de,name)             — delete file
//   Time:
//     #(ct,)                 — current time string
//     #(ct,filename)         — file modification time
//   Library:
//     #(ll,filename)         — load string definitions from file
//     #(sl,filename,str..)   — save named strings to file
//   Execute:
//     #(ex,prog,args)        — run program, capture stdout
//   Control:
//     #(hl,N)                — halt with exit code N (default 0)
//   Editor buffer:
//     #(ba,N)                — allocate/reset buffer (size N or clear)
//     #(bi,N,M,E)            — insert string E into buffer at pos M
//     #(is,S,E)              — insert string E at current point
//     #(tr,M,S)              — replace region [point,mark) with S
//     #(dm,M)                — delete region [point,mark), return text
//   Marks:
//     #(pm,push|pop)         — push point onto ring / pop back
//     #(sm,M,V)              — set mark M to position V
//     #(sp,M)                — set point to position M
//     #(rm,M,V)              — read mark M, set to V, return old
//     #(rc,M)                — get/set repeat count
//     #(mb,M,Y,N)            — set mark to current point
//   Display (standalone prints to stderr or no-ops):
//     #(an,L,F,R)            — announce F to stderr
//     #(rd,F)                — redisplay (no-op)
//     #(xy,X,Y)              — set screen position
//     #(ow,S)                — overwrite screen (print to stderr)
//     #(pp)                  — return current point position
//   Search (in editor buffer):
//     #(lp,S,N,R,F)          — look pattern from point
//     #(lk,S,E,F,L,N)        — look string from point
//     #(l?,S,E,F,L,Y,N)      — look & test, return Y/N
//   Environment:
//     #(ev)                  — set up env.* strings from environ
//   I/O:
//     #(it,T)                — read from stdin (ignore timeout)
//     #(lv,F)                — load file F into string named F
//     #(sv,F,V)              — save string V to file named F
//   Debug:
//     #(db)                  — print processor state to stderr
//
// NOT IMPLEMENTED: sc(spell check), st(syntax table) — editor/DOS specific
//
//
// PARAMETER MARKERS (user-defined macros)
// #(mp,name,p1,p2..) scans the string body and replaces each occurrence
// of p1 with marker 129, p2 with 130, etc. Later, #(gs,name,a1,a2..)
// replaces 129→a1, 130→a2, ... and returns the result. Calling any
// undefined name as a function (e.g. #(myfn,a1..)) fills markers
// 1=myfn, 2=a1, 3=a2, ... and evaluates.
//
// HOW IT WORKS
// Two buffers: active (input being scanned) and neutral (output
// building). Scanner reads active left-to-right. Paren-protected
// content and literals go to neutral. #(...) extracts args from
// neutral, dispatches builtin or user fn, places result back on
// active (rescan) or neutral (literal). ##(...) forces literal.
// Marker bytes (129-255) represent parameter slots in string bodies.
//
// EXAMPLES
//   echo '#(++,3,4)' | mint               # 7
//   echo '#(nc,hello world)' | mint       # 11
//   echo '#(sa,c,b,a)' | mint             # a,b,c
//   echo '#(==,abc,abc,yes,no)' | mint    # yes
//   echo '#(ds,g,Hello )#(gs,g,World)' | mint  # Hello World
//
// USAGE
//   mint [file]
//
// EXIT CODES
//   0  success  1  file error  N  from #(hl,N)
//
// DEPENDENCIES
//   ccraft — shebang-based build
//   libc  — POSIX strdup (needs _POSIX_C_SOURCE 200809L)
//
// REFERENCE
//   Freemacs — https://en.wikipedia.org/wiki/Freemacs
//          Russ Nelson (1988). EMACS-like editor for MS-DOS. This file
//          implements its MINT macro language as a standalone processor.
//   MINTREF.ELI — https://github.com/rutgers-nbiz/freemacs
//          Original reference manual for MINT builtins.
//   TRAC — https://en.wikipedia.org/wiki/TRAC_(programming_language)
//          Mooers, Calvin (1960s). \'#(fn,args)\' syntax and rescan
//          semantics that MINT inherits.
//
// ============================================================================
// MINT TUTORIAL
// ============================================================================
//
// WHAT IS MINT?
// ============================================================================
//
// Imagine you have a string of text. MINT scans it left-to-right,
// calls functions it finds, and replaces them with their results.
// The result is a new string. That's all MINT does:
// string in -> transform -> string out.
//
// ============================================================================
// THE THREE RULES
// ============================================================================
//
// 1. `#(name,args..)` calls a function and RESCANS the result
// 2. `##(name,args..)` calls a function but does NOT rescan
// 3. `(...)` protects the contents from evaluation
//
// Everything else is copied literally to the output.
//
// ============================================================================
// SIMPLE MATH
// ============================================================================
//
//     #(++,3,4)        outputs: 7
//     #(++,a10,5)      outputs: a15   (preserves prefix)
//     #(--,10,3)       outputs: 7
//     #(**,3,4)        outputs: 12
//
// ============================================================================
// WHY RESCAN MATTERS
// ============================================================================
//
// `#(...)` rescans the result. This means functions can nest:
//
//     #(++,#(++,1,2),3)         -> inner ++ makes 3, outer ++ makes 6
//     #(nc,#(++,5,5))           -> ++ makes 10, nc counts "10" = 2
//
// `##(...)` does NOT rescan. Use it when the result is data, not code:
//
//     ##(++,3,4)                outputs: 7 (but 7 is just text now)
//
// ============================================================================
// STRING STORAGE
// ============================================================================
//
// MINT stores named strings. Think of them as variables that hold text.
//
//     #(ds,greeting,Hello)      -- define string "greeting" = "Hello"
//     #(gs,greeting,)            -- get string "greeting" = "Hello"
//     #(n?,greeting,yes,no)     -- does it exist? "yes"
//     #(es,greeting)             -- erase it
//
// ============================================================================
// PARAMETER MARKERS (THE TRICKY PART)
// ============================================================================
//
// This is what makes MINT powerful: you can create TEMPLATES with holes.
//
// Step 1: Define a string with placeholder text:
//
//     #(ds,greet,Hello $ world)
//     -- "greet" = "Hello $ world"
//
// Step 2: Replace the placeholder with a parameter marker:
//
//     #(mp,greet,$)
//     -- scans "Hello $ world", finds $
//     -- replaces $ with byte 129 (parameter marker #1)
//     -- "greet" now = "Hello " + marker_129 + " world"
//
// Step 3: Fill the marker with actual text:
//
//     #(gs,greet,dear)
//     -- fills marker 129 with "dear"
//     -- returns "Hello dear world"
//
// Multiple markers work the same way:
//
//     #(ds,fmt,$1 and $2 and $3)#(mp,fmt,$1,$2,$3)#(gs,fmt,A,B,C)
//     -- fills markers 129->A, 130->B, 131->C
//     -- returns "A and B and C"
//
// ============================================================================
// USER-DEFINED FUNCTIONS
// ============================================================================
//
// Call any undefined name as a function. It fills marker 1 with the
// function name, marker 2 with the first argument, marker 3 with the
// second, etc.:
//
//     #(ds,double,$ doubled is $$)
//     #(mp,double,$,$$)
//     #(double,5)
//     -- marker 1 = "double" (the function name)
//     -- marker 2 = "5" (the first argument)
//     -- result: "double doubled is 5"
//
// This is how Freemacs scripts define reusable macros.
//
// ============================================================================
// STRING POINTER: SEQUENTIAL READING
// ============================================================================
//
// Each string has a pointer that you can read through one char at a time.
//
//     #(ds,x,hello)
//     #(go,x,z)      -- get one char: "h", pointer moves to "e"
//     #(go,x,z)      -- get one char: "e"
//     #(gn,x,2,z)    -- get 2 chars: "ll"
//     #(fm,x,l,z)    -- find "l": returns "" (already past first l)
//     #(rs,x)        -- reset pointer to start
//     #(go,x,z)      -- get one char: "h"
//
// If the pointer reaches the end, the "alt" value is returned (active).
//
// ============================================================================
// COMPARISON: CONDITIONAL RESULTS
// ============================================================================
//
// Comparisons return one of two strings depending on the result:
//
//     #(g?,5,3,big,small)        -- "big"   (5 > 3)
//     #(==,abc,abc,equal,diff)   -- "equal"
//     #(a?,ant,zebra,first,last) -- "first" (ant < zebra)
//
// ============================================================================
// FILE I/O
// ============================================================================
//
//     #(rf,file.txt)             -- read file content
//     #(wf,out.txt,Hello)        -- write "Hello" to out.txt
//     #(rn,old.txt,new.txt)      -- rename file
//     #(de,trash.txt)            -- delete file
//     #(ff,*.txt,|)              -- find files: "a.txt|b.txt"
//
// ============================================================================
// LISTS: SORT, LIST, COUNT
// ============================================================================
//
//     #(sa,c,b,a)                -- sort: "a,b,c" (commas lost in rescan)
//     ##(sa,c,b,a)               -- sort: "a,b,c" (commas preserved)
//     #(nc,hello)                -- count: 5
//     #(ls,|,test)               -- list strings starting with "test"
//     #(n?,name,y,n)             -- name exists?
//
// ============================================================================
// BASE CONVERSION
// ============================================================================
//
//     #(bc,65,D,A)               -- 65 decimal -> "A" (ASCII)
//     #(bc,FF,H,D)               -- FF hex -> 255
//     #(bc,255,D,H)              -- 255 decimal -> "FF"
//
// ============================================================================
// EXECUTING PROGRAMS
// ============================================================================
//
//     #(ex,echo,hello)           -- runs "echo hello", captures stdout
//     #(hl,0)                    -- halt with exit code 0
//
// ============================================================================
// EDITOR BUFFER (STANDALONE)
// ============================================================================
//
// MINT includes a text buffer with point and mark:
//
//     #(ba,1000)                 -- allocate buffer
//     #(is,,Hello World)         -- insert at point
//     #(sp,0)                    -- set point to 0
//     #(pp)                      -- pick position (returns point)
//     #(sm,0,5)                  -- set mark to position 5
//     #(dm,,)                    -- delete region [point,mark)
//     #(tr,,replacement)         -- replace region with text
//     #(lp,pattern,,,,0)         -- search forward for pattern
//     #(lp,pattern,,,,1)         -- search backward
//     #(l?,pat,,,,y,n)           -- search, return y/n
//
// ============================================================================
// QUICK REFERENCE
// ============================================================================
//
// STRINGS:     ds mp gs go gn fm rs es si
// ARITHMETIC:  ++ -- ** // %% |  | && ^^
// COMPARE:     g? == a?
// UTILITY:     nc n? ls sa bc hl
// FILE I/O:    rf wf ff rn de
// TIME:        ct
// LIBRARY:     ll sl
// EXECUTE:     ex
// BUFFER:      ba bi is tr dm
// MARKS:       pm sm sp rm rc mb
// DISPLAY:     an rd xy ow pp
// SEARCH:      lp lk l?
// ENV:         ev
// DEBUG:       db
// INPUT:       it
// VAR I/O:     lv sv
// NOT IMPL:    sc st
//
// USAGE: echo 'EXPR' | mint   or   mint file.mt

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <glob.h>

#define MAX_BUF     65536
#define MAX_BUF_SIZE MAX_BUF
#define MAX_ARGS    128
#define MAX_STRINGS 1024
#define PARAM_BASE  128
#define INT64_DIGITS 20

// ---------------------------------------------------------------------------
// Character predicates - test single character properties
// ---------------------------------------------------------------------------

static int is_tab(char c)     { return c == '\t'; }
static int is_cr(char c)      { return c == '\r'; }
static int is_lf(char c)      { return c == '\n'; }
static int is_comma(char c)   { return c == ','; }
static int is_hash(char c)    { return c == '#'; }
static int is_open(char c)    { return c == '('; }
static int is_close(char c)   { return c == ')'; }
static int is_plus(char c)    { return c == '+'; }
static int is_minus(char c)   { return c == '-'; }
static int is_digit(char c)   { return c >= '0' && c <= '9'; }
static int is_sign(char c)    { return is_plus(c) || is_minus(c); }
static int is_skip(char c)    { return is_tab(c) || is_cr(c) || is_lf(c); }
static int is_marker(char c)  { return (unsigned char)c >= PARAM_BASE + 1; }

// has_marker_at - check if any byte in range [pos, pos+len) is a marker
static int
has_marker_at(const char *s, size_t pos, size_t len) {
    for (size_t k = 0; k < len; k++) {
        if (is_marker(s[pos + k])) {
            return 1;
        }
    }
    return 0;
}

// String predicates - domain questions about text
static int eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

// ---------------------------------------------------------------------------
// String storage - named strings with body and read pointer
//
// MINT stores named strings that can be retrieved and manipulated.
// Each string has a "pointer" for sequential reading via go/gn/fm.
// ---------------------------------------------------------------------------

struct str {
    char *name;     // string identifier
    char *body;     // string content (may contain parameter markers)
    size_t ptr;     // read pointer position
};

static struct str strs[MAX_STRINGS];
static size_t nstrs;

// str_find - look up a string by name, return NULL if not found
static struct str *
str_find(const char *name) {
    for (size_t i = 0; i < nstrs; i++) {
        if (eq(strs[i].name, name)) {
            return &strs[i];
        }
    }
    return NULL;
}

// str_create - create or replace a string with given name and body
static struct str *
str_create(const char *name, const char *body) {
    struct str *s = str_find(name);
    if (s == NULL) {
        if (nstrs >= MAX_STRINGS) {
            return NULL;
        }
        s = &strs[nstrs++];
        s->name = strdup(name);
        if (s->name == NULL) return NULL;
    } else {
        free(s->body);
    }
    s->body = strdup(body);
    if (s->body == NULL) return NULL;
    s->ptr = 0;
    return s;
}

// str_erase - remove a string from storage
static void
str_erase(const char *name) {
    for (size_t i = 0; i < nstrs; i++) {
        if (eq(strs[i].name, name)) {
            free(strs[i].name);
            free(strs[i].body);
            strs[i] = strs[--nstrs];
            return;
        }
    }
}

// String predicates and operations
static int has_string(const char *name)      { return str_find(name) != NULL; }
static int str_exhausted(struct str *s)      { return s->ptr >= strlen(s->body); }
static int str_has_remaining(struct str *s, size_t n) { return s->ptr + n <= strlen(s->body); }
static void str_reset(struct str *s)         { s->ptr = 0; }
static char str_read(struct str *s)          { return s->body[s->ptr++]; }

// Pattern predicates
static int is_empty_pattern(const char *s) { return s == NULL || s[0] == '\0'; }

// Safe append — bounds-checked string copy to output buffer
static void
safe_append(char *out, size_t *pos, size_t sz, const char *s, size_t len) {
    if (len >= sz - *pos - 1) { return; }

    memcpy(out + *pos, s, len);
    *pos += len;
}

// Scan predicates — active buffer content at head
static int starts_active_call(const char *buf)  { return is_hash(buf[0]) && is_open(buf[1]); }
static int starts_neutral_call(const char *buf) { return is_hash(buf[0]) && is_hash(buf[1]) && is_open(buf[2]); }

// ---------------------------------------------------------------------------
// Active/neutral buffers - the MINT scanning state
//
// MINT maintains two buffers during evaluation:
// - active: input being scanned (consumed from left)
// - neutral: accumulated output (built from left)
//
// The scanner moves characters from active to neutral, except when
// it encounters function calls #(...) or ##(...).
// ---------------------------------------------------------------------------

static char active[MAX_BUF];    // input buffer (right of scan pointer)
static char neutral[MAX_BUF];   // output buffer (left of scan pointer)
static size_t alen, nlen;       // current lengths

// Active buffer operations
static int  active_empty(void)               { return alen == 0; }
static char active_peek(void)                { return active[0]; }
static int  active_has(size_t n)             { return alen >= n; }

// active_consume - remove n characters from front of active buffer
static void
active_consume(size_t n) {
    if (n > alen) n = alen;
    memmove(active, active + n, alen - n + 1);
    alen -= n;
}

// active_prepend - insert string at front (for rescanning results)
static void
active_prepend(const char *s, size_t len) {
    if (len + alen >= MAX_BUF) return;
    memmove(active + len, active, alen + 1);
    memcpy(active, s, len);
    alen += len;
}

// neutral_append - add string to end of neutral buffer
static void
neutral_append(const char *s, size_t len) {
    if (nlen + len >= MAX_BUF) {
        len = MAX_BUF - nlen - 1;
    }
    memcpy(neutral + nlen, s, len);
    nlen += len;
    neutral[nlen] = '\0';
}

// neutral_put - add single character to neutral buffer
static void
neutral_put(char c) {
    if (nlen < MAX_BUF - 1) {
        neutral[nlen++] = c;
        neutral[nlen] = '\0';
    }
}

// neutral_truncate - discard everything after position pos
static void
neutral_truncate(size_t pos) {
    nlen = pos;
    neutral[nlen] = '\0';
}

// ---------------------------------------------------------------------------
// Function markers and argument tracking
//
// When we see #( or ##(, we record a "marker" at the current neutral
// position. When we see ), we pop the marker and extract arguments.
// Commas between markers define argument boundaries.
// ---------------------------------------------------------------------------

struct marker {
    size_t pos;     // position in neutral where function args start
    int rescan;     // 1 = rescan result (#), 0 = don't rescan (##)
};

static struct marker marks[MAX_ARGS];
static size_t nmarks;

static size_t argpos[MAX_ARGS];     // positions of commas (argument separators)
static size_t nargpos;

// mark_func - record start of a function call
static void mark_func(size_t pos, int rescan) {
    if (nmarks >= MAX_ARGS) { return; }

    marks[nmarks].pos = pos;
    marks[nmarks].rescan = rescan;
    nmarks++;
}

// mark_arg - record position of argument separator (comma)
static void mark_arg(void) {
    if (nargpos >= MAX_ARGS) { return; }

    argpos[nargpos++] = nlen;
}

// has_marks - check if we're inside any function call
static int has_marks(void) { return nmarks > 0; }

// ---------------------------------------------------------------------------
// MINT number parsing
//
// MINT numbers are the longest suffix matching [+-]?[0-9]+
// Examples: "abc123" -> 123, "x-5" -> -5, "foo" -> 0
// The non-numeric prefix is preserved in arithmetic results.
// ---------------------------------------------------------------------------

// num_parse - extract numeric value and prefix length from string
static long
num_parse(const char *s, size_t *prefix) {
    size_t len = strlen(s);
    if (len == 0) {
        *prefix = 0;
        return 0;
    }

    // Scan backwards to find start of numeric suffix
    size_t start = len;
    for (size_t i = len; i > 0; i--) {
        size_t k = i - 1;
        if (is_digit(s[k])) {
            start = k;
        } else if (is_sign(s[k]) && i < len && is_digit(s[i])) {
            start = k;
            break;
        } else {
            break;
        }
    }

    *prefix = start;
    return (start == len) ? 0 : strtol(s + start, NULL, 10);
}

// num_val - extract numeric value, discard prefix
static long
num_val(const char *s) {
    size_t prefix;
    return num_parse(s, &prefix);
}

// num_format - format result with original prefix preserved
static void
num_format(char *out, size_t sz, const char *a, long val) {
    size_t plen;
    num_parse(a, &plen);
    if (plen > 0 && plen < sz - INT64_DIGITS) {
        memcpy(out, a, plen);
        snprintf(out + plen, sz - plen, "%ld", val);
    } else {
        snprintf(out, sz, "%ld", val);
    }
}

// ---------------------------------------------------------------------------
// Parameter filling
//
// After #(mp,...) creates parameter markers in a string body,
// #(gs,...) or default calls fill those markers with arguments.
// Markers are bytes 129-255 mapping to argument indices 0-126.
// ---------------------------------------------------------------------------

// params_fill - replace parameter markers with actual arguments
static void
params_fill(char *out, size_t sz, const char *body, char **args, size_t n) {
    size_t j = 0;
    for (size_t i = 0; body[i] && j < sz - 1; i++) {
        if (is_marker(body[i])) {
            size_t k = (unsigned char)body[i] - PARAM_BASE - 1;
            if (k < n && args[k]) {
                safe_append(out, &j, sz, args[k], strlen(args[k]));
            }
        } else {
            out[j++] = body[i];
        }
    }
    out[j] = '\0';
}

// ---------------------------------------------------------------------------
// Built-in primitives
//
// All builtins have signature: (args, nargs, out, outsz, ret_active)
// - args: array of argument strings (after function name)
// - nargs: number of arguments
// - out: output buffer for result
// - outsz: size of output buffer
// - ret_active: set to 1 to force rescan even in ## mode
// ---------------------------------------------------------------------------

typedef void (*builtin_fn)(char**, size_t, char*, size_t, int*);

// --- Binary arithmetic macro ---
// Generates two-operand builtins differing only by operator
#define arith2(name, op) \
static void \
name(char **a, size_t n, char *out, size_t sz, int *ra) { \
    (void)ra; out[0] = '\0'; \
    if (n < 2) return; \
    size_t p1, p2; \
    num_format(out, sz, a[0], num_parse(a[0], &p1) op num_parse(a[1], &p2)); \
}

// truth_out - select true/false result based on condition
static void
truth_out(char *out, size_t sz, int cond, char **a, size_t n) {
    const char *r = cond ? (n > 2 ? a[2] : "") : (n > 3 ? a[3] : "");
    strncpy(out, r, sz - 1); out[sz - 1] = '\0';
}

// replace_text_with_marker - find and replace pattern text with marker byte
// SIDEFF: modifies body in-place, updates blen
static void
replace_text_with_marker(char *body, size_t *blen, const char *pat, size_t plen, unsigned char marker) {
    for (size_t i = 0; i + plen <= *blen; ) {
        if (has_marker_at(body, i, plen)) { i++; continue; }
        if (memcmp(body + i, pat, plen) == 0) {
            memmove(body + i + 1, body + i + plen, *blen - i - plen + 1);
            body[i] = marker;
            *blen = *blen - plen + 1;
        }
        i++;
    }
}

// --- String operations ---

// b_ds - #(ds,name,body) - define string
// Creates a named string with the given body. Replaces if exists.
static void
b_ds(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)sz; (void)ra;
    out[0] = '\0';
    if (n >= 1) str_create(a[0], n > 1 ? a[1] : "");
}

// b_mp - #(mp,name,p1,p2,...) - make parameters
// Replaces occurrences of p1,p2,... in string body with parameter markers.
// Marker 129 = p1, 130 = p2, etc.
// NOTE: patterns are processed left-to-right in order. If pattern A is
// a prefix of pattern B, replacing A will destroy later occurrences of
// B in the same region. Order longer patterns before shorter ones.
// SIDEFF: modifies string body in-place
static void
b_mp(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)sz; (void)ra;
    out[0] = '\0';
    if (n < 1) return;

    struct str *s = str_find(a[0]);
    if (s == NULL) return;

    char *body = s->body;
    size_t blen = strlen(body);

    for (size_t pi = 1; pi < n && pi < MAX_ARGS; pi++) {
        if (is_empty_pattern(a[pi])) continue;
        replace_text_with_marker(body, &blen, a[pi], strlen(a[pi]), (unsigned char)(PARAM_BASE + pi));
    }
    s->ptr = 0;
}

// b_gs - #(gs,name,a1,a2,...) - get string with parameters filled
// Returns body of string with markers replaced by arguments.
static void
b_gs(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 1) return;
    struct str *s = str_find(a[0]);
    if (s) params_fill(out, sz, s->body, a + 1, n - 1);
}

// b_go - #(go,name,z) - get one character
// Returns next char from string pointer. If exhausted, returns z (active).
static void
b_go(char **a, size_t n, char *out, size_t sz, int *ra) {
    out[0] = '\0';
    if (n < 1) return;

    struct str *s = str_find(a[0]);
    if (s == NULL || str_exhausted(s)) {
        if (n > 1) { strncpy(out, a[1], sz - 1); out[sz-1] = '\0'; *ra = 1; }
        return;
    }
    out[0] = str_read(s);
    out[1] = '\0';
}

// b_gn - #(gn,name,count,z) - get n characters
// Returns count chars from string pointer. If not enough, returns z (active).
static void
b_gn(char **a, size_t n, char *out, size_t sz, int *ra) {
    out[0] = '\0';
    if (n < 2) return;

    long d = num_val(a[1]);
    if (d == 0) return;
    size_t ud = d > 0 ? (size_t)d : 0;

    struct str *s = str_find(a[0]);
    if (s == NULL || !str_has_remaining(s, ud)) {
        if (n > 2) { strncpy(out, a[2], sz - 1); out[sz-1] = '\0'; *ra = 1; }
        return;
    }

    size_t len = ud < sz - 1 ? ud : sz - 1;
    memcpy(out, s->body + s->ptr, len);
    out[len] = '\0';
    s->ptr += ud;
}

// b_fm - #(fm,name,pattern,z) - find first match
// Returns chars from pointer to pattern. Pointer moves past pattern.
// If not found, returns z (active).
static void
b_fm(char **a, size_t n, char *out, size_t sz, int *ra) {
    out[0] = '\0';
    if (n < 2) return;

    struct str *s = str_find(a[0]);
    if (s == NULL) {
        if (n > 2) { strncpy(out, a[2], sz - 1); out[sz-1] = '\0'; *ra = 1; }
        return;
    }

    const char *pat = a[1];
    size_t plen = strlen(pat);
    if (is_empty_pattern(pat)) return;

    size_t blen = strlen(s->body);
    size_t found = blen;

    for (size_t i = s->ptr; i + plen <= blen; i++) {
        if (has_marker_at(s->body, i, plen)) continue;
        if (memcmp(s->body + i, pat, plen) == 0) { found = i; break; }
    }

    if (found == blen) {
        if (n > 2) { strncpy(out, a[2], sz - 1); out[sz-1] = '\0'; *ra = 1; }
        return;
    }

    size_t len = found - s->ptr;
    if (len >= sz) len = sz - 1;
    memcpy(out, s->body + s->ptr, len);
    out[len] = '\0';
    s->ptr = found + plen;
}

// b_rs - #(rs,name) - restore string pointer to start
static void
b_rs(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)sz; (void)ra;
    out[0] = '\0';
    if (n >= 1) {
        struct str *s = str_find(a[0]);
        if (s) str_reset(s);
    }
}

// b_es - #(es,n1,n2,...) - erase strings
static void
b_es(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)sz; (void)ra;
    out[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (a[i]) {
            str_erase(a[i]);
        }
    }
}

// b_si - #(si,name,index) - get character at index (1-based)
static void
b_si(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)sz; (void)ra;
    out[0] = '\0';
    if (n < 2) return;
    struct str *s = str_find(a[0]);
    if (s == NULL) return;

    long c = num_val(a[1]);
    if (c <= 0) return;

    size_t idx = (size_t)(c - 1);
    if (idx < strlen(s->body)) {
        out[0] = s->body[idx];
        out[1] = '\0';
    }
}

// --- Arithmetic operations ---

arith2(b_add, +)
arith2(b_sub, -)
arith2(b_mul, *)
arith2(b_or, |)
arith2(b_and, &)
arith2(b_xor, ^)

// b_div - #(//,a,b) - integer division (0 if b=0)
static void
b_div(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 2) return;
    size_t p1, p2;
    long x = num_parse(a[0], &p1), y = num_parse(a[1], &p2);
    num_format(out, sz, a[0], y == 0 ? 0 : x / y);
}

// b_mod - #(%%,a,b) - modulo (0 if b=0)
static void
b_mod(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 2) return;
    size_t p1, p2;
    long x = num_parse(a[0], &p1), y = num_parse(a[1], &p2);
    num_format(out, sz, a[0], y == 0 ? 0 : x % y);
}

// --- Comparison operations ---

// b_gt - #(g?,a,b,t,f) - numeric greater than: returns t if a>b, else f
static void
b_gt(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 2) return;
    size_t p1, p2;
    truth_out(out, sz, num_parse(a[0], &p1) > num_parse(a[1], &p2), a, n);
}

// b_eq - #(==,a,b,t,f) - string equality: returns t if a==b, else f
static void
b_eq(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 2) return;
    truth_out(out, sz, eq(a[0], a[1]), a, n);
}

// b_lt - #(a?,a,b,t,f) - alphabetic less than: returns t if a<b, else f
static void
b_lt(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 2) return;
    truth_out(out, sz, strcmp(a[0], a[1]) < 0, a, n);
}

// --- Utility operations ---

// b_nc - #(nc,s) - number of characters in s
static void
b_nc(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    snprintf(out, sz, "%zu", n >= 1 ? strlen(a[0]) : 0);
}

// b_nq - #(n?,name,y,n) - name exists? returns y if string exists, else n
// Note: args are (name,y,n), not (a,b,t,f) like comparisons — uses own logic
static void
b_nq(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 1) return;
    const char *r = has_string(a[0]) ? (n > 1 ? a[1] : "") : (n > 2 ? a[2] : "");
    strncpy(out, r, sz - 1); out[sz - 1] = '\0';
}

// b_ls - #(ls,sep,prefix) - list strings with given prefix, separated by sep
static void
b_ls(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    const char *sep = n > 0 ? a[0] : "";
    const char *pre = n > 1 ? a[1] : "";
    size_t j = 0;
    int first = 1;

    for (size_t i = 0; i < nstrs && j < sz - 1; i++) {
        if (!starts(strs[i].name, pre)) continue;
        if (!first) {
            safe_append(out, &j, sz, sep, strlen(sep));
        }
        first = 0;
        safe_append(out, &j, sz, strs[i].name, strlen(strs[i].name));
    }
    out[j] = '\0';
}

// val_from_base - parse string v in given base (A=ascii, D/H/O/B)
static long
val_from_base(const char *v, char base) {
    if (base == 'A') return v[0] ? (unsigned char)v[0] : 0;
    if (base == 'D') return strtol(v, NULL, 10);
    if (base == 'H') return strtol(v, NULL, 16);
    if (base == 'O') return strtol(v, NULL, 8);
    if (base == 'B') return strtol(v, NULL, 2);
    return 0;
}

// fmt_to_base - format value to given base (A=ascii, D/H/O/B)
static void
fmt_to_base(long val, char base, char *out, size_t sz) {
    if (base == 'A') { out[0] = (char)val; out[1] = '\0'; }
    else if (base == 'D') snprintf(out, sz, "%ld", val);
    else if (base == 'H') snprintf(out, sz, "%lX", val);
    else if (base == 'O') snprintf(out, sz, "%lo", val);
    else if (base == 'B') {
        if (val == 0) { out[0] = '0'; out[1] = '\0'; return; }
        char tmp[65]; int i = 0;
        unsigned long u = val < 0 ? (unsigned long)-val : (unsigned long)val;
        while (u && i < 64) { tmp[i++] = '0' + (u & 1); u >>= 1; }
        size_t j = 0;
        if (val < 0 && j < sz - 1) out[j++] = '-';
        while (i > 0 && j < sz - 1) out[j++] = tmp[--i];
        out[j] = '\0';
    }
}

// b_bc - #(bc,value,from,to) - base conversion
// Bases: A=ascii, D=decimal, H=hex, O=octal, B=binary
static void
b_bc(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 1) return;
    char from = (char)(n > 1 && a[1][0] ? toupper((unsigned char)a[1][0]) : 'A');
    char to   = (char)(n > 2 && a[2][0] ? toupper((unsigned char)a[2][0]) : 'D');
    long val = val_from_base(a[0], from);
    fmt_to_base(val, to, out, sz);
}

// cmp_str - comparison function for qsort
static int
cmp_str(const void *x, const void *y) {
    return strcmp(*(const char **)x, *(const char **)y);
}

// b_sa - #(sa,a,b,c,...) - sort ascending, return comma-separated
static void
b_sa(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n == 0) return;

    char *sorted[MAX_ARGS];
    for (size_t i = 0; i < n && i < MAX_ARGS; i++) sorted[i] = a[i];
    qsort(sorted, n, sizeof(char *), cmp_str);

    size_t j = 0;
    for (size_t i = 0; i < n && j < sz - 1; i++) {
        if (i > 0 && j < sz - 1) out[j++] = ',';
        safe_append(out, &j, sz, sorted[i], strlen(sorted[i]));
    }
    out[j] = '\0';
}

// b_hl - #(hl,code) - halt with exit code
static void
b_hl(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)out; (void)sz; (void)ra;
    exit((int)(n > 0 ? num_val(a[0]) : 0));
}

// --- File I/O operations ---

// b_rf - #(rf,name) - read file, return content
// SIDEFF: reads file from disk
static void
b_rf(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 1) return;
    FILE *f = fopen(a[0], "rb");
    if (f == NULL) { strncpy(out, "File not found", sz - 1); out[sz-1] = '\0'; return; }
    size_t pos = 0;
    int c;
    while ((c = fgetc(f)) != EOF && pos < sz - 1) out[pos++] = (char)c;
    out[pos] = '\0';
    fclose(f);
}

// b_wf - #(wf,name,content) - write content to file
// SIDEFF: writes file to disk
static void
b_wf(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 2) return;
    FILE *f = fopen(a[0], "wb");
    if (f == NULL) { strncpy(out, "Write error", sz - 1); out[sz-1] = '\0'; return; }
    fwrite(a[1], 1, strlen(a[1]), f);
    fclose(f);
}

// b_rn - #(rn,old,new) - rename file
// SIDEFF: renames file on disk
static void
b_rn(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 2) return;
    if (rename(a[0], a[1]) != 0) {
        strncpy(out, "Rename error", sz - 1); out[sz-1] = '\0';
    }
}

// b_de - #(de,name) - delete file
// SIDEFF: deletes file from disk
static void
b_de(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 1) return;
    if (unlink(a[0]) != 0) {
        strncpy(out, "File not found", sz - 1); out[sz-1] = '\0';
    }
}

// b_ff - #(ff,pattern,sep) - find files matching pattern
// SIDEFF: reads directory entries from disk
static void
b_ff(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 1) return;
    glob_t g;
    int ret = glob(a[0], 0, NULL, &g);
    if (ret != 0) { out[0] = '\0'; return; }
    const char *sep = n > 1 ? a[1] : ",";
    size_t j = 0;
    for (size_t i = 0; i < g.gl_pathc && j < sz - 1; i++) {
        if (i > 0) {
            safe_append(out, &j, sz, sep, strlen(sep));
        }
        safe_append(out, &j, sz, g.gl_pathv[i], strlen(g.gl_pathv[i]));
    }
    out[j] = '\0';
    globfree(&g);
}

// b_ct - #(ct,F) - current time, or file modification time
static void
b_ct(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n > 0 && a[0][0] != '\0') {
        // File modification time
        struct stat st;
        if (stat(a[0], &st) == 0) {
            struct tm *t = localtime(&st.st_mtime);
            if (t) strftime(out, sz - 1, "%a %b %d %H:%M:%S %Y", t);
        }
        return;
    }
    // Current time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) strftime(out, sz - 1, "%a %b %d %H:%M:%S %Y", t);
}

// b_ex - #(ex,prog,args,stdin,stdout,stderr) - execute program
// SIDEFF: spawns subprocess
static void
b_ex(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 1) return;
    const char *prog = a[0];
    const char *args = n > 1 ? a[1] : "";
    char cmd[MAX_BUF];
    snprintf(cmd, sizeof(cmd), "%s %s", prog, args);
    FILE *p = popen(cmd, "r");
    if (p == NULL) { strncpy(out, "Exec error", sz - 1); out[sz-1] = '\0'; return; }
    size_t pos = 0;
    int c;
    while ((c = fgetc(p)) != EOF && pos < sz - 1) out[pos++] = (char)c;
    out[pos] = '\0';
    pclose(p);
}

// ll_format - write a 4-byte little-endian length prefix
static void
ll_putlen(FILE *f, size_t n) {
    putc(n & 0xFF, f);
    putc((n >> 8) & 0xFF, f);
    putc((n >> 16) & 0xFF, f);
    putc((n >> 24) & 0xFF, f);
}

// ll_getlen - read 4-byte little-endian length, returns ~0 on error
static size_t
ll_getlen(FILE *f) {
    int b0 = getc(f), b1 = getc(f), b2 = getc(f), b3 = getc(f);
    if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF) return ~(size_t)0;
    return (size_t)(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
}

// b_ll - #(ll,filename) - load library from file
// Restores string definitions from a .mnt file. Silent if file missing.
// SIDEFF: reads file, modifies global string storage
static void
b_ll(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz;
    out[0] = '\0';
    if (n < 1) return;
    FILE *f = fopen(a[0], "rb");
    if (f == NULL) return;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MINT", 4) != 0) { fclose(f); return; }
    size_t nstr = ll_getlen(f);
    if (nstr == ~(size_t)0 || nstr > MAX_STRINGS) { fclose(f); return; }
    char namebuf[MAX_BUF], bodybuf[MAX_BUF];
    for (size_t i = 0; i < nstr; i++) {
        size_t llen = ll_getlen(f);
        if (llen >= MAX_BUF) { fclose(f); return; }
        if (fread(namebuf, 1, llen, f) != llen) { fclose(f); return; }
        namebuf[llen] = '\0';
        size_t blen = ll_getlen(f);
        if (blen >= MAX_BUF) { fclose(f); return; }
        if (fread(bodybuf, 1, blen, f) != blen) { fclose(f); return; }
        bodybuf[blen] = '\0';
        str_create(namebuf, bodybuf);
    }
    fclose(f);
}

// b_sl - #(sl,filename,str1,str2,...) - save library
// Writes named strings to a .mnt file.
// SIDEFF: writes file to disk
static void
b_sl(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 2) return;
    FILE *f = fopen(a[0], "wb");
    if (f == NULL) { strncpy(out, "Write error", sz - 1); out[sz-1] = '\0'; return; }
    fwrite("MINT", 1, 4, f);
    size_t nstr = n - 1;
    ll_putlen(f, nstr);
    for (size_t i = 1; i < n; i++) {
        struct str *s = str_find(a[i]);
        if (s == NULL) {
            ll_putlen(f, 0); putc(0, f); ll_putlen(f, 0);
            continue;
        }
        size_t sllen = strlen(s->name), blen = strlen(s->body);
        ll_putlen(f, sllen); fwrite(s->name, 1, sllen, f);
        ll_putlen(f, blen); fwrite(s->body, 1, blen, f);
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Editor state — buffer, point, mark ring, repeat count, screen pos
// Standalone simulation of Freemacs editor state for non-DOS builtins.
// ---------------------------------------------------------------------------

#define MAX_MARKS 32

static struct {
    char text[MAX_BUF_SIZE];  // buffer content
    size_t len;                // current length
    size_t point;              // cursor position (0 = before first char)
    size_t mark;               // mark position
    size_t marks[MAX_MARKS];   // mark ring
    int nmarks;                // entries in mark ring
    int repeat;                // repeat count
    int scrx, scry;            // screen cursor position
} ed;

// ed_insert_at — insert string s at position pos in editor buffer
// SIDEFF: modifies editor buffer
static void
ed_insert_at(size_t pos, const char *s, size_t slen) {
    if (pos > ed.len) pos = ed.len;
    if (ed.len + slen >= MAX_BUF_SIZE) slen = MAX_BUF_SIZE - ed.len - 1;
    memmove(ed.text + pos + slen, ed.text + pos, ed.len - pos + 1);
    memcpy(ed.text + pos, s, slen);
    ed.len += slen;
}

// ed_region — ordered bounds between point and mark
static void
ed_region(size_t *start, size_t *end) {
    if (ed.point < ed.mark) { *start = ed.point; *end = ed.mark; }
    else                    { *start = ed.mark;  *end = ed.point; }
}

// ed_delete_range — delete range [from, to) in editor buffer
// SIDEFF: modifies editor buffer
static void
ed_delete_range(size_t from, size_t to) {
    if (from > ed.len) from = ed.len;
    if (to > ed.len) to = ed.len;
    if (to <= from) return;
    memmove(ed.text + from, ed.text + to, ed.len - to + 1);
    ed.len -= to - from;
}

// --- Buffer builtins ---

// b_ba - #(ba,N) - buffer allocate: set buffer size ceiling or reset
static void
b_ba(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 1) return;
    long N = num_val(a[0]);
    if (N <= 0) { ed.len = 0; ed.point = 0; ed.mark = 0; return; }
    size_t cap = (size_t)N;
    if (cap > MAX_BUF_SIZE) cap = MAX_BUF_SIZE;
    if (cap > ed.len) {
        memset(ed.text + ed.len, 0, cap - ed.len + 1);
    } else if (cap < ed.len) {
        ed.len = ed.text[cap] = '\0';
    }
    ed.len = cap;
    if (ed.point > ed.len) ed.point = ed.len;
    if (ed.mark > ed.len) ed.mark = ed.len;
}

// b_bi - #(bi,N,M,E) - buffer insert: insert string E into buffer at pos M
// SIDEFF: modifies editor buffer
static void
b_bi(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 3) return;
    long M = num_val(a[1]);
    if (M < 0) M = 0;
    ed_insert_at((size_t)M, a[2], strlen(a[2]));
}

// b_is - #(is,S,E) - insert string E at current point
// SIDEFF: modifies editor buffer
static void
b_is(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 2) return;                           // #(is,S,E) — need both args
    size_t slen = strlen(a[1]);
    ed_insert_at(ed.point, a[1], slen);
    ed.point += slen;
}

// b_tr - #(tr,M,S) - translate region [point,mark) to string S (replace)
// SIDEFF: modifies editor buffer
static void
b_tr(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    size_t start, end;
    ed_region(&start, &end);
    const char *repl = n > 1 ? a[1] : "";
    size_t rlen = strlen(repl);
    if (start < end) {
        // Capture replaced text as return value
        size_t clen = end - start;
        if (clen >= sz) clen = sz - 1;
        memcpy(out, ed.text + start, clen);
        out[clen] = '\0';
        ed_delete_range(start, end);
    }
    ed_insert_at(start, repl, rlen);
    ed.point = start + rlen;
}

// b_dm - #(dm,M) - delete to mark M, return deleted text
// SIDEFF: modifies editor buffer
static void
b_dm(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)a; (void)n; out[0] = '\0';
    size_t start, end;
    ed_region(&start, &end);
    size_t clen = end - start;
    if (clen >= sz) clen = sz - 1;
    memcpy(out, ed.text + start, clen);
    out[clen] = '\0';
    ed_delete_range(start, end);
    ed.point = start;
}

// --- Mark operations ---

// b_pm - #(pm,S,E) - push/pop mark ring
// S='push': push current point onto mark ring. S='pop': pop top of ring.
static void
b_pm(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 1) return;
    if (eq(a[0], "push")) {
        if (ed.nmarks < MAX_MARKS) ed.marks[ed.nmarks++] = ed.point;
    } else if (eq(a[0], "pop")) {
        if (ed.nmarks > 0) {
            ed.point = ed.marks[--ed.nmarks];
            snprintf(out, sz, "%zu", ed.point);
        }
    }
}

// b_sm - #(sm,M,V) - set mark M to position V (0-based)
static void
b_sm(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 2) return;
    long V = num_val(a[1]);
    ed.mark = V < 0 ? 0 : (size_t)V;
}

// b_sp - #(sp,M) - set point to position M (0-based)
static void
b_sp(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 1) return;
    long M = num_val(a[0]);
    ed.point = M < 0 ? 0 : (size_t)M;
    if (ed.point > ed.len) ed.point = ed.len;
}

// b_rm - #(rm,M,V) - read mark M, set it to V (0-based), return old value
static void
b_rm(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    out[0] = '\0';
    if (n < 2) return;
    long V = num_val(a[1]);
    snprintf(out, sz, "%zu", ed.mark);
    ed.mark = V < 0 ? 0 : (size_t)V;
}

// b_rc - #(rc,M) - read count: return/set repeat count
static void
b_rc(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    if (n < 1) {
        snprintf(out, sz, "%d", ed.repeat);
        return;
    }
    long M = num_val(a[0]);
    snprintf(out, sz, "%d", ed.repeat);
    ed.repeat = (int)M;
}

// b_mb - #(mb,M,Y,N) - mark before: set mark before point
static void
b_mb(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)a; (void)n; out[0] = '\0';
    size_t old = ed.mark;
    ed.mark = ed.point;
    snprintf(out, sz, "%zu", old);
}

// --- Annunciator ---

// b_an - #(an,L,F,R) - announce: write to stderr
// L=0: show F. L=1: return previous value in R.
static void
b_an(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 2) return;
    long L = num_val(a[0]);
    if (L == 0) fprintf(stderr, "%s", a[1]);
}

// --- Display stubs ---

// b_rd - #(rd,F) - redisplay (no-op standalone)
static void
b_rd(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; (void)a; (void)n;
    out[0] = '\0';
}

// b_xy - #(xy,X,Y) - set screen position
static void
b_xy(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 2) return;
    ed.scrx = (int)num_val(a[0]);
    ed.scry = (int)num_val(a[1]);
}

// b_ow - #(ow,S) - overwrite screen (print to stderr)
static void
b_ow(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz;
    out[0] = '\0';
    if (n < 1) return;
    fprintf(stderr, "%s", a[0]);
}

// b_pp - #(pp) - pick position: return current point
static void
b_pp(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)a; (void)n;
    snprintf(out, sz, "%zu", ed.point);
}

// Search direction predicate
static int search_dir_is_backward(char **a, size_t n) { return n > 3 && num_val(a[3]) != 0; }
static int search_impossible(size_t plen, size_t buflen) { return plen == 0 || plen > buflen; }

// --- Text search in editor buffer ---

// search_forward — find pattern from point, set point after match
static int
search_forward(const char *pat, size_t plen, char *out, size_t sz) {
    for (size_t i = ed.point; i + plen <= ed.len; i++) {
        if (memcmp(ed.text + i, pat, plen) == 0) {
            snprintf(out, sz, "%zu", i);
            ed.point = i + plen;
            return 1;
        }
    }
    return 0;
}

// search_backward — find pattern before point, set point after match
static int
search_backward(const char *pat, size_t plen, char *out, size_t sz) {
    size_t end = ed.point + plen;
    if (end > ed.len) end = ed.len;
    for (size_t i = end; i >= plen; i--) {
        size_t pos = i - plen;
        if (pos < ed.point && memcmp(ed.text + pos, pat, plen) == 0) {
            snprintf(out, sz, "%zu", pos);
            ed.point = pos + plen;
            return 1;
        }
    }
    return 0;
}

// b_lp - #(lp,S,N,R,F) - look pattern: search forward/backward from point
static void
b_lp(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 1) return;
    const char *pat = a[0];
    size_t plen = strlen(pat);
    if (search_impossible(plen, ed.len)) return;
    if (search_dir_is_backward(a, n)) {
        search_backward(pat, plen, out, sz);
    } else {
        search_forward(pat, plen, out, sz);
    }
}

// b_lk - #(lk,S,E,F,L,N) - look string: search from point for S
// Like lp but returns position of match start.
static void
b_lk(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra;
    if (n < 1) { out[0] = '\0'; return; }
    search_forward(a[0], strlen(a[0]), out, sz);
}

// b_lq - #(l?,S,E,F,L,Y,N) - look and test: search, return Y if found else N
static void
b_lq(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; out[0] = '\0';
    if (n < 1) return;
    const char *pat = a[0];
    size_t plen = strlen(pat);
    size_t found = ed.len;
    for (size_t i = ed.point; i + plen <= ed.len; i++) {
        if (memcmp(ed.text + i, pat, plen) == 0) { found = i; break; }
    }
    if (found < ed.len) {
        ed.point = found + plen;
        const char *y = n > 4 ? a[4] : "";
        strncpy(out, y, sz - 1); out[sz-1] = '\0';
    } else {
        const char *nval = n > 5 ? a[5] : "";
        strncpy(out, nval, sz - 1); out[sz-1] = '\0';
    }
}

// --- Environment ---

// b_ev - #(ev) - read environment: set up env.* strings from environ
// SIDEFF: creates env.* strings in global string storage
static void
b_ev(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)a; (void)n; (void)sz;
    out[0] = '\0';
    extern char **environ;
    char ename[MAX_BUF];
    for (char **e = environ; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        if (eq == NULL) continue;
        size_t evlen = (size_t)(eq - *e);
        if (evlen + 4 >= MAX_BUF) continue;
        memcpy(ename, "env.", 4);
        memcpy(ename + 4, *e, evlen);
        ename[4 + evlen] = '\0';
        str_create(ename, eq + 1);
    }
}

// --- Timed input ---

// b_it - #(it,T) - input timed: read from stdin (ignore T delay)
static void
b_it(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)a; (void)n;
    out[0] = '\0';
    int c = getchar();
    if (c == EOF) { strncpy(out, "Timeout", sz - 1); out[sz-1] = '\0'; return; }
    out[0] = (char)c;
    out[1] = '\0';
}

// --- Variable file I/O ---

// b_lv - #(lv,F) - load variable: read file content into string named F
// SIDEFF: reads file, creates string
static void
b_lv(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 1) return;
    FILE *f = fopen(a[0], "rb");
    if (f == NULL) return;
    char buf[MAX_BUF];
    size_t pos = 0;
    int c;
    while ((c = fgetc(f)) != EOF && pos < MAX_BUF - 1) buf[pos++] = (char)c;
    buf[pos] = '\0';
    fclose(f);
    str_create(a[0], buf);
}

// b_sv - #(sv,F,V) - save variable: write string V to file named F
// SIDEFF: writes file
static void
b_sv(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)sz; out[0] = '\0';
    if (n < 2) return;
    const char *file = a[0];               // #(sv,F,V) — F = filename
    const char *val  = a[1];               // V = value to write
    FILE *f = fopen(file, "wb");
    if (f == NULL) return;
    fwrite(val, 1, strlen(val), f);
    fclose(f);
}

// --- Debug ---

// b_db - #(db) - debug: print processor state to stderr
static void
b_db(char **a, size_t n, char *out, size_t sz, int *ra) {
    (void)ra; (void)a; (void)n; (void)sz;
    out[0] = '\0';
    fprintf(stderr, "MINT debug:\n");
    fprintf(stderr, "  active:  [%s]\n", active);
    fprintf(stderr, "  neutral: [%s]\n", neutral);
    fprintf(stderr, "  nmarks:  %zu\n", nmarks);
    fprintf(stderr, "  nstrs:   %zu\n", nstrs);
    fprintf(stderr, "  editor:  len=%zu point=%zu mark=%zu\n", ed.len, ed.point, ed.mark);
}

// ---------------------------------------------------------------------------
// Dispatch table - maps 2-char function names to implementations
// ---------------------------------------------------------------------------

struct dispatch {
    const char *name;
    builtin_fn fn;
};

static struct dispatch builtins[] = {
    // String operations
    {"ds", b_ds}, {"mp", b_mp}, {"gs", b_gs}, {"go", b_go}, {"gn", b_gn},
    {"fm", b_fm}, {"rs", b_rs}, {"es", b_es}, {"si", b_si},
    // Arithmetic
    {"++", b_add}, {"--", b_sub}, {"**", b_mul}, {"//", b_div}, {"%%", b_mod},
    // Bitwise
    {"||", b_or}, {"&&", b_and}, {"^^", b_xor},
    // Comparison
    {"g?", b_gt}, {"==", b_eq}, {"a?", b_lt},
    // Utility
    {"nc", b_nc}, {"n?", b_nq}, {"ls", b_ls}, {"bc", b_bc}, {"sa", b_sa},
    {"hl", b_hl},
    // File I/O
    {"rf", b_rf}, {"wf", b_wf}, {"ff", b_ff}, {"rn", b_rn}, {"de", b_de},
    // Time
    {"ct", b_ct},
    // Execute
    {"ex", b_ex},
    // Editor buffer
    {"ba", b_ba}, {"bi", b_bi}, {"is", b_is}, {"tr", b_tr}, {"dm", b_dm},
    // Mark ops
    {"pm", b_pm}, {"sm", b_sm}, {"sp", b_sp}, {"rm", b_rm}, {"rc", b_rc},
    {"mb", b_mb},
    // Display
    {"an", b_an}, {"rd", b_rd}, {"xy", b_xy}, {"ow", b_ow}, {"pp", b_pp},
    // Search
    {"lp", b_lp}, {"lk", b_lk}, {"l?", b_lq},
    // Environment
    {"ev", b_ev},
    // I/O
    {"it", b_it}, {"lv", b_lv}, {"sv", b_sv},
    // Debug
    {"db", b_db},
    // Library
    {"ll", b_ll}, {"sl", b_sl},
    {NULL, NULL}
};

// builtin_find - look up builtin function by name
static builtin_fn
builtin_find(const char *name) {
    for (struct dispatch *d = builtins; d->name; d++) {
        if (eq(d->name, name)) {
            return d->fn;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Default function - invoke user-defined string as macro
//
// #(name,a1,a2,...) fills markers: 1=name, 2=a1, 3=a2, ...
// This differs from gs where markers: 1=a1, 2=a2, ...
// ---------------------------------------------------------------------------

static void
call_default(const char *name, char **a, size_t n, char *out, size_t sz) {
    out[0] = '\0';
    struct str *s = str_find(name);
    if (s == NULL) return;

    char *all[MAX_ARGS + 1];
    all[0] = (char *)name;
    for (size_t i = 0; i < n && i < MAX_ARGS; i++) all[i + 1] = a[i];
    params_fill(out, sz, s->body, all, n + 1);
}

// call - dispatch to builtin or user-defined function
static void
call(const char *name, char **a, size_t n, char *out, size_t sz, int *ra) {
    *ra = 0;
    builtin_fn fn = builtin_find(name);
    if (fn) {
        fn(a, n, out, sz, ra);
    } else {
        call_default(name, a, n, out, sz);
    }
}

// ---------------------------------------------------------------------------
// Scanner - MINT evaluation loop
//
// Processes active buffer character by character:
// - (...) protection: strip parens, copy contents to neutral
// - #(...) active call: evaluate, rescan result
// - ##(...) neutral call: evaluate, don't rescan
// - , argument separator
// - ) end of function call
// - other: copy to neutral
// ---------------------------------------------------------------------------

// find_close - find matching ) accounting for nesting
static size_t
find_close(const char *s, size_t len) {
    int depth = 1;
    for (size_t i = 0; i < len; i++) {
        if (is_open(s[i])) depth++;
        else if (is_close(s[i]) && --depth == 0) return i;
    }
    return len;
}

// collect_args - extract function arguments from neutral buffer
// Returns arg count, allocates strings in args[] (caller frees).
static size_t
collect_args(char **args, size_t start, size_t end) {
    size_t argc = 0, prev = start;

    for (size_t i = 0; i < nargpos; i++) {
        if (argpos[i] > start && argpos[i] <= end) {
            size_t len = argpos[i] - prev;
            args[argc] = malloc(len + 1);
            if (args[argc] == NULL) { return 0; }
            memcpy(args[argc], neutral + prev, len);
            args[argc][len] = '\0';
            argc++;
            prev = argpos[i];
        }
    }
    // Final argument
    size_t len = end - prev;
    args[argc] = malloc(len + 1);
    if (args[argc] == NULL) { return 0; }
    memcpy(args[argc], neutral + prev, len);
    args[argc][len] = '\0';
    argc++;

    // Remove consumed argument positions
    size_t j = 0;
    for (size_t i = 0; i < nargpos; i++) {
        if (argpos[i] <= start) {
            argpos[j++] = argpos[i];
        }
    }
    nargpos = j;

    return argc;
}

// dispatch_call - invoke function and place result
// SIDEFF: may call exit() via b_hl, frees args[], modifies buffers
static void
dispatch_call(struct marker m, char **args, size_t argc) {
    neutral_truncate(m.pos);

    char result[MAX_BUF];
    int ra = 0;
    if (argc > 0) {
        call(args[0], args + 1, argc - 1, result, sizeof(result), &ra);
    } else {
        result[0] = '\0';
    }

    for (size_t i = 0; i < argc; i++) {
        free(args[i]);
    }

    size_t rlen = strlen(result);
    if (m.rescan || ra) active_prepend(result, rlen);
    else neutral_append(result, rlen);
}

// handle_protection_paren - copy (...)
// Copies protected content to neutral, advancing past closing paren.
// Returns 1 if unmatched paren (scan should terminate).
static int
handle_protection_paren(void) {
    active_consume(1);
    size_t end = find_close(active, alen);
    if (end >= alen) { printf("%s", neutral); return 1; }
    neutral_append(active, end);
    active_consume(end + 1);
    return 0;
}

// handle_active_call - process #(, record with rescan
static void
handle_active_call(void) {
    active_consume(2);
    mark_func(nlen, 1);
}

// handle_neutral_call - process ##(, record without rescan
static void
handle_neutral_call(void) {
    active_consume(3);
    mark_func(nlen, 0);
}

// handle_argument_separator - process comma between arguments
static void
handle_argument_separator(void) {
    active_consume(1);
    mark_arg();
}

// handle_function_end - process ), collect args and dispatch
// Returns 1 if unmatched paren (scan should terminate).
static int
handle_function_end(void) {
    active_consume(1);
    if (!has_marks()) { printf("%s", neutral); return 1; }

    nmarks--;
    struct marker m = marks[nmarks];
    size_t start = m.pos;
    char *args[MAX_ARGS];
    size_t argc = collect_args(args, start, nlen);
    dispatch_call(m, args, argc);
    return 0;
}

// scan - main evaluation entry point
static void
scan(const char *input) {
    nlen = 0; neutral[0] = '\0';
    alen = strlen(input);
    if (alen >= MAX_BUF) alen = MAX_BUF - 1;
    memcpy(active, input, alen);
    active[alen] = '\0';
    nmarks = nargpos = 0;

    while (!active_empty()) {
        char c = active_peek();

        if (is_skip(c)) {
            active_consume(1);
            continue;
        }
        if (is_open(c)) {
            if (handle_protection_paren()) return;
            continue;
        }
        if (is_comma(c)) {
            handle_argument_separator();
            continue;
        }
        if (active_has(3) && starts_neutral_call(active)) {
            handle_neutral_call();
            continue;
        }
        if (active_has(2) && starts_active_call(active)) {
            handle_active_call();
            continue;
        }
        if (is_hash(c)) {
            neutral_put(c);
            active_consume(1);
            continue;
        }
        if (is_close(c)) {
            if (handle_function_end()) return;
            continue;
        }

        neutral_put(c);
        active_consume(1);
    }

    printf("%s", neutral);
}

// ---------------------------------------------------------------------------
// Main - read input and run scanner
// ---------------------------------------------------------------------------

// slurp - read entire file into malloc'd buffer
static char *
slurp(FILE *f) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *nbuf = realloc(buf, cap);
            if (nbuf == NULL) { free(buf); return NULL; }
            buf = nbuf;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

int
main(int argc, char **argv) {
    char *input;

    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (f == NULL) { fprintf(stderr, "mint: cannot open %s\n", argv[1]); return 1; }
        input = slurp(f);
        fclose(f);
    } else {
        input = slurp(stdin);
    }

    scan(input);
    free(input);
    return 0;
}
