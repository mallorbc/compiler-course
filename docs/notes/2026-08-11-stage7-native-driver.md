# Stage 7: safe native driver

Stage 7 closes GitHub Issue #1's standard-Linux toolchain step without moving
host-process policy into the handwritten parser or restricted-C emitter. The
legacy `./compiler SOURCE` check-only path and
`./compiler --emit-c OUTPUT SOURCE` path are unchanged. Native publication is
selected by `-o`, `--output`, or `--native`:

```text
./compiler -o OUTPUT SOURCE
```

After successful frontend and finalized-IR checks, the driver asks
`RestrictedCEmitter::emit()` for an in-memory `RestrictedCResult`. No C file is
published. `NativeToolchain` then validates the destination, atomically reserves
a sibling directory with `mkdtemp`, restricts it to mode 0700, and writes the C
text to a new mode-0600 file inside it. Both the C input and compiler output
arguments are absolute paths in that private directory; the language source and
final executable path are never passed to the host compiler.

The host compiler defaults to `cc`. A nonempty `CC` value replaces that one
executable name or path literally. It is not tokenized, expanded, or sent to a
shell; `CFLAGS` and `LDFLAGS` are deliberately ignored. The child is created by
`posix_spawnp`, reads stdin from `/dev/null`, and inherits stdout/stderr so real
compiler diagnostics remain visible. Its argv is exactly `CC`, `-std=c11`, the
absolute private C path, `-o`, the absolute private executable path, followed by
one `-lm` only when the emitter's reachable dependency set contains
`RestrictedCLink::Math`. Dead `sqrt` code therefore adds no link option.

`waitpid` retries interruption. Launch errors, signals, nonzero exits, and a
zero exit without a nonempty, direct, link-count-one regular file carrying an
execute bit and executable by the current effective identity (`X_OK`) all fail
the transaction. Immediately before publication, the
driver rechecks that the final path is missing or a direct regular file and is
distinct from the source and resolved host compiler. It then uses one `rename`
to replace/publish the executable atomically. Every earlier failure preserves an
existing output, and the private directory is removed on every return path.
Bounds, integer division by zero, frame, or heap failures are runtime behavior
of a successfully published executable; they do not retroactively make
compilation fail.

This seam intentionally targets standard Linux/POSIX facilities:
`posix_spawnp`, `waitpid`, `mkdtemp`, `lstat`, executable permission bits,
`/dev/null`, and atomic same-filesystem `rename`. It is not a Windows adapter,
and other platforms may spell the math-library option differently. POSIX path
APIs cannot make a policy check and later `rename` indivisible when an unrelated
same-privilege process is concurrently replacing parent path components. The
driver is fail-closed on every inspection it performs and never follows a
direct final-output symlink, but defending that parent-component race would
require a Linux-specific dirfd/`renameat2` policy beyond this portability slice.

Durable tests use a fake compiler to pin literal argv, typed/dead Math linking,
missing and signaled compilers, all malformed product kinds, non-shell hostile
`CC`, source/compiler aliases, symlinks, sentinel preservation, and cleanup.
Real-`cc` tests run Integer/control/recursive procedures, all four runtime value
types and canonical I/O, String and numeric arrays, checked-bound failure,
`sqrt`, hostile Unicode/newline/metacharacter paths, deterministic replacement,
all eleven professor `correct/` fixtures with their pinned success/rejection
outcomes, and 32 concurrent writers. The check-only corpus remains 210 cases
(98 OK, 112 ERRORS), and generated-C strict builds remain a separate
independent oracle.
