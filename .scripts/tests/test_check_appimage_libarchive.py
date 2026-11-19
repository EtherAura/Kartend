#!/usr/bin/env python3
"""Tests for packaging/appimage/check-libarchive.py.

The check reads the libarchive version string out of one staged library file
and fails below the approved floor. These tests run the script's command line
via subprocess against small temporary files that only look like an ELF
library (the ELF magic, some filler, and a NUL-terminated version string); no
real library is committed, loaded or run. They show that an older version is
printed and rejected, the floor and a later version are printed and accepted,
and a missing file or missing, ambiguous or non-ELF version evidence fails.

stdlib unittest — pytest is not available in CI. Run directly
(`python3 .scripts/tests/test_check_appimage_libarchive.py`) or via
`python3 -m unittest`.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
CHECK = REPO / "packaging" / "appimage" / "check-libarchive.py"

ELF_HEADER = b"\x7fELF\x02\x01\x01" + b"\x00" * 57
FILLER = b"\x00GLIBC_2.34\x00libz.so.1\x00archive_read_new\x00"


def fake_library(*version_strings: bytes, header: bytes = ELF_HEADER) -> bytes:
    body = header + FILLER
    for text in version_strings:
        body += text + b"\x00" + FILLER
    return body


class TestCheckLibarchive(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = pathlib.Path(self._tmp.name)

    def stage(self, data: bytes, name: str = "libarchive.so.13") -> pathlib.Path:
        path = self.dir / name
        path.write_bytes(data)
        return path

    def run_check(self, path: pathlib.Path) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(CHECK), str(path)], capture_output=True, text=True, check=False
        )

    def test_older_version_is_printed_and_rejected(self) -> None:
        result = self.run_check(self.stage(fake_library(b"libarchive 3.8.10")))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.10", result.stdout)
        self.assertIn("too old", result.stdout)

    def test_much_older_version_is_rejected(self) -> None:
        result = self.run_check(self.stage(fake_library(b"libarchive 3.7.2")))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libarchive 3.7.2", result.stdout)

    def test_floor_version_is_printed_and_accepted(self) -> None:
        result = self.run_check(self.stage(fake_library(b"libarchive 3.8.11")))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.11", result.stdout)
        self.assertIn("ok", result.stdout)

    def test_later_version_is_compared_numerically(self) -> None:
        # 3.10.0 is later than 3.8.11 even though "10" < "8" as text.
        for text in (b"libarchive 3.8.12", b"libarchive 3.10.0", b"libarchive 4.0.0"):
            with self.subTest(text=text):
                result = self.run_check(self.stage(fake_library(text)))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(text.decode(), result.stdout)

    def test_prerelease_of_floor_is_rejected(self) -> None:
        result = self.run_check(self.stage(fake_library(b"libarchive 3.8.11dev")))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.11dev", result.stdout)

    def test_repeated_identical_string_is_not_ambiguous(self) -> None:
        data = fake_library(b"libarchive 3.8.11", b"libarchive 3.8.11")
        result = self.run_check(self.stage(data))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_file_name_does_not_supply_the_version(self) -> None:
        # The name claims a new release; the bytes carry an old one.
        path = self.stage(fake_library(b"libarchive 3.8.10"), name="libarchive.so.3.8.11")
        result = self.run_check(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.10", result.stdout)

    def test_missing_file_fails(self) -> None:
        result = self.run_check(self.dir / "absent.so")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cannot read", result.stderr)

    def test_no_version_string_fails(self) -> None:
        result = self.run_check(self.stage(fake_library()))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("no libarchive version string", result.stderr)

    def test_unterminated_version_string_is_not_evidence(self) -> None:
        data = ELF_HEADER + FILLER + b"libarchive 3.8.11.so" + FILLER
        result = self.run_check(self.stage(data))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_two_different_versions_fail_as_ambiguous(self) -> None:
        data = fake_library(b"libarchive 3.8.10", b"libarchive 3.8.11")
        result = self.run_check(self.stage(data))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("more than one", result.stderr)
        self.assertIn("3.8.10", result.stderr)
        self.assertIn("3.8.11", result.stderr)

    def test_non_elf_file_fails(self) -> None:
        data = fake_library(b"libarchive 3.8.11", header=b"libarchive 3.8.11\n")
        result = self.run_check(self.stage(data))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("not an ELF file", result.stderr)

    def test_empty_file_fails(self) -> None:
        result = self.run_check(self.stage(b""))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
