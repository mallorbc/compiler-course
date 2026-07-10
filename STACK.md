# Stack ledger

Trunk: `master` (tag `v0-course-baseline` = 2019 submission + recovered assignment docs).
Nothing merges to master without Blake's explicit go.

| # | Branch | Purpose | Gates run | Collapse-eligible |
|---|--------|---------|-----------|-------------------|
| 1 | `finish-compiler` | Baseline audit: verified defect inventory + spec provenance before any fixes (docs/audit/AUDIT.md) | Full build (3 -Wreturn-type warnings recorded); 14-program test matrix (6 crash — documented); 49 claims adversarially verified, 0 refuted; ~160 probe programs recorded with outcomes | Pending Blake review |

Next planned layers (not yet started, order per AUDIT.md §11): crash fixes →
test harness → parser correctness → type system → codegen → runtime.
