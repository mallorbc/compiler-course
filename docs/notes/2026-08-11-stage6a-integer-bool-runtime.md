# Stage 6A: Integer/Bool builtin runtime

Stage 6A extends only the Restricted-C consumer of finalized IR.  The
handwritten scanner, parser, typechecker, typed IR, builtin catalog, and
check-only CLI output are unchanged.  This keeps the runtime decision behind
the backend-neutral `ir::Call`/`value_shape` seam for a future LLVM consumer.

`BuiltinCatalog` remains the sole signature source.  The restricted-C backend
now accepts exact canonical calls to these scalar builtins in Program and in
the reachable user-procedure closure:

| Builtin | Catalog signature | Runtime helper |
|---|---|---|
| `getInteger` | `() -> Integer` | `R_get_i32` |
| `getBool` | `() -> Bool` | `R_get_b1` |
| `putInteger` | `(Integer) -> Bool` | `R_put_i32` |
| `putBool` | `(Bool) -> Bool` | `R_put_b1` |

The output calls retain their established catalog Bool result; successful C
`printf` calls produce `1`, and failures produce `0`.  `putBool` writes
lowercase `true` or `false`, followed by a newline.

Input is deliberately deterministic and token based.  Each getter consumes
one complete whitespace-delimited token, including an overlong or malformed
one.  `getInteger` accepts a signed decimal value in the Int32 range; malformed,
EOF, or overflow yields `0`.  `getBool` accepts case-insensitive `true` and
`false`, or an Int32 decimal token normalized as zero/nonzero; malformed,
EOF, and overflow yield `false`.  The fixed emitted support uses only standard
C headers/functions (`ctype.h`, `stdint.h`, `inttypes.h`, and `stdio.h`) and is
emitted only when a reachable call requires it.

This was the Stage 6A boundary.  Stage 6B subsequently adds scalar Float I/O,
Float arithmetic/conversions, and `sqrt` while String builtins, arrays, and
other external calls remain atomically Unsupported.  The compiler still emits
C rather than invoking a host compiler; `tests/test_generated_c.py` alone
compiles/runs generated C as test tooling.
