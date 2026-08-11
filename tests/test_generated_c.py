#!/usr/bin/env python3
"""Stage 4B process checks for restricted-C emission (stdlib only)."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
COMPILER = REPO_ROOT / "compiler"
TIMEOUT_SEC = 3.0


def run_compiler(*arguments: str) -> subprocess.CompletedProcess[str]:
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


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_no_temp_debris(directory: Path) -> None:
    debris = list(directory.rglob("*restricted-c-tmp-*"))
    check(not debris, f"restricted-C temporary debris remains: {debris!r}")


def strict_c11_syntax_check(generated: Path) -> None:
    c_compiler = next(
        (candidate for candidate in ("cc", "gcc", "clang") if shutil.which(candidate)), None
    )
    if c_compiler is None:
        print("SKIP test_generated_c.py: no C11 compiler available for syntax-only check")
        return
    result = subprocess.run(
        [
            c_compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic-errors",
            "-fsyntax-only",
            str(generated),
        ],
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=TIMEOUT_SEC,
        check=False,
    )
    check(
        result.returncode == 0,
        "strict C11 syntax check failed:\n"
        f"stdout={result.stdout!r}\nstderr={result.stderr!r}\nsource={generated.read_text()!r}",
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="compiler_stage4b_") as temporary:
        root = Path(temporary)
        source = root / "straight-line.src"
        output = root / "straight-line.c"
        source.write_text(
            "program backend_source is\n"
            "variable left_value : integer;\n"
            "variable right_value : integer;\n"
            "variable truth_value : bool;\n"
            "begin\n"
            "    left_value := -5 + 12;\n"
            "    right_value := left_value * 3 / 3;\n"
            "    truth_value := right_value > 0;\n"
            "    truth_value := not truth_value;\n"
            "    left_value := truth_value;\n"
            "    truth_value := left_value;\n"
            "end program.\n",
            encoding="utf-8",
        )

        first = run_compiler("--emit-c", str(output), str(source))
        check(first.returncode == 0, f"restricted-C emit failed: {first.stderr!r}")
        check(first.stderr == "", f"unexpected emit stderr: {first.stderr!r}")
        generated = output.read_text(encoding="utf-8")
        check("#include <stdint.h>" in generated, "missing fixed C runtime include")
        check("#define MM_BYTES (67108864u)" in generated, "missing MM model")
        check("int32_t MM[MM_BYTES / sizeof(int32_t)];" in generated, "missing MM array")
        check("int32_t Reg[REGISTER_COUNT];" in generated, "missing register array")
        check("goto L_f0_b0;" in generated and "L_f0_b0:" in generated, "missing numeric entry")
        check("MM[0u]" in generated and "MM[1u]" in generated, "global layout is not ID ordered")
        for source_identifier in ("backend_source", "left_value", "right_value", "truth_value"):
            check(source_identifier not in generated, f"source name leaked into C: {source_identifier}")
        check("==" in generated and "=" in generated, "equality/load/store structure is missing")
        check("(uint32_t)" in generated, "integer arithmetic is not widened through uint32_t")
        check("if (Reg[" in generated and "goto L_f0_b2;" in generated, "division is not flat-lowered")
        check("else" not in generated and "return 1;" not in generated, "division has inline control flow")
        check_no_temp_debris(root)

        second = run_compiler("--emit-c", str(output), str(source))
        check(second.returncode == 0, f"second emit failed: {second.stderr!r}")
        check(generated == output.read_text(encoding="utf-8"), "restricted-C output is not deterministic")
        strict_c11_syntax_check(output)

        hostile_output = root / "hostile ; $ [name].c"
        hostile = run_compiler("--emit-c", str(hostile_output), str(source))
        check(hostile.returncode == 0, f"hostile output path failed: {hostile.stderr!r}")
        check(hostile_output.read_text(encoding="utf-8") == generated, "hostile path changed output")

        frontend_error = root / "frontend-error.src"
        frontend_error.write_text(
            "program frontend_error is\n"
            "variable value : integer;\n"
            "begin\n"
            '    value := "wrong";\n'
            "end program.\n",
            encoding="utf-8",
        )
        sentinel = root / "sentinel.c"
        sentinel.write_text("do not replace\n", encoding="utf-8")
        frontend = run_compiler("--emit-c", str(sentinel), str(frontend_error))
        check(frontend.returncode != 0, "frontend error unexpectedly emitted C")
        check("codegen: frontend-error" in frontend.stderr, f"missing frontend codegen error: {frontend.stderr!r}")
        check(sentinel.read_text(encoding="utf-8") == "do not replace\n", "frontend error replaced sentinel")

        unsupported_source = root / "unsupported.src"
        unsupported_source.write_text(
            "program unsupported_source is\n"
            "begin\n"
            "    if (true) then\n"
            "    end if;\n"
            "end program.\n",
            encoding="utf-8",
        )
        unsupported = run_compiler("--emit-c", str(sentinel), str(unsupported_source))
        check(unsupported.returncode != 0, "unsupported control flow unexpectedly emitted C")
        check("codegen: unsupported" in unsupported.stderr, f"missing unsupported error: {unsupported.stderr!r}")
        check(sentinel.read_text(encoding="utf-8") == "do not replace\n", "unsupported input replaced sentinel")

        output_directory = root / "output-directory"
        output_directory.mkdir()
        directory_result = run_compiler("--emit-c", str(output_directory), str(source))
        check(directory_result.returncode != 0, "directory output unexpectedly succeeded")
        check("codegen: io-error" in directory_result.stderr, "directory output lacked I/O diagnostic")

        missing_parent = root / "missing-parent" / "output.c"
        missing_parent_result = run_compiler("--emit-c", str(missing_parent), str(source))
        check(missing_parent_result.returncode != 0, "missing parent output unexpectedly succeeded")
        check("codegen: io-error" in missing_parent_result.stderr, "missing parent lacked I/O diagnostic")
        check(not missing_parent.exists(), "missing-parent request created an output")

        source_before_alias = source.read_text(encoding="utf-8")
        alias = run_compiler("--emit-c", str(source), str(source))
        check(alias.returncode != 0, "source/output alias unexpectedly succeeded")
        check("codegen: io-error: output aliases source" in alias.stderr, "alias diagnostic was not focused")
        check(source.read_text(encoding="utf-8") == source_before_alias, "alias changed source")

        hardlink_alias = root / "hardlink-alias.c"
        os.link(source, hardlink_alias)
        hardlink = run_compiler("--emit-c", str(hardlink_alias), str(source))
        check(hardlink.returncode != 0, "hardlink source/output alias unexpectedly succeeded")
        check("codegen: io-error: output aliases source" in hardlink.stderr, "hardlink alias diagnostic lost")
        check(source.read_text(encoding="utf-8") == source_before_alias, "hardlink alias changed source")

        direct_target = root / "direct-target.c"
        direct_target.write_text("preserve direct target\n", encoding="utf-8")
        direct_link = root / "direct-output-link.c"
        os.symlink(direct_target, direct_link)
        direct_link_result = run_compiler("--emit-c", str(direct_link), str(source))
        check(direct_link_result.returncode != 0, "direct output symlink unexpectedly succeeded")
        check("codegen: io-error" in direct_link_result.stderr, "direct symlink lacked I/O diagnostic")
        check(direct_target.read_text(encoding="utf-8") == "preserve direct target\n", "direct symlink target changed")
        check(direct_link.is_symlink(), "direct output symlink was replaced")

        dangling_link = root / "dangling-output-link.c"
        os.symlink(root / "missing-target.c", dangling_link)
        dangling_result = run_compiler("--emit-c", str(dangling_link), str(source))
        check(dangling_result.returncode != 0, "dangling output symlink unexpectedly succeeded")
        check("codegen: io-error" in dangling_result.stderr, "dangling symlink lacked I/O diagnostic")
        check(dangling_link.is_symlink(), "dangling output symlink was replaced")

        publication = root / "publication"
        publication.mkdir()
        planted_target = root / "outside-target-does-not-exist.c"
        planted_output = publication / "planted-output.c"
        planted_temp = publication / f".{planted_output.name}.restricted-c-tmp-0"
        os.symlink(planted_target, planted_temp)
        planted = run_compiler("--emit-c", str(planted_output), str(source))
        check(planted.returncode == 0, f"preplanted temp symlink blocked safe retry: {planted.stderr!r}")
        check(not planted_target.exists(), "dangling temp symlink created or redirected outside output")
        check(planted_output.read_text(encoding="utf-8") == generated, "safe retry emitted wrong output")
        planted_temp.unlink()

        variant_source = root / "concurrent-variant.src"
        variant_source.write_text(
            "program concurrent_variant is\n"
            "variable value : integer;\n"
            "begin\n"
            "    value := 99;\n"
            "end program.\n",
            encoding="utf-8",
        )
        variant_output = root / "concurrent-variant.c"
        variant = run_compiler("--emit-c", str(variant_output), str(variant_source))
        check(variant.returncode == 0, f"variant emit failed: {variant.stderr!r}")
        variant_generated = variant_output.read_text(encoding="utf-8")
        check(variant_generated != generated, "concurrency variants unexpectedly match")
        concurrent_output = root / "concurrent.c"
        concurrent_sources = [source if index % 2 == 0 else variant_source for index in range(32)]
        with ThreadPoolExecutor(max_workers=32) as executor:
            concurrent = list(executor.map(
                lambda item: run_compiler("--emit-c", str(concurrent_output), str(item)),
                concurrent_sources,
            ))
        check(all(result.returncode == 0 for result in concurrent), "concurrent emits did not all succeed")
        check(
            concurrent_output.read_text(encoding="utf-8") in (generated, variant_generated),
            "concurrent emit produced a torn or foreign output",
        )
        check_no_temp_debris(root)

    print("generated C test pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
