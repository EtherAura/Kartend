#!/usr/bin/env python3
"""Offline test of `smoke-test.sh --check-extract-cache present|absent`.

Runs the real smoke-test.sh against tiny executables written to a temporary
directory, each standing in for an AppImage started with
`--appimage-extract-and-run --version`: one that leaves its
appimage_extracted_* directory in TMPDIR, one that removes it, ones that leave
a file or a symlink of that name instead, ones that fail, hang or print
something other than the version line, and one that removes its TMPDIR so it
cannot be listed afterwards. This proves the checker, not a real
AppImage: nothing here builds, downloads or starts one.
"""

import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SMOKE = Path(__file__).resolve().parent.parent / "smoke-test.sh"
BASH = shutil.which("bash")
LEFTOVER = "appimage_extracted_0123abcd"

# Every fixture records what it was started with, then does its part.
_RECORD = r"""#!/bin/sh
{
  printf 'args=%s\n' "$*"
  printf 'tmpdir=%s\n' "${TMPDIR-}"
  printf 'home=%s\n' "${HOME-}"
  printf 'platform=%s\n' "${QT_QPA_PLATFORM-}"
  printf 'no_cleanup=%s\n' "${NO_CLEANUP-<unset>}"
  printf 'tmp_entries=%s\n' "$(ls -A "$TMPDIR" | wc -l)"
} >"$FIXTURE_RECORD"
"""

_VERSION = "echo 'kartend 0.0.22'\n"

FIXTURES = {
    # A release whose runtime leaves the unpacked copy behind, read-only
    # parts included.
    "retained": _RECORD
    + f'mkdir -p "$TMPDIR/{LEFTOVER}/usr/bin"\n'
    + f'chmod 0500 "$TMPDIR/{LEFTOVER}/usr/bin" "$TMPDIR/{LEFTOVER}/usr"\n'
    + _VERSION,
    # A release that removes it when the app exits.
    "removed": _RECORD
    + f'mkdir -p "$TMPDIR/{LEFTOVER}/usr/bin"\n'
    + f'rm -rf "$TMPDIR/{LEFTOVER}"\n'
    + _VERSION,
    # Entries of that name that are not an unpacked directory.
    "leftover-file": _RECORD + f'echo stale >"$TMPDIR/{LEFTOVER}"\n' + _VERSION,
    "leftover-link-out": _RECORD + f'ln -s "$FIXTURE_OUTSIDE" "$TMPDIR/{LEFTOVER}"\n' + _VERSION,
    "leftover-dangling": _RECORD + f'ln -s "$TMPDIR/gone" "$TMPDIR/{LEFTOVER}"\n' + _VERSION,
    # Starts fail; neither leaves anything that could look like a pass.
    "fails": _RECORD + "echo 'cannot start' >&2\nexit 3\n",
    "fails-retained": _RECORD + f'mkdir -p "$TMPDIR/{LEFTOVER}"\n' + "exit 1\n",
    "hangs": _RECORD + "exec sleep 60\n",
    # Exit 0 without the version line.
    "not-kartend": _RECORD + "echo 'something else 1.0'\n",
    "kartend-not-version": _RECORD + "echo 'Kartend could not read its settings'\n",
    "version-suffix": _RECORD + "echo 'kartend 0.0.22-dirty'\n",
    "version-twice": _RECORD + _VERSION + _VERSION,
    # A clean start whose TMPDIR cannot be listed afterwards: it leaves the
    # unpacked copy, then removes the whole private TMPDIR it was given.
    "tmpdir-gone": _RECORD
    + f'mkdir -p "$TMPDIR/{LEFTOVER}/usr/bin"\n'
    + 'rm -rf "$TMPDIR"\n'
    + _VERSION,
}


