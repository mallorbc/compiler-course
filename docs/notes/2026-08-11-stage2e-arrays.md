# Stage 2E: array shapes and frontend validation

TY-8 extends the existing handwritten parser's synthesized expression result
without introducing an AST.  A declared variable or parameter now records an
element type, array bit, and inclusive upper bound.  Expression results retain
that same shape in their synthetic token fields; they remain `I_NONE` tokens
and are never declaration identities.

The adopted source policy is `integer[N]` with valid indices `0..N`; `[0]` is
therefore a one-element array.  Only a non-negative integer literal is a valid
bound.  A float or negative suffix is consumed and reported transactionally,
so no partial variable/procedure declaration is published.

Unqualified array operands lift existing unary and binary operations
elementwise.  Array/scalar operations broadcast through the operator, while
two array operands require equal upper bounds.  Relations consequently produce
boolean arrays.  Assignment permits only scalar-to-scalar and equal-bound
array-to-array values; call arguments require exact element/array/bound
signatures.  Array values cannot be procedure returns or `if`/`for`
conditions.  Indexed uses validate that the base is an array and the index is
an exact scalar integer, while retaining ordinary parser recovery.

The 2024 requirement for bounds checks is deliberately not approximated with
literal folding.  At this Stage 2E boundary every indexed access still awaited
runtime lowering, so the historical out-of-range probe remained accepted.
Stage 6D1 subsequently adds linear `CheckIndex`/element IR, MM spans, snapshots,
whole copies, elementwise assignment casts, and exact by-value calls. Stage
6D2 now adds the separate lifted unary/binary/broadcast slice while retaining
these bounds.
