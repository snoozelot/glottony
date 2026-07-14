#!/usr/bin/env -S ccraft
// joy - purely functional concatenative language interpreter
//
// ============================================================================
// WHAT IS JOY?
// ============================================================================
//
// Imagine you have a stack of plates. You can only touch the top plate.
// Joy is a language where EVERYTHING works like that stack of plates.
//
// When you write a number, it goes on top of the stack:
//
//     3 5 7        -- stack is now [3, 5, 7] with 7 on top
//
// Words do things to the stack. "dup" copies the top:
//
//     3 dup        -- stack: [3, 3]
//
// Math words grab numbers and put back the answer:
//
//     3 5 +        -- stack: [8]  (3+5)
//     3 5 *        -- stack: [15] (3*5)
//
// ============================================================================
// QUOTATIONS: CODE AS DATA
// ============================================================================
//
// Square brackets make a "quotation" - code that doesn't run yet:
//
//     [dup *]      -- this just sits on the stack, waiting
//
// A quotation is both code AND data. It's a list you can also execute.
// This is Joy's superpower: programs that write programs, code that
// manipulates code.
//
// ============================================================================
// COMBINATORS: THE HEART OF JOY
// ============================================================================
//
// Combinators are words that take quotations and run them in clever ways.
// They ARE the control flow - Joy has no if statements, no loops, no
// special syntax. Just combinators.
//
// BASIC EXECUTION
// ---------------
//
// "i" - run a quotation:
//
//     5 [dup *] i       -- gives 25
//     ^    ^    ^
//     |    |    run it!
//     |    code to run
//     data to work on
//
// "dip" - run code BELOW the top item, then put it back:
//
//     1 2 [10 *] dip    -- gives 10 2
//         ^^^^^
//     runs on 1, not 2; then 2 goes back on top
//
//     This is HUGE. It lets you "reach under" the top of the stack.
//     Want to do something to the second item? Use dip.
//
// "x" - run a quotation but keep it on the stack:
//
//     5 [dup *] x       -- gives 5 [dup *] 25
//
//     The quotation stays! This enables self-reference and recursion.
//
// CONDITIONALS
// ------------
//
// "ifte" - if-then-else with THREE quotations:
//
//     5 [0 =] [pop 99] [dup *] ifte    -- gives 25
//        ^^^   ^^^^^^^  ^^^^^^
//        test  if-true  if-false
//
//     The test runs WITHOUT destroying the stack - it peeks, doesn't consume.
//     Then either the true-branch or false-branch runs on the original stack.
//
// "choice" - simpler conditional, picks between two values:
//
//     true  10 20 choice    -- gives 10
//     false 10 20 choice    -- gives 20
//
// LOOPS AND REPETITION
// --------------------
//
// "times" - repeat N times:
//
//     1 5 [2 *] times       -- gives 32 (1 doubled 5 times)
//
// "while" - repeat while condition is true:
//
//     1 [dup 10 <] [dup 2 *] while    -- 1,2,4,8,16 (stops when >= 10)
//       ^^^^^^^^^  ^^^^^^^^^
//       keep going?  what to do
//
// LIST COMBINATORS
// ----------------
//
// "map" - run code on every item, collect results:
//
//     [1 2 3 4 5] [dup *] map       -- gives [1 4 9 16 25]
//
// "filter" - keep items that pass a test:
//
//     [1 2 3 4 5 6] [2 % 0 =] filter   -- gives [2 4 6] (evens only)
//
// "fold" - combine all items into one:
//
//     [1 2 3 4 5] 0 [+] fold        -- gives 15 (sum)
//     [1 2 3 4 5] 1 [*] fold        -- gives 120 (product)
//         ^       ^  ^
//         list  start combiner
//
// "step" - run code for each item (like map but no result list):
//
//     [1 2 3] [. ] step             -- prints 1 2 3
//
// RECURSION COMBINATORS
// ---------------------
//
// These are the crown jewels. Instead of writing recursive functions
// by name, you describe the PATTERN of recursion.
//
// "linrec" - linear recursion (one recursive call):
//
//     5 [0 =] [pop 1] [dup pred] [*] linrec   -- factorial! gives 120
//        ^^^   ^^^^^^  ^^^^^^^^   ^^
//        done?  base   before    after
//              case   recursion  recursion
//
//     This says: "If zero, return 1. Otherwise, dup and decrement,
//     recurse, then multiply." That's factorial without naming it!
//
//     How linrec works:
//       - Test [0 =]. If true, run [pop 1] and stop.
//       - Otherwise: run [dup pred], recurse, then run [*].
//
//     5 -> dup pred -> 5 4 -> recurse -> 5 4 3 2 1 1 -> multiply back up
//                                                       5*4*3*2*1*1 = 120
//
// "tailrec" - tail recursion (no "after" phase, very efficient):
//
//     1000000 [0 =] [] [pred] tailrec   -- counts to zero, no stack overflow!
//
//     Unlike linrec, tailrec doesn't need to remember anything.
//     It just loops until done. Can run forever without using memory.
//
// ============================================================================
// WHY COMBINATORS MATTER
// ============================================================================
//
// In most languages, control flow is built-in syntax:
//
//     if (x == 0) return 1;           // special syntax
//     for (int i = 0; i < n; i++)     // special syntax
//
// In Joy, control flow is just WORDS:
//
//     [0 =] [pop 1] [dup pred] [*] linrec
//
// This means YOU CAN BUILD NEW CONTROL FLOW. Want a new kind of loop?
// Define it! Want a new recursion pattern? Define it!
//
//     DEFINE binrec == ...    -- binary recursion (like mergesort)
//     DEFINE genrec == ...    -- general recursion
//
// The language doesn't limit what patterns you can express.
//
// ============================================================================
// NO VARIABLES - AND THAT'S GOOD
// ============================================================================
//
// Joy has no variables. Everything flows through the stack.
//
// This sounds limiting but it's freeing. Any two programs can combine
// just by writing them next to each other:
//
//     [dup *]           -- square
//     [1 +]             -- increment
//     [dup * 1 +]       -- square then increment - just concatenate!
//
// This is why Joy is called "concatenative" - programs compose by
// concatenation. No plumbing, no argument passing, no glue code.
//
// ============================================================================
// DEFINING YOUR OWN WORDS
// ============================================================================
//
//     DEFINE square == dup * .
//     DEFINE cube == dup dup * * .
//     DEFINE factorial == [0 =] [pop 1] [dup pred] [*] linrec .
//
//     5 square           -- 25
//     3 cube             -- 27
//     6 factorial        -- 720
//
// Definitions can be recursive - just use the name inside the definition:
//
//     DEFINE countdown == [0 =] [] [dup . pred countdown] ifte .
//     5 countdown        -- prints 5 4 3 2 1 0
//
// ============================================================================
// QUICK REFERENCE
// ============================================================================
//
// STACK:      dup swap pop rollup rolldown   -- rearrange
// ARITHMETIC: + - * / % pred succ abs neg    -- math
// COMPARISON: = < > <= >= !=                 -- gives true/false
// LOGIC:      and or not                     -- boolean
// LISTS:      first rest cons uncons size    -- take apart / build
//             concat reverse null            -- manipulate
// EXECUTION:  i x dip                        -- run quotations
// CONDITIONAL: ifte choice                   -- branching
// LOOPS:      times while                    -- repetition
// LISTS+CODE: map fold filter step           -- list processing
// RECURSION:  linrec tailrec                 -- recursion patterns
// I/O:        . put putch get                -- print / read
// DEFINE:     DEFINE name == body .          -- name your words
//
// ============================================================================
// USAGE
// ============================================================================
//
//   joy [FILE]           -- run a file
//   joy -e 'EXPR'        -- evaluate expression
//   echo 'EXPR' | joy    -- read from stdin
//
// EXAMPLES
//
//   joy -e '5 dup *'                                  # 25
//   joy -e '[1 2 3] [dup *] map'                      # [1 4 9]
//   joy -e 'DEFINE sq == dup * . 5 sq'                # 25
//   joy -e '5 [0 =] [pop 1] [dup pred] [*] linrec'    # 120
//   joy -e 'DEFINE fib == [2 <] [] [dup pred fib swap pred pred fib +] ifte .
//           25 fib'                                   # 75025
//
// ============================================================================
// IMPLEMENTATION
// ============================================================================
//
// Bytecode VM with mark-sweep GC. No C recursion, unlimited depth.

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Configuration
// ============================================================================

