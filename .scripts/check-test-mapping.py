#!/usr/bin/env python3
"""Directory mapping lint for Kartend's test tree.

docs/dev/testing.md documents that every `src/modules/<group>/<feature>/`
feature owns a matching `tests/modules/<feature>/` folder, with the
intermediate `behavior/data/input/media` group level dropped. Without a
machine check, the two trees drift silently when a new module is added
without a test folder, or when a test folder outlives its module.

This lint runs three sibling checks:

  modules (bidirectional):
  - A `src/modules/<group>/<feature>/` with no `tests/modules/<feature>/`
    (unless the feature is in INTEGRATION_ONLY — UI-coordinator modules
    whose coverage lives entirely in tests/integration/).
  - A `tests/modules/<feature>/` with no `src/modules/<group>/<feature>/`.

  utils (one-directional, test → src only):
  - A `tests/utils/<cluster>/` with no `src/utils/<cluster>/`.
    The flat tests/utils/ was split into app/db/fs/text/view clusters
    mirroring src/utils/; this guards the mirror against drift
    (a renamed/removed src cluster leaving an orphaned test folder).
    The reverse direction is deliberately NOT enforced: many src/utils
    clusters are header-only or intentionally untested (e.g. threading/),
    so requiring a test folder per src cluster would be all false
    positives. Presence, not file coverage — so no per-file allowlist.

  registration (file-level):
  - Every `tests/**/test_*.cpp` must be referenced by a
    `tests/**/CMakeLists.txt`. The unit suite hand-writes an
    add_executable/add_test pair per file and the integration suite lists its
    sources in INTEGRATION_TEST_SOURCES, so a new test file that nobody wires
    in compiles into nothing and silently never runs. This is the backstop
    the folder-level checks above can't provide.

Pairs with .scripts/check-layering.py and .scripts/check-singleshot-comments.py
in the maintenance-check CI job.

Exit status: 0 = clean, 1 = drift detected, 2 = usage error.
"""

from __future__ import annotations

import os
import pathlib
import sys

# REPO is overridable via the KARTEND_REPO env var so the script can run against
# fixture trees in tests (Kartend audit b7phb); unset, it resolves to the repo
# this script lives in.
REPO = (
    pathlib.Path(os.environ["KARTEND_REPO"]).resolve()
    if os.environ.get("KARTEND_REPO")
    else pathlib.Path(__file__).resolve().parent.parent
)
SRC_MODULES = REPO / "src" / "modules"
TESTS_MODULES = REPO / "tests" / "modules"
SRC_UTILS = REPO / "src" / "utils"
TESTS_UTILS = REPO / "tests" / "utils"
SRC_CORE = REPO / "src" / "core"
SRC_CHROME = REPO / "src" / "chrome"
TESTS = REPO / "tests"

# Features whose coverage lives in tests/integration/ (UI-coordinator
# managers that pull the full kartend_lib closure and can't be tested
# in isolation). New entries here need a justification — every other
# module should have a per-feature tests/modules/<feature>/ folder.
INTEGRATION_ONLY: set[str] = {
    # ApplicationManager (src/modules/behavior/application) is the top-level
    # orchestrator that constructs and wires every other manager + the
    # ApplicationContext seed. A pure-unit test isn't viable because its
    # constructor pulls the full kartend_lib closure (database, settings,
    # scroll, interaction, …) and exercising the wiring is the test's point.
    # Covered by: tests/integration/test_applicationmanager_lifecycle.cpp.
    "application",
    # DetailsPaneManager moved out of src/modules/media/detailspane/ to
    # src/ui/controllers/detailspanemanager/ in Kartend-uk5z. It still lacks
    # unit tests (integration-only via test_eventmanager_detailspane /
    # test_detailspane_coverflow), but it's no longer a src/modules feature,
    # so no INTEGRATION_ONLY entry is needed.
}

