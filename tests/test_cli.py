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

    print("PASS: CLI and numeric failure paths terminate promptly with exit 1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
