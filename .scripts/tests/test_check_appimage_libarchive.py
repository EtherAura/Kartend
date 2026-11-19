#!/usr/bin/env python3
"""Tests for packaging/appimage/check-libarchive.py.

The check reads the libarchive version string out of one staged library file
and fails below the approved floor. These tests run the script's command line
via subprocess against small temporary files that only look like an ELF
library (the ELF magic, some filler, and a NUL-terminated version string); no
real library is committed, loaded or run. They show that an older version is
printed and rejected, the floor and a later version are printed and accepted,
and a missing file or missing, ambiguous or non-ELF version evidence fails.

The --appdir cases run twice: once with real symlinks through the command line
(where the host allows unprivileged symlinks, as Linux does), and once with
links modelled in a table and the same main() run in-process, so that a host
without symlink rights (Windows without Developer Mode) still runs every case.

stdlib unittest — pytest is not available in CI. Run directly
(`python3 .scripts/tests/test_check_appimage_libarchive.py`) or via
`python3 -m unittest`.
"""
from __future__ import annotations

import contextlib
import functools
import importlib.util
import io
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

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


@functools.lru_cache(maxsize=None)
def host_can_symlink() -> bool:
    """Whether this host lets an unprivileged process create a symlink.

    Windows refuses without Developer Mode or elevation; Linux allows it.
    """
    with tempfile.TemporaryDirectory() as tmp:
        try:
            os.symlink("target", os.path.join(tmp, "probe"))
        except (OSError, NotImplementedError):
            return False
    return True


def load_checker():
    spec = importlib.util.spec_from_file_location("check_libarchive", CHECK)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def link_key(path: str) -> str:
    return os.path.normcase(os.path.abspath(path))


class ModelledLinkOs:
    """The os module, except that lstat and readlink also see modelled links.

    links maps link_key(path) to the link's target text. Nothing is written to
    disk for a modelled link; every other call goes to the real os module.
    """

    def __init__(self, links: dict[str, str]) -> None:
        self._links = links

    def __getattr__(self, name: str):
        return getattr(os, name)

    def lstat(self, path):
        if link_key(path) in self._links:
            return os.stat_result((stat.S_IFLNK | 0o777,) + (0,) * 9)
        return os.lstat(path)

    def readlink(self, path):
        key = link_key(path)
        if key in self._links:
            return self._links[key]
        return os.readlink(path)


def refuse_symlink(*args, **kwargs):
    raise AssertionError("the modelled-link fixture must not create a filesystem symlink")


