# Kartend Tests
cmake_minimum_required(VERSION 3.20)

# Find Qt6 Test module
find_package(Qt6 REQUIRED COMPONENTS Test Gui Widgets)

# Enable testing
enable_testing()

# Include directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
include_directories(
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/modules/data/query
  ${SRC_DIR}/modules/input/scroll
  ${SRC_DIR}/modules/input/interaction
  ${SRC_DIR}/modules/behavior/launch
  ${SRC_DIR}/modules/behavior/filter
  ${SRC_DIR}/modules/data/cache
  ${SRC_DIR}/modules/data/session
  ${SRC_DIR}/ui
)

# GridLayoutCalculator tests
add_executable(test_gridlayoutcalculator
  modules/scroll/test_gridlayoutcalculator.cpp
  ${SRC_DIR}/modules/input/scroll/gridlayoutcalculator.cpp
)
target_link_libraries(test_gridlayoutcalculator
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME GridLayoutCalculator COMMAND test_gridlayoutcalculator)

# InteractionStateHolder tests
add_executable(test_interactionstateholder
  modules/interaction/test_interactionstateholder.cpp
  ${SRC_DIR}/modules/input/interaction/interactionstateholder.cpp
)
target_link_libraries(test_interactionstateholder
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME InteractionStateHolder COMMAND test_interactionstateholder)

