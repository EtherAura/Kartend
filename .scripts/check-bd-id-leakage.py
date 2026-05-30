#!/usr/bin/env python3
"""Lint: forbid bd issue IDs (Kartend-XXXX) in user-facing surfaces.

The contributor-facing rule (CONTRIBUTING.md → "Issue tracking
conventions") allows `Kartend-XXXX` cross-references in code comments,
commit messages, CHANGELOG.md, and dev docs under `docs/` — but blocks
them in surfaces the end user actually sees:

  - readme.md (project README)
  - docs/user/*.md (wiki content, mirrored to GitHub Wiki)
  - packaging/*.metainfo.xml, *.appdata.xml (AppStream metadata)

This script scans those paths for `Kartend-XXXX` patterns and exits 1
on a hit. False-positive guard: phrases like "Kartend-specific" or
"Kartend-only" use `Kartend-` as a project-name prefix, not an issue
ID — those are filtered by an English-word blocklist on the suffix.

Issue IDs are always lowercase alphanumerics with an optional `.N`
suffix (e.g. `Kartend-5wuk.1`). Lowercase + length ≥ 4 is the
fingerprint that separates them from prose suffixes.

Exit status: 0 = clean, 1 = violations found, 2 = usage error.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# Surfaces end users see directly. Each entry is a glob from the repo
# root. Keep this list narrow — code comments, CHANGELOG, and dev docs
# under docs/ are intentionally NOT covered because contributors read
# them and bd IDs serve as legitimate cross-references there.
USER_FACING_GLOBS: list[str] = [
    "readme.md",
    "docs/user/*.md",
    "packaging/*.metainfo.xml",
    "packaging/*.appdata.xml",
]

# Issue-ID shape: `Kartend-` followed by lowercase alphanumerics, with
# an optional `.N` suffix. Conservative on the suffix length (≥ 4) to
# stay away from short English words while still catching the 4-char
# canonical form.
BD_ID_RE = re.compile(r"\bKartend-([a-z0-9]{4,})(?:\.\d+)?\b")

# Words that follow `Kartend-` as a project-name prefix rather than an
# issue ID. All-lowercase prose hyphenations only — anything mixed-case
# or longer than these is treated as an ID candidate. The list is
# deliberately short; new false positives should be added only with a
# justification.
PROJECT_NAME_PREFIX_WORDS: set[str] = {
    "specific",
    "only",
    "compatible",
    "compliant",
    "style",
    "wide",
}


def find_violations() -> list[tuple[str, int, str]]:
    """Scan user-facing files. Returns (path, line_number, match)."""
    findings: list[tuple[str, int, str]] = []
    for glob in USER_FACING_GLOBS:
        for path in sorted(REPO.glob(glob)):
            text = path.read_text(encoding="utf-8", errors="replace")
            for line_idx, line in enumerate(text.splitlines(), start=1):
                for match in BD_ID_RE.finditer(line):
                    suffix = match.group(1)
                    if suffix in PROJECT_NAME_PREFIX_WORDS:
                        continue
                    rel = str(path.relative_to(REPO))
                    findings.append((rel, line_idx, match.group(0)))
    return findings


def main() -> int:
    findings = find_violations()
    if findings:
        print("check-bd-id-leakage: bd issue ID found in user-facing surface:")
        for rel, line, match in findings:
            print(f"  {rel}:{line}  ->  {match}")
        print(
            "\nFix: rewrite the reference to omit the ID — describe the "
            "feature / change in plain language. Bd IDs are allowed in "
            "code comments and dev docs (see CONTRIBUTING.md → 'Issue "
            "tracking conventions') but must not leak into user-facing "
            "surfaces (readme.md, docs/user/, AppStream metadata)."
        )
        return 1
    surfaces = ", ".join(USER_FACING_GLOBS)
    print(f"check-bd-id-leakage: OK — no bd issue IDs in {surfaces}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
