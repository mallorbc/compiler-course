# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Hand-written compiler front end for a custom Ada-like language (EECS 6083 course project), in C++ with no compiler-generator tools. This snapshot implements scanning, recursive-descent parsing, and type checking only — **there is no code generation** (the README's "compile to C or LLVM" is aspirational).

The assignment PDFs live in `docs/assignment/`: `projectLanguage.pdf` (the BNF grammar and semantics — the authoritative language definition) and `project.pdf` (project requirements, including the codegen expectations). `2023-snapshot/` is the closest recoverable version to the 2019 offering the code was written against; `2024-current/` has the same grammar with cleaner semantics wording. See `docs/assignment/PROVENANCE.md`.

## Commands

```sh
make                # builds ./compiler (g++ -g -Wall -Wextra -Werror=return-type, no optimization)
make clean          # removes *.o, compiler, tests/unit_tests
make unit           # builds + runs the doctest unit suite (tests/unit/)
make check          # golden-file regression run: all 177 test programs vs recorded baseline (~2s)
make test           # unit + check — run this before and after any change
./compiler <file.src>   # compile one source file; diagnostics go to stdout
python3 tests/run_golden.py --update   # re-record the golden baseline after an INTENDED behavior change (review the diff it prints)
./test_all.sh       # legacy manual eyeball-runner (sleeps + clears between tests); superseded by make check
```

The golden baseline (`tests/manifest.json` + `tests/golden/`) records **current** behavior, bugs included — a red `make check` means behavior *changed*, not that the compiler is wrong. Intended changes are re-recorded with `--update` and the diff reviewed in the commit. See `tests/README.md`.

To run a single test, build and invoke directly, e.g. `./compiler testPgms/correct/math.src`. Test programs live in `testPgms/correct/` (professor-provided), `testPgms/custom/`, `testPgms/fail/`, and ~160 audit regression probes under `docs/audit/probes/`. Since Batch 1 the exit code is truthful: 0 = no errors reported, 1 = errors reported, ≥128 = the compiler itself crashed. Note the `correct/` directory name is not authoritative — three of its programs legitimately report errors today (`logicals.src`, `test1.src`, `test1b.src`).

## Known broken state (updated 2026-08, post-Batch 1)

Batch 1 (commit `ccef008`) fixed the 15 minimal-tier defects: the three missing-`return` UB crashes (SIGILL), silent accepts of invalid operators, untruthful exit codes, and all `-Wreturn-type` warnings. The full verified defect inventory is `docs/audit/AUDIT.md`; the remaining fix queue is `docs/audit/FIX-TIERS.md`. Still broken:

- `docs/audit/probes/scanner/t_overflow.src` aborts (uncaught `std::stoi` out-of-range in scanner.cpp — CR-4), and five probe programs hang the compiler (CR-5/LX-3): `probes/resync/hang.src`, `probes/scanner/t_case.src`, `probes/scanner/t_number.src`, `probes/scopes/malformed_end_test.src`, `probes/scopes/minimal_hang_test.src`.
- `Typechecker::check_assignment_statement` is a stub that always returns true — final LHS-vs-RHS assignment type checking never runs. `check_loop_statement` is dead code; `parse_loop_statement` calls `check_if_statement` instead, so loop-condition errors get if-statement wording. Both are slated for the TY-2 type-propagation rebuild (High tier).
- Procedure calls are never validated against a declared signature (no lookup, no arity/type check). This is also how the built-in I/O procedures (`getInteger`, `putInteger`, etc.) used throughout testPgms "work" — they are not predeclared anywhere.
- Type propagation is fundamentally stream-shaped, not tree-shaped (parens lose types, call return types don't propagate, unary ops desync the checker, for-conditions unchecked) — the TY-2 rebuild addresses this class.

## Architecture

**Single-pass, parser-driven pipeline.** `main.cpp` just constructs `parser(filename)`; the parser constructor (parser.cpp) creates the `scanner`, creates the `Typechecker` (which holds a back-pointer to the parser), runs `parse_program()`, and prints accumulated errors. There are no separate scan/typecheck passes: the parser pulls tokens lazily from the scanner and calls the typechecker inline as it parses.

**Token flow.** The parser keeps one-token lookahead (`Current_parse_token` / `Next_parse_token`) refilled via `Get_Valid_Token()` (parser.cpp). At EOF the scanner returns a token of type `T_INVALID` — that is the sentinel checked throughout the parser's loops. Two-character operators (`<=`, `==`, …) are combined by the parser from single-character tokens, not by the scanner.

**One shared symbol table, and a dual-purpose token struct.** The single live `SymbolTable` is owned by the scanner (`scanner.h`) and mutated by the parser through `Lexer->symbol_table`. The `token` struct (token.h) is both the lexer's token type and the symbol table's entry type — it carries lexical fields plus `scope_id`, `identifer_type`, `identifier_data_type`, `procedure_params`, etc. The scanner itself inserts new identifiers into the table during lexing.

**Scoping.** `SymbolTable` maps `scope_id -> ScopeTable` (per-scope map of name → token). Scope 0 is the program scope; each `procedure` entered bumps `current_scope_id` via `parser::update_scopes(true)` and tears down via `update_scopes(false)` at `end procedure`. Two gotchas:
- `number_of_scopes` is decremented on scope exit, so **scope ids are reused** by later sibling procedures — they are not unique across the program.
- `global` declarations go into a separate pseudo-scope with id **-1** (not scope 0); lookups special-case it.

After mutating a token, the parser must call `SymbolTable::resync_tables` to write it back into the right `ScopeTable`; procedure tokens are additionally written one scope up so the name is visible both inside its body and in the declaring scope.

**Type checking is interleaved, streaming.** As the expression grammar descends (`parse_expression → parse_arithOp → parse_relation → parse_term → parse_factor`), the parser feeds every operand/operator token to `Typechecker::feed_in_tokens`, which accumulates until it has a full operation and then runs `is_valid_operation()` (the type-compatibility matrix). `set_statement_type` is called at the start of each statement to classify it and reset state. Note: the synthesized result type returned by `feed_in_tokens` is discarded at most call sites — the `resolved_token` that bubbles up to if/loop/return checks is the innermost factor's token, not the whole expression's type.

**Error handling.** All diagnostics funnel through `parser::generate_error_report` into an `error_reports` vector printed at the end; there is no warning/error distinction. Recovery is panic-mode: `resync_parser(parser_state)` (a large switch keyed by the `parser_state` enum in parser.h, mirroring grammar productions) skips to a synchronizing token and re-dispatches; `resync_status` suppresses duplicate reports meanwhile. The typechecker reports line numbers from `Lexer->current_line` (the scanner's lookahead position), so reported lines can trail the actual offending code.
