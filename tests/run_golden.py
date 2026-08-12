#!/usr/bin/env python3
"""Golden-file regression harness for the EECS 6083 compiler check-only mode.

Runs ./compiler over every test program in the repo and compares the observed
behaviour (exit status class, exit code, stdout bytes, stderr emptiness)
against a recorded baseline in tests/manifest.json + tests/golden/.

The baseline records CURRENT behaviour, bugs included.  A golden file is not a
statement about what the compiler *should* print -- it is a tripwire that fires
when what it *does* print changes.  See tests/README.md.

Usage (from anywhere; paths are resolved relative to this script):

    python3 tests/run_golden.py                 # verify, exit 0 iff all match
    python3 tests/run_golden.py --update        # re-record baseline, show diff
    python3 tests/run_golden.py --filter scope  # only paths containing "scope"
    python3 tests/run_golden.py --verbose       # one line per program
    python3 tests/run_golden.py --list          # recorded status of each program
"""

from __future__ import annotations

import argparse
import concurrent.futures
import difflib
import hashlib
import json
import os
import signal as signal_module
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

# --------------------------------------------------------------------------
# Layout
# --------------------------------------------------------------------------

TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parent
COMPILER = REPO_ROOT / "compiler"
MANIFEST_PATH = TESTS_DIR / "manifest.json"
GOLDEN_DIR = TESTS_DIR / "golden"

# Directories searched (recursively) for *.src programs.  Top-level roots on
# purpose: a future subdirectory (say docs/audit/probes/codegen/) is picked up
# automatically, so a new program can never slip in unrecorded.  The stray
# non-.src scratch file in testPgms/UnitTests is naturally excluded by the
# *.src glob.
PROGRAM_ROOTS = (
    "testPgms",
    "docs/audit/probes",
)

MANIFEST_VERSION = 1

# Seconds a program gets before it is killed.  Programs already recorded as
# TIMEOUT get the shorter budget: they never terminate today, so we only need
# long enough to be confident nothing changed, and any termination inside the
# budget is itself a behaviour change that must fail verification.
#
# The default budget is deliberately generous: it is an upper bound that only a
# genuinely-new hang ever pays (every terminating program today finishes in
# ~2ms).  It must stay well above ~10s because a signal-killed program (the
# recorded CRASH) costs ~1.1s of core-dump piping on hosts with apport-style
# core_pattern handlers, and those handlers SERIALIZE system-wide -- concurrent
# suite runs queue behind each other and a 5s budget produced spurious
# timeouts.
DEFAULT_TIMEOUT_SEC = 30.0
TIMEOUT_BUDGET_SEC = 2.0

# Status classes.
OK = "OK"            # exited 0
ERRORS = "ERRORS"    # exited nonzero, normal termination
CRASH = "CRASH"      # killed by a signal
TIMEOUT = "TIMEOUT"  # still running when the budget expired
STATUS_ORDER = (OK, ERRORS, CRASH, TIMEOUT)

MAX_DIFF_LINES = 24


# --------------------------------------------------------------------------
# Running one program
# --------------------------------------------------------------------------


@dataclass
class RunResult:
    """What actually happened when we ran one program."""

    status: str
    exit_code: int | None   # shell convention: 128+signum for signal deaths
    signal_name: str | None
    stdout: bytes | None    # None for TIMEOUT (killed mid-flight; not recorded)
    stderr_nonempty: bool
    timeout_sec: float
    duration: float


def run_program(rel_path: str, timeout_sec: float) -> RunResult:
    started = time.monotonic()
    try:
        proc = subprocess.run(
            [str(COMPILER), str(REPO_ROOT / rel_path)],
            cwd=REPO_ROOT,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_sec,
        )
    except subprocess.TimeoutExpired:
        return RunResult(
            status=TIMEOUT,
            exit_code=None,
            signal_name=None,
            stdout=None,
            stderr_nonempty=False,
            timeout_sec=timeout_sec,
            duration=time.monotonic() - started,
        )

    duration = time.monotonic() - started
    rc = proc.returncode
    if rc < 0:
        signum = -rc
        try:
            signal_name = signal_module.Signals(signum).name
        except ValueError:
            signal_name = f"SIG{signum}"
        status, exit_code = CRASH, 128 + signum
    else:
        signal_name = None
        status, exit_code = (OK if rc == 0 else ERRORS), rc

    return RunResult(
        status=status,
        exit_code=exit_code,
        signal_name=signal_name,
        stdout=proc.stdout,
        stderr_nonempty=bool(proc.stderr),
        timeout_sec=timeout_sec,
        duration=duration,
    )


