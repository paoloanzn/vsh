#pragma once

#include "lexer.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

/**
 * Grammar
 * <program> ::= <arg> <tail> | _epsilon_
 * <tail>    ::= <program> | io <program>
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

// Maps Token(s) from parser.c to their relative Symbol element.
Symbol static gTokenSymbolMap[TOKEN_COUNT] = {
    [TOKEN_WORD] = SYMBOL_WORD,
    [TOKEN_LITERAL] = SYMBOL_LITERAL,
    [TOKEN_EMPTY] = SYMBOL_EOI,
    [TOKEN_IO] = SYMBOL_IO
};

#define MAX_SYMBOL_LITERAL_REFERENCE_LEN 20
char *gSymbolsLiteralsReferenceTable[COUNT_SYMBOL] = {
    [SYMBOL_PROGRAM] = "<program>",
    [SYMBOL_TAIL] = "<tail>",
    [SYMBOL_ARG] = "<arg>",
    [SYMBOL_WORD] = "word",
    [SYMBOL_LITERAL] = "literal",
    [SYMBOL_IO] = "io",
    [SYMBOL_EPSILON] = "_epsilon_",
    [SYMBOL_EOI] = "$",
};

bool is_terminal(const Symbol symbol) {
    // SYMBOL_WORD <= symbol <= SYMBOL_IO
    return ((symbol >= SYMBOL_WORD) && (symbol <= SYMBOL_IO));
};
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
    | <arg>     | word, literal, io, $ |
*/

/*
    |           | word            | literal         | io             | $
    |-----------+-----------------+-----------------+----------------+----------|
    | <program> | <arg> <tail>    | <arg> <tail>    |                | _epsilon_
    | <tail>    | <program>       | <program>       | io <program>   | <program>
    | <arg>     | word            | literal         |                |
*/

// Suppress the compiler warning for intentional override-init behavior.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif

