# Stage 6E: typed procedure fallthrough completion

Stage 6E makes a deliberately narrow compatibility choice for a typed procedure
that reaches its closing `end procedure` without executing an explicit return.
The compiler returns the type's deterministic default: Integer `0`, Float
`+0.0`, Bool `false`, or the semantic empty String. This is a compiler
compatibility policy, not a claim that the recovered course specification
defines those values. The 2021/2023/2024 grammar permits an empty statement
list, the surviving semantics impose no definite-return rule, the original
handwritten parser never diagnosed a missing return, and the professor-provided
2019 corpus contains accepted typed procedures with no return. No surviving
document specifies what result word an eventual backend should have produced.

The rule is lowered before leaving a successfully closed source procedure by
`IRBuilder::complete_procedure_fallthrough()`. If the selected block is open,
the builder appends an ordinary exact scalar `Constant` followed by an ordinary
`Return`; if it is already terminated, completion succeeds without mutation.
This keeps the policy backend-neutral. It is not inferred by IR finalization and
is not special-cased by restricted C. A raw builder procedure that is left open
therefore remains atomically `Unsupported`, and the verifier continues to
require that every block is terminated and can reach an exact typed Return.
Invalid Program/context use or a non-scalar/unresolved return shape is
`InvalidIR`.

Reachability is structural. A straight-line or empty body, the open join of a
partial/no-else conditional, and a loop's conservatively reachable exit receive
the default. Procedure CFG lowering already avoids creating a join when every
conditional arm returns; an explicit return followed by source statements also
keeps its closed block while dead lowering is suppressed. Uncalled source
procedures receive valid IR completion so the whole module verifies, but normal
call-graph pruning still omits their labels, frames, helpers, headers, and
literal dependencies from generated C.

The parser's procedure-body success state now also reflects the grammar for a
truly empty `begin`/`end procedure` body. Malformed closing delimiters still
produce frontend errors, expose no partial IR, and cannot publish generated C.

The check-only corpus remains byte-for-byte unchanged at 210 cases (98 OK, 112
ERRORS). The six previously fallthrough-blocked local sources now cross from
frontend-valid `Unsupported` to `Ready`: `test2.src`, `test_heap.src`,
`test_program_minimal.src`, `p13_two_procs.src`, `resync/baseline.src`, and
`proc_test_noreturn.src`. `test_heap.src` now executes its recursive per-frame
String lifetime test and discards the default Integer result. Under the stated
policy, the unmodified `test2.src` and `test_program_minimal.src` print `0`;
their apparent intended nonzero results would require explicit source returns
and are not inferred from local names or final assignments.

This milestone does not repair unrelated sample-source behavior. In particular,
`recursiveFib.src` still subtracts from an unassigned, deterministically
zero-initialized `Sub.val2`, drives recursion negative, and reaches the ordinary
frame-capacity exit. Nested lexical capture, aggregate procedure results,
aggregate conditions, and production host linking remain separate work.
