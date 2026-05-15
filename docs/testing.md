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

Tests are grouped into four areas. Each `test_*.cpp` is a standalone Qt
Test binary discovered by CTest, with the exception of the integration
suite which links all of its `TestXxx` classes into a single binary
(`test_integration`) driven by `tests/integration/test_main.cpp`.

| Area | Path | Binaries | Coverage |
|------|------|----------|----------|
| Module unit tests | `tests/modules/<feature>/` | 37 | Per-manager and per-helper coverage mirroring `src/modules/<feature>/` |
| Utility unit tests | `tests/utils/` | 15 | Helpers under `src/utils/` (cliargs, collectionutils, configvalidation, dbmigrations, gridutils, historystore, itemartwork, itemmetadata, itemmetadatacache, pathutils, searchutils, stringutils, titlefilter, usagestatsstore, videoutils) |
| Integration tests | `tests/integration/` | 1 | `MainWindowFixture`-driven multi-manager scenarios (application lifecycle, settings dialog apply / changes / scope, scroll, navigation, details-pane coverflow, event-manager wiring, mainwindow smoke) |
| UI widget tests | `tests/ui/widgets/` | 2 | Widget-level rendering and behavior (`CoverflowWidget`, `EmptyStateWidget`) |

**Total: 64 `test_*.cpp` files, ~340 test methods across 55 binaries.**
Method counts drift fast — prefer `ctest --output-on-failure --test-dir
build/ninja-release` for an authoritative pass count.

## Adding New Tests

1. Create `tests/<area>/test_<classname>.cpp` mirroring the source location
   (e.g. a test for `src/modules/data/cache/cachemanager.cpp` lives at
   `tests/modules/cache/test_cachemanager.cpp`; a test for
   `src/utils/fs/pathutils.cpp` lives at `tests/utils/test_pathutils.cpp`).
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
`tests/integration/test_integration` so we pay the full kartend_lib link
closure once instead of per test.

### Architecture

- `kartend_lib` — top-level CMake `OBJECT` library that compiles every Kartend
  source file (everything except `src/core/main.cpp`). It owns `PUBLIC`
  include directories, compile definitions, and Qt link dependencies, so any
  consumer of the library inherits them.
- `kartend` (executable) — `src/core/main.cpp` plus
  `target_link_libraries(kartend PRIVATE kartend_lib)`.
- `tests/integration/test_integration` — Qt Test binary that also links
  `kartend_lib` and adds the `kartend_lib_autogen/include` path so it can see
  AUTOUIC-generated `ui_*.h` headers.

LTO link options for Release builds are set as `INTERFACE` link options on
`kartend_lib`, so both the executable and the test harness link with the
matching `-flto=auto` (and `-fuse-ld=lld` under Clang) without each consumer
needing to know about it.

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

Each integration test binary statically pulls the full `kartend_lib` closure
(~140 TUs, full Qt Widgets/Sql/Concurrent link). One binary per manager would
balloon CI link time and disk usage. The shared-binary trade-off is that all
tests share a single `QApplication` and process, so a hard crash in one slot
stops the rest — but slots are isolated by `MainWindowFixture` constructing
and destroying a fresh `MainWindow` per test.
