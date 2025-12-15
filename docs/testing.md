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
./tests/test_interactionstateholder
./tests/test_launchmanager
./tests/test_pathutils
./tests/test_widgetpoolmanager
./tests/test_gridutils
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
| `test_gridlayoutcalculator` | 19 | Grid metrics, item positioning, row ranges |
| `test_interactionstateholder` | 15 | State flags, suppression timers, struct access |
| `test_launchmanager` | 12 | Security validation, path checking, parameter parsing |
| `test_pathutils` | 10 | Path validation, expansion, Result<T> error handling |
| `test_widgetpoolmanager` | 20 | Widget acquisition, release, soft/hard clear |
| `test_gridutils` | 24 | Row/column math, centering, grid metrics calculation |

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