enum {
    INIT_STACK    = 1024,
    INIT_CODE     = 65536,
    INIT_HEAP     = 1024,
    MAX_DEFS      = 512,
    MAX_NAME      = 64,
    MAX_INPUT     = 65536,
    GC_THRESHOLD  = 10000,
};

// ============================================================================
// Opcodes
// ============================================================================

enum opcode {
    OP_NOP,
    OP_HALT,
    OP_PUSH,          // push value (next word is value ptr)
    OP_CALL,          // call word (next word is def index)
    OP_RET,           // return
    OP_BRANCH,        // branch if true (next word is offset)
    OP_JUMP,          // unconditional jump (next word is offset)
    // Stack
    OP_DUP, OP_SWAP, OP_POP, OP_ROLLUP, OP_ROLLDOWN,
    OP_DUPD, OP_POPD, OP_SWAPD,
    // Arithmetic
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_SUCC, OP_PRED, OP_ABS, OP_NEG, OP_MAX, OP_MIN,
    // Comparison
    OP_EQ, OP_LT, OP_GT, OP_LE, OP_GE, OP_NE,
    // Logic
    OP_AND, OP_OR, OP_NOT,
    // List
    OP_FIRST, OP_REST, OP_CONS, OP_UNCONS, OP_SWONS,
    OP_NULL, OP_SIZE, OP_CONCAT, OP_LIST, OP_REVERSE, OP_AT,
    // Combinators
    OP_I, OP_X, OP_DIP, OP_IFTE, OP_TIMES,
    OP_MAP, OP_FOLD, OP_FILTER, OP_STEP,
    OP_LINREC, OP_TAILREC, OP_WHILE,
    // Misc
    OP_CHOICE, OP_STACK, OP_UNSTACK,
    // I/O
    OP_PRINT, OP_PUT, OP_PUTCH, OP_GET,
};

// ============================================================================
// Value Types
// ============================================================================

enum value_type { T_INT, T_BOOL, T_SYM, T_LIST };

struct value {
    struct value *next;    // heap chain for GC
    unsigned char marked;
    unsigned char type;
    int *code;             // bytecode (for lists used as quotations)
    int code_len;
    union {
        long num;
        int boolean;
        char *sym;
        struct {
            struct value **items;
            int len;
        } list;
    };
};

// ============================================================================
// VM State
// ============================================================================

// Heap (linked list for GC)
static struct value *HEAP = NULL;
static int HEAP_COUNT = 0;

// Data stack (growable)
static struct value **STACK = NULL;
static int SP = 0;
static int STACK_CAP = 0;

// Return stack (growable)
static int *RET_STACK = NULL;
static int RSP = 0;
static int RET_CAP = 0;

// Code buffer
static int *CODE = NULL;
static int CODE_LEN = 0;
static int CODE_CAP = 0;

// Instruction pointer
static int IP = 0;

// Dictionary
struct definition {
    char name[MAX_NAME];
    int addr;           // bytecode address
    int compiled;       // 1 if compiled
};
static struct definition DEFS[MAX_DEFS];
static int NDEF = 0;

// Temp roots for GC safety during allocation
static struct value *TEMP_ROOTS[256];
static int TEMP_ROOT_COUNT = 0;

// ============================================================================
// Error Handling
// ============================================================================

static void
die(const char *msg) {
    fprintf(stderr, "joy: %s\n", msg);
    exit(1);
}

