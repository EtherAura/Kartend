# Integration test harness for UI-coordinator managers.
#
# Pure unit tests are not viable for managers that include ui_*.h headers
# (ApplicationManager, NavigationManager, ScrollManager, InteractionManager
# and friends) because they pull in the full MainWindow widget graph and
# cross-manager wiring. Instead, those tests run inside this single shared
# integration binary, which links every per-area kartend_<area> OBJECT lib
# directly so it sees every TU and AUTOMOC/AUTOUIC artifact already generated
# for the main executable.
#
# Add new test source files to INTEGRATION_TEST_SOURCES below. All tests share
# one binary because per-test executables would each pay the ~140-TU link cost
# of the full per-area closure declared in src/CMakeLists.txt.

set(INTEGRATION_TEST_SOURCES
  test_main.cpp
  mainwindowfixture.cpp
  mocks/mockedmainwindowfixture.cpp
  test_mainwindow_smoke.cpp
  test_menucontroller.cpp
  test_settingsdialog_scope.cpp
  test_applysettingsdialog.cpp
  test_applicationmanager_lifecycle.cpp
  test_navigationmanager.cpp
  test_interactionmanager.cpp
  test_eventmanager_detailspane.cpp
  test_detailspane_coverflow.cpp
  test_settingsdialog_changes.cpp
  test_settingsdialog_navigation.cpp
  test_settingsdialog_perf.cpp
  test_scrapedialog_perf.cpp
  test_scrollmanager.cpp
  test_selectionoverlaymanager.cpp
  test_virtualcontainermanager.cpp
  test_filtermanager.cpp
  test_searchmanager.cpp
  test_selectiondisplaymanager.cpp
  test_detailspanemanager.cpp
  test_attractmanager.cpp
  test_dbeventscontroller.cpp
  test_detailpagemanager.cpp
  test_kartmanager.cpp
  test_launchflow.cpp
  test_scanservice.cpp
)

set(INTEGRATION_TEST_HEADERS
  mainwindowfixture.h
  test_mainwindow_smoke.h
  test_menucontroller.h
  test_settingsdialog_scope.h
  test_applysettingsdialog.h
  test_applicationmanager_lifecycle.h
  test_navigationmanager.h
  test_interactionmanager.h
  test_eventmanager_detailspane.h
  test_detailspane_coverflow.h
  test_settingsdialog_changes.h
  test_settingsdialog_navigation.h
  test_settingsdialog_perf.h
  test_scrapedialog_perf.h
  test_scrollmanager.h
  test_selectionoverlaymanager.h
  test_virtualcontainermanager.h
  test_filtermanager.h
  test_searchmanager.h
  test_selectiondisplaymanager.h
  test_detailspanemanager.h
  test_attractmanager.h
  test_dbeventscontroller.h
  test_detailpagemanager.h
  test_kartmanager.h
  test_launchflow.h
  test_scanservice.h
  mocks/mockdatabasemanager.h
  mocks/mockedmainwindowfixture.h
  mocks/mocksettingsmanager.h
)

add_executable(test_integration
  ${INTEGRATION_TEST_SOURCES}
  ${INTEGRATION_TEST_HEADERS}
)

# Each per-area OBJECT lib publishes its own PUBLIC include directories,
# compile definitions, and Qt link dependencies through the inter-area DAG
# declared in src/CMakeLists.txt, so listing every area pulls in the full
# include + Qt surface. OBJECT libs don't propagate through
# INTERFACE_LINK_LIBRARIES, so the executable + this binary both list every
# area directly (matches kartend's link line in the top-level CMakeLists).
target_link_libraries(test_integration
  PRIVATE
    ${KARTEND_AREA_LIBS}
    Qt6::Test
)
target_link_options(test_integration PRIVATE ${KARTEND_LTO_LINK_OPTIONS})

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

# Register one ctest entry per test class (Kartend-40hxp). Every entry runs the
# same shared binary with a class-name selector (see kSuites in test_main.cpp)
# so `ctest -j` parallelizes across classes and a failure names the class
# instead of one opaque "Integration" result. Keep this list in sync with
# kSuites in test_main.cpp.
set(INTEGRATION_TEST_CLASSES
  TestMainWindowSmoke
  TestMenuController
  TestSettingsDialogScope
  TestApplySettingsDialog
  TestApplicationManagerLifecycle
  TestNavigationManager
  TestInteractionManager
  TestDbEventsController
  TestEventManagerDetailsPane
  TestDetailsPaneCoverflow
  TestSettingsDialogChanges
  TestSettingsDialogNavigation
  TestSettingsDialogPerf
  TestScrapeDialogPerf
  TestScrollManager
  TestSelectionOverlayManager
  TestVirtualContainerManager
  TestFilterManager
  TestSearchManager
  TestSelectionDisplayManager
  TestDetailsPaneManager
  TestAttractManager
  TestDetailPageManager
  TestKartManager
  TestLaunchFlow
  TestScanService
)
foreach(_integration_class IN LISTS INTEGRATION_TEST_CLASSES)
  add_test(NAME Integration.${_integration_class}
           COMMAND test_integration ${_integration_class})
endforeach()

# Kartend-kkeur: give the integration test its own HOME so QStandardPaths test
# mode (~/.qttest) is isolated from the unit tests under `ctest -j`. Mirrors
# the per-test HOME loop at the end of tests/CMakeLists.txt — that loop only
# sees tests registered in that directory, not this subdirectory's.
get_property(_kartend_integration_tests DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS _kartend_integration_tests)
  set(_test_home "${CMAKE_BINARY_DIR}/test-homes/${_test}")
  file(MAKE_DIRECTORY "${_test_home}")
  set_property(TEST ${_test} APPEND PROPERTY ENVIRONMENT "HOME=${_test_home}")
endforeach()
