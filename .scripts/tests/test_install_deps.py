#!/usr/bin/env python3
"""Offline process-level tests for .github/actions/install-deps (Kartend-b128j).

The "Install apt packages" step's real `run:` script is extracted from
action.yml and executed exactly as a GitHub composite step runs it
(`bash --noprofile --norc -eo pipefail <file>`), with `${{ inputs.packages }}`
substituted and the step's own env block (APT_OPTS) parsed from the YAML.

Only `sudo` and `apt-get` are faked, and only inside a per-test temporary
directory placed first on PATH: the fake sudo never elevates (it just execs
`env "$@"`), and the fake apt-get never touches the network or the host — it
logs its argv and follows a scripted per-call plan ("ok", "hang", or an exit
code). The REAL coreutils `timeout` enforces the wall-clock bounds, so a
"hang" is genuinely killed and the elapsed time is genuinely measured.

stdlib unittest — pytest is not available in CI. Run directly
(`python3 .scripts/tests/test_install_deps.py`); ctest runs it as
InstallDepsActionTest. KARTEND_INSTALL_DEPS_ACTION overrides the action path.
"""
from __future__ import annotations

import json
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest

SCRIPTS = pathlib.Path(__file__).resolve().parent.parent
REPO = SCRIPTS.parent
ACTION = pathlib.Path(
    os.environ.get(
        "KARTEND_INSTALL_DEPS_ACTION", REPO / ".github" / "actions" / "install-deps" / "action.yml"
    )
)
STEP_NAME = "Install apt packages"
PACKAGES = "cmake ninja-build qt6-base-dev libqt6sql6-sqlite"

FAKE_SUDO = """#!/bin/sh
# Test double: never elevates. `sudo VAR=val cmd args` -> `env VAR=val cmd args`.
exec env "$@"
"""

FAKE_APT_GET = """#!/usr/bin/env python3
# Test double: no network, no dpkg, no host changes. Logs argv, then follows
# the scripted plan for this call index.
import json, os, sys
log = os.environ["FAKE_APT_LOG"]
with open(os.environ["FAKE_APT_PLAN"], encoding="utf-8") as f:
    plan = json.load(f)
with open(log, "a", encoding="utf-8") as f:
    f.write(json.dumps({"argv": sys.argv[1:],
                        "frontend": os.environ.get("DEBIAN_FRONTEND")}) + "\\n")
with open(log, encoding="utf-8") as f:
    index = sum(1 for _ in f) - 1
action = plan[index] if index < len(plan) else 99
if action == "ok":
    sys.exit(0)
if action == "hang":
    # exec so the real `timeout` kills this very process (no orphaned child
    # holding the captured stdout pipe open).
    os.execvp("sleep", ["sleep", "60"])
sys.exit(int(action))
"""


def _step_lines(text: str) -> list[str]:
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == f"- name: {STEP_NAME}":
            indent = len(line) - len(line.lstrip()) + 2
            out = []
            for nxt in lines[i + 1 :]:
                if nxt.strip() and len(nxt) - len(nxt.lstrip()) < indent:
                    break
                if nxt.strip().startswith("- ") and len(nxt) - len(nxt.lstrip()) == indent - 2:
                    break
                out.append(nxt)
            return out
    raise AssertionError(f"step {STEP_NAME!r} not found in {ACTION}")


def _block(lines: list[str], key_line: str) -> list[str]:
    """Lines of the indented block under the first line equal to key_line."""
    for i, line in enumerate(lines):
        if line.strip() == key_line:
            indent = len(line) - len(line.lstrip())
            out = []
            for nxt in lines[i + 1 :]:
                if nxt.strip() and len(nxt) - len(nxt.lstrip()) <= indent:
                    break
                out.append(nxt)
            while out and not out[-1].strip():
                out.pop()
            return out
    raise AssertionError(f"{key_line!r} not found in step {STEP_NAME!r}")


def load_step() -> tuple[str, str]:
    """(run script, APT_OPTS) of the install step, read from the real action."""
    step = _step_lines(ACTION.read_text(encoding="utf-8"))
    run = _block(step, "run: |")
    margin = min(len(line) - len(line.lstrip()) for line in run if line.strip())
    script = "\n".join(line[margin:] for line in run) + "\n"
    env = _block(step, "env:")
    apt_opts = " ".join(line.strip() for line in _block(env, "APT_OPTS: >-"))
    return script, apt_opts


class InstallDepsStepTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = pathlib.Path(self._tmp.name)
        self.bin = self.tmp / "bin"
        self.bin.mkdir()
        for name, body in (("sudo", FAKE_SUDO), ("apt-get", FAKE_APT_GET)):
            path = self.bin / name
            path.write_text(body, encoding="utf-8")
            path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        self.log = self.tmp / "apt.log"
        self.plan = self.tmp / "plan.json"
        script, self.apt_opts = load_step()
        self.assertIn("${{ inputs.packages }}", script)
        script = script.replace("${{ inputs.packages }}", PACKAGES)
        self.assertNotIn("${{", script, "unhandled GitHub expression in the run block")
        self.script = self.tmp / "step.sh"
        self.script.write_text(script, encoding="utf-8")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def run_step(self, plan: list, budget: int = 3, recommends: str = ""):
        self.plan.write_text(json.dumps(plan), encoding="utf-8")
        path = f"{self.bin}{os.pathsep}{os.environ.get('PATH', '')}"
        # Guard: the doubles, never the host's sudo/apt-get, must resolve.
        self.assertEqual(shutil.which("sudo", path=path), str(self.bin / "sudo"))
        self.assertEqual(shutil.which("apt-get", path=path), str(self.bin / "apt-get"))
        env = dict(
            os.environ,
            PATH=path,
            APT_OPTS=self.apt_opts,
            RECOMMENDS_OPT=recommends,
            INSTALL_TIMEOUT=str(budget),
            FAKE_APT_LOG=str(self.log),
            FAKE_APT_PLAN=str(self.plan),
        )
        start = time.monotonic()
        result = subprocess.run(
            ["bash", "--noprofile", "--norc", "-eo", "pipefail", str(self.script)],
            env=env,
            capture_output=True,
            text=True,
            check=False,
            timeout=budget + 30,
        )
        elapsed = time.monotonic() - start
        calls = []
        if self.log.exists():
            calls = [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()]
        return result, calls, elapsed

    @staticmethod
    def is_download(call: dict) -> bool:
        return "--download-only" in call["argv"]

    def assert_forwarded(self, call: dict, recommends: str = "") -> None:
        argv = call["argv"]
        self.assertEqual(call["frontend"], "noninteractive")
        expected_opts = self.apt_opts.split()
        self.assertEqual(argv[: len(expected_opts)], expected_opts)
        for opt in ("Acquire::Retries=3", "Acquire::http::Timeout=30", "Acquire::https::Timeout=30"):
            self.assertIn(opt, expected_opts)
        self.assertIn("install", argv)
        self.assertIn("-y", argv)
        self.assertEqual(argv[-len(PACKAGES.split()) :], PACKAGES.split())
        self.assertEqual("--no-install-recommends" in argv, recommends == "--no-install-recommends")

    def test_healthy_install_downloads_then_installs_once(self) -> None:
        result, calls, _ = self.run_step(["ok", "ok"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([self.is_download(c) for c in calls], [True, False])
        for call in calls:
            self.assert_forwarded(call)

    def test_options_and_packages_forwarded_with_no_recommends(self) -> None:
        result, calls, _ = self.run_step(["ok", "ok"], recommends="--no-install-recommends")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assert_forwarded(call, "--no-install-recommends")

    def test_transient_acquisition_timeout_is_retried_once(self) -> None:
        result, calls, elapsed = self.run_step(["hang", "ok", "ok"], budget=3)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([self.is_download(c) for c in calls], [True, True, False])
        for call in calls:
            self.assert_forwarded(call)
        self.assertIn("::warning", result.stdout)
        self.assertLess(elapsed, 3 + 1.0)

    def test_acquisition_timeout_exhaustion_fails_without_installing(self) -> None:
        result, calls, elapsed = self.run_step(["hang", "hang", "ok"], budget=3)
        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        self.assertEqual(len(calls), 2)
        self.assertTrue(all(self.is_download(c) for c in calls), calls)
        self.assertIn("::error title=apt install timed out::", result.stdout)
        self.assertLess(elapsed, 3 + 1.0)

    def test_permanent_acquisition_failure_is_not_retried(self) -> None:
        result, calls, _ = self.run_step([100, "ok", "ok"])
        self.assertEqual(result.returncode, 100, result.stdout + result.stderr)
        self.assertEqual(len(calls), 1)
        self.assertTrue(self.is_download(calls[0]))

    def test_permanent_failure_on_retry_is_propagated(self) -> None:
        result, calls, _ = self.run_step(["hang", 100, "ok"], budget=3)
        self.assertEqual(result.returncode, 100, result.stdout + result.stderr)
        self.assertEqual(len(calls), 2)
        self.assertTrue(all(self.is_download(c) for c in calls), calls)

    def test_install_failure_is_fatal_and_not_retried(self) -> None:
        result, calls, _ = self.run_step(["ok", 100, "ok"])
        self.assertEqual(result.returncode, 100, result.stdout + result.stderr)
        self.assertEqual([self.is_download(c) for c in calls], [True, False])

    def test_install_after_retry_hangs_within_the_one_budget(self) -> None:
        # Retry used, then the dpkg phase hangs: the step must still die
        # inside the caller's single budget, attempting the install once.
        result, calls, elapsed = self.run_step(["hang", "ok", "hang", "ok"], budget=3)
        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        self.assertEqual([self.is_download(c) for c in calls], [True, True, False])
        self.assertIn("::error title=apt install timed out::", result.stdout)
        self.assertGreater(elapsed, 3 - 0.5)
        self.assertLess(elapsed, 3 + 1.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
