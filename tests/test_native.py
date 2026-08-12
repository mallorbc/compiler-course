#!/usr/bin/env python3
"""Stage 7 process tests for safe native-executable publication."""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
COMPILER = REPO_ROOT / "compiler"
TIMEOUT_SEC = 15.0


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_compiler(
    *arguments: str,
    env: dict[str, str] | None = None,
    unset_env: tuple[str, ...] = (),
) -> subprocess.CompletedProcess[str]:
    process_env = os.environ.copy()
    for name in unset_env:
        process_env.pop(name, None)
    if env:
        process_env.update(env)
    return subprocess.run(
        [str(COMPILER), *arguments],
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=TIMEOUT_SEC,
        check=False,
        env=process_env,
    )


def run_native(
    executable: Path, stdin_text: str = "", timeout: float = TIMEOUT_SEC
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(executable)],
        cwd=executable.parent,
        input=stdin_text,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )


def write_source(path: Path, body: str) -> None:
    path.write_text(body, encoding="utf-8")


def no_temp_debris(directory: Path) -> None:
    debris = list(directory.rglob(".compiler-native-tmp-*"))
    check(not debris, f"native temporary debris remains: {debris!r}")


def install_sentinel(path: Path) -> tuple[bytes, int, int]:
    contents = b"preserve existing executable\n"
    path.write_bytes(contents)
    path.chmod(0o751)
    status = path.stat()
    return contents, stat.S_IMODE(status.st_mode), status.st_ino


def check_sentinel(path: Path, expected: tuple[bytes, int, int], context: str) -> None:
    contents, mode, inode = expected
    status = path.stat()
    check(path.read_bytes() == contents, f"{context} changed output bytes")
    check(stat.S_IMODE(status.st_mode) == mode, f"{context} changed output permissions")
    check(status.st_ino == inode, f"{context} replaced the stable output inode")


def make_fake_compiler(root: Path) -> Path:
    fake = root / "fake cc ; literal"
    fake.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, signal, sys\n"
        "capture = os.environ.get('FAKE_CC_CAPTURE')\n"
        "if capture:\n"
        "    capture_path = pathlib.Path(capture)\n"
        "    capture_path.write_text(json.dumps(sys.argv), encoding='utf-8')\n"
        "    capture_path.chmod(0o600)\n"
        "copy = os.environ.get('FAKE_CC_SOURCE_COPY')\n"
        "if copy and len(sys.argv) > 2:\n"
        "    copy_path = pathlib.Path(copy)\n"
        "    copy_path.write_bytes(pathlib.Path(sys.argv[2]).read_bytes())\n"
        "    copy_path.chmod(0o600)\n"
        "metadata = os.environ.get('FAKE_CC_METADATA')\n"
        "if metadata and len(sys.argv) > 2:\n"
        "    source = pathlib.Path(sys.argv[2])\n"
        "    details = {'source_mode': source.stat().st_mode & 0o777, "
        "'directory_mode': source.parent.stat().st_mode & 0o777, "
        "'stdin': os.read(0, 1).decode('latin1')}\n"
        "    metadata_path = pathlib.Path(metadata)\n"
        "    metadata_path.write_text(json.dumps(details), encoding='utf-8')\n"
        "    metadata_path.chmod(0o600)\n"
        "mode = os.environ.get('FAKE_CC_MODE', 'success')\n"
        "if mode == 'fail':\n"
        "    raise SystemExit(7)\n"
        "if mode == 'signal':\n"
        "    os.kill(os.getpid(), signal.SIGTERM)\n"
        "if mode == 'no-output':\n"
        "    raise SystemExit(0)\n"
        "if mode == 'stdio':\n"
        "    print('fake compiler stdout', flush=True)\n"
        "    print('fake compiler stderr', file=sys.stderr, flush=True)\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "if mode == 'directory':\n"
        "    out.mkdir()\n"
        "elif mode == 'symlink':\n"
        "    out.symlink_to(pathlib.Path(os.environ['FAKE_CC_PRODUCT_TARGET']))\n"
        "elif mode == 'hardlink':\n"
        "    os.link(os.environ['FAKE_CC_PRODUCT_TARGET'], out)\n"
        "elif mode == 'fifo':\n"
        "    os.mkfifo(out)\n"
        "else:\n"
        "    out.write_bytes(b'' if mode == 'empty' else b'#!/bin/sh\\nexit 0\\n')\n"
        "    if mode != 'nonexec':\n"
        "        out.chmod(0o700)\n"
        "if mode == 'race-symlink':\n"
        "    pathlib.Path(os.environ['FAKE_CC_FINAL_PATH']).symlink_to(\n"
        "        pathlib.Path(os.environ['FAKE_CC_PRODUCT_TARGET']))\n"
        "elif mode == 'race-directory':\n"
        "    pathlib.Path(os.environ['FAKE_CC_FINAL_PATH']).mkdir()\n",
        encoding="utf-8",
    )
    fake.chmod(0o700)
    return fake


