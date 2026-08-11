# Stack ledger

Trunk is `master`; tag `v0-course-baseline` is the frozen 2019 submission plus
recovered assignment documents. Nothing merges to `master` without Blake's
explicit approval. The completed implementation is layered on
`finish-compiler` and remains local until an explicit push request.

| Layer | Commits / purpose | Durable gate |
|---|---|---|
| Baseline audit | `8380cb5`–`a26acec`: recovered specs, verified defect inventory, fix tiers | 49 claims and the original crash/hang matrix recorded under `docs/audit/` |
| Batches 1–2 | `ccef008`, `83c1cc3`: minimal crash fixes and permanent golden/doctest harness | 177-program historical baseline plus 20 initial unit cases |
| Stage 1 | `467bbe0`–`27e6e95`: CLI/scanner safety, deterministic recovery, grammar chains, diagnostics and typing | zero hangs/crashes; warning-clean default build; expanded scanner/parser units |
| Stage 2 | `2f288a5`–`0c6723d`: retained scopes, synthesized types, exact calls, assignments, conditions, arrays and returns | semantic matrices and 210-program golden corpus |
| Stage 4 | `e16e1ff`–`bfbab88`: backend-neutral typed IR, verifier, restricted-C emitter, first runtime call | direct IR mutation tests and strict deterministic C11 output |
| Stage 5 | `4872c46`–`456a9dd`: Program/procedure CFG, loops, calls, recursion and manual activation frames | dominance/CFG adversaries plus native-equivalent procedure runtimes |
| Stage 6 | `4d81803`–`267c17f`: all scalar runtimes, Strings, checked arrays, lifted operations and typed fallthrough | strict runtime/helper/capacity matrices for all language value types |
| Stage 7 | `f67bfd6`: safe host toolchain adapter and atomic native publication | fake/real toolchains, hostile paths, signals, malformed products, concurrency |
| Final grammar gate | `87b1db5`: require scanner-confirmed EOF after the final program period | 147 unit cases / 3,405 assertions; CLI; 210 goldens; generated C; all professor/native outcomes |
| Final ownership gate | `c02f253`: deterministic parser/scanner/typechecker/IR-builder lifetime with unchanged public seams | strict C++ and default full suites; ASan+UBSan; LeakSanitizer host limitation recorded |

The current pipeline is:

```text
handwritten frontend -> verified typed IR -> restricted C -> native executable
```

Issue #1 implementation is complete in the local branch. Optimized, sanitizer,
and portability evidence is recorded in `docs/FINAL_VERIFICATION.md`; only its
final tracked-export reproduction remains to be filled after the documentation
commit. Pushing, updating the GitHub issue, and any merge to `master` remain
separate user-authorized actions.
