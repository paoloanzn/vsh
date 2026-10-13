// Lexer: splits a command line into Tokens using a table-driven DFA
// (deterministic finite automaton). Each byte of the input moves the DFA from
// one State to the next; reaching an accepting state emits a Token.

#pragma once

#include <stdio.h>
#include <stdbool.h>

// Debug logging: prints only when compiled with -DDEBUG (see compile.sh).
#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

typedef enum {
    // A NUL byte: end of input. Being 0, it is also the type of any
    // zero-initialized Token, which the parser reads as end of input.
    TOKEN_EMPTY = 0,
    // An unquoted word, e.g. a command name or an argument.
    TOKEN_WORD,
    // A double-quoted string; its value includes the surrounding quotes.
    TOKEN_LITERAL,
    // A single I/O operator character: '|', '<' or '>'
    TOKEN_IO,

    TOKEN_COUNT
} TokenType;

// Printable names of the token types, used for debug logging.
char *gTokenLiteralsReferenceTable[TOKEN_COUNT] = {
    [TOKEN_EMPTY] = "TOKEN_EMPTY",
    [TOKEN_IO] = "TOKEN_IO",
    [TOKEN_WORD] = "TOKEN_WORD",
    [TOKEN_LITERAL] = "TOKEN_LITERAL",
};

// DFA states. Numbered states (TWO, SIX, SEVEN, NINE) are intermediate: the
// lexer keeps consuming input. Named states (WORD, EMPTY, IO, LITERAL) are
// accepting: the lexer emits a token and goes back to STATE_START.
typedef enum {
    // Between tokens; whitespace is skipped here.
    STATE_START = 1,
    // Inside an unquoted word.
    STATE_TWO,
    // Accepting: a word ended by whitespace or NUL.
    STATE_WORD,
    // Accepting: a NUL byte found between tokens.
    STATE_EMPTY,
    // Accepting: an I/O operator.
    STATE_IO,
    // Just read the opening '"' of a literal.
    STATE_SIX,
    // Just read the closing '"' of a literal.
    STATE_SEVEN,
    // Accepting: a literal ended by whitespace or NUL.
    STATE_LITERAL,
    // Inside a quoted literal.
    STATE_NINE,
    // No transition exists for the input byte: syntax error.
    STATE_INVALID,

    // Sentinel, used only to compute STATE_COUNT.
    STATE_END
} State;
#define STATE_COUNT (STATE_END - STATE_START)

// In the code this struct holds a token as a reference + len to an existing
// string. It's non-owning and can't outlive the referenced string.
typedef struct {
    TokenType type;
    const char *value; // points into the input string, not NUL-terminated
    // Number of bytes of `value` that belong to the token.
    size_t len;
} Token;

// The transition table is a flattened 2D array, [state][input byte]: each
// state owns a row with one entry per 7-bit ASCII character.
#define STATE_COUNT (STATE_END - STATE_START)
#define TRANSITIONS_PER_STATE 128 // one for each possible ASCII character.
#define TRANSITIONS_TABLE_SIZE (STATE_COUNT * TRANSITIONS_PER_STATE)

// Character classes (ASCII codes, ranges inclusive):
//   WORD    '-'..':' (45-58), '?'..'_' (63-95), 'a'..'z' (97-122), '~' (126)
//   IO      Ctrl-Z (26), '<' (60), '>' (62), '|' (124)
//   QUOTE   '"' (34)
//   BLANK   '\t' (9), '\n' (10), ' ' (32)
//   NUL     0
//   DELIM   0-32 (NUL, control characters, space)
//   STRING  32-33, 35-126 (any printable character except '"')
//
// Transitions: any (state, character) pair not listed leads to STATE_INVALID.
// [STATE_NAME] marks an accepting state, where a token is emitted.
//
//   STATE_START  --WORD-->    STATE_TWO
//                --IO-->      [STATE_IO]
//                --QUOTE-->   STATE_SIX
//                --BLANK-->   STATE_START
//                --NUL-->     [STATE_EMPTY]
//   STATE_TWO    --WORD-->    STATE_TWO
//                --DELIM-->   [STATE_WORD]
//   STATE_SIX    --QUOTE-->   STATE_SEVEN
//                --STRING-->  STATE_NINE
//   STATE_NINE   --QUOTE-->   STATE_SEVEN
//                --STRING-->  STATE_NINE
//   STATE_SEVEN  --DELIM-->   [STATE_LITERAL]

// Index of the first entry of `state`'s row in the transition table.
// It is used to compute the starting point of state's chunk containing all of
// its transitions.
#define COMPUTE_OFFSET(state) ((state - STATE_START) * TRANSITIONS_PER_STATE)
// Range designators require GNU C compiler extensions.
// Use `-std=gnuXX`. 
// Designated initializer for the entries [start, end] of a state's row.
#define SET_VALUE_AT_RANGE(start, end, offset, value) \
[start+offset ... end+offset] = value,
// Designated initializer for a single table entry.
#define SET_VALUE_AT_INDEX(idx, value) \
[idx] = value,
// Initializer list for gTransitionsTable, encoding the transitions above.
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

// Maps a (state, input byte) pair to its index in the transition table.
static int compute_index(State s, char c) {
    int offset = (s - STATE_START) * TRANSITIONS_PER_STATE;
    return offset + (int)c;
}

// Returns the state the DFA moves to when reading `input_char` in `state`.
State run_transition(State state, char input_char, const State *transition_table) {
    // `char` may be signed: non-ASCII bytes would index outside the table.
    if ((unsigned char)input_char >= TRANSITIONS_PER_STATE)
        return STATE_INVALID;
    int index = compute_index(state, input_char);
    return transition_table[index];
}

// Tokenizes the first `s_len` bytes of `input_string`.
// Tokens are appended to `tokens_out` and counted in `*num_token`; their
// values point into `input_string`, so it must outlive them.
// Returns false and writes a message to `error_msg` on invalid input.
bool get_tokens_for_string(const char *input_string, size_t s_len, 
    Token tokens_out[], char *error_msg, int *num_token) {
    // Bytes of the delimiter that ended the current token: the DFA consumes
    // it, but it is not part of the token's value.
    int trail_junk_count = 0;
    // Bytes consumed since the current token started, delimiter included.
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
                trail_junk_count++;
                break;
            case STATE_WORD:
                token.type = TOKEN_WORD;
                token_len++;
                trail_junk_count++;
                break;
            case STATE_INVALID:
                // TODO: replace tmp with an actual error.
                sprintf(error_msg, "invalid syntax: %s", input_string+cursor);
                return false;
            default:
                // Intermediate state: the token is not complete yet.
                token_len++;
                continue;
        }

        // An accepting state was reached: emit the token, then restart the
        // DFA for the next one.

        token.value = input_string + cursor + 1 - token_len;
        token.len = token_len - trail_junk_count;
        tokens_out[*num_token] = token;
        *num_token += 1;
        token_len = 0;
        trail_junk_count = 0;
        state = STATE_START;
        LOG("Lexer: Found token: %s\n", gTokenLiteralsReferenceTable[token.type]);
    }
    return true;
};