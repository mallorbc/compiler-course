# Stage 5B: Program `for` CFG lowering

Stage 5B lowers the existing handwritten `for` production into the
project-owned, backend-neutral Program CFG. The source grammar remains
`for (assignment_statement ; expression) (statement ;)* end for`: the
initializer runs once, the condition runs before every iteration (including
the final false check), and there is no third header/update clause. A source
update such as `i := i + 1` remains an ordinary body statement.

After the initializer has been parsed in the current preheader block, the
parser allocates condition, body, and exit blocks in that order. It jumps to
and selects the condition block before parsing the condition expression, so
loads and calls are emitted in a block executed again on every backedge. A
valid Bool condition branches to body/exit; an Integer condition receives the
existing explicit `IntToBool` conversion. A fully owned `end for` closes the
current body tail with a jump to condition and leaves exit selected for the
following source statement.

The generic IR verifier already accepts cyclic Program CFGs that have a path
from every reachable block to the single Halt, and its dominance rules reject
body-only values leaking into condition or exit blocks. No new loop node,
phi value, or builder API is needed: the language's mutable variables are
represented by the existing Load/Store operations.

Restricted C now accepts verifier-valid cycles. It emits the same flat numeric
labels and backward `goto` edges used by Program branches; it does not emit C
`for`, `while`, or `else`. Existing all-block preflight, `putInteger` runtime,
division exit behavior, atomic output publication, and source-name isolation
remain unchanged.

Deferred: Procedure CFG/return-path lowering, arrays/bounds checks, float and
string backend values, break/continue, phi values, and compiler-owned native
executable creation.
