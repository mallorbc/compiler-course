# Stage 2C: exact procedure-call validation

SIL-1 keeps procedure declarations canonical in the retained scope graph and
keeps call values synthetic. `parse_factor` passes the callee occurrence, its
resolved canonical procedure token, and the synthetic return expression on
separate paths; no call result becomes a declaration or is looked up again.

After the handwritten parser has structurally consumed `)`, the Typechecker
compares the ordered argument results to the canonical `procedure_params`.
Arity is checked first, then exact primitive `data_types` equality. There are
no int/float or bool/int conversions at call boundaries. An unresolved
parameter or argument type is rejected explicitly rather than being treated as
a matching `TYPE_NONE` pair.

An invalid call remains syntactically valid but returns a fresh semantic-invalid
expression result. The Typechecker sets parser/type error and statement
suppression directly, without clearing the retired accumulator. A bad nested
call therefore consumes its enclosing syntax and prevents an outer signature
cascade. Unresolved/wrong-kind callees and malformed argument lists likewise
consume their grammar but never receive signature diagnostics.

This is deliberately not assignment checking (SIL-2), runtime builtin
implementation, array/index typing (TY-8), loop-specific condition checking
(TY-6), or an overhaul of the legacy compatibility accumulator. The nine
builtin declarations now participate in the same exact validation path, but
their existing return convention is unchanged.
