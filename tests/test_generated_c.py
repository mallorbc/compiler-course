#!/usr/bin/env python3
"""Stage 4B process checks for restricted-C emission (stdlib only)."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
import os
import re
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


def check_flat_array_c(generated: str, fixture: str) -> None:
    main_offset = generated.find("int main(void)")
    check(main_offset != -1, f"{fixture}: generated C has no main")
    user_generated = generated[main_offset:]
    direct_store = re.search(r"MM\[[^\n;]*\]\s*=\s*MM\[", user_generated)
    check(direct_store is None,
          f"{fixture}: direct MM-to-MM assignment: "
          f"{direct_store.group(0) if direct_store is not None else ''}")
    check(not re.search(r"\b[A-Za-z_]\w*\([^;\n]*MM\[", user_generated),
          f"{fixture}: helper receives a direct MM operand")
    check(not re.search(r"\bif\s*\([^\n)]*MM\[", user_generated),
          f"{fixture}: branch consumes MM directly")
    for line in user_generated.splitlines():
        stripped = line.strip()
        if " = " not in stripped:
            continue
        right_hand_side = stripped.split(" = ", 1)[1]
        if "MM[" not in right_hand_side:
            continue
        check(not stripped.startswith("MM["),
              f"{fixture}: MM-to-MM store: {stripped}")
        check(right_hand_side.startswith("MM[") and
              right_hand_side.endswith("];") and
              right_hand_side.count("MM[") == 1,
              f"{fixture}: MM appears outside one pure load: {stripped}")


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
    object_file = generated.with_suffix(".o")
    result = subprocess.run(
        [
            c_compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic-errors",
            "-c",
            str(generated),
            "-o",
            str(object_file),
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
        "strict C11 object compilation failed:\n"
        f"stdout={result.stdout!r}\nstderr={result.stderr!r}\nsource={generated.read_text()!r}",
    )


def strict_c11_compile_and_run(
    generated: Path,
    expected_stdout: str,
    expected_returncode: int = 0,
    stdin_text: str = "",
    link_math: bool = False,
    timeout_sec: float = TIMEOUT_SEC,
) -> None:
    c_compiler = next(
        (candidate for candidate in ("cc", "gcc", "clang") if shutil.which(candidate)), None
    )
    if c_compiler is None:
        print("SKIP test_generated_c.py: no C11 compiler available for Gate4 runtime check")
        return
    executable = generated.with_suffix(".native")
    command = [
            c_compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic-errors",
            str(generated),
            "-o",
            str(executable),
        ]
    if link_math:
        command.append("-lm")
    compilation = subprocess.run(
        command,
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout_sec,
        check=False,
    )
    check(
        compilation.returncode == 0,
        f"Gate4 C compilation failed: stdout={compilation.stdout!r} stderr={compilation.stderr!r}",
    )
    execution = subprocess.run(
        [str(executable)],
        cwd=REPO_ROOT,
        input=stdin_text,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout_sec,
        check=False,
    )
    check(
        execution.returncode == expected_returncode,
        f"native executable exit was {execution.returncode}, expected {expected_returncode}",
    )
    check(execution.stdout == expected_stdout, f"native stdout was {execution.stdout!r}")
    check(execution.stderr == "", f"Gate4 stderr was {execution.stderr!r}")


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
        check("if (Reg[" in generated and "goto L_f0_d0_0;" in generated,
              "division is not flat-lowered")
        check("else" not in generated and "return 1;" not in generated, "division has inline control flow")
        check_no_temp_debris(root)

        second = run_compiler("--emit-c", str(output), str(source))
        check(second.returncode == 0, f"second emit failed: {second.stderr!r}")
        check(generated == output.read_text(encoding="utf-8"), "restricted-C output is not deterministic")
        strict_c11_syntax_check(output)

        gate4_source = root / "gate4.src"
        gate4_output = root / "gate4.c"
        gate4_source.write_text(
            "program Gate4 is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    answer := 6 * 7;\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        gate4 = run_compiler("--emit-c", str(gate4_output), str(gate4_source))
        check(gate4.returncode == 0, f"Gate4 emit failed: {gate4.stderr!r}")
        gate4_c = gate4_output.read_text(encoding="utf-8")
        check("#include <inttypes.h>" in gate4_c and "#include <stdio.h>" in gate4_c,
              "Gate4 runtime headers are missing")
        check("R_put_i32" in gate4_c, "Gate4 runtime helper is missing")
        for source_identifier in ("Gate4", "answer", "printed", "putInteger"):
            check(source_identifier not in gate4_c, f"Gate4 source name leaked: {source_identifier}")
        strict_c11_compile_and_run(gate4_output, "42\n")

        singleton_cases = (
            (
                "get-integer",
                "program RuntimeGetInteger is\n"
                "variable value : integer;\n"
                "begin\n"
                "    value := getInteger();\n"
                "end program.\n",
                "-2147483648\n",
                "",
                ("R_get_i32", "R_decimal_i32", "R_next_token_char"),
                ("R_get_b1", "R_bool_word", "R_skip_token", "R_put_i32", "R_put_b1"),
            ),
            (
                "get-bool",
                "program RuntimeGetBool is\n"
                "variable value : bool;\n"
                "begin\n"
                "    value := getBool();\n"
                "end program.\n",
                "FaLsE\n",
                "",
                ("R_get_b1", "R_bool_word", "R_skip_token", "R_decimal_i32"),
                ("R_get_i32", "R_put_i32", "R_put_b1"),
            ),
            (
                "put-integer",
                "program RuntimePutInteger is\n"
                "variable result : bool;\n"
                "begin\n"
                "    result := putInteger(2147483647);\n"
                "end program.\n",
                "",
                "2147483647\n",
                ("R_put_i32",),
                ("R_get_i32", "R_get_b1", "R_put_b1", "R_decimal_i32"),
            ),
            (
                "put-bool",
                "program RuntimePutBool is\n"
                "variable result : bool;\n"
                "begin\n"
                "    result := putBool(true);\n"
                "    if (result) then\n"
                "        result := putBool(false);\n"
                "    else\n"
                "        result := putBool(true);\n"
                "    end if;\n"
                "end program.\n",
                "",
                "true\nfalse\n",
                ("R_put_b1",),
                ("R_get_i32", "R_get_b1", "R_put_i32", "R_decimal_i32"),
            ),
        )
        for name, singleton_source_text, singleton_stdin, singleton_stdout, required, absent in singleton_cases:
            singleton_source = root / f"stage6a-{name}.src"
            singleton_output = root / f"stage6a-{name}.c"
            singleton_source.write_text(singleton_source_text, encoding="utf-8")
            singleton = run_compiler("--emit-c", str(singleton_output), str(singleton_source))
            check(singleton.returncode == 0,
                  f"Stage6A {name} singleton emit failed: {singleton.stderr!r}")
            singleton_c = singleton_output.read_text(encoding="utf-8")
            for helper in required:
                check(helper in singleton_c, f"Stage6A {name} omitted helper {helper}")
            for helper in absent:
                check(helper not in singleton_c, f"Stage6A {name} leaked helper {helper}")
            strict_c11_compile_and_run(
                singleton_output, singleton_stdout, stdin_text=singleton_stdin
            )

        runtime_tokens_source = root / "stage6a-tokens.src"
        runtime_tokens_output = root / "stage6a-tokens.c"
        runtime_tokens_source.write_text(
            "program Stage6ATokens is\n"
            "variable int_slot : integer;\n"
            "variable flag : bool;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "    int_slot := getInteger();\n"
            "    printed := putInteger(int_slot);\n"
            "    flag := getBool();\n"
            "    printed := putBool(flag);\n"
            "end program.\n",
            encoding="utf-8",
        )
        runtime_tokens = run_compiler(
            "--emit-c", str(runtime_tokens_output), str(runtime_tokens_source)
        )
        check(runtime_tokens.returncode == 0,
              f"Stage6A token runtime emit failed: {runtime_tokens.stderr!r}")
        runtime_tokens_c = runtime_tokens_output.read_text(encoding="utf-8")
        for helper in ("R_get_i32", "R_get_b1", "R_put_i32", "R_put_b1"):
            check(helper in runtime_tokens_c, f"Stage6A omitted required helper {helper}")
        check("#include <ctype.h>" in runtime_tokens_c,
              "Stage6A token runtime omitted ctype support")
        for source_identifier in ("Stage6ATokens", "int_slot", "flag", "printed",
                                  "getInteger", "getBool", "putInteger", "putBool"):
            check(source_identifier not in runtime_tokens_c,
                  f"Stage6A source identifier leaked into C: {source_identifier}")
        strict_c11_compile_and_run(
            runtime_tokens_output,
            "2147483647\n-2147483648\n0\n17\n0\n-23\n0\n5\n0\n6\n"
            "true\nfalse\ntrue\nfalse\ntrue\nfalse\ntrue\n0\nfalse\n",
            stdin_text=(
                "2147483647 -2147483648 2147483648 17 -2147483649 -23 "
                "+ 5 12x 6 TRUE fAlSe -2 2147483648 true nope 1"
            ),
        )
        runtime_tokens_repeat = root / "stage6a-tokens-repeat.c"
        runtime_tokens_second = run_compiler(
            "--emit-c", str(runtime_tokens_repeat), str(runtime_tokens_source)
        )
        check(runtime_tokens_second.returncode == 0,
              f"second Stage6A token runtime emit failed: {runtime_tokens_second.stderr!r}")
        check(runtime_tokens_c == runtime_tokens_repeat.read_text(encoding="utf-8"),
              "Stage6A token runtime C is not deterministic")

        runtime_procedure_source = root / "stage6a-procedure.src"
        runtime_procedure_output = root / "stage6a-procedure.c"
        runtime_procedure_source.write_text(
            "program Stage6AProcedure is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "procedure readAndReport : integer(variable limit : integer)\n"
            "variable value : integer;\n"
            "variable flag : bool;\n"
            "variable did_print : bool;\n"
            "begin\n"
            "    value := getInteger();\n"
            "    flag := getBool();\n"
            "    if (flag) then\n"
            "        did_print := putBool(true);\n"
            "    else\n"
            "        did_print := putBool(false);\n"
            "    end if;\n"
            "    for (limit := 0; limit < 1)\n"
            "        did_print := putInteger(value);\n"
            "        limit := limit + 1;\n"
            "    end for;\n"
            "    return value;\n"
            "end procedure;\n"
            "begin\n"
            "    answer := readAndReport(0);\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        runtime_procedure = run_compiler(
            "--emit-c", str(runtime_procedure_output), str(runtime_procedure_source)
        )
        check(runtime_procedure.returncode == 0,
              f"Stage6A procedure runtime emit failed: {runtime_procedure.stderr!r}")
        runtime_procedure_c = runtime_procedure_output.read_text(encoding="utf-8")
        check("L_f10_b0:" in runtime_procedure_c,
              "Stage6A procedure runtime omitted the reachable procedure")
        check("if (MM[" not in runtime_procedure_c,
              "Stage6A procedure runtime branches directly on frame memory")
        check(not re.search(r"MM\[[^\n]+\]\s*=\s*R_", runtime_procedure_c),
              "Stage6A getter result bypasses a scratch register")
        check(not re.search(r"R_put_[^(]+\([^\n]*MM\[", runtime_procedure_c),
              "Stage6A output argument bypasses a scratch register")
        check(not re.search(r"MM\[[^\n]+\]\s*=\s*MM\[", runtime_procedure_c),
              "Stage6A procedure runtime emits direct MM-to-MM traffic")
        strict_c11_compile_and_run(
            runtime_procedure_output, "true\n12\n12\n", stdin_text="12 TRUE\n"
        )

        procedure_source = root / "stage5c-procedures.src"
        procedure_output = root / "stage5c-procedures.c"
        procedure_source.write_text(
            "program Stage5C is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "procedure sumdown : integer(variable n : integer)\n"
            "begin\n"
            "    if (n == 0) then\n"
            "        return 0;\n"
            "    else\n"
            "        return n + sumdown(n - 1);\n"
            "    end if;\n"
            "end procedure;\n"
            "procedure count : integer(variable n : integer)\n"
            "variable i : integer;\n"
            "variable total : integer;\n"
            "begin\n"
            "    total := 0;\n"
            "    for (i := 0; i < n)\n"
            "        total := total + 1;\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    return total;\n"
            "end procedure;\n"
            "begin\n"
            "    answer := sumdown(4) + count(2);\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        procedure = run_compiler(
            "--emit-c", str(procedure_output), str(procedure_source)
        )
        check(procedure.returncode == 0, f"Stage5C procedure emit failed: {procedure.stderr!r}")
        procedure_c = procedure_output.read_text(encoding="utf-8")
        check("L_f10_b0:" in procedure_c and "L_f11_b0:" in procedure_c,
              "Stage5C procedure labels are missing")
        check("L_f0_r0:" in procedure_c and "L_f10_c" in procedure_c,
              "Stage5C continuation dispatch is missing")
        for source_identifier in ("Stage5C", "sumdown", "count", "answer", "printed"):
            check(source_identifier not in procedure_c,
                  f"Stage5C source identifier leaked into C: {source_identifier}")
        check(not re.search(r"MM\[[^\n]+\]\s*=\s*MM\[", procedure_c),
              "Stage5C procedure lowering emitted a direct MM-to-MM assignment")
        check("MM[(uint32_t)Reg[" in procedure_c,
              "Stage5C procedure frame traffic was not staged through registers")
        strict_c11_compile_and_run(procedure_output, "12\n")

        procedure_matrix_source = root / "stage5c-matrix.src"
        procedure_matrix_output = root / "stage5c-matrix.c"
        procedure_matrix_source.write_text(
            "program Stage5CMatrix is\n"
            "variable global_value : integer;\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "procedure bump : integer(variable global_value : integer)\n"
            "begin\n"
            "    global_value := global_value + 1;\n"
            "    return global_value;\n"
            "end procedure;\n"
            "procedure setglobal : integer()\n"
            "begin\n"
            "    global_value := global_value + 10;\n"
            "    return global_value;\n"
            "end procedure;\n"
            "procedure pair : integer(variable left : integer, variable right : integer)\n"
            "begin\n"
            "    return left * 10 + right;\n"
            "end procedure;\n"
            "procedure choose : integer(variable value : integer)\n"
            "begin\n"
            "    if (value == 0) then\n"
            "        return 44;\n"
            "    else\n"
            "        return 55;\n"
            "    end if;\n"
            "end procedure;\n"
            "procedure iterate : integer(variable value : integer)\n"
            "variable i : integer;\n"
            "variable total : integer;\n"
            "begin\n"
            "    total := 0;\n"
            "    for (i := 0; i < value)\n"
            "        total := total + 1;\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    return total;\n"
            "end procedure;\n"
            "procedure speak : integer(variable value : integer)\n"
            "variable did_print : bool;\n"
            "begin\n"
            "    did_print := putInteger(value);\n"
            "    return value;\n"
            "end procedure;\n"
            "procedure truth : bool()\n"
            "begin\n"
            "    return 9;\n"
            "end procedure;\n"
            "procedure negate : integer(variable value : integer)\n"
            "begin\n"
            "    return -value;\n"
            "end procedure;\n"
            "procedure minimumDivide : integer()\n"
            "begin\n"
            "    return (-2147483647 - 1) / -1;\n"
            "end procedure;\n"
            "procedure divide : integer(variable value : integer)\n"
            "begin\n"
            "    return value / 2;\n"
            "end procedure;\n"
            "begin\n"
            "    global_value := 5;\n"
            "    answer := bump(global_value);\n"
            "    printed := putInteger(answer);\n"
            "    printed := putInteger(global_value);\n"
            "    answer := setglobal();\n"
            "    printed := putInteger(answer);\n"
            "    answer := pair(speak(1), speak(2));\n"
            "    printed := putInteger(answer);\n"
            "    answer := choose(0);\n"
            "    printed := putInteger(answer);\n"
            "    answer := iterate(3);\n"
            "    printed := putInteger(answer);\n"
            "    answer := truth();\n"
            "    printed := putInteger(answer);\n"
            "    for (answer := 0; answer < 3)\n"
            "        answer := bump(answer);\n"
            "    end for;\n"
            "    printed := putInteger(answer);\n"
            "    answer := speak(9);\n"
            "    printed := putInteger(answer);\n"
            "    answer := negate(-5);\n"
            "    printed := putInteger(answer);\n"
            "    answer := minimumDivide();\n"
            "    printed := putInteger(answer);\n"
            "    answer := divide(8);\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        procedure_matrix = run_compiler(
            "--emit-c", str(procedure_matrix_output), str(procedure_matrix_source)
        )
        check(procedure_matrix.returncode == 0,
              f"Stage5C matrix emit failed: {procedure_matrix.stderr!r}")
        matrix_c = procedure_matrix_output.read_text(encoding="utf-8")
        check(not re.search(r"MM\[[^\n]+\]\s*=\s*MM\[", matrix_c),
              "Stage5C matrix contains a direct MM-to-MM assignment")
        check("if (MM[" not in matrix_c,
              "Stage5C matrix uses memory directly as a branch operand")
        check(not re.search(r"MM\[[^\n]*MM\[", matrix_c),
              "Stage5C matrix uses nested MM indirection")
        for line in matrix_c.splitlines():
            stripped = line.strip()
            if stripped.startswith("MM[") and " = " in stripped:
                right_hand_side = stripped.split(" = ", 1)[1]
                check("MM[" not in right_hand_side,
                      f"Stage5C MM store bypassed scratch registers: {stripped}")
            elif " = " in stripped and "MM[" in stripped.split(" = ", 1)[1]:
                right_hand_side = stripped.split(" = ", 1)[1]
                check(right_hand_side.startswith("MM[") and
                      right_hand_side.endswith("];") and
                      right_hand_side.count("MM[") == 1,
                      f"Stage5C arithmetic used an MM operand directly: {stripped}")
            if stripped.startswith("if ("):
                check(stripped.startswith("if (Reg["),
                      f"Stage5C branch bypassed a normalized register: {stripped}")
        strict_c11_compile_and_run(
            procedure_matrix_output,
            "6\n5\n15\n1\n2\n12\n44\n3\n1\n3\n9\n9\n5\n-2147483648\n4\n",
        )
        procedure_matrix_repeat = root / "stage5c-matrix-repeat.c"
        repeat_matrix = run_compiler(
            "--emit-c", str(procedure_matrix_repeat), str(procedure_matrix_source)
        )
        check(repeat_matrix.returncode == 0,
              f"repeated Stage5C matrix emit failed: {repeat_matrix.stderr!r}")
        check(matrix_c == procedure_matrix_repeat.read_text(encoding="utf-8"),
              "Stage5C procedure output is not deterministic")

        procedure_divide_source = root / "stage5c-procedure-divide.src"
        procedure_divide_output = root / "stage5c-procedure-divide.c"
        procedure_divide_source.write_text(
            "program Stage5CDivide is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "procedure bad : integer()\n"
            "begin\n"
            "    return 1 / 0;\n"
            "end procedure;\n"
            "begin\n"
            "    answer := bad();\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        procedure_divide = run_compiler(
            "--emit-c", str(procedure_divide_output), str(procedure_divide_source)
        )
        check(procedure_divide.returncode == 0,
              f"procedure divide emit failed: {procedure_divide.stderr!r}")
        strict_c11_compile_and_run(procedure_divide_output, "", expected_returncode=1)

        stack_source = root / "stage5c-stack-overflow.src"
        stack_output = root / "stage5c-stack-overflow.c"
        stack_source.write_text(
            "program Stage5CStack is\n"
            "variable answer : integer;\n"
            "procedure recurse : integer(variable value : integer)\n"
            "variable a : integer;\n"
            "variable b : integer;\n"
            "variable c : integer;\n"
            "variable d : integer;\n"
            "variable e : integer;\n"
            "variable f : integer;\n"
            "variable g : integer;\n"
            "variable h : integer;\n"
            "begin\n"
            "    return recurse(value + 1);\n"
            "end procedure;\n"
            "begin\n"
            "    answer := recurse(0);\n"
            "end program.\n",
            encoding="utf-8",
        )
        stack = run_compiler("--emit-c", str(stack_output), str(stack_source))
        check(stack.returncode == 0, f"stack-overflow emit failed: {stack.stderr!r}")
        strict_c11_compile_and_run(stack_output, "", expected_returncode=1)

        no_capture_source = root / "stage5c-no-capture.src"
        no_capture_output = root / "stage5c-no-capture.c"
        no_capture_output.write_text("preserve no-capture output\n", encoding="utf-8")
        no_capture_source.write_text(
            "program Stage5CNoCapture is\n"
            "procedure outer : integer()\n"
            "variable hidden : integer;\n"
            "procedure inner : integer()\n"
            "begin\n"
            "    return hidden;\n"
            "end procedure;\n"
            "begin\n"
            "    hidden := 1;\n"
            "    return inner();\n"
            "end procedure;\n"
            "begin\n"
            "end program.\n",
            encoding="utf-8",
        )
        no_capture = run_compiler("--emit-c", str(no_capture_output), str(no_capture_source))
        check(no_capture.returncode != 0, "nested no-capture source unexpectedly emitted C")
        check('Undeclared identifier "hidden"' in no_capture.stdout,
              f"nested no-capture failure was not focused: {no_capture.stdout!r}")
        check(no_capture_output.read_text(encoding="utf-8") == "preserve no-capture output\n",
              "nested no-capture frontend failure replaced the output sentinel")

        unused_procedure_source = root / "stage5c-unused-procedure.src"
        unused_procedure_output = root / "stage5c-unused-procedure.c"
        unused_procedure_source.write_text(
            "program Stage5CUnused is\n"
            "procedure hidden : integer()\n"
            "begin\n"
            "    return 1;\n"
            "end procedure;\n"
            "begin\n"
            "end program.\n",
            encoding="utf-8",
        )
        unused_procedure = run_compiler(
            "--emit-c", str(unused_procedure_output), str(unused_procedure_source)
        )
        check(unused_procedure.returncode == 0,
              f"unused Stage5C procedure blocked emission: {unused_procedure.stderr!r}")
        unused_procedure_c = unused_procedure_output.read_text(encoding="utf-8")
        check("L_f10_" not in unused_procedure_c,
              "unreachable procedure leaked labels into restricted C")
        strict_c11_compile_and_run(unused_procedure_output, "")

        compact_source = root / "stage5c-compact-layout.src"
        compact_output = root / "stage5c-compact-layout.c"
        compact_source.write_text(
            "program Stage5CCompact is\n"
            "variable first : integer;\n"
            "variable second : integer;\n"
            "variable printed : bool;\n"
            "procedure dead : integer(variable parameter : integer)\n"
            "variable local : integer;\n"
            "begin\n"
            "    local := getInteger();\n"
            "    return local + parameter;\n"
            "end procedure;\n"
            "procedure live : integer()\n"
            "begin\n"
            "    return 7;\n"
            "end procedure;\n"
            "begin\n"
            "    first := live();\n"
            "    printed := putInteger(first);\n"
            "end program.\n",
            encoding="utf-8",
        )
        compact = run_compiler("--emit-c", str(compact_output), str(compact_source))
        check(compact.returncode == 0,
              f"compact Stage5C layout emit failed: {compact.stderr!r}")
        compact_c = compact_output.read_text(encoding="utf-8")
        check("Reg[0u] = INT32_C(4);" in compact_c,
              "compact Stage5C layout did not reserve exactly three globals and one call spill")
        check("L_f10_" not in compact_c and "L_f11_b0:" in compact_c,
              "reachable closure emitted the dead procedure or omitted the live procedure")
        check("R_get" not in compact_c,
              "an unsupported builtin reachable only from a dead procedure leaked into C")
        strict_c11_compile_and_run(compact_output, "7\n")

        gate5_source = root / "gate5-if.src"
        gate5_output = root / "gate5-if.c"
        gate5_source.write_text(
            "program Gate5 is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    answer := 6 * 7;\n"
            "    if (answer) then\n"
            "        printed := putInteger(answer);\n"
            "    else\n"
            "        printed := putInteger(0);\n"
            "    end if;\n"
            "end program.\n",
            encoding="utf-8",
        )
        gate5 = run_compiler("--emit-c", str(gate5_output), str(gate5_source))
        check(gate5.returncode == 0, f"Gate5 if emit failed: {gate5.stderr!r}")
        gate5_c = gate5_output.read_text(encoding="utf-8")
        for label in ("L_f0_b0:", "L_f0_b1:", "L_f0_b2:", "L_f0_b3:"):
            check(label in gate5_c, f"Gate5 missing CFG label {label}")
        check("L_f0_x0:" in gate5_c, "Gate5 missing common exit label")
        check("else" not in gate5_c, "Gate5 used structured C else")
        strict_c11_compile_and_run(gate5_output, "42\n")

        gate5_false_source = root / "gate5-false.src"
        gate5_false_output = root / "gate5-false.c"
        gate5_false_source.write_text(
            "program Gate5False is\n"
            "variable printed : bool;\n"
            "begin\n"
            "    if (0) then\n"
            "        printed := putInteger(1);\n"
            "    else\n"
            "        printed := putInteger(7);\n"
            "    end if;\n"
            "end program.\n",
            encoding="utf-8",
        )
        gate5_false = run_compiler("--emit-c", str(gate5_false_output), str(gate5_false_source))
        check(gate5_false.returncode == 0, f"Gate5 false emit failed: {gate5_false.stderr!r}")
        strict_c11_compile_and_run(gate5_false_output, "7\n")

        side_effect_source = root / "gate5-condition-call.src"
        side_effect_output = root / "gate5-condition-call.c"
        side_effect_source.write_text(
            "program Gate5ConditionCall is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    if (putInteger(7)) then\n"
            "        answer := 42;\n"
            "    else\n"
            "        answer := 0;\n"
            "    end if;\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        side_effect = run_compiler(
            "--emit-c", str(side_effect_output), str(side_effect_source)
        )
        check(side_effect.returncode == 0, f"condition-call emit failed: {side_effect.stderr!r}")
        strict_c11_compile_and_run(side_effect_output, "7\n42\n")

        empty_nested_source = root / "gate5-empty-nested.src"
        empty_nested_output = root / "gate5-empty-nested.c"
        empty_nested_source.write_text(
            "program Gate5EmptyNested is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    if (0) then\n"
            "    end if;\n"
            "    if (-7) then\n"
            "        if (false) then\n"
            "            answer := 0;\n"
            "        else\n"
            "            answer := 42;\n"
            "        end if;\n"
            "    else\n"
            "        answer := 1;\n"
            "    end if;\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        empty_nested = run_compiler(
            "--emit-c", str(empty_nested_output), str(empty_nested_source)
        )
        check(empty_nested.returncode == 0, f"empty/nested emit failed: {empty_nested.stderr!r}")
        strict_c11_compile_and_run(empty_nested_output, "42\n")

        dead_divide_source = root / "gate5-dead-divide.src"
        dead_divide_output = root / "gate5-dead-divide.c"
        dead_divide_source.write_text(
            "program Gate5DeadDivide is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    if (false) then\n"
            "        answer := 1 / 0;\n"
            "    else\n"
            "        answer := 42;\n"
            "    end if;\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        dead_divide = run_compiler(
            "--emit-c", str(dead_divide_output), str(dead_divide_source)
        )
        check(dead_divide.returncode == 0, f"dead-branch divide emit failed: {dead_divide.stderr!r}")
        strict_c11_compile_and_run(dead_divide_output, "42\n")

        taken_divide_source = root / "gate5-taken-divide.src"
        taken_divide_output = root / "gate5-taken-divide.c"
        taken_divide_source.write_text(
            "program Gate5TakenDivide is\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    if (true) then\n"
            "        answer := 1 / 0;\n"
            "    else\n"
            "        answer := 42;\n"
            "    end if;\n"
            "    printed := putInteger(answer);\n"
            "end program.\n",
            encoding="utf-8",
        )
        taken_divide = run_compiler(
            "--emit-c", str(taken_divide_output), str(taken_divide_source)
        )
        check(taken_divide.returncode == 0, f"taken-branch divide emit failed: {taken_divide.stderr!r}")
        strict_c11_compile_and_run(taken_divide_output, "", expected_returncode=1)

        runtime_edges_source = root / "runtime-edges.src"
        runtime_edges_output = root / "runtime-edges.c"
        runtime_edges_source.write_text(
            "program RuntimeEdges is\n"
            "variable value : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    value := -5;\n"
            "    printed := putInteger(value);\n"
            "    value := printed;\n"
            "    printed := putInteger(value);\n"
            "    value := -2147483647 - 1;\n"
            "    printed := putInteger(value);\n"
            "end program.\n",
            encoding="utf-8",
        )
        runtime_edges = run_compiler(
            "--emit-c", str(runtime_edges_output), str(runtime_edges_source)
        )
        check(runtime_edges.returncode == 0,
              f"runtime-edge emit failed: {runtime_edges.stderr!r}")
        strict_c11_compile_and_run(runtime_edges_output, "-5\n1\n-2147483648\n")

        loop_source = root / "gate5b-loops.src"
        loop_output = root / "gate5b-loops.c"
        loop_source.write_text(
            "program Gate5BLoops is\n"
            "variable i : integer;\n"
            "variable j : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    for (i := 0; i < 3)\n"
            "        printed := putInteger(i);\n"
            "        if (i == 1) then\n"
            "            printed := putInteger(50);\n"
            "        end if;\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    printed := putInteger(i);\n"
            "    for (i := -2; i)\n"
            "        printed := putInteger(i);\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    for (j := 0; false)\n"
            "        printed := putInteger(99);\n"
            "    end for;\n"
            "    for (i := 0; i < 2)\n"
            "        for (j := 0; j < 2)\n"
            "            printed := putInteger(i * 2 + j);\n"
            "            j := j + 1;\n"
            "        end for;\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    if (true) then\n"
            "        for (j := 0; j < 2)\n"
            "            printed := putInteger(j + 10);\n"
            "            j := j + 1;\n"
            "        end for;\n"
            "    end if;\n"
            "    for (i := 0; putInteger(i) & (i < 2))\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    printed := putInteger(42);\n"
            "end program.\n",
            encoding="utf-8",
        )
        loop_result = run_compiler("--emit-c", str(loop_output), str(loop_source))
        check(loop_result.returncode == 0, f"Gate5B loop emit failed: {loop_result.stderr!r}")
        loop_c = loop_output.read_text(encoding="utf-8")
        check("goto L_f0_b1;" in loop_c, "Gate5B output is missing a loop backedge")
        check("while" not in loop_c and "for (" not in loop_c and "else" not in loop_c,
              "Gate5B output used structured C control flow")
        labels = re.findall(r"^L_f0_(?:[bd]\\d+(?:_\\d+)?|x0):$", loop_c, flags=re.MULTILINE)
        check(len(labels) == len(set(labels)), "Gate5B output reused a numeric label")
        for target in re.findall(r"goto (L_f0_(?:[bd]\\d+(?:_\\d+)?|x0));", loop_c):
            check(f"{target}:" in loop_c, f"Gate5B goto target is missing: {target}")
        for source_identifier in ("Gate5BLoops", "printed", "putInteger"):
            check(source_identifier not in loop_c,
                  f"Gate5B source identifier leaked into C: {source_identifier}")
        strict_c11_compile_and_run(
            loop_output, "0\n1\n50\n2\n3\n-2\n-1\n0\n1\n2\n3\n10\n11\n0\n1\n2\n42\n"
        )

        loop_initializer_source = root / "gate5b-initializer-call.src"
        loop_initializer_output = root / "gate5b-initializer-call.c"
        loop_initializer_source.write_text(
            "program Gate5BInitializerCall is\n"
            "variable i : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    for (i := putInteger(7); i < 3)\n"
            "        printed := putInteger(i);\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    printed := putInteger(i);\n"
            "end program.\n",
            encoding="utf-8",
        )
        loop_initializer = run_compiler(
            "--emit-c", str(loop_initializer_output), str(loop_initializer_source)
        )
        check(loop_initializer.returncode == 0,
              f"loop initializer call emit failed: {loop_initializer.stderr!r}")
        strict_c11_compile_and_run(loop_initializer_output, "7\n1\n2\n3\n")

        loop_dead_divide_source = root / "gate5b-dead-divide.src"
        loop_dead_divide_output = root / "gate5b-dead-divide.c"
        loop_dead_divide_source.write_text(
            "program Gate5BDeadDivide is\n"
            "variable i : integer;\n"
            "variable value : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    for (i := 0; i < 1)\n"
            "        if (false) then\n"
            "            value := 1 / 0;\n"
            "        else\n"
            "            value := 7;\n"
            "        end if;\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    printed := putInteger(value);\n"
            "end program.\n",
            encoding="utf-8",
        )
        loop_dead_divide = run_compiler(
            "--emit-c", str(loop_dead_divide_output), str(loop_dead_divide_source)
        )
        check(loop_dead_divide.returncode == 0,
              f"dead loop divide emit failed: {loop_dead_divide.stderr!r}")
        strict_c11_compile_and_run(loop_dead_divide_output, "7\n")

        loop_taken_divide_source = root / "gate5b-taken-divide.src"
        loop_taken_divide_output = root / "gate5b-taken-divide.c"
        loop_taken_divide_source.write_text(
            "program Gate5BTakenDivide is\n"
            "variable i : integer;\n"
            "variable value : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    for (i := 0; i < 1)\n"
            "        value := 1 / 0;\n"
            "        i := i + 1;\n"
            "    end for;\n"
            "    printed := putInteger(99);\n"
            "end program.\n",
            encoding="utf-8",
        )
        loop_taken_divide = run_compiler(
            "--emit-c", str(loop_taken_divide_output), str(loop_taken_divide_source)
        )
        check(loop_taken_divide.returncode == 0,
              f"taken loop divide emit failed: {loop_taken_divide.stderr!r}")
        strict_c11_compile_and_run(loop_taken_divide_output, "", expected_returncode=1)

        float_source = root / "stage6b-float.src"
        float_output = root / "stage6b-float.c"
        float_source.write_text(
            "program Stage6BFloat is\n"
            "variable value : float;\n"
            "variable printed : bool;\n"
            "procedure recurse : float(variable current : float, variable n : integer)\n"
            "variable scratch : float;\n"
            "begin\n"
            "    scratch := current;\n"
            "    if (n == 0) then\n"
            "        return scratch;\n"
            "    else\n"
            "        return recurse(scratch + 0.5, n - 1);\n"
            "    end if;\n"
            "end procedure;\n"
            "begin\n"
            "    value := recurse(1.0, 2);\n"
            "    printed := putFloat(value);\n"
            "    printed := putFloat(0.0);\n"
            "    printed := putFloat(-0.0);\n"
            "    printed := putFloat(0.1);\n"
            "    printed := putFloat(1.0 / 0.0);\n"
            "    printed := putFloat(1.0 / -0.0);\n"
            "    printed := putFloat(0.0 / 0.0);\n"
            "    printed := putFloat(sqrt(9));\n"
            "    printed := putFloat(sqrt(2));\n"
            "    printed := putFloat(sqrt(-1));\n"
            "    printed := putBool(1.0 < 2.0);\n"
            "    printed := putBool(1.0 <= 1.0);\n"
            "    printed := putBool(1.0 > 2.0);\n"
            "    printed := putBool(2.0 >= 2.0);\n"
            "    printed := putBool(0.0 == -0.0);\n"
            "    value := 0.0 / 0.0;\n"
            "    printed := putBool(value != value);\n"
            "end program.\n",
            encoding="utf-8",
        )
        float_emit = run_compiler("--emit-c", str(float_output), str(float_source))
        check(float_emit.returncode == 0, f"Stage6B Float emit failed: {float_emit.stderr!r}")
        float_c = float_output.read_text(encoding="utf-8")
        check("memcpy" in float_c and "union" not in float_c, "Float transport is not memcpy-only")
        check("goto L_f0_d" not in float_c, "Float division used the integer trap path")
        check(not re.search(r"R_(?:word_f32|f32_word|f32_i32)\([^\n]*MM\[", float_c),
              "procedure Float helper received an MM operand")
        strict_c11_compile_and_run(
            float_output,
            "2\n0\n-0\n0.100000001\ninf\n-inf\nnan\n3\n1.41421354\nnan\n"
            "true\ntrue\nfalse\ntrue\ntrue\ntrue\n",
            link_math=True,
        )

        float_cast_source = root / "stage6b-float-casts.src"
        float_cast_output = root / "stage6b-float-casts.c"
        float_cast_source.write_text(
            "program Stage6BFloatCasts is\n"
            "variable value : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    value := 1.9;\n"
            "    printed := putInteger(value);\n"
            "    value := -1.9;\n"
            "    printed := putInteger(value);\n"
            "    value := 1.0 / 0.0;\n"
            "    printed := putInteger(value);\n"
            "    value := -1.0 / 0.0;\n"
            "    printed := putInteger(value);\n"
            "    value := 0.0 / 0.0;\n"
            "    printed := putInteger(value);\n"
            "    value := 2147483648.0;\n"
            "    printed := putInteger(value);\n"
            "    value := -2147483648.0;\n"
            "    printed := putInteger(value);\n"
            "end program.\n",
            encoding="utf-8",
        )
        float_cast_emit = run_compiler(
            "--emit-c", str(float_cast_output), str(float_cast_source)
        )
        check(float_cast_emit.returncode == 0,
              f"Stage6B Float cast emit failed: {float_cast_emit.stderr!r}")
        strict_c11_compile_and_run(
            float_cast_output,
            "1\n-1\n2147483647\n-2147483648\n0\n2147483647\n-2147483648\n",
        )

        float_input_source = root / "stage6b-float-input.src"
        float_input_output = root / "stage6b-float-input.c"
        statements = "".join(
            "    value := getFloat();\n    printed := putFloat(value);\n" for _ in range(23)
        )
        float_input_source.write_text(
            "program Stage6BFloatInput is\n"
            "variable value : float;\n"
            "variable printed : bool;\n"
            "begin\n" + statements + "end program.\n",
            encoding="utf-8",
        )
        float_input_emit = run_compiler(
            "--emit-c", str(float_input_output), str(float_input_source)
        )
        check(float_input_emit.returncode == 0,
              f"Stage6B Float input emit failed: {float_input_emit.stderr!r}")
        strict_c11_compile_and_run(
            float_input_output,
            "1.25\n-0\n100\n0.5\n1.40129846e-45\n3.40282347e+38\n"
            "0\n11.5\n0\n12.5\n0\n13.5\n0\n14.5\n"
            "0\n15.5\n0\n16.5\n0\n17.5\n0\n18.5\n0\n",
            stdin_text=(
                "1.25 -0 1e2 .5 1.40129846e-45 3.40282347e38 "
                "bad 11.5 1.0f 12.5 0x1 13.5 1_0 14.5 "
                "nan 15.5 inf 16.5 1e999 17.5 "
                + ("9" * 10000)
                + "x 18.5"
            ),
        )

        singleton_bodies = {
            "constant": "variable value : float;\nbegin\n    value := 1.5;\n",
            "arithmetic": "variable value : float;\nbegin\n    value := 1.0 + 2.0;\n",
            "comparison": (
                "variable value : bool;\nbegin\n"
                "    value := 1.0 < 2.0;\n"
                "    value := 1.0 <= 2.0;\n"
                "    value := 1.0 > 2.0;\n"
                "    value := 1.0 >= 2.0;\n"
                "    value := 1.0 == 2.0;\n"
                "    value := 1.0 != 2.0;\n"
            ),
            "int_to_float": "variable value : float;\nbegin\n    value := 1;\n",
            "float_to_int": "variable value : integer;\nbegin\n    value := 1.5;\n",
            "get_float": "variable value : float;\nbegin\n    value := getFloat();\n",
            "put_float": (
                "variable value : bool;\nbegin\n    value := putFloat(1.5);\n"
            ),
            "sqrt": "variable value : float;\nbegin\n    value := sqrt(2);\n",
        }
        for singleton_name, singleton_body in singleton_bodies.items():
            singleton_source = root / f"stage6b-singleton-{singleton_name}.src"
            singleton_output = root / f"stage6b-singleton-{singleton_name}.c"
            singleton_source.write_text(
                f"program Stage6BSingleton{singleton_name} is\n"
                + singleton_body
                + "end program.\n",
                encoding="utf-8",
            )
            singleton_emit = run_compiler(
                "--emit-c", str(singleton_output), str(singleton_source)
            )
            check(singleton_emit.returncode == 0,
                  f"Stage6B singleton {singleton_name} failed: {singleton_emit.stderr!r}")
            if singleton_name == "comparison":
                comparison_c = singleton_output.read_text(encoding="utf-8")
                check("R_word_f32" in comparison_c,
                      "comparison-only Float omitted its decode helper")
                check("R_f32_word" not in comparison_c,
                      "comparison-only Float emitted an unused encode helper")
            strict_c11_syntax_check(singleton_output)

        procedure_comparison_source = root / "stage6b-procedure-comparison.src"
        procedure_comparison_output = root / "stage6b-procedure-comparison.c"
        procedure_comparison_source.write_text(
            "program Stage6BProcedureComparison is\n"
            "variable value : bool;\n"
            "procedure compare : bool(variable left : float, variable right : float)\n"
            "begin\n"
            "    return left < right;\n"
            "end procedure;\n"
            "begin\n"
            "    value := compare(1.0, 2.0);\n"
            "end program.\n",
            encoding="utf-8",
        )
        procedure_comparison_emit = run_compiler(
            "--emit-c", str(procedure_comparison_output), str(procedure_comparison_source)
        )
        check(procedure_comparison_emit.returncode == 0,
              f"Stage6B procedure comparison failed: {procedure_comparison_emit.stderr!r}")
        procedure_comparison_c = procedure_comparison_output.read_text(encoding="utf-8")
        check("R_word_f32" in procedure_comparison_c,
              "procedure Float comparison omitted its decode helper")
        check("R_f32_word" not in procedure_comparison_c,
              "procedure Float comparison emitted an unused encode helper")
        strict_c11_syntax_check(procedure_comparison_output)

        string_source = root / "stage6c-strings.src"
        string_output = root / "stage6c-strings.c"
        string_source.write_text(
            "program Stage6CStrings is\n"
            "variable global_text : string;\n"
            "variable alias_text : string;\n"
            "variable printed : bool;\n"
            "procedure localEcho : string(variable text : string)\n"
            "variable global_text : string;\n"
            "begin\n"
            "    global_text := text;\n"
            "    return global_text;\n"
            "end procedure;\n"
            "procedure retain : string(variable text : string, variable n : integer)\n"
            "begin\n"
            "    if (n == 0) then\n"
            "        return text;\n"
            "    else\n"
            "        return retain(text, n - 1);\n"
            "    end if;\n"
            "end procedure;\n"
            "procedure same : bool(variable left : string, variable right : string)\n"
            "begin\n"
            "    return left == right;\n"
            "end procedure;\n"
            "begin\n"
            "    printed := putString(global_text);\n"
            "    printed := putString(\"Hello world\");\n"
            "    if (printed) then\n"
            "        printed := putString(\"put-ok\");\n"
            "    end if;\n"
            "    global_text := \"alpha\";\n"
            "    alias_text := global_text;\n"
            "    global_text := \"beta\";\n"
            "    printed := putString(alias_text);\n"
            "    printed := putString(global_text);\n"
            "    printed := putString(localEcho(\"MiXeD\"));\n"
            "    printed := putString(retain(\"recursive\", 3));\n"
            "    printed := putBool(same(\"content\", \"content\"));\n"
            "    printed := putString(\"two\nlines\");\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_emit = run_compiler("--emit-c", str(string_output), str(string_source))
        check(string_emit.returncode == 0, f"Stage6C String emit failed: {string_emit.stderr!r}")
        string_c = string_output.read_text(encoding="utf-8")
        for source_identifier in (
            "Stage6CStrings", "global_text", "alias_text", "localEcho", "retain", "same",
            "Hello world", "MiXeD", "recursive", "content",
        ):
            check(source_identifier not in string_c,
                  f"Stage6C source text leaked into generated C: {source_identifier}")
        check("char *" not in string_c and "char*" not in string_c,
              "Stage6C emitted a C pointer String value")
        check(not re.search(r"R_(?:str_eq|put_str)\([^\n]*MM\[", string_c),
              "Stage6C procedure helper received an MM operand")
        strict_c11_compile_and_run(
            string_output,
            "\nHello world\nput-ok\nalpha\nbeta\nMiXeD\nrecursive\ntrue\ntwo\nlines\n",
        )

        string_layout_source = root / "stage6c-string-layout.src"
        string_layout_output = root / "stage6c-string-layout.c"
        string_layout_source.write_text(
            "program Stage6CStringLayout is\n"
            "variable printed : bool;\n"
            "procedure choose : string(variable ignored : string)\n"
            "variable local : string;\n"
            "begin\n"
            "    local := \"Q\";\n"
            "    local := \"Q\";\n"
            "    return local;\n"
            "end procedure;\n"
            "begin\n"
            "    printed := putString(choose(\"P\"));\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_layout_emit = run_compiler(
            "--emit-c", str(string_layout_output), str(string_layout_source)
        )
        check(string_layout_emit.returncode == 0,
              f"Stage6C exact layout emit failed: {string_layout_emit.stderr!r}")
        string_layout_c = string_layout_output.read_text(encoding="utf-8")
        for exact_layout_line in (
            "#define STRING_EMPTY_HANDLE INT32_C(2)\n",
            "    Reg[0u] = INT32_C(7);\n",
            "    MM[2u] = INT32_C(0);\n",
            "    MM[3u] = INT32_C(80);\n",
            "    MM[4u] = INT32_C(0);\n",
            "    MM[5u] = INT32_C(81);\n",
            "    MM[6u] = INT32_C(0);\n",
            "    Reg[8u] = 1u;\n",
        ):
            check(exact_layout_line in string_layout_c,
                  f"Stage6C exact static layout omitted {exact_layout_line!r}")
        check(string_layout_c.count("    MM[3u] = INT32_C(80);\n") == 1,
              "Stage6C Program literal P was not exactly deduplicated")
        check(string_layout_c.count("    MM[5u] = INT32_C(81);\n") == 1,
              "Stage6C reachable-procedure literal Q was not exactly deduplicated")
        check(string_layout_c.count("    Reg[6u] = INT32_C(5);\n") == 2,
              "Stage6C procedure-owned Q constants did not use exact handle 5")
        strict_c11_compile_and_run(string_layout_output, "Q\n")

        string_local_source = root / "stage6c-string-local-empty.src"
        string_local_output = root / "stage6c-string-local-empty.c"
        string_local_source.write_text(
            "program Stage6CStringLocalEmpty is\n"
            "variable printed : bool;\n"
            "procedure blank : string()\n"
            "variable untouched : string;\n"
            "begin\n"
            "    return untouched;\n"
            "end procedure;\n"
            "begin\n"
            "    printed := putString(blank());\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_local_emit = run_compiler(
            "--emit-c", str(string_local_output), str(string_local_source)
        )
        check(string_local_emit.returncode == 0,
              f"Stage6C empty local emit failed: {string_local_emit.stderr!r}")
        string_local_c = string_local_output.read_text(encoding="utf-8")
        check("    MM[(uint32_t)Reg[0u] + 4u] = INT32_C(2);\n" in string_local_c,
              "Stage6C procedure String local omitted canonical-empty initialization")
        strict_c11_compile_and_run(string_local_output, "\n")

        string_input_source = root / "stage6c-string-input.src"
        string_input_output = root / "stage6c-string-input.c"
        string_input_source.write_text(
            "program Stage6CStringInput is\n"
            "variable first : string;\n"
            "variable later : string;\n"
            "variable truth : bool;\n"
            "procedure keep : string(variable text : string, variable n : integer)\n"
            "begin\n"
            "    if (n == 0) then\n"
            "        return text;\n"
            "    else\n"
            "        return keep(text, n - 1);\n"
            "    end if;\n"
            "end procedure;\n"
            "begin\n"
            "    first := getString();\n"
            "    truth := putString(first);\n"
            "    later := getString();\n"
            "    truth := putString(later);\n"
            "    later := getString();\n"
            "    truth := putString(later);\n"
            "    truth := later == \"same\";\n"
            "    truth := putBool(truth);\n"
            "    truth := later != \"different\";\n"
            "    truth := putBool(truth);\n"
            "    later := getString();\n"
            "    truth := putString(later);\n"
            "    later := getString();\n"
            "    truth := putString(later);\n"
            "    later := getString();\n"
            "    truth := putString(later);\n"
            "    truth := putString(keep(first, 3));\n"
            "    later := getString();\n"
            "    truth := putString(later);\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_input_emit = run_compiler(
            "--emit-c", str(string_input_output), str(string_input_source)
        )
        check(string_input_emit.returncode == 0,
              f"Stage6C String input emit failed: {string_input_emit.stderr!r}")
        strict_c11_compile_and_run(
            string_input_output,
            "hello world\n\nsame\ntrue\ntrue\n\nfollowing\nfinal\nhello world\n\n",
            stdin_text="hello world\n\nsame\nbad\x00tail\nfollowing\nfinal",
        )

        string_singletons = {
            "literal": (
                "variable value : string;\nbegin\n    value := \"literal\";\n",
                (),
            ),
            "equality": (
                "variable value : bool;\nbegin\n    value := \"a\" == \"a\";\n",
                ("R_str_eq",),
            ),
            "get": (
                "variable value : string;\nbegin\n    value := getString();\n",
                ("R_get_str", "#include <stdio.h>"),
            ),
            "put": (
                "variable value : bool;\nbegin\n    value := putString(\"x\");\n",
                ("R_put_str", "#include <stdio.h>"),
            ),
        }
        for singleton_name, (singleton_body, required) in string_singletons.items():
            singleton_source = root / f"stage6c-singleton-{singleton_name}.src"
            singleton_output = root / f"stage6c-singleton-{singleton_name}.c"
            singleton_source.write_text(
                f"program Stage6CStringSingleton{singleton_name} is\n"
                + singleton_body
                + "end program.\n",
                encoding="utf-8",
            )
            singleton_emit = run_compiler(
                "--emit-c", str(singleton_output), str(singleton_source)
            )
            check(singleton_emit.returncode == 0,
                  f"Stage6C singleton {singleton_name} failed: {singleton_emit.stderr!r}")
            singleton_c = singleton_output.read_text(encoding="utf-8")
            for required_text in required:
                check(required_text in singleton_c,
                      f"Stage6C singleton {singleton_name} omitted {required_text}")
            if singleton_name == "literal":
                check("static int32_t R_" not in singleton_c and "#include <stdio.h>" not in singleton_c,
                      "literal-only String emitted a helper or I/O header")
            if singleton_name == "equality":
                check("#include <stdio.h>" not in singleton_c,
                      "String equality emitted an I/O header")
            strict_c11_syntax_check(singleton_output)

        dead_string_source = root / "stage6c-dead-string.src"
        dead_string_output = root / "stage6c-dead-string.c"
        dead_string_source.write_text(
            "program Stage6CDeadString is\n"
            "variable value : integer;\n"
            "procedure hidden : string()\n"
            "variable local : string;\n"
            "begin\n"
            "    local := getString();\n"
            "    return \"dead literal\";\n"
            "end procedure;\n"
            "begin\n"
            "    value := 7;\n"
            "end program.\n",
            encoding="utf-8",
        )
        dead_string_emit = run_compiler(
            "--emit-c", str(dead_string_output), str(dead_string_source)
        )
        check(dead_string_emit.returncode == 0,
              f"Stage6C dead String emit failed: {dead_string_emit.stderr!r}")
        dead_string_c = dead_string_output.read_text(encoding="utf-8")
        for forbidden in ("L_f10_", "R_get_str", "STRING_HEAP_REGISTER", "STRING_EMPTY_HANDLE"):
            check(forbidden not in dead_string_c,
                  f"dead String procedure contributed emitted state: {forbidden}")
        strict_c11_syntax_check(dead_string_output)

        string_capacity_source = root / "stage6c-string-capacity.src"
        string_capacity_output = root / "stage6c-string-capacity.c"
        string_capacity_source.write_text(
            "program Stage6CStringCapacity is\n"
            "variable value : string;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    value := getString();\n"
            "    printed := putString(value);\n"
            "    value := getString();\n"
            "    printed := putString(value);\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_capacity_emit = run_compiler(
            "--emit-c", str(string_capacity_output), str(string_capacity_source)
        )
        check(string_capacity_emit.returncode == 0,
              f"Stage6C capacity emit failed: {string_capacity_emit.stderr!r}")
        strict_c11_compile_and_run(
            string_capacity_output,
            "\nok\n",
            stdin_text=("x" * (16 * 1024 * 1024)) + "\nok\n",
            timeout_sec=30.0,
        )

        string_collision_source = root / "stage6c-string-collision.src"
        string_collision_output = root / "stage6c-string-collision.c"
        string_collision_source.write_text(
            "program Stage6CStringCollision is\n"
            "variable value : string;\n"
            "procedure echo : string(variable text : string)\n"
            "variable scratch : integer[5];\n"
            "begin\n"
            "    return text;\n"
            "end procedure;\n"
            "begin\n"
            "    value := getString();\n"
            "    value := echo(value);\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_collision_emit = run_compiler(
            "--emit-c", str(string_collision_output), str(string_collision_source)
        )
        check(string_collision_emit.returncode == 0,
              f"Stage6C collision emit failed: {string_collision_emit.stderr!r}")
        collision_c = string_collision_output.read_text(encoding="utf-8")
        check("STRING_HEAP_REGISTER" in collision_c and
              "(uint32_t)Reg[0u]" in collision_c,
              "Stage6C frame push omitted the heap collision guard")
        strict_c11_compile_and_run(
            string_collision_output,
            "",
            expected_returncode=1,
            stdin_text="x" * ((16 * 1024 * 1024) - 10),
            timeout_sec=30.0,
        )

        declaration_array_source = root / "stage6d1-array-declaration-only.src"
        declaration_array_output = root / "stage6d1-array-declaration-only.c"
        declaration_array_source.write_text(
            "program Stage6D1ArrayDeclarationOnly is\n"
            "variable values : integer[2];\n"
            "begin\n"
            "end program.\n",
            encoding="utf-8",
        )
        declaration_array_emit = run_compiler(
            "--emit-c", str(declaration_array_output), str(declaration_array_source)
        )
        check(declaration_array_emit.returncode == 0,
              f"Stage6D1 declaration-only array emit failed: {declaration_array_emit.stderr!r}")
        declaration_array_c = declaration_array_output.read_text(encoding="utf-8")
        check("L_f0_s0:" not in declaration_array_c,
              "declaration-only array emitted an unreferenced failure label")
        strict_c11_syntax_check(declaration_array_output)
        strict_c11_compile_and_run(declaration_array_output, "")

        copy_cast_array_source = root / "stage6d1-array-copy-cast-only.src"
        copy_cast_array_output = root / "stage6d1-array-copy-cast-only.c"
        copy_cast_array_source.write_text(
            "program Stage6D1ArrayCopyCastOnly is\n"
            "variable ints : integer[2];\n"
            "variable copy : integer[2];\n"
            "variable floats : float[2];\n"
            "begin\n"
            "    copy := ints;\n"
            "    floats := copy;\n"
            "    ints := floats;\n"
            "end program.\n",
            encoding="utf-8",
        )
        copy_cast_array_emit = run_compiler(
            "--emit-c", str(copy_cast_array_output), str(copy_cast_array_source)
        )
        check(copy_cast_array_emit.returncode == 0,
              f"Stage6D1 copy/cast-only array emit failed: {copy_cast_array_emit.stderr!r}")
        copy_cast_array_c = copy_cast_array_output.read_text(encoding="utf-8")
        check("L_f0_s0:" not in copy_cast_array_c,
              "copy/cast-only arrays emitted an unreferenced failure label")
        check_flat_array_c(copy_cast_array_c, "copy/cast-only arrays")
        strict_c11_syntax_check(copy_cast_array_output)
        strict_c11_compile_and_run(copy_cast_array_output, "")

        array_source = root / "stage6d1-array-basics.src"
        array_output = root / "stage6d1-array-basics.c"
        array_source.write_text(
            "program Stage6D1ArrayBasics is\n"
            "variable ints : integer[5];\n"
            "variable copy : integer[5];\n"
            "variable floats : float[5];\n"
            "variable bools : bool[5];\n"
            "variable index : integer;\n"
            "variable printed : bool;\n"
            "begin\n"
            "    ints[0] := 10;\n"
            "    ints[5] := 60;\n"
            "    index := 5;\n"
            "    copy := ints;\n"
            "    copy := copy;\n"
            "    printed := putInteger(copy[0]);\n"
            "    printed := putInteger(copy[index]);\n"
            "    floats := copy;\n"
            "    copy := floats;\n"
            "    bools := copy;\n"
            "    copy := bools;\n"
            "    printed := putInteger(copy[0]);\n"
            "    printed := putInteger(copy[index]);\n"
            "end program.\n",
            encoding="utf-8",
        )
        array_emit = run_compiler("--emit-c", str(array_output), str(array_source))
        check(array_emit.returncode == 0,
              f"Stage6D1 array basics emit failed: {array_emit.stderr!r}")
        array_c = array_output.read_text(encoding="utf-8")
        check("for (" not in array_c and "while (" not in array_c,
              "Stage6D1 generated a C loop instead of flat numeric labels")
        check("Stage6D1ArrayBasics" not in array_c and "ints" not in array_c,
              "Stage6D1 leaked source names into generated C")
        check(not re.search(r"MM\[[^\n;]*\]\s*=\s*MM\[", array_c),
              "Stage6D1 emitted an MM-to-MM aggregate copy")
        check_flat_array_c(array_c, "array basics")
        strict_c11_compile_and_run(array_output, "10\n60\n1\n1\n")

        zero_bound_source = root / "stage6d1-array-zero-bound.src"
        zero_bound_output = root / "stage6d1-array-zero-bound.c"
        zero_bound_source.write_text(
            "program Stage6D1ArrayZeroBound is\n"
            "variable values : integer[0];\n"
            "variable printed : bool;\n"
            "begin\n"
            "    values[0] := 7;\n"
            "    printed := putInteger(values[0]);\n"
            "end program.\n",
            encoding="utf-8",
        )
        zero_bound_emit = run_compiler(
            "--emit-c", str(zero_bound_output), str(zero_bound_source)
        )
        check(zero_bound_emit.returncode == 0,
              f"Stage6D1 zero-bound array emit failed: {zero_bound_emit.stderr!r}")
        check_flat_array_c(zero_bound_output.read_text(encoding="utf-8"),
                           "zero-bound arrays")
        strict_c11_compile_and_run(zero_bound_output, "7\n")

        for oob_name, oob_statement in (
            ("negative-read", "printed := putInteger(values[-1]);"),
            ("negative-write", "values[-1] := 7;"),
            ("upper-read", "printed := putInteger(values[1]);"),
            ("upper-write", "values[1] := 7;"),
        ):
            boundary_source = root / f"stage6d1-array-{oob_name}.src"
            boundary_output = root / f"stage6d1-array-{oob_name}.c"
            boundary_source.write_text(
                "program Stage6D1ArrayBoundary is\n"
                "variable values : integer[0];\n"
                "variable printed : bool;\n"
                "begin\n"
                f"    {oob_statement}\n"
                "end program.\n",
                encoding="utf-8",
            )
            boundary_emit = run_compiler(
                "--emit-c", str(boundary_output), str(boundary_source)
            )
            check(boundary_emit.returncode == 0,
                  f"Stage6D1 {oob_name} emit failed: {boundary_emit.stderr!r}")
            boundary_c = boundary_output.read_text(encoding="utf-8")
            check("goto L_f0_s0;" in boundary_c and "L_f0_s0:" in boundary_c,
                  f"Stage6D1 {oob_name} omitted its reachable failure edge")
            check_flat_array_c(boundary_c, f"array {oob_name}")
            strict_c11_compile_and_run(boundary_output, "", expected_returncode=1)

        index_once_source = root / "stage6d1-array-index-once.src"
        index_once_output = root / "stage6d1-array-index-once.c"
        index_once_source.write_text(
            "program Stage6D1ArrayIndexOnce is\n"
            "variable values : integer[0];\n"
            "variable counter : integer;\n"
            "variable printed : bool;\n"
            "procedure next : integer()\n"
            "begin\n"
            "    counter := counter + 1;\n"
            "    return 0;\n"
            "end procedure;\n"
            "begin\n"
            "    counter := 0;\n"
            "    values[next()] := 7;\n"
            "    printed := putInteger(counter);\n"
            "    printed := putInteger(values[next()]);\n"
            "    printed := putInteger(counter);\n"
            "end program.\n",
            encoding="utf-8",
        )
        index_once_emit = run_compiler(
            "--emit-c", str(index_once_output), str(index_once_source)
        )
        check(index_once_emit.returncode == 0,
              f"Stage6D1 side-effecting index emit failed: {index_once_emit.stderr!r}")
        index_once_c = index_once_output.read_text(encoding="utf-8")
        check_flat_array_c(index_once_c, "side-effecting array indexes")
        strict_c11_compile_and_run(index_once_output, "1\n7\n2\n")

        array_call_source = root / "stage6d1-array-call.src"
        array_call_output = root / "stage6d1-array-call.c"
        array_call_source.write_text(
            "program Stage6D1ArrayCall is\n"
            "variable values : integer[1];\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "procedure recurse : integer(variable input : integer[1], variable n : integer)\n"
            "variable values : integer[1];\n"
            "begin\n"
            "    values := input;\n"
            "    input[0] := 99;\n"
            "    if (n == 0) then\n"
            "        return values[0];\n"
            "    else\n"
            "        values[0] := values[0] + 1;\n"
            "        return recurse(values, n - 1);\n"
            "    end if;\n"
            "end procedure;\n"
            "procedure mutate : integer()\n"
            "begin\n"
            "    values[0] := 88;\n"
            "    return 1;\n"
            "end procedure;\n"
            "procedure choose : integer(variable first : integer[1], variable later : integer)\n"
            "begin\n"
            "    return first[0];\n"
            "end procedure;\n"
            "begin\n"
            "    values[0] := 7;\n"
            "    values[1] := 8;\n"
            "    answer := recurse(values, 3);\n"
            "    printed := putInteger(answer);\n"
            "    printed := putInteger(values[0]);\n"
            "    answer := choose(values, mutate());\n"
            "    printed := putInteger(answer);\n"
            "    printed := putInteger(values[0]);\n"
            "end program.\n",
            encoding="utf-8",
        )
        array_call_emit = run_compiler(
            "--emit-c", str(array_call_output), str(array_call_source)
        )
        check(array_call_emit.returncode == 0,
              f"Stage6D1 array call emit failed: {array_call_emit.stdout!r} {array_call_emit.stderr!r}")
        array_call_c = array_call_output.read_text(encoding="utf-8")
        check_flat_array_c(array_call_c, "array calls")
        strict_c11_compile_and_run(array_call_output, "10\n7\n7\n88\n")

        string_array_source = root / "stage6d1-string-array.src"
        string_array_output = root / "stage6d1-string-array.c"
        string_array_source.write_text(
            "program Stage6D1StringArray is\n"
            "variable values : string[1];\n"
            "variable copy : string[1];\n"
            "variable printed : bool;\n"
            "procedure show : bool(variable input : string[1])\n"
            "variable local : string[1];\n"
            "variable shown : bool;\n"
            "begin\n"
            "    shown := putString(local[0]);\n"
            "    local := input;\n"
            "    return putString(local[1]);\n"
            "end procedure;\n"
            "begin\n"
            "    printed := putString(values[0]);\n"
            "    values[1] := \"same\";\n"
            "    copy := values;\n"
            "    values[1] := \"changed\";\n"
            "    printed := putBool(copy[1] == \"same\");\n"
            "    printed := putString(copy[1]);\n"
            "    printed := show(copy);\n"
            "end program.\n",
            encoding="utf-8",
        )
        string_array_emit = run_compiler(
            "--emit-c", str(string_array_output), str(string_array_source)
        )
        check(string_array_emit.returncode == 0,
              f"Stage6D1 String array emit failed: {string_array_emit.stderr!r}")
        check_flat_array_c(string_array_output.read_text(encoding="utf-8"), "String arrays")
        strict_c11_compile_and_run(string_array_output, "\ntrue\nsame\n\nsame\n")

        oob_source = root / "stage6d1-oob-before-rhs.src"
        oob_output = root / "stage6d1-oob-before-rhs.c"
        oob_source.write_text(
            "program Stage6D1OOB is\n"
            "variable values : integer[5];\n"
            "begin\n"
            "    values[6] := putInteger(99);\n"
            "end program.\n",
            encoding="utf-8",
        )
        oob_emit = run_compiler("--emit-c", str(oob_output), str(oob_source))
        check(oob_emit.returncode == 0,
              f"Stage6D1 OOB emit failed: {oob_emit.stderr!r}")
        strict_c11_compile_and_run(oob_output, "", expected_returncode=1)

        large_array_source = root / "stage6d1-large-array.src"
        large_array_output = root / "stage6d1-large-array.c"
        large_array_source.write_text(
            "program Stage6D1LargeArray is\n"
            "variable values : integer[16777215];\n"
            "variable printed : bool;\n"
            "begin\n"
            "    values[16777215] := 7;\n"
            "    printed := putInteger(values[16777215]);\n"
            "end program.\n",
            encoding="utf-8",
        )
        large_array_emit = run_compiler(
            "--emit-c", str(large_array_output), str(large_array_source)
        )
        check(large_array_emit.returncode != 0 and
              "codegen: unsupported" in large_array_emit.stderr,
              "Stage6D1 reachable over-capacity global was not rejected atomically")
        check(not large_array_output.exists(),
              "Stage6D1 over-capacity global published partial output")

        compact_array_source = root / "stage6d1-compact-array.src"
        compact_array_output = root / "stage6d1-compact-array.c"
        compact_array_source.write_text(
            "program Stage6D1CompactArray is\n"
            "variable values : integer[1000000];\n"
            "variable printed : bool;\n"
            "begin\n"
            "    values[1000000] := 7;\n"
            "    printed := putInteger(values[1000000]);\n"
            "end program.\n",
            encoding="utf-8",
        )
        compact_array_emit = run_compiler(
            "--emit-c", str(compact_array_output), str(compact_array_source)
        )
        check(compact_array_emit.returncode == 0,
              f"Stage6D1 compact large array emit failed: {compact_array_emit.stderr!r}")
        compact_array_c = compact_array_output.read_text(encoding="utf-8")
        check(len(compact_array_c) < 20000,
              "Stage6D1 generated source grew proportionally with a large bound")
        strict_c11_compile_and_run(compact_array_output, "7\n")

        dead_huge_source = root / "stage6d1-dead-huge-array.src"
        dead_huge_output = root / "stage6d1-dead-huge-array.c"
        dead_huge_source.write_text(
            "program Stage6D1DeadHuge is\n"
            "variable answer : integer;\n"
            "procedure hidden : integer()\n"
            "variable huge : integer[16777215];\n"
            "begin\n"
            "    return 1;\n"
            "end procedure;\n"
            "begin\n"
            "    answer := 7;\n"
            "end program.\n",
            encoding="utf-8",
        )
        dead_huge_emit = run_compiler(
            "--emit-c", str(dead_huge_output), str(dead_huge_source)
        )
        check(dead_huge_emit.returncode == 0,
              f"Stage6D1 dead huge array was not pruned: {dead_huge_emit.stderr!r}")
        dead_huge_c = dead_huge_output.read_text(encoding="utf-8")
        check("L_f10_" not in dead_huge_c and "16777216" not in dead_huge_c,
              "Stage6D1 dead huge array contributed layout or labels")
        strict_c11_syntax_check(dead_huge_output)

        reachable_huge_source = root / "stage6d1-reachable-huge-array.src"
        reachable_huge_output = root / "stage6d1-reachable-huge-array.c"
        reachable_huge_source.write_text(
            "program Stage6D1ReachableHuge is\n"
            "variable answer : integer;\n"
            "procedure huge : integer()\n"
            "variable values : integer[16777215];\n"
            "begin\n"
            "    return 1;\n"
            "end procedure;\n"
            "begin\n"
            "    answer := huge();\n"
            "end program.\n",
            encoding="utf-8",
        )
        reachable_huge_output.write_text("preserve huge output\n", encoding="utf-8")
        reachable_huge_emit = run_compiler(
            "--emit-c", str(reachable_huge_output), str(reachable_huge_source)
        )
        check(reachable_huge_emit.returncode != 0 and
              "codegen: unsupported" in reachable_huge_emit.stderr,
              "Stage6D1 reachable huge frame was not Unsupported")
        check(reachable_huge_output.read_text(encoding="utf-8") == "preserve huge output\n",
              "Stage6D1 reachable huge frame replaced its output sentinel")

        recursive_frame_source = root / "stage6d1-recursive-array-frame.src"
        recursive_frame_output = root / "stage6d1-recursive-array-frame.c"
        recursive_frame_source.write_text(
            "program Stage6D1RecursiveArrayFrame is\n"
            "variable seed : integer[0];\n"
            "variable answer : integer;\n"
            "procedure descend : integer(variable input : integer[0])\n"
            "variable pressure : integer[1048575];\n"
            "begin\n"
            "    return descend(input);\n"
            "end procedure;\n"
            "begin\n"
            "    answer := descend(seed);\n"
            "end program.\n",
            encoding="utf-8",
        )
        recursive_frame_emit = run_compiler(
            "--emit-c", str(recursive_frame_output), str(recursive_frame_source)
        )
        check(recursive_frame_emit.returncode == 0,
              f"Stage6D1 recursive frame emit failed: {recursive_frame_emit.stderr!r}")
        recursive_frame_c = recursive_frame_output.read_text(encoding="utf-8")
        check("goto L_f0_s0;" in recursive_frame_c and "L_f0_s0:" in recursive_frame_c,
              "Stage6D1 recursive frame omitted controlled capacity failure")
        check_flat_array_c(recursive_frame_c, "recursive array frames")
        strict_c11_compile_and_run(
            recursive_frame_output, "", expected_returncode=1, timeout_sec=10.0
        )

        lifted_only_source = root / "stage6d2-lifted-only.src"
        lifted_only_output = root / "stage6d2-lifted-only.c"
        lifted_only_source.write_text(
            "program Stage6D2LiftedOnly is\n"
            "variable left : integer[2];\n"
            "variable right : integer[2];\n"
            "variable result : integer[2];\n"
            "begin\n"
            "    result := left + right * 2;\n"
            "    result := -result;\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_only_emit = run_compiler(
            "--emit-c", str(lifted_only_output), str(lifted_only_source)
        )
        check(lifted_only_emit.returncode == 0,
              f"Stage6D2 lifted-only emit failed: {lifted_only_emit.stderr!r}")
        lifted_only_c = lifted_only_output.read_text(encoding="utf-8")
        check("L_f0_s0:" not in lifted_only_c,
              "Stage6D2 lifted-only program emitted an unreferenced failure label")
        check("for (" not in lifted_only_c and "while (" not in lifted_only_c,
              "Stage6D2 lifted-only program emitted a C loop")
        check_flat_array_c(lifted_only_c, "lifted-only arrays")
        strict_c11_syntax_check(lifted_only_output)
        strict_c11_compile_and_run(lifted_only_output, "")

        lifted_matrix_source = root / "stage6d2-lifted-matrix.src"
        lifted_matrix_output = root / "stage6d2-lifted-matrix.c"
        lifted_matrix_source.write_text(
            "program Stage6D2LiftedMatrix is\n"
            "variable ints : integer[2];\n"
            "variable other : integer[2];\n"
            "variable out : integer[2];\n"
            "variable floats : float[2];\n"
            "variable float_other : float[2];\n"
            "variable float_out : float[2];\n"
            "variable bools : bool[2];\n"
            "variable bool_other : bool[2];\n"
            "variable relations : bool[2];\n"
            "variable strings : string[2];\n"
            "variable text_other : string[2];\n"
            "variable printed : bool;\n"
            "begin\n"
            "    ints[0] := 10;\n"
            "    ints[1] := -2147483647 - 1;\n"
            "    ints[2] := 6;\n"
            "    other[0] := 3;\n"
            "    other[1] := -1;\n"
            "    other[2] := 2;\n"
            "    out := ints + other;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := ints - 2;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := 20 - ints;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := ints * other;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := ints / other;\n"
            "    printed := putInteger(out[0]);\n"
            "    printed := putInteger(out[1]);\n"
            "    printed := putInteger(out[2]);\n"
            "    out := not ints;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := -ints;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := (ints + 1) * 2;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := ints & 6;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := 1 | ints;\n"
            "    printed := putInteger(out[0]);\n"
            "    out := ints + 1;\n"
            "    ints[0] := 99;\n"
            "    printed := putInteger(out[0]);\n"
            "    ints := ints + 1;\n"
            "    printed := putInteger(ints[0]);\n"
            "    relations := ints < other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := ints <= other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := ints > other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := ints >= other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := ints == other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := ints != other;\n"
            "    printed := putBool(relations[0]);\n"
            "    bools[0] := true;\n"
            "    bools[1] := false;\n"
            "    bools[2] := true;\n"
            "    bool_other[0] := false;\n"
            "    bool_other[1] := true;\n"
            "    bool_other[2] := true;\n"
            "    relations := not bools;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := bools & bool_other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := bools | bool_other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := bools < true;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := bools == true;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := bools == ints;\n"
            "    printed := putBool(relations[0]);\n"
            "    floats[0] := 1.5;\n"
            "    floats[1] := 0.0;\n"
            "    floats[2] := -0.0;\n"
            "    float_other[0] := 2.0;\n"
            "    float_other[1] := 0.0;\n"
            "    float_other[2] := 0.0;\n"
            "    float_out := floats + float_other;\n"
            "    printed := putFloat(float_out[0]);\n"
            "    float_out := 2.0 - floats;\n"
            "    printed := putFloat(float_out[0]);\n"
            "    float_out := floats * 2.0;\n"
            "    printed := putFloat(float_out[0]);\n"
            "    float_out := floats / 2.0;\n"
            "    printed := putFloat(float_out[0]);\n"
            "    float_out := -floats;\n"
            "    printed := putFloat(float_out[0]);\n"
            "    relations := floats < float_other;\n"
            "    printed := putBool(relations[0]);\n"
            "    relations := floats == float_other;\n"
            "    printed := putBool(relations[0]);\n"
            "    float_out := float_other / float_other;\n"
            "    relations := float_out == float_out;\n"
            "    printed := putBool(relations[1]);\n"
            "    relations := float_out != float_out;\n"
            "    printed := putBool(relations[1]);\n"
            "    strings[0] := getString();\n"
            "    strings[1] := \"same\";\n"
            "    strings[2] := \"other\";\n"
            "    text_other[0] := \"same\";\n"
            "    text_other[1] := \"different\";\n"
            "    text_other[2] := \"other\";\n"
            "    relations := strings == text_other;\n"
            "    printed := putBool(relations[0]);\n"
            "    printed := putBool(relations[1]);\n"
            "    printed := putBool(relations[2]);\n"
            "    relations := strings == \"same\";\n"
            "    printed := putBool(relations[0]);\n"
            "    printed := putBool(relations[1]);\n"
            "    printed := putBool(relations[2]);\n"
            "    relations := \"same\" != strings;\n"
            "    printed := putBool(relations[0]);\n"
            "    printed := putBool(relations[2]);\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_matrix_emit = run_compiler(
            "--emit-c", str(lifted_matrix_output), str(lifted_matrix_source)
        )
        check(lifted_matrix_emit.returncode == 0,
              f"Stage6D2 lifted matrix emit failed: {lifted_matrix_emit.stdout!r} "
              f"{lifted_matrix_emit.stderr!r}")
        lifted_matrix_c = lifted_matrix_output.read_text(encoding="utf-8")
        check_flat_array_c(lifted_matrix_c, "lifted matrix")
        check("R_str_eq" in lifted_matrix_c and "R_word_f32" in lifted_matrix_c and
              "R_f32_word" in lifted_matrix_c,
              "Stage6D2 lifted matrix omitted reachable typed helpers")
        strict_c11_compile_and_run(
            lifted_matrix_output,
            "13\n8\n10\n30\n3\n-2147483648\n3\n-11\n-10\n22\n2\n11\n11\n100\n"
            "false\nfalse\ntrue\ntrue\nfalse\ntrue\n"
            "false\nfalse\ntrue\nfalse\ntrue\nfalse\n"
            "3.5\n0.5\n3\n0.75\n-1.5\ntrue\nfalse\nfalse\ntrue\n"
            "true\nfalse\ntrue\ntrue\ntrue\nfalse\nfalse\ntrue\n",
            stdin_text="same\n",
        )

        lifted_order_source = root / "stage6d2-lifted-order.src"
        lifted_order_output = root / "stage6d2-lifted-order.c"
        lifted_order_source.write_text(
            "program Stage6D2LiftedOrder is\n"
            "variable source : integer[2];\n"
            "variable destination : integer[2];\n"
            "variable counter : integer;\n"
            "variable index : integer;\n"
            "variable answer : integer;\n"
            "variable printed : bool;\n"
            "procedure mutate : integer()\n"
            "begin\n"
            "    counter := counter + 1;\n"
            "    source[0] := source[0] + 40;\n"
            "    return counter * 10;\n"
            "end procedure;\n"
            "procedure recurse : integer(variable input : integer[2], variable n : integer)\n"
            "variable adjusted : integer[2];\n"
            "begin\n"
            "    adjusted := input + n;\n"
            "    if (n == 0) then\n"
            "        return adjusted[0];\n"
            "    else\n"
            "        return recurse(adjusted, n - 1);\n"
            "    end if;\n"
            "end procedure;\n"
            "begin\n"
            "    source[0] := 1;\n"
            "    source[1] := 2;\n"
            "    source[2] := 3;\n"
            "    counter := 0;\n"
            "    destination := source + mutate();\n"
            "    printed := putInteger(destination[0]);\n"
            "    printed := putInteger(source[0]);\n"
            "    printed := putInteger(counter);\n"
            "    destination := mutate() + source;\n"
            "    printed := putInteger(destination[0]);\n"
            "    printed := putInteger(source[0]);\n"
            "    printed := putInteger(counter);\n"
            "    source := source + 1;\n"
            "    printed := putInteger(source[0]);\n"
            "    source[0] := 1;\n"
            "    answer := recurse(source, 3);\n"
            "    printed := putInteger(answer);\n"
            "    printed := putInteger(source[0]);\n"
            "    for (index := 0; index < 3)\n"
            "        destination := source + index;\n"
            "        printed := putInteger(destination[0]);\n"
            "        index := index + 1;\n"
            "    end for;\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_order_emit = run_compiler(
            "--emit-c", str(lifted_order_output), str(lifted_order_source)
        )
        check(lifted_order_emit.returncode == 0,
              f"Stage6D2 order/recursion emit failed: {lifted_order_emit.stderr!r}")
        lifted_order_c = lifted_order_output.read_text(encoding="utf-8")
        check_flat_array_c(lifted_order_c, "lifted order and recursion")
        strict_c11_compile_and_run(
            lifted_order_output, "11\n41\n1\n101\n81\n2\n82\n7\n1\n1\n2\n3\n"
        )

        lifted_divide_source = root / "stage6d2-lifted-divide-failure.src"
        lifted_divide_output = root / "stage6d2-lifted-divide-failure.c"
        lifted_divide_source.write_text(
            "program Stage6D2LiftedDivideFailure is\n"
            "variable dividend : integer[2];\n"
            "variable divisor : integer[2];\n"
            "variable destination : integer[2];\n"
            "variable printed : bool;\n"
            "procedure side : integer()\n"
            "begin\n"
            "    printed := putInteger(destination[0]);\n"
            "    return 0;\n"
            "end procedure;\n"
            "begin\n"
            "    dividend[0] := 8;\n"
            "    dividend[1] := 8;\n"
            "    dividend[2] := 8;\n"
            "    divisor[0] := 2;\n"
            "    divisor[1] := 0;\n"
            "    divisor[2] := 2;\n"
            "    destination[0] := 9;\n"
            "    destination[1] := 9;\n"
            "    destination[2] := 9;\n"
            "    destination := dividend / (divisor + side());\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_divide_emit = run_compiler(
            "--emit-c", str(lifted_divide_output), str(lifted_divide_source)
        )
        check(lifted_divide_emit.returncode == 0,
              f"Stage6D2 divide failure emit failed: {lifted_divide_emit.stderr!r}")
        lifted_divide_c = lifted_divide_output.read_text(encoding="utf-8")
        check_flat_array_c(lifted_divide_c, "lifted division failure")
        strict_c11_compile_and_run(
            lifted_divide_output, "9\n", expected_returncode=1
        )

        lifted_compact_source = root / "stage6d2-lifted-compact.src"
        lifted_compact_output = root / "stage6d2-lifted-compact.c"
        lifted_compact_source.write_text(
            "program Stage6D2LiftedCompact is\n"
            "variable source : integer[100000];\n"
            "variable result : integer[100000];\n"
            "begin\n"
            "    result := source + 1;\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_compact_emit = run_compiler(
            "--emit-c", str(lifted_compact_output), str(lifted_compact_source)
        )
        check(lifted_compact_emit.returncode == 0,
              f"Stage6D2 compact lift emit failed: {lifted_compact_emit.stderr!r}")
        lifted_compact_c = lifted_compact_output.read_text(encoding="utf-8")
        check(len(lifted_compact_c) < 20000,
              "Stage6D2 lifted source grew proportionally with its bound")
        check("L_f0_s0:" not in lifted_compact_c,
              "Stage6D2 compact lift emitted an unused failure label")
        check_flat_array_c(lifted_compact_c, "compact lifted array")
        strict_c11_compile_and_run(lifted_compact_output, "")

        lifted_capacity_source = root / "stage6d2-lifted-capacity.src"
        lifted_capacity_output = root / "stage6d2-lifted-capacity.c"
        lifted_capacity_source.write_text(
            "program Stage6D2LiftedCapacity is\n"
            "variable left : integer[4000000];\n"
            "variable right : integer[4000000];\n"
            "begin\n"
            "    left := left + right;\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_capacity_output.write_text("preserve lifted capacity\n", encoding="utf-8")
        lifted_capacity_emit = run_compiler(
            "--emit-c", str(lifted_capacity_output), str(lifted_capacity_source)
        )
        check(lifted_capacity_emit.returncode != 0 and
              "codegen: unsupported" in lifted_capacity_emit.stderr,
              "Stage6D2 over-capacity lifted temporaries were not Unsupported")
        check(lifted_capacity_output.read_text(encoding="utf-8") ==
              "preserve lifted capacity\n",
              "Stage6D2 over-capacity lift replaced its output sentinel")

        dead_lifted_source = root / "stage6d2-dead-lifted.src"
        dead_lifted_output = root / "stage6d2-dead-lifted.c"
        dead_lifted_source.write_text(
            "program Stage6D2DeadLifted is\n"
            "variable answer : integer;\n"
            "procedure hidden : integer()\n"
            "variable huge : float[16777215];\n"
            "begin\n"
            "    huge := -huge + 1.0;\n"
            "    return 1;\n"
            "end procedure;\n"
            "begin\n"
            "    answer := 7;\n"
            "end program.\n",
            encoding="utf-8",
        )
        dead_lifted_emit = run_compiler(
            "--emit-c", str(dead_lifted_output), str(dead_lifted_source)
        )
        check(dead_lifted_emit.returncode == 0,
              f"Stage6D2 dead lifted procedure was not pruned: {dead_lifted_emit.stderr!r}")
        dead_lifted_c = dead_lifted_output.read_text(encoding="utf-8")
        check("L_f10_" not in dead_lifted_c and "16777216" not in dead_lifted_c and
              "R_word_f32" not in dead_lifted_c and "R_f32_word" not in dead_lifted_c,
              "Stage6D2 dead lift contributed labels, layout, or Float helpers")
        strict_c11_syntax_check(dead_lifted_output)

        ty8_arrays_source = REPO_ROOT / "docs/audit/probes/typechecker/ty8_valid_arrays.src"
        ty8_arrays_output = root / "stage6d2-ty8-valid-arrays.c"
        ty8_arrays_emit = run_compiler(
            "--emit-c", str(ty8_arrays_output), str(ty8_arrays_source)
        )
        check(ty8_arrays_emit.returncode == 0,
              f"Stage6D2 TY-8 array probe emit failed: {ty8_arrays_emit.stderr!r}")
        ty8_arrays_c = ty8_arrays_output.read_text(encoding="utf-8")
        check_flat_array_c(ty8_arrays_c, "TY-8 valid array probe")
        strict_c11_compile_and_run(ty8_arrays_output, "", expected_returncode=1)

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

        formerly_unsupported_source = root / "stage6d2-formerly-unsupported.src"
        formerly_unsupported_output = root / "stage6d2-formerly-unsupported.c"
        formerly_unsupported_source.write_text(
            "program Stage6D2FormerlyUnsupported is\n"
            "variable values : integer[0];\n"
            "begin\n"
            "    values := values + values;\n"
            "end program.\n",
            encoding="utf-8",
        )
        formerly_unsupported = run_compiler(
            "--emit-c", str(formerly_unsupported_output), str(formerly_unsupported_source)
        )
        check(formerly_unsupported.returncode == 0,
              f"Stage6D2 formerly unsupported lift failed: {formerly_unsupported.stderr!r}")
        formerly_unsupported_c = formerly_unsupported_output.read_text(encoding="utf-8")
        check("L_f0_s0:" not in formerly_unsupported_c,
              "Stage6D2 formerly unsupported lift emitted a failure label")
        check_flat_array_c(formerly_unsupported_c, "formerly unsupported lift")
        strict_c11_compile_and_run(formerly_unsupported_output, "")

        lifted_array_source = root / "stage6d2-lifted-array.src"
        lifted_array_output = root / "stage6d2-lifted-array.c"
        lifted_array_source.write_text(
            "program Stage6D2LiftedArray is\n"
            "variable values : integer[1];\n"
            "variable other : integer[1];\n"
            "begin\n"
            "    values := values + other;\n"
            "end program.\n",
            encoding="utf-8",
        )
        lifted_array = run_compiler(
            "--emit-c", str(lifted_array_output), str(lifted_array_source)
        )
        check(lifted_array.returncode == 0,
              f"Stage6D2 lifted array failed: {lifted_array.stderr!r}")
        lifted_array_c = lifted_array_output.read_text(encoding="utf-8")
        check_flat_array_c(lifted_array_c, "Stage6D2 lifted array")
        strict_c11_compile_and_run(lifted_array_output, "")

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
