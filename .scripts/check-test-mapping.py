#!/usr/bin/env python3
"""Module → test directory mapping lint for Kartend.

docs/testing.md documents that every `src/modules/<group>/<feature>/`
feature owns a matching `tests/modules/<feature>/` folder, with the
intermediate `behavior/data/input/media` group level dropped. Without a
machine check, the two trees drift silently when a new module is added
without a test folder, or when a test folder outlives its module.

This lint walks both trees and fails when either direction breaks:

  - A `src/modules/<group>/<feature>/` with no `tests/modules/<feature>/`
    (unless the feature is in INTEGRATION_ONLY — UI-coordinator modules
    whose coverage lives entirely in tests/integration/).
  - A `tests/modules/<feature>/` with no `src/modules/<group>/<feature>/`.

Pairs with .scripts/check-layering.py and .scripts/check-singleshot-comments.py
in the maintenance-check CI job.

Exit status: 0 = clean, 1 = drift detected, 2 = usage error.
"""

from __future__ import annotations

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC_MODULES = REPO / "src" / "modules"
TESTS_MODULES = REPO / "tests" / "modules"

# Features whose coverage lives in tests/integration/ (UI-coordinator
# managers that pull the full kartend_lib closure and can't be tested
# in isolation). New entries here need a justification — every other
# module should have a per-feature tests/modules/<feature>/ folder.
INTEGRATION_ONLY: set[str] = {
    "application",  # src/modules/behavior/application — ApplicationManager
                    # exercised by tests/integration/test_applicationmanager_lifecycle.cpp
    # DetailsPaneManager moved out of src/modules/media/detailspane/ to
    # src/ui/controllers/detailspanemanager/ in Kartend-uk5z. It still lacks
    # unit tests (integration-only via test_eventmanager_detailspane /
    # test_detailspane_coverflow), but it's no longer a src/modules feature,
    # so no INTEGRATION_ONLY entry is needed.
}


def main() -> int:
    if not SRC_MODULES.is_dir():
        print(f"check-test-mapping: {SRC_MODULES} not found", file=sys.stderr)
        return 2
    if not TESTS_MODULES.is_dir():
        print(f"check-test-mapping: {TESTS_MODULES} not found", file=sys.stderr)
        return 2

    # src/modules/<group>/<feature> — one entry per leaf feature dir.
    src_features: dict[str, str] = {}  # feature -> group
    for group_dir in sorted(SRC_MODULES.iterdir()):
        if not group_dir.is_dir():
            continue
        for feat_dir in sorted(group_dir.iterdir()):
            if not feat_dir.is_dir():
                continue
            src_features[feat_dir.name] = group_dir.name

    # tests/modules/<feature> — single level (no group).
    test_features: set[str] = {
        d.name for d in TESTS_MODULES.iterdir() if d.is_dir()
    }

    missing_tests: list[tuple[str, str]] = []  # (feature, group)
    for feature, group in src_features.items():
        if feature in INTEGRATION_ONLY:
            continue
        if feature not in test_features:
            missing_tests.append((feature, group))

    orphan_tests: list[str] = []
    for feature in sorted(test_features):
        if feature not in src_features:
            orphan_tests.append(feature)

    if missing_tests or orphan_tests:
        if missing_tests:
            print("check-test-mapping: src/modules features without a tests/modules folder:")
            for feature, group in sorted(missing_tests):
                print(f"  src/modules/{group}/{feature}/  ->  tests/modules/{feature}/ (missing)")
            print(
                "\nFix: add tests/modules/{feature}/ with at least one test_*.cpp, "
                "or add the feature to INTEGRATION_ONLY in this script with a "
                "comment pointing to its integration test."
            )
        if orphan_tests:
            print("\ncheck-test-mapping: tests/modules folders with no matching src/modules feature:")
            for feature in orphan_tests:
                print(f"  tests/modules/{feature}/  ->  src/modules/**/{feature}/ (missing)")
            print(
                "\nFix: rename the test folder to match the renamed feature, "
                "or delete the test folder if the feature was removed."
            )
        return 1

    print(
        f"check-test-mapping: OK — {len(src_features)} src/modules features, "
        f"{len(test_features)} tests/modules folders aligned"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
