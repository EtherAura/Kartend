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
  ${SRC_DIR}/utils/pathutils.cpp
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

# WidgetPoolManager tests
add_executable(test_widgetpoolmanager
  test_widgetpoolmanager.cpp
  ${SRC_DIR}/modules/widgetpool/widgetpoolmanager.cpp
  ${SRC_DIR}/ui/widgets/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetpaint.cpp
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

# GridUtils tests
add_executable(test_gridutils test_gridutils.cpp)
target_link_libraries(test_gridutils Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME GridUtils COMMAND test_gridutils)

# CacheManager tests
add_executable(test_cachemanager
  test_cachemanager.cpp
  ${SRC_DIR}/modules/cache/cachemanager.cpp
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
  ${SRC_DIR}/modules/session/sessionmanager.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
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

# ArtworkManager tests (cancellation, dedup, QPointer race)
add_executable(test_artworkmanager
  test_artworkmanager.cpp
  ${SRC_DIR}/modules/artwork/artworkmanager.cpp
  ${SRC_DIR}/modules/artwork/artworksilentloading.cpp
  ${SRC_DIR}/modules/cache/cachemanager.cpp
  ${SRC_DIR}/modules/interaction/interactionstateholder.cpp
  ${SRC_DIR}/ui/widgets/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetpaint.cpp
  ${SRC_DIR}/ui/widgets/itemwidgetsetters.cpp
  ${SRC_DIR}/utils/artworkutils.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/extensionutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/timerutils.cpp
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
