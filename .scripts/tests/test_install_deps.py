#!/usr/bin/env python3
"""Tests for .github/actions/install-deps/install-packages.sh (Kartend-wym32, Kartend-b128j).

Every Linux job installs its dependencies through that script, and the thing it
guards against (a mirror that hangs on some files) cannot be had on demand. So
these tests run the real script with a stand-in apt-get whose download hangs
while a named mirror is first in the list, and assert what the script ran, in
which order, the list it left and its exit status.

stdlib unittest — pytest is not available in CI. Run directly
(`python3 .scripts/tests/test_install_deps.py`) or via `python3 -m unittest`.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
SCRIPT = REPO / ".github" / "actions" / "install-deps" / "install-packages.sh"
A, B, C = "http://a.example/ubuntu/", "https://b.example/ubuntu/", "http://c.example/ubuntu/"

# The stand-in apt-get: appends "<first mirror> <its arguments>" to calls.log. A download hangs while the first
# mirror is one named in $HANGS (or always, with no list, when $HANGS says "nolist"); $FAIL_DOWNLOAD and
# $FAIL_INSTALL make that phase exit 100.
APT_GET = r"""#!/bin/sh
here="$(dirname "$0")"
first="$(awk '$1 ~ /^https?:/ { print $1; exit }' "$APT_MIRRORS_FILE" 2>/dev/null)"
echo "${first:-nolist} frontend=${DEBIAN_FRONTEND:-unset} $*" >> "$here/calls.log"
case " $* " in
*" --download-only "*)
    [ -z "${FAIL_DOWNLOAD:-}" ] || exit 100
    for mirror in ${HANGS:-}; do
        [ "$mirror" = "${first:-nolist}" ] && exec sleep 60
    done ;;
*)
    [ -z "${FAIL_INSTALL:-}" ] || exit 100 ;;
esac
exit 0
"""


class InstallPackages(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.list = self.dir / "apt-mirrors.txt"
        (self.dir / "apt-get").write_text(APT_GET)
        (self.dir / "apt-get").chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def write_list(self, *urls: str) -> str:
        text = "".join(f"{url}\tpriority:{n}\n" for n, url in enumerate(urls, 1))
        self.list.write_text(text)
        return text

    def run_script(self, *packages: str, **env: str) -> subprocess.CompletedProcess:
        settings = {"APT_MIRRORS_FILE": str(self.list), "APT_GET": str(self.dir / "apt-get"), "APT_SUDO": "",
                    "APT_MIRROR_FALLBACKS": "", "INSTALL_TIMEOUT": "30", "DOWNLOAD_ATTEMPT_SECONDS": "1"}
        settings.update(env)
        return subprocess.run(["bash", str(SCRIPT), *packages], env=dict(os.environ, **settings), capture_output=True,
                              text=True, check=False, timeout=120)

    def calls(self) -> list[str]:
        log = self.dir / "calls.log"
        return log.read_text().splitlines() if log.exists() else []

    def urls(self) -> list[str]:
        return [line.split("\t")[0] for line in self.list.read_text().splitlines()]

    def test_healthy_mirror_one_download_one_install_and_the_list_is_left(self):
        before = self.write_list(A, B)
        done = self.run_script("cmake", "ninja-build", APT_OPTS="-o Acquire::Retries=3", RECOMMENDS_OPT="--no-install-recommends")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        flags = "frontend=noninteractive -o Acquire::Retries=3 --no-install-recommends install -y"
        self.assertEqual(self.calls(), [f"{A} {flags} --download-only cmake ninja-build", f"{A} {flags} cmake ninja-build"])
        self.assertEqual(self.list.read_text(), before)
        self.assertNotIn("::warning", done.stdout)

    def test_a_mirror_that_hangs_is_left_behind_and_the_download_carries_on(self):
        self.write_list(A, B)
        done = self.run_script("cmake", HANGS=A)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual([call.split()[0] for call in self.calls()], [A, B, B])          # download, download, install
        self.assertEqual([("--download-only" in call) for call in self.calls()], [True, True, False])
        self.assertEqual(self.list.read_text(), f"{B}\tpriority:1\n{A}\tpriority:2\n")
        self.assertIn(f"::warning title=apt mirror did not deliver::The package download did not finish in 1s on {A}; carrying on with {B}", done.stdout)

    def test_the_public_fallbacks_join_the_list_in_front_of_the_mirror_that_hung(self):
        self.write_list(A)
        done = self.run_script("cmake", HANGS=A, APT_MIRROR_FALLBACKS=f"{C} {A.replace('http://', 'https://').rstrip('/')}")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.urls(), [C, A])            # the second fallback is A itself, by another spelling

    def test_two_mirrors_that_hang_are_both_left_behind(self):
        self.write_list(A, B, C)
        done = self.run_script("cmake", HANGS=f"{A} {B}")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual([call.split()[0] for call in self.calls()], [A, B, C, C])
        self.assertEqual(self.urls(), [C, A, B])

    def test_when_every_mirror_hangs_the_step_fails_as_a_labelled_timeout_and_installs_nothing(self):
        self.write_list(A, B)
        done = self.run_script("cmake", HANGS=f"{A} {B}", INSTALL_TIMEOUT="15")     # 10 s for downloads, 5 kept back
        self.assertEqual(done.returncode, 124, done.stdout + done.stderr)
        self.assertTrue(all("--download-only" in call for call in self.calls()), self.calls())
        self.assertGreaterEqual(len(self.calls()), 3)                               # A, B and A again
        self.assertIn("::error title=apt install timed out::Dependency install exceeded 15s — the apt mirror is crawling (Kartend-wym32)", done.stdout)

    def test_a_single_mirror_that_hangs_ends_after_its_one_attempt(self):
        before = self.write_list(A)
        done = self.run_script("cmake", HANGS=A)
        self.assertEqual(done.returncode, 124, done.stdout + done.stderr)
        self.assertEqual(len(self.calls()), 1)
        self.assertEqual(self.list.read_text(), before)
        self.assertIn("there is no other mirror to move on to", done.stdout)

    def test_without_a_mirror_list_it_still_installs_and_a_hang_is_a_timeout(self):
        done = self.run_script("cmake")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(self.calls()), 2)
        done = self.run_script("cmake", HANGS="nolist")
        self.assertEqual(done.returncode, 124, done.stdout + done.stderr)
        self.assertFalse(self.list.exists())

    def test_a_list_that_cannot_be_written_is_a_timeout_not_a_crash(self):
        before = self.write_list(A, B)
        done = self.run_script("cmake", HANGS=A, APT_SUDO="false")       # "false install ..." fails; so does "false env apt-get"
        self.assertNotEqual(done.returncode, 0)
        self.assertEqual(self.list.read_text(), before)

    def test_apts_own_download_failure_is_not_retried_and_nothing_is_installed(self):
        before = self.write_list(A, B)
        done = self.run_script("cmake", FAIL_DOWNLOAD="1")
        self.assertEqual(done.returncode, 100, done.stdout + done.stderr)
        self.assertEqual(len(self.calls()), 1)
        self.assertEqual(self.list.read_text(), before)
        self.assertNotIn("::error title=apt install timed out", done.stdout)

    def test_a_failed_install_is_reported_as_it_is_and_never_run_twice(self):
        self.write_list(A, B)
        done = self.run_script("cmake", FAIL_INSTALL="1")
        self.assertEqual(done.returncode, 100, done.stdout + done.stderr)
        self.assertEqual([("--download-only" in call) for call in self.calls()], [True, False])

    def test_no_package_is_a_usage_error(self):
        done = self.run_script()
        self.assertEqual(done.returncode, 2)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
