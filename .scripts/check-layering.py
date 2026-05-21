#!/usr/bin/env python3
"""Module-layering lint for Kartend.

Kartend's `src/` is organized into layers, but the build links everything
into one OBJECT library, so the layering is not compiler-enforced. This
script enforces the invariants that must always hold:

    src/utils/    is the foundation layer and MUST NOT depend on any
                  higher layer (src/modules/, src/chrome/, src/ui/,
                  src/core/).

    src/chrome/   is the neutral chrome layer (Kartend-jvib) sitting
                  between utils/ and modules/. The QWidgets in here
                  must stay "dumb" — pixmaps, strings, Qt signals — so
                  input/media can include them without a transitive
                  upward dep on src/ui/ or src/core/. They MUST NOT
                  include from src/modules/, src/ui/, or src/core/.

It maps every quoted `#include "x.h"` in those directories to the area
its header actually lives in and fails if a file reaches upward.

This is the first guardrail for issue "module folders don't enforce
layering" — the eventual fix is to split each top-level module into its
own library with explicit `target_link_libraries`. Until then, this lint
stops the foundation/chrome layers from silently accreting upward edges.

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
    # What's "upward" depends on which layer we're checking:
    #   utils/   — nothing above is reachable (utils is the floor)
    #   chrome/  — modules/, ui/, core/ are above; utils/ + chrome/ are OK
    # The eventual hchk split will make this CMake-enforced; until then the
    # lint is the only guardrail.
    layer_upward = {
        "utils": {"modules", "chrome", "ui", "core"},
        "chrome": {"modules", "ui", "core"},
    }
    violations: list[tuple[str, str, str, str]] = []  # (layer, file, include, target)

    for layer, upward in layer_upward.items():
        for path in sorted((SRC / layer).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for inc in INCLUDE_RE.findall(text):
                base = inc.rsplit("/", 1)[-1]
                if base in ALLOWLIST:
                    continue
                target = area.get(base)
                if target in upward:
                    rel = str(path.relative_to(REPO))
                    violations.append((layer, rel, inc, target))

    if violations:
        by_layer: dict[str, list[str]] = {}
        for layer, rel, inc, target in violations:
            by_layer.setdefault(layer, []).append(f"  {rel}  ->  {inc}  (in src/{target}/)")
        for layer, lines in by_layer.items():
            print(f"check-layering: src/{layer}/ reaches upward:")
            print("\n".join(lines))
        print(
            "\nFix: move the shared header into a lower layer, or invert "
            "the dependency (callbacks/signals instead of #include)."
        )
        return 1

    print(
        "check-layering: OK — src/utils/ and src/chrome/ stay within "
        "their layers"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
