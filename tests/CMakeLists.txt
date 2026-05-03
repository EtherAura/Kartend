# Kartend Tests
cmake_minimum_required(VERSION 3.20)

# Find Qt6 Test module
find_package(Qt6 REQUIRED COMPONENTS Test Gui Widgets)

# Enable testing
enable_testing()

# Include directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
include_directories(
  ${SRC_DIR}/utils
  ${SRC_DIR}/modules/query
  ${SRC_DIR}/modules/scroll
  ${SRC_DIR}/modules/interaction
  ${SRC_DIR}/modules/launch
  ${SRC_DIR}/modules/filter
  ${SRC_DIR}/modules/cache
  ${SRC_DIR}/modules/session
  ${SRC_DIR}/ui
)

# GridLayoutCalculator tests
add_executable(test_gridlayoutcalculator
  test_gridlayoutcalculator.cpp
  ${SRC_DIR}/modules/scroll/gridlayoutcalculator.cpp
)
target_link_libraries(test_gridlayoutcalculator
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME GridLayoutCalculator COMMAND test_gridlayoutcalculator)

# InteractionStateHolder tests
add_executable(test_interactionstateholder
  test_interactionstateholder.cpp
  ${SRC_DIR}/modules/interaction/interactionstateholder.cpp
)
target_link_libraries(test_interactionstateholder
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME InteractionStateHolder COMMAND test_interactionstateholder)

# LaunchManager tests (security validation)
add_executable(test_launchmanager
  test_launchmanager.cpp
  ${SRC_DIR}/modules/launch/launchmanager.cpp
  ${SRC_DIR}/modules/launch/launchmanagerarchive.cpp
  ${SRC_DIR}/ui/dialogs/launcherchooserdialog.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
)
target_include_directories(test_launchmanager PRIVATE
  ${SRC_DIR}/ui/dialogs
)
target_link_libraries(test_launchmanager
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME LaunchManager COMMAND test_launchmanager)

# PathUtils tests
add_executable(test_pathutils
  test_pathutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
)
target_link_libraries(test_pathutils
  Qt6::Core
  Qt6::Test
)
add_test(NAME PathUtils COMMAND test_pathutils)

# VideoUtils tests
add_executable(test_videoutils
  test_videoutils.cpp
  ${SRC_DIR}/utils/videoutils.cpp
)
target_link_libraries(test_videoutils
  Qt6::Core
  Qt6::Test
)
add_test(NAME VideoUtils COMMAND test_videoutils)

# WidgetPoolManager tests
add_executable(test_widgetpoolmanager
  test_widgetpoolmanager.cpp
  ${SRC_DIR}/modules/widgetpool/widgetpoolmanager.cpp
  ${SRC_DIR}/ui/widgets/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetindicator.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetlifecycle.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetpaint.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetplaceholder.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetsetters.cpp
)
target_include_directories(test_widgetpoolmanager PRIVATE
  ${SRC_DIR}/modules/widgetpool
  ${SRC_DIR}/ui/widgets
)
target_link_libraries(test_widgetpoolmanager
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME WidgetPoolManager COMMAND test_widgetpoolmanager)

# EmptyStateWidget tests
add_executable(test_emptystatewidget
  test_emptystatewidget.cpp
  ${SRC_DIR}/ui/widgets/emptystatewidget.cpp
)
target_include_directories(test_emptystatewidget PRIVATE
  ${SRC_DIR}/ui/widgets
)
target_link_libraries(test_emptystatewidget
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME EmptyStateWidget COMMAND test_emptystatewidget)

# GridUtils tests
add_executable(test_gridutils test_gridutils.cpp)
target_link_libraries(test_gridutils Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME GridUtils COMMAND test_gridutils)

