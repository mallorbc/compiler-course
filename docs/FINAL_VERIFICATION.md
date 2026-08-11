# Final verification record

This record is the reproducible evidence companion to the one-page
[completion report](FINAL_REPORT.md). Commands were run on standard Linux
x86-64 with GCC/G++ 13.3.0, GNU Make 4.3, Python 3.12.4, and glibc 2.39.
Clang/Clang++ were not installed, so no Clang result is claimed.

## Current committed-tree gates

The final tracked-only archive reproduction is recorded here after the final
documentation commit. Before that export, the exact working tree passed:

```sh
make clean && make test
```

- warning-free default build;
- 147/147 doctest cases and 3,405/3,405 assertions;
- CLI pass;
- 210/210 golden programs: 98 OK, 112 ERRORS, 0 CRASH, 0 TIMEOUT;
- strict generated-C object/runtime pass;
- native driver, publication, professor-source, and runtime pass.

The optimized configuration reproduced the same complete result and identical
golden output:

```sh
make clean
make CXXFLAGS='-std=c++17 -O2 -Wall -Wextra -Werror=return-type -MMD -MP' test
```

The warning-strict GCC configuration also passed every layer:

```sh
make clean
make CXX=g++ \
  CXXFLAGS='-std=c++17 -g -Wall -Wextra -Werror -MMD -MP' test
```

## Sanitizers

Compiler and unit/process coverage passed ASan+UBSan:

```sh
make clean
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
make CXXFLAGS='-std=c++17 -g -O1 -Wall -Wextra -Werror=return-type \
  -MMD -MP -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Generated and native C were independently compiled through a temporary `cc`
wrapper whose body was:

```sh
#!/bin/sh
exec /usr/bin/gcc -fsanitize=address,undefined \
  -fno-omit-frame-pointer "$@"
```

With the wrapper first on `PATH`, the following complete runtime suites passed:

```sh
PATH=/tmp/compiler-sanitized-cc:/usr/bin:/bin \
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 tests/test_generated_c.py

PATH=/tmp/compiler-sanitized-cc:/usr/bin:/bin \
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 tests/test_native.py
```

`parser` now owns its scanner, typechecker, and IR builder through private
`unique_ptr`s, while `main` gives the parser automatic lifetime. ASan+UBSan
with `detect_leaks=0` is clean. LeakSanitizer itself was unavailable: after
tests completed it aborted with `LeakSanitizer does not work under ptrace` and
reported no actionable leak stack. This record therefore does not claim an
LSan pass.

## Manifest and publication evidence

After the documentation-only note correction, `tests/manifest.json` has
SHA-256 `9d916be96e1607a04290cae4c7ff0a099a002fbdfbc70b3238fe9285d3b14e1e`.
There are exactly 210 discovered sources, 210 manifest entries, and 210 golden
outputs; every recorded byte count and output hash verifies.

Invalid syntax, semantic failures, unsupported enum/aggregate contexts,
compile-time capacity/preflight failures, trailing input, host compiler
failures/signals, malformed products, and alias/symlink/FIFO paths are tested
with both existing sentinels and absent destinations. They do not publish or
replace an artifact and leave no native/restricted-C temporary debris. Runtime
bounds, integer division by zero, frame, and String-heap capacity traps
intentionally publish a valid executable which later exits with status 1.

## Final tracked-only export

Pending the final documentation commit; all ownership and sanitizer results are
recorded above.
