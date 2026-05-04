# Integration test harness for UI-coordinator managers.
#
# Pure unit tests are not viable for managers that include ui_*.h headers
# (ApplicationManager, NavigationManager, ScrollManager, InteractionManager
# and friends) because they pull in the full MainWindow widget graph and
# cross-manager wiring. Instead, those tests run inside this single shared
# integration binary, which links the ${KARTEND_LIB_TARGET} OBJECT library
# directly so it sees every TU and AUTOMOC/AUTOUIC artifact already generated
# for the main executable.
#
# Add new test source files to INTEGRATION_TEST_SOURCES below. All tests share
# one binary because per-test executables would each pay the ~140-TU link cost
# of the full kartend_lib closure.

set(INTEGRATION_TEST_SOURCES
  test_main.cpp
  mainwindowfixture.cpp
  test_mainwindow_smoke.cpp
  test_settingsdialog_scope.cpp
  test_applysettingsdialog.cpp
)

set(INTEGRATION_TEST_HEADERS
  mainwindowfixture.h
  test_mainwindow_smoke.h
  test_settingsdialog_scope.h
  test_applysettingsdialog.h
)

add_executable(test_integration
  ${INTEGRATION_TEST_SOURCES}
  ${INTEGRATION_TEST_HEADERS}
)

# The OBJECT library propagates its PUBLIC include directories, compile
# definitions, and Qt link dependencies, so this binary needs no extra wiring
# to see the kartend source tree.
target_link_libraries(test_integration
  PRIVATE
    ${KARTEND_LIB_TARGET}
    Qt6::Test
)

# AUTOUIC generates ui_*.h headers next to the consuming target's autogen
# directory (e.g. kartend_lib_autogen/include/ui_itemwidget.h). Many MainWindow
# transitive includes pull in those headers (itemwidget.h, mainwindow.h, etc.),
# so the test binary needs visibility of the kartend_lib autogen include path.
target_include_directories(test_integration PRIVATE
  ${CMAKE_BINARY_DIR}/${KARTEND_LIB_TARGET}_autogen/include
)

# AUTOMOC/AUTOUIC are inherited from the top-level CMakeLists.txt
# (CMAKE_AUTOMOC/CMAKE_AUTOUIC are ON globally), so test_main.cpp can declare
# QObject-derived test classes without manual moc invocations. Search paths
# for AUTOUIC are also already configured at the top level.

add_test(NAME Integration COMMAND test_integration)
