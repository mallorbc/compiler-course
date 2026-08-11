# Stage 2D: scalar assignments and loop conditions

SIL-2 validates scalar assignment after the handwritten parser has completed
both destination and expression. The direct primitive matrix accepts equal
types, bool/integer pairs, and integer/float pairs; bool/float remains
incompatible and strings are exact-only. A rejected assignment is syntactically
valid, reports once at the destination occurrence, and suppresses only that
semantic statement without touching the retired accumulator.

The 2024 specification says compatible assignments cast to the target type.
There is no AST/IR value that can own such a conversion yet, so this slice
validates compatibility only. A lowering-time typed value/IR node must record
the actual conversion later; parser tokens are deliberately not mutated into a
target type.

TY-6 treats the `for` initializer and condition as separate semantic units.
After a consumed initializer semicolon, `begin_loop_condition` resets
suppression and legacy tokens, anchors the condition at its first token, and
lets a bad initializer and a bad condition report independently. The loop
checker runs only after a syntactically complete closing `)`, accepts bool or
integer, and reports the dedicated loop wording otherwise.

Arrays, unqualified whole-array operations, index type/bounds checks, array
shape/length validation, and enum identity are intentionally deferred to TY-8.
The scalar layer neither reads nor changes `is_array`, so existing array probes
retain their current behavior.
