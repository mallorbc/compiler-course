# Stage 6C: scalar String words and line runtime

Stage 6C adds scalar `String` to the existing typed IR, restricted-C backend,
and manual-frame runtime without changing the handwritten scanner or replacing
the word-addressed machine model.  The parser alone removes a valid literal's
outer quotes when it creates an IR constant; `Constant<string>` therefore
contains semantic bytes, including case and embedded newlines, but no source
delimiters.

A runtime String value is a nonnegative `MM` word index.  Each String byte is
stored as one unsigned value in one `int32_t` word and is followed by a zero
word.  Values, globals, locals, parameters, call spills, and returns contain
only this handle: generated user code has no C String pointers and assignment
copies the handle.  The backend rejects direct IR constants containing NUL and
oversized static pools atomically as Unsupported.

Any reachable scalar String selects the unified manual-frame emitter, avoiding
a second String implementation in the simple Program path.  Non-String
Programs retain their existing output.  Unified static storage is ordered as:
compact globals, Program call-result spills, one canonical empty terminator,
then exact-deduplicated nonempty literals in deterministic reachable order.
Literal bytes and terminators are initialized with numeric C constants, so
quotes, newlines, high bytes, and other source text cannot inject generated C.
String globals and procedure locals start with the canonical empty handle.

String assignment, exact by-value parameters, returns, nested calls, and
recursion copy handles.  Equality and inequality use a bounds-defensive content
comparison over MM words and return canonical Bool; handles are never compared
as pointers or identities.  Procedure operands are staged through scratch
registers before helpers, preserving the no-MM-helper-argument rule.

`putString` validates and writes each byte with `putchar`, appends a newline,
and returns Bool success.  `getString` implements line input: it preserves
spaces, excludes the newline, treats an empty line or immediate EOF as the
canonical empty String, and commits bytes when EOF follows a nonempty line.
An embedded NUL or capacity exhaustion drains the rest of that line, leaves
the persistent heap pointer unchanged, publishes no partial handle, and
returns empty.

Reachable `getString` reserves one dedicated register as a persistent heap
pointer.  Immutable input Strings allocate downward from `MM_WORDS`; frames
continue growing upward from the static pool.  Each procedure push checks the
current heap boundary as well as the active stack pointer, and a collision
uses the existing controlled nonzero exit.  Input allocations are never
reclaimed, so handles remain valid across later reads, returns, and recursion.

Reachability controls every String artifact.  Literal-only output has no
helper or header, equality needs only its content helper, and get/put each add
only their own helper plus `stdio.h`.  A dead String procedure contributes no
pool words, heap register, helper, frame, label, or link metadata.  String
ordering and arithmetic are excluded from typed IR: source attempts are
frontend errors, while malformed direct IR is InvalidIR.  Well-formed String
arrays were outside this scalar milestone; Stage 6D1 subsequently represents
them as spans of String handle words with checked indexing and whole copies.
Stage 6D2 subsequently lifts String `==` and `!=` content comparison over
equal-bound arrays and scalar broadcasts; ordering and arithmetic stay errors.
Noncanonical external calls remain Unsupported.  Production still emits C
source only and never invokes a host compiler.