def run_many(jobs: list[tuple[str, float]], workers: int) -> dict[str, RunResult]:
    """Run (path, timeout) pairs concurrently; subprocess waits release the GIL."""
    results: dict[str, RunResult] = {}
    if not jobs:
        return results
    workers = max(1, min(workers, len(jobs)))
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(run_program, path, budget): path for path, budget in jobs}
        for future in concurrent.futures.as_completed(futures):
            results[futures[future]] = future.result()
    return results


# --------------------------------------------------------------------------
# Discovery, manifest I/O
# --------------------------------------------------------------------------


def discover_programs() -> list[str]:
    """Every *.src under PROGRAM_ROOTS, as repo-relative posix paths, sorted."""
    found: list[str] = []
    for root in PROGRAM_ROOTS:
        base = REPO_ROOT / root
        if not base.is_dir():
            continue
        for src in base.rglob("*.src"):
            found.append(src.relative_to(REPO_ROOT).as_posix())
    return sorted(found)


def golden_rel_path(rel_path: str) -> str:
    """Golden stdout for a program, mirroring the source tree under tests/."""
    return f"tests/golden/{rel_path}.out"


def load_manifest() -> dict | None:
    if not MANIFEST_PATH.exists():
        return None
    with MANIFEST_PATH.open(encoding="utf-8") as handle:
        return json.load(handle)


def entry_for(rel_path: str, result: RunResult) -> dict:
    """Manifest entry describing an observed run."""
    entry: dict = {"status": result.status}
    if result.status == TIMEOUT:
        # Nothing meaningful to record: the process was killed mid-flight, so
        # any buffered stdout is an artifact of when the kill landed.  Future
        # runs get the short budget; terminating at all is a change.
        entry["timeout_sec"] = TIMEOUT_BUDGET_SEC
        return entry

    entry["exit_code"] = result.exit_code
    if result.signal_name:
        entry["signal"] = result.signal_name
    entry["timeout_sec"] = DEFAULT_TIMEOUT_SEC
    stdout = result.stdout or b""
    entry["golden"] = golden_rel_path(rel_path)
    entry["stdout_bytes"] = len(stdout)
    entry["stdout_sha256"] = hashlib.sha256(stdout).hexdigest()
    entry["stderr_nonempty"] = result.stderr_nonempty
    return entry


def budget_for(entry: dict | None) -> float:
    if entry is None:
        return DEFAULT_TIMEOUT_SEC
    return float(entry.get("timeout_sec", DEFAULT_TIMEOUT_SEC))


def describe_entry(entry: dict) -> str:
    status = entry.get("status", "?")
    if status == TIMEOUT:
        return f"{status} (budget {entry.get('timeout_sec')}s)"
    bits = [status, f"exit {entry.get('exit_code')}"]
    if entry.get("signal"):
        bits.append(entry["signal"])
    bits.append(f"{entry.get('stdout_bytes', 0)}B stdout")
    if entry.get("stderr_nonempty"):
        bits.append("stderr nonempty")
    return ", ".join(bits)


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------


def stdout_diff(expected: bytes, actual: bytes) -> list[str]:
    expected_lines = expected.decode("utf-8", "replace").splitlines(keepends=True)
    actual_lines = actual.decode("utf-8", "replace").splitlines(keepends=True)
    diff = list(
        difflib.unified_diff(
            expected_lines, actual_lines, fromfile="golden", tofile="actual", n=1
        )
    )
    lines = [line.rstrip("\n") for line in diff]
    if len(lines) > MAX_DIFF_LINES:
        hidden = len(lines) - MAX_DIFF_LINES
        lines = lines[:MAX_DIFF_LINES] + [f"... ({hidden} more diff lines suppressed)"]
    return lines


