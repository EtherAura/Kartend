#!/usr/bin/env python3
"""Kartend-oieo: portable failure-detail dumper for CI.

Both the Linux and Windows test runs invoke `ctest --output-on-failure`. On
Linux that captures the QTest TAP / -v2 output into the workflow log fine.
On Windows the ctest stdout pipe doesn't receive any QTest output (every
test reports `Output: <end of output>` in LastTest.log, even passing
ones) — so a red CI run gives us nothing to diagnose with.

This script reimplements the original Windows-only PowerShell fallback in
a cross-platform form. It walks `build/Testing/Temporary/LastTestsFailed.log`,
locates the matching test executable in `build/tests/`, and re-invokes it
with `-v2 -o <tmp>,txt` so QTest writes its per-assertion output to a
file (bypassing the broken stdout pipe). Each test's verdicts + assertion
text gets dumped to stdout inside a GitHub Actions `::group::` block so
the workflow log shows the same shape on both OSes.

It ALSO dumps the raw `LastTest.log` first (dump_last_test_log): a flaky test
that passes on the isolated re-run above would otherwise hide the original
failure. LastTest.log preserves the actual failing run's output, so a
heisenbug like Kartend-c5byx stays diagnosable on recurrence.

CI invocation:
    python .scripts/print-failed-tests.py --build-dir build

Locally:
    .scripts/print-failed-tests.py --build-dir build/ninja-release

The build dir layout follows CTest conventions:
    <build>/Testing/Temporary/LastTestsFailed.log
    <build>/tests/test_<name>[.exe]
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# QTest output (and the GitHub `::group::` labels we build from test names)
# can contain non-Latin-1 characters like `→` / `—`. On Windows, Python's
# stdout/stderr default to the legacy code page (e.g. cp1252), which raises
# UnicodeEncodeError on those characters — and since this script's whole job
# is to surface diagnostics for a failing CI run, that crash would mask EVERY
# Windows test failure. Force UTF-8 with replacement so a stray glyph can
# never abort the dump. `reconfigure` exists on the standard TextIOWrapper
# streams since Python 3.7; guard for redirected/replaced streams that lack it.
for _stream in (sys.stdout, sys.stderr):
    _reconfigure = getattr(_stream, "reconfigure", None)
    if _reconfigure is not None:
        _reconfigure(encoding="utf-8", errors="replace")


def find_failed_tests(build_dir: Path) -> list[str]:
    """Return the list of test names that failed in the most recent ctest run.

    LastTestsFailed.log format: one line per failure, `<index>:<TestName>`.
    Returns [] when the file is missing (no failures recorded yet).
    """
    log_path = build_dir / "Testing" / "Temporary" / "LastTestsFailed.log"
    if not log_path.is_file():
        return []
    failures: list[str] = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        # Each line is `<n>:<TestName>` — split once on the colon so test
        # names that themselves contain a colon (rare but possible) round-
        # trip intact.
        parts = line.split(":", 1)
        if len(parts) == 2 and parts[1].strip():
            failures.append(parts[1].strip())
    return failures


def find_test_executable(build_dir: Path, name: str) -> Path | None:
    """Locate the test executable for the given ctest test name.

    Naming convention is `test_<name>[.exe]` under `<build>/tests/`. Matches
    case-insensitively on the suffix because tests like `KartReader` map to
    `test_kartreader.exe` (display-case in CTest, lowercase on disk).
    """
    tests_dir = build_dir / "tests"
    if not tests_dir.is_dir():
        return None
    # Allow both `test_<name>` and `test_<name>.exe`. fnmatch-style would be
    # simpler but the case-insensitive suffix match is the actual rule.
    pattern = re.compile(r"^test_(.+?)(?:\.exe)?$", re.IGNORECASE)
    for candidate in tests_dir.iterdir():
        if not candidate.is_file():
            continue
        match = pattern.match(candidate.name)
        if match and match.group(1).lower() == name.lower():
            return candidate
    return None


def dump_failed_test(name: str, exe: Path | None) -> None:
    """Print a `::group::` block with the test's QTest output, or a stub if
    we couldn't locate the executable. The group header includes the
    resolved exe path so a wrong-glob bug is visible in the log.
    """
    group_label = f"{name} → {exe}" if exe else f"{name} (no matching exe under build/tests/)"
    print(f"::group::{group_label}", flush=True)
    if exe is None:
        print(f"(could not locate test exe for {name!r})", flush=True)
        print("::endgroup::", flush=True)
        return
    # `-o file,txt` is the workaround: QTest writes its full per-test
    # verdicts + assertion text via fopen/fprintf rather than through
    # stdout, sidestepping the broken-on-Windows ctest pipe.
    with tempfile.NamedTemporaryFile(
        prefix=f"{name}-qtest-", suffix=".txt", delete=False
    ) as tmp:
        out_path = Path(tmp.name)
    try:
        proc = subprocess.run(
            [str(exe), "-v2", "-o", f"{out_path},txt"],
            check=False,
        )
        print("--- QTest -o txt ---", flush=True)
        if out_path.is_file():
            try:
                print(out_path.read_text(encoding="utf-8", errors="replace"), flush=True)
            except OSError as err:
                print(f"(could not read {out_path}: {err})", flush=True)
        else:
            print(f"(no {out_path} produced — process exited before QTest wrote)", flush=True)
        print(f"exit: {proc.returncode}", flush=True)
    finally:
        # Always clean up the tmp file. The runner already disposes
        # $RUNNER_TEMP between jobs, but a long local debug run could leak.
        try:
            out_path.unlink()
        except OSError:
            pass
    print("::endgroup::", flush=True)


def dump_last_test_log(build_dir: Path) -> None:
    """Print the raw LastTest.log from the ctest run that just failed.

    This script RE-RUNS each failed test to capture QTest output, but a flaky
    heisenbug — Kartend-c5byx: a rare `DatabaseManager` subprocess abort under
    `ctest -j2`, green on an isolated re-run — passes that re-run, so
    dump_failed_test() reports a misleading success and the original crash is
    lost. LastTest.log holds the ACTUAL failing run's captured output (including
    an abort's stderr / signal), which ctest leaves untouched by our direct exe
    re-invocations. Surface it verbatim before the re-runs so the one artifact
    c5byx asked to capture "immediately" on recurrence is always in the CI log.
    """
    log_path = build_dir / "Testing" / "Temporary" / "LastTest.log"
    print("::group::LastTest.log (original failing ctest run, pre-rerun)", flush=True)
    if log_path.is_file():
        try:
            print(log_path.read_text(encoding="utf-8", errors="replace"), flush=True)
        except OSError as err:
            print(f"(could not read {log_path}: {err})", flush=True)
    else:
        print(f"(no {log_path} — ctest wrote no per-run log)", flush=True)
    print("::endgroup::", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump QTest output for failed CI tests.")
    parser.add_argument(
        "--build-dir",
        required=True,
        type=Path,
        help="Path to the CMake build directory (contains Testing/Temporary/).",
    )
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        print(f"::warning::{build_dir} not found", flush=True)
        return 0

    failed = find_failed_tests(build_dir)
    if not failed:
        # No LastTestsFailed.log or it was empty — likely tests passed or
        # the failure was at build time. Don't emit a warning; the parent
        # workflow step already surfaced the build failure.
        return 0

    # Surface the original failing run's output first: for a flaky abort the
    # per-test re-runs below pass and would otherwise hide it (Kartend-c5byx).
    dump_last_test_log(build_dir)

    for name in failed:
        exe = find_test_executable(build_dir, name)
        dump_failed_test(name, exe)

    return 0


if __name__ == "__main__":
    sys.exit(main())
