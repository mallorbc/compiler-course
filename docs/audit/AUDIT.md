# Baseline Audit — 2026-07-10

Full audit of the compiler as inherited from the 2019 course submission (tag
`v0-course-baseline`), performed before any fixes. Method: nine parallel
analysis agents (six code subsystems, spec conformance, spec drift, empirical
test runs) produced 79 candidate findings plus a 71-item conformance checklist;
49 non-trivially-proven claims were then re-verified by four independent
adversarial verifiers (refute-by-default, fresh code reads, scratch builds,
probe programs, gdb). **Verdict rate: 46 CONFIRMED, 3 PARTIAL, 0 REFUTED**,
plus 4 new discoveries made during verification. Everything below is confirmed
unless marked otherwise. Raw findings and verdicts: `raw/*.json`. Probe
programs (~160, a seed regression suite): `probes/`.

## TL;DR

- The compiler **crashes on 6 of the 14 provided test programs** and has **five
  distinct crash/hang defects**, four of them from a single bug family
  (missing `return` in non-void functions → g++ emits a trap → SIGILL).
- Whole categories of **valid programs cannot be parsed** (multi-parameter
  procedures, chained `a*b*c`, unary minus inside expressions) and whole
  categories of **invalid programs compile silently** (undeclared procedure
  calls, `a := & 5;`, type-mismatched assignments).
- **Semantic analysis is largely non-functional end-to-end**: the assignment
  type check result is computed then discarded, for-loop conditions are never
  type-checked (an accumulator desync eats them), and expression result types
  never propagate up the parse.
- **Spec drift is a non-issue** (§1): the code targets the recovered spec on
  every grammar fork; one scoping rule is a 2013-spec holdover; `type`/`enum`
  support matches no known spec vintage.
- Path to each grade rung mapped in §10; recommended fix order in §11.

## 1. Spec-drift verdict (was 2019-you targeting a different spec?)

**No meaningful drift.** All grammar-level forks between the 2013 and
2021/2023/2024 spec vintages land on the modern side in this code: procedure
return types (`procedure <id> : <type_mark>`), `variable <id> : <type_mark>`
declarations, no `in`/`out` parameter modes, mandatory trailing `.` after `end
program`, nested `/* */` comments, case-insensitive identifiers, procedure
calls as expression factors. The 2023 snapshot in `docs/assignment/` is the
right document to finish against.

Two nuances:

1. **Global visibility follows the 2013 rule** — a program-level declaration is
   only visible inside procedures when literally prefixed `global`
   (parser.cpp:369-373, lookup at parser.cpp:3304-3316). The 2023/2024 spec
   makes all outermost-scope declarations global regardless. This is a real
   defect against our target spec, but it is a legacy-rule holdover, not a
   random bug. (Fix = adopt the 2023 rule.)
2. **`type`/`enum` declarations** (parser.cpp:386-390, 1416) appear in *none*
   of the three spec vintages — likely a 2019-spec feature lost to time or an
   extension. Decision needed: keep as extension or drop.

## 2. Crashes and hangs (5 distinct)

| ID | Defect | Location | Trigger |
|----|--------|----------|---------|
| CR-1 | Missing `return` in non-void `second_relation_token_chains` → UB → SIGILL | Typechecker.cpp:203-243 (intended `return true;` commented out at :242) | Any `<=`, `>=`, `==`, `!=` in an if-condition or standalone relation. **Kills 6/14 test programs** (math, logicals, recursiveFib, test2, custom_math, math_fail). |
| CR-2 | Missing `return` in `check_return_statement` → SIGILL | Typechecker.cpp:1211 | Any spec-legal implicit-conversion return (int↔float, int↔bool). Bool returns crash *because of* TY-1 (bool registered as int → "compatible but not equal" path). |
| CR-3 | Missing `return` in `create_new_scope_table` → SIGILL | SymbolTable.cpp:166-172 | Any `global procedure` declaration (the global-path skips the `operator[]` auto-creation every other path does, so this function actually executes). Found during verification; adding `return true;` in a scratch build fixes it. |
| CR-4 | Unguarded `std::stoi`/`std::stof` → `std::out_of_range` → SIGABRT | scanner.cpp:410 (int), :402 (float) | Any integer literal > INT_MAX, e.g. a 40-digit number. |
| CR-5 | Infinite-loop hang (100% CPU, forever) | parser.cpp:609 | Procedure body missing `end procedure;` followed by the outer `begin` — the declaration loop never consumes the token. |

