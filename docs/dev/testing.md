# Testing

Unit tests use the Qt Test framework with CTest integration.

## Building Tests

Fast path using the build script:

```bash
./.scripts/build.sh --tests --run-tests
```

Manual CMake build:

```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARTEND_BUILD_TESTS=ON
cmake --build build/ninja-release --parallel $(nproc)
```

## Running Tests

```bash
# Run all tests via CTest
ctest --test-dir build/ninja-release --output-on-failure

# Run a single test by its CTest NAME — note the name is the CamelCase
# label from add_test(NAME ...), NOT the binary name (see "Adding New
# Tests"): e.g. binary test_gridlayoutcalculator registers as
# GridLayoutCalculator. -R takes a regex, so a prefix matches too.
ctest --test-dir build/ninja-release -R GridLayoutCalculator --output-on-failure

# Run individual test binary directly (CMake places every test binary
# directly under build/<config>/tests/ regardless of where the source lives)
cd build/ninja-release
./tests/test_gridlayoutcalculator
./tests/test_sessionmanager
./tests/test_artworkmanager
./tests/test_integration       # single binary for all tests/integration/*

# Run one suite (class) inside the integration binary via its leading
# class-name selector (these are the per-class ctest entries too):
./tests/test_integration TestNavigationManager
# ...and narrow to a single QTest slot by appending the function name
# (the selector is stripped; the rest passes through to QTest::qExec):
./tests/test_integration TestNavigationManager testCollectionSelected
```

## Sanitizers (optional)

Sanitizers are useful for catching memory safety bugs (use-after-free, OOB,
UB) during development.

```bash
# Build Debug + ASan/UBSan
./.scripts/build.sh --sanitize --keep-builds

# Configure and build tests
cmake -S . -B build/ninja-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKARTEND_BUILD_TESTS=ON -DKARTEND_ENABLE_SANITIZERS=ON
cmake --build build/ninja-sanitize --parallel $(nproc)

# Run the suite
ctest --test-dir build/ninja-sanitize --output-on-failure
```

## Test Coverage

Tests are grouped into seven areas. Each `test_*.cpp` is a standalone Qt
Test binary discovered by CTest, with the exception of the integration
suite which links all of its `TestXxx` classes into a single binary
(`test_integration`) driven by `tests/integration/test_main.cpp`.
(`tests/support/` holds shared fixture headers, not tests, and
`tests/suppressions/` holds sanitizer suppression files — neither is a
test area; both are described below the table.)

