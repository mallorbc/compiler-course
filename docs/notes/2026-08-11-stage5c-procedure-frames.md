# Stage 5C: scalar procedure CFG and manual frames

Stage 5C extends the project-owned typed IR and restricted-C backend without
changing the handwritten parser's grammar, resolver, or check-only output.
Procedures may now contain multi-block `if` and `for` CFGs, including early
returns and recursive calls.  The verifier requires every reachable procedure
block to reach an exact typed `Return`; a syntactically valid fallthrough
procedure remains frontend-valid but is atomically `Unsupported` for code
generation.

The restricted-C backend remains one `main` with numeric labels and gotos.  It
maps globals in canonical StorageId order to compact `MM` words, reserves compact static spill words
only for Program values that receive reachable user-call results, and uses `Reg[0]`/`Reg[1]` as
the manual SP/FP.  Each procedure frame contains saved SP, saved FP, a numeric
return continuation, a result-word address, ordered parameters, locals, and
procedure value slots.  Return dispatch is a fixed numeric if/goto chain, so
recursive reuse of one call site is safe without emitting C user functions.
Procedure memory operands are explicitly loaded through fixed scratch registers
before use and results are spilled from registers, avoiding direct MM-to-MM
assignment, arithmetic on MM cells, and nested MM indirection.  Preflight
computes the deterministic user-procedure closure from Program: the whole
module is still verified, while unreachable declarations do not emit labels,
frames, runtime helpers, or strict-C unused-label warnings.

This was the Stage 5C boundary.  Stage 6B subsequently lowers scalar Float
procedures and `sqrt`, and Stage 6C lowers scalar String handles, without
changing this frame ABI.  Stage 6D1 subsequently makes the same frames
width-aware for exact by-value array parameters, locals, and aggregate Value
spans. Stage 6D2 adds lifted array operators within those same spans.
Static-link/capture semantics for nested procedures and fallthrough/no-return
procedures remain deliberately outside the supported slice.  Nested declaration emission restores the outer IR context, but
the existing resolver policy still exposes only current/self and global names;
no enclosing-local capture was introduced.
