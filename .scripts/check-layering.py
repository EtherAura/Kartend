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
# live above it. The UIConstants subheaders under src/ui/uiconstants/ are
# pure compile-time constants namespaces with no behavioural dependency;
# the seeding loop below adds them all to this set. Relocating them into
# utils/ is tracked separately. Keep additions to this list short and
# justified.
ALLOWLIST: set[str] = set()

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

    # Second guardrail: setup structs must NOT carry sibling-manager pointers.
    # The architecture's DI rule is that every <FooSetup> struct carries
    # `const ApplicationContext *ctx` plus only non-manager refs (widgets,
    # value containers, callbacks). Manager pointers belong on ctx so a
    # manager can't accidentally pin its siblings' lifetimes through the
    # setup struct.
    setup_violations = check_setup_struct_members()
    if setup_violations:
        print("check-layering: setup struct carries manager/service pointer:")
        for rel, struct, line, member in setup_violations:
            print(f"  {rel}  struct {struct} (line {line})  ->  {member}")
        print(
            "\nFix: route manager/service access through `ctx` "
            "(ApplicationContext *) instead. Setup structs are limited to "
            "ApplicationContext + non-manager refs (widgets, value "
            "containers, callbacks) so that sibling-manager lifetimes "
            "stay owned by ApplicationManager, not pinned through a setup "
            "field."
        )
        return 1

    print(
        "check-layering: OK — src/utils/ and src/chrome/ stay within "
        "their layers; setup structs carry only ctx + non-manager refs"
    )
    return 0


# Manager/Service pointer detector for setup structs. We scan all setup struct
# bodies and flag any field whose type ends in `Manager *` / `Service *`
# (with or without a leading `I`/`const`). `const ApplicationContext *` is
# the canonical exception — siblings are reached through ctx.
SETUP_STRUCT_RE = re.compile(
    r"^(?:\s*template\s*<[^>]*>\s*)?\s*struct\s+(\w*Setup)\s*\{",
    re.MULTILINE,
)
# Match `Foo *name` or `IFoo *name` where Foo ends in Manager or Service.
# Tolerate const-qualified and pointer-qualified variants but explicitly
# allow `const ApplicationContext *ctx` as the canonical DI handle.
MANAGER_FIELD_RE = re.compile(
    r"""^\s*
        (?!\s*//)                # skip comment lines
        (?:const\s+)?
        (\w*(?:Manager|Service))   # type name ending in Manager or Service
        \s*\*\s*
        (\w+)                      # field name
        \s*(?:=[^;]*)?\s*;""",
    re.MULTILINE | re.VERBOSE,
)


def check_setup_struct_members() -> list[tuple[str, str, int, str]]:
    """Find Manager*/Service* fields in any *Setup struct.

    Returns (relative_path, struct_name, line_number, field_decl_excerpt).
    """
    findings: list[tuple[str, str, int, str]] = []
    for path in sorted(SRC.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for struct_match in SETUP_STRUCT_RE.finditer(text):
            struct_name = struct_match.group(1)
            # Known legacy violations slated for migration to ctx-based
            # access. Each entry should have a tracking issue; remove from
            # this set as each migration lands. Whitelist by struct name —
            # *every* field on these structs is grandfathered.
            if struct_name in SETUP_STRUCT_GRANDFATHERED:
                continue
            body_start = struct_match.end()
            depth = 1
            i = body_start
            while i < len(text) and depth > 0:
                ch = text[i]
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                i += 1
            body_end = i
            body = text[body_start:body_end]
            for field_match in MANAGER_FIELD_RE.finditer(body):
                type_name = field_match.group(1)
                field_name = field_match.group(2)
                # ApplicationContext is the canonical DI handle, not a
                # sibling manager pointer — allow it through.
                if type_name == "ApplicationContext":
                    continue
                line_number = (
                    text[: body_start + field_match.start()].count("\n") + 1
                )
                rel = str(path.relative_to(REPO))
                decl = f"{type_name} *{field_name}"
                findings.append((rel, struct_name, line_number, decl))
    return findings


# Grandfathered legacy setup structs that still carry sibling-manager
# pointers. Each needs a migration to ctx-based access; the lint rule
# stays effective for NEW structs while these are queued for cleanup.
# Drop entries from this set as the corresponding refactor lands.
SETUP_STRUCT_GRANDFATHERED: set[str] = set()


if __name__ == "__main__":
    sys.exit(main())