| Area | Path | Coverage |
|------|------|----------|
| Module unit tests | `tests/modules/<feature>/` | Per-manager and per-helper coverage for `src/modules/`. One flat folder per feature — the `behavior/data/input/media` group level is omitted. Includes `dat/` and `scraper/`. |
| Utility unit tests | `tests/utils/` | Helpers under `src/utils/` **only** (`app`, `db`, `fs`, `text`, `threading`, `view`). Tests for `src/modules/` files must NOT land here. |
| Core helper tests | `tests/core/` | Extractable helpers lifted out of `src/core/` (e.g. `GridWidthDebouncer`, title-counts helpers). Test the lift-able logic; leave the `QMainWindow`/`QWidget` shell to integration tests. See [Core Helper Tests](#core-helper-tests). |
| Integration tests | `tests/integration/` | One binary (`test_integration`). `MainWindowFixture`-driven multi-manager scenarios; shared mocks under `tests/integration/mocks/`. |
| UI widget tests | `tests/ui/widgets/` | Widget-level rendering and behavior for generic `src/ui/widgets/` widgets (e.g. `CoverFlowWidget`). Tests for widgets that live under `src/modules/` go in `tests/modules/<feature>/`. |
| UI dialog tests | `tests/ui/dialogs/` | Dialog, panel, and dialog-controller coverage for `src/ui/` dialogs (e.g. `BulkEditDialog`, `ScraperSettingsPanel`, `GamepadCaptureController`). Each is a standalone binary. |
| Benchmarks | `tests/benchmarks/` | Perf benchmarks labelled `benchmark`; skipped by default. Run with `ctest -L benchmark`. |

**Shared fixtures (`tests/support/`).** Header-only test support reused
across areas — `TestSandbox`, `InspectorDb`, `MacHomeSandbox`,
`ScopeExit`. These are `#include`d by test files; they are not themselves
tests and register no CTest binaries. **Sanitizer suppressions
(`tests/suppressions/`)** holds the ASan/UBSan/TSan suppression lists
consumed by sanitizer builds — likewise not a test area.

Binary and method counts drift fast — prefer `ctest --output-on-failure
--test-dir build/ninja-release` for an authoritative list and pass count.

### Module → Test Folder Mapping

Every `src/modules/<group>/<feature>/` owns a matching
`tests/modules/<feature>/` (group level dropped). The mapping is
machine-checked by `.scripts/check-test-mapping.py` in the
maintenance-check CI job — adding a new module without a test folder, or
leaving a test folder behind after deleting a module, fails the lint.

| Source folder | Test folder |
|---|---|
| `src/modules/behavior/application/` | *(integration-only — `tests/integration/test_applicationmanager_lifecycle.cpp`)* |
| `src/modules/data/cache/` | `tests/modules/cache/` |
| `src/modules/data/dat/` | `tests/modules/dat/` |
| `src/modules/data/database/` | `tests/modules/database/` |
| `src/modules/data/dataudit/` | `tests/modules/dataudit/` |
| `src/modules/data/kart/` | `tests/modules/kart/` |
| `src/modules/data/playlist/` | `tests/modules/playlist/` |
| `src/modules/data/query/` | `tests/modules/query/` |
| `src/modules/data/restore/` | `tests/modules/restore/` |
| `src/modules/data/scraper/` | `tests/modules/scraper/` |
| `src/modules/data/session/` | `tests/modules/session/` |
| `src/modules/data/settings/` | `tests/modules/settings/` |
| `src/modules/data/watcher/` | `tests/modules/watcher/` |
| `src/modules/input/animation/` | `tests/modules/animation/` |
| `src/modules/input/attract/` | `tests/modules/attract/` |
| `src/modules/input/event/` | `tests/modules/event/` |
| `src/modules/input/filter/` | `tests/modules/filter/` |
| `src/modules/input/gamepad/` | `tests/modules/gamepad/` |
| `src/modules/input/interaction/` | `tests/modules/interaction/` |
| `src/modules/input/keyboard/` | `tests/modules/keyboard/` |
| `src/modules/input/launch/` | `tests/modules/launch/` |
| `src/modules/input/mouse/` | `tests/modules/mouse/` |
| `src/modules/input/navigation/` | `tests/modules/navigation/` |
| `src/modules/input/overlay/` | `tests/modules/overlay/` |
| `src/modules/input/scroll/` | `tests/modules/scroll/` |
| `src/modules/input/search/` | `tests/modules/search/` |
| `src/modules/input/selection/` | `tests/modules/selection/` |
| `src/modules/input/viewport/` | `tests/modules/viewport/` |
| `src/modules/input/widgetpool/` | `tests/modules/widgetpool/` |
| `src/modules/media/artwork/` | `tests/modules/artwork/` |
| `src/modules/media/detailpage/` | `tests/modules/detailpage/` |
| `src/ui/controllers/detailspanemanager/` | *(integration-only — `tests/integration/test_eventmanager_detailspane.cpp`, `test_detailspane_coverflow.cpp`; moved here from `src/modules/media/detailspane/` when DetailsPaneManager was relayered as a ui-layer controller)* |

Integration-only features (no `tests/modules/<feature>/`) are listed in
`INTEGRATION_ONLY` inside `check-test-mapping.py`; new additions there
need a comment pointing to the integration test that covers them.

### Core Helper Tests

`src/core/` is the application's largest layer by line count and its
largest untested area — its biggest files are the `MainWindow` split
(`mainwindow_dialogs`, `mainwindow_setup`, `mainwindow_wiring`) and the
toolbar/menu controllers (`menucontroller`, `toolbarcontroller`). Most of
that bulk is `QMainWindow`/`QWidget` shell code that's only reachable
through the full widget graph, so it's exercised via the integration
binary rather than in isolation.

`tests/core/` holds standalone unit tests for the **extractable helpers**
lifted out of that shell — pure logic with no widget dependency, such as
`GridWidthDebouncer` (`tests/core/test_gridwidthdebouncer.cpp`) and the
title-counts helpers (`tests/core/test_titlecountshelpers.cpp`). Follow
the same rule used elsewhere in this doc: **test the extractable helpers,
leave the `QWidget` shell.** When adding logic under `src/core/`, prefer
to factor the lift-able part into a small helper (mirroring
`titlecountshelpers` / `gridwidthdebouncer`) and unit-test it here; cover
the surrounding `MainWindow`/controller wiring through
`tests/integration/` instead.

**Enforcement: `src/core/` is NOT mapping-tracked, by design.** Unlike
`src/modules/` (bidirectional per-feature mapping) and `tests/utils/`
(cluster mirror), `check-test-mapping.py` enforces **no** structural rule
for `src/core/`: a new `src/core/` file will *not* trip the mapping lint.
This is intentional — most of `src/core/` is shell code that can't be
unit-tested in isolation, so a per-file mapping requirement would be
almost all false positives. Instead, the script emits a **non-fatal
advisory coverage report** for `src/core/` and `src/chrome/`
(`report_core_chrome_coverage`, added in Kartend-tu2hq): it lists every
`.cpp` with no matching `test_<name>.cpp` so the gap stays visible, but it
**never affects the lint's exit status**. Raising core coverage is
incremental work, not a gate.

## Adding New Tests

> **Single-file vs header convention.** Standalone unit tests (under
> `tests/modules/`, `tests/utils/`, `tests/ui/`) declare the `TestXxx` class
> **inline** in the `.cpp` with `QTEST_MAIN(...)` + `#include "test_x.moc"` — no
> separate header. **Integration** tests (`tests/integration/`) are the
> exception: their `Q_OBJECT` class lives in a `.h` so AUTOMOC picks it up when
> the class is linked into the shared `test_integration` binary (a `QTEST_MAIN`
> per class can't work there — see the Integration Test Harness section). So a
> separate `test_*.h` signals "integration suite," not a style choice; prefer
> the inline form everywhere else.

1. Create the test file mirroring the source location. A test for a
   `src/modules/` file goes in `tests/modules/<feature>/` — the
   `behavior/data/input/media` group level is dropped (e.g. a test for
   `src/modules/data/cache/cachemanager.cpp` lives at
   `tests/modules/cache/test_cachemanager.cpp`; a test for
   `src/modules/data/scraper/parsers/tmdbparser.cpp` lives at
   `tests/modules/scraper/test_tmdbparser.cpp`). A test for a `src/utils/`
   file goes in `tests/utils/` (e.g. `tests/utils/test_pathutils.cpp`).
   Use the Qt Test structure:

```cpp
#include <QTest>
#include "classname.h"

class TestClassName : public QObject {
  Q_OBJECT
private slots:
  void testSomething();
};

void TestClassName::testSomething() {
  QCOMPARE(actual, expected);
}

QTEST_MAIN(TestClassName)
#include "test_classname.moc"
```

2. Add to `tests/CMakeLists.txt` via the `kartend_add_test()` helper
   (Kartend-j0yin). `LINK` names the per-area lib(s) the source compiles
   into (plus their downward deps) — there is **no** `${SOURCES}` variable.
   A `src/utils/` test usually needs only `kartend_utils`; a `src/modules/`
   test links its area-lib closure (e.g. `kartend_input kartend_data
   kartend_chrome kartend_api kartend_utils`). The source path is relative
   to `tests/`, the `NAME` is CamelCase and independent of the binary name,
   and the build target is derived from the first source's basename
   (`utils/text/test_classname.cpp` → `test_classname`). `Qt6::Test` is
   appended automatically — don't list it.

```cmake
# Utility test — links only kartend_utils:
kartend_add_test(NAME ClassName
  SOURCES utils/text/test_classname.cpp
  LINK kartend_utils
)

# Module test — links the area-lib closure (see existing entries for the
# exact set per area), e.g. a src/modules/input/ test:
#   LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
```

   The helper rewrites each area lib to its `_static` twin (an archive of
   the same object files, declared at the top of `tests/CMakeLists.txt`) so
   the linker dead-strips unreferenced objects instead of embedding every
   area `.o` the way a direct `OBJECT`-lib link does. Extra per-target
   tweaks (`set_tests_properties`, `target_include_directories`,
   `target_compile_definitions`, conditional links) go as trailing
   statements referencing the derived target name.

## Integration Test Harness (UI-Coordinator Managers)

Pure unit tests are not viable for managers that include `ui_*.h` headers and
reference the full MainWindow widget graph (`ApplicationManager`,
`NavigationManager`, `ScrollManager`, `InteractionManager`, etc.). Those
managers run inside a **single shared integration binary** at
`tests/integration/test_integration` so we pay the full per-area `OBJECT`-lib
link closure once instead of per test.

### Architecture

- **Per-area `OBJECT` libraries** declared in `src/CMakeLists.txt`:
  `kartend_utils`, `kartend_api`, `kartend_chrome`, `kartend_data`,
  `kartend_input`, `kartend_media`, `kartend_behavior`, `kartend_ui`,
  `kartend_core`. Each lib publishes its own `PUBLIC` include directories
  and links upward to its dependencies via `target_link_libraries(<area>
  PUBLIC <dep>)`, forming a CMake-enforced layering DAG (utils → api →
  chrome → data → input/media → ui → behavior → core). Replaced the prior
  `kartend_lib` INTERFACE aggregator in the per-area-library refactor.
- `kartend` (executable) — `src/core/main.cpp` plus
  `target_link_libraries(kartend PRIVATE ${KARTEND_AREA_LIBS})`. OBJECT libs
  don't propagate `.o` files through `INTERFACE_LINK_LIBRARIES`, so every
  consumer enumerates the area libs directly.
- `tests/integration/test_integration` — Qt Test binary that links the same
  per-area libs and adds the `kartend_{chrome,core,ui}_autogen/include`
  paths so it can see AUTOUIC-generated `ui_*.h` headers.

LTO link options for Release builds live in the `KARTEND_LTO_LINK_OPTIONS`
list at the top-level CMakeLists.txt and are applied directly to both the
executable and the integration-test binary (`-flto=auto`, plus
`-fuse-ld=lld` under Clang).

### MainWindowFixture

`tests/integration/mainwindowfixture.h` provides an RAII fixture that
constructs a fully-wired `MainWindow` against an isolated `QStandardPaths`
sandbox:

```cpp
#include "mainwindowfixture.h"
#include "navigationmanager.h"

void TestNavigationManager::testCollectionSelected() {
  KartendTest::MainWindowFixture fixture;
  auto *nav = fixture.window()->getNavigationManager();
  QSignalSpy spy(nav, &NavigationManager::collectionChanged);
  nav->onCollectionSelected(0);
  QCOMPARE(spy.count(), 1);
}
```

Key properties:

- Calls `QStandardPaths::setTestModeEnabled(true)` and wipes the per-user
  qttest sandbox (`~/.qttest/...` on Linux) before constructing
  `MainWindow`, so settings/session/cache/database files never touch real
  user data.
- Tears down the window first so `SessionManager` flushes JSON while the
  sandbox is still active, then disables test mode.
- Does **not** call `show()`. Tests that need rendering can do
  `fixture.window()->show()` themselves; the `offscreen` QPA platform plugin
  is selected by `test_main.cpp` so this works on headless CI.
- **Sandbox escape guard (Kartend-jcj7):** the constructor verifies
  `QStandardPaths::isTestModeEnabled()` actually stuck, and snapshots the
  developer's real per-app config / data dirs (`~/.config/kartend`,
  `<appConfig>/`, `<appData>/`). The destructor recaptures and `qFatal`s
  the test run if any file in those directories was created or modified —
  the only way that can happen is a code path that constructs `QSettings`
  with an absolute path, bypassing `QStandardPaths`. Keep new persistence
  code routed through `SettingsUtils::getConfigPath()` / `QStandardPaths`
  so this guard stays silent.

### Adding a New Integration Test

1. Create `tests/integration/test_<feature>.{h,cpp}` with a slots-only
   `QObject` test class. Put the `Q_OBJECT` macro in the header so AUTOMOC
   picks it up (AUTOMOC and AUTOUIC are inherited from the top-level
   CMakeLists.txt).
2. Instantiate the class in `tests/integration/test_main.cpp` and add it to
   the `QTest::qExec(...)` chain.
3. Append the new sources to `INTEGRATION_TEST_SOURCES` /
   `INTEGRATION_TEST_HEADERS` in `tests/integration/CMakeLists.txt`.

The existing `test_mainwindow_smoke.{h,cpp}` is the smallest end-to-end
example to copy from.

### Why Not Per-Manager Test Binaries?

Each integration test binary statically pulls the full per-area-libs closure
(~140 TUs, full Qt Widgets/Sql/Concurrent link). One binary per manager would
balloon CI link time and disk usage. The shared-binary trade-off is that all
tests share a single `QApplication` and process, so a hard crash in one slot
stops the rest — but slots are isolated by `MainWindowFixture` constructing
and destroying a fresh `MainWindow` per test.
