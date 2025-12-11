# Testing

Unit tests use the Qt Test framework with CTest integration.

## Building Tests

```bash
cd build/release
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)
```

## Running Tests

```bash
# Run all tests via CTest
cd build/release && ctest --output-on-failure

# Run individual test
./tests/test_gridlayoutcalculator
./tests/test_interactionstateholder
./tests/test_launchmanager
./tests/test_pathutils
./tests/test_widgetpoolmanager
./tests/test_gridutils
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