def verify_one(rel_path: str, entry: dict, result: RunResult) -> list[str]:
    """Return a list of failure reasons (empty == matches the baseline)."""
    problems: list[str] = []
    expected_status = entry.get("status")

    if expected_status == TIMEOUT:
        if result.status != TIMEOUT:
            problems.append(
                f"expected TIMEOUT but the program terminated within "
                f"{result.timeout_sec:g}s as {result.status} "
                f"(exit {result.exit_code}) -- behaviour changed"
            )
        return problems

    if result.status == TIMEOUT:
        problems.append(
            f"expected {expected_status} but the program did not terminate "
            f"within {result.timeout_sec:g}s"
        )
        return problems

    if result.status != expected_status:
        problems.append(f"status {expected_status} -> {result.status}")

    if result.exit_code != entry.get("exit_code"):
        problems.append(f"exit code {entry.get('exit_code')} -> {result.exit_code}")

    if result.signal_name != entry.get("signal"):
        problems.append(f"signal {entry.get('signal')} -> {result.signal_name}")

    if result.stderr_nonempty != bool(entry.get("stderr_nonempty", False)):
        was = "nonempty" if entry.get("stderr_nonempty") else "empty"
        now = "nonempty" if result.stderr_nonempty else "empty"
        problems.append(f"stderr {was} -> {now}")

    golden_rel = entry.get("golden")
    if not golden_rel:
        problems.append("manifest entry has no golden file recorded -- run --update")
        return problems

    golden_file = REPO_ROOT / golden_rel
    if not golden_file.exists():
        problems.append(f"golden file missing: {golden_rel} -- run --update")
        return problems

    golden = golden_file.read_bytes()
    recorded_sha = entry.get("stdout_sha256")
    if recorded_sha and hashlib.sha256(golden).hexdigest() != recorded_sha:
        problems.append(
            f"golden file {golden_rel} does not match its manifest checksum "
            "(edited by hand?) -- re-record with --update"
        )
        return problems

    actual = result.stdout or b""
    if actual != golden:
        problems.append(
            f"stdout differs ({len(golden)}B golden vs {len(actual)}B actual)"
        )
        problems.extend("    " + line for line in stdout_diff(golden, actual))

    return problems


def print_summary(counts: dict[str, tuple[int, int]]) -> None:
    """counts: status -> (matched, failed)."""
    print()
    print(f"  {'status':<9} {'expected':>9} {'matched':>8} {'failed':>7}")
    print(f"  {'-' * 9} {'-' * 9:>9} {'-' * 8:>8} {'-' * 7:>7}")
    total_expected = total_matched = total_failed = 0
    # STATUS_ORDER first, then anything unexpected a hand-edited manifest holds,
    # so the total row always accounts for every program.
    ordered = list(STATUS_ORDER) + sorted(s for s in counts if s not in STATUS_ORDER)
    for status in ordered:
        matched, failed = counts.get(status, (0, 0))
        if matched == 0 and failed == 0:
            continue
        expected = matched + failed
        total_expected += expected
        total_matched += matched
        total_failed += failed
        print(f"  {status:<9} {expected:>9} {matched:>8} {failed:>7}")
    print(f"  {'-' * 9} {'-' * 9:>9} {'-' * 8:>8} {'-' * 7:>7}")
    print(f"  {'total':<9} {total_expected:>9} {total_matched:>8} {total_failed:>7}")


def cmd_verify(args: argparse.Namespace) -> int:
    manifest = load_manifest()
    if manifest is None:
        print(f"error: no baseline at {MANIFEST_PATH.relative_to(REPO_ROOT)} -- "
              f"run: python3 tests/run_golden.py --update", file=sys.stderr)
        return 2

    entries: dict = manifest.get("programs", {})
    on_disk = discover_programs()
    selected_disk = [p for p in on_disk if matches(p, args.filter)]
    selected_manifest = [p for p in sorted(entries) if matches(p, args.filter)]
    all_paths = sorted(set(selected_disk) | set(selected_manifest))

    if not all_paths:
        print(f"no programs matched filter {args.filter!r}", file=sys.stderr)
        return 2

    runnable = [p for p in all_paths if p in entries and p in set(on_disk)]
    jobs = [(p, budget_for(entries[p])) for p in runnable]
    started = time.monotonic()
    results = run_many(jobs, args.jobs)
    wall = time.monotonic() - started

    counts: dict[str, list[int]] = {status: [0, 0] for status in STATUS_ORDER}
    failures = 0
    unexpected = 0

    for rel_path in all_paths:
        entry = entries.get(rel_path)
        if entry is None:
            print(f"FAIL {rel_path}")
            print("    no expectation recorded -- run --update")
            failures += 1
            unexpected += 1
            continue
        status = entry.get("status", OK)
        if rel_path not in set(on_disk):
            print(f"FAIL {rel_path}")
            print("    stale manifest entry: source file no longer exists -- "
                  "run --update")
            counts.setdefault(status, [0, 0])[1] += 1
            failures += 1
            continue

        problems = verify_one(rel_path, entry, results[rel_path])
        if problems:
            print(f"FAIL {rel_path}")
            for problem in problems:
                print(f"    {problem}")
            counts.setdefault(status, [0, 0])[1] += 1
            failures += 1
        else:
            counts.setdefault(status, [0, 0])[0] += 1
            if args.verbose:
                print(f"ok   {rel_path}  ({describe_entry(entry)})")

    print_summary({k: (v[0], v[1]) for k, v in counts.items()})
    if unexpected:
        print(f"  plus {unexpected} program(s) with no expectation recorded")
    print(f"\n{len(runnable)} program(s) run in {wall:.1f}s wall "
          f"({args.jobs} workers)")

    if failures:
        print(f"FAILED: {failures} program(s) diverge from the recorded baseline")
        return 1
    print("PASS: every program matches the recorded baseline")
    return 0