# Source .cpp that are intentionally NOT unit-tested — headless-hostile
# QWidget paint/lifecycle shells and decode paths (docs/dev/testing.md: "test
# the extractable helpers, leave the QWidget shell"). Keyed by repo-relative
# path with a one-line reason. The advisory coverage report below segregates
# these from not-yet-triaged gaps, so anything still printed as an "unexempted
# gap" is a NEW, unexamined hole rather than a known-accepted one. Eligible
# paths are anywhere the report scans — src/chrome widgets AND src/modules
# widget shells (a feature folder's QWidget partials, e.g. a header/overlay
# widget, are the same test-the-helpers-not-the-shell category). src/core
# UI-coordinator gaps stay listed (a broader category, much of it
# integration-tested — left visible deliberately).
#
# THIS DICT IS A RATCHET, NOT A PARKING LOT (Kartend-pnlot.11). An exemption
# means "no extractable logic here today", not "never testable". So:
#
#   * When you touch an exempted file for any reason, try to lift its
#     testable logic into a free function or small helper, test that, and
#     DELETE the entry. tests/ui/widgets/test_itemplaceholdertint.cpp is the
#     worked example — tint math peeled out of a paint-only widget.
#   * Adding an entry needs a reason that says why nothing is liftable, not
#     just that the file is untested.
#
# The metric is the entry count, and it should trend down. It was 14 when
# this note was written (2026-07-25); +1 on 2026-07-31 when the coverage
# report was extended to src/modules (first module widget-shell entry). If
# you find it materially higher, the ratchet has slipped and that is worth a
# look rather than a shrug.
COVERAGE_EXEMPT: dict[str, str] = {
    "src/chrome/items/itemwidget.cpp": "QWidget item shell; paint/lifecycle, no extractable logic",
    "src/chrome/items/itemwidgetlifecycle.cpp": "QWidget lifecycle partial of itemwidget",
    "src/chrome/items/itemwidgetpaint.cpp": "QPainter paint partial of itemwidget",
    "src/chrome/items/itemwidgetplaceholder.cpp": "placeholder-paint partial of itemwidget",
    "src/chrome/items/itemwidgetsetters.cpp": "trivial property setters partial of itemwidget",
    "src/chrome/items/itemwidgetindicator.cpp": "QWidget overlay indicator; paint-only",
    "src/chrome/items/itemplaceholderrenderer.cpp": "QPainter placeholder renderer; paint-only",
    "src/chrome/items/coverflowgallerystrip.cpp": "QWidget coverflow strip; paint/layout shell",
    "src/chrome/items/coverflowwidget_artwork.cpp": "artwork paint partial of coverflowwidget",
    "src/chrome/items/coverflowwidget_input.cpp": "input-handling partial of coverflowwidget",
    "src/chrome/media/videopreviewwidget.cpp": "QWidget video surface; needs a live decoder",
    "src/chrome/media/videothumbnailextractor.cpp": "FFmpeg/GPU decode path; not headless-testable",
    "src/chrome/overlays/artworkpreviewoverlay.cpp": "QWidget hover overlay; paint/geometry shell",
    # Renamed from overlaylayermanager.cpp (2026-07-31). Its restack/reregister
    # contract IS asserted — by the OverlayZOrderRegistry cases hosted in
    # tests/ui/widgets/test_coverflowwidget.cpp — but the name-match report
    # cannot credit a cross-cutting suite, so the entry stays to keep the gap
    # list honest rather than to declare it untestable.
    "src/chrome/overlays/overlayzorderregistry.cpp": (
        "covered by the OverlayZOrderRegistry cases in test_coverflowwidget.cpp"
    ),
    # First src/modules widget-shell entry (the report scans src/modules too):
    "src/modules/input/scroll/listheaderwidget.cpp": (
        "QWidget list-header shell; column paint + drag-resize hit-testing"
    ),
}


def check_modules() -> bool:
    """src/modules <-> tests/modules, bidirectional. Returns True if clean."""
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
        return False

    print(
        f"check-test-mapping: OK — {len(src_features)} src/modules features, "
        f"{len(test_features)} tests/modules folders aligned"
    )
    return True


