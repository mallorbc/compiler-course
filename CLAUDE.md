# CLAUDE.md

This file is current project guidance for any coding agent working in this
repository. The earlier frontend-only description was retired when Issue #1's
compiler pipeline was completed.

## Project status

This is a complete handwritten compiler for the EECS 6083 course language. It
checks source, builds and verifies project-owned typed IR, emits restricted C,
and can invoke a host C toolchain to publish a native executable. The design is
deliberately an evolution of the 2019 code, not a replacement frontend.

Authoritative language material is in `docs/assignment/`; use the 2024 document
for current semantics and `PROVENANCE.md` when vintage behavior matters. The
completion summary and explicit policy choices are in `docs/FINAL_REPORT.md`.

## Commands

```sh
make                          # build ./compiler with C++17
make clean                    # remove build products
make unit                     # doctest unit suite
make cli                      # process-level frontend/CLI checks
make check                    # 210-program byte-exact golden corpus
make codegen                  # strict generated-C compile/runtime checks
make native                   # host-toolchain and native runtime checks
make test                     # all five layers above

./compiler SOURCE             # check only; no artifact
./compiler --emit-c OUT.c SOURCE
./compiler -o OUT SOURCE      # native executable
```

Use `python3 tests/run_golden.py --update` only for an intended, reviewed
check-only behavior change. Never hand-edit golden output files.

## Architecture

- `scanner` remains handwritten and lazily supplies the parser's token window.
- `parser` remains recursive descent and invokes semantic checking while it
  parses; there is no generated parser or full-AST rewrite.
- `SymbolTable` owns retained unique lexical scopes. Scope `0` is global; the
  scanner's lexeme map is only an interning cache. Resolution is current local,
  owning procedure/self, then source-ordered global.
- `Typechecker` returns synthesized expression shapes and pure conversion plans.
  The old streaming accumulator remains inert compatibility surface and must
  not be reintroduced into live parsing.
- `SemanticTypes` and `BuiltinCatalog` are the shared semantic vocabulary.
  The catalog is the only production source for the nine builtin signatures.
- `IR`/`IRBuilder` provide strong IDs, scalar/array shapes, CFG, calls, storage,
  verifier invariants, and atomic FrontendError/InvalidIR/Unsupported status.
- `RestrictedCEmitter` consumes finalized IR only. It knows nothing about
  scanner/parser tokens and returns C plus typed link metadata.
- `NativeToolchain` is the sole host-process seam. It uses literal argv with
  `posix_spawnp`, validates a private product, and atomically renames only after
  every earlier phase succeeds.

Diagnostics are accumulated by the parser and printed to stdout for historical
compatibility. A diagnostic marks the IR frontend-error state; emit/native
modes additionally report their phase status on stderr and never publish a
partial artifact.

## Supported and excluded behavior

Supported code generation includes all four primitive types, all nine
builtins, scalar and aggregate expressions/conversions, checked arrays,
conditionals, finite loops, procedures, nested declarations, recursion, manual
frames, and deterministic typed fallthrough defaults.

Do not silently broaden these deliberate boundaries:

- LLVM is a future IR consumer; no LLVM dependency belongs in the frontend.
- Vintage primitive `type` aliases remain accepted and lower as their
  underlying primitive. Enum/unresolved types remain frontend-valid but are
  atomically Unsupported by the 2024-target backend.
- Nested procedures do not capture enclosing procedure locals. The recovered
  spec does not define that ABI; globals and self recursion are supported.
- `return` is procedure-only. The canonical `put*` signatures return Bool
  success/failure, following the displayed signatures where the prose differs.
- Conditions and procedure return values are scalar, and direct array
  assignment never broadcasts.
- The restricted machine is a fixed 64 MiB word-addressed model. Runtime bounds,
  integer division by zero, frame, and String-heap exhaustion terminate the
  generated program with status 1.
- Native mode is POSIX/Linux; keep platform policy isolated from the IR/emitter.

Some professor fixtures contain source-level mistakes. In particular,
`recursiveFib.src` publishes successfully but drives recursion negative and
hits frame capacity; `test1.src` and `test1b.src` are frontend-invalid. Tests
pin those observed outcomes rather than repairing the fixtures.

## Change rules

- Preserve the recognizable scanner/parser/typechecker organization unless a
  requirement genuinely forces a change.
- Production compiler-core functionality must remain project-owned. Test-only
  libraries are permitted; doctest is currently vendored under `tests/vendor/`.
- Prefer backend-neutral semantics in IR over C-specific parser behavior.
- Keep failures atomic: invalid source exposes no usable IR, and failed emit or
  native publication leaves no new output.
- Run `make test` before committing. For memory-sensitive work also run the
  ASan+UBSan recipe recorded in `docs/FINAL_VERIFICATION.md`.
- `master` and tag `v0-course-baseline` are the frozen 2019 baseline. Work stays
  on `finish-compiler`; do not merge or push without Blake's explicit approval.
- `docs/audit/AUDIT.md`, `docs/audit/raw/`, and dated notes are historical
  evidence. Add current corrections to the final report or a new dated note
  instead of rewriting what those snapshots observed.
