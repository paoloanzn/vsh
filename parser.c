// Parser: a table-driven LL(1) parser. It checks the tokens produced by the
// lexer against the grammar below and builds an abstract syntax tree (AST).

#pragma once

#include "lexer.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Debug logging: prints only when compiled with -DDEBUG (see compile.sh).
#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

// Grammar
// <program> ::= <arg> <tail> | _epsilon_
// <tail>    ::= <program> | io <program>
// <arg>     ::= word | literal

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
// Number of terminal symbols.
#define TERM_SYM_COUNT (TERM_SYM_COUNT_RELATIVE - NON_TRM_SYM_COUNT)

// Every grammar symbol. Used for the parsing stack, as indexes of the parsing
// table and as AST node labels (through gSymbolsLiteralsReferenceTable).
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

// Printable names of the symbols, used in logs, errors and AST labels.
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

// Returns true for the terminals that match an input token. _epsilon_ and $
// are not included.
bool is_terminal(const Symbol symbol) {
    // SYMBOL_WORD <= symbol <= SYMBOL_IO
    return ((symbol >= SYMBOL_WORD) && (symbol <= SYMBOL_IO));
};
// FIRST and FOLLOW sets of the grammar, used to build the parsing table.
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

// LL(1) parsing table
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
// Dimensions: [non-terminal][next input symbol][count + production symbols].
// Cells without a production are left filled with SYMBOL_NAN.
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
    // Text of the matched token for terminals;
    // NULL for non-terminal symbols.
    char *value;
    int num_children;
    struct ASTNode *parent;
    // The node right of this one in the same production, or NULL if this is
    // the rightmost one.
    struct ASTNode *right_sibling;
    // Children are stored in reverse order: the last element is the leftmost
    // symbol of the production.
    struct ASTNode **children;
    bool (*add_children)(struct ASTNode *self, struct ASTNode *child);
} ASTNode;

#define MAX_CHILDREN_FOR_NODE 100
bool add_children(ASTNode *self, ASTNode *child) {
    if (self->num_children == MAX_CHILDREN_FOR_NODE)
        return false;
    child->parent = self;
    // Children must be added right to left.
    self->children[self->num_children] = child;
    if (self->num_children > 0)
        child->right_sibling = self->children[self->num_children - 1];
    self->num_children += 1;
    return true;
}

// Allocates a new childless node and NULL when the alloc fails.
// The node is heap allocated.
ASTNode *create_node(const char *label) {
    ASTNode *new_node = (ASTNode *)malloc(sizeof(ASTNode));
    if (new_node == NULL)
        return NULL;

    int label_len = strlen(label);
    new_node->label = (char *)malloc(label_len+1);
    strcpy(new_node->label, label);

    new_node->value = NULL;

    new_node->num_children = 0;
    new_node->children = (ASTNode **)malloc(MAX_CHILDREN_FOR_NODE*sizeof(ASTNode *));
    if (new_node->children == NULL) {
        free(new_node->label);
        free(new_node);
        return NULL;
    }

    new_node->right_sibling = NULL;
    new_node->parent = NULL;
    new_node->add_children = add_children;
    return new_node;
}

// Frees `root` and its whole subtree.
void delete_tree(ASTNode *root) {
    if (root == NULL)
        return;
    free(root->label);
    if (root->value)
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
        printf("    "); \
    } \
// Prints `node` and its entire subtree.
// Children are printed in reverser order: right to left, which
// is the storage order.
void print_node(ASTNode *node, int indent_level) {
    SET_LINE_INDENTATION(indent_level);
    printf("Label: %s | ", node->label);
    if (node->value != NULL)
        printf("Value: %s\n", node->value);
    else
        printf("NaN\n");

    if (node->num_children == 0)
        return;

    for (int i = 0; i < node->num_children; i++) {
        print_node(node->children[i], indent_level+1);
    }
    return;
}

// Capacity of the parser stack, in symbols.
#define MAX_PARSING_STACK_SIZE 100

// A fixed-capacity stack of Symbol, backed by an external buffer.
typedef struct SymbolStack {
    // Capacity of `ptr_stack`.
    size_t size;
    int top; // Always equal to last element's index + 1.
    bool (*is_empty)(struct SymbolStack *self);
    bool (*push)(struct SymbolStack *self, Symbol *symbol);
    int (*pop)(struct SymbolStack *self, Symbol symbols_out[], int num_symbols);
    // Buffer holding the elements, bottom first.
    Symbol *ptr_stack;
} SymbolStack;

bool is_empty(SymbolStack *self) {
    return (self->top == 0);
}

// Pushes a copy of `*symbol`. Returns false if the stack is full.
bool push(SymbolStack *self, Symbol *symbol) {
    if (self->top == (self->size))
        return false; // No more space on the stack.
    memcpy(self->ptr_stack+self->top, symbol, sizeof(Symbol));
    self->top += 1;
    return true;
}

// Pops up to `num_symbols` symbols into `symbols_out`, topmost first.
// Returns the number of symbols popped.
int pop(SymbolStack *self, Symbol symbols_out[], int num_symbols) {
    int copied = 0;
    // TODO: Could be optimized with one single memcpy operation.
    for (int i = 0; (i < self->top) && (i < num_symbols); i++) {
        int top_idx = self->top - 1;
        memcpy(symbols_out+i, self->ptr_stack+top_idx, sizeof(Symbol));
        self->top -= 1; copied += 1;
    }
    return copied;
}

