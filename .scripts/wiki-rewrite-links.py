#!/usr/bin/env python3
"""Rewrite out-of-wiki relative links to absolute GitHub URLs.

The wiki is flat (single directory of .md files), so any link in
docs/guide/*.md that escapes the guide directory (../foo or ../../bar)
will 404 once published. This script rewrites those into absolute
https://github.com/<repo>/{blob,tree}/main/<path> URLs, choosing
blob vs tree based on whether the target exists as a directory in
the source checkout.

Wiki-internal links like [Other-Page](Other-Page.md) are NOT touched —
GitHub Wiki resolves those correctly on its own.

Usage: wiki-rewrite-links.py <wiki-dir> <repo> <source-root>
"""

import os
import re
import sys
from pathlib import Path

LINK_RE = re.compile(r"(\]\()(\.\./[^)]+)(\))")


def make_replacer(repo: str, src_root: Path, src_subdir: str = "docs/guide"):
    def replacer(match: re.Match) -> str:
        prefix, rel, suffix = match.groups()
        # Resolve relative to docs/guide/ (the source layout)
        target = os.path.normpath(os.path.join(src_subdir, rel))
        if target.startswith("./"):
            target = target[2:]
        abs_target = src_root / target
        # Directory → /tree/main; file (or non-existent) → /blob/main.
        # Non-existent paths fall through to blob; the user sees a 404 at
        # the GitHub URL, same broken-link signal as before but pointed
        # at github.com instead of a phantom wiki page.
        kind = "tree" if abs_target.is_dir() else "blob"
        return f"{prefix}https://github.com/{repo}/{kind}/main/{target}{suffix}"

    return replacer


def main() -> int:
    if len(sys.argv) != 4:
        print(
            f"usage: {sys.argv[0]} <wiki-dir> <repo> <source-root>",
            file=sys.stderr,
        )
        return 2
    wiki_dir = Path(sys.argv[1])
    repo = sys.argv[2]
    src_root = Path(sys.argv[3]).resolve()
    if not wiki_dir.is_dir():
        print(f"error: wiki-dir not found: {wiki_dir}", file=sys.stderr)
        return 1
    if not src_root.is_dir():
        print(f"error: source-root not found: {src_root}", file=sys.stderr)
        return 1

    replacer = make_replacer(repo, src_root)
    changed = 0
    for md in sorted(wiki_dir.glob("*.md")):
        original = md.read_text()
        rewritten = LINK_RE.sub(replacer, original)
        if rewritten != original:
            md.write_text(rewritten)
            changed += 1
    print(f"rewrote out-of-wiki links in {changed} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
