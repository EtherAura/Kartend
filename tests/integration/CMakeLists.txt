# Integration test harness for UI-coordinator managers.
#
# Pure unit tests are not viable for managers that include ui_*.h headers
# (ApplicationManager, NavigationManager, ScrollManager, InteractionManager
# and friends) because they pull in the full MainWindow widget graph and
# cross-manager wiring. Instead, those tests run inside this single shared
# integration binary, which links the ${KARTEND_LIB_TARGET} INTERFACE
# aggregator (which transitively pulls every kartend_<area> OBJECT lib) so
# it sees every TU and AUTOMOC/AUTOUIC artifact already generated for the
# main executable.
#
# Add new test source files to INTEGRATION_TEST_SOURCES below. All tests share
# one binary because per-test executables would each pay the ~140-TU link cost
# of the full kartend_lib closure (which is now a thin aggregator wrapping
# nine per-area OBJECT libs declared in src/CMakeLists.txt).

set(INTEGRATION_TEST_SOURCES
  test_main.cpp
  mainwindowfixture.cpp
  mocks/mockedmainwindowfixture.cpp
  test_mainwindow_smoke.cpp
  test_settingsdialog_scope.cpp
  test_applysettingsdialog.cpp
  test_applicationmanager_lifecycle.cpp
  test_navigationmanager.cpp
  test_eventmanager_detailspane.cpp
  test_detailspane_coverflow.cpp
  test_settingsdialog_changes.cpp
  test_scrollmanager.cpp
)

set(INTEGRATION_TEST_HEADERS
  mainwindowfixture.h
  test_mainwindow_smoke.h
  test_settingsdialog_scope.h
  test_applysettingsdialog.h
  test_applicationmanager_lifecycle.h
  test_navigationmanager.h
  test_eventmanager_detailspane.h
  test_detailspane_coverflow.h
  test_settingsdialog_changes.h
  test_scrollmanager.h
  mocks/mockdatabasemanager.h
  mocks/mockedmainwindowfixture.h
  mocks/mocksettingsmanager.h
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
    # OBJECT libs don't propagate through INTERFACE_LINK_LIBRARIES on the
    # aggregator, so list every per-area lib directly to pull in their .o
    # files (matches the executable's link line in the top-level CMakeLists).
    ${KARTEND_AREA_LIBS}
    Qt6::Test
)

# AUTOUIC generates ui_*.h headers next to the consuming target's autogen
# directory. With the per-area OBJECT lib split, each area that owns a
# matching .ui file produces its own autogen include path: kartend_chrome
# for itemwidget.ui, kartend_core for mainwindow.ui, kartend_ui for every
# settings panel .ui under src/ui/dialogs/settings/, and detailspane.ui under
# src/ui/widgets/panes/. The integration test pulls headers (itemwidget.h,
# mainwindow.h, settingsdialog.h, detailspane.h) that #include those
# generated ui_*.h files, so all four autogen include dirs are needed.
target_include_directories(test_integration PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# AUTOMOC/AUTOUIC are inherited from the top-level CMakeLists.txt
# (CMAKE_AUTOMOC/CMAKE_AUTOUIC are ON globally), so test_main.cpp can declare
# QObject-derived test classes without manual moc invocations. Search paths
# for AUTOUIC are also already configured at the top level.

add_test(NAME Integration COMMAND test_integration)
