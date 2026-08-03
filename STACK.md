# Stack ledger

Trunk: `master` (tag `v0-course-baseline` = 2019 submission + recovered assignment docs).
Nothing merges to master without Blake's explicit go.

| # | Branch | Purpose | Gates run | Collapse-eligible |
|---|--------|---------|-----------|-------------------|
| 1 | `finish-compiler` | Baseline audit: verified defect inventory + spec provenance before any fixes (docs/audit/AUDIT.md) | Full build (3 -Wreturn-type warnings recorded); 14-program test matrix (6 crash — documented); 49 claims adversarially verified, 0 refuted; ~160 probe programs recorded with outcomes | Pending Blake review |
| 2 | `finish-compiler` (Batch 1 commits) | Minimal-tier fixes: all 15 from FIX-TIERS.md (3 crash fixes, silent-accepts now error, truthful exit codes, warning-clean build) | 177-program before/after matrix: crashes 30→1, professor programs crashing 6→0, build warnings 3→0, zero regressions; per-fix adversarial verification 15/15 CORRECT (REJ-5 after one-line completion) — docs/notes/2026-07-10-batch1-results.md | Pending Blake review |
| 3 | `finish-compiler` (Batch 2 commit) | Test harness: golden-file runner over all 177 programs (committed baseline 89/82/1/5), doctest unit layer (20 cases/213 assertions), `-Wall -Wextra -Werror=return-type -MMD -MP` build, `make test` gate. Zero compiler-source changes. | make test green ×2 + concurrent double-run + fresh-checkout reproduction; 8+ seeded regressions all caught (2 verifier lenses + critic); 2 blockers found+fixed pre-commit — docs/notes/2026-08-03-batch2-results.md. Note: `-Wall/-Wextra` reveal 41 warnings (filed WARN-1); "warnings 3→0" in row 2 was for the plain 2019 flags. | Pending Blake review |

Next planned layers (queue in docs/audit/FIX-TIERS.md):
Batch 3 low-tier fixes (16 + CLI-1/LX-6/TY-11/WARN-1 from Batch 2
discoveries) → medium items individually → TY-2 type-propagation rebuild
(design doc first) → codegen → runtime. Critical-tier decisions pending
Blake: type/enum policy, codegen target.
