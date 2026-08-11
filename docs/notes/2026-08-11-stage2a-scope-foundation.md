# Stage 2A scope and declaration foundation

The compiler now has one canonical declaration store per retained scope. Scope
`0` is global; the scanner's identifier cache is lexical-only and cannot carry
declaration signatures or types between occurrences.

Procedure bodies receive unique retained scope IDs with parent and owning
procedure references. Name resolution is current procedure, self-recursive
procedure, then source-ordered global. It intentionally does not capture
enclosing procedure locals: the 2024 assignment does not explicitly settle
that nested-procedure rule.

Declarations are staged before publication. Duplicate or malformed variable,
type, parameter, and procedure declarations do not overwrite or partially
publish earlier canonical symbols. Calls now resolve callee existence/kind;
argument arity and argument-type validation remain the next Stage 2 slice.