# LaunchManager tests (security validation)
add_executable(test_launchmanager
  modules/launch/test_launchmanager.cpp
  ${SRC_DIR}/modules/behavior/launch/launchmanager.cpp
  ${SRC_DIR}/modules/behavior/launch/launchmanagerarchive.cpp
  ${SRC_DIR}/ui/dialogs/launcherchooserdialog.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
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
  utils/test_pathutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_link_libraries(test_pathutils
  Qt6::Core
  Qt6::Test
)
add_test(NAME PathUtils COMMAND test_pathutils)

# VideoUtils tests
add_executable(test_videoutils
  utils/test_videoutils.cpp
  ${SRC_DIR}/utils/view/videoutils.cpp
)
target_link_libraries(test_videoutils
  Qt6::Core
  Qt6::Test
)
add_test(NAME VideoUtils COMMAND test_videoutils)

# WidgetPoolManager tests
add_executable(test_widgetpoolmanager
  modules/widgetpool/test_widgetpoolmanager.cpp
  ${SRC_DIR}/modules/behavior/widgetpool/widgetpoolmanager.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetindicator.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetlifecycle.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetpaint.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetplaceholder.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetsetters.cpp
)
target_include_directories(test_widgetpoolmanager PRIVATE
  ${SRC_DIR}/modules/behavior/widgetpool
  ${SRC_DIR}/ui/widgets/items
  ${SRC_DIR}/ui/widgets/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/ui/widgets/panes
)
target_link_libraries(test_widgetpoolmanager
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME WidgetPoolManager COMMAND test_widgetpoolmanager)

# EmptyStateWidget tests
add_executable(test_emptystatewidget
  ui/widgets/test_emptystatewidget.cpp
  ${SRC_DIR}/ui/widgets/items/emptystatewidget.cpp
)
target_include_directories(test_emptystatewidget PRIVATE
  ${SRC_DIR}/ui/widgets/items
  ${SRC_DIR}/ui/widgets/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/ui/widgets/panes
)
target_link_libraries(test_emptystatewidget
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME EmptyStateWidget COMMAND test_emptystatewidget)

# GridUtils tests
add_executable(test_gridutils utils/test_gridutils.cpp)
target_link_libraries(test_gridutils Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME GridUtils COMMAND test_gridutils)

# CacheManager tests
add_executable(test_cachemanager
  modules/cache/test_cachemanager.cpp
  ${SRC_DIR}/modules/data/cache/cachemanager.cpp
  ${SRC_DIR}/modules/data/cache/cachemanagerdisk.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/threading/threadpoolutils.cpp
)
target_include_directories(test_cachemanager PRIVATE
  ${SRC_DIR}/modules/data/cache
  ${SRC_DIR}/ui
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_cachemanager
  Qt6::Core
  Qt6::Widgets
  Qt6::Gui
  Qt6::Test
)
add_test(NAME CacheManager COMMAND test_cachemanager)

# PlaylistManager tests
add_executable(test_playlistmanager
  modules/playlist/test_playlistmanager.cpp
  ${SRC_DIR}/modules/data/playlist/playlistmanager.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
)
target_include_directories(test_playlistmanager PRIVATE
  ${SRC_DIR}/modules/data/playlist
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_playlistmanager
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME PlaylistManager COMMAND test_playlistmanager)

# SessionManager tests
add_executable(test_sessionmanager
  modules/session/test_sessionmanager.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_sessionmanager PRIVATE
  ${SRC_DIR}/modules/data/session
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
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
  modules/query/test_querymanager_cancel_scan.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerpersist.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscanandsave.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_querymanager_cancel_scan PRIVATE
  ${SRC_DIR}/modules/data/query
  ${SRC_DIR}/modules/data/session
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
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
  modules/database/test_databasemanager.cpp
  ${SRC_DIR}/modules/data/database/databasemanager.cpp
  ${SRC_DIR}/modules/data/database/databasemanagerpaths.cpp
  ${SRC_DIR}/modules/data/database/databasemanagerworker.cpp
  ${SRC_DIR}/utils/text/titlefilter.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerpersist.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscanandsave.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/db/historystore.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
  ${SRC_DIR}/utils/db/usagestatsstore.cpp
)
target_include_directories(test_databasemanager PRIVATE
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/modules/data/query
  ${SRC_DIR}/modules/data/session
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
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
  modules/artwork/test_artworkmanager.cpp
  ${SRC_DIR}/modules/media/artwork/artworkmanager.cpp
  ${SRC_DIR}/modules/media/artwork/artworkmanagerbatch.cpp
  ${SRC_DIR}/modules/media/artwork/artworkmanagerprecache.cpp
  ${SRC_DIR}/modules/media/artwork/artworkmanagerpaths.cpp
  ${SRC_DIR}/modules/media/artwork/artworkmanagerpending.cpp
  ${SRC_DIR}/modules/media/artwork/artworksilentloading.cpp
  ${SRC_DIR}/modules/data/cache/cachemanager.cpp
  ${SRC_DIR}/modules/data/cache/cachemanagerdisk.cpp
  ${SRC_DIR}/modules/input/interaction/interactionstateholder.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetindicator.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetlifecycle.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetpaint.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetplaceholder.cpp
  ${SRC_DIR}/ui/widgets/items/itemwidgetsetters.cpp
  ${SRC_DIR}/utils/view/artworkutils.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/threading/threadpoolutils.cpp
  ${SRC_DIR}/utils/threading/timerutils.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_artworkmanager PRIVATE
  ${SRC_DIR}
  ${SRC_DIR}/modules/media/artwork
  ${SRC_DIR}/modules/data/cache
  ${SRC_DIR}/modules/input/interaction
  ${SRC_DIR}/ui/widgets/items
  ${SRC_DIR}/ui/widgets/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/ui/widgets/panes
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_artworkmanager
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Concurrent
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ArtworkManager COMMAND test_artworkmanager)

# StringUtils tests (header-only)
add_executable(test_stringutils utils/test_stringutils.cpp)
target_link_libraries(test_stringutils Qt6::Core Qt6::Test)
add_test(NAME StringUtils COMMAND test_stringutils)

# CliArgs tests (--collection startup override)
add_executable(test_cliargs
  utils/test_cliargs.cpp
  ${SRC_DIR}/utils/app/cliargs.cpp
)
target_include_directories(test_cliargs PRIVATE ${SRC_DIR}/utils)
target_link_libraries(test_cliargs Qt6::Core Qt6::Test)
add_test(NAME CliArgs COMMAND test_cliargs)

# TitleFilter tests (per-collection regex title cleanup)
add_executable(test_titlefilter
  utils/test_titlefilter.cpp
  ${SRC_DIR}/utils/text/titlefilter.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_titlefilter PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_titlefilter Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME TitleFilter COMMAND test_titlefilter)

# CollectionUtils tests (enum conversions, validation, UUID)
add_executable(test_collectionutils
  utils/test_collectionutils.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_collectionutils PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_collectionutils Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtils COMMAND test_collectionutils)

# KeyboardManager tests (static selection helpers)
# Only the selection-helper TU is linked; it has no dependencies on
# ScrollManager / InteractionStateHolder / etc., keeping the test isolated.
add_executable(test_keyboardmanager
  modules/keyboard/test_keyboardmanager.cpp
  ${SRC_DIR}/modules/input/keyboard/keyboardmanagerselection.cpp
)
target_include_directories(test_keyboardmanager PRIVATE
  ${SRC_DIR}/modules/input/keyboard
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
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
  modules/navigation/test_navigationstackmanager.cpp
  ${SRC_DIR}/modules/input/navigation/navigationstackmanager.h
)
target_include_directories(test_navigationstackmanager PRIVATE
  ${SRC_DIR}/modules/input/navigation
)
target_link_libraries(test_navigationstackmanager
  Qt6::Core
  Qt6::Test
)
add_test(NAME NavigationStackManager COMMAND test_navigationstackmanager)

# DbMigrations tests (sqlite in-memory, no UI)
add_executable(test_dbmigrations
  utils/test_dbmigrations.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
)
target_include_directories(test_dbmigrations PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_dbmigrations
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME DbMigrations COMMAND test_dbmigrations)

# ItemMetadata storage round-trip tests
add_executable(test_itemmetadata
  utils/test_itemmetadata.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
)
target_include_directories(test_itemmetadata PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_itemmetadata
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ItemMetadata COMMAND test_itemmetadata)

# ItemArtwork storage + standard-type resolution tests
add_executable(test_itemartwork
  utils/test_itemartwork.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
)
target_include_directories(test_itemartwork PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_itemartwork
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ItemArtwork COMMAND test_itemartwork)

# UsageStatsStore tests
add_executable(test_usagestatsstore
  utils/test_usagestatsstore.cpp
  ${SRC_DIR}/utils/db/usagestatsstore.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
)
target_include_directories(test_usagestatsstore PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_usagestatsstore
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME UsageStatsStore COMMAND test_usagestatsstore)

# HistoryStore tests
add_executable(test_historystore
  utils/test_historystore.cpp
  ${SRC_DIR}/utils/db/historystore.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
)
target_include_directories(test_historystore PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_historystore
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME HistoryStore COMMAND test_historystore)

# KartManifest tests (JSON round-trip for the .kart packaging format)
add_executable(test_kartmanifest
  modules/kart/test_kartmanifest.cpp
  ${SRC_DIR}/modules/data/kart/kartmanifest.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
)
target_include_directories(test_kartmanifest PRIVATE
  ${SRC_DIR}/modules/data/kart
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_kartmanifest
  Qt6::Core
  Qt6::Sql
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME KartManifest COMMAND test_kartmanifest)

# KartReader tests (read + extract + sha256 verification)
add_executable(test_kartreader
  modules/kart/test_kartreader.cpp
  ${SRC_DIR}/modules/data/kart/kartcompression.cpp
  ${SRC_DIR}/modules/data/kart/kartmanifest.cpp
  ${SRC_DIR}/modules/data/kart/kartreader.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
)
target_include_directories(test_kartreader PRIVATE
  ${SRC_DIR}/modules/data/kart
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_kartreader
  Qt6::Core
  Qt6::Sql
  Qt6::Widgets
  Qt6::Test
)
if (TARGET PkgConfig::ZSTD)
  target_link_libraries(test_kartreader PkgConfig::ZSTD)
  target_compile_definitions(test_kartreader PRIVATE KARTEND_HAS_ZSTD=1)
endif()
add_test(NAME KartReader COMMAND test_kartreader)

# KartWriter tests (export collection + zstd compression)
add_executable(test_kartwriter
  modules/kart/test_kartwriter.cpp
  ${SRC_DIR}/modules/data/kart/kartcompression.cpp
  ${SRC_DIR}/modules/data/kart/kartmanifest.cpp
  ${SRC_DIR}/modules/data/kart/kartreader.cpp
  ${SRC_DIR}/modules/data/kart/kartwriter.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
)
target_include_directories(test_kartwriter PRIVATE
  ${SRC_DIR}/modules/data/kart
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_kartwriter
  Qt6::Core
  Qt6::Sql
  Qt6::Widgets
  Qt6::Test
)
if (TARGET PkgConfig::ZSTD)
  target_link_libraries(test_kartwriter PkgConfig::ZSTD)
  target_compile_definitions(test_kartwriter PRIVATE KARTEND_HAS_ZSTD=1)
endif()
add_test(NAME KartWriter COMMAND test_kartwriter)

# KartMerge tests (merge policy + metadata persistence)
add_executable(test_kartmerge
  modules/kart/test_kartmerge.cpp
  ${SRC_DIR}/modules/data/kart/kartmanifest.cpp
  ${SRC_DIR}/modules/data/kart/kartmerge.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
)
target_include_directories(test_kartmerge PRIVATE
  ${SRC_DIR}/modules/data/kart
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_kartmerge
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Sql
  Qt6::Test
)
add_test(NAME KartMerge COMMAND test_kartmerge)

# SearchUtils smoke tests (header-only enum/struct)
add_executable(test_searchutils utils/test_searchutils.cpp)
target_include_directories(test_searchutils PRIVATE ${SRC_DIR}/utils)
target_link_libraries(test_searchutils Qt6::Core Qt6::Test)
add_test(NAME SearchUtils COMMAND test_searchutils)

# NavigationHelpers tests (pure helpers extracted)
add_executable(test_navigationhelpers
  modules/navigation/test_navigationhelpers.cpp
  ${SRC_DIR}/modules/input/navigation/navigationhelpers.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_navigationhelpers PRIVATE
  ${SRC_DIR}/modules/input/navigation
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_navigationhelpers
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME NavigationHelpers COMMAND test_navigationhelpers)

# SearchHelpers tests (pure helpers extracted)
add_executable(test_searchhelpers
  modules/search/test_searchhelpers.cpp
  ${SRC_DIR}/modules/behavior/search/searchhelpers.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_searchhelpers PRIVATE
  ${SRC_DIR}/modules/behavior/search
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_searchhelpers
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME SearchHelpers COMMAND test_searchhelpers)

# MouseHelpers tests (pure helpers extracted)
add_executable(test_mousehelpers
  modules/mouse/test_mousehelpers.cpp
  ${SRC_DIR}/modules/input/mouse/mousehelpers.cpp
)
target_include_directories(test_mousehelpers PRIVATE
  ${SRC_DIR}/modules/input/mouse
)
target_link_libraries(test_mousehelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME MouseHelpers COMMAND test_mousehelpers)

# SelectionHelpers tests (pure helpers extracted)
add_executable(test_selectionhelpers
  modules/selection/test_selectionhelpers.cpp
  ${SRC_DIR}/modules/input/selection/selectionhelpers.cpp
)
target_include_directories(test_selectionhelpers PRIVATE
  ${SRC_DIR}/modules/input/selection
)
target_link_libraries(test_selectionhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME SelectionHelpers COMMAND test_selectionhelpers)

# ScrollHelpers tests (pure helpers extracted)
add_executable(test_scrollhelpers
  modules/scroll/test_scrollhelpers.cpp
  ${SRC_DIR}/modules/input/scroll/scrollhelpers.cpp
)
target_include_directories(test_scrollhelpers PRIVATE
  ${SRC_DIR}/modules/input/scroll
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_scrollhelpers
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME ScrollHelpers COMMAND test_scrollhelpers)

# QueryHelpers tests (pure helpers extracted)
add_executable(test_queryhelpers
  modules/query/test_queryhelpers.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
)
target_include_directories(test_queryhelpers PRIVATE
  ${SRC_DIR}/modules/data/query
)
target_link_libraries(test_queryhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME QueryHelpers COMMAND test_queryhelpers)

# FilterHelpers tests (pure helpers extracted)
add_executable(test_filterhelpers
  modules/filter/test_filterhelpers.cpp
  ${SRC_DIR}/modules/behavior/filter/filterhelpers.cpp
)
target_include_directories(test_filterhelpers PRIVATE
  ${SRC_DIR}/modules/behavior/filter
)
target_link_libraries(test_filterhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME FilterHelpers COMMAND test_filterhelpers)

# Filter hot-path benchmarks. Labelled "benchmark" so default ctest runs skip
# them — invoke with `ctest -L benchmark` to track regression. Walltime
# numbers vary by hardware; compare against a stored baseline.
add_executable(bench_filterhelpers
  modules/filter/bench_filterhelpers.cpp
  ${SRC_DIR}/modules/behavior/filter/filterhelpers.cpp
)
target_include_directories(bench_filterhelpers PRIVATE
  ${SRC_DIR}/modules/behavior/filter
)
target_link_libraries(bench_filterhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME BenchFilterHelpers COMMAND bench_filterhelpers)
set_tests_properties(BenchFilterHelpers PROPERTIES LABELS "benchmark")

# Integration test harness for UI-coordinator managers.
# Only built when the kartend_lib OBJECT library is available (i.e. when
# tests/CMakeLists.txt is added from the top-level CMakeLists.txt rather than
# configured stand-alone).
if (TARGET ${KARTEND_LIB_TARGET})
  add_subdirectory(integration)
endif()
