#!/usr/bin/env python3
"""Module-layering lint for Kartend.

Kartend's `src/` is organized into layers, but the build links everything
into one OBJECT library, so the layering is not compiler-enforced. This
script enforces the one invariant that must always hold:

    src/utils/  is the foundation layer and MUST NOT depend on any
    higher layer (src/modules/, src/ui/, src/core/).

It maps every quoted `#include "x.h"` in `src/utils/` to the area its
header actually lives in and fails if a utils file reaches upward.

This is the first guardrail for issue "module folders don't enforce
layering" — the eventual fix is to split each top-level module into its
own library with explicit `target_link_libraries`. Until then, this lint
stops the foundation layer from silently accreting upward edges.

Exit status: 0 = clean, 1 = violations found, 2 = usage error.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "src"

# Headers a foundation-layer file is allowed to include even though they
# live above it. UIConstants is a pure compile-time constants namespace
# with no behavioural dependency; relocating it into utils/ is tracked
# separately. Keep this list short and justified.
ALLOWLIST = {"uiconstants.h"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def header_area_map() -> dict[str, str]:
    """Map each src header's basename to its top-level area."""
    area: dict[str, str] = {}
    for hdr in SRC.rglob("*.h"):
        rel = hdr.relative_to(SRC)
        area[hdr.name] = rel.parts[0]
    # uiconstants sub-headers live under src/ui/uiconstants/; treat the
    # whole namespace as allowlisted constants.
    for hdr in (SRC / "ui" / "uiconstants").glob("*.h"):
        ALLOWLIST.add(hdr.name)
    return area


def main() -> int:
    if not SRC.is_dir():
        print(f"check-layering: {SRC} not found", file=sys.stderr)
        return 2

    area = header_area_map()
    upward = {"modules", "ui", "core"}
    violations: list[str] = []

    for path in sorted((SRC / "utils").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for inc in INCLUDE_RE.findall(text):
            base = inc.rsplit("/", 1)[-1]
            if base in ALLOWLIST:
                continue
            target = area.get(base)
            if target in upward:
                rel = path.relative_to(REPO)
                violations.append(f"  {rel}  ->  {inc}  (in src/{target}/)")

    if violations:
        print("check-layering: foundation layer (src/utils/) reaches upward:")
        print("\n".join(violations))
        print(
            "\nsrc/utils/ must not include from src/modules|ui|core. "
            "Move the shared code down into src/utils/, or invert the "
            "dependency."
        )
        return 1

    print("check-layering: OK — src/utils/ stays within the foundation layer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
