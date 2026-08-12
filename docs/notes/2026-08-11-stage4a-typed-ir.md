# Stage 4A: typed scalar straight-line IR

This slice adds a project-owned IR seam without changing scanner, parser, or
CLI behaviour. It is deliberately narrower than a backend: the parser builds
a provisional module only for a single straight-line basic block per function,
then exposes it only if the complete frontend run and IR verifier succeed.
There is no C emission, LLVM dependency, runtime, or command-line output.

`SemanticTypes.h` contains the shared primitive/shape and canonical-symbol
types. `BuiltinCatalog` is the one source for all nine builtin signatures;
both the symbol table and IR external declarations consume it. `IR.h` is
frontend-independent and holds strong function/storage/value/block IDs, typed
instructions, terminators, module status, and a compact verifier. `IRBuilder`
owns provisional identity maps and function context, discarding all scratch
state on `FrontendError`, `InvalidIR`, or `Unsupported`.

The root `Program` is explicitly result-free (`TYPE_NONE` is permitted there
only); it owns the final `Halt` rather than pretending to be a Boolean
procedure. Procedures and external builtins retain exact resolved scalar return
shapes, and calls can target only those two function kinds.

The parser's composition seam is `lowered_expression` and
`lowered_destination`: synthesized frontend semantics remain token-based while
their runtime values/storages are separate IR IDs. Scalar literals, loads,
unary and exact-type binary operations, exact calls, planned scalar casts,
stores, returns, and program halt lower directly. Mixed frontend-compatible
binary operands deliberately produce `Unsupported` rather than inventing an
implicit IR conversion.

The present support boundary is intentional: conditionals, loops, arrays and
indexes, enum/unresolved runtime values, procedure fallthrough, and statements
after a return all leave the frontend result untouched but finalize as clean
`Unsupported` with an empty exposed module. Syntax/scanner/type errors take
priority as `FrontendError`; verifier/internal errors next as `InvalidIR`.
Only a finalized `Ready` module makes `can_generate_code()` true;
`frontend_valid()` retains the independent no-diagnostic frontend fact.

The direct builder/verifier and parser fixtures cover scalar value flow,
catalog identity, casts, calls, returns, atomic status discard, and a parser
round trip. The existing 210-process golden corpus remains unchanged, which is
a constraint of this internal-only slice.

Deferred: control-flow blocks/phis, arrays/runtime bounds checks, enum
representation, ABI/runtime builtin implementation, IR serialization, and any
restricted-C or LLVM emission.
