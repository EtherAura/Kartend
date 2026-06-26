#!/usr/bin/env python3
"""Guard the doc-only path lists that branch protection depends on (Kartend
audit DX-12).

build.yml and coverage.yml SKIP doc-only changes via `paths-ignore`
(Kartend-q9gmq). Because those skipped runs would otherwise leave the required
status checks pending forever, required-checks-noop.yml (Kartend-gy0o4) emits
same-named green jobs on the INVERSE `paths` set. The whole scheme only works
while every one of those bracketed path lists is byte-identical -- if they
drift, doc-only PRs hang (no required check ever reports) or a code PR gets a
green no-op instead of the real build.

Today the three workflow files are kept in lockstep only by comments. This
check enforces it so a future edit to one list can't silently break merges.

Pure-Python, no deps (mirrors the other .scripts/check-*.py gates).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WF = ROOT / ".github" / "workflows"

# (file, yaml-key) pairs whose bracketed list(s) must all match.
SOURCES = [
    ("build.yml", "paths-ignore"),
    ("coverage.yml", "paths-ignore"),
    ("required-checks-noop.yml", "paths"),
]


def extract_lists(path, key):
    """Returns every inline `key: [ ... ]` list found in the file, each as a
    list of unquoted glob strings."""
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    found = []
    for m in re.finditer(rf"^[ \t]*{re.escape(key)}:[ \t]*\[([^\]]*)\]", text, re.MULTILINE):
        items = [s.strip().strip("'\"") for s in m.group(1).split(",") if s.strip()]
        found.append((m.start(), items))
    return found


def main():
    collected = []  # (label, list)
    for fname, key in SOURCES:
        lists = extract_lists(WF / fname, key)
        if lists is None:
            print(f"check-required-checks-consistency: {fname} not found", file=sys.stderr)
            return 1
        if not lists:
            print(
                f"check-required-checks-consistency: no inline `{key}: [...]` list in {fname}",
                file=sys.stderr,
            )
            return 1
        for _pos, items in lists:
            collected.append((f"{fname} `{key}`", items))

    canonical = collected[0][1]
    drift = [(label, items) for label, items in collected if items != canonical]
    if drift:
        print(
            "check-required-checks-consistency: doc-only path lists have drifted "
            "(branch protection will break on doc-only or mixed PRs):",
            file=sys.stderr,
        )
        print(f"  canonical ({collected[0][0]}): {canonical}", file=sys.stderr)
        for label, items in drift:
            print(f"  MISMATCH {label}: {items}", file=sys.stderr)
        print(
            "  Keep build.yml/coverage.yml `paths-ignore` and required-checks-noop.yml "
            "`paths` byte-identical (Kartend-gy0o4 / audit DX-12).",
            file=sys.stderr,
        )
        return 1

    print(
        f"check-required-checks-consistency: OK ({len(collected)} path lists match {canonical})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
