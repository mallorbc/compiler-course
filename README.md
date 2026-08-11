# Compiler Course

This repository contains a completed handwritten compiler for the EECS 6083
course language. It keeps the original 2019 C++ organization—scanner,
recursive-descent parser, parser-driven type checking, and symbol tables—while
adding a typed intermediate representation, an inspectable restricted-C
backend, and native executable production on standard Linux.

The compiler core is project-owned. It uses no parser, scanner, type-checker,
IR, or code-generation library. The only vendored third-party component is
doctest, used by the unit-test binary only.

The assignment PDFs were not committed in 2019. Recovered copies and their
provenance are in [docs/assignment/](docs/assignment/); the 2024 document is the
current semantic target, with the surviving 2019 behavior called out where it
differs. The implementation and verification summary is in
[docs/FINAL_REPORT.md](docs/FINAL_REPORT.md).

## Requirements

- A standard Linux/POSIX environment
- GNU Make and a C++17 compiler (`g++` by default)
- Python 3.10 or newer for the test harness
- A C11 host compiler (`cc` by default) for generated-C and native modes

Production C++ uses the standard library plus the small POSIX process and file
boundary isolated in `NativeToolchain`. Generated programs use standard C11
and add the platform math library only when reachable `sqrt` code requires it.

## Build and use

```sh
make
```

The legacy one-argument form checks a source file and writes no artifact:

```sh
./compiler program.src
```

Restricted C can be inspected directly:

```sh
./compiler --emit-c program.c program.src
```

Or compile and atomically publish a native executable:

```sh
./compiler -o program program.src
./program
```

`--output` and `--native` are aliases for `-o`. Native mode uses `cc` unless
`CC` names one literal executable or path. It never invokes a shell, never
splits `CC` into flags, and deliberately ignores `CFLAGS` and `LDFLAGS`. A
failed frontend, backend, host compilation, or product validation leaves an
existing output unchanged and does not create a previously absent output.

## Supported language

The compiler supports:

- case-insensitive identifiers, comments, literals, and the recovered program
  grammar;
- global/local variables, shadowing, nested procedure declarations, exact
  parameters, calls, recursion, and typed returns;
- `Integer`, `Float`, `Bool`, and `String` values;
- unary, arithmetic, logical/bitwise, relational, and equality expressions
  with the specified conversions;
- assignment, `if`/`else`, the course's two-clause `for`, and `return`;
- all nine canonical builtins: four getters, four putters, and `sqrt`;
- inclusive-bound arrays, checked indexing, whole snapshots/copies/conversions,
  exact by-value procedure arguments, lifted operators, and scalar broadcast.

Typed procedures that reach `end procedure` without an explicit return produce
the deterministic default for their scalar type. This preserves professor
fixtures accepted by the original frontend; the compatibility rule is emitted
as ordinary typed IR rather than hidden backend behavior.

`return` is valid only inside a procedure. The canonical catalog follows the
displayed assignment signatures for `put*`: each returns a Bool indicating
runtime output success, despite contradictory prose elsewhere in the handout.

## Architecture

```text
scanner -> recursive-descent parser + symbol/type semantics
        -> verified typed IR
        -> pure restricted-C emitter
        -> isolated native toolchain adapter -> executable
```

`SemanticTypes`, `BuiltinCatalog`, `IR`, and `IRBuilder` form a frontend-
independent contract. `RestrictedCEmitter` is one consumer; a future LLVM
backend can consume the same verified IR without replacing the frontend.

Generated C deliberately resembles the course target: one `main`, a fixed
64 MiB `int32_t` memory, numeric registers and labels, explicit gotos, manual
procedure frames, and a downward persistent String heap. Source identifiers do
not leak into the generated user-flow labels or storage layout.

## Testing

Run the complete gate with:

```sh
make test
```

It runs the doctest unit suite, CLI tests, all 210 byte-exact golden programs,
strict generated-C compilation/runtime tests, and native toolchain/runtime
tests. The current golden split is 98 successful frontend programs and 112
expected-error programs, with no crash or timeout entries. See
[tests/README.md](tests/README.md) for focused commands and baseline rules.

The legacy `test_all.sh` runner remains for historical/manual use; it is not the
authoritative gate.

## Deliberate boundaries

- LLVM emission and optimization are future backends, not part of Issue #1.
- The vintage `type`/enum syntax is outside the 2024 grammar. Primitive aliases
  are preserved and lower as their underlying primitive; enum/unresolved types
  remain frontend-valid but atomically unsupported by restricted-C/native code
  generation.
- Nested procedures resolve current locals, self, and source-ordered globals;
  enclosing-procedure local capture is not invented because the recovered spec
  defines no static-link/capture rule.
- Conditions and procedure results are scalar; assignment itself does not
  broadcast. These are language contracts, not missing array lowering.
- Native publication targets POSIX/Linux. The direct destination is checked
  fail-closed, but eliminating a hostile same-privilege replacement of an
  ancestor path component would require a Linux-specific dirfd/`renameat2`
  policy.

Historical audit snapshots remain under [docs/audit/](docs/audit/), and the
incremental implementation decisions are recorded in [docs/notes/](docs/notes/).
