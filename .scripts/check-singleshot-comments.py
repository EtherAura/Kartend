#!/usr/bin/env python3
"""QTimer::singleShot 'why' comment lint for Kartend.

Project rule (.github/copilot-instructions.md): every QTimer::singleShot
call site must be preceded by a comment explaining *why* the delay is
needed — "wait for layout settle", "defer past event loop", "break
reentrancy", and so on. The reason is that QTimer::singleShot(N) is a
load-bearing hack at most call sites: without the rationale the next
person to touch the code can't tell whether the delay is essential or
just superstition.

This lint walks every src/ .cpp / .h file. For each QTimer::singleShot(
it walks backwards past blank lines to the first content line and fails
the lint unless that line is a //-comment or block-comment continuation.

Pairs with .scripts/check-layering.py in the maintenance-check CI job.

Exit status: 0 = clean, 1 = violations found, 2 = usage error.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "src"

CALL_RE = re.compile(r"QTimer::singleShot\s*\(")


def is_comment_line(stripped: str) -> bool:
    # `//` is the dominant style here; `/*` covers an opening block
    # comment that wraps onto the singleShot line; ` * ` covers a
    # continuation line of a Doxygen / block comment immediately above
    # the call.
    return stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*")


def main() -> int:
    if not SRC.is_dir():
        print(f"check-singleshot-comments: {SRC} not found", file=sys.stderr)
        return 2

    violations: list[tuple[str, int, str]] = []
    total = 0

    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as exc:
            print(f"check-singleshot-comments: cannot read {path}: {exc}", file=sys.stderr)
            return 2

        for i, line in enumerate(lines):
            if not CALL_RE.search(line):
                continue
            total += 1
            j = i - 1
            documented = False
            while j >= 0:
                stripped = lines[j].strip()
                if not stripped:
                    j -= 1
                    continue
                if is_comment_line(stripped):
                    documented = True
                break
            if not documented:
                rel = str(path.relative_to(REPO))
                violations.append((rel, i + 1, line.strip()))

    if violations:
        print(
            f"check-singleshot-comments: {len(violations)} of {total} "
            f"QTimer::singleShot call site(s) missing a 'why' comment:"
        )
        for rel, lineno, content in violations:
            print(f"  {rel}:{lineno}  {content}")
        print(
            "\nFix: add a // comment immediately above the call explaining "
            "WHY the delay is needed (event-loop defer, layout settle, "
            "reentrancy break, etc.) — not WHAT singleShot does."
        )
        return 1

    print(f"check-singleshot-comments: OK — {total} call site(s) all carry a 'why' comment")
    return 0


if __name__ == "__main__":
    sys.exit(main())
