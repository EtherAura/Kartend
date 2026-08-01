# Module layering

`src/` is split into per-area OBJECT libraries (`kartend_utils`, `_api`,
`_chrome`, `_data`, `_input`, `_media`, `_behavior`, `_ui`, `_core`) wired
bottom-up with an explicit `target_link_libraries` DAG ([src/CMakeLists.txt](../../src/CMakeLists.txt)).
Each area publishes only its **own** directories on its PUBLIC include path
and inherits lower layers through the DAG, and there is no global
`include_directories()` umbrella — so an upward `#include "foo.h"` by basename
**already fails to compile**: the header simply isn't on that layer's include
path. That compile-time scoping is the first line of defence.

The [.scripts/check-layering.py](../../.scripts/check-layering.py) pre-push
lint is the **residual backstop**, not the only guardrail. It catches what
include-scoping cannot — a subpath-style upward include that resolves through
a directory placed on a lower layer's PUBLIC path, and basename collisions
that would silently mask a real violation — plus several DI / ctx-accessor
invariants that aren't about include paths at all. It runs locally on push so
violations are caught before CI.

## The layers

```
       ┌──────────────────────┐
       │  src/core/           │   App entry, MainWindow, controllers
       └──────────────────────┘
              ▲
              │
       ┌──────────────────────┐
       │  src/modules/behavior│   ApplicationManager (manager lifecycles —
       └──────────────────────┘   owns the ui-layer DetailsPaneManager, so
              ▲                   it sits ABOVE ui/; its only forbidden
              │                   upward edge is core/)
       ┌──────────────────────┐
       │  src/ui/             │   Dialogs, widgets coupled to ui/
       └──────────────────────┘
              ▲
              │
       ┌──────────────────────┐
       │  src/modules/        │   Feature managers (input/data/media)
       └──────────────────────┘
              ▲
              │
       ┌──────────────────────┐
       │  src/chrome/         │   Neutral, "dumb" widgets shared between layers
       └──────────────────────┘
              ▲
              │
       ┌──────────────────────┐
       │  src/utils/          │   Foundation: types, helpers, no widgets
       └──────────────────────┘

       (api/ headers can be included from anywhere — they're role
        interfaces, no implementation. In the other direction api/ may
        include only src/utils/ — value types that appear in interface
        signatures; chrome/modules/ui/core includes from api/ are
        upward edges and linted.)
```

**Rule of thumb**: arrows are includes. Each layer can include from
itself or any layer below it; **never** from a layer above.

## What each layer is for

| Layer | Holds | Doesn't hold |
|-------|-------|--------------|
| `src/api/` | Header-only role interfaces (`iartworkmanager.h`, `iselectionmanager.h`, …). No `.cpp` files. | Anything stateful or imperative. |
| `src/utils/` | Pure helpers: types, validation, math, path tools, threading utilities. | QWidgets, manager state, business logic. |
| `src/chrome/` | "Dumb" widgets — pixmaps and strings in / Qt signals out. `ItemWidget`, `CoverFlowWidget`, `VideoPreviewWidget`, overlay layer manager. | Coupling to the ui/ layer. Domain logic. |
| `src/modules/` | Feature managers grouped by domain (`input/`, `data/`, `media/`). `behavior/` is the exception that sits ABOVE `src/ui/` — `ApplicationManager` owns the ui-layer `DetailsPaneManager` controller (sanctioned in `src/CMakeLists.txt`); its only forbidden upward include is `core/` (linted, Kartend-1ha73). | Direct widget creation outside the chrome boundary. |
| `src/ui/` | UI-coupled widgets and dialogs (settings dialog, details pane, marquee window). | Anything `src/chrome/` could provide more neutrally. |
| `src/core/` | `main.cpp`, `MainWindow`, top-level controllers. | Feature implementation that belongs in a module. |

## Allowed exceptions

There are currently **none** — the lint's allowlist is empty.

It previously held `src/ui/uiconstants/*.h` (pure compile-time constant
namespaces — `grid.h`, `dialog.h`, `icons.h`, …) because those headers
lived under `src/ui/` but were included from lower layers. They have
since been relocated to `src/utils/uiconstants/`, so they sit at the
foundation layer and need no exception: `#include "uiconstants/<name>.h"`
is now a same-or-lower-layer include from anywhere. (The `uiconstants/`
prefix is kept deliberately — see the `kartend_utils` include-path note
in [src/CMakeLists.txt](../../src/CMakeLists.txt).)

Don't add new allowlist entries casually. Any future entry needs a
comment in [check-layering.py](../../.scripts/check-layering.py)
justifying why the header is harmless to include from below — prefer
relocating the header into a lower layer instead.