class AppDirCases:
    """--appdir: the usr/lib/libarchive.so.13 entry build-appimage.sh requires.

    A subclass sets modelled_links. False runs the script's command line via
    subprocess against real symlinks. True records each link in a table, runs
    the same main() in-process with the checker's os module wrapped so that
    lstat and readlink report the table's links, and makes os.symlink fail, so
    no filesystem symlink is created. The cases and assertions are the same.
    """

    modelled_links = False

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.base = pathlib.Path(self._tmp.name)
        self.appdir = self.base / "Staged App Dir"
        self.lib = self.appdir / "usr" / "lib"
        self.lib.mkdir(parents=True)
        self.entry = self.lib / "libarchive.so.13"
        if self.modelled_links:
            self.links: dict[str, str] = {}
            patcher = mock.patch("os.symlink", refuse_symlink)
            patcher.start()
            self.addCleanup(patcher.stop)
            self.addCleanup(self.assert_no_filesystem_links)

    def link(self, path: pathlib.Path, target) -> None:
        if not self.modelled_links:
            path.symlink_to(target)
            return
        self.assertFalse(os.path.lexists(path), path)
        parent = os.path.realpath(path.parent)
        self.links[link_key(os.path.join(parent, path.name))] = str(target)

    def assert_no_filesystem_links(self) -> None:
        for directory, names, files in os.walk(self.base):
            for name in names + files:
                self.assertFalse(os.path.islink(os.path.join(directory, name)), name)

    def run_appdir(self, *extra: str) -> subprocess.CompletedProcess:
        argv = ["--appdir", str(self.appdir), *extra]
        if not self.modelled_links:
            return subprocess.run(
                [sys.executable, str(CHECK), *argv],
                capture_output=True,
                text=True,
                check=False,
            )
        checker = load_checker()
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(checker, "os", ModelledLinkOs(self.links)):
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                try:
                    code = checker.main(argv)
                except SystemExit as exit_:
                    code = exit_.code
        return subprocess.CompletedProcess(argv, code, out.getvalue(), err.getvalue())

    def outside(self, data: bytes) -> pathlib.Path:
        other = self.base / "outside" / "usr" / "lib"
        other.mkdir(parents=True)
        path = other / "libarchive.so.13.9.0"
        path.write_bytes(data)
        return path

    def test_staged_older_version_is_printed_and_rejected(self) -> None:
        self.entry.write_bytes(fake_library(b"libarchive 3.8.10"))
        result = self.run_appdir()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.10", result.stdout)
        self.assertIn("too old", result.stdout)
        self.assertIn("usr/lib/libarchive.so.13", result.stdout)

    def test_soname_link_to_floor_version_is_printed_and_accepted(self) -> None:
        (self.lib / "libarchive.so.13.8.11").write_bytes(fake_library(b"libarchive 3.8.11"))
        self.link(self.entry, "libarchive.so.13.8.11")
        result = self.run_appdir()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.11", result.stdout)
        self.assertIn("ok", result.stdout)
        self.assertIn("Staged App Dir", result.stdout)
        self.assertIn("-> usr/lib/libarchive.so.13.8.11", result.stdout)

    def test_link_through_parent_inside_appdir_is_followed(self) -> None:
        real = self.appdir / "usr" / "lib64 with space"
        real.mkdir()
        (real / "libarchive.so.13.8.11").write_bytes(fake_library(b"libarchive 3.8.11"))
        self.link(self.entry, "../lib64 with space/libarchive.so.13.8.11")
        result = self.run_appdir()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("lib64 with space/libarchive.so.13.8.11", result.stdout)

    def test_link_target_bytes_decide_not_entry_name(self) -> None:
        (self.lib / "libarchive.so.13.8.11").write_bytes(fake_library(b"libarchive 3.8.10"))
        self.link(self.entry, "libarchive.so.13.8.11")
        result = self.run_appdir()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("libarchive 3.8.10", result.stdout)

    def test_missing_entry_fails(self) -> None:
        (self.lib / "libarchive.so.3.8.11").write_bytes(fake_library(b"libarchive 3.8.11"))
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("has no usr/lib/libarchive.so.13", result.stderr)
        self.assertNotIn("ok", result.stdout)

    def test_missing_appdir_fails(self) -> None:
        self.appdir = self.base / "absent"
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_dangling_link_fails(self) -> None:
        self.link(self.entry, "libarchive.so.13.8.11")
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("dangling", result.stderr)

    def test_relative_link_out_of_appdir_to_newer_file_fails(self) -> None:
        target = self.outside(fake_library(b"libarchive 3.9.0"))
        self.link(self.entry, "../../../outside/usr/lib/" + target.name)
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("outside the AppDir", result.stderr)
        self.assertNotIn("3.9.0", result.stdout)

    def test_absolute_link_to_newer_file_fails(self) -> None:
        target = self.outside(fake_library(b"libarchive 3.9.0"))
        self.link(self.entry, target)
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("outside the AppDir", result.stderr)
        self.assertNotIn("3.9.0", result.stdout)

    def test_linked_lib_directory_out_of_appdir_fails(self) -> None:
        self.outside(fake_library(b"libarchive 3.9.0"))
        (self.base / "outside" / "usr" / "lib" / "libarchive.so.13").write_bytes(
            fake_library(b"libarchive 3.9.0")
        )
        self.entry.parent.rename(self.appdir / "usr" / "lib.old")
        self.link(self.appdir / "usr" / "lib", "../../outside/usr/lib")
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("outside the AppDir", result.stderr)

    def test_link_loop_fails(self) -> None:
        self.link(self.entry, "libarchive.so.13.loop")
        self.link(self.lib / "libarchive.so.13.loop", "libarchive.so.13")
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("too many levels", result.stderr)

    def test_directory_entry_fails(self) -> None:
        self.entry.mkdir()
        result = self.run_appdir()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("not a regular file", result.stderr)

    def test_appdir_and_library_together_are_refused(self) -> None:
        self.entry.write_bytes(fake_library(b"libarchive 3.8.11"))
        result = self.run_appdir(str(self.entry))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)


class TestCheckLibarchiveAppDir(AppDirCases, unittest.TestCase):
    """Real symlinks and the real command line where the host allows them.

    A host that refuses unprivileged symlinks (Windows without Developer Mode)
    runs the same cases with modelled links instead.
    """

    def setUp(self) -> None:
        self.modelled_links = not host_can_symlink()
        super().setUp()


class TestCheckLibarchiveAppDirModelledLinks(AppDirCases, unittest.TestCase):
    """The same cases with modelled links, on every host, Linux included."""

    modelled_links = True

    def test_fixture_cannot_create_a_filesystem_link(self) -> None:
        with self.assertRaises(AssertionError):
            self.entry.symlink_to("libarchive.so.13.8.11")
        self.assertFalse(os.path.lexists(self.entry))


if __name__ == "__main__":
    unittest.main()
