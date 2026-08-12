# Compiler completion report

**Scope:** GitHub Issue #1—restricted C first, with an LLVM-ready boundary  
**Target:** recovered 2024 EECS 6083 semantics, preserving witnessed 2019
behavior where the surviving specification is silent

## Outcome

The repository now builds a working compiler that turns valid course-language
source into inspectable C11 or a native executable. The implementation improves
the submitted design rather than replacing it: the handwritten scanner,
recursive-descent parser, parser-integrated type checker, token-oriented
semantics, symbol tables, and C++ organization remain recognizable. Compiler-
core logic is project-owned; the only vendored third-party component is doctest
in the unit-test binary.

## Software structure

```text
scanner -> parser + symbols + type semantics -> verified typed IR
        -> restricted-C emitter -> POSIX toolchain adapter -> executable
```

`SemanticTypes`, the canonical nine-entry `BuiltinCatalog`, and the strong-ID
CFG/storage/value model in `IR` form the backend-neutral seam. The parser lowers
only validated semantics into provisional IR; `IRBuilder` exposes a module only
after frontend success and independent verification. `RestrictedCEmitter`
consumes IR without scanner/parser dependencies. `NativeToolchain` alone owns
host-process and linker policy. A future LLVM emitter can therefore reuse the
frontend and verified IR instead of forcing an AST/frontend rewrite.

## Build and operation

The standard-Linux prerequisites are GNU Make, a C++17 compiler, Python 3.10+,
and a C11 compiler. `make` builds the compiler; `./compiler SOURCE` checks only;
`--emit-c OUT.c SOURCE` publishes C; and `-o OUT SOURCE` publishes a native
executable. Native mode renders in memory, uses a private sibling directory and
literal `posix_spawnp` argv, validates the host product, and performs one final
rename. Every handled frontend/backend/compiler/product failure preserves an
existing output and leaves an absent output absent.

## Completed language and runtime

The compiler supports the four primitive types; declarations and shadowing;
exact procedure parameters, nested declarations, calls and recursion;
assignments and specified conversions; all expression operators; `if`/`else`,
the two-clause `for`, and procedure `return`; and all four getters, four
putters, and `sqrt`. Arrays have inclusive bounds, runtime-checked elements,
whole snapshots/copies/conversions, exact by-value parameters, lifted
operators, and scalar broadcast.

Generated C retains the course's simple machine: one `main`, numeric labels and
gotos, a fixed 64 MiB `int32_t` memory, explicit registers, manual frames, and a
downward persistent String heap. Float uses exact IEEE binary32 words.
Out-of-bounds access, integer division by zero, frame capacity, and heap/frame
collisions are controlled exits.
Source procedure fallthrough becomes an ordinary typed IR return of `0`,
`+0.0`, `false`, or empty String, preserving professor programs accepted by the
original frontend. Root/program `return` is diagnosed. Following the handout's
displayed signatures where its prose conflicts, each `put*` returns Bool
success/failure.

## Deliberate boundaries

LLVM emission and optimization remain future backend work. Vintage primitive
`type` aliases lower as their underlying primitives; enum/unresolved types are
frontend-valid but backend Unsupported, and all vintage type syntax is outside
the 2024 target. Nested procedures resolve current locals, self, and globals;
enclosing-local capture is not invented because no recovered static-link rule
defines it. Conditions and procedure results are scalar, and direct array
assignment does not broadcast. Native publication targets POSIX/Linux and the
runtime deliberately uses a bounded 64 MiB machine.

## Verification and distinctive work

The complete gate covers 147 doctest cases / 3,405 assertions, 210 golden
programs (98 OK / 112 expected errors, no crashes/timeouts), strict generated-C
runtimes, native transaction adversaries, and all eleven professor `correct/`
fixtures. The completion process also added recovered-spec provenance, a
byte-exact behavioral corpus, typed reachability/dependency pruning, IR mutation
tests, safe manual-frame/string/array runtimes, and atomic host-tool invocation.
Exact clean-export, optimized, warning-strict, sanitizer, environment, and
manifest evidence is in [FINAL_VERIFICATION.md](FINAL_VERIFICATION.md).
