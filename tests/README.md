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

The manifest is canonically produced with the default build (`make`, `-g`, no
optimization). Optimized, warning-strict, and sanitizer builds are additional
validation configurations; they should verify the same recorded output, not be
used to regenerate it. Never run `--update` merely to hide a configuration-
specific difference.

Directory names do **not** imply pass/fail. Two professor fixtures in
`correct/` (`test1.src` and `test1b.src`) are intentionally pinned as frontend
errors. Expectations come only from the recorded baseline, never from where a
file happens to live.

## What "golden" means here

A golden file records the compiler's **current check-only interface**. It is a
regression oracle, not by itself a language-conformance judgment. Some output
still preserves historical formatting or wording for compatibility, while the
dated audit documents describe the state that existed when they were written;
they are not a claim that every original defect remains live.

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
procedure/frame-lowering cases and Stage 6E typed fallthrough completion.
`test_codegen.cpp` covers the Stage 4B/4C/5A/5B/5C/6A/6B/6C/6D1/6D2 pure
restricted-C emitter, while `test_generated_c.py` drives the
explicit `--emit-c OUTPUT SOURCE` CLI, syntax-checks generated C, and
test-compiles/runs scalar Integer/Bool/Float/String runtime I/O, branch, finite-loop, and Stage 5C+
procedure/frame programs (calls, by-value parameters, recursion, early returns,
typed default returns for empty, straight-line, conditional, loop, and recursive
fallthrough paths, guarded procedure division, String pools and persistent
downward heap, checked array elements, aggregate copies/conversions, exact
by-value array calls,
lifted unary/binary operations, scalar broadcasts, expanded frames, and
capacity collisions) with an available host C compiler. Stage 7 adds
`test_native_toolchain.cpp` for the isolated host adapter and
`test_native.py` for its real/fake compiler process boundary: exact argv and
typed `-lm`, hostile paths, non-shell `CC`, failed/malformed products, sentinel
preservation, aliases/symlinks, runtime I/O and traps, recursive procedures,
all 11 professor `correct/` sources with their publish/reject and representative
runtime behavior, trailing-input rejection before launch, and 32 concurrent
atomic publishers. `make native` runs that process layer;
`make test` runs the unit, CLI, golden, generated-C, and native layers.

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
- A `KNOWN-BUG ...` test normally pins current behavior until its fix lands.
  The retained TY-10 case is different: it documents asymmetry in an inert
  legacy compatibility helper that live parsing no longer calls. It is
  compatibility-surface debt, not a supported-language defect. Fixture
  programs are written via `mkstemp` to `$TMPDIR` and cleaned up—never
  committed.
