# Stage 4C runtime sub-slice: `putInteger`

The restricted-C backend now recognizes one canonical external IR call:
`BuiltinId::PutInteger`, whose spelling, return shape, and ordered parameter
shape remain owned by `BuiltinCatalog`. The emitter accepts only the verified
catalog declaration and an exact one-Integer-argument/Bool-result `Call` in
the root Program. It emits the fixed C runtime helper `R_put_i32`, which uses
`printf("%" PRId32 "\\n", ...)` and returns canonical Bool `1` on a
nonnegative `printf` result and `0` otherwise. Other builtins, procedures,
calls, returns, parameters, and the previous unsupported type/control-flow
surfaces remain rejected with no partial C output.

The helper and its `<inttypes.h>`/`<stdio.h>` headers are emitted only when a
verified `putInteger` call occurs. They use fixed backend names and never
render source identifiers. The surrounding user-program lowering remains the
flat numeric-label register/memory form introduced in Stage 4B.

`tests/test_generated_c.py` is the only place that invokes an available host C
compiler: it emits the Gate4 source, compiles the generated C as strict C11 in
a temporary directory, runs that test executable, and requires `42\n`. The
compiler binary itself still writes C source only. Native executable creation
by the compiler awaits the approved Stage 4C platform adapter; no production
process APIs, `cc`, shell, `popen`, or POSIX dependency were added.