## How the lint works

The script enforces several guardrails; the three most relevant to
day-to-day work are:

**1. Include layering.** For every `*.cpp` and `*.h` under the linted
layers (`src/utils/`, `src/chrome/`, `src/api/`, `src/ui/`, and each
`src/modules/` sub-area):

1. Scans for `#include "x.h"` directives (quoted, not bracketed).
2. Maps each included header back to its top-level area
   (`src/utils/`, `src/modules/`, etc.) via a basename-to-area
   table built from a full repo walk.
3. Fails if the included header's area is "above" the includer's.

**2. Setup-struct DI.** Every `<Foo>Setup` struct must carry only
`const ApplicationContext *ctx` plus non-manager refs (widgets, value
containers, callbacks). Sibling-manager pointers belong on ctx so a
manager can't accidentally pin its siblings through a setup field.

**3. MainWindow accessor routing.** No file outside
`src/core/mainwindow*` may call `mainWindow->getXxxManager()` directly.
The canonical path is `mainWindow->getApplicationManager()->getXxxManager()`;
ui-layer dialogs reach siblings through an injected `ApplicationContext`.
`IMainWindow` exposes **only** `applicationManager()` (the former
`settingsManager()` / `scrollManager()` / `interactionManager()` forwarders
were removed in Kartend-qjtz) — the lint **fails the build** if any other
`*Manager` accessor is re-added to it.

(The script also enforces further guardrails not detailed here —
IMainWindow exposing only `applicationManager()`, the two distinct
`ctx` accessor styles staying visually apart, the read-only
`currentCollectionIndex` back-channel, and teardown slot-nulling: every
`ManagerRefs` pointer slot in `applicationcontext.h` must be nulled —
directly or via its `seedXxxRoles(nullptr)` group — inside
`ApplicationManager::destroyManagersAndClearContextSlots()`, so
teardown-phase `if (auto *m = ctx->x())` guards can't read a dangling
pointer. See the docstrings in
[check-layering.py](../../.scripts/check-layering.py).)

The lint runs:

- **Locally** on every `git push` via the
  [pre-push hook](git-hooks.md#pre-push), and as the python-guardrails
  step of `.scripts/build.sh --maintenance`.
- **In CI** in the `script-lint` job (build.yml, main/develop pushes and
  PRs) and via branch-lint.yml on every feature-branch push.

## ApplicationContext fan-out metric + ratchet

The include DAG is enforced *vertically*, but `ApplicationContext` is a
*horizontal* back-channel: any manager holding `ctx` can reach most of its
sibling managers through `ctx->xxxManager()`, and the include guardrails say
nothing about it. The lint keeps that hub coupling visible (a metric) and
keeps its worst dimension from silently growing (a soft ratchet).

**Metric (informational).**

- A normal `check-layering.py` run appends a one-line summary
  (total sibling reads, manager count, widest consumer).
- `check-layering.py --fanout` prints the full table: per-manager
  *incoming* fan-out (how many call sites reach each manager) plus the
  *widest reachers* (files touching the most distinct managers — the
  prime candidates for role-scoped dependency structs).
- Role-accessor reads (`ctx->scrollGrid()`, `ctx->wheelAnimator()`, …)
  count toward their underlying manager via an alias table derived from
  the `seedXxxRoles()` groupings in `applicationcontext.h` — narrowing a
  file onto role interfaces keeps its coupling visible rather than
  hiding it from the metric and the ratchet.

**Ratchet (soft gate, Kartend-n1hpy.1).** The normal run also gates the
**outgoing** dimension against a checked-in baseline
([.scripts/ctx-fanout-baseline.json](../../.scripts/ctx-fanout-baseline.json)):
it **fails** when a single file's distinct-manager breadth grows past its
baseline, or a new file crosses the floor (currently 3 distinct managers).
That outgoing breadth is the actual smell — a file going 9 → 10 managers is
a regression; many files each reaching one manager is not — so *incoming*
per-manager totals stay **uncapped** (they grow naturally with features).

- The ratchet is **inert until the baseline exists**, so it never blocks a
  fresh checkout that hasn't opted in.
- Lowering breadth always passes; after an intentional change run
  `check-layering.py --write-baseline` to accept the new shape.
- `check-layering.py --check-fanout` runs only the ratchet.

The right response to a ratchet failure is usually to narrow the file onto
a **role interface** (see `IScrollManager`'s six role views) rather than
re-baseline upward. This feeds the longer-term effort to thin `ctx` into
per-consumer dependency structs rather than passing the whole hub everywhere.

## Common violations and fixes

### "I need to call a manager from a utility"

**Don't.** The utility is the wrong shape — it's claiming
foundation-layer purity but really needs the world. Two fixes
depending on what the util is doing:

- **Pass the values in** as parameters. Most utility functions
  don't actually need the manager; they need the manager's *output*.
  Have the call site fetch it and pass it down.
- **Move the helper into the module.** If the function legitimately
  needs to know about the manager's lifecycle / state, it isn't a
  utility — it's part of the manager.

### "I need an enum / struct that lives in a module"

Move the type into `src/utils/`. The `src/utils/app/collection/`
leaf headers are the precedent — many of them carry types that the
modules consume, but the types themselves are pure data with no
upward dependency.

### "I need a widget from `src/ui/` in a module"

Look at `src/chrome/` first — most widgets that modules want
(`ItemWidget`, `CoverFlowWidget`, `VideoPreviewWidget`) live there
specifically so modules can use them. If your widget genuinely
shouldn't be in chrome (it's tightly coupled to a dialog, say),
the module probably shouldn't be reaching for it — wire the widget
in from the call site at the ui/ layer.

### "But it builds locally"

If you added an upward **basename** include, it usually *won't* build — the
header isn't on your layer's PUBLIC include path (see the per-area OBJECT-lib
DAG above). What can still slip past the compiler is a subpath-style include
that resolves via a lower layer's PUBLIC path, or a basename collision masking
a real violation — and that's exactly what the lint catches. So if the lint
fails on something the build accepted, the lint is right: fix the include,
don't bypass the lint.

## What enforces the layering today

The per-area split has **already landed** — this is not future work. Each
layer is its own CMake OBJECT library with PUBLIC include-scoping and a
`target_link_libraries` DAG ([src/CMakeLists.txt](../../src/CMakeLists.txt)),
the `kartend_lib` INTERFACE aggregator is gone, and the `ui/` controllers
(e.g. DetailsPaneManager) sit at their proper layer. Two mechanisms guard
the DAG:

1. **Compile-time include-scoping** (primary) — an upward basename
   `#include` doesn't resolve on the consumer's PUBLIC include path and
   fails to build.
2. **The pre-push / CI lint** (residual backstop) — catches the subpath /
   collision cases the compiler can't, plus the setup-struct DI and
   ctx-accessor-style invariants.

A road **not** taken: converting the OBJECT libs to STATIC to get *link-time*
layering enforcement was investigated and **declined** (Kartend-q3vfq). It
wouldn't help — every area links into one executable
(`target_link_libraries(kartend PRIVATE ${KARTEND_AREA_LIBS})`,
[CMakeLists.txt](../../CMakeLists.txt)) and usage requirements propagate
identically for OBJECT and STATIC, so all symbols resolve at the final link
regardless; STATIC would only risk dropping Qt meta-object TUs the linker
sees as unused. The compile-time scoping above is the real enforcement, and
the lint stays load-bearing for the residual cases it can't see.

## Other discoverability gotchas

A handful of architecture / build rules used to live only in the
agent-facing `.github/copilot-instructions.md`. The contributor-
facing equivalents are now in proper dev docs:

| Rule | Lives in |
|------|----------|
| `ctest` must run from the **build directory `build.sh` just wrote** — stale binaries pass silently from a parent dir | [testing.md](testing.md), [ci-local.md](ci-local.md) |
| Qt 6.4 vs 6.5+ API gotchas (silent `QHash::insert` copy, etc.) | [ci-local.md](ci-local.md) |
| `clang-format` must be v19 (system v21 drifts) | [git-hooks.md](git-hooks.md), [ci-local.md](ci-local.md) |
| Reproduce CI in the `kartend-ci` Docker image before pushing | [ci-local.md](ci-local.md) |
| Atomic file writes via `QSaveFile` + `PathUtils::syncDirectory` | [architecture.md](architecture.md#atomic-file-writes) |

If you find yourself reaching for `.github/copilot-instructions.md`
for a rule that should be in the dev docs, move it — the
copilot-instructions file is for agent-specific workflow (beads,
prompt-injection mitigation, etc.), not architectural facts every
contributor needs.

## Related code

| Concern | File |
|---------|------|
| Layering lint | [.scripts/check-layering.py](../../.scripts/check-layering.py) |
| Pre-push gate | [.scripts/git-hooks/pre-push](../../.scripts/git-hooks/pre-push) + [git-hooks.md](git-hooks.md) |
| Layer roots | `src/utils/`, `src/chrome/`, `src/modules/`, `src/ui/`, `src/core/`, `src/api/` |
| Module breakdown | [architecture.md](architecture.md) |