SIMPLE_SOURCE = (
    "program NativeStraight is\n"
    "variable printed : bool;\n"
    "begin\n"
    "    printed := putInteger(42);\n"
    "end program.\n"
)

MATH_SOURCE = (
    "program NativeMath is\n"
    "variable printed : bool;\n"
    "begin\n"
    "    printed := putFloat(sqrt(9));\n"
    "end program.\n"
)

DEAD_MATH_SOURCE = (
    "program NativeDeadMath is\n"
    "variable printed : bool;\n"
    "procedure unused : float()\n"
    "begin\n"
    "    return sqrt(9);\n"
    "end procedure;\n"
    "begin\n"
    "    printed := putInteger(1);\n"
    "end program.\n"
)


def test_fake_compiler_and_failures(root: Path) -> None:
    source = root / "fake-source.src"
    write_source(source, SIMPLE_SOURCE)
    math_source = root / "fake-math.src"
    write_source(math_source, MATH_SOURCE)
    dead_math_source = root / "fake-dead-math.src"
    write_source(dead_math_source, DEAD_MATH_SOURCE)
    fake = make_fake_compiler(root)
    capture = root / "argv.json"
    source_copy = root / "captured-source.c"
    metadata_capture = root / "metadata.json"
    output = root / "fake-native"
    base_env = {
        "CC": str(fake),
        "FAKE_CC_CAPTURE": str(capture),
        "FAKE_CC_SOURCE_COPY": str(source_copy),
        "FAKE_CC_METADATA": str(metadata_capture),
        "CFLAGS": "--not-a-real-flag ; touch ignored-cflags",
        "LDFLAGS": "--also-ignored",
    }

    for option in ("-o", "--output", "--native"):
        for arguments in ((option,), (option, str(output)),
                          (option, str(output), str(source), "extra")):
            capture.unlink(missing_ok=True)
            malformed = run_compiler(*arguments, env=base_env)
            check(malformed.returncode == 1,
                  f"malformed {arguments!r} returned {malformed.returncode}")
            check(malformed.stdout ==
                  f"Error!\nUsage: {COMPILER} <file to compile>\n" and
                  malformed.stderr == "",
                  f"malformed {arguments!r} changed usage: {malformed!r}")
            check(not capture.exists(), f"malformed {arguments!r} launched CC")

    previous_umask = os.umask(0o777)
    try:
        built = run_compiler("--output", str(output), str(source), env=base_env)
    finally:
        os.umask(previous_umask)
    check(built.returncode == 0, f"fake native compile failed: {built.stderr!r}")
    argv = json.loads(capture.read_text(encoding="utf-8"))
    check(argv[0] == str(fake), f"CC executable changed: {argv!r}")
    check(argv[1] == "-std=c11", f"missing exact language option: {argv!r}")
    check(argv[2].startswith(str(root.resolve())) and argv[2].endswith("/source.c"),
          f"temporary C source is not an absolute sibling path: {argv!r}")
    check(argv[3] == "-o" and argv[4].endswith("/program"),
          f"temporary executable argv is malformed: {argv!r}")
    check(Path(argv[4]).is_absolute(), f"temporary executable is not absolute: {argv!r}")
    check(len(argv) == 5 and "-lm" not in argv, f"unexpected nonmath flags: {argv!r}")
    check(str(source) not in argv and str(output) not in argv,
          f"language source or final output leaked to CC: {argv!r}")
    metadata = json.loads(metadata_capture.read_text(encoding="utf-8"))
    check(metadata == {"source_mode": 0o600, "directory_mode": 0o700, "stdin": ""},
          f"temporary ownership or child stdin contract changed: {metadata!r}")
    check("int main(void)" in source_copy.read_text(encoding="utf-8"),
          "fake compiler did not receive emitted C")
    check(output.is_file() and os.access(output, os.X_OK), "fake product was not published")
    check(not (root / "ignored-cflags").exists(), "CFLAGS was interpreted")

    stdio_output = root / "fake-stdio-native"
    stdio = run_compiler(
        "-o", str(stdio_output), str(source),
        env={**base_env, "FAKE_CC_MODE": "stdio"},
    )
    check(stdio.returncode == 0, f"fake stdio compile failed: {stdio.stderr!r}")
    check("fake compiler stdout\n" in stdio.stdout,
          f"child stdout was not inherited: {stdio.stdout!r}")
    check("fake compiler stderr\n" in stdio.stderr,
          f"child stderr was not inherited: {stdio.stderr!r}")

    math_output = root / "fake-math-native"
    math = run_compiler("-o", str(math_output), str(math_source), env=base_env)
    check(math.returncode == 0, f"fake math native compile failed: {math.stderr!r}")
    math_argv = json.loads(capture.read_text(encoding="utf-8"))
    check(math_argv[-1] == "-lm" and math_argv.count("-lm") == 1,
          f"math link metadata was not one trailing -lm: {math_argv!r}")

    dead_output = root / "fake-dead-math-native"
    dead = run_compiler("--native", str(dead_output), str(dead_math_source), env=base_env)
    check(dead.returncode == 0, f"dead math compile failed: {dead.stderr!r}")
    dead_argv = json.loads(capture.read_text(encoding="utf-8"))
    check("-lm" not in dead_argv, f"dead sqrt leaked link metadata: {dead_argv!r}")

    failure_modes = (
        "fail", "signal", "no-output", "empty", "nonexec", "directory", "fifo"
    )
    for mode in failure_modes:
        sentinel = install_sentinel(output)
        failed = run_compiler(
            "-o", str(output), str(source), env={**base_env, "FAKE_CC_MODE": mode}
        )
        check(failed.returncode != 0, f"fake compiler mode {mode!r} succeeded")
        if mode == "signal":
            check(failed.stderr ==
                  "native: compiler-error: host C compiler terminated by signal 15\n",
                  f"signaled compiler diagnostic changed: {failed.stderr!r}")
        check_sentinel(output, sentinel, f"fake compiler mode {mode!r}")
        no_temp_debris(root)

    external_target = root / "fake-product-target"
    external_target.write_bytes(b"external target\n")
    for mode in ("symlink", "hardlink"):
        sentinel = install_sentinel(output)
        failed = run_compiler(
            "-o", str(output), str(source),
            env={**base_env, "FAKE_CC_MODE": mode,
                 "FAKE_CC_PRODUCT_TARGET": str(external_target)},
        )
        check(failed.returncode != 0, f"fake {mode} product succeeded")
        check_sentinel(output, sentinel, f"fake {mode} product")
        check(external_target.read_bytes() == b"external target\n",
              f"fake {mode} altered external target")
        no_temp_debris(root)

    sentinel = install_sentinel(output)
    missing = run_compiler(
        "-o", str(output), str(source), env={"CC": str(root / "missing-cc")}
    )
    check(missing.returncode != 0 and "native: launch-error" in missing.stderr,
          f"missing CC was not a launch error: {missing.stderr!r}")
    check_sentinel(output, sentinel, "missing CC")

    injection_marker = root / "cc-shell-injection"
    injected = run_compiler(
        "-o", str(output), str(source),
        env={"CC": f"{fake} ; touch {injection_marker}"},
    )
    check(injected.returncode != 0 and not injection_marker.exists(),
          "CC was split or interpreted by a shell")
    check_sentinel(output, sentinel, "hostile CC")

    compiler_alias = root / "compiler-alias-output"
    os.link(fake, compiler_alias)
    compiler_alias_status = compiler_alias.stat()
    capture.unlink(missing_ok=True)
    alias_result = run_compiler(
        "-o", str(compiler_alias), str(source), env=base_env
    )
    check(alias_result.returncode != 0 and "aliases host compiler" in alias_result.stderr,
          f"host compiler hardlink alias was not rejected: {alias_result.stderr!r}")
    check(not capture.exists(), "compiler/output alias launched CC")
    check(compiler_alias.stat().st_ino == compiler_alias_status.st_ino and
          stat.S_IMODE(compiler_alias.stat().st_mode) ==
          stat.S_IMODE(compiler_alias_status.st_mode),
          "compiler/output alias replaced the compiler inode or permissions")
    check(fake.read_text(encoding="utf-8").startswith("#!/usr/bin/env python3"),
          "compiler/output alias changed fake compiler")

    invalid = root / "invalid.src"
    write_source(
        invalid,
        "program InvalidNative is\nvariable x : integer;\nbegin\n"
        '    x := "wrong";\nend program.\n',
    )
    unsupported = root / "unsupported.src"
    write_source(
        unsupported,
        "program UnsupportedNative is\nvariable value : enum{red, green};\n"
        "begin\nend program.\n",
    )
    for bad_source in (invalid, unsupported):
        capture.unlink(missing_ok=True)
        sentinel = install_sentinel(output)
        rejected = run_compiler("-o", str(output), str(bad_source), env=base_env)
        check(rejected.returncode != 0, f"invalid source {bad_source.name} succeeded")
        check(not capture.exists(), f"invalid source {bad_source.name} launched CC")
        check_sentinel(output, sentinel, f"invalid source {bad_source.name}")

        absent_output = root / f"absent-{bad_source.stem}"
        absent_output.unlink(missing_ok=True)
        capture.unlink(missing_ok=True)
        absent = run_compiler("-o", str(absent_output), str(bad_source), env=base_env)
        check(absent.returncode != 0, f"invalid source {bad_source.name} succeeded absent")
        check(not capture.exists(), f"invalid source {bad_source.name} launched CC absent")
        check(not absent_output.exists(),
              f"invalid source {bad_source.name} published an absent destination")

    trailing_cases = (
        ("identifier", " trailing_identifier\n", 'Unexpected token after final "."'),
        ("period", ".\n", 'Unexpected token after final "."'),
        ("punctuation", ";\n", 'Unexpected token after final "."'),
        ("illegal", "@\n", "Illegal character: '@'"),
        ("string", '"unterminated', "quotation left open"),
        ("comment", "/* unterminated", "Unclosed block comment detected"),
    )
    for label, suffix, expected_diagnostic in trailing_cases:
        trailing_source = root / f"native-trailing-{label}.src"
        trailing_output = root / f"native-trailing-{label}"
        write_source(
            trailing_source,
            "program trailing is\nbegin\nend program." + suffix,
        )
        sentinel = install_sentinel(trailing_output)
        capture.unlink(missing_ok=True)
        rejected = run_compiler(
            "-o", str(trailing_output), str(trailing_source), env=base_env
        )
        check(rejected.returncode != 0, f"native trailing {label} succeeded")
        check(expected_diagnostic in rejected.stdout,
              f"native trailing {label} lost its diagnostic: {rejected.stdout!r}")
        check(rejected.stderr == "native: frontend-error\n",
              f"native trailing {label} status changed: {rejected.stderr!r}")
        check(not capture.exists(), f"native trailing {label} launched CC")
        check_sentinel(trailing_output, sentinel, f"native trailing {label}")

        trailing_output.unlink()
        absent = run_compiler(
            "-o", str(trailing_output), str(trailing_source), env=base_env
        )
        check(absent.returncode != 0, f"native trailing {label} absent succeeded")
        check(not capture.exists(), f"native trailing {label} absent launched CC")
        check(not trailing_output.exists(), f"native trailing {label} published output")

    trailing_valid_source = root / "native-trailing-valid.src"
    trailing_valid_output = root / "native-trailing-valid"
    write_source(
        trailing_valid_source,
        "program trailing is\nbegin\nend program.  \t\n"
        "// trailing line comment with punctuation @ \" /*\n"
        "/* trailing block comment /* nested */ closed */\n",
    )
    capture.unlink(missing_ok=True)
    trailing_valid = run_compiler(
        "-o", str(trailing_valid_output), str(trailing_valid_source), env=base_env
    )
    check(trailing_valid.returncode == 0,
          f"valid trailing comments blocked native publication: {trailing_valid.stderr!r}")
    check(capture.exists(), "valid trailing comments did not launch CC")
    check(trailing_valid_output.is_file() and os.access(trailing_valid_output, os.X_OK),
          "valid trailing comments did not publish executable")
    no_temp_debris(root)


