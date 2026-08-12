# Stage 5A: Program `if` CFG lowering

Stage 5A extends the project-owned IR from one straight-line Program block to
an explicit, backend-neutral Program control-flow graph.  It adds `Branch` and
`Jump` terminators while retaining function-wide `ValueId`s, the handwritten
recursive-descent parser, and the provisional/atomic IRBuilder publication
model.

After a complete, semantically valid `if` header, the parser creates exactly
three blocks in source order: then, else, and join.  An omitted source `else`
still gets an empty else block which jumps to the join.  Integer conditions are
lowered through the existing explicit `IntToBool` conversion plan.  Nested
conditionals return with their join block selected, so later outer statements
continue in the correct arm without an AST or phi values.

The verifier derives definition locations from instructions rather than adding
source/frontend metadata to `Value`.  It requires contiguous Program blocks,
reachability, one Halt, valid intra-function branch targets, and a visible
scalar-Bool branch condition.  Dominator checking rejects value leakage from a
then/else sibling into another arm or their merge.  It permits cyclic IR with
an exit for a later loop slice; the restricted-C emitter deliberately rejects
that verified cyclic CFG as Unsupported today.

Restricted C now emits each Program block as a numeric `L_f0_bN` label.  A
branch uses a simple `if (Reg[n]) goto ...;` followed by the false-edge goto;
Halt transfers to distinct `L_f0_x0`.  Division helper labels use a separate
`L_f0_dN_K` namespace, so no synthetic label can collide with an IR block.
Existing atomic output publication and frontend/error status priority remain
unchanged.

Deferred: `for` lowering, procedure CFG/emission, phi values, arrays,
runtime bounds checks, and native-executable production by the compiler.
