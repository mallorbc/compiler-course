#!/usr/bin/env python3
"""End-to-end tests for deterministic, atomic textual IR publication."""

from __future__ import annotations

import concurrent.futures
import os
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
COMPILER = REPO_ROOT / "compiler"


def run_compiler(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(COMPILER), *arguments],
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=5,
        check=False,
    )


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_no_debris(root: Path) -> None:
    check(not list(root.rglob("*.ir-tmp-*")), "IR temporary publication debris remains")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="compiler-ir-output-") as directory:
        root = Path(directory)
        source = root / "sample.src"
        source.write_text(
            "program sample is\n"
            "variable a : integer;\n"
            "variable b : integer;\n"
            "begin\n"
            "a := 1;\n"
            "b := 2;\n"
            "if (a == b) then\n"
            "    a := a + 1;\n"
            "end if;\n"
            "end program.\n",
            encoding="utf-8",
        )
        first_output = root / "sample.ir"
        first = run_compiler("--emit-ir", str(first_output), str(source))
        check(first.returncode == 0, f"IR emission failed: {first!r}")
        check(first.stderr == "", f"IR emission wrote stderr: {first.stderr!r}")
        check(first_output.is_file(), "IR output was not published")
        text = first_output.read_text(encoding="utf-8")
        required = (
            "module {",
            'storage @s0 global owner @f0 symbol(0, "a") : int',
            'define @f0 program symbol(0, "sample")',
            "%v4 : bool = equal %v2, %v3",
            "branch %v4, ^b1, ^b2",
            "%v7 : int = add %v5, %v6",
            "jump ^b3",
            "halt",
            'declare @f6 external symbol(0, "putinteger")',
        )
        for fragment in required:
            check(fragment in text, f"IR output lacks {fragment!r}:\n{text}")
        check("#include" not in text and "goto " not in text,
              "IR output leaked restricted-C syntax")

        second_output = root / "second.ir"
        second = run_compiler("--emit-ir", str(second_output), str(source))
        check(second.returncode == 0, f"repeat IR emission failed: {second!r}")
        check(second_output.read_bytes() == first_output.read_bytes(),
              "textual IR is not byte-deterministic")

        invalid = root / "invalid.src"
        invalid.write_text(
            "program invalid is\nbegin\nmissing := 1;\nend program.\n",
            encoding="utf-8",
        )
        unsupported = root / "unsupported.src"
        unsupported.write_text(
            "program unsupported is\n"
            "type color is enum{red,green};\n"
            "variable c : color;\n"
            "begin\nend program.\n",
            encoding="utf-8",
        )
        for rejected_source, expected in (
            (invalid, "ir: frontend-error"),
            (unsupported, "ir: unsupported"),
        ):
            sentinel = root / f"{rejected_source.stem}.ir"
            sentinel.write_text("preserve existing IR\n", encoding="utf-8")
            rejected = run_compiler("--emit-ir", str(sentinel), str(rejected_source))
            check(rejected.returncode == 1, f"{rejected_source.name} emitted IR")
            check(expected in rejected.stderr,
                  f"{rejected_source.name} lost status: {rejected.stderr!r}")
            check(sentinel.read_text(encoding="utf-8") == "preserve existing IR\n",
                  f"{rejected_source.name} replaced its sentinel")
            absent = root / f"absent-{rejected_source.stem}.ir"
            absent_result = run_compiler("--emit-ir", str(absent), str(rejected_source))
            check(absent_result.returncode == 1 and not absent.exists(),
                  f"{rejected_source.name} published an initially absent IR output")

        source_bytes = source.read_bytes()
        alias = run_compiler("--emit-ir", str(source), str(source))
        check(alias.returncode == 1 and "ir: io-error: output aliases source" in alias.stderr,
              f"source alias was not rejected: {alias!r}")
        check(source.read_bytes() == source_bytes, "source alias changed the source")

        hardlink = root / "hardlink.ir"
        os.link(source, hardlink)
        linked = run_compiler("--emit-ir", str(hardlink), str(source))
        check(linked.returncode == 1 and "ir: io-error: output aliases source" in linked.stderr,
              f"hardlink alias was not rejected: {linked!r}")
        check(source.read_bytes() == source_bytes, "hardlink alias changed the source")

        target = root / "target.ir"
        target.write_text("target sentinel\n", encoding="utf-8")
        symlink = root / "symlink.ir"
        symlink.symlink_to(target)
        linked_output = run_compiler("--emit-ir", str(symlink), str(source))
        check(linked_output.returncode == 1 and "ir: io-error" in linked_output.stderr,
              f"symlink output was not rejected: {linked_output!r}")
        check(target.read_text(encoding="utf-8") == "target sentinel\n",
              "symlink output changed its target")
        check(symlink.is_symlink(), "symlink output was replaced")

        dangling = root / "dangling.ir"
        dangling.symlink_to(root / "missing-target.ir")
        dangling_output = run_compiler("--emit-ir", str(dangling), str(source))
        check(dangling_output.returncode == 1 and "ir: io-error" in dangling_output.stderr,
              f"dangling output symlink was not rejected: {dangling_output!r}")
        check(dangling.is_symlink() and not (root / "missing-target.ir").exists(),
              "dangling output symlink was followed or replaced")

        planted_output = root / "planted.ir"
        planted_target = root / "outside-target.ir"
        planted_temp = root / ".planted.ir.ir-tmp-0"
        planted_temp.symlink_to(planted_target)
        planted = run_compiler("--emit-ir", str(planted_output), str(source))
        check(planted.returncode == 0, f"preplanted temp symlink blocked safe retry: {planted!r}")
        check(not planted_target.exists(), "preplanted temp symlink redirected IR output")
        check(planted_temp.is_symlink(), "IR publisher replaced a preplanted temp symlink")
        planted_temp.unlink()

        concurrent_output = root / "concurrent.ir"
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
            concurrent_results = list(executor.map(
                lambda _: run_compiler("--emit-ir", str(concurrent_output), str(source)),
                range(16),
            ))
        check(all(result.returncode == 0 for result in concurrent_results),
              f"concurrent IR writers failed: {concurrent_results!r}")
        check(concurrent_output.read_bytes() == first_output.read_bytes(),
              "concurrent IR publication produced a partial or different result")

        output_directory = root / "output-directory"
        output_directory.mkdir()
        directory_result = run_compiler("--emit-ir", str(output_directory), str(source))
        check(directory_result.returncode == 1 and "ir: io-error" in directory_result.stderr,
              f"directory output was not rejected: {directory_result!r}")
        check_no_debris(root)

    print("IR output test pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
