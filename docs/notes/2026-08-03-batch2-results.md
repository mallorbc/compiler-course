# Batch 2 (test harness) — what was built and how it was proven

*2026-08-03. Batch 2 from `docs/audit/FIX-TIERS.md` (the HARNESS row): permanent
test infrastructure, built BEFORE further fixes so everything later lands under
regression cover. Zero compiler-source changes — behaviour is identical to
Batch 1 (`ccef008`); every change is tests, build, or docs.*

## What exists now

- **Golden-file harness** (`tests/run_golden.py`, Python 3.12 stdlib only):
  runs all **177** programs (`testPgms/` 14 + `docs/audit/probes/` 163,
  discovered recursively so new probe directories can't slip in unrecorded)
  and compares byte-exact stdout + exit code + status class against a
  committed baseline (`tests/manifest.json` + 172 goldens under
  `tests/golden/`, SHA-256 checksummed against hand edits). Baseline:
  **89 OK / 82 ERRORS / 1 CRASH / 5 TIMEOUT**. Verify ~2s (8 workers);
  `--update` re-records with a reviewable change report (~32s).
- **Unit layer** (doctest 2.4.11, vendored with provenance + license in
  `tests/vendor/`): **20 cases / 213 assertions** pinning the scanner's token
  contract, `Tolower_string`, and the provably-parser-free typechecker
  predicates. Seven `KNOWN-BUG <audit-id>` cases assert current buggy
  behaviour on purpose (flip in the fixing commit).
- **Build hardening** (Makefile): `CXXFLAGS = -g -Wall -Wextra
  -Werror=return-type -MMD -MP` — the warning class that caused the 2019
  SIGILL crashes is now a build error; auto header deps mean editing any
  header rebuilds every TU that includes it (previously `scanner.h` edits
  rebuilt only `scanner.o` — a false-green hazard for the TY-2 rebuild).
  Targets: `make unit`, `make check`, `make test`.
- Docs: CLAUDE.md refreshed (commands + post-Batch-1 broken state),
  `tests/README.md` (golden semantics, update policy, unit-layer rules),
  `.gitattributes` (goldens never EOL-translated).

## Gates (all recorded from real runs)

| Gate | Result |
|---|---|
| `make clean && make` | 0 errors; 41 `-Wall/-Wextra` warnings (recorded in FIX-TIERS as WARN-1); 0 `-Wreturn-type` |
| `make unit` | 20/20 cases, 213/213 assertions, byte-identical across runs |
| `make check` | 177/177 match, ~2.1s, serial == parallel byte-identical |
| Fresh-checkout reproduction | tracked+untracked tree materialized at a different path: `make test` green; `--update` regenerates a byte-identical manifest |
| Concurrent double-run | two simultaneous full verifies both PASS (this failed at the original 5s budget — see below) |

## Adversarial verification (two independent lenses + completeness critic)

**Execution lens** — seeded 8+ regressions into the real tree; the gate caught:
semantic flip (TY-1 bool→int: 1 program red with exact diff), message-text
change (20 programs red; byte-exact compare, length-preserving edit still
caught), re-broken Batch-1 missing-return (**build refuses** via
-Werror=return-type before any test runs), scanner lowercasing and
line-counting breaks (caught by unit layer AND goldens, including one
TIMEOUT→terminates flip), `main.cpp` exit-code regression (82 programs red),
unrecorded new file, stale manifest entry. Verdict: CONFIRMED.

**Reading lens** — full line-by-line read of the runner; independent manifest
audit (`sha256sum -c` outside Python, set-algebra manifest↔disk: exact);
timeout-transition branches, `--update` pruning bounds (symlink-escape probed),
no vacuous-pass paths, worker-exception propagation all verified. Verdict:
initially REFUTED on two blockers, both fixed and re-proven (below).

**Blockers found and fixed before commit:**

1. **5s timeout budget was flaky on the CRASH program.** The host pipes core
   dumps through apport (~1.1s per abort, serialized system-wide), so
   concurrent suite runs pushed the SIGABRT program past 5s → spurious red.
   Fixed: default budget 30s (an upper bound only genuinely-new hangs pay;
   terminating programs finish in ~2ms), baseline re-recorded, concurrent
   double-run now green. Always-fails-closed either way, but a gate that cries
   wolf gets ignored.
2. **Mid-verification tree wipe (process incident):** a verifier's blanket
   `git checkout -- .` cleanup reverted the three tracked deliverables
   (Makefile, .gitignore, CLAUDE.md). Both verifiers independently preserved
   byte-exact copies; restoration was proven by matching SHA-256 from the two
   independent sources. Lesson recorded: agents revert only files they
   themselves seeded, never blanket-checkout a shared tree.

**Completeness critic** then swept what nobody covered; its blocking items
(stale README numbers after the timeout fix, unfiled new defects, missing
ledger entries) are fixed in this commit; the rest is filed as TESTS-1..6 +
WARN-1 in FIX-TIERS.

## New defects discovered (filed in FIX-TIERS, pinned by KNOWN-BUG tests)

- **CLI-1** (Low): `./compiler <nonexistent path>` infinite-loops.
- **LX-6** (Minimal): number ending a line stamped one line late.
- **TY-10** (→TY-2): type-compat matrix is asymmetric (float-left accepts
  string/bool literal right).
- **TY-11** (Minimal): `give_token_type_name(typechecker_null)` → `""` — the
  source of the `type ""` wording in today's diagnostics.
- Plus: baseline is build-flag-dependent (13 programs' output moves under
  `-O2` — residual-UB evidence for the LX-5 class), documented in
  tests/README.md, durable fix filed as TESTS-4.

## Notes for review

- Golden = **current behaviour, bugs included**. A red `make check` means
  "behaviour changed", not "compiler wrong". Intended changes re-record via
  `--update` in the same commit as the code change, diff reviewed.
- The unit layer deliberately avoids `feed_in_tokens` internals (deleted by
  TY-2) and any typechecker error path (uninitialized `parser_parent` in the
  default ctor — safety argument in `tests/unit/test_support.cpp`).
- `test_all.sh` is superseded by `make check` but left untouched.
