# Module layering

`src/` is split into layers, but the build links everything into one
object library — there's no `target_link_libraries` boundary forcing
the layering at link time. The
[.scripts/check-layering.py](../../.scripts/check-layering.py) pre-push
lint catches violations before they hit CI.

## The layers

```
       ┌────────────────┐
       │  src/core/     │   App entry, MainWindow, controllers
       └────────────────┘
              ▲
              │
       ┌────────────────┐
       │  src/ui/       │   Dialogs, widgets coupled to ui/
       └────────────────┘
              ▲
              │
       ┌────────────────┐
       │  src/modules/  │   Feature managers (input/data/media/behavior)
       └────────────────┘
              ▲
              │
       ┌────────────────┐
       │  src/chrome/   │   Neutral, "dumb" widgets shared between layers
       └────────────────┘
              ▲
              │
       ┌────────────────┐
       │  src/utils/    │   Foundation: types, helpers, no widgets
       └────────────────┘

       (api/ headers can be included from anywhere — they're role
        interfaces, no implementation)
```

**Rule of thumb**: arrows are includes. Each layer can include from
itself or any layer below it; **never** from a layer above.

## What each layer is for

| Layer | Holds | Doesn't hold |
|-------|-------|--------------|
| `src/api/` | Header-only role interfaces (`iartworkmanager.h`, `iselectionmanager.h`, …). No `.cpp` files. | Anything stateful or imperative. |
| `src/utils/` | Pure helpers: types, validation, math, path tools, threading utilities. | QWidgets, manager state, business logic. |
| `src/chrome/` | "Dumb" widgets — pixmaps and strings in / Qt signals out. `ItemWidget`, `CoverFlowWidget`, `VideoPreviewWidget`, overlay layer manager. | Coupling to the ui/ layer. Domain logic. |
| `src/modules/` | Feature managers grouped by domain (`input/`, `data/`, `media/`, `behavior/`). | Direct widget creation outside the chrome boundary. |
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

**1. Include layering.** For every `*.cpp` and `*.h` under `src/utils/`
and `src/chrome/`:

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
The documented paths are `mainWindow->applicationManager()->getXxxManager()`
(fallback) or one of the three `IMainWindow` forwarders
(`settingsManager()`, `scrollManager()`, `interactionManager()`) used by
ui-layer settings dialogs. `getApplicationManager()` itself is the
canonical hop and is always allowed.

(The script also enforces two further guardrails not detailed here —
IMainWindow exposing only `applicationManager()`, and the two distinct
`ctx` accessor styles staying visually apart. See the docstrings in
[check-layering.py](../../.scripts/check-layering.py).)

The lint runs:

- **Locally** on every `git push` via the
  [pre-push hook](git-hooks.md#pre-push).
- **In CI** as the `lint` job, before the build job starts.

## ApplicationContext fan-out metric (not a guardrail)

The include DAG is enforced *vertically*, but `ApplicationContext` is a
*horizontal* back-channel: any manager holding `ctx` can reach ~17
siblings through `ctx->xxxManager()`, and the guardrails above say
nothing about it. To keep that hub coupling visible, the lint reports a
**fan-out metric** — it is a measurement, **not** a pass/fail check, so
it never fails the build (capping it would only grandfather today's
state or block unrelated work).

- A normal `check-layering.py` run appends a one-line summary
  (total sibling reads, manager count, widest consumer).
- `check-layering.py --fanout` prints the full table: per-manager
  *incoming* fan-out (how many call sites reach each manager) plus the
  *widest reachers* (files touching the most distinct managers — the
  prime candidates for role-scoped dependency structs).

This feeds the longer-term effort to thin `ctx` into per-consumer
dependency structs rather than passing the whole hub everywhere.

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

The build doesn't enforce layering — it's all one library. The
lint is the only guardrail. Fix the include, don't bypass the lint.

## Long-term direction

The eventual goal is to split each layer into its own CMake target
with explicit `target_link_libraries`, so the compiler enforces what
the lint does today. Initial steps have landed (the DetailsPaneManager
move to `ui/`, the deletion of the `kartend_lib` INTERFACE aggregator
in favour of per-area OBJECT libraries); the remaining work is
incremental. Until each layer is its own CMake target with no
back-doors, the lint is load-bearing.

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
