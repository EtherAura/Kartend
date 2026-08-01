#!/usr/bin/env python3
"""Module-layering lint for Kartend.

Kartend's `src/` is organized into per-area OBJECT libraries (kartend_utils,
_api, _chrome, _data, _input, _media, _ui, _core) with an explicit
target_link_libraries DAG. Each area publishes only its OWN dirs on its PUBLIC
include path and inherits lower layers through the DAG, so an upward
`#include "foo.h"` by basename already fails to compile — the header simply
isn't on that layer's include path. This lint is not a substitute for that
compile-time check; it catches the residual cases include-scoping can't (and
several DI/accessor invariants that aren't about include paths at all):

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

The per-area include-scoping already fails an upward basename `#include` at
compile time. This lint additionally catches what that scoping cannot: a
subpath-style upward include that resolves through a directory placed on a
lower layer's PUBLIC path (the kind of umbrella that used to exist when
`src/ui` was on `kartend_utils`'s path for `uiconstants/`; that exception is
gone — uiconstants now lives under `src/utils/uiconstants/`), and basename
collisions that would silently mask a real violation. It keeps the
foundation/chrome (and data/input/media) layers from accreting upward edges.

NOTE: converting these OBJECT libs to STATIC would NOT add link-time layering
enforcement (Kartend-q3vfq, investigated & declined): usage requirements
propagate identically for OBJECT and STATIC, and every area links into one
executable (CMakeLists.txt: `target_link_libraries(kartend PRIVATE
${KARTEND_AREA_LIBS})`), so all symbols resolve at the final link regardless.
STATIC would only risk dropping Qt meta-object TUs the linker sees as unused.

Exit status: 0 = clean, 1 = violations found, 2 = usage error.

Usage:
    check-layering.py             run all guardrails (the lint), then the
                                  fan-out ratchet; appends a one-line
                                  ApplicationContext fan-out summary.
    check-layering.py --fanout    print ONLY the per-manager ctx-> fan-out
                                  table (an informational coupling metric, no
                                  pass/fail) and exit 0.
    check-layering.py --write-baseline
                                  (re)write .scripts/ctx-fanout-baseline.json
                                  from the current tree — run this after an
                                  intended coupling change to accept it.
    check-layering.py --check-fanout
                                  run ONLY the fan-out ratchet (exit 1 on a
                                  regression) — what the default run also does.

The fan-out report (Kartend-5y7zm) is an informational metric: it counts how
many call sites reach each sibling manager through `ctx->xxxManager()` so that
coupling growth around the ApplicationContext hub is visible in review. Role
accessor reads (`ctx->scrollGrid()`, `ctx->wheelAnimator()`, …) count toward
their underlying manager via an alias table derived from the seedXxxRoles()
groupings in applicationcontext.h (see ctx_role_aliases).

The teardown slot-null guardrail enforces the Kartend-rdzu9 invariant: every
ManagerRefs pointer slot in applicationcontext.h must be nulled — directly or
via its seedXxxRoles(nullptr) group — inside
ApplicationManager::destroyManagersAndClearContextSlots(), so teardown-phase
`if (auto *m = ctx->x())` guards can never read a dangling pointer.

The currentCollectionIndex back-channel guardrail (Kartend-dl0uz.1) keeps the
raw `int *` into MainWindow's m_currentCollectionIndex read-only: every holder
outside a small writer allowlist (NavigationManager, TreeManager,
SettingsDialogController, plus the ApplicationContext conduit) must spell it
`const int *` — in setup-struct fields, SETUP_GETTER types, and members alike —
so none of the ~20 read-only consumers can silently become a writer.

The fan-out RATCHET (Kartend-n1hpy.1) turns the report's OUTGOING dimension
into a soft gate: it CAN fail the build when a single file's ctx-> breadth
(distinct managers reached) grows past its recorded baseline, or a new file
crosses the floor. Incoming per-manager totals are still uncapped — they grow
naturally with features. The ratchet is inert until
.scripts/ctx-fanout-baseline.json exists (see check_fanout_ratchet). Run
--write-baseline to accept an intended change; lowering breadth is always
allowed.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import sys

# REPO is overridable via the KARTEND_REPO env var so the script can run against
# fixture trees in tests (Kartend audit b7phb); unset, it resolves to the repo
# this script lives in.
REPO = (
    pathlib.Path(os.environ["KARTEND_REPO"]).resolve()
    if os.environ.get("KARTEND_REPO")
    else pathlib.Path(__file__).resolve().parent.parent
)
SRC = REPO / "src"

# Headers a foundation-layer file is allowed to include even though they
# live above it. This used to hold the UIConstants subheaders (when they
# lived under src/ui/uiconstants/ but were included from lower layers);
# they have since been relocated into src/utils/uiconstants/, so they sit
# at the foundation layer and need no exception. The set is intentionally
# empty now — keep any future additions short and justified.
ALLOWLIST: set[str] = set()

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)

# A sibling-manager read through the ApplicationContext hub:
#   ctx->scrollManager()   /   m_ctx->scrollManager()
# (no-get-prefix, arrow form — that's the ApplicationContext accessor style;
# controller-ctx thunks use the dotted, get-prefixed `m_ctx.getXxxManager()`
# form and are intentionally NOT counted here). Captures the accessor name;
# compute_ctx_fanout() then keeps only `*Manager` accessors plus the role
# accessors in the ctx_role_aliases() table (scrollGrid, wheelAnimator, …),
# which count toward their underlying manager. Anything else (isValid,
# currentIndex, interactionState) is ignored.
CTX_FANOUT_RE = re.compile(r"\b(?:m_)?ctx\s*->\s*([a-z]\w*)\s*\(\s*\)")

# ── ApplicationContext fan-out RATCHET (Kartend-n1hpy.1) ─────────────────────
# The fan-out report below is informational; the ratchet turns its OUTGOING
# dimension into a soft gate so hub coupling can't silently grow. We gate the
# per-file *outgoing breadth* — how many DISTINCT sibling managers a single
# file reaches through ctx-> — because that is the actual smell the audit
# flagged ("widest reachers ... the prime candidates for role-scoped dependency
# structs"). A file going 9 -> 10 distinct managers is a regression; a new file
# reaching one manager is not. INCOMING per-manager totals grow naturally as
# features are added, so they are recorded for context but NOT gated.
#
# Rule: a file may reach up to max(FANOUT_FLOOR - 1, its baseline) distinct
# managers. Crossing the floor as a brand-new hub, or growing an existing
# baselined hub, fails — until `--write-baseline` records the intended new
# shape. Lowering is always allowed. This is a speed-bump, not a cap.
FANOUT_FLOOR = 3
FANOUT_BASELINE_PATH = REPO / ".scripts" / "ctx-fanout-baseline.json"


def header_area_map() -> dict[str, str]:
    """Map each src header's basename to its area.

    Areas are the top-level dirs under src/ ("utils", "chrome", "ui", …)
    except under src/modules/, where the sub-area is kept
    ("modules/data", "modules/behavior", …) because the module layers
    occupy DIFFERENT rungs of the DAG — behavior sits above ui/ while
    data/input/media sit below it — so a bare "modules" target would be
    too coarse to lint ui/'s upward edges. The layer_upward matcher in
    main() accepts either granularity.

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
            if rel.parts[0] == "modules" and len(rel.parts) > 2:
                area[hdr.name] = f"{rel.parts[0]}/{rel.parts[1]}"
            else:
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
    return area