def check_utils() -> bool:
    """tests/utils -> src/utils, one-directional. Returns True if clean.

    Only orphan test clusters fail (a tests/utils/<cluster>/ with no
    src/utils/<cluster>/). src/utils clusters without a test folder are
    allowed — see the module docstring for why the reverse isn't enforced.
    """
    src_clusters: set[str] = {d.name for d in SRC_UTILS.iterdir() if d.is_dir()}
    test_clusters: set[str] = {d.name for d in TESTS_UTILS.iterdir() if d.is_dir()}

    orphan_tests = sorted(c for c in test_clusters if c not in src_clusters)
    if orphan_tests:
        print("check-test-mapping: tests/utils folders with no matching src/utils cluster:")
        for cluster in orphan_tests:
            print(f"  tests/utils/{cluster}/  ->  src/utils/{cluster}/ (missing)")
        print(
            "\nFix: rename the test folder to match the renamed src/utils cluster, "
            "or delete it if the cluster was removed."
        )
        return False

    print(
        f"check-test-mapping: OK — {len(test_clusters)} tests/utils folders all mirror a "
        f"src/utils cluster ({len(src_clusters)} src/utils clusters; untested clusters allowed)"
    )
    return True


def check_test_registration() -> bool:
    """Every tests/**/test_*.cpp must be referenced by a tests/**/CMakeLists.txt
    or a tests/cmake/*.cmake include() fragment.

    tests/CMakeLists.txt registers ~100 tests with hand-written
    add_executable/add_test triples and tests/integration/CMakeLists.txt lists
    its sources in INTEGRATION_TEST_SOURCES. A new test_*.cpp that nobody wires
    in compiles into nothing and silently never runs — this is the backstop.
    Returns True if clean.
    """
    cpps = sorted(TESTS.rglob("test_*.cpp"))
    # CMakeLists.txt *and* *.cmake include() fragments (Kartend-n1hpy.8): the
    # unit registrations were split out of the 1800-line tests/CMakeLists.txt
    # into ordered tests/cmake/*.cmake fragments, so a basename can now live in
    # either. Scan both, or every moved test reads as "unregistered".
    cmake_files = sorted(TESTS.rglob("CMakeLists.txt")) + sorted(TESTS.rglob("*.cmake"))

    def _strip_cmake_comments(text: str) -> str:
        # Drop `# ...` line comments so a test referenced ONLY in a comment is
        # not counted as registered — otherwise a commented-out add_executable
        # would mask a test that compiles into nothing, the exact silent-loss
        # this backstop exists to catch (Kartend audit DX-05).
        out = []
        for line in text.splitlines():
            hash_idx = line.find("#")
            out.append(line if hash_idx < 0 else line[:hash_idx])
        return "\n".join(out)

    cmake_text = _strip_cmake_comments(
        "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in cmake_files)
    )
    # Test-file basenames are unique across the tree, and every CMakeLists
    # reference ends in the literal basename (whether `sub/dir/test_x.cpp` or
    # `test_x.cpp`), so an exact-basename substring test is unambiguous and
    # prefix-collision-safe — `test_x.cpp` cannot match inside `test_xy.cpp`
    # because the trailing `.cpp` must line up. Comments are stripped first
    # (DX-05) so a `# test_x.cpp` mention can't pass an otherwise-unregistered test.
    unregistered = [str(p.relative_to(REPO)) for p in cpps if p.name not in cmake_text]

    if unregistered:
        print(
            "check-test-mapping: test_*.cpp files not registered in any "
            "tests/**/CMakeLists.txt:"
        )
        for rel in unregistered:
            print(f"  {rel}  ->  no add_executable / source-list entry; it silently never runs")
        print(
            "\nFix: register the test in tests/CMakeLists.txt (add_executable + "
            "target_link_libraries + add_test), or add it to "
            "INTEGRATION_TEST_SOURCES in tests/integration/CMakeLists.txt for a "
            "UI-coordinator test. Delete the file if it is dead."
        )
        return False

    print(f"check-test-mapping: OK — all {len(cpps)} test_*.cpp files are registered in CMake")
    return True


