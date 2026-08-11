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


def strict_c11_compile_and_run(
    generated: Path, expected_stdout: str, expected_returncode: int = 0
) -> None:
    c_compiler = next(
        (candidate for candidate in ("cc", "gcc", "clang") if shutil.which(candidate)), None
    )
    if c_compiler is None:
        print("SKIP test_generated_c.py: no C11 compiler available for Gate4 runtime check")
        return
    executable = generated.with_suffix(".native")
    compilation = subprocess.run(
        [
            c_compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic-errors",
            str(generated),
            "-o",
            str(executable),
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
        compilation.returncode == 0,
        f"Gate4 C compilation failed: stdout={compilation.stdout!r} stderr={compilation.stderr!r}",
    )
    execution = subprocess.run(
        [str(executable)],
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=TIMEOUT_SEC,
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
            "variable values : integer[0];\n"
            "begin\n"
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
