# Stage 6D1: array values, checked elements, and aggregate copies

Stage 6D1 carries the Stage 2E inclusive array shape through typed IR and the
restricted-C word machine.  It is deliberately the representation/index/copy
slice, not the later lifted-operator slice: whole arrays can be stored, loaded,
converted, and passed exactly by value, while array unary, binary, and
broadcast expressions remain atomically Unsupported.

The canonical resolved-value-shape predicate accepts either a scalar with the
sentinel bound `-1` or an array with an inclusive nonnegative upper bound.
Storage, user-procedure parameters, Values, whole loads/stores, shape-preserving
casts, and exact user calls use that predicate.  Constants, conditions,
procedure/call results and returns, and canonical external builtins remain
scalar.  Malformed aggregate IR is InvalidIR rather than an invitation for the
backend to guess a representation.

An indexed access is three explicit IR operations.  `CheckIndex` consumes a
visible array Storage and a scalar Integer raw index and produces a checked
scalar Integer.  That checked value is a linear, same-basic-block capability:
it must be consumed exactly once by an `ElementLoad` or `ElementStore` for the
same Storage after its definition.  General, duplicate, raw, cross-block, or
cross-storage use fails verification.  The parser emits a destination check
immediately after its index expression, before `:=` and the RHS; an indexed
read emits only its check and element load, while an unqualified name emits a
whole-array snapshot load.

Each element occupies one `int32_t` MM word.  String array elements are String
handle words, not bytes or C pointers.  Width is `upper_bound + 1`, computed in
a wide host type and checked against the fixed MM capacity.  Unified static
layout is expanded globals in StorageId order, scalar Program procedure-call
spills, the unchanged canonical-empty/String literal pool, Program aggregate
ValueId temporary spans, then the upward-growing stack.  Frames expand each
parameter, local, and Value span after the four ABI words.  Numeric, Bool, and
Float storage begins at zero; every String array element begins at the
canonical empty handle.

Whole loads are snapshots.  Stores, self-copies, elementwise casts, aggregate
initialization, and parameter copies use deterministic numeric-label loops in
generated `main`; source size is independent of the array bound.  Every MM
operand and result is staged through fixed scratch registers, so generated
user flow contains no C `for`/`while`, MM-to-MM copy, helper MM argument, or
source-derived name.  Array arguments are copied into a fully capacity-checked
callee frame before FP/SP publication, preserving exact by-value behavior
through nested calls and recursion.

Bounds checks execute in instruction order on every access.  A negative raw
index is rejected before unsigned address formation, and an index above the
inclusive bound follows the existing controlled exit-status-1 path without an
MM access or diagnostic.  This ordering also guarantees that an invalid
destination prevents RHS calls and output.

Any reachable array selects the unified emitter.  Dead procedures contribute
no array frame or temporary footprint, so an unreachable huge local is
pruned; a reachable global, Program temporary, or frame that exceeds capacity
returns atomic Unsupported.  String-heap/frame collision checks use the full
expanded frame width.  Nonarray generated C remains byte-identical.

`iterativeFib.src` now emits and executes because it needs only checked element
access.  `recursiveFib.src` also lowers, but its existing `Sub` procedure
subtracts from an unassigned zero-initialized local, makes the recursive input
negative, and consequently reaches the ordinary frame-capacity exit; that is
source behavior, not a missing array operation.  Lifted array unary/binary and
broadcast lowering remains the explicit Stage 6D2 boundary.