static void
die_type(const char *op, const char *expected) {
    fprintf(stderr, "joy: %s expects %s\n", op, expected);
    exit(1);
}

// ============================================================================
// GC: Mark Phase
// ============================================================================

static void mark(struct value *v);

static void
mark_list(struct value *v) {
    for (int i = 0; i < v->list.len; i++) {
        mark(v->list.items[i]);
    }
}

static void
mark(struct value *v) {
    if (!v || v->marked) { return; }
    v->marked = 1;
    if (v->type == T_LIST) {
        mark_list(v);
        // Also scan embedded code for value pointers
        if (v->code) {
            for (int i = 0; i < v->code_len; i++) {
                if (v->code[i] == OP_PUSH) {
                    uintptr_t lo = (unsigned int)v->code[i + 1];
                    uintptr_t hi = (unsigned int)v->code[i + 2];
                    struct value *ref = (struct value *)(lo | (hi << 32));
                    mark(ref);
                    i += 2;
                }
            }
        }
    }
}

// ============================================================================
// GC: Sweep Phase
// ============================================================================

static void *read_ptr(int addr);  // forward declaration

static void
free_value(struct value *v) {
    if (v->type == T_SYM) { free(v->sym); }
    if (v->type == T_LIST) { free(v->list.items); }
    if (v->code) { free(v->code); }
    free(v);
}

static void
gc_sweep(void) {
    struct value **p = &HEAP;
    while (*p) {
        if ((*p)->marked) {
            (*p)->marked = 0;
            p = &(*p)->next;
        } else {
            struct value *dead = *p;
            *p = dead->next;
            free_value(dead);
            HEAP_COUNT--;
        }
    }
}

// ============================================================================
// GC: Collection
// ============================================================================

// Forward declarations for GC roots
struct saved_state {
    struct value **data;
    int sp;
    int cap;
};
static struct saved_state *SAVE_STACK = NULL;
static int SAVE_SP = 0;
static int SAVE_CAP = 0;

static void
gc(void) {
    // Mark from data stack
    for (int i = 0; i < SP; i++) { mark(STACK[i]); }

    // Mark from all saved stacks
    for (int s = 0; s < SAVE_SP; s++) {
        for (int i = 0; i < SAVE_STACK[s].sp; i++) {
            mark(SAVE_STACK[s].data[i]);
        }
    }

    // Mark from temp roots
    for (int i = 0; i < TEMP_ROOT_COUNT; i++) { mark(TEMP_ROOTS[i]); }

    // Mark values referenced in CODE buffer (OP_PUSH instructions)
    for (int i = 0; i < CODE_LEN; i++) {
        if (CODE[i] == OP_PUSH) {
            struct value *v = read_ptr(i + 1);
            mark(v);
            i += 2;  // skip the pointer
        }
    }

    gc_sweep();
}

static void
gc_maybe(void) {
    if (HEAP_COUNT > GC_THRESHOLD) { gc(); }
}

// ============================================================================
// Allocation
// ============================================================================

static struct value *
alloc_value(enum value_type type) {
    gc_maybe();
    struct value *v = calloc(1, sizeof(struct value));
    v->type = type;
    v->next = HEAP;
    HEAP = v;
    HEAP_COUNT++;
    return v;
}

// ============================================================================
// Value Constructors
// ============================================================================

static struct value *
make_int(long n) {
    struct value *v = alloc_value(T_INT);
    v->num = n;
    return v;
}

static struct value *
make_bool(int b) {
    struct value *v = alloc_value(T_BOOL);
    v->boolean = b;
    return v;
}

static struct value *
make_sym(const char *s) {
    struct value *v = alloc_value(T_SYM);
    v->sym = strdup(s);
    return v;
}

static struct value *
make_list(struct value **items, int len) {
    struct value *v = alloc_value(T_LIST);
    v->list.items = malloc(len * sizeof(struct value *));
    v->list.len = len;
    for (int i = 0; i < len; i++) {
        v->list.items[i] = items[i];
    }
    return v;
}

static struct value *
make_list_with_code(struct value **items, int len, int *code, int code_len) {
    struct value *v = alloc_value(T_LIST);
    v->list.items = malloc(len * sizeof(struct value *));
    v->list.len = len;
    for (int i = 0; i < len; i++) {
        v->list.items[i] = items[i];
    }
    if (code && code_len > 0) {
        v->code = malloc(code_len * sizeof(int));
        v->code_len = code_len;
        memcpy(v->code, code, code_len * sizeof(int));
    }
    return v;
}

// ============================================================================
// Value Predicates
// ============================================================================

static int is_int(struct value *v)   { return v->type == T_INT; }
static int is_bool(struct value *v)  { return v->type == T_BOOL; }
static int is_list(struct value *v)  { return v->type == T_LIST; }
static int is_sym(struct value *v)   { return v->type == T_SYM; }
static int is_callable(struct value *v) { return v->type == T_LIST && v->code; }

static int
is_truthy(struct value *v) {
    if (is_bool(v)) { return v->boolean; }
    if (is_int(v)) { return v->num != 0; }
    if (is_list(v)) { return v->list.len > 0; }
    return 1;
}

// ============================================================================
// Stack Operations
// ============================================================================

static void
stack_grow(void) {
    STACK_CAP = STACK_CAP ? STACK_CAP * 2 : INIT_STACK;
    STACK = realloc(STACK, STACK_CAP * sizeof(struct value *));
}

static void
push(struct value *v) {
    if (SP >= STACK_CAP) { stack_grow(); }
    STACK[SP++] = v;
}

static struct value *
pop(void) {
    if (SP == 0) { die("stack underflow"); }
    return STACK[--SP];
}

static struct value *
peek(int depth) {
    if (SP <= depth) { die("stack underflow"); }
    return STACK[SP - 1 - depth];
}

static void
require(int n, const char *op) {
    if (SP < n) {
        fprintf(stderr, "joy: %s requires %d items\n", op, n);
        exit(1);
    }
}

// ============================================================================
// Return Stack
// ============================================================================