# --------------------------------------------------------------------------
# Updating the baseline
# --------------------------------------------------------------------------


def cmd_update(args: argparse.Namespace) -> int:
    previous = load_manifest() or {}
    previous_entries: dict = previous.get("programs", {})

    on_disk = discover_programs()
    on_disk_set = set(on_disk)
    selected = [p for p in on_disk if matches(p, args.filter)]
    # Recorded programs whose source has since vanished; a filtered update still
    # has to be able to drop these, so they count as "matched" for the guard.
    stale = [p for p in sorted(previous_entries)
             if matches(p, args.filter) and p not in on_disk_set]
    if not selected and not stale:
        print(f"no programs matched filter {args.filter!r}", file=sys.stderr)
        return 2

    # Re-discover honestly: run everything at the full budget so a program that
    # used to hang but now terminates in 3s is recorded as terminating.
    jobs = [(p, DEFAULT_TIMEOUT_SEC) for p in selected]
    started = time.monotonic()
    results = run_many(jobs, args.jobs)
    wall = time.monotonic() - started

    # A filtered update must not drop the entries it did not run.
    entries: dict = dict(previous_entries) if args.filter else {}
    for path in stale:
        entries.pop(path, None)

    added: list[str] = []
    changed: list[tuple[str, list[str]]] = []

    for rel_path in selected:
        result = results[rel_path]
        entry = entry_for(rel_path, result)
        old = previous_entries.get(rel_path)
        entries[rel_path] = entry

        golden_file = REPO_ROOT / golden_rel_path(rel_path)
        if result.status == TIMEOUT:
            if golden_file.exists():
                golden_file.unlink()
        else:
            golden_file.parent.mkdir(parents=True, exist_ok=True)
            golden_file.write_bytes(result.stdout or b"")

        if old is None:
            added.append(rel_path)
            continue
        notes = diff_entries(old, entry)
        if notes:
            changed.append((rel_path, notes))

    removed = [p for p in sorted(previous_entries) if p not in entries]
    for rel_path in removed:
        stale = REPO_ROOT / golden_rel_path(rel_path)
        if stale.exists():
            stale.unlink()

    entries = {path: entries[path] for path in sorted(entries)}
    manifest = {
        "manifest_version": MANIFEST_VERSION,
        "note": ("Recorded current check-only compiler behaviour. "
                 "The default build is the canonical baseline producer; "
                 "optimized and sanitizer builds verify rather than regenerate it. "
                 "Regenerate only for an intended reviewed change with: "
                 "python3 tests/run_golden.py --update"),
        "default_timeout_sec": DEFAULT_TIMEOUT_SEC,
        "timeout_budget_sec": TIMEOUT_BUDGET_SEC,
        "program_roots": list(PROGRAM_ROOTS),
        "programs": entries,
    }
    MANIFEST_PATH.parent.mkdir(parents=True, exist_ok=True)
    with MANIFEST_PATH.open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    prune_orphan_goldens(entries)

    if not previous_entries:
        print(f"recorded a new baseline for {len(entries)} program(s)")
    else:
        for rel_path in added:
            print(f"added    {rel_path}  ({describe_entry(entries[rel_path])})")
        for rel_path, notes in changed:
            print(f"changed  {rel_path}")
            for note in notes:
                print(f"           {note}")
        for rel_path in removed:
            print(f"removed  {rel_path}  (was {describe_entry(previous_entries[rel_path])})")
        if not (added or changed or removed):
            print("no changes: the recorded baseline already matches current behaviour")
        else:
            print(f"\n{len(added)} added, {len(changed)} changed, {len(removed)} removed")

    by_status: dict[str, int] = {}
    for entry in entries.values():
        by_status[entry["status"]] = by_status.get(entry["status"], 0) + 1
    print("\nbaseline: " + ", ".join(
        f"{by_status[s]} {s}" for s in STATUS_ORDER if s in by_status))
    print(f"{len(selected)} program(s) run in {wall:.1f}s wall ({args.jobs} workers)")
    return 0


