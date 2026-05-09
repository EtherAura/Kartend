#!/usr/bin/env python3
"""Rewrite links in source markdown to GitHub Wiki conventions.

Two passes:

1. Out-of-wiki relative links — anything starting with `../` (e.g.
   `../building.md`, `../../src/utils/`). The wiki is flat (a single
   directory of .md files) so these would 404. Rewrite to absolute
   https://github.com/<repo>/{blob,tree}/main/<path> URLs; the
   blob-vs-tree distinction is resolved by probing the source
   checkout.

2. Wiki-internal .md links — links of the form
   `[text](Page-Name.md)` or `[text](Page-Name.md#anchor)`. GitHub
   Wiki serves `.md`-suffixed paths as raw blobs instead of
   rendered pages, so we strip the extension. Anchor fragments are
   preserved.

Pass 1 runs before pass 2 so absolute https://...readme.md URLs
emitted by pass 1 are skipped by pass 2's negative-lookahead.

Usage: wiki-rewrite-links.py <wiki-dir> <repo> <source-root>
"""

import os
import re
import sys
from pathlib import Path

# Pass 1: out-of-wiki relative links — `](../...)` or `](../../...)`.
RELATIVE_LINK_RE = re.compile(r"(\]\()(\.\./[^)]+)(\))")

# Pass 2: wiki-internal .md links. Match `](path.md)` and `](path.md#anchor)`
# but skip absolute URLs (negative lookahead on http(s)://) so the URLs
# emitted by pass 1 stay intact.
INTERNAL_MD_RE = re.compile(
    r"(\]\()(?!https?://)(?!#)([^)#]+)\.md(#[^)]*)?(\))"
)


def make_relative_replacer(repo: str, src_root: Path, src_subdir: str = "docs/guide"):
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


def strip_md_extension(match: re.Match) -> str:
    """Drop the trailing .md from a wiki-internal link, preserving any anchor."""
    prefix, path, anchor, suffix = match.groups()
    return f"{prefix}{path}{anchor or ''}{suffix}"


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

    relative_replacer = make_relative_replacer(repo, src_root)
    changed = 0
    for md in sorted(wiki_dir.glob("*.md")):
        original = md.read_text()
        # Pass 1: rewrite ../ relative links to absolute github.com URLs.
        rewritten = RELATIVE_LINK_RE.sub(relative_replacer, original)
        # Pass 2: strip .md from remaining wiki-internal links so the wiki
        # serves them as rendered pages instead of raw markdown blobs.
        rewritten = INTERNAL_MD_RE.sub(strip_md_extension, rewritten)
        if rewritten != original:
            md.write_text(rewritten)
            changed += 1
    print(f"rewrote links in {changed} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
