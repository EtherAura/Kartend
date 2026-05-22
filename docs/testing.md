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

# Run individual test (CMake places every test binary directly under
# build/<config>/tests/ regardless of where the source lives)
cd build/ninja-release
./tests/test_gridlayoutcalculator
./tests/test_sessionmanager
./tests/test_artworkmanager
./tests/test_integration       # single binary for all tests/integration/*
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

Tests are grouped into five areas. Each `test_*.cpp` is a standalone Qt
Test binary discovered by CTest, with the exception of the integration
suite which links all of its `TestXxx` classes into a single binary
(`test_integration`) driven by `tests/integration/test_main.cpp`.

| Area | Path | Coverage |
|------|------|----------|
| Module unit tests | `tests/modules/<feature>/` | Per-manager and per-helper coverage for `src/modules/`. One flat folder per feature — the `behavior/data/input/media` group level is omitted. Includes `dat/` and `scraper/`. |
| Utility unit tests | `tests/utils/` | Helpers under `src/utils/` **only** (`app`, `db`, `fs`, `text`, `threading`, `view`). Tests for `src/modules/` files must NOT land here. |
| Integration tests | `tests/integration/` | One binary (`test_integration`). `MainWindowFixture`-driven multi-manager scenarios; shared mocks under `tests/integration/mocks/`. |
| UI widget tests | `tests/ui/widgets/` | Widget-level rendering and behavior for generic `src/ui/widgets/` widgets (e.g. `CoverFlowWidget`). Tests for widgets that live under `src/modules/` go in `tests/modules/<feature>/`. |
| Benchmarks | `tests/benchmarks/` | Perf benchmarks labelled `benchmark`; skipped by default. Run with `ctest -L benchmark`. |

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
| `src/modules/data/kart/` | `tests/modules/kart/` |
| `src/modules/data/playlist/` | `tests/modules/playlist/` |
| `src/modules/data/query/` | `tests/modules/query/` |
| `src/modules/data/restore/` | `tests/modules/restore/` |
| `src/modules/data/scraper/` | `tests/modules/scraper/` |
| `src/modules/data/session/` | `tests/modules/session/` |
| `src/modules/data/settings/` | `tests/modules/settings/` |
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
| `src/ui/controllers/detailspanemanager/` | *(integration-only — `tests/integration/test_eventmanager_detailspane.cpp`, `test_detailspane_coverflow.cpp`; moved from `src/modules/media/detailspane/` in Kartend-uk5z)* |

Integration-only features (no `tests/modules/<feature>/`) are listed in
`INTEGRATION_ONLY` inside `check-test-mapping.py`; new additions there
need a comment pointing to the integration test that covers them.

## Adding New Tests

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

2. Add to `tests/CMakeLists.txt`:

```cmake
add_executable(test_classname test_classname.cpp ${SOURCES})
target_link_libraries(test_classname PRIVATE Qt6::Test Qt6::Widgets ...)
add_test(NAME test_classname COMMAND test_classname)
```

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
  `kartend_lib` INTERFACE aggregator in Kartend-w1wv.
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
