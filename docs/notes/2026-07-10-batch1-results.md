# Batch 1 (minimal-tier fixes) — gate results

*2026-07-10. The 15 minimal-tier fixes from `docs/audit/FIX-TIERS.md`, applied
and adversarially verified (per-fix verdicts: 14 CORRECT on first pass; REJ-5
needed a one-line completion — the negated-literal branch also had to set
`valid_parse = true` — found by the verifier, applied, re-verified).*

## Behavior change, measured over 177 programs
(14 testPgms + 163 audit probes; identical run loop before/after)

| Metric | Before | After |
|---|---|---|
| Crashes (SIGILL/SIGABRT) | 30 | **1** (scanner overflow — CR-4, Low tier, out of batch) |
| Hangs (5s timeout) | 5 | 5 (identical set — CR-5/LX-3, Low tier, out of batch) |
| Professor's 14 test programs crashing | 6 | **0** |
| Build warnings (plain `make`) | 3 | **0** (all seven TUs pass `-Werror=return-type`) |
| Invalid programs accepted silently (SIL-3/4/5 targets) | 4 | 0 — all now report errors |
| Multi-argument calls (`foo(1,2,3)`) | 4 spurious errors | parses clean |
| Negated literals in expressions (`3 * -5`) | spurious error | parses clean |
| Programs whose exit code now truthfully reports errors (ER-4) | 0 | 84 exit 1 |

Full matrices: scratchpad-only (`matrix-before.txt` / `matrix-final.txt`
diffed line-by-line during verification); every changed line classified as
intended-improvement; **zero regressions** — no valid program newly fails, no
new crash, no new hang, error counts on resync/scanner suites unchanged.

## Notable exposures (pre-existing bugs now visible, not caused by Batch 1)

- `logicals.src` no longer crashes but now *shows* the pre-existing TY-2
  type-propagation false positive ("If statements must resolve to either type
  Bool or Integer") — same class test1.src always showed.
- De-crashed `global procedure` probes now show a pre-existing TY-2 return-type
  false positive (`return type of ""`).
- These are the expected next tier of work, already queued (TY-2, High).

## Notes

- ER-4 changes the compiler's contract: error-reporting runs now exit 1.
  `test_all.sh` and any scripts that assume exit 0 still work (they echo
  output, don't test exit codes), but future tooling should test exit codes.
- Wall time for the full 177-program matrix: ~28s, dominated by the five
  5-second timeout hangs (~25s); actual compile time per program is
  milliseconds — no measurable compile-speed change from the fixes.
