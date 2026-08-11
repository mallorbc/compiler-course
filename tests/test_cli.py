#!/usr/bin/env python3
"""Process-level regression tests for compiler failure paths."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
COMPILER = REPO_ROOT / "compiler"
TIMEOUT_SEC = 2.0


def run_compiler(*arguments: str) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            [str(COMPILER), *arguments],
            cwd=REPO_ROOT,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=TIMEOUT_SEC,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(
            f"compiler did not terminate within {TIMEOUT_SEC:g}s: {arguments!r}"
        ) from error


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    no_args = run_compiler()
    check(no_args.returncode == 1, f"no-arg exit was {no_args.returncode}, expected 1")
    check(
        no_args.stdout == f"Error!\nUsage: {COMPILER} <file to compile>\n",
        f"unexpected no-arg stdout: {no_args.stdout!r}",
    )
    check(no_args.stderr == "", f"unexpected no-arg stderr: {no_args.stderr!r}")

    incomplete_emit = run_compiler("--emit-c", "output.c")
    check(
        incomplete_emit.returncode == 1,
        f"incomplete emit exit was {incomplete_emit.returncode}, expected 1",
    )
    check(
        incomplete_emit.stdout == f"Error!\nUsage: {COMPILER} <file to compile>\n",
        f"unexpected incomplete-emit stdout: {incomplete_emit.stdout!r}",
    )
    check(incomplete_emit.stderr == "", f"unexpected incomplete-emit stderr: {incomplete_emit.stderr!r}")

    extra_args = run_compiler("one.src", "two.src")
    check(
        extra_args.returncode == 1,
        f"extra-args exit was {extra_args.returncode}, expected 1",
    )
    check(
        extra_args.stdout == f"Error!\nUsage: {COMPILER} <file to compile>\n",
        f"unexpected extra-args stdout: {extra_args.stdout!r}",
    )
    check(
        extra_args.stderr == "",
        f"unexpected extra-args stderr: {extra_args.stderr!r}",
    )

    with tempfile.TemporaryDirectory(prefix="compiler_cli_") as temp_dir:
        missing = Path(temp_dir) / "does-not-exist.src"
        bad_path = run_compiler(str(missing))
        check(bad_path.returncode == 1, f"bad-path exit was {bad_path.returncode}, expected 1")
        check(
            bad_path.stdout == f"Error!\nUnable to open source file: {missing}\n",
            f"unexpected bad-path stdout: {bad_path.stdout!r}",
        )
        check(bad_path.stderr == "", f"unexpected bad-path stderr: {bad_path.stderr!r}")

        directory = Path(temp_dir) / "source-directory"
        directory.mkdir()
        directory_input = run_compiler(str(directory))
        check(
            directory_input.returncode == 1,
            f"directory exit was {directory_input.returncode}, expected 1",
        )
        check(
            directory_input.stdout
            == f"Error!\nUnable to open source file: {directory}\n",
            f"unexpected directory stdout: {directory_input.stdout!r}",
        )
        check(
            directory_input.stderr == "",
            f"unexpected directory stderr: {directory_input.stderr!r}",
        )

        numeric_cases = (
            (
                "integer-overflow.src",
                "integer",
                "99999999999999999999999999999999999999999",
                "Error on line 4: Numeric literal is out of range",
            ),
            (
                "float-overflow.src",
                "float",
                "99999999999999999999999999999999999999999.0",
                "Error on line 4: Numeric literal is out of range",
            ),
            (
                "multi-decimal.src",
                "float",
                "1.2.3",
                "Error on line 4: Malformed numeric literal: multiple decimal points",
            ),
        )
        for file_name, type_name, literal, expected_diagnostic in numeric_cases:
            source = Path(temp_dir) / file_name
            source.write_text(
                "program numeric_failure is\n"
                f"    variable value : {type_name};\n"
                "begin\n"
                f"    value := {literal};\n"
                "end program.\n",
                encoding="utf-8",
            )
            numeric_failure = run_compiler(str(source))
            check(
                numeric_failure.returncode == 1,
                f"{file_name} exit was {numeric_failure.returncode}, expected 1",
            )
            check(
                numeric_failure.stdout.count(expected_diagnostic) == 1,
                f"{file_name} expected diagnostic exactly once: "
                f"{numeric_failure.stdout!r}",
            )
            check(
                numeric_failure.stderr == "",
                f"unexpected {file_name} stderr: {numeric_failure.stderr!r}",
            )

        separated_numbers = Path(temp_dir) / "separated-numbers.src"
        separated_numbers.write_text(
            "program separated_numbers is\n"
            "    variable integer_value : integer;\n"
            "    variable float_value : float;\n"
            "begin\n"
            "    integer_value := 1_000;\n"
            "    float_value := 3.1_4;\n"
            "end program.\n",
            encoding="utf-8",
        )
        separated_numbers_result = run_compiler(str(separated_numbers))
        check(
            separated_numbers_result.returncode == 0,
            "underscore-separated numeric literals should compile cleanly: "
            f"{separated_numbers_result.stdout!r}",
        )
        check(
            separated_numbers_result.stdout
            == "The program parsed successfully with no errors\n",
            f"unexpected separated-numbers stdout: {separated_numbers_result.stdout!r}",
        )
        check(
            separated_numbers_result.stderr == "",
            f"unexpected separated-numbers stderr: {separated_numbers_result.stderr!r}",
        )

        missing_begin = Path(temp_dir) / "missing-begin.src"
        missing_begin.write_text(
            "program missing_begin is\n"
            "    variable value : integer;\n"
            "end program.\n",
            encoding="utf-8",
        )
        missing_begin_result = run_compiler(str(missing_begin))
        check(
            missing_begin_result.returncode == 1,
            f"missing-begin exit was {missing_begin_result.returncode}, expected 1",
        )
        check(
            'Missing keyword "begin" to begin program statements'
            in missing_begin_result.stdout,
            f"missing-begin diagnostic was lost: {missing_begin_result.stdout!r}",
        )
        check(
            'Missing "." to end the program' not in missing_begin_result.stdout,
            f"missing-begin recovery consumed the end boundary: "
            f"{missing_begin_result.stdout!r}",
        )
        check(
            missing_begin_result.stderr == "",
            f"unexpected missing-begin stderr: {missing_begin_result.stderr!r}",
        )

        malformed_expression_cases = (
            (
                "leading-multiply.src",
                "* 2",
                'Missing left operand before "*" operator',
            ),
            (
                "leading-divide.src",
                "/ 2",
                'Missing left operand before "/" operator',
            ),
            (
                "double-multiply.src",
                "* * 2",
                'Missing left operand before "*" operator',
            ),
            (
                "double-multiply-after-factor.src",
                "1 * * 2",
                "Invalid token for factor discovered",
            ),
            (
                "trailing-multiply.src",
                "1 *",
                "Invalid token for factor discovered",
            ),
            (
                "trailing-divide.src",
                "1 /",
                "Invalid token for factor discovered",
            ),
            (
                "leading-less.src",
                "< 2",
                'Missing left operand before "<" operator',
            ),
            (
                "leading-less-equal.src",
                "<= 2",
                'Missing left operand before "<" operator',
            ),
            (
                "leading-greater.src",
                "> 2",
                'Missing left operand before ">" operator',
            ),
            (
                "leading-greater-equal.src",
                ">= 2",
                'Missing left operand before ">" operator',
            ),
            (
                "leading-equal.src",
                "== 2",
                'Missing left operand before "==" operator',
            ),
            (
                "leading-not-equal.src",
                "!= 2",
                'Missing left operand before "!=" operator',
            ),
        )
        for file_name, expression, expected_diagnostic in malformed_expression_cases:
            source = Path(temp_dir) / file_name
            source.write_text(
                "program malformed_expression is\n"
                "    variable value : integer;\n"
                "begin\n"
                f"    value := {expression};\n"
                "end program.\n",
                encoding="utf-8",
            )
            malformed_expression = run_compiler(str(source))
            check(
                malformed_expression.returncode == 1,
                f"{file_name} exit was {malformed_expression.returncode}, expected 1",
            )
            check(
                malformed_expression.stdout.count(expected_diagnostic) == 1,
                f"{file_name} expected one focused diagnostic: "
                f"{malformed_expression.stdout!r}",
            )
            check(
                'Type ""' not in malformed_expression.stdout,
                f"{file_name} polluted the type-checker state: "
                f"{malformed_expression.stdout!r}",
            )
            check(
                malformed_expression.stdout.count("Error on line") <= 3,
                f"{file_name} produced an error cascade: "
                f"{malformed_expression.stdout!r}",
            )
            check(
                malformed_expression.stderr == "",
                f"unexpected {file_name} stderr: {malformed_expression.stderr!r}",
            )

        # A malformed loop header must retain ownership of its `end for` so
        # that parsing resumes at the statement following the loop.  These
        # process-level cases complement the parser assertions with exit and
        # stdout progress checks.
        loop_boundary_cases = (
            (
                "loop-missing-colon.src",
                "    for (i = 0; true)\n"
                "        i := 1;\n"
                "    end for;\n"
                "    i := \"later\";\n",
                'Missing ":" needed for assignment statement',
                1,
                2,
            ),
            (
                "loop-missing-internal-semicolon.src",
                "    for (i := 0 true)\n"
                "        i := 1;\n"
                "    end for;\n"
                "    i := \"later\";\n",
                'Missing ";" for loop assignment statement',
                1,
                2,
            ),
            (
                "loop-missing-initializer-identifier.src",
                "    for (; true)\n"
                "        i := 1;\n"
                "    end for;\n"
                "    i := \"later\";\n",
                "Missing expeceted identifier for assignment statement",
                1,
                2,
            ),
            (
                "loop-missing-open.src",
                "    for i := 0; true)\n"
                "        i := 1;\n"
                "    end for;\n"
                "    i := \"after\";\n",
                'Missing "(" required for loop',
                1,
                2,
            ),
            (
                "nested-loop-missing-close.src",
                "    for (i := 0; true)\n"
                "        for (i := 0; true\n"
                "            i := 1;\n"
                "        end for;\n"
                "        i := \"outer\";\n"
                "    end for;\n"
                "    i := \"later\";\n",
                'Missing ")" for loop declaration',
                2,
                3,
            ),
            (
                "nested-loop-depth-missing-close.src",
                "    for (i := 0; true\n"
                "        for (i := 0; true)\n"
                "            i := 1;\n"
                "        end for;\n"
                "    end for;\n"
                "    i := \"later\";\n",
                'Missing ")" for loop declaration',
                1,
                2,
            ),
            (
                "nested-if-loop-missing-close.src",
                "    for (i := 0; true)\n"
                "        for (i := 0; true\n"
                "            if (true) then\n"
                "                i := 1;\n"
                "            end if;\n"
                "        end for;\n"
                "    end for;\n"
                "    i := \"later\";\n",
                'Missing ")" for loop declaration',
                1,
                2,
            ),
            (
                "enclosing-if-loop-missing-close.src",
                "    if (true) then\n"
                "        for (i := 0; true\n"
                "            i := 1;\n"
                "    end if;\n"
                "    i := \"later\";\n",
                'Missing ")" for loop declaration',
                1,
                2,
            ),
        )
        assignment_error = (
            'Assignment target type "integer" is not compatible with expression type "string"'
        )
        for (file_name, loop_source, header_error, assignment_error_count,
             total_error_count) in loop_boundary_cases:
            source = Path(temp_dir) / file_name
            source.write_text(
                "program loop_boundary is\n"
                "variable i : integer;\n"
                "begin\n" + loop_source + "end program.\n",
                encoding="utf-8",
            )
            recovery = run_compiler(str(source))
            check(recovery.returncode == 1,
                  f"{file_name} exit was {recovery.returncode}, expected 1")
            check(recovery.stdout.count(header_error) == 1,
                  f"{file_name} lost its focused header error: {recovery.stdout!r}")
            check(recovery.stdout.count(assignment_error) == assignment_error_count,
                  f"{file_name} did not resume after its loop boundary: {recovery.stdout!r}")
            check(recovery.stdout.count("Error on line") == total_error_count,
                  f"{file_name} produced a recovery cascade: {recovery.stdout!r}")
            check('Missing ";" to end statement in loop statement' not in recovery.stdout,
                  f"{file_name} lost loop ownership: {recovery.stdout!r}")
            check('Missing keyworkd "program" to end program' not in recovery.stdout,
                  f"{file_name} lost the program boundary: {recovery.stdout!r}")
            check('Missing keyword "end" to end if statement' not in recovery.stdout,
                  f"{file_name} lost an enclosing if boundary: {recovery.stdout!r}")
            check(recovery.stderr == "",
                  f"unexpected {file_name} stderr: {recovery.stderr!r}")

    recovery_cases = (
        (
            "docs/audit/probes/resync/hang.src",
            'Missing keyword "end" to close procedure body',
        ),
        (
            "docs/audit/probes/scanner/t_case.src",
            'Expected keyword "Program" not found',
        ),
        (
            "docs/audit/probes/scopes/malformed_end_test.src",
            'Missing keyword "end" to close procedure body',
        ),
        (
            "docs/audit/probes/scopes/minimal_hang_test.src",
            'Missing keyword "end" to close procedure body',
        ),
        (
            "docs/audit/probes/scopes/strayend.src",
            'Missing keyworkd "program" to end program',
        ),
    )
    for relative_path, expected_diagnostic in recovery_cases:
        recovery = run_compiler(str(REPO_ROOT / relative_path))
        check(
            recovery.returncode == 1,
            f"{relative_path} exit was {recovery.returncode}, expected 1",
        )
        check(
            expected_diagnostic in recovery.stdout,
            f"{relative_path} lost its recovery diagnostic: {recovery.stdout!r}",
        )
        check(
            recovery.stderr == "",
            f"unexpected {relative_path} stderr: {recovery.stderr!r}",
        )

    print("PASS: CLI, numeric, and parser recovery paths terminate with the expected result")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