# CacheManager tests
add_executable(test_cachemanager
  test_cachemanager.cpp
  ${SRC_DIR}/modules/cache/cachemanager.cpp
  ${SRC_DIR}/modules/cache/cachemanagerdisk.cpp
)
target_include_directories(test_cachemanager PRIVATE
  ${SRC_DIR}/modules/cache
  ${SRC_DIR}/ui
)
target_link_libraries(test_cachemanager
  Qt6::Core
  Qt6::Widgets
  Qt6::Gui
  Qt6::Test
)
add_test(NAME CacheManager COMMAND test_cachemanager)

# SessionManager tests
add_executable(test_sessionmanager
  test_sessionmanager.cpp
  ${SRC_DIR}/modules/session/sessionmanager.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
)
target_include_directories(test_sessionmanager PRIVATE
  ${SRC_DIR}/modules/session
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_sessionmanager
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME SessionManager COMMAND test_sessionmanager)

# QueryManager cancellation semantics test
add_executable(test_querymanager_cancel_scan
  test_querymanager_cancel_scan.cpp
  ${SRC_DIR}/modules/query/querymanager.cpp
  ${SRC_DIR}/modules/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/modules/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/query/querymanagerpersist.cpp
  ${SRC_DIR}/modules/query/querymanagerscanandsave.cpp
  ${SRC_DIR}/modules/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/query/querymanagercache.cpp
  ${SRC_DIR}/modules/query/querymanagerload.cpp
  ${SRC_DIR}/modules/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/query/queryhelpers.cpp
  ${SRC_DIR}/modules/session/sessionmanager.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
  ${SRC_DIR}/utils/loggingcategories.cpp
)
target_include_directories(test_querymanager_cancel_scan PRIVATE
  ${SRC_DIR}/modules/query
  ${SRC_DIR}/modules/session
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_querymanager_cancel_scan
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME QueryManagerCancelScan COMMAND test_querymanager_cancel_scan)

# DatabaseManager lifecycle / threading tests
# Reuses the same query-stack source list as QueryManagerCancelScan and adds
# the databasemanager TUs. Validates worker-thread shutdown, SQL connection
# cleanup, and thread-safe path-resolution helpers.
add_executable(test_databasemanager
  test_databasemanager.cpp
  ${SRC_DIR}/modules/database/databasemanager.cpp
  ${SRC_DIR}/modules/database/databasemanagerpaths.cpp
  ${SRC_DIR}/modules/database/databasemanagerworker.cpp
  ${SRC_DIR}/modules/query/querymanager.cpp
  ${SRC_DIR}/modules/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/modules/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/query/querymanagerpersist.cpp
  ${SRC_DIR}/modules/query/querymanagerscanandsave.cpp
  ${SRC_DIR}/modules/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/query/querymanagercache.cpp
  ${SRC_DIR}/modules/query/querymanagerload.cpp
  ${SRC_DIR}/modules/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/query/queryhelpers.cpp
  ${SRC_DIR}/modules/session/sessionmanager.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
  ${SRC_DIR}/utils/extensionutils.cpp
  ${SRC_DIR}/utils/historystore.cpp
  ${SRC_DIR}/utils/itemartwork.cpp
  ${SRC_DIR}/utils/itemmetadata.cpp
  ${SRC_DIR}/utils/loggingcategories.cpp
  ${SRC_DIR}/utils/usagestatsstore.cpp
)
target_include_directories(test_databasemanager PRIVATE
  ${SRC_DIR}/modules/database
  ${SRC_DIR}/modules/query
  ${SRC_DIR}/modules/session
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_databasemanager
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME DatabaseManager COMMAND test_databasemanager)

