# Stage 2F: frontend consumer hardening

Stage 2F makes assignment, return, and condition compatibility explicit without
introducing an AST or backend dependency. `Typechecker::plan_target_conversion`
is a pure shape planner: it records exact, integer/float, boolean/integer, and
elementwise array conversions. It rejects unresolved shapes, scalar/array
mixes, unequal array bounds, string/non-string pairs, and the deliberately
non-transitive boolean/float pair. `plan_condition` always targets a scalar
boolean: booleans are exact, integers carry an `IntToBool` conversion plan,
and all invalid plans retain that target while never claiming elementwise
lowering.

The parser retains these local plans for a future lowering pass but does not
store them in tokens. No cast, array copy, broadcast, or runtime check is
emitted in this slice. The existing handwritten scope and synthesized-token
design remains intact.

Returns now parse their whole expression before checking context. A root-scope
return gets one focused error; an ownerless recovery scope gets no invented
return contract. `if` is now parsed in explicit delimiter/branch phases, so
empty branches, nested conditionals, and one optional `else` have clear token
ownership. Delimiter recovery leaves `else`/`end` available to the enclosing
conditional and avoids a duplicate missing-`then` error after a missing `)`.
One optional `else` is accepted; any repeated `else` tokens are consumed with
one focused repeated-else diagnostic so following statements still parse.

`parser::frontend_valid()` is intentionally minimal: it is true exactly when
no diagnostics were recorded and does not consult the legacy `errors_occured`
flag. Stage 4A changes `can_generate_code()` to the stricter finalized-IR
readiness predicate; frontend-valid programs that need control-flow or array
lowering therefore remain clean but are not yet code-generatable.
