# Stage 6B: scalar Float words and runtime

Stage 6B extends the typed-IR-to-Restricted-C path with scalar `Float` while
retaining the handwritten frontend, `int32_t MM`/`Reg` storage, manual
activation frames, and one generated C `main`.  A Float occupies one raw IEEE
binary32 word.  Finite IR constants are emitted by their exact `uint32_t`
payload, including negative zero, subnormals, and the maximum finite value;
nonfinite direct IR constants are InvalidIR.  The host emitter refuses Float
modules unless `float` is four-byte IEC 60559 binary32, and emitted C contains
matching compile-time guards.  Runtime word/Float transport uses `memcpy`,
never a union, pointer pun, or numeric transport cast.

The parser's private scalar-binary promotion helper is used at all four binary
fold sites.  Exact operands are unchanged.  Mixed Integer/Float arithmetic and
relations cast only the Integer operand to Float; mixed Bool/Integer relations
cast only Bool to Integer.  Arrays, unresolved shapes, and other mixed pairs
remain unsupported, and procedure calls retain exact argument types.

Reachable Program and procedure code supports Float globals, locals,
parameters, returns, recursion, by-value calls, constants, loads/stores,
negation, arithmetic, all six IEEE comparisons, `IntToFloat`, `FloatToInt`,
`getFloat`, `putFloat`, and `sqrt(Integer) -> Float`.  Procedure runtime-helper
operands/results are staged in scratch registers, so Float support introduces
no MM arithmetic or direct MM helper arguments.  Dead Float, `sqrt`, and other
unsupported procedure declarations remain pruned with their labels, frames,
headers, helpers, and link requirements.

Exceptional behavior is fixed as follows:

- Float-to-Integer truncates finite in-range values toward zero, saturates
  positive/negative overflow and infinities to `INT32_MAX`/`INT32_MIN`, and
  maps NaN to zero.  Range and NaN guards precede the C cast.
- Float division by either signed zero follows IEEE and continues, producing
  signed infinity or NaN as applicable.  Integer division by zero retains its
  exit-status trap and `INT32_MIN / -1` policy.
- Negative `sqrt` returns the canonical quiet-NaN word and continues.  Float
  comparisons use C's IEEE comparison rules, including unordered NaN.
- `getFloat` consumes one complete whitespace token.  It accepts only a finite
  signed decimal with optional fraction and exponent.  Suffixes, hexadecimal,
  underscores, `nan`, `inf`, malformed input, EOF, and overflow yield positive
  zero; underflow may produce a subnormal or signed zero.
- `putFloat` writes lowercase `nan`, `inf`, or `-inf`; preserves `-0`; and
  otherwise prints `FLT_DECIMAL_DIG` significant digits plus a newline.  Like
  the other output builtins, its Bool result reports `printf` success.

Restricted-C results carry typed link metadata.  Exactly reachable `sqrt`
adds `RestrictedCLink::Math`; failures and dead calls expose no link metadata,
and successful atomic file publication preserves it.  The backend still only
emits C source and never invokes a host compiler.  Test tooling alone maps the
Math requirement to `-lm`.

Dependency-specific headers and encode/decode helpers are emitted only when a
reachable instruction needs them.  Singleton generated programs are compiled
with strict C11 `-Wall -Wextra -Werror -pedantic-errors` to keep that property
durable.

This was the Stage 6B boundary.  Stage 6C subsequently adds scalar String
handles, content equality, line input, output, and the persistent downward
String heap while preserving the binary32 and manual-frame contracts above.
