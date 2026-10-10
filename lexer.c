#pragma once

#include <stdio.h>
#include <stdbool.h>

#define MAX_TOKEN_LEN 10000

#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

typedef enum {
    TOKEN_EMPTY = 0,
    TOKEN_WORD,
    TOKEN_LITERAL,
    TOKEN_IO,

    TOKEN_COUNT
} TokenType;

char *gTokenLiteralsReferenceTable[TOKEN_COUNT] = {
    [TOKEN_EMPTY] = "TOKEN_EMPTY",
    [TOKEN_IO] = "TOKEN_IO",
    [TOKEN_WORD] = "TOKEN_WORD",
    [TOKEN_LITERAL] = "TOKEN_LITERAL",
};

typedef enum {
    STATE_START = 1,
    STATE_TWO,
    STATE_WORD,
    STATE_EMPTY,
    STATE_IO,
    STATE_SIX,
    STATE_SEVEN,
    STATE_LITERAL,
    STATE_NINE,
    STATE_INVALID,

    STATE_END
} State;
#define STATE_COUNT (STATE_END - STATE_START)

typedef struct {
    TokenType type;
    const char *value; // points into the input string, not NUL-terminated
    size_t len;
} Token;

#define STATE_COUNT (STATE_END - STATE_START)
#define TRANSITIONS_PER_STATE 128
#define TRANSITIONS_TABLE_SIZE (STATE_COUNT * TRANSITIONS_PER_STATE)

/**
    STATE_START_TRANSITIONS = {
        [STATE_TWO] = [[45, 58], [63, 95], [97, 122], 126],
        [STATE_IO] = [26, 60, 62, 124],
        [STATE_SIX] = [34],
        [STATE_START] = [9, 10, 32],
        [STATE_EMPTY] = [0]
    }
    STATE_TWO_TRANSITIONS = {
        [STATE_TWO] = [[45, 58], [63, 95], [97, 122], 126],
        [STATE_WORD] = [0, 32],
    }
    STATE_SIX_TRANSITIONS = {
        [STATE_SEVEN] = [34],
        [STATE_NINE] = [[32, 33], [35, 126]],
    }
    STATE_NINE_TRANSITIONS = {
        [STATE_SEVEN] = [34],
        [STATE_NINE] = [[32, 33], [35, 126]],
    }
    STATE_SEVEN_TRANSITIONS = {
        STATE_LITERAL = [0, 32]
    }
 */