static void
ret_grow(void) {
    RET_CAP = RET_CAP ? RET_CAP * 2 : INIT_STACK;
    RET_STACK = realloc(RET_STACK, RET_CAP * sizeof(int));
}

static void
ret_push(int addr) {
    if (RSP >= RET_CAP) { ret_grow(); }
    RET_STACK[RSP++] = addr;
}

static int
ret_pop(void) {
    if (RSP == 0) { die("return stack underflow"); }
    return RET_STACK[--RSP];
}

// ============================================================================
// Code Buffer
// ============================================================================

static void
code_grow(void) {
    CODE_CAP = CODE_CAP ? CODE_CAP * 2 : INIT_CODE;
    CODE = realloc(CODE, CODE_CAP * sizeof(int));
}

static void
emit(int word) {
    if (CODE_LEN >= CODE_CAP) { code_grow(); }
    CODE[CODE_LEN++] = word;
}

static void
emit_ptr(void *p) {
    // Store pointer as two ints (portable for 64-bit)
    uintptr_t ptr = (uintptr_t)p;
    emit((int)(ptr & 0xFFFFFFFF));
    emit((int)(ptr >> 32));
}

static void *
read_ptr(int addr) {
    uintptr_t lo = (unsigned int)CODE[addr];
    uintptr_t hi = (unsigned int)CODE[addr + 1];
    return (void *)(lo | (hi << 32));
}

// ============================================================================
// Value Printing
// ============================================================================

static void print_value(struct value *v);

static void
print_list(struct value *v) {
    printf("[");
    for (int i = 0; i < v->list.len; i++) {
        if (i > 0) { printf(" "); }
        print_value(v->list.items[i]);
    }
    printf("]");
}

static void
print_value(struct value *v) {
    switch (v->type) {
    case T_INT:   printf("%ld", v->num); break;
    case T_BOOL:  printf("%s", v->boolean ? "true" : "false"); break;
    case T_SYM:   printf("%s", v->sym); break;
    case T_LIST:  print_list(v); break;
    }
}

static void
print_stack(void) {
    for (int i = 0; i < SP; i++) {
        if (i > 0) { printf(" "); }
        print_value(STACK[i]);
    }
    if (SP > 0) { printf("\n"); }
}

// ============================================================================
// Parser State
// ============================================================================

static char *INPUT;
static char *POS;

// ============================================================================
// Character Classes
// ============================================================================

static int is_ws(char c)    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_sym_char(char c) { return c && !is_ws(c) && c != '[' && c != ']'; }

// ============================================================================
// Tokenizer
// ============================================================================

static void skip_ws(void) { while (is_ws(*POS)) { POS++; } }

static int
at_end(void) {
    skip_ws();
    return *POS == '\0';
}

static int
looking_at(char c) {
    skip_ws();
    return *POS == c;
}

static char *
parse_token(void) {
    skip_ws();
    if (*POS == '\0' || *POS == '[' || *POS == ']') { return NULL; }

    char *start = POS;
    while (is_sym_char(*POS)) { POS++; }

    int len = POS - start;
    char *tok = malloc(len + 1);
    memcpy(tok, start, len);
    tok[len] = '\0';
    return tok;
}

static int
tok_is_number(const char *s) {
    if (*s == '-') { s++; }
    if (!is_digit(*s)) { return 0; }
    while (*s) {
        if (!is_digit(*s)) { return 0; }
        s++;
    }
    return 1;
}

// ============================================================================
// Dictionary Lookup
// ============================================================================

static int
lookup_def(const char *name) {
    for (int i = 0; i < NDEF; i++) {
        if (strcmp(DEFS[i].name, name) == 0) { return i; }
    }
    return -1;
}

static int
add_def(const char *name) {
    if (NDEF >= MAX_DEFS) { die("too many definitions"); }
    int idx = NDEF++;
    strncpy(DEFS[idx].name, name, MAX_NAME - 1);
    DEFS[idx].addr = -1;
    DEFS[idx].compiled = 0;
    return idx;
}

// ============================================================================
// Primitive Lookup
// ============================================================================

struct prim_entry {
    const char *name;
    int opcode;
};

static struct prim_entry PRIMS[] = {
    // Stack
    { "dup", OP_DUP }, { "swap", OP_SWAP }, { "pop", OP_POP },
    { "rollup", OP_ROLLUP }, { "rolldown", OP_ROLLDOWN },
    { "dupd", OP_DUPD }, { "popd", OP_POPD }, { "swapd", OP_SWAPD },
    { "stack", OP_STACK }, { "unstack", OP_UNSTACK },
    { "id", OP_NOP },
    // Arithmetic
    { "+", OP_ADD }, { "-", OP_SUB }, { "*", OP_MUL },
    { "/", OP_DIV }, { "%", OP_MOD },
    { "succ", OP_SUCC }, { "pred", OP_PRED },
    { "abs", OP_ABS }, { "neg", OP_NEG },
    { "max", OP_MAX }, { "min", OP_MIN },
    // Comparison
    { "=", OP_EQ }, { "<", OP_LT }, { ">", OP_GT },
    { "<=", OP_LE }, { ">=", OP_GE }, { "!=", OP_NE },
    // Logic
    { "and", OP_AND }, { "or", OP_OR }, { "not", OP_NOT },
    // List
    { "first", OP_FIRST }, { "rest", OP_REST },
    { "cons", OP_CONS }, { "uncons", OP_UNCONS }, { "swons", OP_SWONS },
    { "null", OP_NULL }, { "size", OP_SIZE },
    { "concat", OP_CONCAT }, { "list", OP_LIST },
    { "reverse", OP_REVERSE }, { "at", OP_AT },
    // Combinators
    { "i", OP_I }, { "x", OP_X }, { "dip", OP_DIP },
    { "ifte", OP_IFTE }, { "times", OP_TIMES },
    { "map", OP_MAP }, { "fold", OP_FOLD }, { "filter", OP_FILTER },
    { "step", OP_STEP }, { "linrec", OP_LINREC },
    { "tailrec", OP_TAILREC }, { "while", OP_WHILE },
    { "choice", OP_CHOICE },
    // I/O
    { ".", OP_PRINT }, { "put", OP_PUT },
    { "putch", OP_PUTCH }, { "get", OP_GET },
    { NULL, 0 }
};

