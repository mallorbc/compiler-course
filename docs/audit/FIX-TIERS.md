# Fix tiers — execution queue derived from AUDIT.md

*2026-07-10. Every confirmed audit finding assigned a tier. The tier measures
**fix complexity/ceremony** — how much design thinking, testing, tooling, or
human guidance the fix needs — NOT symptom severity (a crash can be a one-line
fix; a "cosmetic" issue can hide an architecture problem). Severity stays as
recorded in AUDIT.md.*

Tier definitions:

- **Minimal** — mechanical one-to-few-line fixes with an obvious correct
  answer and an existing probe to verify. Crankable autonomously in batches.
- **Low** — single-function fixes; correct behavior is clear from the spec;
  needs a new test or two and light care around interactions.
- **Medium** — subsystem-level changes with bounded design decisions;
  needs focused testing and self-review.
- **High** — architectural / cross-cutting; needs upfront design, deliberate
  scoping, and review before merging. AI-executable with a plan Blake has
  seen.
- **Critical** — blocked on a human decision, new scope, or new
  infrastructure choices (the "requires guidance" tier, not the "most severe
  symptom" tier).

## Minimal — 15 items (Batch 1 — **DONE 2026-07-10**, see docs/notes/2026-07-10-batch1-results.md)

| ID | Fix | Verify with |
|----|-----|-------------|
| CR-1 | Restore `return true;` at Typechecker.cpp:242 (`second_relation_token_chains`) | probes/typechecker/if_le.src + 6 crashing testPgms |
| CR-2 | Add `return true;` on the compatible-types path, Typechecker.cpp:1211 (`check_return_statement`) | probes/typechecker/c7_*.src |
| CR-3 | Add `return true;` in `create_new_scope_table`, SymbolTable.cpp:172 | probes/scopes/gp_minimal.src |
| REJ-2 | Consume the comma before recursing in `parse_argument_list` (parser.cpp:2560) | probes/expressions/call3arg.src |
| REJ-5 | Use `T_INTEGER_VALUE`/`T_FLOAT_VALUE` (not `*_TYPE`) in the unary-minus factor branch (parser.cpp:2401) | probes/expressions/p4_multminus.src |
| SIL-3 | Report an error in the leading-`&`/`|` branches instead of silently accepting (parser.cpp:2003-2017) | probes/expressions/p8_leadamp.src |
| SIL-4 | Report an error for a leading relational operator (parser.cpp:2154+) | probes/expressions/p9_leadrel.src |
| SIL-5 | Report an error for bare `!` (parser.cpp:2258) | probes/expressions/p12_barebang.src |
| TY-1 | Record `TYPE_BOOL` (not `TYPE_INT`) for bool declarations (parser.cpp:901) | probes/scopes/bool.src |
| TY-9 | Add TYPE_NONE case + default to `convert_to_typechecker_types` (Typechecker.cpp:1256) | probes/typechecker/c11_enum.src |
| SC-6 | Floor check in `update_scopes(false)` so scope id can't hit -1 (parser.cpp:3289) | probes/scopes/strayend.src |
| ER-4 | `main` returns nonzero when errors were reported; fix the contradictory success+error output | testPgms/correct/test1.src |
| ER-6 | Initialize `valid_parse` at declaration in the 5 flagged functions (parser.cpp:220, 540, 1784, 2278, 2370) | clean `-Wmaybe-uninitialized` |
| LX-4 | Record a string token's starting line; fix line-0 EOF token | probes/scanner/t_unterm_string.src |
| LX-5 | Default-initialize `token::identifier_data_type` (token.h:126) | build + existing probes |

## Low — 16 items (Batch 3 candidate, after harness)

| ID | Fix | Notes |
|----|-----|-------|
| CR-4 | try/catch around stoi/stof (scanner.cpp:402,410), emit range error | decide the error message path |
| CR-5 | No-progress guard in the declaration loop (parser.cpp:609) to kill the hang | probe under `timeout` |
| REJ-1 | Restructure `parse_parameter_list` recursion → loop (parser.cpp:1197-1233) | the ')' ownership bug |
| REJ-3 | Loop/self-recurse in `parse_term` for chained `*`/`/` (parser.cpp:2306) | mirror parse_arithOp |
| REJ-4 | Same missing-loop fix at relation level (parser.cpp:2146) | confirm grammar intent first |
| REJ-6 | Accept `_` in number scanning; strip before value conversion (scanner.cpp:363) | spec grammar `[0-9][0-9_]*` |
| SIL-8 | Report illegal characters (wire `error_detected` or report directly); also stop the char=0 priming false-positive (scanner.cpp:161,489) | spec requires reporting |
| TY-7 | Add `&`/`|` cases to `is_valid_operation` enforcing int-only bitwise / bool-only logical (Typechecker.cpp:469) | operand-type fidelity improves further with TY-2 |
| SC-5 | Ensure `update_scopes(false)` runs on malformed `end procedure` paths (parser.cpp:664) | scope leak |
| ER-1 | Reset `resync_status` on every resync exit path (parser.cpp:2942) | suppressed-errors probe pair exists |
| ER-2 | Stop `clear_error_reports()` wiping history on unterminated strings (parser.cpp:2446) | keep prior diagnostics |
| ER-3 | Line-attribution batch: use token `line_found` in Typechecker errors; restrict the prev-line heuristic; fix line-0; fix stale post-EOF lines | 4 small related fixes |
| ER-5 | Initialize `new_state`/`return_state`, add default cases in resync dispatch (parser.cpp:2627, 3046, 3286) | mechanical; deeper resync design stays as-is |
| LX-1 | Skip comment_handler while inside a `//` comment (scanner.cpp:535) | probes/scanner/t_comment*.src |
| LX-2 | Floor `nested_comment_counter` at 0; report stray `*/` (scanner.cpp:565) | leak probe exists |
| LX-3 | Report multi-decimal numbers instead of silently discarding (scanner.cpp:373) | probes/scanner/t_number.src |

## Medium — 9 items (each its own focused effort)

| ID | Work | Notes / dependencies |
|----|------|----------------------|
| HARNESS | Test runner over testPgms/ + docs/audit/probes/ with expected outcomes; `-Wall -Wextra -Werror=return-type` in the build | **Do first (Batch 2)** — everything else lands with regression cover; needs Blake's OK on tooling choice (shell vs Python) |
| SIL-1 | Procedure-call validation: callee lookup, arity, argument types — **DONE Stage 2C** | depends on SC-1/SC-4 lookups being trustworthy and REJ-1/2 (multi-param works) |
| SIL-6 | Duplicate-declaration detection (per scope; global uniqueness per spec) | needs scope-correct lookup (SC-1) |
| SIL-7 | Real undeclared-identifier diagnostics; stop auto-vivifying blank entries | avoid cascade errors; touches lookup flow |
| TY-6 | Wire loop-condition checking properly — **DONE Stage 2D/2F** | loop initialization and condition remain separate; Stage 2F routes both `if` and `for` through the shared pure condition planner |
| SC-1 | Shadowing: local-scope-first lookup; stop local decls corrupting same-named globals | the global-first short-circuit family in SymbolTable.cpp |
| SC-2 | Adopt the modern global rule (outermost scope = global) per the 2024 target doc | policy already decided (target 2024-current); vintage note in AUDIT §1 |
| SC-3 | Make global-path updates persist caller mutations (identifer_type, is_array) (SymbolTable.cpp:265) | same subsystem as SC-1; batch together |
| SC-4 | Scope-aware identifier resolution (stop flat-map last-write-wins corrupting reused names) | may fold into SC-1 work; if it grows, escalate to High |

## High — 2 work items, 6 audit IDs (design → review → build)

| ID | Work | Subsumes |
|----|------|----------|
| TY-2 | **DONE through Stage 2F frontend consumers**: each `parse_*` expression function returns its synthesized type; the live parser no longer uses the `feed_in_tokens` side-channel; assignment/return/condition consumers retain pure scalar/array conversion plans | TY-3 (parens), TY-4 (call return types), TY-5 (unary desync), TY-10 (matrix asymmetry). Conversion plans deliberately stop before backend casts, IR, or runtime representation. |
| TY-8 | **DONE through Stage 6D1 representation slice**: canonical inclusive bounds, checked element access, MM allocation, snapshots/copies, elementwise assignment casts, and exact by-value array calls | Lifted unary/binary/broadcast lowering remains Stage 6D2; compile-time OOB folding is intentionally unnecessary because checks execute at runtime |

## Critical — 3 items (blocked on Blake)

| ID | Decision / scope | Notes |
|----|-------------------|-------|
| POLICY-1 | `type`/`enum`: drop (match 2024 target) or keep as documented extension | vintage-correct feature (see AUDIT §1); pure decision, then Low-Medium implementation |
| CODEGEN | Restricted-C target chosen; **Stage 5A/5B/5C DONE** for Program/procedure CFG/manual frames; **Stage 6A/6B/6C DONE** for scalar Integer/Bool/Float/String; **Stage 6D1 DONE** for array representation, checked elements, copies/conversions, and exact calls | Lifted array operators, nested captures, and host executable production remain separate |
| RUNTIME | Stage 6A/6B/6C lower canonical scalar I/O plus `sqrt`; Stage 6D1 adds one-word-per-element arrays, flat copy/conversion loops, by-value frames, and bounds/capacity exits | Lifted/broadcast array operations and the host adapter remain separate |

## Batch 2 discoveries (2026-08-03, filed during the test-harness build)

New defects, found while writing unit tests and adversarially verifying the
harness. Each is pinned by a `KNOWN-BUG <id>` doctest case (tests/unit/) that
asserts the current buggy behaviour and gets flipped in the fixing commit:

| ID | Tier | Defect |
|----|------|--------|
| LX-6 | Minimal | A number token ending a line is stamped one line late: `build_number_token` counts the terminating newline before setting `line_found` (scanner.cpp:395-416). Fold into the ER-3 line-attribution batch. |
| TY-10 | — (subsumed by TY-2) | `token_types_compatible_at_all` is asymmetric: `ident<float>` on the left accepts a *string* or *bool* literal on the right (Typechecker.cpp:891-913) while the mirrored pair is rejected. |
| TY-11 | Minimal | `give_token_type_name(typechecker_null)` returns `""` — missing switch case, flagged by the build's one `-Wswitch` warning (Typechecker.cpp:1151). Source of the `type ""` wording in current diagnostics. |
| CLI-1 | Low | `./compiler <nonexistent path>` infinite-loops: a failed `open` sets failbit, not eofbit, so `Get_token`'s `source.eof()` guard never trips (verified hang; no CLI path — no-args or bad-path — has any test coverage). Guard in main.cpp or the scanner ctor, then add coverage. |

Already-filed defects newly pinned by unit tests: SIL-8 (the `next_char = '\0'`
priming sets `error_detected` on every file, even clean ones), LX-1, LX-2.

Also recorded: the `-Wall -Wextra` flags added in Batch 2 surface **41
warnings** (33 unused-variable, 3 unused-parameter, 2 sign-compare, 1 each
unused-but-set / switch / comment). Two are live defect evidence: the
`-Wswitch` is TY-11 and `parser.cpp:1585 unused 'types_match'` is SIL-2's
discarded result. Warning cleanup = **WARN-1 (Low)**, natural companion to
Batch 3.

Test-infrastructure follow-ups (not compiler defects; queue as touched):

- TESTS-1 (Low): golden coverage for CLI paths (no args, nonexistent file) —
  blocked on CLI-1's fix defining sane behaviour to record.
- TESTS-2 (Low): SymbolTable/ScopeTable unit tests — both are standalone-safe
  (no parser back-pointer) and encode the scope-id-reuse and global-(-1)
  gotchas TY-2/codegen will lean on.
- TESTS-3 (Low): CI — run `make test` on push so the gate outlives attention.
- TESTS-4 (Low): record build config (CXXFLAGS, g++ version) in the manifest;
  the baseline is only valid for the default `-g` build (`-O2` moves 13
  UB-carrying programs — see tests/README.md).
- TESTS-5 (defer): cache unit-test objects (doctest.h recompiles every run).
- TESTS-6 (defer): distinct exit code for worker infrastructure exceptions.

## Suggested batch order

1. **Batch 1 = Minimal tier** (15 fixes, one branch layer, verified against
   existing probes + the 6 formerly-crashing test programs).
2. **Batch 2 = HARNESS** — lock in Batch 1 with regression cover before
   touching anything subtle.
3. **Batch 3 = Low tier** (16 fixes, each landing with new harness cases).
4. **Batch 4+ = Medium items** one at a time (SC-1/SC-3/SC-4 together, then
   SIL-6/SIL-7, then SIL-1, then TY-6, then SC-2).
5. **TY-2 design doc → review with Blake → build**, then TY-8.
6. **Critical items** whenever Blake makes the calls — CODEGEN's decision can
   be made early since it shapes TY-8 and RUNTIME.

Count check: 45 audit findings + HARNESS + 3 Critical = 49 rows; every
AUDIT.md ID appears exactly once (TY-3/4/5, SIL-2 listed under TY-2's row as
subsumed).