#define COMPUTE_OFFSET(state) ((state - STATE_START) * TRANSITIONS_PER_STATE)
// Range designators require GNU C compiler extensions.
// Use `-std=gnuXX`. 
#define SET_VALUE_AT_RANGE(start, end, offset, value) \
[start+offset ... end+offset] = value,
#define SET_VALUE_AT_INDEX(idx, value) \
[idx] = value,
#define INIT_TRANSITION_TABLE \
    [0 ... TRANSITIONS_TABLE_SIZE - 1] = STATE_INVALID, \
    SET_VALUE_AT_RANGE(45, 58, COMPUTE_OFFSET(STATE_START), STATE_TWO) \
    SET_VALUE_AT_RANGE(63, 95, COMPUTE_OFFSET(STATE_START), STATE_TWO) \
    SET_VALUE_AT_RANGE(97, 122, COMPUTE_OFFSET(STATE_START), STATE_TWO) \
    SET_VALUE_AT_INDEX(126 + COMPUTE_OFFSET(STATE_START), STATE_TWO) \
    SET_VALUE_AT_INDEX(26 + COMPUTE_OFFSET(STATE_START), STATE_IO) \
    SET_VALUE_AT_INDEX(60 + COMPUTE_OFFSET(STATE_START), STATE_IO) \
    SET_VALUE_AT_INDEX(62 + COMPUTE_OFFSET(STATE_START), STATE_IO) \
    SET_VALUE_AT_INDEX(124 + COMPUTE_OFFSET(STATE_START), STATE_IO) \
    SET_VALUE_AT_INDEX(34 + COMPUTE_OFFSET(STATE_START), STATE_SIX) \
    SET_VALUE_AT_INDEX(9 + COMPUTE_OFFSET(STATE_START), STATE_START) \
    SET_VALUE_AT_INDEX(10 + COMPUTE_OFFSET(STATE_START), STATE_START) \
    SET_VALUE_AT_INDEX(32 + COMPUTE_OFFSET(STATE_START), STATE_START) \
    SET_VALUE_AT_INDEX(0 + COMPUTE_OFFSET(STATE_START), STATE_EMPTY) \
    SET_VALUE_AT_RANGE(45, 58, COMPUTE_OFFSET(STATE_TWO), STATE_TWO) \
    SET_VALUE_AT_RANGE(63, 95, COMPUTE_OFFSET(STATE_TWO), STATE_TWO) \
    SET_VALUE_AT_RANGE(97, 122, COMPUTE_OFFSET(STATE_TWO), STATE_TWO) \
    SET_VALUE_AT_INDEX(126 + COMPUTE_OFFSET(STATE_TWO), STATE_TWO) \
    SET_VALUE_AT_RANGE(0, 32, COMPUTE_OFFSET(STATE_TWO), STATE_WORD) \
    SET_VALUE_AT_INDEX(34 + COMPUTE_OFFSET(STATE_SIX), STATE_SEVEN) \
    SET_VALUE_AT_RANGE(32, 33, COMPUTE_OFFSET(STATE_SIX), STATE_NINE) \
    SET_VALUE_AT_RANGE(35, 126, COMPUTE_OFFSET(STATE_SIX), STATE_NINE) \
    SET_VALUE_AT_INDEX(34 + COMPUTE_OFFSET(STATE_NINE), STATE_SEVEN) \
    SET_VALUE_AT_RANGE(32, 33, COMPUTE_OFFSET(STATE_NINE), STATE_NINE) \
    SET_VALUE_AT_RANGE(35, 126, COMPUTE_OFFSET(STATE_NINE), STATE_NINE) \
    SET_VALUE_AT_RANGE(0, 32, COMPUTE_OFFSET(STATE_SEVEN), STATE_LITERAL)

// The DFA transition table is first zeroed to STATE_INVALID,
// then specific entries are overridden with valid transition targets.
// Suppress the compiler warning for intentional override-init behavior.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif

static State gTransitionsTable[TRANSITIONS_TABLE_SIZE] = {
    INIT_TRANSITION_TABLE
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static int compute_index(State s, char c) {
    int offset = (s - STATE_START) * TRANSITIONS_PER_STATE;
    return offset + (int)c;
}

State run_transition(State state, char input_char, const State *transition_table) {
    // `char` may be signed: non-ASCII bytes would index outside the table.
    if ((unsigned char)input_char >= TRANSITIONS_PER_STATE)
        return STATE_INVALID;
    int index = compute_index(state, input_char);
    return transition_table[index];
}

bool get_tokens_for_string(const char *input_string, size_t s_len, 
    Token tokens_out[], char *error_msg, int *num_token) {
    int token_len = 0;
    State state = STATE_START;
    for (int cursor = 0; cursor < s_len; cursor++) {
        Token token;
        char c = input_string[cursor];

        state = run_transition(state, c, gTransitionsTable);
        switch(state) {
            case STATE_START:
                // Whitespace between tokens is skipped, not part of any token.
                continue;
            case STATE_EMPTY:
                token.type = TOKEN_EMPTY;
                token_len++;
                break;
            case STATE_IO:
                token.type = TOKEN_IO;
                token_len++;
                break;
            case STATE_LITERAL:
                token.type = TOKEN_LITERAL;
                token_len++;
                break;
            case STATE_WORD:
                token.type = TOKEN_WORD;
                token_len++;
                break;
            case STATE_INVALID:
                // TODO: replace tmp with an actual error.
                sprintf(error_msg, "invalid syntax: %s", input_string+cursor);
                return false;
            default:
                token_len++;
                continue;
        }

        token.value = input_string + cursor + 1 - token_len;
        token.len = token_len;
        tokens_out[*num_token] = token;
        *num_token += 1;
        token_len = 0;
        state = STATE_START;
        LOG("Lexer: Found token: %s\n", gTokenLiteralsReferenceTable[token.type]);
    }
    return true;
};