def test_destination_safety(root: Path) -> None:
    source = root / "safety-source.src"
    write_source(source, SIMPLE_SOURCE)
    original = source.read_bytes()
    fake = make_fake_compiler(root)
    env = {"CC": str(fake), "FAKE_CC_CAPTURE": str(root / "safety-argv.json")}

    alias = run_compiler("-o", str(source), str(source), env=env)
    check(alias.returncode != 0 and source.read_bytes() == original,
          "source/output alias changed the source")

    hardlink = root / "source-hardlink"
    os.link(source, hardlink)
    linked = run_compiler("-o", str(hardlink), str(source), env=env)
    check(linked.returncode != 0 and source.read_bytes() == original,
          "source/output hardlink alias changed the source")

    directory = root / "output-directory"
    directory.mkdir()
    directory_result = run_compiler("-o", str(directory), str(source), env=env)
    check(directory_result.returncode != 0 and directory.is_dir(),
          "directory output was accepted or changed")

    missing_parent = root / "missing-parent" / "program"
    missing_result = run_compiler("-o", str(missing_parent), str(source), env=env)
    check(missing_result.returncode != 0 and not missing_parent.exists(),
          "missing output parent was created")

    target = root / "symlink-target"
    target.write_bytes(b"preserve target\n")
    direct_link = root / "direct-link"
    direct_link.symlink_to(target)
    direct = run_compiler("-o", str(direct_link), str(source), env=env)
    check(direct.returncode != 0 and direct_link.is_symlink() and
          target.read_bytes() == b"preserve target\n",
          "direct output symlink was followed or replaced")

    dangling = root / "dangling-link"
    dangling.symlink_to(root / "missing-target")
    dangling_result = run_compiler("-o", str(dangling), str(source), env=env)
    check(dangling_result.returncode != 0 and dangling.is_symlink(),
          "dangling output symlink was accepted or replaced")

    fifo = root / "direct-fifo"
    os.mkfifo(fifo)
    capture = Path(env["FAKE_CC_CAPTURE"])
    capture.unlink(missing_ok=True)
    fifo_result = run_compiler("-o", str(fifo), str(source), env=env)
    check(fifo_result.returncode != 0 and
          stat.S_ISFIFO(os.lstat(fifo).st_mode),
          "direct FIFO output was accepted or replaced")
    check(not capture.exists(), "direct FIFO output launched CC")

    race_target = root / "race-symlink-target"
    race_target.write_bytes(b"race target sentinel\n")
    for mode in ("race-symlink", "race-directory"):
        raced_output = root / f"final-{mode}"
        capture.unlink(missing_ok=True)
        raced = run_compiler(
            "-o", str(raced_output), str(source),
            env={**env, "FAKE_CC_MODE": mode,
                 "FAKE_CC_FINAL_PATH": str(raced_output),
                 "FAKE_CC_PRODUCT_TARGET": str(race_target)},
        )
        check(raced.returncode != 0 and "native: io-error" in raced.stderr,
              f"final {mode} race was not rejected: {raced.stderr!r}")
        raced_argv = json.loads(capture.read_text(encoding="utf-8"))
        check(str(raced_output) not in raced_argv,
              f"final {mode} path was passed to CC: {raced_argv!r}")
        check(race_target.read_bytes() == b"race target sentinel\n",
              f"final {mode} race altered its target")
        if mode == "race-symlink":
            check(raced_output.is_symlink(), "final symlink race was replaced")
            raced_output.unlink()
        else:
            check(raced_output.is_dir(), "final directory race was replaced")
            raced_output.rmdir()
        no_temp_debris(root)
    no_temp_debris(root)


