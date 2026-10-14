# vsh

> A basic shell that lets users & other processes connect to it and spy on it.

`vsh` is a tiny Unix shell written in plain C, with **zero** dependencies for the hard parts: the lexer and the parser are rolled out by hand, no `lex`, no `yacc`, no parsing library of any kind. Every command line goes through a **DFA** lexer and a table-driven **LL(1)** parser that builds an AST, then gets `fork`ed and `execv`ed with `<` and `>` redirection. Next to the shell, on its own thread, runs a TCP server bound to a port derived from the shell's PID, so anyone on the machine can `telnet` in and spy on it, while the user typing has no idea.

The idea comes from an [old UChicago OS assignment](https://www.classes.cs.uchicago.edu/archive/2003/winter/33000-1/vsh.html), the _Voyeur Shell_, a shell built for the "Department of Homeland Security" to watch everybody's Unix commands.

> [!NOTE]
> Both the lexer state machine and the parsing table are built **at compile time** with macros and designated initializers, so at runtime there is nothing to build and every step is a single array lookup. The compiled shell is barebone but extremely lightweight, **under 80KB** binary size on Linux.

---

## TL;DR
- [The lexer is a DFA](#the-lexer-is-a-dfa)
- [The parser is LL(1)](#the-parser-is-ll1)
- [Spying on the shell](#spying-on-the-shell)

---

## The lexer is a DFA

> The lexer is a deterministic finite automaton: it reads one byte at a time, moves from one state to the next, and every time it lands on an accepting state it emits a token.

Formally the lexer is the 5-tuple

$$
M = (Q, \Sigma, \delta, q_0, F)
$$

Where:

- $Q$ is a finite set of states.
- $\Sigma$ is a finite set of input symbols, the entire allowed charset (7-bit ASCII).
- $\delta$ is the transition function, also known as the **transition table**:

$$
\delta : Q \times \Sigma \rightarrow Q, \qquad \delta(q_1, s) = q_2
$$

- $q_0 \in Q$ is the start state, `STATE_START`.
- $F \subseteq Q$ is the set of final (accepting) states: `STATE_WORD`, `STATE_LITERAL`, `STATE_IO` and `STATE_EMPTY`.

There are only three kinds of tokens: a `word` (`ls`, `-la`, `/tmp`), a double-quoted `literal` (`"hello world"`), and an `io` operator (`<`, `>`, `|`). Any $(q, s)$ pair with no transition goes to `STATE_INVALID`, which is a syntax error.

### Transition function in O(1)

$\delta$ is stored as a flat 1-dimensional array, one row of 128 entries (one per ASCII character) for each state:

$$
\mathrm{max\_len} = |Q| \cdot 128
$$

$$
\mathrm{idx} = \delta(q, s) = (q - q_0) \cdot 128 + s, \qquad 0 \leq \mathrm{idx} < \mathrm{max\_len}
$$

The whole table is generated **at compile time** by a handful of macros in [lexer.c](lexer.c): the array is first filled with `STATE_INVALID`, then `SET_VALUE_AT_RANGE` and `SET_VALUE_AT_INDEX` override only the valid transitions using GNU range designators (`[start ... end] = value`). The result is a `static` array baked into the binary, and running a transition is just:

```c
return transition_table[(state - STATE_START) * 128 + c];
```

No branches on the character class, no lookups, no allocations: one byte in, one array read, next state.

## The parser is LL(1)

> The parser is a hand written, table-driven LL(1) parser: one stack, one token of lookahead, and a parsing table that tells it which production to expand. No recursion, no backtracking.

The grammar is small on purpose:

```
<program> ::= <arg> <tail> | ε
<tail>    ::= <program> | io <program>
<arg>     ::= word | literal
```

From the $\mathrm{FIRST}$ and $\mathrm{FOLLOW}$ sets of the grammar we get the parsing table $M[A, a]$, where $A$ is the non-terminal on top of the stack and $a$ is the lookahead token ($\$$ is end of input):

|             | `word`          | `literal`       | `io`             | `$`         |
|-------------|-----------------|-----------------|------------------|-------------|
| `<program>` | `<arg> <tail>`  | `<arg> <tail>`  |                  | ε           |
| `<tail>`    | `<program>`     | `<program>`     | `io <program>`   | `<program>` |
| `<arg>`     | `word`          | `literal`       |                  |             |

Every cell holds at most one production, which is exactly what makes the grammar LL(1). Like the lexer, the table in [parser.c](parser.c) is a `static` 3D array filled at compile time, so picking the next production is again one lookup, $M[A, a]$. While it parses, the parser builds an **AST** that the shell then walks to find the command, its arguments and the redirections.

## Spying on the shell

> The user of the shell should have no awareness of the remote connection.

When `vsh` starts it spins up a TCP server on a separate `pthread`, listening on a port derived from its own PID, so multiple shells can run at the same time without clashing:

$$
\mathrm{port} = 10000 + (\mathrm{pid} \bmod 55536)
$$

which always falls in the range $[10000, 65535]$, far from the privileged ports. Find the port of a running shell with [get-port.sh](get-port.sh) and connect to it:

```sh
telnet localhost $(./get-port.sh shell)
```

---

## License

[MIT](LICENSE)
