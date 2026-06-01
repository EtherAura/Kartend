#!/usr/bin/env python3
"""Module-layering lint for Kartend.

Kartend's `src/` is organized into per-area OBJECT libraries (kartend_utils,
_api, _chrome, _data, _input, _media, _ui, _core) with an explicit
target_link_libraries DAG. But OBJECT libs propagate only usage requirements
(include dirs, compile defs) — every area's .o files land in each consumer
regardless — so a cross-layer `#include` still compiles. The layering is thus
not link-enforced; this script enforces the invariants that must always hold:

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

This lint backstops the layering until the per-area OBJECT libs are
converted to STATIC (Kartend-q3vfq), which would turn a cross-layer include
into a link error. Until then, it stops the foundation/chrome (and
data/input/media) layers from silently accreting upward edges.

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
    """Map each src header's basename to its top-level area.

    Fails with sys.exit(2) on a basename collision — two headers under
    src/ sharing the same `*.h` basename would silently overwrite each
    other in this map, and the layering lint (which keys by basename
    from `#include "x.h"`) would resolve to whichever happened to be
    visited last. That's a quietly broken state that can mask real
    layer violations, so collisions must be eliminated rather than
    arbitrated.

    Fix collisions by renaming one of the two files so its basename
    becomes unique — e.g. add a `-constants` suffix to the constant-
    namespace half of a name shared with a widget class.
    """
    area: dict[str, str] = {}
    seen: dict[str, pathlib.Path] = {}
    collisions: list[tuple[str, pathlib.Path, pathlib.Path]] = []
    for hdr in sorted(SRC.rglob("*.h")):
        rel = hdr.relative_to(SRC)
        if hdr.name in seen and seen[hdr.name] != hdr:
            collisions.append((hdr.name, seen[hdr.name], hdr))
        else:
            seen[hdr.name] = hdr
            area[hdr.name] = rel.parts[0]
    if collisions:
        print("check-layering: header basename collisions detected:")
        for name, first, second in collisions:
            print(
                f"  {name}\n"
                f"    {first.relative_to(REPO)}\n"
                f"    {second.relative_to(REPO)}"
            )
        print(
            "\nFix: rename one of the colliding files so its basename "
            "is unique. The layering lint maps included headers to "
            "areas by basename — a collision silently masks layer "
            "violations because the second file's area overwrites the "
            "first's."
        )
        sys.exit(2)
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

    # Third guardrail: no legacy MainWindow::getXxxManager() callers outside
    # src/core/mainwindow*. The documented routing is:
    #   - mainWindow->applicationManager()->getXxx()  (preferred fallback)
    #   - an injected ApplicationContext for ui-layer dialogs (Kartend-qjtz)
    # `mainWindow->getXxxManager()` accessors are gone outside mainwindow*.cpp
    # since the Kartend-5wuk.1 audit; this lint stops them from creeping back.
    mw_accessor_violations = check_mainwindow_accessor_callers()
    if mw_accessor_violations:
        print(
            "check-layering: legacy MainWindow::getXxxManager() callers outside "
            "src/core/mainwindow*:"
        )
        for rel, line, snippet in mw_accessor_violations:
            print(f"  {rel}:{line}  ->  {snippet}")
        print(
            "\nFix: route through ApplicationManager — "
            "`mainWindow->applicationManager()->getXxxManager()` — or, for "
            "new classes, take an `ApplicationManager *` in the constructor. "
            "Ui-layer dialogs that need sibling managers take an injected "
            "ApplicationContext instead (Kartend-qjtz)."
        )
        return 1

    # Fourth guardrail: IMainWindow must expose no `*Manager`-returning
    # accessor other than applicationManager(). Kartend-qjtz removed the
    # per-manager forwarders (settingsManager / scrollManager /
    # interactionManager) in favor of ApplicationContext-based sibling access;
    # re-adding a `virtual XxxManager *` method to the interface would
    # resurrect the data/ui->core coupling those forwarders caused.
    # applicationManager() is the one sanctioned hop.
    imw_manager_violations = check_imainwindow_manager_methods()
    if imw_manager_violations:
        print(
            "check-layering: IMainWindow exposes a sibling-manager accessor "
            "other than applicationManager():"
        )
        for line, method, snippet in imw_manager_violations:
            print(f"  src/api/imainwindow.h:{line}  ->  {snippet}")
        print(
            "\nFix: drop the accessor from IMainWindow. Ui-layer dialogs reach "
            "sibling managers through an injected ApplicationContext "
            "(Kartend-qjtz); other callers route through "
            "applicationManager()->getXxxManager(). Only applicationManager() "
            "is allowed to return a manager from this interface."
        )
        return 1

    # Fifth guardrail: the two documented ctx accessor styles must stay
    # visually distinct (architecture.md "Two ctx patterns"). ApplicationContext
    # exposes terse no-get-prefix accessors (`ctx->scrollManager()`); controller-
    # ctx structs expose get-prefixed std::function thunks
    # (`m_ctx.getScrollManager()`). The audit-grep that tells the two apart keys
    # off this naming via the `mainWindow->` qualifier neither pattern uses; a
    # refactor that "normalizes" either side would silently blind it.
    appctx_findings, controller_findings = check_ctx_accessor_styles()
    if appctx_findings:
        print(
            "check-layering: ApplicationContext exposes a get-prefixed accessor "
            "(its style is no-get-prefix — scrollManager(), not getScrollManager()):"
        )
        for rel, line, snippet in appctx_findings:
            print(f"  {rel}:{line}  ->  {snippet}")
        print(
            "\nFix: drop the get prefix. ApplicationContext accessors are terse "
            "ctx-scoped reads so the audit-grep can tell `ctx->xxx()` apart from "
            "controller-ctx `m_ctx.getXxx()` thunks. See docs/dev/architecture.md "
            "\"Two ctx patterns\"."
        )
        return 1
    if controller_findings:
        print(
            "check-layering: controller-context std::function accessor lacks the "
            "get prefix (its style is get-prefixed — getScrollManager()):"
        )
        for rel, struct, line, field in controller_findings:
            print(f"  {rel}:{line}  struct {struct}  ->  std::function ... {field}")
        print(
            "\nFix: prefix the accessor thunk with get (getScrollManager, not "
            "scrollManager). Controller-ctx structs use get-prefixed std::function "
            "thunks so the audit-grep can tell them apart from ApplicationContext's "
            "no-prefix `ctx->xxx()` accessors. Void command/event callbacks "
            "(onXxx, refreshXxx, setWindowTitle) are exempt — only non-void "
            "accessor thunks need the prefix. See docs/dev/architecture.md."
        )
        return 1

    print(
        "check-layering: OK — src/utils/ and src/chrome/ stay within "
        "their layers; setup structs carry only ctx + non-manager refs; "
        "no legacy MainWindow::getXxxManager() callers outside mainwindow*; "
        "IMainWindow exposes only applicationManager(); ctx accessor styles "
        "stay distinct (ApplicationContext no-get-prefix, controller-ctx "
        "get-prefixed)"
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


# Match calls like `mainWindow->getDatabaseManager()` or
# `m_mainWindow->getScrollManager()`. The qualifier list intentionally
# stays narrow — `mw`, `mainWindow`, `m_mainWindow`, `mainwindow`,
# `m_mainwindow` — to keep false positives low. `getApplicationManager()`
# is the canonical fallback hop and is filtered out below.
MAINWINDOW_GETTER_RE = re.compile(
    r"\b(?:(?:m_)?[Mm]ain[Ww]indow|mw)\s*->\s*(get[A-Z][A-Za-z0-9]*Manager)\s*\("
)


def check_mainwindow_accessor_callers() -> list[tuple[str, int, str]]:
    """Find `mainWindow->getXxxManager()` callers outside mainwindow*.

    Returns (relative_path, line_number, snippet) for each non-allowed
    accessor call. `getApplicationManager` is exempt — going through
    ApplicationManager is the documented fallback.
    """
    findings: list[tuple[str, int, str]] = []
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        # Exempt the MainWindow's own TUs — they define and call these
        # legacy accessors internally as the rename progresses.
        if path.parent == SRC / "core" and path.stem.startswith("mainwindow"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in MAINWINDOW_GETTER_RE.finditer(text):
            accessor = match.group(1)
            if accessor == "getApplicationManager":
                continue
            line_number = text[: match.start()].count("\n") + 1
            # Reconstruct the snippet by taking the matched substring plus
            # any context up to the end of the call's open paren.
            snippet = match.group(0).rstrip()
            rel = str(path.relative_to(REPO))
            findings.append((rel, line_number, snippet))
    return findings


# IMainWindow is allowed exactly one `*Manager`-returning accessor —
# applicationManager(), the sanctioned hop to ApplicationManager. Everything
# else (settingsManager / scrollManager / interactionManager, …) was removed
# in Kartend-qjtz; ui-layer dialogs reach siblings via ApplicationContext.
IMAINWINDOW_HEADER = SRC / "api" / "imainwindow.h"
IMAINWINDOW_MANAGER_ALLOWLIST: set[str] = {"applicationManager"}
# Match a pure-virtual accessor whose return type is a `*Manager`/`*Service`
# pointer: `virtual [const] XxxManager *methodName(`. Captures the method name.
IMAINWINDOW_MANAGER_METHOD_RE = re.compile(
    r"virtual\s+(?:const\s+)?\w*(?:Manager|Service)\s*\*\s*(\w+)\s*\("
)


def check_imainwindow_manager_methods() -> list[tuple[int, str, str]]:
    """Find `*Manager`-returning virtual accessors on IMainWindow.

    Returns (line_number, method_name, snippet) for each method not on the
    allowlist. Only applicationManager() is permitted.
    """
    findings: list[tuple[int, str, str]] = []
    if not IMAINWINDOW_HEADER.is_file():
        return findings
    text = IMAINWINDOW_HEADER.read_text(encoding="utf-8", errors="replace")
    for match in IMAINWINDOW_MANAGER_METHOD_RE.finditer(text):
        method = match.group(1)
        if method in IMAINWINDOW_MANAGER_ALLOWLIST:
            continue
        line_number = text[: match.start()].count("\n") + 1
        findings.append((line_number, method, match.group(0).strip()))
    return findings


# ── Fifth guardrail helpers: the two ctx accessor styles ────────────────────
#
# docs/dev/architecture.md "Two ctx patterns" splits sibling-manager access
# into two deliberately distinct shapes:
#   ApplicationContext        raw IXxxManager* fields    ctx->scrollManager()      (NO get prefix)
#   <Foo>ControllerContext    std::function<Xxx*()> thunks  m_ctx.getScrollManager()  (get prefix)
# Only non-void thunks are "accessors"; void thunks are command/event callbacks
# (onOpenSettings, refreshTitleCounts, setWindowTitle) and are exempt from the
# get-prefix rule. The checks below keep both shapes intact so the audit-grep
# that distinguishes them stays accurate.
APPCONTEXT_STRUCT_RE = re.compile(r"struct\s+ApplicationContext\s*\{")
CONTROLLER_CTX_STRUCT_RE = re.compile(r"struct\s+(\w*ControllerContext)\s*\{")
# A get-prefixed member function: `getXxx(` with an uppercase letter after get.
CTX_GET_METHOD_RE = re.compile(r"\bget[A-Z]\w*\s*\(")
STD_FUNCTION_RE = re.compile(r"std::function\s*<")
GET_PREFIX_RE = re.compile(r"get[A-Z]")


def _blank_comments(text: str) -> str:
    """Replace // and /* */ comment bodies with spaces, preserving newlines
    and total length so byte offsets / line numbers map back to the original.

    Blanking (rather than deleting) keeps the brace-walk honest — a stray `{`
    or `}` inside a comment or doc block can't skew struct-body depth — while
    leaving line-number arithmetic on the original text exact.
    """

    def blank(match: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", match.group(0))

    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", blank, text)
    return text


def _brace_body_end(text: str, open_idx: int) -> int:
    """Index just past the `}` matching the `{` at open_idx (comments blanked)."""
    depth = 0
    i = open_idx
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(text)


def _function_fields(body: str):
    """Yield (is_void, field_name, offset) for each std::function<...> field.

    Walks the template `<...>` by bracket depth so nested generics
    (`std::function<const QList<CollectionConfig> *()>`) are matched whole.
    `is_void` is true when the return type is exactly `void` — those are the
    command/event callbacks exempt from the get-prefix rule.
    """
    for m in STD_FUNCTION_RE.finditer(body):
        i = m.end() - 1  # index of the opening '<'
        depth = 0
        while i < len(body):
            if body[i] == "<":
                depth += 1
            elif body[i] == ">":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if depth != 0:
            continue  # unbalanced — skip rather than misreport
        template_arg = body[m.end() : i]
        return_type = template_arg.split("(", 1)[0].strip()
        is_void = return_type == "void"
        name_match = re.match(r"\s*(\w+)", body[i + 1 :])
        if not name_match:
            continue
        yield (is_void, name_match.group(1), m.start())


def check_ctx_accessor_styles() -> tuple[
    list[tuple[str, int, str]], list[tuple[str, str, int, str]]
]:
    """Enforce the two documented ctx accessor styles.

    Returns (appctx_findings, controller_findings):
      appctx_findings      (rel, line, snippet) — get-prefixed methods inside
                           ApplicationContext, which must use NO get prefix.
      controller_findings  (rel, struct, line, field) — non-void std::function
                           accessor thunks in *ControllerContext structs that
                           lack a get prefix. Void thunks are exempt.
    """
    appctx_findings: list[tuple[str, int, str]] = []
    controller_findings: list[tuple[str, str, int, str]] = []
    for path in sorted(SRC.rglob("*.h")):
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = _blank_comments(raw)
        rel = str(path.relative_to(REPO))

        # (a) ApplicationContext: no get-prefixed accessor methods.
        for m in APPCONTEXT_STRUCT_RE.finditer(text):
            open_idx = m.end() - 1
            body = text[open_idx + 1 : _brace_body_end(text, open_idx) - 1]
            for gm in CTX_GET_METHOD_RE.finditer(body):
                off = open_idx + 1 + gm.start()
                line = raw[:off].count("\n") + 1
                appctx_findings.append((rel, line, gm.group(0).strip()))

        # (b) *ControllerContext: non-void std::function thunks need a get prefix.
        for m in CONTROLLER_CTX_STRUCT_RE.finditer(text):
            struct_name = m.group(1)
            open_idx = m.end() - 1
            body_off = open_idx + 1
            body = text[body_off : _brace_body_end(text, open_idx) - 1]
            for is_void, field_name, field_off in _function_fields(body):
                if is_void or GET_PREFIX_RE.match(field_name):
                    continue
                off = body_off + field_off
                line = raw[:off].count("\n") + 1
                controller_findings.append((rel, struct_name, line, field_name))
    return appctx_findings, controller_findings


if __name__ == "__main__":
    sys.exit(main())