def diff_entries(old: dict, new: dict) -> list[str]:
    """Human-readable notes describing how a recorded entry changed."""
    notes: list[str] = []
    if old.get("status") != new.get("status"):
        notes.append(f"{old.get('status')} -> {new.get('status')}")
    if old.get("exit_code") != new.get("exit_code"):
        notes.append(f"exit code {old.get('exit_code')} -> {new.get('exit_code')}")
    if old.get("signal") != new.get("signal"):
        notes.append(f"signal {old.get('signal')} -> {new.get('signal')}")
    if bool(old.get("stderr_nonempty")) != bool(new.get("stderr_nonempty")):
        notes.append(f"stderr nonempty {bool(old.get('stderr_nonempty'))} -> "
                     f"{bool(new.get('stderr_nonempty'))}")
    if old.get("stdout_sha256") != new.get("stdout_sha256"):
        notes.append(f"stdout differs ({old.get('stdout_bytes')}B -> "
                     f"{new.get('stdout_bytes')}B)")
    return notes


def prune_orphan_goldens(entries: dict) -> None:
    """Delete *.out under tests/golden/ that no manifest entry claims."""
    if not GOLDEN_DIR.is_dir():
        return
    wanted = {
        (REPO_ROOT / entry["golden"]).resolve()
        for entry in entries.values()
        if entry.get("golden")
    }
    for path in sorted(GOLDEN_DIR.rglob("*.out")):
        if path.resolve() not in wanted:
            path.unlink()
            print(f"pruned   {path.relative_to(REPO_ROOT).as_posix()} (orphaned golden)")
    # Tidy up directories left empty by pruning.
    for directory in sorted(GOLDEN_DIR.rglob("*"), reverse=True):
        if directory.is_dir() and not any(directory.iterdir()):
            directory.rmdir()


# --------------------------------------------------------------------------
# Listing
# --------------------------------------------------------------------------


def cmd_list(args: argparse.Namespace) -> int:
    manifest = load_manifest() or {}
    entries: dict = manifest.get("programs", {})
    on_disk = set(discover_programs())
    paths = sorted(set(entries) | on_disk)
    paths = [p for p in paths if matches(p, args.filter)]
    if not paths:
        print(f"no programs matched filter {args.filter!r}", file=sys.stderr)
        return 2

    width = max(len(p) for p in paths)
    for rel_path in paths:
        entry = entries.get(rel_path)
        if entry is None:
            note = "(no expectation recorded -- run --update)"
        elif rel_path not in on_disk:
            note = f"{describe_entry(entry)}  (STALE: source file missing)"
        else:
            note = describe_entry(entry)
        print(f"{rel_path:<{width}}  {note}")

    by_status: dict[str, int] = {}
    for rel_path in paths:
        entry = entries.get(rel_path)
        status = entry["status"] if entry else "UNRECORDED"
        by_status[status] = by_status.get(status, 0) + 1
    order = [s for s in STATUS_ORDER if s in by_status] + \
            [s for s in sorted(by_status) if s not in STATUS_ORDER]
    print(f"\n{len(paths)} program(s): " + ", ".join(f"{by_status[s]} {s}" for s in order))
    return 0


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------


def matches(rel_path: str, needle: str | None) -> bool:
    return needle is None or needle in rel_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="run_golden.py",
        description="Golden-file regression harness for compiler check-only mode.",
    )
    parser.add_argument(
        "--update", action="store_true",
        help="re-record the baseline (manifest + goldens) and report what changed",
    )
    parser.add_argument(
        "--list", action="store_true", dest="list_only",
        help="print every program with its recorded status and exit",
    )
    parser.add_argument(
        "--filter", metavar="SUBSTR", default=None,
        help="only act on programs whose repo-relative path contains SUBSTR",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="in verify mode, print a line for every program, not just failures",
    )
    parser.add_argument(
        "--jobs", "-j", type=int, default=min(8, os.cpu_count() or 4),
        metavar="N", help="how many programs to run concurrently (default: %(default)s)",
    )
    args = parser.parse_args(argv)

    if args.update and args.list_only:
        parser.error("--update and --list are mutually exclusive")
    if args.jobs < 1:
        parser.error("--jobs must be >= 1")

    if args.list_only:
        return cmd_list(args)

    if not COMPILER.exists():
        print(f"error: {COMPILER} not found -- build it first with `make`",
              file=sys.stderr)
        return 2
    if not os.access(COMPILER, os.X_OK):
        print(f"error: {COMPILER} is not executable", file=sys.stderr)
        return 2

    return cmd_update(args) if args.update else cmd_verify(args)


if __name__ == "__main__":
    sys.exit(main())