// The first element of each production rule in the table is equal to the count
// of elements it contains.
static Symbol gParsingTable[NON_TRM_SYM_COUNT][TERM_SYM_COUNT_RELATIVE][NON_TRM_SYM_COUNT+TERM_SYM_COUNT+1] = {
    [0 ... NON_TRM_SYM_COUNT-1][0 ... TERM_SYM_COUNT_RELATIVE-1][0 ... NON_TRM_SYM_COUNT+TERM_SYM_COUNT] = SYMBOL_NAN,

    [SYMBOL_PROGRAM][SYMBOL_WORD] = {2, SYMBOL_ARG, SYMBOL_TAIL},
    [SYMBOL_PROGRAM][SYMBOL_LITERAL] = {2, SYMBOL_ARG, SYMBOL_TAIL},
    [SYMBOL_PROGRAM][SYMBOL_EOI] = {1, SYMBOL_EPSILON},

    [SYMBOL_TAIL][SYMBOL_WORD] = {1, SYMBOL_PROGRAM},
    [SYMBOL_TAIL][SYMBOL_LITERAL] = {1, SYMBOL_PROGRAM},
    [SYMBOL_TAIL][SYMBOL_IO] = {2, SYMBOL_IO, SYMBOL_PROGRAM},
    [SYMBOL_TAIL][SYMBOL_EOI] = {1, SYMBOL_PROGRAM},

    [SYMBOL_ARG][SYMBOL_WORD] = {1, SYMBOL_WORD},
    [SYMBOL_ARG][SYMBOL_LITERAL] = {1, SYMBOL_LITERAL},
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

typedef struct ASTNode {
    char *label;
    char *value;
    int num_children;
    struct ASTNode **children;
    bool (*add_children)(struct ASTNode *self,
        const char *label, const char *value);
} ASTNode;

#define MAX_CHILDREN_FOR_NODE 100
ASTNode *create_node(const char *label, const char *value, size_t value_len) {
    ASTNode *new_node = (ASTNode *)malloc(sizeof(ASTNode));
    if (new_node == NULL)
        return NULL;

    int label_len = strlen(label);
    new_node->label = (char *)malloc(label_len+1);
    strcpy(new_node->label, label);

    new_node->value = (char *)malloc(value_len+1);
    memcpy(new_node->value, value, value_len);
    new_node->value[value_len] = '\0';

    new_node->num_children = 0;
    new_node->children = (ASTNode **)malloc(MAX_CHILDREN_FOR_NODE*sizeof(ASTNode *));
    if (new_node->children == NULL) {
        free(new_node->label);
        free(new_node->value);
        free(new_node);
        return NULL;
    }
    return new_node;
}

void delete_tree(ASTNode *root) {
    free(root->label);
    free(root->value);
    for (int i = 0; i < root->num_children; i++) {
        delete_tree(root->children[i]);
    }
    free(root->children);
    free(root);
    return;
}

#define SET_LINE_INDENTATION(n) \
    for (int i = 0; i < n; i++) { \
        printf("\t"); \
    }

void print_node(ASTNode *node, int indent_level) {
    SET_LINE_INDENTATION(indent_level);
    printf("Label: %s\n", node->label);
    SET_LINE_INDENTATION(indent_level);
    printf("Value: %s\n", node->value);

    if (node->num_children == 0)
        return;

    for (int i = 0; i < node->num_children; i++) {
        print_node(node->children[i], indent_level+1);
    }
    return;
}

#define MAX_PARSING_STACK_SIZE 20

typedef struct SymbolStack {
    size_t size;
    int top; // Always equal to last element's index + 1.
    bool (*is_empty)(struct SymbolStack *self);
    bool (*push)(struct SymbolStack *self, Symbol *symbol);
    int (*pop)(struct SymbolStack *self, Symbol symbols_out[], int num_symbols);
    Symbol *ptr_stack;
} SymbolStack;

bool is_empty(SymbolStack *self) {
    return (self->top == 0);
}

bool push(SymbolStack *self, Symbol *symbol) {
    if (self->top == (self->size))
        return false; // No more space on the stack.
    memcpy(self->ptr_stack+self->top, symbol, sizeof(Symbol));
    self->top += 1;
    return true;
}

int pop(SymbolStack *self, Symbol symbols_out[], int num_symbols) {
    int copied = 0;
    // Could be optimized with one single memcpy operation.
    for (int i = 0; (i < self->top) && (i < num_symbols); i++) {
        int top_idx = self->top - 1;
        memcpy(symbols_out+i, self->ptr_stack+top_idx, sizeof(Symbol));
        self->top -= 1; copied += 1;
    }
    return copied;
}

bool add_children(ASTNode *self, ASTNode *child) {
    if (self->num_children == MAX_CHILDREN_FOR_NODE)
        return false;
    self->children[self->num_children] = child;
    self->num_children += 1;
    return true;
}

bool parse_tokens(Token tokens[], ASTNode *root, char *err_msg) {
    Symbol _stack[MAX_PARSING_STACK_SIZE] = {};
    SymbolStack stack = {
        MAX_PARSING_STACK_SIZE, 0,
        is_empty, push, pop,
        _stack
    };
    Symbol start_symbol = SYMBOL_PROGRAM;
    if (!stack.push(&stack, &start_symbol)) {
        #define ERROR_MSG "parsing error: failed to push element \
        onto the stack, no space left on it\n"
        snprintf(err_msg, 
            sizeof(ERROR_MSG), ERROR_MSG);
        return false;
    }

    int token_cursor = 0;

    while(!stack.is_empty(&stack)) {
        LOG("LL(1): Iterating, stack size=%d\n", stack.top);
        #define POPPED_BUFFER_SIZE 4096
        Symbol next_symbol = gTokenSymbolMap[tokens[token_cursor].type];
        Symbol top = stack.ptr_stack[stack.top-1];
        Symbol popped[POPPED_BUFFER_SIZE] = {};

        LOG("LL(1): Next token: %s\n", gSymbolsLiteralsReferenceTable[next_symbol]);
        if (is_terminal(top) || top == SYMBOL_EOI) {
            LOG("LL(1): Processing terminal: %s\n", gSymbolsLiteralsReferenceTable[top]);
            if (!(top == next_symbol)) {
                #define ERROR_MSG "syntax error: expected %s, got %s\n"
                snprintf(err_msg, 
                    sizeof(ERROR_MSG), 
                    ERROR_MSG, gSymbolsLiteralsReferenceTable[top],
                    gSymbolsLiteralsReferenceTable[next_symbol]);
                return false;
            }

            LOG("LL(1): Popping symbol: %s\n", \
                gSymbolsLiteralsReferenceTable[stack.ptr_stack[stack.top-1]]);
            if (!stack.pop(&stack, popped, 1)) {
                #define ERROR_MSG "parsing error: failed to pop element from the stack\n"
                snprintf(err_msg, sizeof(ERROR_MSG), ERROR_MSG);
                return false;
            }
            token_cursor += 1;
            // TODO: add to AST;
        }
        if (!is_terminal(top)) {
            LOG("LL(1): Processing non-terminal: %s\n", gSymbolsLiteralsReferenceTable[top]);
            if (gParsingTable[top][next_symbol][0] == SYMBOL_NAN) {
                #define ERROR_MSG "syntax error: no matching production for %s starting with %s\n"
                snprintf(err_msg, sizeof(ERROR_MSG)+50, ERROR_MSG, 
                    gSymbolsLiteralsReferenceTable[top],
                    gSymbolsLiteralsReferenceTable[next_symbol]);
                return false;
            }

            LOG("LL(1): Popping symbol: %s\n", \
                gSymbolsLiteralsReferenceTable[stack.ptr_stack[stack.top-1]]);
            if (!stack.pop(&stack, popped, 1)) {
                #define ERROR_MSG "parsing error: failed to pop element from the stack\n"
                snprintf(err_msg, sizeof(ERROR_MSG), ERROR_MSG);
                return false;
            }

            int num_symbols = gParsingTable[top][next_symbol][0];
            for (int i = 0; i < num_symbols; i++) {
                LOG("LL(1): Pushing symbol: %s\n", 
                    gSymbolsLiteralsReferenceTable[gParsingTable[top][next_symbol][num_symbols-i]]);
                if (!stack.push(&stack, &gParsingTable[top][next_symbol][num_symbols-i])) {
                    #define ERROR_MSG "parsing error: failed to push element \
                    onto the stack, no space left on it\n"
                    snprintf(err_msg, 
                        sizeof(ERROR_MSG), ERROR_MSG);
                    return false;
                }
            }
        }


    }

    return true;
}