def report_core_chrome_coverage() -> None:
    """Non-fatal visibility report for src/core, src/chrome and src/modules
    (Kartend-tu2hq; extended to src/modules leaf features).

    src/core controllers and src/chrome widgets have no structural
    test-mapping rule at all, so missing coverage there is invisible.
    src/modules DOES have the per-feature folder rule above, but that only
    guarantees the folder exists — a 20-TU feature with one test file passes
    it, so per-file gaps inside a feature were just as invisible. This report
    lists each .cpp (for src/modules: every .cpp under each
    src/modules/<group>/<feature>/ leaf) with no matching
    tests/**/test_<stem>.cpp (a unit OR an integration test file named after
    it) so the gap is at least tracked.

    Intentionally advisory: it prints but never changes the exit status. Many
    of these are genuinely hard to unit-test (UI-coordinator code exercised via
    tests/integration/), so failing CI on the whole set would be all noise.
    Raising real coverage here is incremental work, not a gate.

    Files in COVERAGE_EXEMPT (headless-hostile QWidget shells) are listed under
    a separate "[exempt]" bucket with their reason, so anything still printed
    as an "unexempted gap" is a NEW, unexamined hole — not a known-accepted one.
    A COVERAGE_EXEMPT entry that no longer matches an untested .cpp (renamed,
    removed, or newly tested) is flagged as stale so the allowlist can't rot.
    Both the bucketing and the stale check stay advisory.
    """
    test_stems = {p.stem for p in TESTS.rglob("test_*.cpp")}  # e.g. "test_foo"

    def covered(cpp: pathlib.Path) -> bool:
        return f"test_{cpp.stem}" in test_stems

    # DX-05/T-02: make the name-match limitation explicit so a green count is
    # not over-trusted. This report credits src/foo.cpp iff a test_foo.cpp
    # exists by name — it does NOT credit cross-cutting suites that exercise
    # foo under a different name, and does NOT verify the test asserts anything
    # about foo. Read 'covered' as "has a same-named test file", not "tested".
    print(
        "check-test-mapping: NOTE — the coverage reports below are name-match "
        "only (src/foo.cpp <-> test_foo.cpp); they do not credit cross-cutting "
        "suites or verify assertions."
    )

    seen_exempt: set[str] = set()
    for area, label in (
        (SRC_CORE, "src/core"),
        (SRC_CHROME, "src/chrome"),
        (SRC_MODULES, "src/modules"),
    ):
        if not area.is_dir():
            continue
        cpps = sorted(area.rglob("*.cpp"))
        untested: list[pathlib.Path] = []
        exempt: list[pathlib.Path] = []
        for p in cpps:
            if covered(p):
                continue
            rel = str(p.relative_to(REPO))
            if rel in COVERAGE_EXEMPT:
                exempt.append(p)
                seen_exempt.add(rel)
            else:
                untested.append(p)
        covered_n = len(cpps) - len(untested) - len(exempt)
        print(
            f"\ncheck-test-mapping: {label} coverage report (advisory) — "
            f"{covered_n}/{len(cpps)} .cpp have a matching test_<name>.cpp, "
            f"{len(exempt)} intentionally exempt, {len(untested)} unexempted gap(s)"
        )
        for p in untested:
            print(f"  {p.relative_to(REPO)}  ->  no test_{p.stem}.cpp")
        for p in exempt:
            rel = str(p.relative_to(REPO))
            print(f"  [exempt] {rel}  ->  {COVERAGE_EXEMPT[rel]}")

    # Stale-exemption guard: a COVERAGE_EXEMPT key that matched no scanned,
    # still-untested .cpp (file renamed/removed, or it gained a test) would
    # silently rot the allowlist. Advisory only — never changes exit status.
    stale = sorted(set(COVERAGE_EXEMPT) - seen_exempt)
    if stale:
        print(
            "\ncheck-test-mapping: stale COVERAGE_EXEMPT entries "
            "(no longer an untested src .cpp):"
        )
        for rel in stale:
            print(f"  {rel}  ->  remove from COVERAGE_EXEMPT (renamed/removed, or now tested)")


def main() -> int:
    for path in (SRC_MODULES, TESTS_MODULES, SRC_UTILS, TESTS_UTILS):
        if not path.is_dir():
            print(f"check-test-mapping: {path} not found", file=sys.stderr)
            return 2

    # Run both checks unconditionally so a single invocation reports every
    # drift, rather than stopping at the first failing tree.
    modules_ok = check_modules()
    utils_ok = check_utils()
    registration_ok = check_test_registration()
    # Advisory only — never affects exit status (see function docstring).
    report_core_chrome_coverage()
    return 0 if (modules_ok and utils_ok and registration_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
