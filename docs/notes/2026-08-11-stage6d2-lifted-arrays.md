# Stage 6D2: lifted array expressions and scalar broadcast

Stage 6D2 completes the bounded lifted-expression slice on top of Stage 6D1's
one-word-per-element representation. Unary operations are elementwise. Binary
operations accept equal-bound array/array operands or one array and one scalar;
the scalar word is broadcast without reevaluating its expression. Whole-array
assignment itself remains exact-shape and never broadcasts.

The frontend-independent IR owns one canonical result-shape rule shared by the
builder and verifier. Typed operands must have equal element types after
explicit Cast instructions. Two arrays must have the same inclusive upper
bound; an array/scalar pair derives its bound from the array. All six relations
produce Bool with that bound. Arithmetic accepts Integer and Float, `&`/`|`
accept exact Integer/Integer or Bool/Bool operands, ordering accepts Integer,
Float, and Bool, and equality additionally accepts String. String has only
`==` and `!=`, using content rather than handle identity.

The parser retains the established Typechecker diagnostics, precedence, and
left folds. At each logical, additive, relation, and multiplicative fold it
inserts only the promotions already admitted by the semantic matrix: Integer
to Float for mixed numeric operations and Bool to Integer for mixed
Bool/Integer relations. A Cast preserves its operand's scalar or array shape.
Every unqualified array name is still loaded to a snapshot Value, and every
lifted result receives a fresh aggregate Value span before a later destination
Store. Calls used as broadcast scalars therefore execute exactly once, in
source operand order; a later mutation cannot alter an earlier snapshot.

The restricted-C backend lowers every reachable aggregate Unary, Binary, and
Cast to an increasing numeric-label loop. Source words, broadcast words,
results, and helper arguments are staged through fixed Reg scratch slots.
There are no generated C loops, bound-proportional unrolling, MM-to-MM stores,
MM arithmetic operands, direct helper MM arguments, or source names. Float
operations reuse the binary32 memcpy helpers, and String equality reuses the
bounds-defensive content helper with dependency-exact emission.

Integer division checks elements in increasing index order. A zero divisor
follows the controlled status-1 exit immediately; `INT32_MIN / -1` stores
`INT32_MIN` and continues. The fresh result span means a failed lifted RHS
never begins its destination Store, although calls and output evaluated before
the failing operation remain observable. Float division and comparisons keep
their Stage 6B IEEE behavior, including unordered NaN relations.

Array Values continue to consume explicit Program temporary or procedure-frame
spans, so large bounds remain compact in C source but are capacity checked
atomically. Dead lifted procedures contribute no spans, labels, helpers, or
link metadata. Checked indexing, exact by-value calls, frame/heap collision,
scalar conditions, and scalar returns are unchanged.

This does not make every conceivable aggregate context legal. Array constants,
conditions, procedure results/returns, canonical external calls, and assignment
broadcast remain excluded by their existing scalar or exact-shape contracts.
Nested lexical captures and production host executable linking also remain
separate work.