def main() -> int:
    if not SRC.is_dir():
        print(f"check-layering: {SRC} not found", file=sys.stderr)
        return 2

    area = header_area_map()
    # What's "upward" depends on which layer we're checking:
    #   utils/   — nothing above is reachable (utils is the floor)
    #   chrome/  — modules/, ui/, core/ are above; utils/ + chrome/ are OK
    # The middle module layers (data/input/media) sit below ui/ + core/, so an
    # upward #include into either is a layering violation. chrome/ + utils/ +
    # sibling modules/ are at-or-below them and remain allowed. The per-area
    # include-scoping already fails an upward basename include at compile time;
    # this lint guards the residual subpath/collision cases it can't.
    layer_upward = {
        "utils": {"modules", "chrome", "ui", "core"},
        "chrome": {"modules", "ui", "core"},
        "modules/data": {"ui", "core"},
        "modules/input": {"ui", "core"},
        "modules/media": {"ui", "core"},
        # modules/behavior sits ABOVE ui/ in the real DAG (ApplicationManager
        # owns the ui-layer DetailsPaneManager controller; src/CMakeLists.txt
        # links kartend_behavior PUBLIC kartend_ui — sanctioned). Its one
        # forbidden upward edge is core/ — previously unlinted entirely
        # (Kartend-1ha73).
        "modules/behavior": {"core"},
        # ui/ sits above data/input/media but BELOW modules/behavior (see
        # above) and core/, so those two are its upward edges. The area map
        # keeps modules sub-areas distinct exactly so this rule can forbid
        # behavior without also forbidding the sanctioned data/input/media
        # includes.
        "ui": {"core", "modules/behavior"},
        # api/ is the header-only role-interface floor: it may be included
        # from anywhere, so it may itself reach DOWN only to utils/ (value
        # types in interface signatures). Everything else is upward.
        "api": {"chrome", "modules", "ui", "core"},
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
                if target is None:
                    continue
                # `upward` entries come in either granularity: a bare area
                # ("modules") forbids every sub-area, while a scoped one
                # ("modules/behavior") forbids only that rung.
                if target in upward or target.split("/", 1)[0] in upward:
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

    # Sixth guardrail: the raw `int *currentCollectionIndex` back-channel
    # stays read-only outside the writer allowlist. MainWindow's
    # `int m_currentCollectionIndex` fans out as a raw pointer through ~20
    # setup structs and manager members (Kartend-dl0uz.1); every read-only
    # holder was flipped to `const int *` so a consumer can't silently become
    # a writer. Only the classes in CURRENT_INDEX_WRITER_ALLOWLIST may still
    # hold it non-const.
    backchannel_findings = check_current_index_backchannel()
    if backchannel_findings:
        print(
            "check-layering: non-const `int *currentCollectionIndex` "
            "back-channel holder outside the writer allowlist:"
        )
        for rel, line, snippet in backchannel_findings:
            print(f"  {rel}:{line}  ->  {snippet}")
        print(
            "\nFix: declare the holder `const int *` — the setup-struct field, "
            "the SETUP_GETTER type, and the member all flip together. "
            "MainWindow's currentCollectionIndex fans out as a raw pointer "
            "back-channel; only the writer classes in "
            "CURRENT_INDEX_WRITER_ALLOWLIST (NavigationManager, TreeManager, "
            "SettingsDialogController, plus the ApplicationContext conduit "
            "they draw from) may hold it writable. A genuine new writer needs "
            "an explicit allowlist entry AND a review of the write-before-"
            "signal ordering existing readers rely on (see "
            "dbeventscontroller.cpp)."
        )
        return 1

    # Seventh guardrail: every ManagerRefs slot must be nulled in
    # ApplicationManager::destroyManagersAndClearContextSlots() (the
    # Kartend-rdzu9 invariant documented in applicationcontext.h).
    teardown_findings = check_context_slot_teardown()
    if teardown_findings:
        print(
            "check-layering: ManagerRefs slot(s) never nulled in "
            "ApplicationManager::destroyManagersAndClearContextSlots():"
        )
        for slot, hint in teardown_findings:
            print(f"  ctx->managers.{slot}  ->  {hint}")
        print(
            "\nFix: null every ManagerRefs pointer slot immediately BEFORE its "
            "owning unique_ptr resets in destroyManagersAndClearContextSlots() "
            "(applicationmanager.cpp) — directly, or through the group's "
            "seedXxxRoles(nullptr) teardown overload. Otherwise a "
            "teardown-phase `if (auto *m = ctx->x())` reads a dangling pointer "
            "(see the INVARIANT comment under ManagerRefs in "
            "applicationcontext.h)."
        )
        return 1

    print(
        "check-layering: OK — src/utils/, src/chrome/, src/api/, src/ui/, and "
        "src/modules/{behavior,data,input,media}/ stay within their layers; setup "
        "structs carry only ctx + non-manager refs; "
        "no legacy MainWindow::getXxxManager() callers outside mainwindow*; "
        "IMainWindow exposes only applicationManager(); ctx accessor styles "
        "stay distinct (ApplicationContext no-get-prefix, controller-ctx "
        "get-prefixed); the currentCollectionIndex back-channel is const "
        "outside its writer allowlist; every ManagerRefs slot is nulled in "
        "ApplicationManager teardown"
    )
    # Informational coupling metric (Kartend-5y7zm) — never fails the lint.
    by_manager, _ = compute_ctx_fanout()
    print(ctx_fanout_summary_line(by_manager))
    # Soft ratchet on the OUTGOING dimension (Kartend-n1hpy.1): this CAN fail
    # the lint when a single file's ctx-> breadth grows past its baseline.
    # Inert until a baseline exists (see check_fanout_ratchet).
    return check_fanout_ratchet()


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
# A get-prefixed member function DECLARATION: `getXxx(` with an uppercase
# letter after get. The negative lookbehind excludes qualified *calls*
# (`obj.getX()`, `ptr->getX()`, `Ns::getX()`) so an inline accessor body that
# legitimately calls a get* helper isn't misread as a forbidden declaration —
# a member declaration is never preceded by `.`, `>` (from `->`), or `:`
# (Kartend audit DX-09).
CTX_GET_METHOD_RE = re.compile(r"(?<![.>:])\bget[A-Z]\w*\s*\(")
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


# ── Sixth guardrail helpers: currentCollectionIndex back-channel const-ness ──
#
# Kartend-dl0uz.1. MainWindow's `int m_currentCollectionIndex` is threaded as a
# raw pointer through setup structs (`int *currentCollectionIndex`), their
# SETUP_GETTER types, and manager members (`int *m_currentCollectionIndex`).
# Every read-only holder declares it `const int *`; the regex flags any
# remaining non-const spelling — field, getter TYPE argument
# (`SETUP_GETTER_DECL(int *, CurrentCollectionIndex)`), member, or parameter —
# outside the writer allowlist below. Comments are blanked first so doc
# examples don't trip it.
CURRENT_INDEX_PTR_RE = re.compile(
    r"(?<!const\s)\bint\s*\*\s*(?:,\s*)?(?:m_)?[Cc]urrentCollectionIndex\b"
)

# Files that legitimately hold the pointer WRITABLE. Each entry names the
# write sites that justify it; everything else in src/ must use `const int *`.
# Adding a new writer here requires reviewing the write-before-signal ordering
# that read-only consumers rely on (dbeventscontroller.cpp documents it).
CURRENT_INDEX_WRITER_ALLOWLIST: set[str] = {
    # NavigationManager — writes in navigationmanager.cpp:141,
    # navigationmanagerroute.cpp:147, navigationmanagersubcollection.cpp:248.
    "src/modules/input/navigation/navigationmanager.h",
    # TreeManager — writes in treemanager.cpp:288,325; setSelectionState()
    # takes the writable pointer (declared in the .h, defined in the .cpp).
    "src/ui/dialogs/settings/core/treemanager.h",
    "src/ui/dialogs/settings/core/treemanager.cpp",
    # SettingsDialogController — writes the resolved index back through
    # `int &currentCollectionIndex = *context.currentCollectionIndex;`
    # (settingsdialogcontroller.cpp:303, assigned at :508), so its context
    # struct field stays writable.
    "src/ui/controllers/settingsdialogcontroller/settingsdialogcontroller.h",
    # ApplicationContext.collection is the conduit the setup-getter ctx
    # fallback draws from; the writer NavigationManager receives its pointer
    # through it, so the ctx field itself must stay `int *`.
    "src/utils/app/applicationcontext.h",
}


def check_current_index_backchannel() -> list[tuple[str, int, str]]:
    """Find non-const `int *currentCollectionIndex` holders outside the allowlist.

    Returns (relative_path, line_number, source_line_excerpt).
    """
    findings: list[tuple[str, int, str]] = []
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        rel = str(path.relative_to(REPO))
        if rel in CURRENT_INDEX_WRITER_ALLOWLIST:
            continue
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = _blank_comments(raw)
        for m in CURRENT_INDEX_PTR_RE.finditer(text):
            line_number = raw[: m.start()].count("\n") + 1
            snippet = raw.splitlines()[line_number - 1].strip()
            findings.append((rel, line_number, snippet))
    return findings


# ── Seventh guardrail helpers: teardown slot-nulling completeness ────────────
#
# applicationcontext.h documents the INVARIANT (Kartend-rdzu9): every pointer
# slot in ManagerRefs must be explicitly nulled — directly
# (`ctx->managers.x = nullptr;`) or via its group's
# `ctx->managers.seedXxxRoles(nullptr)` — inside
# ApplicationManager::destroyManagersAndClearContextSlots() before its owner
# resets. A slot added without that line leaves teardown-phase
# `if (auto *m = ctx->x())` reads dangling exactly when the guard matters.
# Until now the invariant lived only in a comment; this check makes it fail
# the lint. Grep-level on purpose, like every other guardrail here.


def check_context_slot_teardown() -> list[tuple[str, str]]:
    """Find ManagerRefs slots not nulled in destroyManagersAndClearContextSlots.

    Returns (slot_or_marker, explanation) findings. Empty when
    applicationcontext.h / applicationmanager.cpp are absent (fixture trees).
    """
    fields, seed_overloads = _manager_refs_model()
    if fields is None or not APPMANAGER_CPP.is_file():
        return []
    raw = APPMANAGER_CPP.read_text(encoding="utf-8", errors="replace")
    text = _blank_comments(raw)
    m = re.search(
        r"ApplicationManager::destroyManagersAndClearContextSlots\s*\(\s*\)\s*\{", text
    )
    if not m:
        return [
            (
                "<function>",
                "ApplicationManager::destroyManagersAndClearContextSlots() not "
                "found in applicationmanager.cpp — renamed? Update this check.",
            )
        ]
    open_idx = m.end() - 1
    body = text[open_idx + 1 : _brace_body_end(text, open_idx) - 1]
    directly_nulled = set(
        re.findall(r"ctx\s*->\s*managers\s*\.\s*(\w+)\s*=\s*nullptr\s*;", body)
    )
    seeds_called = set(
        re.findall(r"ctx\s*->\s*managers\s*\.\s*(seed\w+Roles)\s*\(\s*nullptr\s*\)", body)
    )
    covered = set(directly_nulled)
    for seed_name in seeds_called:
        covered |= seed_overloads.get(seed_name, set())
    findings: list[tuple[str, str]] = []
    for field in fields:
        if field in covered:
            continue
        group = next(
            (name for name, nulled in seed_overloads.items() if field in nulled), None
        )
        hint = (
            f"null it via ctx->managers.{group}(nullptr)"
            if group
            else f"add `ctx->managers.{field} = nullptr;` before its owner resets"
        )
        findings.append((field, hint))
    return findings


# ── Eighth concern (metric, not guardrail): ApplicationContext fan-out ──────
#
# Kartend-5y7zm. The include-layering DAG is enforced vertically, but the
# ApplicationContext is a horizontal back-channel: any manager holding `ctx`
# can reach ~22 siblings via `ctx->xxxManager()`, and the lint above says
# nothing about it. This report counts those reads per sibling manager so the
# hub's fan-out is visible in review and its growth is noticeable — WITHOUT a
# hard cap (capping would either grandfather the current state or block
# unrelated work). It feeds the larger "thin ctx into role-scoped dep structs"
# effort (Kartend-7pawj).


# ── ManagerRefs model: parsed once from applicationcontext.h ────────────────
#
# Shared by the ctx-fanout role aliasing below and the teardown-slot guardrail
# (check_context_slot_teardown). Parsing the real header instead of keeping a
# hand-maintained table means a new role accessor / seedXxxRoles() group is
# picked up automatically and can't silently drift out of the lint.
APPCONTEXT_HEADER = SRC / "utils" / "app" / "applicationcontext.h"
APPMANAGER_CPP = SRC / "modules" / "behavior" / "application" / "applicationmanager.cpp"
MANAGER_REFS_FIELD_RE = re.compile(r"^\s*\w[\w:]*\s*\*\s*(\w+)\s*=\s*nullptr\s*;", re.MULTILINE)
SEED_NULL_OVERLOAD_RE = re.compile(r"void\s+(seed\w+Roles)\s*\(\s*std::nullptr_t\s*\)\s*\{")
NULLED_FIELD_RE = re.compile(r"\b(\w+)\s*=\s*nullptr\s*;")


def _manager_refs_model() -> tuple[list[str] | None, dict[str, set[str]]]:
    """Parse ApplicationContext::ManagerRefs from applicationcontext.h.

    Returns (fields, seed_overloads):
      fields          every pointer slot name declared `Foo *name = nullptr;`
                      inside ManagerRefs, or None when the header is absent
                      (fixture trees) — callers treat that as "skip".
      seed_overloads  seedXxxRoles-name -> set of fields its std::nullptr_t
                      teardown overload nulls.
    """
    if not APPCONTEXT_HEADER.is_file():
        return None, {}
    raw = APPCONTEXT_HEADER.read_text(encoding="utf-8", errors="replace")
    text = _blank_comments(raw)
    m = re.search(r"struct\s+ManagerRefs\s*\{", text)
    if not m:
        return None, {}
    open_idx = m.end() - 1
    body = text[open_idx + 1 : _brace_body_end(text, open_idx) - 1]
    fields = MANAGER_REFS_FIELD_RE.findall(body)
    seed_overloads: dict[str, set[str]] = {}
    for sm in SEED_NULL_OVERLOAD_RE.finditer(body):
        so = sm.end() - 1
        sbody = body[so + 1 : _brace_body_end(body, so) - 1]
        seed_overloads[sm.group(1)] = set(NULLED_FIELD_RE.findall(sbody))
    return fields, seed_overloads


def ctx_role_aliases() -> dict[str, str]:
    """Role-accessor -> underlying-manager alias table (Kartend audit).

    Mirrors the seedXxxRoles() groupings in applicationcontext.h by deriving
    them from the parsed teardown overloads: within each group the single
    `*Manager` field is the facade, every other field is a role view of the
    same object (scrollGrid -> scrollManager, wheelAnimator ->
    animationManager, userActivity -> artworkManager, …). Role reads through
    ctx must count toward their manager's breadth in the fan-out ratchet —
    otherwise narrowing a file onto role interfaces (the recommended fix for
    a ratchet trip) would make its coupling invisible instead of visible.
    Empty when applicationcontext.h is absent (fixture trees).
    """
    _, seed_overloads = _manager_refs_model()
    aliases: dict[str, str] = {}
    for nulled in seed_overloads.values():
        facades = [f for f in nulled if f.endswith("Manager")]
        if len(facades) != 1:
            continue  # ambiguous group — leave its members unaliased
        for field in nulled:
            if field != facades[0]:
                aliases[field] = facades[0]
    return aliases


def compute_ctx_fanout() -> tuple[dict[str, list[tuple[str, int]]], dict[str, set[str]]]:
    """Scan src/ for `ctx->xxxManager()` sibling reads.

    Comments are blanked first (reusing _blank_comments) so documentation
    examples and placeholders like `ctx->xxxManager()` don't inflate counts.

    Role-accessor reads (`ctx->scrollGrid()`, `ctx->wheelAnimator()`, …) are
    counted toward their underlying manager via ctx_role_aliases(), so a file
    that swaps `ctx->scrollManager()` for three scroll role views still shows
    (and ratchets) as reaching one manager — not as reaching zero.

    Returns:
      by_manager  manager-name -> list of (relative_path, line) call sites
                  (incoming fan-out: how many places reach this manager)
      by_file     relative_path -> set of distinct managers it reaches
                  (outgoing fan-out: how wide a single file's ctx reach is)
    """
    aliases = ctx_role_aliases()
    by_manager: dict[str, list[tuple[str, int]]] = {}
    by_file: dict[str, set[str]] = {}
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = _blank_comments(raw)
        rel = str(path.relative_to(REPO))
        for m in CTX_FANOUT_RE.finditer(text):
            accessor = m.group(1)
            manager = aliases.get(accessor)
            if manager is None:
                if not accessor.endswith("Manager"):
                    continue  # isValid()/currentIndex()/interactionState()/…
                manager = accessor
            line = raw[: m.start()].count("\n") + 1
            by_manager.setdefault(manager, []).append((rel, line))
            by_file.setdefault(rel, set()).add(manager)
    return by_manager, by_file


def ctx_fanout_summary_line(by_manager: dict[str, list[tuple[str, int]]]) -> str:
    """One-line summary appended to the normal lint run."""
    total = sum(len(v) for v in by_manager.values())
    if not by_manager:
        return "check-layering: ctx fan-out — no ApplicationContext sibling reads found"
    widest = max(by_manager.items(), key=lambda kv: len(kv[1]))
    return (
        f"check-layering: ctx fan-out — {total} sibling reads across "
        f"{len(by_manager)} managers (widest: {widest[0]} {len(widest[1])}). "
        "Run check-layering.py --fanout for the full table."
    )


def report_ctx_fanout() -> int:
    """Print the full per-manager ctx-> fan-out table. Always returns 0 —
    this is an informational metric, not a pass/fail guardrail."""
    by_manager, by_file = compute_ctx_fanout()
    print("check-layering: ApplicationContext ctx-> manager fan-out (metric, no pass/fail)")
    print("  call sites reaching each sibling manager via ctx->xxxManager() / m_ctx->xxxManager()\n")
    if not by_manager:
        print("  (none found)")
        return 0

    name_w = max(len(n) for n in by_manager)
    name_w = max(name_w, len("manager"))
    print(f"  {'manager':<{name_w}}  sites  files")
    print(f"  {'-' * name_w}  -----  -----")
    total_sites = 0
    for manager, sites in sorted(
        by_manager.items(), key=lambda kv: (-len(kv[1]), kv[0])
    ):
        files = len({rel for rel, _ in sites})
        total_sites += len(sites)
        print(f"  {manager:<{name_w}}  {len(sites):>5}  {files:>5}")
    print(f"  {'-' * name_w}  -----  -----")
    print(f"  {'total':<{name_w}}  {total_sites:>5}  {len(by_file):>5}")

    # Outgoing fan-out: files reaching the most distinct managers are the
    # prime candidates for role-scoped dependency structs (Kartend-7pawj).
    widest_files = sorted(by_file.items(), key=lambda kv: (-len(kv[1]), kv[0]))[:10]
    if widest_files and len(widest_files[0][1]) > 1:
        print("\n  Widest reachers (files touching the most distinct managers):")
        for rel, managers in widest_files:
            if len(managers) < 2:
                break
            print(f"    {len(managers):>2} managers  {rel}")
    return 0


def _fanout_snapshot() -> dict:
    """Build the serializable fan-out snapshot used by the baseline + ratchet.

    by_file_outgoing records only files at or above FANOUT_FLOOR (the ones the
    ratchet actually gates); by_manager_incoming is recorded for context only.
    Both dicts are key-sorted so the baseline file diffs cleanly.
    """
    by_manager, by_file = compute_ctx_fanout()
    outgoing = {
        rel: len(managers)
        for rel, managers in by_file.items()
        if len(managers) >= FANOUT_FLOOR
    }
    incoming = {mgr: len(sites) for mgr, sites in by_manager.items()}
    return {
        "_comment": (
            "Soft ratchet for ApplicationContext ctx-> OUTGOING fan-out (how many "
            "DISTINCT sibling managers a single file reaches). Regenerate after an "
            "intended change with: python3 .scripts/check-layering.py --write-baseline. "
            "The lint FAILS when a file's outgoing breadth rises above its baseline, "
            "or a new file crosses the floor. Lowering is always allowed (re-baseline "
            "to lock the win). by_manager_incoming is context only and is NOT gated."
        ),
        "floor": FANOUT_FLOOR,
        "by_file_outgoing": dict(sorted(outgoing.items())),
        "by_manager_incoming": dict(sorted(incoming.items())),
    }


def write_fanout_baseline() -> int:
    """Write .scripts/ctx-fanout-baseline.json from the current tree. Returns 0."""
    snapshot = _fanout_snapshot()
    FANOUT_BASELINE_PATH.write_text(
        json.dumps(snapshot, indent=2) + "\n", encoding="utf-8"
    )
    gated = snapshot["by_file_outgoing"]
    print(
        f"check-layering: wrote {FANOUT_BASELINE_PATH.relative_to(REPO)} — "
        f"{len(gated)} file(s) at/above the floor of {snapshot['floor']} "
        f"distinct managers are now ratcheted."
    )
    return 0


def check_fanout_ratchet() -> int:
    """Fail (1) if any file's outgoing ctx-> breadth exceeds its allowance.

    Allowance per file = max(FANOUT_FLOOR - 1, baseline[file]). So a file may
    freely reach up to FANOUT_FLOOR-1 managers; crossing the floor as a new hub
    or growing a baselined hub fails until re-baselined. Missing baseline file
    is a soft pass with a hint (so the ratchet is inert until adopted).
    """
    if not FANOUT_BASELINE_PATH.exists():
        print(
            "check-layering: ctx fan-out ratchet inactive — no "
            f"{FANOUT_BASELINE_PATH.relative_to(REPO)} yet. Create it with "
            "`python3 .scripts/check-layering.py --write-baseline`."
        )
        return 0

    baseline = json.loads(FANOUT_BASELINE_PATH.read_text(encoding="utf-8"))
    base_files: dict[str, int] = baseline.get("by_file_outgoing", {})
    floor = int(baseline.get("floor", FANOUT_FLOOR))

    _, by_file = compute_ctx_fanout()
    current = {rel: len(mgrs) for rel, mgrs in by_file.items()}

    regressions: list[tuple[str, int, int]] = []  # (file, allowed, current)
    for rel, count in sorted(current.items()):
        allowed = max(floor - 1, base_files.get(rel, 0))
        if count > allowed:
            regressions.append((rel, allowed, count))

    if regressions:
        print(
            "\ncheck-layering: ApplicationContext ctx-> fan-out RATCHET — a file's "
            "outgoing breadth grew beyond its baseline:"
        )
        for rel, allowed, count in regressions:
            print(f"  {rel}: reaches {count} distinct managers (allowed {allowed})")
        print(
            "\nThis hub coupling is what the role-scoped dependency-struct effort "
            "is unwinding — prefer narrowing the file onto a role interface (see "
            "IScrollManager's six role views) over widening it. If the growth is "
            "intentional, accept it with:\n"
            "  python3 .scripts/check-layering.py --write-baseline"
        )
        return 1

    # Note (non-failing) when a baselined hub shrank, so the win can be locked.
    improved = [
        rel for rel, base in base_files.items() if current.get(rel, 0) < base
    ]
    if improved:
        print(
            f"check-layering: ctx fan-out ratchet OK — {len(improved)} hub(s) "
            "narrowed below baseline; re-run --write-baseline to lock the win."
        )
    else:
        print("check-layering: ctx fan-out ratchet OK — no hub grew past its baseline.")
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--fanout" in args:
        sys.exit(report_ctx_fanout())
    if "--write-baseline" in args:
        sys.exit(write_fanout_baseline())
    if "--check-fanout" in args:
        sys.exit(check_fanout_ratchet())
    sys.exit(main())