def test_real_native_runtime(root: Path) -> None:
    if shutil.which("cc") is None:
        print("SKIP test_native.py real compiler coverage: cc not found")
        return

    hostile_source = root / "- hostile ; Ω\nsource.src"
    hostile_output = root / "- hostile ; Ω\nprogram"
    write_source(hostile_source, SIMPLE_SOURCE)
    straight = run_compiler("-o", str(hostile_output), str(hostile_source), env={"CC": "cc"})
    check(straight.returncode == 0, f"hostile native path failed: {straight.stderr!r}")
    straight_run = run_native(hostile_output)
    check(straight_run.returncode == 0 and straight_run.stdout == "42\n" and
          straight_run.stderr == "", f"straight native output changed: {straight_run!r}")

    for label, configured, unset in (
        ("unset", None, ("CC",)),
        ("empty", {"CC": ""}, ()),
    ):
        default_output = root / f"default-cc-{label}"
        default_build = run_compiler(
            "-o", str(default_output), str(hostile_source),
            env=configured, unset_env=unset,
        )
        check(default_build.returncode == 0,
              f"{label} CC did not default to cc: {default_build.stderr!r}")
        check(run_native(default_output).stdout == "42\n",
              f"{label} CC default produced the wrong executable")

    for option in ("--output", "--native"):
        alias_output = root / f"native-alias-{option[2:]}"
        alias_build = run_compiler(option, str(alias_output), str(hostile_source), env={"CC": "cc"})
        check(alias_build.returncode == 0, f"{option} alias failed: {alias_build.stderr!r}")
        check(run_native(alias_output).stdout == "42\n", f"{option} emitted wrong program")

    runtime_source = root / "runtime.src"
    runtime_output = root / "runtime"
    write_source(
        runtime_source,
        "program NativeRuntime is\n"
        "variable number : integer;\nvariable real : float;\n"
        "variable flag : bool;\nvariable text : string;\nvariable printed : bool;\n"
        "variable numbers : integer[1];\nvariable texts : string[1];\n"
        "begin\n"
        "    text := getString();\n    number := getInteger();\n"
        "    real := getFloat();\n    flag := getBool();\n"
        "    numbers[0] := number;\n    numbers[1] := 2;\n"
        "    numbers := numbers + 1;\n"
        "    texts[0] := text;\n    texts[1] := \"tail\";\n"
        "    printed := putInteger(numbers[0]);\n"
        "    printed := putInteger(numbers[1]);\n"
        "    printed := putFloat(real + 0.5);\n"
        "    printed := putBool(flag);\n"
        "    printed := putString(texts[0]);\n"
        "    printed := putString(texts[1]);\n"
        "    printed := putFloat(sqrt(9));\n"
        "end program.\n",
    )
    runtime_build = run_compiler("-o", str(runtime_output), str(runtime_source), env={"CC": "cc"})
    check(runtime_build.returncode == 0,
          f"all-runtime native compile failed: {runtime_build.stderr!r}")
    runtime = run_native(runtime_output, "hello world\n4 1.5 true\n")
    check(runtime.returncode == 0 and
          runtime.stdout == "5\n3\n2\ntrue\nhello world\ntail\n3\n" and
          runtime.stderr == "", f"native runtime matrix changed: {runtime!r}")

    recursive_source = root / "recursive.src"
    recursive_output = root / "recursive"
    write_source(
        recursive_source,
        "program NativeRecursive is\nvariable printed : bool;\n"
        "procedure sum_down : integer(variable value : integer)\n"
        "begin\n"
        "    if (value == 0) then\n        return 0;\n"
        "    else\n        return value + sum_down(value - 1);\n    end if;\n"
        "end procedure;\n"
        "begin\n    printed := putInteger(sum_down(5));\nend program.\n",
    )
    recursive = run_compiler(
        "-o", str(recursive_output), str(recursive_source), env={"CC": "cc"},
    )
    check(recursive.returncode == 0, f"recursive native compile failed: {recursive.stderr!r}")
    recursive_run = run_native(recursive_output)
    check(recursive_run.returncode == 0 and recursive_run.stdout == "15\n",
          f"recursive native execution changed: {recursive_run!r}")

    bounds_source = root / "bounds.src"
    bounds_output = root / "bounds"
    write_source(
        bounds_source,
        "program NativeBounds is\nvariable values : integer[0];\n"
        "variable printed : bool;\nbegin\n"
        "    values[0] := 7;\n    printed := putInteger(values[0]);\n"
        "    values[1] := 9;\n    printed := putInteger(99);\nend program.\n",
    )
    bounds_build = run_compiler("-o", str(bounds_output), str(bounds_source), env={"CC": "cc"})
    check(bounds_build.returncode == 0, f"bounds native compile failed: {bounds_build.stderr!r}")
    bounds_run = run_native(bounds_output)
    check(bounds_run.returncode == 1 and bounds_run.stdout == "7\n" and bounds_run.stderr == "",
          f"native bounds trap changed: {bounds_run!r}")

    professor_cases = (
        ("iterativeFib.src", "5\n", "0\n1\n1\n2\n3\n", 0),
        ("logicals.src", "true\nA\n", "T\nT\n", 0),
        ("math.src", "", "1\n", 0),
        ("multipleProcs.src", "", "8\n", 0),
        ("recursiveFib.src", "3\n", "0\n1\n", 1),
        ("source.src", "", "", 0),
        ("test2.src", "", "0\n", 0),
        (
            "test_heap.src",
            "alpha\nbeta two\ngamma\ndelta\n",
            "Enter a string:\nEnter a string:\nEnter a string:\nEnter a string:\n"
            "delta\ngamma\nbeta two\nalpha\n",
            0,
        ),
        ("test_program_minimal.src", "", "0\n", 0),
    )
    for file_name, stdin_text, expected_stdout, expected_returncode in professor_cases:
        professor_source = REPO_ROOT / "testPgms" / "correct" / file_name
        professor_output = root / f"professor-{Path(file_name).stem}"
        built = run_compiler(
            "-o", str(professor_output), str(professor_source), env={"CC": "cc"}
        )
        check(built.returncode == 0,
              f"professor source {file_name} did not publish: {built.stderr!r}")
        check(professor_output.is_file() and os.access(professor_output, os.X_OK),
              f"professor source {file_name} product is not executable")
        executed = run_native(professor_output, stdin_text)
        check(executed.returncode == expected_returncode and
              executed.stdout == expected_stdout and executed.stderr == "",
              f"professor source {file_name} runtime changed: {executed!r}")

    for file_name in ("test1.src", "test1b.src"):
        professor_source = REPO_ROOT / "testPgms" / "correct" / file_name
        professor_output = root / f"professor-rejected-{Path(file_name).stem}"
        rejected = run_compiler(
            "-o", str(professor_output), str(professor_source), env={"CC": "cc"}
        )
        check(rejected.returncode != 0,
              f"invalid professor source {file_name} unexpectedly succeeded")
        check(not professor_output.exists(),
              f"invalid professor source {file_name} published an executable")

    rewrite = root / "rewrite"
    first_source = root / "rewrite-first.src"
    second_source = root / "rewrite-second.src"
    write_source(first_source, SIMPLE_SOURCE)
    write_source(second_source, SIMPLE_SOURCE.replace("42", "99"))
    for expected, selected in (("42\n", first_source), ("99\n", second_source),
                               ("99\n", second_source)):
        rebuilt = run_compiler("-o", str(rewrite), str(selected), env={"CC": "cc"})
        check(rebuilt.returncode == 0, f"native rewrite failed: {rebuilt.stderr!r}")
        check(run_native(rewrite).stdout == expected, "native rewrite published stale/torn output")

    concurrent_output = root / "concurrent"
    variants: list[Path] = []
    for index in range(32):
        variant = root / f"concurrent-{index}.src"
        write_source(variant, SIMPLE_SOURCE.replace("42", "17" if index % 2 == 0 else "29"))
        variants.append(variant)
    with ThreadPoolExecutor(max_workers=32) as executor:
        results = list(executor.map(
            lambda item: run_compiler("-o", str(concurrent_output), str(item), env={"CC": "cc"}),
            variants,
        ))
    check(all(result.returncode == 0 for result in results),
          f"concurrent native writers failed: {[r.stderr for r in results if r.returncode]}")
    final = run_native(concurrent_output)
    check(final.returncode == 0 and final.stdout in ("17\n", "29\n"),
          f"concurrent publication was torn or foreign: {final!r}")
    no_temp_debris(root)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="compiler_native_") as temp_dir:
        root = Path(temp_dir)
        test_fake_compiler_and_failures(root)
        test_destination_safety(root)
        test_real_native_runtime(root)
        no_temp_debris(root)
    print("native driver test pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