**The if-vs-for asymmetry, explained** (why `if (a <= b)` crashes but
`for (i := 0; i <= n)` doesn't): `set_statement_type(T_IF)` clears the
typechecker accumulator, so the condition's tokens stream in cleanly and reach
the UB function CR-1. The for-loop path never clears between the initializer
and the condition, so the initializer's leftover token causes every condition
token to be silently dropped by feed_in_tokens' "too many tokens" branch
(Typechecker.cpp:120-123). I.e. **for-conditions don't crash because they are
never type-checked at all** (TY-6).

## 3. Valid programs that fail to compile

| ID | Defect | Location | Repro |
|----|--------|----------|-------|
| REJ-1 | Multi-parameter procedure **declarations** rejected: the recursive `parse_parameter_list` call consumes `)`, then the outer frame demands another `)` | parser.cpp:1197-1233 | `procedure f : integer(variable a : integer, variable b : integer)` → "Missing ")" to close procedure parameter list" |
| REJ-2 | Multi-argument procedure **calls** rejected: comma never consumed before recursion | parser.cpp:2560-2562 | `x := foo(1, 2);` → "Invalid token for factor discovered" cascade |
| REJ-3 | Chained multiplication/division rejected: `parse_term` consumes one `*`/`/` then never loops (contrast `parse_arithOp`'s self-recursion) | parser.cpp:2306-2330 | `x := 1 * 2 * 3;` → "Missing \";\" to end program statement" (`1+2+3+4` works fine) |
| REJ-4 | Chained relations rejected (same missing-loop pattern at relation level) | parser.cpp:2146 | `a < b < c` fails |
| REJ-5 | Unary minus before a literal inside an expression: branch tests the type-keyword constants (`T_INTEGER_TYPE`) instead of the literal constants (`T_INTEGER_VALUE`) | parser.cpp:2401 | `x := 3 * -5;` → "Unexpected negative factor…". Top-level `x := -5;` works only because `parse_arithOp`'s leading-minus branch masks it. |
| REJ-6 | Underscore digit separators from the spec's `<number>` grammar not scanned; `1_000` lexes as three tokens and the value is wrong | scanner.cpp:363 | `1_000` → INTEGER(1), UNDERSCORE, INTEGER(0) |

## 4. Invalid programs that compile silently ("no errors")

| ID | Defect | Location | Repro |
|----|--------|----------|-------|
| SIL-1 | Call to a completely undeclared procedure: zero diagnostics (no lookup, no arity, no arg types — the call side of the typechecker doesn't exist) | parser.cpp:2550-2618 | `a := doesNotExist(1);` → "parsed successfully with no errors" |
| SIL-2 | Assignment type mismatches never reported: `check_assignment_statement` is a stub returning true AND its call-site result is discarded anyway | Typechecker.cpp:608-612; parser.cpp:1611 | `i := s;` (int := string) → no errors |
| SIL-3 | Leading/duplicated `&` or `|` with a missing operand accepted | parser.cpp:2003-2054 | `a := & 5;` and `a := b & & 5;` → no errors |
| SIL-4 | Leading relational operator with missing left operand accepted | parser.cpp:2154-2229 | `a := < 5;` → no errors |
| SIL-5 | Bare `!` (not `!=`) accepted as a valid operator | parser.cpp:2258 | confirmed by probe |
| SIL-6 | No duplicate-declaration detection anywhere; re-declaring a global with a different type silently wins | parser.cpp:466 | confirmed by probe |
| SIL-7 | Undeclared variable *reference* produces only the useless `Type "" and type "" have no valid operations` (auto-vivified blank entry), not an undeclared-identifier error | parser.cpp:1487 | `a := b + 1;` (b undeclared) |
| SIL-8 | Illegal characters skipped but never reported (`error_detected` is write-only across the codebase; spec requires reporting) | scanner.cpp:489 | confirmed by grep: 4 writes, 0 reads |

## 5. Type-system defects

| ID | Defect | Location |
|----|--------|----------|
| TY-1 | **`bool` variable declarations recorded as TYPE_INT** (copy-paste bug; three agents found it independently). Root cause of the bool-return crash path in CR-2. | parser.cpp:901 |
| TY-2 | Expression result types never propagate: `resolved_token` carries only the innermost/last operand's token; every `feed_in_tokens` return value is discarded at the call sites. if/loop/return checks therefore test the wrong type for any compound expression. | parser.cpp:2062, 2139, 2279, 2332 |
| TY-3 | Parenthesized sub-expressions lose their type entirely (default-constructed `resolved_token`): `if ((x > 3))` errors while `if (x > 3)` passes. | parser.cpp:2347-2373 |
| TY-4 | Procedure-call-as-factor loses the callee's return type the same way: `if (isPos(5))` fails the bool check; `return true;` from a bool procedure reports `type ""`. | parser.cpp:2374-2394 |
| TY-5 | Leading unary `not`/`-` desyncs the feed_in_tokens accumulator, silently disabling type checking for the **entire remaining expression**. | Typechecker.cpp:68 |
| TY-6 | For-loop conditions are never type-checked (stale-accumulator token drop — see §2 asymmetry). `check_loop_statement` itself is dead code; the live path borrows `check_if_statement` and its wording. | Typechecker.cpp:120-123, 1297; parser.cpp:1808 |
| TY-7 | `&`/`|` spec rule (integer-only bitwise / bool-only logical) never enforced; `float & float` accepted. (PARTIAL verdict: base-type incompat is still caught; the specific integer-only rule is not.) | Typechecker.cpp:469 |
| TY-8 | Typechecker is array-blind: `is_array` never consulted, index-must-be-integer unimplemented (TODO comment), no whole-array op rules. Arrays also broken at declaration: bound parsed but discarded, `is_array` never set. | Typechecker.cpp:626; parser.cpp:1289 |
| TY-9 | `convert_to_typechecker_types` has no TYPE_NONE case and no default → returns uninitialized enum for none-typed identifiers (flows into condition checks). | Typechecker.cpp:1251-1295 |

## 6. Scoping defects

| ID | Defect | Location |
|----|--------|----------|
| SC-1 | **Local shadowing of globals is broken both ways**: lookup prefers global over local (spec mandates the local win), and declaring a local with a global's name corrupts the global's recorded type. Shared root cause: the global-first short-circuit in SymbolTable's update/lookup functions ignores scope_id. | SymbolTable.cpp:136, 349 |
| SC-2 | Program-level declarations without `global` invisible inside procedures (2013-rule holdover — see §1). | parser.cpp:3304-3316 |
| SC-3 | `update_identifier_type` etc. discard the caller's mutation for globals: global vars never persist `identifer_type`, global arrays never persist `is_array`. | SymbolTable.cpp:265 |
| SC-4 | Reusing a procedure name corrupts the earlier procedure's recorded parameter signature via the scope-unaware flat `map`. | scanner.cpp:442 + SymbolTable design |
| SC-5 | Malformed `end procedure` skips `update_scopes(false)` → scope-id leak for the rest of the parse. | parser.cpp:664 |
| SC-6 | `update_scopes(false)` has no floor; a stray `end procedure` can drive `current_scope_id` to -1, colliding with the reserved global pseudo-scope (latent; harm not demonstrated in probes). | parser.cpp:3289 |

## 7. Error reporting & recovery defects

| ID | Defect | Location |
|----|--------|----------|
| ER-1 | `resync_status` left permanently true when if/loop resync exits via the T_END path → **all subsequent errors in the file are silently suppressed**. | parser.cpp:2942 |
| ER-2 | An unterminated string literal calls `clear_error_reports()` → **wipes every previously accumulated error**. | parser.cpp:2446 |
| ER-3 | Line-number misattribution, four modes: (a) "blame previous line" heuristic fires for wrong-token errors too (parser.cpp:76); (b) typechecker errors use the scanner's live cursor `Lexer->current_line` instead of the token's `line_found` (Typechecker.cpp:331 et al.); (c) first-token errors report "line 0" (parser.cpp:35); (d) post-EOF diagnostics carry stale line numbers past the end of the file. | multiple |
| ER-4 | `main()` always returns exit code 0 — parse failure is not observable by scripts/CI. Also prints the contradictory "parsed successfully with no errors" followed by error text on some inputs (test1.src). | main.cpp:29 |
| ER-5 | `resync_parser` returns uninitialized `new_state`/`return_state`/`bool` on common paths (S_PROCEDURE_BODY at parser.cpp:3133; dispatch at :2627, :3046) — confirmed UB with spurious "Error in resync start state" output. | parser.cpp:2627-3286 |
| ER-6 | Five more `valid_parse may be used uninitialized` sites: parser.cpp:220, 540, 1784, 2278, 2370 — early-error paths return an unassigned success flag. | parser.cpp |

## 8. Scanner defects (beyond CR-4, REJ-6, SIL-8)

| ID | Defect | Location |
|----|--------|----------|
| LX-1 | `/*` **inside a `//` comment** starts real block-comment suppression (comment_handler runs unconditionally); code after the line is silently swallowed. | scanner.cpp:535 |
| LX-2 | A stray `*/` drives `nested_comment_counter` negative (never resets) → a later well-formed nested comment closes one level early and **leaks commented-out text as live tokens**. | scanner.cpp:565 |
| LX-3 | `1.2.3` silently discards the scanned `1.2` (no token, no error) and re-lexes `.3` as PERIOD + INTEGER. | scanner.cpp:373 |
| LX-4 | String token line numbers reflect the closing quote's line; unterminated strings at EOF report nonsense. T_INVALID EOF token can report line 0. | scanner.cpp:531, 156 |
| LX-5 | `token::identifier_data_type` has no default initializer (every sibling field has one) — indeterminate reads throughout (latent; heap zeroing masks it). | token.h:126 |

## 9. Undefined-behavior inventory (one root pattern)

g++ flags all of these; the build was shipping with the warnings visible:

- Missing return in non-void: SymbolTable.cpp:172 (CR-3), Typechecker.cpp:243
  (CR-1), Typechecker.cpp:1211 (CR-2).
- Uninitialized locals read/returned: parser.cpp:2152/2278 (parse_relation),
  :2627/:3046/:3286 (resync), :220, :540, :1784, :2370 (ER-6).
- Uninitialized member: token.h:126 (LX-5). Missing switch coverage:
  Typechecker.cpp:1256 (TY-9).

Any fix phase should start by making the build clean under
`-Wall -Wextra -Werror=return-type -Werror=uninitialized`.

## 10. Conformance map → grade rungs

From the 71-item spec checklist (33 implemented / 14 buggy / 6 partial / 18
missing — `raw/spec-conformance.json`):

- **D rung (Scanner/Parser)**: structurally present; blocked by REJ-1..6 and
  the silent-accept family (SIL-3/4/5) before it could be called solid.
- **C rung (Semantic Analysis)**: implemented in form, non-functional in
  practice — CR-1/2, TY-1..9, SC-1..3, SIL-1/2/6/7. This is the rung the 2019
  submission was graded at; it needs a near-rebuild of the type-propagation
  design (TY-2 is architectural: synthesized types must flow up the recursive
  descent instead of the token-accumulator side-channel).
- **B rung (Code Generation)**: entirely missing. Handout offers LLVM or
  "restricted C" 3-address style output.
- **A rung (Runtime System)**: entirely missing — the 9 builtins
  (getInteger/putInteger/…/sqrt) are not even predeclared in the symbol table.
- **Gate for all rungs**: "pass all test programs" — currently 6/14 crash;
  plus ER-4 (exit code always 0) makes automated gating impossible until fixed.

## 11. Recommended fix order

1. **Stop the bleeding** (hours): add the three missing returns (CR-1/2/3),
   guard stoi/stof (CR-4), fix the hang (CR-5), make `main` return a real exit
   code (ER-4). All 14 test programs then at least run and report.
2. **Build the harness 2019-you needed**: a test runner over `testPgms/` +
   `docs/audit/probes/` (~160 ready programs) with expected-outcome tracking;
   turn on `-Wall -Wextra -Werror=return-type`. Every later fix lands with a
   regression test.
3. **Parser correctness**: REJ-1..5, SIL-3/4/5, ER-1/2/5 — mostly small,
   independent, testable fixes.
4. **Type system**: TY-1 (one line), then the architectural fix for TY-2/3/4
   (propagate synthesized types up the descent), then TY-5..9, SC-1..3,
   SIL-1/2/6/7. Decide the §1 policy questions (global rule; type/enum).
5. **Code generation** (B rung), then **runtime + builtins** (A rung), then the
   full-suite gate.

## Appendix: artifacts

- `raw/*.json` — full findings, checklist, drift analysis, empirical matrix,
  and per-claim verification verdicts with repro commands.
- `probes/<batch>/*.src` — probe programs used to confirm findings; intended
  as the seed of the regression suite (expected outcomes are recorded in the
  corresponding `raw/verdicts-*.json` / `raw/empirical.json`).
- Verification scratch builds were done outside the repo; the working tree was
  never modified by the audit.
