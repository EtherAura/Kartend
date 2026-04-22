# Testing

Unit tests use the Qt Test framework with CTest integration.

## Building Tests

Fast path using the build script:

```bash
./.scripts/build.sh --tests --run-tests --fast
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
# ... one binary per suite under tests/ (23 suites total)
```

## Sanitizers (optional)

Sanitizers are useful for catching memory safety bugs (use-after-free, OOB,
UB) during development.

```bash
# Build Debug + ASan/UBSan
./.scripts/build.sh --sanitize --keep-builds --fast

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

**Total: 392 unit test methods across 23 test suites.**

## Adding New Tests

1. Create `tests/test_<classname>.cpp` with Qt Test structure:

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