# ArtworkManager tests (cancellation, dedup, QPointer race)
add_executable(test_artworkmanager
  test_artworkmanager.cpp
  ${SRC_DIR}/modules/artwork/artworkmanager.cpp
  ${SRC_DIR}/modules/artwork/artworkmanagerbatch.cpp
  ${SRC_DIR}/modules/artwork/artworkmanagerprecache.cpp
  ${SRC_DIR}/modules/artwork/artworkmanagerpaths.cpp
  ${SRC_DIR}/modules/artwork/artworkmanagerpending.cpp
  ${SRC_DIR}/modules/artwork/artworksilentloading.cpp
  ${SRC_DIR}/modules/cache/cachemanager.cpp
  ${SRC_DIR}/modules/cache/cachemanagerdisk.cpp
  ${SRC_DIR}/modules/interaction/interactionstateholder.cpp
  ${SRC_DIR}/ui/widgets/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetindicator.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetlifecycle.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetpaint.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetplaceholder.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetsetters.cpp
  ${SRC_DIR}/utils/artworkutils.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/extensionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/timerutils.cpp
  ${SRC_DIR}/utils/loggingcategories.cpp
)
target_include_directories(test_artworkmanager PRIVATE
  ${SRC_DIR}
  ${SRC_DIR}/modules/artwork
  ${SRC_DIR}/modules/cache
  ${SRC_DIR}/modules/interaction
  ${SRC_DIR}/ui/widgets
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_artworkmanager
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Concurrent
  Qt6::Test
)
add_test(NAME ArtworkManager COMMAND test_artworkmanager)

# StringUtils tests (header-only)
add_executable(test_stringutils test_stringutils.cpp)
target_link_libraries(test_stringutils Qt6::Core Qt6::Test)
add_test(NAME StringUtils COMMAND test_stringutils)

# CollectionUtils tests (enum conversions, validation, UUID)
add_executable(test_collectionutils
  test_collectionutils.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
)
target_include_directories(test_collectionutils PRIVATE
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_collectionutils Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtils COMMAND test_collectionutils)

# KeyboardManager tests (static selection helpers)
# Only the selection-helper TU is linked; it has no dependencies on
# ScrollManager / InteractionStateHolder / etc., keeping the test isolated.
add_executable(test_keyboardmanager
  test_keyboardmanager.cpp
  ${SRC_DIR}/modules/keyboard/keyboardmanagerselection.cpp
)
target_include_directories(test_keyboardmanager PRIVATE
  ${SRC_DIR}/modules/keyboard
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_keyboardmanager
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME KeyboardManager COMMAND test_keyboardmanager)

# NavigationStackManager tests (header-only data class powering NavigationManager)
add_executable(test_navigationstackmanager
  test_navigationstackmanager.cpp
  ${SRC_DIR}/modules/navigation/navigationstackmanager.h
)
target_include_directories(test_navigationstackmanager PRIVATE
  ${SRC_DIR}/modules/navigation
)
target_link_libraries(test_navigationstackmanager
  Qt6::Core
  Qt6::Test
)
add_test(NAME NavigationStackManager COMMAND test_navigationstackmanager)

# DbMigrations tests (sqlite in-memory, no UI)
add_executable(test_dbmigrations
  test_dbmigrations.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
)
target_include_directories(test_dbmigrations PRIVATE
  ${SRC_DIR}/utils
)
target_link_libraries(test_dbmigrations
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME DbMigrations COMMAND test_dbmigrations)

# ItemMetadata storage round-trip tests
add_executable(test_itemmetadata
  test_itemmetadata.cpp
  ${SRC_DIR}/utils/itemmetadata.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
)
target_include_directories(test_itemmetadata PRIVATE
  ${SRC_DIR}/utils
)
target_link_libraries(test_itemmetadata
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ItemMetadata COMMAND test_itemmetadata)

# ItemArtwork storage + standard-type resolution tests
add_executable(test_itemartwork
  test_itemartwork.cpp
  ${SRC_DIR}/utils/itemartwork.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
  ${SRC_DIR}/utils/extensionutils.cpp
)
target_include_directories(test_itemartwork PRIVATE
  ${SRC_DIR}/utils
)
target_link_libraries(test_itemartwork
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ItemArtwork COMMAND test_itemartwork)

