# Golden-file regression harness

`tests/run_golden.py` runs `./compiler` over all 210 test programs in the repo
and compares what happens against a recorded baseline. It is the tripwire for
"did my change alter compiler behaviour anywhere I did not intend?"

## Running it

Build first (the harness never builds for you), then run from anywhere — the
script locates the repo via its own path:

```sh
make
python3 tests/run_golden.py                      # verify; exit 0 iff all match
python3 tests/run_golden.py --verbose            # one line per program, not just failures
python3 tests/run_golden.py --filter scopes      # only paths containing "scopes"
python3 tests/run_golden.py --list               # recorded status of every program (no runs)
python3 tests/run_golden.py --update             # re-record the baseline, print what changed
python3 tests/run_golden.py --jobs 1             # serial (default: 8 concurrent)
```

Verify mode prints a line only for programs that diverge, then a summary table
by status class. Full suite: ~2s wall at the default 8 workers (~12s serial).
`--update` also finishes promptly now that every recorded program terminates;
it still gives each program the full 30-second budget so a new hang is recorded
honestly.

The runner itself uses Python 3.12 and only the standard library. That is a
small-harness implementation choice, not a ban on test-only libraries (the
unit layer vendors doctest); the course constraint is that compiler-core logic
such as the scanner and parser remains project-owned.

## What is covered

Every `*.src` found recursively under `testPgms/` (14) and `docs/audit/probes/`
(196) — new subdirectories are picked up automatically. `testPgms/UnitTests/`
holds one stray non-`.src` file, which the `*.src` glob naturally excludes.

**The baseline is only valid for the default build** (plain `make`: `-g`, no
optimization). Building with `-O2` changes the recorded output of 13 programs
whose behaviour rests on residual undefined behaviour (uninitialized reads —
the LX-5 class). If you change CXXFLAGS and see unexplained reds, that is the
reason; do not `--update` over them without understanding the diff.

Directory names do **not** imply pass/fail. Three programs in `correct/`
legitimately exit 1 today. Expectations come only from the recorded baseline,
never from where a file happens to live.

## What "golden" means here

A golden file records the compiler's **current** behaviour, bugs included. It
is not a statement of what the compiler *should* print. The recorded output
deliberately preserves:

- trailing blank lines produced by the double-`endl` in the error printer,
- literal typos in diagnostics (e.g. `Missing keyworkd "program"`),
- diagnostics on the wrong line number, wrong-statement wording, and every
  other known defect catalogued in `docs/audit/AUDIT.md`.

**Never hand-edit a golden file to make it look right.** Goldens are checksummed
in the manifest; an edited golden fails verification with an explicit "edited by
hand?" message rather than silently becoming the new truth.

The intended workflow when you *do* change behaviour on purpose:

1. Make the compiler change.
2. Run verify — it fails, showing exactly which programs moved and how.
3. Confirm every diff is a change you meant to make.
4. Run `--update` and commit the regenerated manifest + goldens **in the same
   commit as the compiler change**, so the diff of recorded behaviour is
   reviewable next to the code that caused it.

## Files

- `tests/manifest.json` — one entry per program: status class, exit code,
  signal, timeout budget, golden path, stdout length + SHA-256, stderr flag.
- `tests/golden/<path to program>.src.out` — byte-exact stdout, mirroring the
  source tree (e.g. `tests/golden/testPgms/correct/math.src.out`).

Manifest entry, verbatim:

```json
"testPgms/fail/math_fail.src": {
  "status": "ERRORS",
  "exit_code": 1,
  "timeout_sec": 30.0,
  "golden": "tests/golden/testPgms/fail/math_fail.src.out",
  "stdout_bytes": 220,
  "stdout_sha256": "401e5cdc4d1a57c0db38258c35595a2da44ea7b130943ac28e3b2c3f0530571e",
  "stderr_nonempty": false
}
```

## Status classes

| status    | meaning                                  | baseline count |
| --------- | ---------------------------------------- | -------------- |
| `OK`      | exited 0                                 | 98             |
| `ERRORS`  | exited nonzero, terminated normally      | 112            |
| `CRASH`   | killed by a signal (exit code is 128+n)  | 0              |
| `TIMEOUT` | still running when the budget expired    | 0              |

There are currently no recorded `CRASH` or `TIMEOUT` programs. Numeric overflow
and the formerly hanging parser-recovery cases now terminate normally with
counted diagnostics. If a future `--update` records a timeout, its verification
budget is shortened to two seconds because it has no complete stdout to compare.

Note the raw `python3 tests/run_golden.py` invocation checks only that
`./compiler` exists — it does not rebuild. Use `make check`/`make test` (or
build first) so you never verify a stale binary against fresh sources.

Everything else must produce empty stderr; unexpected stderr output is a failure.

## Failure modes it catches

- stdout differs (prints a unified diff, truncated)
- exit code, status class, or signal changed
- stderr appeared where none was recorded
- a program that used to hang now terminates, or vice versa
- a new `.src` with no recorded expectation → *"no expectation recorded — run
  --update"* (new tests cannot slip in unrecorded)
- a manifest entry whose `.src` was deleted → stale-entry failure
- a golden file edited by hand or missing from disk

Output is byte-deterministic: repeated verify runs, and serial vs parallel runs,
produce identical results.

## The unit layer (doctest)

`make unit` builds and runs `tests/unit_tests` from `tests/unit/*.cpp` — a
[doctest](https://github.com/doctest/doctest) suite (vendored single header in
`tests/vendor/`, see its README for provenance). It pins the **stable**
contracts around the handwritten scanner/parser: the scanner's token stream
(`test_scanner.cpp`), `Tolower_string`, synthesized-expression helpers, and
parser-backed semantic cases. `test_ir.cpp` covers the project-owned Stage 4A
IR builder, verifier, catalog, parser seam, Program CFG, and Stage 5C scalar
procedure/frame-lowering cases.
`test_codegen.cpp` covers the Stage 4B/4C/5A/5B/5C pure restricted-C emitter, while `test_generated_c.py` drives the
explicit `--emit-c OUTPUT SOURCE` CLI, syntax-checks generated C, and
test-compiles/runs scalar `putInteger`, branch, finite-loop, and Stage 5C
procedure/frame programs (calls, by-value parameters, recursion, early returns,
and guarded procedure division) with an available host C
compiler (test-only; production never invokes one). `make test` runs the unit,
CLI, golden, and generated-C layers.

Conventions for adding unit tests:

- **Safety rule:** a default-constructed `Typechecker` now initializes its
  `parser_parent` back-pointer to `nullptr`, but
  most legacy error paths still require a parser-backed instance. Only test
  typechecker functions you have verified never dereference the parent (the
  safety argument for the currently-tested surface is written up at the top of
  `test_support.cpp`). The isolated TY-2E helper-preservation, SIL-1
  signature-validator, Stage 2D statement-check, TY-8 shape, and Stage 2F
  conversion-plan regressions are explicit exceptions because those helpers
  guard a null parent. Do not exercise the
  legacy `feed_in_tokens` accumulator as an integration path: TY-2E removed it
  from live expression parsing, and it remains only as compatibility surface
  pending a later cleanup.
- Tests named `KNOWN-BUG ...` assert **current buggy behaviour** on purpose,
  citing the audit ID; when the bug is fixed, the failing test is flipped in
  the same commit. Fixture programs are written via `mkstemp` to `$TMPDIR` and
  cleaned up — never committed.