static int
lookup_prim(const char *name) {
    for (struct prim_entry *p = PRIMS; p->name; p++) {
        if (strcmp(p->name, name) == 0) { return p->opcode; }
    }
    return -1;
}

// ============================================================================
// Compiler: Forward Declaration
// ============================================================================

static struct value *compile_expr(void);
static struct value *compile_list(void);

// ============================================================================
// Compiler: Quotation/List
// ============================================================================

static struct value *
compile_list(void) {
    POS++;  // skip '['

    // Collect values and compile code
    struct value *items[1024];
    int item_count = 0;
    int code_start = CODE_LEN;

    while (!looking_at(']')) {
        if (at_end()) { die("unclosed list"); }
        items[item_count++] = compile_expr();
    }
    POS++;  // skip ']'

    emit(OP_HALT);  // Quotations end with HALT, not RET (no return address)

    // Create list with both items and code
    int code_len = CODE_LEN - code_start;
    struct value *list = make_list_with_code(items, item_count,
                                              CODE + code_start, code_len);

    // Backpatch: replace compiled code with PUSH of the list
    CODE_LEN = code_start;
    emit(OP_PUSH);
    emit_ptr(list);

    return list;
}

// ============================================================================
// Compiler: Expression
// ============================================================================

static struct value *
compile_expr(void) {
    skip_ws();

    if (*POS == '[') {
        return compile_list();
    }

    char *tok = parse_token();
    if (!tok) { die("unexpected character"); }

    // Number?
    if (tok_is_number(tok)) {
        struct value *v = make_int(atol(tok));
        emit(OP_PUSH);
        emit_ptr(v);
        free(tok);
        return v;
    }

    // Boolean?
    if (strcmp(tok, "true") == 0) {
        struct value *v = make_bool(1);
        emit(OP_PUSH);
        emit_ptr(v);
        free(tok);
        return v;
    }
    if (strcmp(tok, "false") == 0) {
        struct value *v = make_bool(0);
        emit(OP_PUSH);
        emit_ptr(v);
        free(tok);
        return v;
    }

    // Primitive?
    int op = lookup_prim(tok);
    if (op >= 0) {
        emit(op);
        struct value *v = make_sym(tok);
        free(tok);
        return v;
    }

    // User definition?
    int def_idx = lookup_def(tok);
    if (def_idx < 0) {
        // Forward reference: create placeholder
        def_idx = add_def(tok);
    }
    emit(OP_CALL);
    emit(def_idx);
    struct value *v = make_sym(tok);
    free(tok);
    return v;
}

// ============================================================================
// Compiler: DEFINE
// ============================================================================

static void
compile_define(void) {
    char *name = parse_token();
    if (!name) { die("DEFINE: expected name"); }

    skip_ws();
    if (POS[0] != '=' || POS[1] != '=') { die("DEFINE: expected =="); }
    POS += 2;

    int def_idx = lookup_def(name);
    if (def_idx < 0) {
        def_idx = add_def(name);
    }

    DEFS[def_idx].addr = CODE_LEN;
    DEFS[def_idx].compiled = 1;

    while (!looking_at('.') && !at_end()) {
        compile_expr();
    }
    if (!looking_at('.')) { die("DEFINE: expected ."); }
    POS++;

    emit(OP_RET);
    free(name);
}

// ============================================================================
// Compiler: Top Level
// ============================================================================

static int
compile(const char *src) {
    INPUT = strdup(src);
    POS = INPUT;

    int entry = -1;

    while (!at_end()) {
        skip_ws();
        if (strncmp(POS, "DEFINE", 6) == 0 && is_ws(POS[6])) {
            POS += 6;
            compile_define();
        } else {
            // First non-DEFINE expression marks entry point
            if (entry < 0) { entry = CODE_LEN; }
            compile_expr();
        }
    }

    if (entry < 0) { entry = CODE_LEN; }  // no main code, just definitions
    emit(OP_HALT);
    free(INPUT);
    return entry;
}

// ============================================================================
// VM: Execute Quotation (helper for combinators)
// ============================================================================

static void run_from(int addr);

static int EXEC_DEPTH = 0;

static void
exec_quotation(struct value *q) {
    if (!q->code || q->code_len == 0) {
        die("cannot execute: no compiled code");
    }

    int saved_ip = IP;
    int start = CODE_LEN;

    EXEC_DEPTH++;

    // Copy quotation code to main code buffer
    for (int i = 0; i < q->code_len; i++) {
        emit(q->code[i]);
    }

    run_from(start);
    IP = saved_ip;

    EXEC_DEPTH--;
    // Only reclaim space at outermost level to avoid overwriting nested code
    if (EXEC_DEPTH == 0) {
        CODE_LEN = start;
    }
}

// ============================================================================
// VM: Combinator Helpers
// ============================================================================

static void
save_stack(void) {
    // Grow save stack if needed
    if (SAVE_SP >= SAVE_CAP) {
        int old_cap = SAVE_CAP;
        SAVE_CAP = SAVE_CAP ? SAVE_CAP * 2 : 64;
        SAVE_STACK = realloc(SAVE_STACK, SAVE_CAP * sizeof(struct saved_state));
        // Zero new entries
        memset(SAVE_STACK + old_cap, 0, (SAVE_CAP - old_cap) * sizeof(struct saved_state));
    }

    struct saved_state *s = &SAVE_STACK[SAVE_SP++];
    if (SP > s->cap) {
        s->cap = SP ? SP * 2 : 16;
        s->data = realloc(s->data, s->cap * sizeof(struct value *));
    }
    s->sp = SP;
    if (SP > 0) {
        memcpy(s->data, STACK, SP * sizeof(struct value *));
    }
}

