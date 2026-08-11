# Stage 4B: deterministic restricted-C emission

Stage 4B adds a backend-neutral proof-of-path from finalized Stage 4A IR to a
small, inspectable C11 source string. `RestrictedCEmitter` consumes only
`IR.h`/`SemanticTypes.h`; it has no dependency on the scanner, parser, tokens,
symbol tables, or type checker. This keeps the frontend suitable for a later
LLVM backend rather than making C concepts leak into syntax or semantic code.

The explicit CLI form is:

```sh
./compiler --emit-c OUTPUT SOURCE
```

The legacy `./compiler SOURCE` check-only form has unchanged output and never
writes an output file. C emission first requires a finalized, verifier-valid
module, then preflights the entire module before rendering. It accepts exactly
one `Program`/`Halt` block, scalar Integer/Bool globals and values, constants,
global loads/stores, exact Integer/Bool unary and binary operations, and
Integer↔Bool casts. Unused builtin declarations are tolerated, but calls,
defined procedures/returns/locals, control flow, floats, strings, arrays, and
unknown instructions produce an atomic `Unsupported` result with no C text.
Verifier-invalid modules similarly produce no text.

The generated C has only a fixed project-owned machine model: a 64 MiB
`int32_t` memory (the emitter's one `memory_word_capacity()` invariant, rendered
as the equivalent `#define MM_BYTES (67108864u)`), a
deterministic register array (`ValueId n` is `Reg[n+2]`), and numeric labels.
Global `StorageId`s map directly to ascending `MM` words; source names are
never rendered. C source is rendered fully in memory before any output path is
touched. File output rejects direct symlinks (including dangling ones), then
atomically reserves an owner-only sibling temporary **directory** with
`create_directory`, writes its fixed regular child, and renames that child to
the output. This avoids probing/opening a predictable temporary file and never
follows a preplanted temporary symlink. Existing outputs are left alone on all
handled preflight/path/write/rename failures and temporary files/directories are
cleaned then. The C++17 standard library still cannot promise exclusive-file
creation against a hostile same-owner process or universal replace-existing
atomic-rename semantics, so publication remains best-effort portable.

Integer add/subtract/multiply/negate and integer bitwise operations route
through `uint32_t` intermediates and a defined two's-complement reconstruction
macro, avoiding signed-overflow UB and out-of-range unsigned-to-signed casts.
Integer division uses flat numeric labels and reserved scratch registers—never
an inline `else` body or early return. A zero divisor sets the reserved exit
register and transfers to the common numeric exit label; `INT32_MIN / -1`
explicitly becomes `INT32_MIN`. These runtime trap/wrap choices are temporary
restricted-C semantics, not a language-runtime specification. Negative C
literals are emitted portably as `(-INT32_C(magnitude))`, except `INT32_MIN`.

Deferred to Stage 4C and later: invoking a host compiler and producing an
executable, ABI/runtime implementations of the nine builtins, procedure and
call lowering, control-flow blocks, float/string/array support and bounds
checks, portable transactional output guarantees, and a full runtime overflow
and division policy. `tests/test_generated_c.py` may use an installed
`cc`/`gcc`/`clang` only to syntax-check generated C; no production code invokes
a host compiler.