@unittest.skipUnless(BASH and sys.platform.startswith("linux"), "needs bash on Linux")
class ExtractCacheCheck(unittest.TestCase):
    def setUp(self):
        # Spaces on purpose: every path the checker handles must be quoted.
        self.workspace = Path(tempfile.mkdtemp(prefix="kartend extract cache "))
        self.addCleanup(shutil.rmtree, self.workspace, ignore_errors=True)
        self.outer_tmp = self.workspace / "outer tmp"
        self.outer_tmp.mkdir()
        # Not the checker's: must survive its cleanup.
        self.sentinel = self.outer_tmp / "not-the-checkers.txt"
        self.sentinel.write_text("keep\n")
        # Outside the checker's workspace, read-only; a symlink left in TMPDIR
        # points here, and neither inspection nor cleanup may touch it.
        self.outside = self.workspace / "outside target"
        self.outside.mkdir()
        (self.outside / "keep.txt").write_text("outside\n")
        self.outside.chmod(0o500)
        self.addCleanup(self.outside.chmod, 0o700)
        self.record = self.workspace / "record.txt"
        fixture_dir = self.workspace / "fixture dir"
        fixture_dir.mkdir()
        self.fixtures = {}
        for name, body in FIXTURES.items():
            path = fixture_dir / f"Kartend {name}.AppImage"
            path.write_text(body)
            path.chmod(stat.S_IRWXU)
            self.fixtures[name] = path

    def run_smoke(self, *args):
        env = dict(os.environ)
        env["TMPDIR"] = str(self.outer_tmp)
        env["NO_CLEANUP"] = "1"
        env["FIXTURE_RECORD"] = str(self.record)
        env["FIXTURE_OUTSIDE"] = str(self.outside)
        proc = subprocess.run(
            [BASH, str(SMOKE), *args],
            env=env,
            capture_output=True,
            text=True,
            timeout=90,
        )
        # Whatever the verdict, the checker removed its own workspace and
        # nothing else.
        self.assertEqual(sorted(p.name for p in self.outer_tmp.iterdir()), [self.sentinel.name], proc.stderr)
        self.assertEqual(self.sentinel.read_text(), "keep\n")
        self.assertEqual(stat.S_IMODE(self.outside.stat().st_mode), 0o500)
        self.assertEqual([p.name for p in self.outside.iterdir()], ["keep.txt"])
        self.assertEqual((self.outside / "keep.txt").read_text(), "outside\n")
        return proc

    def run_check(self, fixture, expectation, *extra):
        return self.run_smoke("--check-extract-cache", expectation, *extra, str(self.fixtures[fixture]))

    def recorded(self):
        lines = self.record.read_text().splitlines()
        return dict(line.split("=", 1) for line in lines)

    def assert_passes(self, proc):
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("passed:", proc.stderr)
        self.assertNotIn("FAIL", proc.stderr)

    def assert_fails(self, proc, message):
        self.assertEqual(proc.returncode, 1, proc.stderr)
        self.assertIn("FAIL:", proc.stderr)
        self.assertIn(message, proc.stderr)
        self.assertNotIn("passed:", proc.stderr)

    def assert_refused(self, proc, message):
        # Refused before anything started: not even the fixture ran.
        self.assertEqual(proc.returncode, 1, proc.stderr)
        self.assertIn(message, proc.stderr)
        self.assertNotIn("passed:", proc.stderr)
        self.assertFalse(self.record.exists(), proc.stderr)

    def test_retained_copy_matches_present(self):
        proc = self.run_check("retained", "present")
        self.assert_passes(proc)
        self.assertIn(f"{LEFTOVER} (directory)", proc.stderr)

    def test_retained_copy_fails_absent(self):
        proc = self.run_check("retained", "absent")
        self.assert_fails(proc, "an appimage_extracted_* entry stayed after the app exited")
        self.assertIn(f"{LEFTOVER} (directory)", proc.stderr)

    def test_removed_copy_matches_absent(self):
        proc = self.run_check("removed", "absent")
        self.assert_passes(proc)
        self.assertIn("no appimage_extracted_* entry", proc.stderr)

    def test_removed_copy_fails_present(self):
        proc = self.run_check("removed", "present")
        self.assert_fails(proc, "no unpacked appimage_extracted_* directory")
        self.assertIn("entries found: none", proc.stderr)

    def test_non_directory_leftover_fails_absent(self):
        for fixture, kind in (
            ("leftover-file", "regular file"),
            ("leftover-link-out", "symlink"),
            ("leftover-dangling", "symlink"),
        ):
            with self.subTest(fixture=fixture):
                proc = self.run_check(fixture, "absent")
                self.assert_fails(proc, "an appimage_extracted_* entry stayed after the app exited")
                self.assertIn(f"{LEFTOVER} ({kind})", proc.stderr)

    def test_non_directory_leftover_is_not_present(self):
        # A symlink to a directory is still not the unpacked copy.
        for fixture, kind in (
            ("leftover-file", "regular file"),
            ("leftover-link-out", "symlink"),
            ("leftover-dangling", "symlink"),
        ):
            with self.subTest(fixture=fixture):
                proc = self.run_check(fixture, "present")
                self.assert_fails(proc, "no unpacked appimage_extracted_* directory")
                self.assertIn(f"entries found: {LEFTOVER} ({kind})", proc.stderr)

    def test_start_runs_isolated_and_without_no_cleanup(self):
        self.assert_passes(self.run_check("removed", "absent"))
        seen = self.recorded()
        self.assertEqual(seen["args"], "--appimage-extract-and-run --version")
        self.assertEqual(seen["no_cleanup"], "<unset>")
        self.assertEqual(seen["platform"], "offscreen")
        self.assertEqual(seen["tmp_entries"].strip(), "0")
        tmpdir = Path(seen["tmpdir"])
        self.assertEqual(tmpdir.parent.parent, self.outer_tmp)
        self.assertEqual(Path(seen["home"]).parent, tmpdir.parent)
        self.assertNotEqual(seen["home"], os.environ.get("HOME"))

    def test_failed_start_is_not_a_pass(self):
        for expectation in ("absent", "present"):
            with self.subTest(expectation=expectation):
                proc = self.run_check("fails", expectation)
                self.assert_fails(proc, "exited with status 3")
                self.assertIn("cannot start", proc.stderr)

    def test_failed_start_with_leftover_is_not_a_pass(self):
        proc = self.run_check("fails-retained", "present")
        self.assert_fails(proc, "exited with status 1")
        self.assertIn("was not judged", proc.stderr)

    def test_timeout_is_not_a_pass(self):
        for expectation in ("absent", "present"):
            with self.subTest(expectation=expectation):
                proc = self.run_check("hangs", expectation, "--extract-timeout", "1")
                self.assert_fails(proc, "did not finish within 1 s")

    def test_failed_scan_is_not_a_pass(self):
        # The start succeeds, but its TMPDIR cannot be listed: no cache verdict.
        for expectation in ("absent", "present"):
            with self.subTest(expectation=expectation):
                proc = self.run_check("tmpdir-gone", expectation)
                self.assert_fails(proc, "could not list the TMPDIR of the start")
                self.assertIn("ok: --appimage-extract-and-run --version: kartend 0.0.22", proc.stderr)
                self.assertIn("find: ", proc.stderr)
                self.assertIn("not judged, because the scan failed", proc.stderr)
                self.assertNotIn("ok: extract cache", proc.stderr)
                self.assertNotIn("entry stayed after the app exited", proc.stderr)
                self.assertNotIn("holds no unpacked", proc.stderr)
                self.assertFalse(Path(self.recorded()["tmpdir"]).exists())

    def test_output_that_is_not_the_version_line_is_not_a_pass(self):
        for fixture in ("not-kartend", "kartend-not-version", "version-suffix", "version-twice"):
            for expectation in ("absent", "present"):
                with self.subTest(fixture=fixture, expectation=expectation):
                    proc = self.run_check(fixture, expectation)
                    self.assert_fails(proc, "not the single version line 'kartend X.Y.Z'")

    def test_expect_version_must_match_exactly(self):
        proc = self.run_check("removed", "absent", "--expect-version", "9.9.9")
        self.assert_fails(proc, "reported version 0.0.22, expected exactly 9.9.9")
        for prefix in ("0.0.2", "0.0", "0"):
            with self.subTest(prefix=prefix):
                proc = self.run_check("removed", "absent", "--expect-version", prefix)
                self.assert_fails(proc, f"reported version 0.0.22, expected exactly {prefix}")
        self.assert_passes(self.run_check("removed", "absent", "--expect-version", "0.0.22"))

    def test_bad_expectation_is_refused(self):
        self.assert_refused(self.run_check("removed", "maybe"), "needs 'present' or 'absent', not 'maybe'")

    def test_empty_expectation_is_refused(self):
        self.assert_refused(self.run_check("removed", ""), "needs 'present' or 'absent', not ''")

    def test_timeout_without_cache_mode_is_refused(self):
        proc = self.run_smoke("--extract-timeout", "5", str(self.fixtures["removed"]))
        self.assert_refused(proc, "--extract-timeout applies only to --check-extract-cache")

    def test_bad_timeout_is_refused(self):
        # A leading 0 is refused by the documented policy, and never reaches
        # Bash arithmetic, where 08 would be a bad octal number.
        for value in ("0", "08", "010", "1.5", "-1", "abc", "", "1234567"):
            with self.subTest(value=value):
                proc = self.run_check("removed", "absent", "--extract-timeout", value)
                self.assert_refused(proc, "--extract-timeout needs whole seconds from 1 to 999999")
                self.assertIn(f"not '{value}'", proc.stderr)
                self.assertNotIn("value too great for base", proc.stderr)
                self.assertNotIn("syntax error", proc.stderr)

    def test_help_documents_the_mode(self):
        proc = subprocess.run([BASH, str(SMOKE), "--help"], capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0)
        self.assertIn("--check-extract-cache present|absent", proc.stdout)
        self.assertIn("--extract-timeout", proc.stdout)
        self.assertIn("unpacked appimage_extracted_*", proc.stdout)
        self.assertIn("entry of any type", proc.stdout)


if __name__ == "__main__":
    unittest.main()