static void
restore_stack(void) {
    if (SAVE_SP == 0) { die("restore without save"); }
    struct saved_state *s = &SAVE_STACK[--SAVE_SP];
    SP = s->sp;
    memcpy(STACK, s->data, SP * sizeof(struct value *));
}

// ============================================================================
// VM: Main Loop
// ============================================================================

static void
run_from(int entry) {
    IP = entry;

    for (;;) {
        int op = CODE[IP++];

        switch (op) {
        case OP_NOP:
            break;

        case OP_HALT:
            return;

        case OP_PUSH: {
            struct value *v = read_ptr(IP);
            IP += 2;
            push(v);
            break;
        }

        case OP_CALL: {
            int def_idx = CODE[IP++];
            if (!DEFS[def_idx].compiled) {
                fprintf(stderr, "joy: undefined: %s\n", DEFS[def_idx].name);
                exit(1);
            }
            ret_push(IP);
            IP = DEFS[def_idx].addr;
            break;
        }

        case OP_RET:
            if (RSP == 0) { return; }
            IP = ret_pop();
            break;

        // Stack operations
        case OP_DUP:
            require(1, "dup");
            push(peek(0));
            break;

        case OP_SWAP: {
            require(2, "swap");
            struct value *a = pop();
            struct value *b = pop();
            push(a);
            push(b);
            break;
        }

        case OP_POP:
            require(1, "pop");
            pop();
            break;

        case OP_ROLLUP: {
            require(3, "rollup");
            struct value *c = pop();
            struct value *b = pop();
            struct value *a = pop();
            push(b);
            push(c);
            push(a);
            break;
        }

        case OP_ROLLDOWN: {
            require(3, "rolldown");
            struct value *c = pop();
            struct value *b = pop();
            struct value *a = pop();
            push(c);
            push(a);
            push(b);
            break;
        }

        case OP_DUPD: {
            require(2, "dupd");
            struct value *a = pop();
            struct value *b = peek(0);
            push(b);
            push(a);
            break;
        }

        case OP_POPD: {
            require(2, "popd");
            struct value *a = pop();
            pop();
            push(a);
            break;
        }

        case OP_SWAPD: {
            require(3, "swapd");
            struct value *c = pop();
            struct value *b = pop();
            struct value *a = pop();
            push(b);
            push(a);
            push(c);
            break;
        }

        case OP_STACK: {
            struct value **items = malloc(SP * sizeof(struct value *));
            for (int i = 0; i < SP; i++) {
                items[i] = STACK[SP - 1 - i];
            }
            push(make_list(items, SP - 0));  // -0 because we already have SP items
            free(items);
            break;
        }

        case OP_UNSTACK: {
            require(1, "unstack");
            struct value *l = pop();
            if (!is_list(l)) { die_type("unstack", "list"); }
            SP = 0;
            for (int i = l->list.len - 1; i >= 0; i--) {
                push(l->list.items[i]);
            }
            break;
        }

        // Arithmetic
        case OP_ADD: {
            require(2, "+");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("+", "two integers"); }
            push(make_int(a->num + b->num));
            break;
        }

        case OP_SUB: {
            require(2, "-");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("-", "two integers"); }
            push(make_int(a->num - b->num));
            break;
        }

        case OP_MUL: {
            require(2, "*");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("*", "two integers"); }
            push(make_int(a->num * b->num));
            break;
        }

        case OP_DIV: {
            require(2, "/");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("/", "two integers"); }
            if (b->num == 0) { die("division by zero"); }
            push(make_int(a->num / b->num));
            break;
        }

        case OP_MOD: {
            require(2, "%");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("%", "two integers"); }
            if (b->num == 0) { die("modulo by zero"); }
            push(make_int(a->num % b->num));
            break;
        }

        case OP_SUCC: {
            require(1, "succ");
            struct value *a = pop();
            if (!is_int(a)) { die_type("succ", "integer"); }
            push(make_int(a->num + 1));
            break;
        }

        case OP_PRED: {
            require(1, "pred");
            struct value *a = pop();
            if (!is_int(a)) { die_type("pred", "integer"); }
            push(make_int(a->num - 1));
            break;
        }

        case OP_ABS: {
            require(1, "abs");
            struct value *a = pop();
            if (!is_int(a)) { die_type("abs", "integer"); }
            push(make_int(a->num < 0 ? -a->num : a->num));
            break;
        }

        case OP_NEG: {
            require(1, "neg");
            struct value *a = pop();
            if (!is_int(a)) { die_type("neg", "integer"); }
            push(make_int(-a->num));
            break;
        }

        case OP_MAX: {
            require(2, "max");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("max", "two integers"); }
            push(make_int(a->num > b->num ? a->num : b->num));
            break;
        }

        case OP_MIN: {
            require(2, "min");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("min", "two integers"); }
            push(make_int(a->num < b->num ? a->num : b->num));
            break;
        }

        // Comparison
        case OP_EQ: {
            require(2, "=");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("=", "two integers"); }
            push(make_bool(a->num == b->num));
            break;
        }

        case OP_LT: {
            require(2, "<");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("<", "two integers"); }
            push(make_bool(a->num < b->num));
            break;
        }

        case OP_GT: {
            require(2, ">");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type(">", "two integers"); }
            push(make_bool(a->num > b->num));
            break;
        }

        case OP_LE: {
            require(2, "<=");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("<=", "two integers"); }
            push(make_bool(a->num <= b->num));
            break;
        }

        case OP_GE: {
            require(2, ">=");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type(">=", "two integers"); }
            push(make_bool(a->num >= b->num));
            break;
        }

        case OP_NE: {
            require(2, "!=");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_int(a) || !is_int(b)) { die_type("!=", "two integers"); }
            push(make_bool(a->num != b->num));
            break;
        }

        // Logic
        case OP_AND: {
            require(2, "and");
            struct value *b = pop();
            struct value *a = pop();
            push(make_bool(is_truthy(a) && is_truthy(b)));
            break;
        }

        case OP_OR: {
            require(2, "or");
            struct value *b = pop();
            struct value *a = pop();
            push(make_bool(is_truthy(a) || is_truthy(b)));
            break;
        }

        case OP_NOT: {
            require(1, "not");
            struct value *a = pop();
            push(make_bool(!is_truthy(a)));
            break;
        }

        // List operations
        case OP_FIRST: {
            require(1, "first");
            struct value *l = pop();
            if (!is_list(l)) { die_type("first", "list"); }
            if (l->list.len == 0) { die("first on empty list"); }
            push(l->list.items[0]);
            break;
        }

        case OP_REST: {
            require(1, "rest");
            struct value *l = pop();
            if (!is_list(l)) { die_type("rest", "list"); }
            if (l->list.len == 0) { die("rest on empty list"); }
            push(make_list(l->list.items + 1, l->list.len - 1));
            break;
        }

        case OP_CONS: {
            require(2, "cons");
            struct value *l = pop();
            struct value *x = pop();
            if (!is_list(l)) { die_type("cons", "list"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = l;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = x;
            struct value **items = malloc((l->list.len + 1) * sizeof(struct value *));
            items[0] = x;
            for (int i = 0; i < l->list.len; i++) {
                items[i + 1] = l->list.items[i];
            }
            push(make_list(items, l->list.len + 1));
            free(items);
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_UNCONS: {
            require(1, "uncons");
            struct value *l = pop();
            if (!is_list(l)) { die_type("uncons", "list"); }
            if (l->list.len == 0) { die("uncons on empty list"); }
            push(make_list(l->list.items + 1, l->list.len - 1));
            push(l->list.items[0]);
            break;
        }

        case OP_SWONS: {
            require(2, "swons");
            struct value *x = pop();
            struct value *l = pop();
            if (!is_list(l)) { die_type("swons", "list"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = l;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = x;
            struct value **items = malloc((l->list.len + 1) * sizeof(struct value *));
            items[0] = x;
            for (int i = 0; i < l->list.len; i++) {
                items[i + 1] = l->list.items[i];
            }
            push(make_list(items, l->list.len + 1));
            free(items);
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_NULL: {
            require(1, "null");
            struct value *l = pop();
            if (!is_list(l)) { die_type("null", "list"); }
            push(make_bool(l->list.len == 0));
            break;
        }

        case OP_SIZE: {
            require(1, "size");
            struct value *l = pop();
            if (!is_list(l)) { die_type("size", "list"); }
            push(make_int(l->list.len));
            break;
        }

        case OP_CONCAT: {
            require(2, "concat");
            struct value *b = pop();
            struct value *a = pop();
            if (!is_list(a) || !is_list(b)) { die_type("concat", "two lists"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = a;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = b;
            int len = a->list.len + b->list.len;
            struct value **items = malloc(len * sizeof(struct value *));
            for (int i = 0; i < a->list.len; i++) {
                items[i] = a->list.items[i];
            }
            for (int i = 0; i < b->list.len; i++) {
                items[a->list.len + i] = b->list.items[i];
            }
            push(make_list(items, len));
            free(items);
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_LIST: {
            require(1, "list");
            struct value *x = pop();
            push(make_list(&x, 1));
            break;
        }

        case OP_REVERSE: {
            require(1, "reverse");
            struct value *l = pop();
            if (!is_list(l)) { die_type("reverse", "list"); }
            struct value **items = malloc(l->list.len * sizeof(struct value *));
            for (int i = 0; i < l->list.len; i++) {
                items[i] = l->list.items[l->list.len - 1 - i];
            }
            push(make_list(items, l->list.len));
            free(items);
            break;
        }

        case OP_AT: {
            require(2, "at");
            struct value *idx = pop();
            struct value *l = pop();
            if (!is_list(l)) { die_type("at", "list"); }
            if (!is_int(idx)) { die_type("at", "integer index"); }
            if (idx->num < 0 || idx->num >= l->list.len) {
                die("at: index out of bounds");
            }
            push(l->list.items[idx->num]);
            break;
        }

        // Combinators
        case OP_I: {
            require(1, "i");
            struct value *q = pop();
            if (!is_callable(q)) { die_type("i", "quotation"); }
            exec_quotation(q);
            break;
        }

        case OP_X: {
            require(1, "x");
            struct value *q = peek(0);
            if (!is_callable(q)) { die_type("x", "quotation"); }
            exec_quotation(q);
            break;
        }

        case OP_DIP: {
            require(2, "dip");
            struct value *q = pop();
            struct value *x = pop();
            if (!is_callable(q)) { die_type("dip", "quotation"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = x;
            exec_quotation(q);
            push(x);
            TEMP_ROOT_COUNT--;
            break;
        }

        case OP_IFTE: {
            require(3, "ifte");
            struct value *f = pop();
            struct value *t = pop();
            struct value *cond = pop();
            if (!is_callable(cond) || !is_callable(t) || !is_callable(f)) {
                die_type("ifte", "three quotations");
            }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = t;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = f;
            save_stack();
            exec_quotation(cond);
            struct value *result = pop();
            restore_stack();
            exec_quotation(is_truthy(result) ? t : f);
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_TIMES: {
            require(2, "times");
            struct value *q = pop();
            struct value *n = pop();
            if (!is_int(n)) { die_type("times", "integer"); }
            if (!is_callable(q)) { die_type("times", "quotation"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = q;
            for (long i = 0; i < n->num; i++) {
                exec_quotation(q);
            }
            TEMP_ROOT_COUNT--;
            break;
        }

        case OP_MAP: {
            require(2, "map");
            struct value *q = pop();
            struct value *l = pop();
            if (!is_list(l)) { die_type("map", "list"); }
            if (!is_callable(q)) { die_type("map", "quotation"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = q;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = l;
            struct value **results = malloc(l->list.len * sizeof(struct value *));
            for (int i = 0; i < l->list.len; i++) {
                push(l->list.items[i]);
                exec_quotation(q);
                results[i] = pop();
                TEMP_ROOTS[TEMP_ROOT_COUNT++] = results[i];
            }
            push(make_list(results, l->list.len));
            free(results);
            TEMP_ROOT_COUNT -= l->list.len + 2;
            break;
        }

        case OP_FOLD: {
            require(3, "fold");
            struct value *q = pop();
            struct value *init = pop();
            struct value *l = pop();
            if (!is_list(l)) { die_type("fold", "list"); }
            if (!is_callable(q)) { die_type("fold", "quotation"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = q;
            push(init);
            for (int i = 0; i < l->list.len; i++) {
                push(l->list.items[i]);
                exec_quotation(q);
            }
            TEMP_ROOT_COUNT--;
            break;
        }

        case OP_FILTER: {
            require(2, "filter");
            struct value *q = pop();
            struct value *l = pop();
            if (!is_list(l)) { die_type("filter", "list"); }
            if (!is_callable(q)) { die_type("filter", "quotation"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = q;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = l;
            struct value **results = malloc(l->list.len * sizeof(struct value *));
            int count = 0;
            for (int i = 0; i < l->list.len; i++) {
                push(l->list.items[i]);
                push(l->list.items[i]);
                exec_quotation(q);
                struct value *pred = pop();
                if (is_truthy(pred)) {
                    results[count++] = pop();
                } else {
                    pop();
                }
            }
            push(make_list(results, count));
            free(results);
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_STEP: {
            require(2, "step");
            struct value *q = pop();
            struct value *l = pop();
            if (!is_list(l)) { die_type("step", "list"); }
            if (!is_callable(q)) { die_type("step", "quotation"); }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = q;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = l;
            for (int i = 0; i < l->list.len; i++) {
                push(l->list.items[i]);
                exec_quotation(q);
            }
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_LINREC: {
            require(4, "linrec");
            struct value *r2 = pop();
            struct value *r1 = pop();
            struct value *t = pop();
            struct value *p = pop();
            if (!is_callable(p) || !is_callable(t) ||
                !is_callable(r1) || !is_callable(r2)) {
                die_type("linrec", "four quotations");
            }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = p;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = t;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = r1;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = r2;

            int depth = 0;
            for (;;) {
                save_stack();
                exec_quotation(p);
                struct value *cond = pop();
                int done = is_truthy(cond);
                restore_stack();
                if (done) {
                    exec_quotation(t);
                    break;
                }
                exec_quotation(r1);
                depth++;
            }
            while (depth > 0) {
                exec_quotation(r2);
                depth--;
            }
            TEMP_ROOT_COUNT -= 4;
            break;
        }

        case OP_TAILREC: {
            require(3, "tailrec");
            struct value *r = pop();
            struct value *t = pop();
            struct value *p = pop();
            if (!is_callable(p) || !is_callable(t) || !is_callable(r)) {
                die_type("tailrec", "three quotations");
            }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = p;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = t;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = r;
            for (;;) {
                save_stack();
                exec_quotation(p);
                struct value *cond = pop();
                int done = is_truthy(cond);
                restore_stack();
                if (done) {
                    exec_quotation(t);
                    break;
                }
                exec_quotation(r);
            }
            TEMP_ROOT_COUNT -= 3;
            break;
        }

        case OP_WHILE: {
            require(2, "while");
            struct value *body = pop();
            struct value *cond = pop();
            if (!is_callable(cond) || !is_callable(body)) {
                die_type("while", "two quotations");
            }
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = cond;
            TEMP_ROOTS[TEMP_ROOT_COUNT++] = body;
            for (;;) {
                save_stack();
                exec_quotation(cond);
                struct value *result = pop();
                int cont = is_truthy(result);
                restore_stack();
                if (!cont) { break; }
                exec_quotation(body);
            }
            TEMP_ROOT_COUNT -= 2;
            break;
        }

        case OP_CHOICE: {
            require(3, "choice");
            struct value *f = pop();
            struct value *t = pop();
            struct value *cond = pop();
            push(is_truthy(cond) ? t : f);
            break;
        }

        // I/O
        case OP_PRINT: {
            require(1, ".");
            struct value *v = pop();
            print_value(v);
            printf("\n");
            break;
        }

        case OP_PUT: {
            require(1, "put");
            struct value *v = pop();
            print_value(v);
            break;
        }

        case OP_PUTCH: {
            require(1, "putch");
            struct value *v = pop();
            if (!is_int(v)) { die_type("putch", "integer"); }
            putchar((char)v->num);
            break;
        }

        case OP_GET: {
            int c = getchar();
            push(make_int(c == EOF ? -1 : c));
            break;
        }

        default:
            fprintf(stderr, "joy: unknown opcode %d\n", op);
            exit(1);
        }
    }
}

// ============================================================================
// File Reading
// ============================================================================

static char *
read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "joy: cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static char *
read_stdin(void) {
    char *buf = malloc(MAX_INPUT);
    size_t len = 0;
    int c;
    while ((c = getchar()) != EOF && len < MAX_INPUT - 1) {
        buf[len++] = c;
    }
    buf[len] = '\0';
    return buf;
}

// ============================================================================
// Entry Point
// ============================================================================

static void
usage(void) {
    fprintf(stderr, "Usage: joy [FILE]\n");
    fprintf(stderr, "       joy -e 'EXPR'\n");
    exit(1);
}

int
main(int argc, char **argv) {
    char *src = NULL;

    if (argc == 1) {
        src = read_stdin();
    } else if (argc == 2 && strcmp(argv[1], "-h") == 0) {
        usage();
    } else if (argc == 3 && strcmp(argv[1], "-e") == 0) {
        src = strdup(argv[2]);
    } else if (argc == 2) {
        src = read_file(argv[1]);
    } else {
        usage();
    }

    int entry = compile(src);
    free(src);

    run_from(entry);
    print_stack();

    return 0;
}
