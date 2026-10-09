#pragma once

/**
 * Grammar
 * <program> ::= <arg> <tail> | _epsilon_
 * <tail>    ::= <arg> <program> | io <program> | _epsilon_
 * <arg>     ::= word | literal
 * 
 */

// Note: I've noticed that things are a bit mixed up, since the lexer in lexer.c
// is also doing some grammar check.

typedef enum {
    NON_TRM_SYM_PROGRAM = 0,
    NON_TRM_SYM_TAIL,
    NON_TRM_SYM_ARG,

    NON_TRM_SYM_COUNT
} NonTerminalSymbol;

typedef enum {
    TERM_SYM_WORD = NON_TRM_SYM_COUNT,
    TERM_SYM_LITERAL,
    TERM_SYM_IO,
    TERM_SYM_EPSILON,
    TERM_SYM_EOI, // $, known as end-of-input.

    TERM_SYM_COUNT_RELATIVE
} TerminalSymbol;
#define TERM_SYM_COUNT (TERM_SYM_COUNT_RELATIVE - NON_TRM_SYM_COUNT)

typedef enum {
    SYMBOL_PROGRAM,
    SYMBOL_TAIL,
    SYMBOL_ARG,
    SYMBOL_WORD,
    SYMBOL_LITERAL,
    SYMBOL_IO,
    SYMBOL_EPSILON,
    SYMBOL_EOI,

    COUNT_SYMBOL
} Symbol;

#define SYMBOL_NAN -1 // use to fill all the spaces in the PT where no symbol is
                      // found.

/*
    first(symbol)
    | symbol    | first                        |
    |-----------+------------------------------|
    | <program> | word, literal, _epsilon_     |
    | <tail>    | word, literal, io, _epsilon_ |
    | <arg>     | word, literal                |

    follow(symbol)
    | symbol    | follow               |
    |-----------+----------------------|
    | <program> | $                    |
    | <tail>    | $                    |
    | <arg>     | word, literal, $     |
*/

/*
    |           | word            | literal         | io             | $
    |-----------+-----------------+-----------------+----------------+----------|
    | <program> | <arg> <tail>    | <arg> <tail>    |                | _epsilon_
    | <tail>    | <arg> <program> | <arg> <program> | io <program>   | _epsilon_
    | <arg>     | word            | literal         |                |
*/

// Suppress the compiler warning for intentional override-init behavior.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif

static Symbol gParsingTable[NON_TRM_SYM_COUNT][TERM_SYM_COUNT_RELATIVE][NON_TRM_SYM_COUNT+TERM_SYM_COUNT] = {
    [0 ... NON_TRM_SYM_COUNT-1][0 ... TERM_SYM_COUNT_RELATIVE-1][0 ... NON_TRM_SYM_COUNT+TERM_SYM_COUNT-1] = SYMBOL_NAN,

    [SYMBOL_PROGRAM][SYMBOL_WORD] = {SYMBOL_ARG, SYMBOL_TAIL},
    [SYMBOL_PROGRAM][SYMBOL_LITERAL] = {SYMBOL_ARG, SYMBOL_TAIL},
    [SYMBOL_PROGRAM][SYMBOL_EOI] = {SYMBOL_EPSILON},

    [SYMBOL_TAIL][SYMBOL_WORD] = {SYMBOL_ARG, SYMBOL_PROGRAM},
    [SYMBOL_TAIL][SYMBOL_LITERAL] = {SYMBOL_ARG, SYMBOL_PROGRAM},
    [SYMBOL_TAIL][SYMBOL_IO] = {SYMBOL_IO, SYMBOL_PROGRAM},
    [SYMBOL_TAIL][SYMBOL_EOI] = {SYMBOL_EPSILON},

    [SYMBOL_ARG][SYMBOL_WORD] = {SYMBOL_WORD},
    [SYMBOL_ARG][SYMBOL_LITERAL] = {SYMBOL_LITERAL},
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif