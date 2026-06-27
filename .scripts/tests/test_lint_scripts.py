#!/usr/bin/env python3
"""Tests for the repo-gatekeeper lint scripts (Kartend audit b7phb).

check-layering.py / check-test-mapping.py gate the codebase but were themselves
unguarded — a regression that makes one silently pass, or crash, would go
unnoticed. These tests run each script via subprocess with KARTEND_REPO pointed
at (a) the real repo (smoke: must still pass) and (b) synthetic fixture trees
(must fail on a seeded violation, or error on a malformed tree), exercising the
pass/fail contract.

stdlib unittest — pytest is not available in CI. Run directly
(`python3 .scripts/tests/test_lint_scripts.py`) or via `python3 -m unittest`.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = pathlib.Path(__file__).resolve().parent.parent
REPO = SCRIPTS.parent
TEST_MAPPING = SCRIPTS / "check-test-mapping.py"
LAYERING = SCRIPTS / "check-layering.py"


def run_script(script: pathlib.Path, root: pathlib.Path) -> subprocess.CompletedProcess:
    """Run a check script with REPO overridden to `root` via KARTEND_REPO."""
    env = dict(os.environ, KARTEND_REPO=str(root))
    return subprocess.run(
        [sys.executable, str(script)], env=env, capture_output=True, text=True, check=False
    )


def make_tree(base: pathlib.Path, dirs: list[str], files: dict[str, str]) -> None:
    for d in dirs:
        (base / d).mkdir(parents=True, exist_ok=True)
    for rel, content in files.items():
        p = base / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")


class TestCheckTestMapping(unittest.TestCase):
    REQUIRED_DIRS = ["src/modules", "tests/modules", "src/utils", "tests/utils"]

    def _clean_fixture(self, base: pathlib.Path) -> None:
        # A src/modules feature with a matching tests/modules folder + a
        # registered test, plus the four required dirs.
        make_tree(
            base,
            self.REQUIRED_DIRS,
            {
                "src/modules/grp/feat/feat.cpp": "// impl\n",
                "tests/modules/feat/test_feat.cpp": "// test\n",
                "tests/CMakeLists.txt": (
                    "kartend_add_test(NAME Feat SOURCES modules/feat/test_feat.cpp)\n"
                ),
            },
        )

    def test_clean_tree_passes(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            self._clean_fixture(base)
            r = run_script(TEST_MAPPING, base)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_missing_test_folder_fails(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            # A src feature with no tests/modules/<feature>/ → check_modules fails.
            make_tree(base, self.REQUIRED_DIRS, {"src/modules/grp/feat/feat.cpp": "// impl\n"})
            r = run_script(TEST_MAPPING, base)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("without a tests/modules folder", r.stdout)

    def test_unregistered_test_fails(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            self._clean_fixture(base)
            # A second test file not referenced by any CMake → registration fails.
            (base / "tests/modules/feat/test_orphan.cpp").write_text("// test\n", encoding="utf-8")
            r = run_script(TEST_MAPPING, base)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("not registered", r.stdout)

    def test_missing_required_dir_errors(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            # Omit tests/utils → main() returns 2 (malformed tree).
            make_tree(base, ["src/modules", "tests/modules", "src/utils"], {})
            r = run_script(TEST_MAPPING, base)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_real_repo_passes(self) -> None:
        r = run_script(TEST_MAPPING, REPO)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


class TestCheckLayering(unittest.TestCase):
    def test_real_repo_passes(self) -> None:
        # check-layering is tightly coupled to a near-full repo, so a minimal
        # violating fixture is fragile; the regression guard here is that the
        # script still runs and passes on the canonical tree.
        r = run_script(LAYERING, REPO)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
