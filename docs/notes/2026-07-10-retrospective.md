# Retrospective: the 2019 code, assessed after the baseline audit

*2026-07-10 — written after the audit (`docs/audit/AUDIT.md`) and spec-lineage
reconstruction (`docs/assignment/PROVENANCE.md`) completed. This is the
project-level assessment: where things stand, what the 2019 work got right,
what it didn't, and which concepts the code shows were not yet internalized.*

## Where the project stands

Two ways to count progress:

- **By artifact**: roughly a third of a finished compiler exists. The scanner
  is ~90% right, the parser skeleton ~80%, the semantic layer perhaps 40%
  (present in form, broken in function), code generation and runtime at zero.
- **By project**: further than the artifact suggests, because the expensive
  unknowns are gone. The spec is recovered and dated, the defect inventory is
  verified (49 claims adversarially checked, 0 refuted), the fix order is
  mapped (AUDIT.md §11), and ~160 regression probes are already committed.

Remaining work, in order: one short layer of crash fixes + test harness; one
layer of mechanical parser fixes; one genuinely hard layer (the
type-propagation rebuild); then codegen and runtime — large but well-trodden.
Estimate: 8-12 working sessions to the "A" rung, with the hard thinking
concentrated in the type-system layer.

## What the 2019 code got right (favorite things)

- **The parser is an honest 1:1 transcription of the grammar.** Each
  `parse_*` function maps to its production; the code reads with the spec
  open beside it. This fidelity is what made the revival tractable — and it
  let us *date the lost spec from the code itself*, like tree rings, proving
  the global-visibility rule and `type`/`enum` support were vintage-correct
  rather than bugs.
- **Real error-recovery ambition.** ~650 lines of panic-mode resync keyed to
  a production-mirroring state enum, plus human error messages. Buggy, but
  far beyond the minimum most students attempt.
- **Failure tests existed** (`testPgms/fail/`), unprompted, in 2019. The
  testing instinct was present; the infrastructure wasn't.

## What it got wrong (least favorite thing)

**Ignoring the compiler when it was telling the answer.** Every build since
2019 printed three `-Wreturn-type` warnings — and those three warnings *are*
the three SIGILL crashes that kill 6 of the 14 test programs. The emblem:
`Typechecker.cpp:242` contains the exact `return true;` whose absence causes
the main crash — commented out, presumably mid-debugging, never restored.
The lesson generalizes: warnings are free information; a clean-warnings
policy (`-Wall -Wextra -Werror=return-type`) would have changed the
semester. The revival adopts it (AUDIT.md §9).

## Concepts the code shows were not yet internalized

1. **Types flow up a tree, not along a stream** (the deepest gap). The
   typechecker watches tokens go by (`feed_in_tokens` into a stateful
   accumulator) instead of each `parse_*` function returning the type of
   what it parsed — synthesized attributes. Every symptom traces here:
   parenthesized expressions losing their type, unary `not`/`-` desyncing
   the checker, for-conditions silently unchecked, the innermost factor's
   type standing in for whole expressions. This is the rebuild ahead.
2. **C++'s contract is real.** Missing returns in non-void functions,
   uninitialized locals (`valid_parse` in six functions, resync's
   `new_state`), an uninitialized `token` member — a consistent mental model
   that variables default to something sane and the compiler fills gaps. It
   doesn't; it hands back undefined behavior and a warning.
3. **Token ownership across recursive calls.** The recursive productions
   don't agree on who consumes what: `parse_parameter_list`'s recursion eats
   `)` and the outer frame demands another; `parse_argument_list` recurses
   without eating the comma; `parse_term` handles exactly one `*`. The
   post-condition discipline — "when this call returns, where is the
   cursor?" — wasn't yet firm.
4. **Scope is a stack of environments, not numbered buckets.** Reused scope
   ids (`number_of_scopes` decrements on exit), a flat last-write-wins name
   map, global-first lookup that inverts shadowing.

## The grade as a diagnostic

The course graded by milestone reached (Scanner/Parser: D, Semantic Analysis:
C, Code Generation: B, Runtime: A), and the 2019 grade (C/C+) located the
understanding boundary with surprising precision: the D-rung concepts
(lexing, grammar transcription, recovery structure) were solid; all four
gaps above are C-rung concepts; B and A were never reached. Finishing the
project means closing exactly those four gaps — this time with a test
harness underneath.