# UsageStatsStore tests (Kartend-7vi)
add_executable(test_usagestatsstore
  test_usagestatsstore.cpp
  ${SRC_DIR}/utils/usagestatsstore.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
)
target_include_directories(test_usagestatsstore PRIVATE
  ${SRC_DIR}/utils
)
target_link_libraries(test_usagestatsstore
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME UsageStatsStore COMMAND test_usagestatsstore)

# HistoryStore tests (Kartend-fse)
add_executable(test_historystore
  test_historystore.cpp
  ${SRC_DIR}/utils/historystore.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
)
target_include_directories(test_historystore PRIVATE
  ${SRC_DIR}/utils
)
target_link_libraries(test_historystore
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME HistoryStore COMMAND test_historystore)

# SearchUtils smoke tests (header-only enum/struct)
add_executable(test_searchutils test_searchutils.cpp)
target_include_directories(test_searchutils PRIVATE ${SRC_DIR}/utils)
target_link_libraries(test_searchutils Qt6::Core Qt6::Test)
add_test(NAME SearchUtils COMMAND test_searchutils)

# NavigationHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_navigationhelpers
  test_navigationhelpers.cpp
  ${SRC_DIR}/modules/navigation/navigationhelpers.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
)
target_include_directories(test_navigationhelpers PRIVATE
  ${SRC_DIR}/modules/navigation
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_navigationhelpers
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME NavigationHelpers COMMAND test_navigationhelpers)

# SearchHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_searchhelpers
  test_searchhelpers.cpp
  ${SRC_DIR}/modules/search/searchhelpers.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
)
target_include_directories(test_searchhelpers PRIVATE
  ${SRC_DIR}/modules/search
  ${SRC_DIR}/utils
  ${SRC_DIR}/ui
)
target_link_libraries(test_searchhelpers
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME SearchHelpers COMMAND test_searchhelpers)

# MouseHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_mousehelpers
  test_mousehelpers.cpp
  ${SRC_DIR}/modules/mouse/mousehelpers.cpp
)
target_include_directories(test_mousehelpers PRIVATE
  ${SRC_DIR}/modules/mouse
)
target_link_libraries(test_mousehelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME MouseHelpers COMMAND test_mousehelpers)

# SelectionHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_selectionhelpers
  test_selectionhelpers.cpp
  ${SRC_DIR}/modules/selection/selectionhelpers.cpp
)
target_include_directories(test_selectionhelpers PRIVATE
  ${SRC_DIR}/modules/selection
)
target_link_libraries(test_selectionhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME SelectionHelpers COMMAND test_selectionhelpers)

# ScrollHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_scrollhelpers
  test_scrollhelpers.cpp
  ${SRC_DIR}/modules/scroll/scrollhelpers.cpp
)
target_include_directories(test_scrollhelpers PRIVATE
  ${SRC_DIR}/modules/scroll
  ${SRC_DIR}/utils
)
target_link_libraries(test_scrollhelpers
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME ScrollHelpers COMMAND test_scrollhelpers)

# QueryHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_queryhelpers
  test_queryhelpers.cpp
  ${SRC_DIR}/modules/query/queryhelpers.cpp
)
target_include_directories(test_queryhelpers PRIVATE
  ${SRC_DIR}/modules/query
)
target_link_libraries(test_queryhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME QueryHelpers COMMAND test_queryhelpers)

# FilterHelpers tests (pure helpers extracted under Kartend-tty)
add_executable(test_filterhelpers
  test_filterhelpers.cpp
  ${SRC_DIR}/modules/filter/filterhelpers.cpp
)
target_include_directories(test_filterhelpers PRIVATE
  ${SRC_DIR}/modules/filter
)
target_link_libraries(test_filterhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME FilterHelpers COMMAND test_filterhelpers)

# Integration test harness for UI-coordinator managers (Kartend-785).
# Only built when the kartend_lib OBJECT library is available (i.e. when
# tests/CMakeLists.txt is added from the top-level CMakeLists.txt rather than
# configured stand-alone).
if (TARGET ${KARTEND_LIB_TARGET})
  add_subdirectory(integration)
endif()
