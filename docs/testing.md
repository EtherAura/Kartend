# Testing

Unit tests use the Qt Test framework with CTest integration.

## Building Tests

Fast path using the build script:

```bash
./.scripts/build.sh --tests --run-tests
```

Manual CMake build:

```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build/ninja-release --parallel $(nproc)
```

## Running Tests

```bash
# Run all tests via CTest
ctest --test-dir build/ninja-release --output-on-failure

# Run individual test
cd build/ninja-release
./tests/test_gridlayoutcalculator
./tests/test_sessionmanager
./tests/test_artworkmanager
# ... one binary per suite under tests/ (24 suites total)
```

## Sanitizers (optional)

Sanitizers are useful for catching memory safety bugs (use-after-free, OOB,
UB) during development.

```bash
# Build Debug + ASan/UBSan
./.scripts/build.sh --sanitize --keep-builds

# Configure and build tests
cmake -S . -B build/ninja-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_SANITIZERS=ON
cmake --build build/ninja-sanitize --parallel $(nproc)

# Run the suite
ctest --test-dir build/ninja-sanitize --output-on-failure
```

## Test Coverage

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| `test_artworkmanager` | 15 | Artwork path resolution, batch loading, suppression |
| `test_cachemanager` | 15 | Pixmap cache, LRU eviction, disk persistence |
| `test_collectionutils` | 30 | CollectionConfig, hierarchy cache, validation helpers |
| `test_databasemanager` | 6 | Worker-thread shutdown, SQL connection cleanup, path resolution |
| `test_dbmigrations` | 10 | SQLite schema migration steps |
| `test_filterhelpers` | 15 | Filter predicate helpers |
| `test_gridlayoutcalculator` | 17 | Grid metrics, item positioning, row ranges |
| `test_gridutils` | 22 | Row/column math, centering, grid metrics calculation |
| `test_interactionstateholder` | 13 | State flags, suppression timers, struct access |
| `test_keyboardmanager` | 27 | Key repeat, arrow + alphabetic navigation handlers |
| `test_launchmanager` | 29 | Security validation, path checking, parameter parsing |
| `test_mousehelpers` | 17 | Mouse helper math (hold scroll, wheel) |
| `test_navigationhelpers` | 18 | Navigation helper utilities |
| `test_navigationstackmanager` | 19 | Navigation stack push/pop/clear |
| `test_pathutils` | 27 | Path validation, expansion, Result<T> error handling |
| `test_queryhelpers` | 21 | SQL helper builders |
| `test_querymanager_cancel_scan` | 1 | Scan cancellation flow |
| `test_scrollhelpers` | 16 | Scroll helper math |
| `test_searchhelpers` | 16 | Search helper utilities |
| `test_searchutils` | 3 | SearchMode utilities |
| `test_selectionhelpers` | 17 | Selection helper math |
| `test_sessionmanager` | 17 | Session persistence with atomic writes |
| `test_stringutils` | 10 | String formatting |
| `test_widgetpoolmanager` | 17 | Widget acquisition, release, soft/hard clear, stale limits |

**Total: 398 unit test methods across 24 test suites.**

## Adding New Tests

1. Create `tests/<area>/test_<classname>.cpp` mirroring the source location
   (e.g. a test for `src/modules/cache/cachemanager.cpp` lives at
   `tests/modules/cache/test_cachemanager.cpp`; a test for
   `src/utils/pathutils.cpp` lives at `tests/utils/test_pathutils.cpp`).
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