// ##__VA_ARGS__ notation requires GNU C compiler extensions.
// Use `-std=gnuXX`. 
// Writes a formatted error message into `err`. 
#define FORMAT_ERROR(format, err, ...) \
snprintf(err, 4096, format, ##__VA_ARGS__)

// Parses `tokens`, which must end with a TOKEN_EMPTY token.
// (1) Validates a string for our grammar.
// (2) Builds the AST.
// On failure writes a message to `err_msg` and returns false.
// C implementation of https://www.geeksforgeeks.org/compiler-design/ll1-parsing-algorithm/
// with added support for AST creation.
bool parse_tokens(Token tokens[], ASTNode **out_root, char *err_msg) {
    Symbol _stack[MAX_PARSING_STACK_SIZE] = {};
    SymbolStack stack = {
        MAX_PARSING_STACK_SIZE, 0,
        is_empty, push, pop,
        _stack
    };
    // The stack always starts with the one single element inside it,
    // which must be the start symbol of the grammar.
    Symbol start_symbol = SYMBOL_PROGRAM;
    if (!stack.push(&stack, &start_symbol)) {
        FORMAT_ERROR(
            "parsing error: failed to push element onto the stack, \
            no space left on it\n", err_msg);
        return false;
    }

    ASTNode *root = create_node(gSymbolsLiteralsReferenceTable[start_symbol]);
    if (root == NULL)
        return false;

    ASTNode *active_node = root;

    // Index of the next input token, LL(1) is 1 symbol lookahead).
    int token_cursor = 0;

    while(!stack.is_empty(&stack)) {
        LOG("LL(1): Iterating, stack size=%d\n", stack.top);
        #define POPPED_BUFFER_SIZE 4096
        Symbol next_symbol = gTokenSymbolMap[tokens[token_cursor].type];
        Symbol top = stack.ptr_stack[stack.top-1];
        Symbol popped[POPPED_BUFFER_SIZE] = {};

        LOG("LL(1): Next token: %s\n", gSymbolsLiteralsReferenceTable[next_symbol]);
        // WHEN: terminal on top
        // It must match the lookahead, syntax error otherwise. 
        // Store the token text in the AST, then consume both the symbol 
        // and the token.
        if (is_terminal(top) || top == SYMBOL_EOI) {
            LOG("LL(1): Processing terminal: %s\n", gSymbolsLiteralsReferenceTable[top]);
            if (!(top == next_symbol)) {
                FORMAT_ERROR("syntax error: expected %s, got %s\n", err_msg,
                    gSymbolsLiteralsReferenceTable[top], 
                    gSymbolsLiteralsReferenceTable[next_symbol]);
                return false;
            }

            LOG("LL(1): Popping symbol: %s\n", \
                gSymbolsLiteralsReferenceTable[stack.ptr_stack[stack.top-1]]);
                
            active_node->value = (char *)malloc(tokens[token_cursor].len+1);
            memcpy(active_node->value, 
                tokens[token_cursor].value, tokens[token_cursor].len);
            active_node->value[tokens[token_cursor].len] = '\0'; 
                
            // Walk up the three to find the first available right sibling.
            // That becomes the new active node to which we continue building
            // the tree.
            ASTNode *nearest_right_sibling = active_node->right_sibling;
            while (nearest_right_sibling == NULL) {
                nearest_right_sibling = active_node->parent->right_sibling;
            }
            active_node = nearest_right_sibling;

            if (!stack.pop(&stack, popped, 1)) {
                FORMAT_ERROR(
                    "parsing error: failed to pop element \
                    from the stack\n", err_msg);
                return false;
            }
            token_cursor += 1;

            
        }
        // WHEN: Non-terminal on top.
        // Replace it with the right-hand side of the production chosen 
        // by the lookahead in the parsing table.
        if (!is_terminal(top)) {
            LOG("LL(1): Processing non-terminal: %s\n", 
                gSymbolsLiteralsReferenceTable[top]);
            if (gParsingTable[top][next_symbol][0] == SYMBOL_NAN) {
                FORMAT_ERROR("syntax error: no matching production \
                    for %s starting with %s\n", err_msg,
                    gSymbolsLiteralsReferenceTable[top],
                gSymbolsLiteralsReferenceTable[next_symbol]);
                return false;
            }

            LOG("LL(1): Popping symbol: %s\n", \
                gSymbolsLiteralsReferenceTable[stack.ptr_stack[stack.top-1]]);
            if (!stack.pop(&stack, popped, 1)) {
                FORMAT_ERROR(
                    "parsing error: failed to pop element \
                    from the stack\n", err_msg);
                return false;
            }

            // Push the production's symbols right to left, so the leftmost
            // one ends up on top. 
            int num_symbols = gParsingTable[top][next_symbol][0];
            for (int i = 0; i < num_symbols; i++) {
                Symbol symbol = gParsingTable[top][next_symbol][num_symbols-i];
                LOG("LL(1): Pushing symbol: %s\n", 
                    gSymbolsLiteralsReferenceTable[symbol]);
                // Each node pushed on the stack is added as a CHILDREN node,
                // of the current active_node.
                ASTNode *child_node = create_node(
                    gSymbolsLiteralsReferenceTable[symbol]);
                if (child_node == NULL) {
                    FORMAT_ERROR("AST error: failed to create node\n", 
                        err_msg);
                    return false;
                }
                active_node->add_children(active_node, child_node);

                if (!stack.push(&stack, &gParsingTable[top][next_symbol][num_symbols-i])) {
                    FORMAT_ERROR(
                        "parsing error: failed to push element onto the stack, \
                        no space left on it\n", err_msg);
                    return false;
                }
            }

            // Move to the leftmost child: it matches the new top of the stack.
            active_node = active_node->children[num_symbols-1];
        }


    }
    *out_root = root;
    return true;
}