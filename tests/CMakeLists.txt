# Kartend Tests
cmake_minimum_required(VERSION 3.20)

# Find Qt6 Test module
find_package(Qt6 REQUIRED COMPONENTS Test Gui Widgets)

# Enable testing
enable_testing()

# Include directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
include_directories(
  ${SRC_DIR}/api
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/modules/data/playlist
  ${SRC_DIR}/modules/data/query
  ${SRC_DIR}/modules/input/scroll
  ${SRC_DIR}/modules/input/interaction
  ${SRC_DIR}/modules/input/launch
  ${SRC_DIR}/modules/input/filter
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

# ScrollDataStore tests (synthetic Home view tile population — Kartend-83iu)
add_executable(test_scrolldatamanager
  modules/scroll/test_scrolldatamanager.cpp
  ${SRC_DIR}/modules/input/scroll/scrolldatamanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/view/artworkutils.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_scrolldatamanager PRIVATE
  ${SRC_DIR}/modules/input/scroll
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_scrolldatamanager
  Qt6::Core
  Qt6::Concurrent
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME ScrollDataStore COMMAND test_scrolldatamanager)

# ViewportHelpers tests (small-movement threshold, force-immediate combine)
add_executable(test_viewporthelpers
  modules/viewport/test_viewporthelpers.cpp
  ${SRC_DIR}/modules/input/viewport/viewporthelpers.cpp
)
target_include_directories(test_viewporthelpers PRIVATE
  ${SRC_DIR}/modules/input/viewport
)
target_link_libraries(test_viewporthelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME ViewportHelpers COMMAND test_viewporthelpers)

# AnimationManager static helper tests (pure functions: duration calc,
# target Y/X computation, visibility scroll target).
add_executable(test_animationmanager
  modules/animation/test_animationmanager.cpp
  ${SRC_DIR}/modules/input/animation/animationhelpers.cpp
)
target_include_directories(test_animationmanager PRIVATE
  ${SRC_DIR}/modules/input/animation
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_animationmanager
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME AnimationManager COMMAND test_animationmanager)

# InteractionStateHolder tests
add_executable(test_interactionstateholder
  modules/interaction/test_interactionstateholder.cpp
  ${SRC_DIR}/utils/app/interactionstateholder.cpp
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
  ${SRC_DIR}/modules/input/launch/launchmanager.cpp
  ${SRC_DIR}/modules/input/launch/launchmanagerarchive.cpp
  ${SRC_DIR}/ui/dialogs/launcher/launcherchooserdialog.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
)
target_include_directories(test_launchmanager PRIVATE
  ${SRC_DIR}/ui/dialogs
  ${SRC_DIR}/ui/dialogs/settings
  ${SRC_DIR}/ui/dialogs/collection
  ${SRC_DIR}/ui/dialogs/launcher
  ${SRC_DIR}/ui/dialogs/scraper
  ${SRC_DIR}/ui/dialogs/kart
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

# ExtensionUtils tests (image-decode allowlist guard — keeps scraped .pdf
# manuals out of QImageReader / Qt's abort()-prone PDF image plugin)
add_executable(test_extensionutils
  utils/test_extensionutils.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
)
target_link_libraries(test_extensionutils
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME ExtensionUtils COMMAND test_extensionutils)

# SearchQueryParser tests (Kartend-9qv2). Pure string -> struct, no DB.
add_executable(test_searchqueryparser
  utils/test_searchqueryparser.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
)
target_link_libraries(test_searchqueryparser
  Qt6::Core
  Qt6::Test
)
add_test(NAME SearchQueryParser COMMAND test_searchqueryparser)

# CollectionFilesystemWatcher tests (Kartend-kr1g). Pure-helper subdir
# enumeration plus the configure() reconcile path. Uses QTemporaryDir for
# scratch trees so no production paths are touched.
add_executable(test_collectionfilesystemwatcher
  modules/watcher/test_collectionfilesystemwatcher.cpp
  ${SRC_DIR}/modules/data/watcher/collectionfilesystemwatcher.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_collectionfilesystemwatcher PRIVATE
  ${SRC_DIR}/modules/data/watcher
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/app/collection
  ${SRC_DIR}/utils/fs
)
target_link_libraries(test_collectionfilesystemwatcher
  Qt6::Core
  Qt6::Test
  Qt6::Widgets
)
add_test(NAME CollectionFilesystemWatcher COMMAND test_collectionfilesystemwatcher)

# ThemePreset tests (Kartend-y3rg). JSON round-trip + file I/O + apply.
add_executable(test_themepreset
  utils/test_themepreset.cpp
  ${SRC_DIR}/utils/app/collection/themepreset.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_themepreset PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/app/collection
  ${SRC_DIR}/utils/fs
)
target_link_libraries(test_themepreset
  Qt6::Core
  Qt6::Test
)
add_test(NAME ThemePreset COMMAND test_themepreset)

# PresentationProfile tests (Kartend-6pp5). JSON round-trip + apply + registry.
add_executable(test_presentationprofile
  utils/test_presentationprofile.cpp
  ${SRC_DIR}/utils/app/collection/presentationprofile.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_presentationprofile PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/app/collection
  ${SRC_DIR}/utils/fs
)
target_link_libraries(test_presentationprofile
  Qt6::Core
  Qt6::Test
)
add_test(NAME PresentationProfile COMMAND test_presentationprofile)

# CollectionHealth tests (Kartend-lprv). Pure analyzer over fixture rows.
add_executable(test_collectionhealth
  utils/test_collectionhealth.cpp
  ${SRC_DIR}/utils/db/collectionhealth.cpp
)
target_include_directories(test_collectionhealth PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/app/collection
  ${SRC_DIR}/utils/db
)
target_link_libraries(test_collectionhealth
  Qt6::Core
  Qt6::Test
)
add_test(NAME CollectionHealth COMMAND test_collectionhealth)

# MetadataQueue tests (Kartend-gtly). Pure analyzer over a metadata loader closure.
add_executable(test_metadataqueue
  utils/test_metadataqueue.cpp
  ${SRC_DIR}/utils/db/metadataqueue.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
)
target_link_libraries(test_metadataqueue
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME MetadataQueue COMMAND test_metadataqueue)

# ArtworkCandidates tests (Kartend-y02i). Pure ranker, no filesystem.
add_executable(test_artworkcandidates
  utils/test_artworkcandidates.cpp
  ${SRC_DIR}/utils/view/artworkcandidates.cpp
  ${SRC_DIR}/utils/text/fuzzymatch.cpp
)
target_link_libraries(test_artworkcandidates
  Qt6::Core
  Qt6::Test
)
add_test(NAME ArtworkCandidates COMMAND test_artworkcandidates)

# FuzzyMatch tests (Kartend-581t). Pure subsequence score, no DB.
add_executable(test_fuzzymatch
  utils/test_fuzzymatch.cpp
  ${SRC_DIR}/utils/text/fuzzymatch.cpp
)
target_link_libraries(test_fuzzymatch
  Qt6::Core
  Qt6::Test
)
add_test(NAME FuzzyMatch COMMAND test_fuzzymatch)

# BulkEdit tests (Kartend-hrff). Pure logic over ItemMetadata, no DB.
add_executable(test_bulkedit
  utils/test_bulkedit.cpp
  ${SRC_DIR}/utils/db/bulkedit.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
)
target_include_directories(test_bulkedit PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
)
target_link_libraries(test_bulkedit
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME BulkEdit COMMAND test_bulkedit)

# PlaceholderWarmer tests
# Links Qt6::Gui so QPixmap (returned from the injected TileFactory) can be
# constructed; QTEST_MAIN spins up QGuiApplication off the back of that.
# Forces the offscreen platform via add_test PROPERTIES so the binary runs
# headless on CI without an X/Wayland session.
add_executable(test_placeholderwarmer
  utils/test_placeholderwarmer.cpp
  ${SRC_DIR}/utils/view/placeholderwarmer.cpp
  ${SRC_DIR}/utils/view/artworkutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_placeholderwarmer PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_placeholderwarmer
  Qt6::Core
  Qt6::Gui
  Qt6::Concurrent
  Qt6::Test
)
add_test(NAME PlaceholderWarmer COMMAND test_placeholderwarmer)
set_tests_properties(PlaceholderWarmer PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# WidgetPoolManager tests
add_executable(test_widgetpoolmanager
  modules/widgetpool/test_widgetpoolmanager.cpp
  ${SRC_DIR}/modules/input/widgetpool/widgetpoolmanager.cpp
  ${SRC_DIR}/chrome/items/itemplaceholderrenderer.cpp
  ${SRC_DIR}/chrome/items/itemwidget.cpp
  ${SRC_DIR}/chrome/items/itemwidgetindicator.cpp
  ${SRC_DIR}/chrome/items/itemwidgetlifecycle.cpp
  ${SRC_DIR}/chrome/items/itemwidgetpaint.cpp
  ${SRC_DIR}/chrome/items/itemwidgetplaceholder.cpp
  ${SRC_DIR}/chrome/items/itemwidgetsetters.cpp
)
target_include_directories(test_widgetpoolmanager PRIVATE
  ${SRC_DIR}/modules/input/widgetpool
  ${SRC_DIR}/chrome/items
  ${SRC_DIR}/chrome/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/chrome/overlays
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
  modules/navigation/test_emptystatewidget.cpp
  ${SRC_DIR}/modules/input/navigation/emptystatewidget.cpp
)
target_include_directories(test_emptystatewidget PRIVATE
  ${SRC_DIR}/modules/input/navigation
  ${SRC_DIR}/chrome/items
  ${SRC_DIR}/chrome/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/chrome/overlays
  ${SRC_DIR}/ui/widgets/panes
)
target_link_libraries(test_emptystatewidget
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME EmptyStateWidget COMMAND test_emptystatewidget)

# CoverFlowWidget tests (selection clamping, gesture state transitions,
# wheel/key/mouse → signal emission, gallery owner bookkeeping). Pulls in
# VideoPreviewWidget + VideoThumbnailExtractor because the widget's ctor
# constructs the preview child and connects to the extractor singleton —
# those are linked but never asked to play, so the QtMultimedia backend is
# not actually started during the test.
add_executable(test_coverflowwidget
  ui/widgets/test_coverflowwidget.cpp
  ${SRC_DIR}/chrome/items/coverflowgallerystrip.cpp
  ${SRC_DIR}/chrome/items/coverflowwidget.cpp
  ${SRC_DIR}/chrome/media/videopreviewwidget.cpp
  ${SRC_DIR}/chrome/media/videothumbnailextractor.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
)
target_include_directories(test_coverflowwidget PRIVATE
  ${SRC_DIR}/chrome/items
  ${SRC_DIR}/chrome/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/chrome/overlays
  ${SRC_DIR}/ui/widgets/panes
  ${SRC_DIR}/ui
)
target_link_libraries(test_coverflowwidget
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Concurrent
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::Test
)
add_test(NAME CoverFlowWidget COMMAND test_coverflowwidget)

# GridUtils tests
add_executable(test_gridutils utils/test_gridutils.cpp)
target_link_libraries(test_gridutils Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME GridUtils COMMAND test_gridutils)

# CacheManager tests
add_executable(test_cachemanager
  modules/cache/test_cachemanager.cpp
  ${SRC_DIR}/modules/data/cache/cachediskstorage.cpp
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
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/api/iplaylistmanager.h
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
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
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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
  modules/query/workersignalspy.h
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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

# Regression test: broken symlinks survive a scan/prune cycle (Kartend-9mxh).
# Reuses the same query-stack source list as QueryManagerCancelScan.
add_executable(test_querymanager_broken_symlinks
  modules/query/test_querymanager_broken_symlinks.cpp
  modules/query/workersignalspy.h
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_querymanager_broken_symlinks PRIVATE
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
target_link_libraries(test_querymanager_broken_symlinks
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME QueryManagerBrokenSymlinks COMMAND test_querymanager_broken_symlinks)

# Regression test: items.path stores the ABSOLUTE path and items.rel_path the
# media-dir-relative form (Kartend-4te3); the v13 reconcile rewrites pre-v13
# relative-path rows in place. Reuses the same query-stack source list as
# QueryManagerBrokenSymlinks.
add_executable(test_querymanager_abspath
  modules/query/test_querymanager_abspath.cpp
  modules/query/workersignalspy.h
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_querymanager_abspath PRIVATE
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
target_link_libraries(test_querymanager_abspath
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME QueryManagerAbsPath COMMAND test_querymanager_abspath)

# Regression test: same-named files across collections count separately
# under queryIncludeAllCollections / showAllSubcollectionItems (Kartend-oyi2).
# Reuses the same query-stack source list as QueryManagerCancelScan.
add_executable(test_querymanager_cross_collection_count
  modules/query/test_querymanager_cross_collection_count.cpp
  modules/query/workersignalspy.h
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_querymanager_cross_collection_count PRIVATE
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
target_link_libraries(test_querymanager_cross_collection_count
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME QueryManagerCrossCollectionCount COMMAND test_querymanager_cross_collection_count)

# QueryManagerCrossCollectionTokens (Kartend-jb6d). Locks in that the
# structured-token filters apply when queryIncludeAllCollections=true so a
# future refactor of the cross-mode SQL path can't quietly drop the
# WHERE clauses.
add_executable(test_querymanager_cross_collection_tokens
  modules/query/test_querymanager_cross_collection_tokens.cpp
  modules/query/workersignalspy.h
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_querymanager_cross_collection_tokens PRIVATE
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
target_link_libraries(test_querymanager_cross_collection_tokens
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME QueryManagerCrossCollectionTokens COMMAND test_querymanager_cross_collection_tokens)

add_executable(test_querymanager_shell_collection_sort
  modules/query/test_querymanager_shell_collection_sort.cpp
  modules/query/workersignalspy.h
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_querymanager_shell_collection_sort PRIVATE
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
target_link_libraries(test_querymanager_shell_collection_sort
  Qt6::Core
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME QueryManagerShellCollectionSort COMMAND test_querymanager_shell_collection_sort)

# DatabaseManager lifecycle / threading tests
# Reuses the same query-stack source list as QueryManagerCancelScan and adds
# the databasemanager TUs. Validates worker-thread shutdown, SQL connection
# cleanup, and thread-safe path-resolution helpers.
add_executable(test_databasemanager
  modules/database/test_databasemanager.cpp
  ${SRC_DIR}/modules/data/database/cachedcountsservice.cpp
  ${SRC_DIR}/modules/data/database/databasemanager.cpp
  ${SRC_DIR}/modules/data/database/databaseschema.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/database/filemapcache.cpp
  ${SRC_DIR}/api/idatabasemanager.h
  ${SRC_DIR}/utils/text/titlefilter.cpp
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/db/historystore.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/itemmetadatacache.cpp
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
  Qt6::Gui
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME DatabaseManager COMMAND test_databasemanager)

# CachedCountsService: stale-completion drop via monotonic generation token.
# Small surface — only needs the service TU, the session interface header,
# and collectionutils for CollectionConfig.
add_executable(test_cachedcountsservice
  modules/database/test_cachedcountsservice.cpp
  ${SRC_DIR}/modules/data/database/cachedcountsservice.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
target_include_directories(test_cachedcountsservice PRIVATE
  ${SRC_DIR}
  ${SRC_DIR}/api
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/fs
)
target_link_libraries(test_cachedcountsservice
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME CachedCountsService COMMAND test_cachedcountsservice)

# ArtworkManager tests (cancellation, dedup, QPointer race)
add_executable(test_artworkmanager
  modules/artwork/test_artworkmanager.cpp
  ${SRC_DIR}/modules/media/artwork/artworkloaddispatcher.cpp
  ${SRC_DIR}/modules/media/artwork/artworkmanager.cpp
  ${SRC_DIR}/modules/media/artwork/artworkpathcatalog.cpp
  ${SRC_DIR}/modules/media/artwork/artworksilentloading.cpp
  ${SRC_DIR}/modules/media/artwork/artworkwidgetregistry.cpp
  ${SRC_DIR}/modules/data/cache/cachediskstorage.cpp
  ${SRC_DIR}/modules/data/cache/cachemanager.cpp
  ${SRC_DIR}/modules/data/cache/cachemanagerdisk.cpp
  ${SRC_DIR}/utils/app/interactionstateholder.cpp
  ${SRC_DIR}/chrome/items/itemplaceholderrenderer.cpp
  ${SRC_DIR}/chrome/items/itemwidget.cpp
  ${SRC_DIR}/chrome/items/itemwidgetindicator.cpp
  ${SRC_DIR}/chrome/items/itemwidgetlifecycle.cpp
  ${SRC_DIR}/chrome/items/itemwidgetpaint.cpp
  ${SRC_DIR}/chrome/items/itemwidgetplaceholder.cpp
  ${SRC_DIR}/chrome/items/itemwidgetsetters.cpp
  ${SRC_DIR}/utils/view/artworkutils.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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
  ${SRC_DIR}/chrome/items
  ${SRC_DIR}/chrome/media
  ${SRC_DIR}/ui/widgets/overlays
  ${SRC_DIR}/chrome/overlays
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

# CliArgs tests (--collection startup override + --import-kart/--to
# path sanitization). pathutils.cpp is compiled in because cliargs.cpp
# routes --import-kart / --to through PathUtils::expandAndValidateCliPath.
add_executable(test_cliargs
  utils/test_cliargs.cpp
  ${SRC_DIR}/utils/app/cliargs.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_cliargs PRIVATE ${SRC_DIR}/utils)
target_link_libraries(test_cliargs Qt6::Core Qt6::Test)
add_test(NAME CliArgs COMMAND test_cliargs)

# TitleFilter tests (per-collection regex title cleanup)
add_executable(test_titlefilter
  utils/test_titlefilter.cpp
  ${SRC_DIR}/utils/text/titlefilter.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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

# CollectionUtils tests — split by concern into three sibling binaries so
# CTest's per-binary output names the failing concern (enum conversion vs
# tree walk vs cycle detection) and so future additions live in their
# cohesive home rather than accreting into a single mega-file.
set(_kartend_collectionutils_test_sources
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
set(_kartend_collectionutils_test_includes
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)

# alignment + viewtype + index validation + categorization + identity + context
add_executable(test_collectionutils_alignment
  utils/test_collectionutils_alignment.cpp
  ${_kartend_collectionutils_test_sources}
)
target_include_directories(test_collectionutils_alignment PRIVATE
  ${_kartend_collectionutils_test_includes}
)
target_link_libraries(test_collectionutils_alignment Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtilsAlignment COMMAND test_collectionutils_alignment)

# ancestorIndexChain + hierarchyCache + descendant walks + applyCollectionRemoval
add_executable(test_collectionutils_ancestry
  utils/test_collectionutils_ancestry.cpp
  ${_kartend_collectionutils_test_sources}
)
target_include_directories(test_collectionutils_ancestry PRIVATE
  ${_kartend_collectionutils_test_includes}
)
target_link_libraries(test_collectionutils_ancestry Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtilsAncestry COMMAND test_collectionutils_ancestry)

# wouldCreateCircularReference (corrupt-data + degenerate-cycle handling)
add_executable(test_collectionutils_cycles
  utils/test_collectionutils_cycles.cpp
  ${_kartend_collectionutils_test_sources}
)
target_include_directories(test_collectionutils_cycles PRIVATE
  ${_kartend_collectionutils_test_includes}
)
target_link_libraries(test_collectionutils_cycles Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtilsCycles COMMAND test_collectionutils_cycles)

# ScrapeJobGrouping tests (shell-parent scrape routing — Kartend-7dng)
add_executable(test_scrapejobgrouping
  modules/scraper/test_scrapejobgrouping.cpp
  ${SRC_DIR}/ui/dialogs/scraper/scrapejobgrouping.cpp
)
target_include_directories(test_scrapejobgrouping PRIVATE
  ${SRC_DIR}/ui/dialogs
  ${SRC_DIR}/ui/dialogs/settings
  ${SRC_DIR}/ui/dialogs/collection
  ${SRC_DIR}/ui/dialogs/launcher
  ${SRC_DIR}/ui/dialogs/scraper
  ${SRC_DIR}/ui/dialogs/kart
)
target_link_libraries(test_scrapejobgrouping Qt6::Core Qt6::Test)
add_test(NAME ScrapeJobGrouping COMMAND test_scrapejobgrouping)

# ConfigValidation tests (schema + cross-collection diagnostics)
add_executable(test_configvalidation
  utils/test_configvalidation.cpp
  ${SRC_DIR}/utils/fs/configvalidation.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_configvalidation PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_configvalidation Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME ConfigValidation COMMAND test_configvalidation)

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

# Per-item LRU cache (in-memory; no DB required)
add_executable(test_itemmetadatacache
  utils/test_itemmetadatacache.cpp
  ${SRC_DIR}/utils/db/itemmetadatacache.cpp
)
target_include_directories(test_itemmetadatacache PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_itemmetadatacache
  Qt6::Core
  Qt6::Test
)
add_test(NAME ItemMetadataCache COMMAND test_itemmetadatacache)

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
  Qt6::Gui
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ItemArtwork COMMAND test_itemartwork)

# Scrape persistence tests
# Compiles the SUT plus itemartwork.cpp (for isStandardType) and the
# inline MockDatabaseManager from tests/integration/mocks. No DB / no
# network — pure file IO + capturing-mock IDatabaseManager.
add_executable(test_scrapepersistence
  modules/scraper/test_scrapepersistence.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapepersistence.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  # Q_OBJECT-bearing headers — AUTOMOC needs them in the source list to
  # generate the metaobject vtables that the linker resolves below.
  ${SRC_DIR}/api/idatabasemanager.h
  integration/mocks/mockdatabasemanager.h
)
target_include_directories(test_scrapepersistence PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_scrapepersistence
  Qt6::Core
  Qt6::Gui
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ScrapePersistence COMMAND test_scrapepersistence)

# Direct (worker-thread) scrape persistence tests
# Exercises Scraper::saveScrapedMetadataDirect against a real :memory:
# SQLite. This is the path BatchScrapeRunner's write worker runs on its
# own thread (Kartend-gnkb); the IDatabaseManager-backed
# saveScrapedMetadata is covered separately by test_scrapepersistence
# above. Both share the private mergeScrapedIntoExisting helper, but the
# direct path adds connection-validity guards and uses ItemMetadataStore /
# ItemArtworkStore directly rather than via the cache-invalidating
# IDatabaseManager facades.
add_executable(test_scrapepersistence_direct
  modules/scraper/test_scrapepersistence_direct.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapepersistence.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  # Q_OBJECT-bearing header — scrapepersistence.cpp references
  # IDatabaseManager's typeinfo, which only exists in the moc-generated
  # metaobject TU. AUTOMOC needs the header in the source list to emit it.
  ${SRC_DIR}/api/idatabasemanager.h
)
target_include_directories(test_scrapepersistence_direct PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_scrapepersistence_direct
  Qt6::Core
  Qt6::Gui
  Qt6::Sql
  Qt6::Test
)
add_test(NAME ScrapePersistenceDirect COMMAND test_scrapepersistence_direct)

# BatchScrapeRunner end-to-end integration with a real DatabaseManager
# Confirms the gnkb wiring (ScrapeWriteWorker on a separate thread + its
# own QSqlDatabase + IDatabaseManager::invalidateMetadataCacheItem hop
# back to the main thread) actually invalidates the metadata cache after
# a write. Without this assertion a regression in onWriteCompleted's
# invalidate call would silently leave the sidebar showing pre-scrape
# stale data and only surface as a user-reported bug. Source list
# mirrors test_databasemanager (real DB stack) plus the scraper sources.
add_executable(test_batchscraperunner_integration
  modules/scraper/test_batchscraperunner_integration.cpp
  modules/scraper/stubmetadataprovider.h
  ${SRC_DIR}/modules/data/scraper/core/batchscraperunner.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapepersistence.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapewriteworker.cpp
  ${SRC_DIR}/modules/data/database/cachedcountsservice.cpp
  ${SRC_DIR}/modules/data/database/databasemanager.cpp
  ${SRC_DIR}/modules/data/database/databaseschema.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/modules/data/database/filemapcache.cpp
  ${SRC_DIR}/api/idatabasemanager.h
  ${SRC_DIR}/utils/text/titlefilter.cpp
  ${SRC_DIR}/modules/data/query/preparedstatementcache.cpp
  ${SRC_DIR}/modules/data/query/scanworkcontroller.cpp
  ${SRC_DIR}/modules/data/query/scanneditemstable.cpp
  ${SRC_DIR}/modules/data/query/scanservice.cpp
  ${SRC_DIR}/modules/data/query/scanservice_persist.cpp
  ${SRC_DIR}/modules/data/query/querymanager.cpp
  ${SRC_DIR}/modules/data/query/querymanagerstatichelpers.cpp
  ${SRC_DIR}/modules/data/query/querymanagerlifecycle.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetchrange.cpp
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
  ${SRC_DIR}/modules/data/query/querymanagerscan.cpp
  ${SRC_DIR}/modules/data/query/querymanagerconnection.cpp
  ${SRC_DIR}/modules/data/query/querymanagercache.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
  ${SRC_DIR}/modules/data/query/querymanagerload.cpp
  ${SRC_DIR}/modules/data/query/querymanagerfetch.cpp
  ${SRC_DIR}/modules/data/query/queryhelpers.cpp
  ${SRC_DIR}/modules/data/session/sessionmanager.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/db/historystore.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/itemmetadatacache.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
  ${SRC_DIR}/utils/db/usagestatsstore.cpp
)
target_include_directories(test_batchscraperunner_integration PRIVATE
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/modules/data/query
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/modules/data/session
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_batchscraperunner_integration
  Qt6::Core
  Qt6::Gui
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME BatchScrapeRunnerIntegration
         COMMAND test_batchscraperunner_integration)

# MusicBrainz parser tests
add_executable(test_musicbrainzparser
  modules/scraper/test_musicbrainzparser.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/musicbrainzparser.cpp
)
target_include_directories(test_musicbrainzparser PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_musicbrainzparser
  Qt6::Core
  Qt6::Test
)
add_test(NAME MusicBrainzParser COMMAND test_musicbrainzparser)

# ScreenScraper system cache tests (parser + on-disk round-trip)
add_executable(test_screenscrapersystemcache
  modules/scraper/test_screenscrapersystemcache.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscrapersystemcache.cpp
)
target_include_directories(test_screenscrapersystemcache PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_screenscrapersystemcache
  Qt6::Core
  Qt6::Test
)
add_test(NAME ScreenScraperSystemCache COMMAND test_screenscrapersystemcache)

# ScreenScraper systems autodetect tests
add_executable(test_screenscrapersystems
  modules/scraper/test_screenscrapersystems.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscrapersystems.cpp
)
target_include_directories(test_screenscrapersystems PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
)
target_link_libraries(test_screenscrapersystems
  Qt6::Core
  Qt6::Test
)
add_test(NAME ScreenScraperSystems COMMAND test_screenscrapersystems)

# ScreenScraper parser tests
add_executable(test_screenscraperparser
  modules/scraper/test_screenscraperparser.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/screenscraperparser.cpp
)
target_include_directories(test_screenscraperparser PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_screenscraperparser
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME ScreenScraperParser COMMAND test_screenscraperparser)

# TMDB parser tests
add_executable(test_tmdbparser
  modules/scraper/test_tmdbparser.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/tmdbparser.cpp
)
target_include_directories(test_tmdbparser PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_tmdbparser
  Qt6::Core
  Qt6::Test
)
add_test(NAME TmdbParser COMMAND test_tmdbparser)

# Open Library parser tests
add_executable(test_openlibraryparser
  modules/scraper/test_openlibraryparser.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/openlibraryparser.cpp
)
target_include_directories(test_openlibraryparser PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_openlibraryparser
  Qt6::Core
  Qt6::Test
)
add_test(NAME OpenLibraryParser COMMAND test_openlibraryparser)

# MetadataProviderRegistry tests
# Must compile the MusicBrainz provider too because the registry's
# builtIn() instantiates one. Qt6::Network is needed because the
# provider's ctor calls HttpClient::instance() (lazy singleton init,
# no network traffic during the test) which #includes QNetworkAccessManager.
add_executable(test_metadataproviderregistry
  modules/scraper/test_metadataproviderregistry.cpp
  ${SRC_DIR}/modules/data/scraper/core/httpclient.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapelogging.cpp
  ${SRC_DIR}/modules/data/scraper/providers/metadataproviderregistry.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/musicbrainzparser.cpp
  ${SRC_DIR}/modules/data/scraper/providers/musicbrainzprovider.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/openlibraryparser.cpp
  ${SRC_DIR}/modules/data/scraper/providers/openlibraryprovider.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscrapercatalogmanager.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscrapermediatypecache.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/screenscraperparser.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscraperprovider.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscraperquotamanager.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscrapersystemcache.cpp
  ${SRC_DIR}/modules/data/scraper/providers/screenscrapersystems.cpp
  ${SRC_DIR}/modules/data/scraper/parsers/tmdbparser.cpp
  ${SRC_DIR}/modules/data/scraper/providers/tmdbprovider.cpp
  ${SRC_DIR}/modules/data/scraper/providers/websearchprovider.cpp
  ${SRC_DIR}/modules/data/scraper/core/bundledcredentials.cpp
  ${SRC_DIR}/utils/fs/romhasher.cpp
  ${SRC_DIR}/modules/data/dat/datlookup.cpp
  ${SRC_DIR}/modules/data/dat/datcache.cpp
)
target_include_directories(test_metadataproviderregistry PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/modules/data/dat
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/fs
)
target_compile_definitions(test_metadataproviderregistry PRIVATE
  APP_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(test_metadataproviderregistry
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Sql
  Qt6::Test
)
add_test(NAME MetadataProviderRegistry COMMAND test_metadataproviderregistry)

# HttpClient tests (Kartend-zy10 response-size cap). Runs a local
# QTcpServer that floods bytes after a minimal HTTP response header and
# verifies HttpClient aborts the reply once the cap is exceeded. The
# control-case test asserts a small response under the cap is delivered
# in full. Pure Qt::Network — no QHttpServer dependency (which isn't in
# Kartend's required Qt components and isn't guaranteed to be in CI).
add_executable(test_httpclient
  modules/scraper/test_httpclient.cpp
  ${SRC_DIR}/modules/data/scraper/core/httpclient.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapelogging.cpp
)
target_include_directories(test_httpclient PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_httpclient
  Qt6::Core
  Qt6::Network
  Qt6::Test
)
add_test(NAME HttpClient COMMAND test_httpclient)

# LauncherProbe tests
add_executable(test_launcherprobe
  utils/test_launcherprobe.cpp
  ${SRC_DIR}/utils/fs/launcherprobe.cpp
)
target_include_directories(test_launcherprobe PRIVATE
  ${SRC_DIR}/utils/fs
)
target_link_libraries(test_launcherprobe
  Qt6::Core
  Qt6::Test
)
add_test(NAME LauncherProbe COMMAND test_launcherprobe)

# DatLookup tests (Logiqx + MAME listxml DAT parsers + indexed store)
add_executable(test_datlookup
  modules/dat/test_datlookup.cpp
  ${SRC_DIR}/modules/data/dat/datlookup.cpp
)
target_include_directories(test_datlookup PRIVATE
  ${SRC_DIR}/modules/data/dat
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_datlookup
  Qt6::Core
  Qt6::Test
)
add_test(NAME DatLookup COMMAND test_datlookup)

# DatCache tests (on-disk sqlite cache for parsed DAT files)
add_executable(test_datcache
  modules/dat/test_datcache.cpp
  ${SRC_DIR}/modules/data/dat/datcache.cpp
  ${SRC_DIR}/modules/data/dat/datlookup.cpp
)
target_include_directories(test_datcache PRIVATE
  ${SRC_DIR}/modules/data/dat
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_datcache
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME DatCache COMMAND test_datcache)

# BatchScrapeRunner tests (async batch scrape state machine)
# Compiles scrapepersistence + its db-store dependencies so the runner
# can exercise its `applyScrapedItem` integration path without a real
# QSqlDatabase. Uses the same mock IDatabaseManager from
# tests/integration/mocks/ that test_scrapepersistence does. The write
# worker + databaseschema sources are linked so the runner's lazy-init
# code path (ensureWriteWorkerStarted) resolves at link time even though
# the null-DB tests skip it at runtime.
add_executable(test_batchscraperunner
  modules/scraper/test_batchscraperunner.cpp
  ${SRC_DIR}/modules/data/scraper/core/batchscraperunner.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapepersistence.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapewriteworker.cpp
  ${SRC_DIR}/modules/data/database/databaseschema.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/api/idatabasemanager.h
  ${SRC_DIR}/modules/data/scraper/core/scrapewriteworker.h
  integration/mocks/mockdatabasemanager.h
)
target_include_directories(test_batchscraperunner PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_batchscraperunner
  Qt6::Core
  Qt6::Gui
  Qt6::Sql
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME BatchScrapeRunner COMMAND test_batchscraperunner)

add_executable(test_scraperservice_resume
  modules/scraper/test_scraperservice_resume.cpp
  ${SRC_DIR}/modules/data/scraper/core/scraperservice.cpp
  ${SRC_DIR}/modules/data/scraper/core/batchscraperunner.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapepersistence.cpp
  ${SRC_DIR}/modules/data/scraper/core/scrapewriteworker.cpp
  ${SRC_DIR}/modules/data/database/databaseschema.cpp
  ${SRC_DIR}/modules/data/database/connectionpragmas.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/api/idatabasemanager.h
  ${SRC_DIR}/modules/data/scraper/core/scrapewriteworker.h
)
target_include_directories(test_scraperservice_resume PRIVATE
  ${SRC_DIR}/modules/data/scraper/core
  ${SRC_DIR}/modules/data/scraper/parsers
  ${SRC_DIR}/modules/data/scraper/providers
  ${SRC_DIR}/modules/data/database
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_scraperservice_resume
  Qt6::Core
  Qt6::Gui
  Qt6::Sql
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME ScraperServiceResume COMMAND test_scraperservice_resume)

# ScrapeLogger tests (message-handler tee of kartend.scrape* to scrape.log)
add_executable(test_scrapelogger
  utils/test_scrapelogger.cpp
  ${SRC_DIR}/utils/app/scrapelogger.cpp
)
target_include_directories(test_scrapelogger PRIVATE
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_scrapelogger
  Qt6::Core
  Qt6::Test
)
add_test(NAME ScrapeLogger COMMAND test_scrapelogger)

# RomHasher tests (streaming MD5/SHA1 for SS hash-based ROM ID)
add_executable(test_romhasher
  utils/test_romhasher.cpp
  ${SRC_DIR}/utils/fs/romhasher.cpp
)
target_include_directories(test_romhasher PRIVATE
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_romhasher
  Qt6::Core
  Qt6::Test
)
add_test(NAME RomHasher COMMAND test_romhasher)

# RetroArchUtils tests (retroarch.cfg parsing + libretro core discovery)
add_executable(test_retroarchutils
  utils/test_retroarchutils.cpp
  ${SRC_DIR}/utils/fs/retroarchutils.cpp
)
target_include_directories(test_retroarchutils PRIVATE
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_retroarchutils
  Qt6::Core
  Qt6::Test
)
add_test(NAME RetroArchUtils COMMAND test_retroarchutils)

# KdeColorScheme tests
# Includes resources.qrc so the test binary's `:/themes/` lookups
# resolve to the same bundled .colors files the production app sees;
# without this, discover_includesBundledSchemes would always fail.
add_executable(test_kdecolorscheme
  utils/test_kdecolorscheme.cpp
  ${SRC_DIR}/utils/view/kdecolorscheme.cpp
  ${SRC_DIR}/assets/resources.qrc
)
target_include_directories(test_kdecolorscheme PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui/uiconstants
)
target_link_libraries(test_kdecolorscheme
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME KdeColorScheme COMMAND test_kdecolorscheme)

# SmartFilter tests
add_executable(test_smartfilter
  utils/test_smartfilter.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
)
target_include_directories(test_smartfilter PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
)
target_link_libraries(test_smartfilter
  Qt6::Core
  Qt6::Test
)
add_test(NAME SmartFilter COMMAND test_smartfilter)

# SmartPlaylistEvaluator tests
add_executable(test_smartplaylistevaluator
  utils/test_smartplaylistevaluator.cpp
  ${SRC_DIR}/utils/db/smartfilter.cpp
  ${SRC_DIR}/utils/db/smartplaylistevaluator.cpp
)
target_include_directories(test_smartplaylistevaluator PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
)
target_link_libraries(test_smartplaylistevaluator
  Qt6::Core
  Qt6::Sql
  Qt6::Test
)
add_test(NAME SmartPlaylistEvaluator COMMAND test_smartplaylistevaluator)

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

# VariantGrouping tests (Kartend-skhk). Pure same-basename grouper.
add_executable(test_variantgrouping
  utils/test_variantgrouping.cpp
  ${SRC_DIR}/utils/db/variantgrouping.cpp
)
target_include_directories(test_variantgrouping PRIVATE
  ${SRC_DIR}/utils/db
)
target_link_libraries(test_variantgrouping
  Qt6::Core
  Qt6::Test
)
add_test(NAME VariantGrouping COMMAND test_variantgrouping)

# KartPreflight tests (Kartend-fr4z). Pure helpers + report assembly so the
# UI dialog has something to render. Reuses kartsuspiciouspaths.cpp so the
# allowlist branch can be exercised without dragging in KartManager itself.
add_executable(test_kartpreflight
  utils/test_kartpreflight.cpp
  ${SRC_DIR}/modules/data/kart/kartpreflight.cpp
  ${SRC_DIR}/modules/data/kart/kartsuspiciouspaths.cpp
  ${SRC_DIR}/modules/data/kart/kartmanifest.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/db/itemmetadata.cpp
  ${SRC_DIR}/utils/db/dbmigrations.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
)
target_include_directories(test_kartpreflight PRIVATE
  ${SRC_DIR}/modules/data/kart
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_kartpreflight
  Qt6::Core
  Qt6::Sql
  Qt6::Widgets
  Qt6::Test
)
add_test(NAME KartPreflight COMMAND test_kartpreflight)

# KartManifest tests (JSON round-trip for the .kart packaging format)
add_executable(test_kartmanifest
  modules/kart/test_kartmanifest.cpp
  ${SRC_DIR}/modules/data/kart/kartmanifest.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
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

# InteractionHelpers tests (pure helpers extracted)
add_executable(test_interactionhelpers
  modules/interaction/test_interactionhelpers.cpp
  ${SRC_DIR}/modules/input/interaction/interactionhelpers.cpp
)
target_include_directories(test_interactionhelpers PRIVATE
  ${SRC_DIR}/modules/input/interaction
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_interactionhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME InteractionHelpers COMMAND test_interactionhelpers)

# EventHelpers tests (pure helpers extracted from EventManager)
add_executable(test_eventhelpers
  modules/event/test_eventhelpers.cpp
  ${SRC_DIR}/modules/input/event/eventhelpers.cpp
)
target_include_directories(test_eventhelpers PRIVATE
  ${SRC_DIR}/modules/input/event
  ${SRC_DIR}/utils/view
)
target_link_libraries(test_eventhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME EventHelpers COMMAND test_eventhelpers)

# SettingsHelpers tests (pure helpers extracted from SettingsManager)
add_executable(test_settingshelpers
  modules/settings/test_settingshelpers.cpp
  ${SRC_DIR}/modules/data/settings/settingshelpers.cpp
)
target_include_directories(test_settingshelpers PRIVATE
  ${SRC_DIR}/modules/data/settings
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_settingshelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME SettingsHelpers COMMAND test_settingshelpers)

# SettingsManager migration / schemaVersion tests (Kartend-39g9). Links
# against the full kartend object-lib aggregate because SettingsManager
# pulls in a wide dependency footprint (path/error/config-validation utils,
# manager interfaces). Fixtures live alongside the test source so each
# build picks them up via the KARTEND_TEST_FIXTURES_DIR compile define.
add_executable(test_settingsmigration
  modules/settings/test_settingsmigration.cpp
)
target_link_libraries(test_settingsmigration
  PRIVATE
    ${KARTEND_AREA_LIBS}
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Widgets
    Qt6::Test
)
target_compile_definitions(test_settingsmigration PRIVATE
  KARTEND_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/modules/settings/fixtures"
)
target_include_directories(test_settingsmigration PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)
add_test(NAME SettingsMigration COMMAND test_settingsmigration)

# Strict round-trip preservation of unknown per-collection INI keys.
# Guards against the version-skew data-loss class where a newer build's
# key is silently dropped when an older build loads + saves the same file.
add_executable(test_settingsroundtrip
  modules/settings/test_settingsroundtrip.cpp
)
target_link_libraries(test_settingsroundtrip
  PRIVATE
    ${KARTEND_AREA_LIBS}
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Widgets
    Qt6::Test
)
target_include_directories(test_settingsroundtrip PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)
add_test(NAME SettingsRoundtrip COMMAND test_settingsroundtrip)

# Per-domain *Changed signals from saveCollections — emit-on-diff
# coverage. Pairs with the contract in src/api/isettingsmanager.h.
add_executable(test_perdomainsignals
  modules/settings/test_perdomainsignals.cpp
)
target_link_libraries(test_perdomainsignals
  PRIVATE
    ${KARTEND_AREA_LIBS}
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Widgets
    Qt6::Test
)
target_include_directories(test_perdomainsignals PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)
add_test(NAME PerDomainSignals COMMAND test_perdomainsignals)

# KeyboardHelpers tests (pure helpers extracted from KeyboardManager)
add_executable(test_keyboardhelpers
  modules/keyboard/test_keyboardhelpers.cpp
  ${SRC_DIR}/modules/input/keyboard/keyboardhelpers.cpp
)
target_include_directories(test_keyboardhelpers PRIVATE
  ${SRC_DIR}/modules/input/keyboard
  ${SRC_DIR}/utils/app
)
target_link_libraries(test_keyboardhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME KeyboardHelpers COMMAND test_keyboardhelpers)

# SearchHelpers tests (pure helpers extracted)
add_executable(test_searchhelpers
  modules/search/test_searchhelpers.cpp
  ${SRC_DIR}/modules/input/search/searchhelpers.cpp
  ${SRC_DIR}/utils/app/collectionutils.cpp
  ${SRC_DIR}/utils/app/collection/helpers.cpp
  ${SRC_DIR}/utils/app/collection/collectionhierarchycache.cpp
  ${SRC_DIR}/utils/app/collection/launcherconfig.cpp
  ${SRC_DIR}/utils/app/settingsutils.cpp
  ${SRC_DIR}/utils/fs/pathutils.cpp
)
target_include_directories(test_searchhelpers PRIVATE
  ${SRC_DIR}/modules/input/search
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
  ${SRC_DIR}/utils/text/searchqueryparser.cpp
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
  ${SRC_DIR}/modules/input/filter/filterhelpers.cpp
)
target_include_directories(test_filterhelpers PRIVATE
  ${SRC_DIR}/modules/input/filter
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
  benchmarks/bench_filterhelpers.cpp
  ${SRC_DIR}/modules/input/filter/filterhelpers.cpp
)
target_include_directories(bench_filterhelpers PRIVATE
  ${SRC_DIR}/modules/input/filter
)
target_link_libraries(bench_filterhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME BenchFilterHelpers COMMAND bench_filterhelpers)
set_tests_properties(BenchFilterHelpers PROPERTIES LABELS "benchmark")# GamepadHelpers tests (pure helpers extracted from GamepadManager)
add_executable(test_gamepadhelpers
  modules/gamepad/test_gamepadhelpers.cpp
  ${SRC_DIR}/modules/input/gamepad/gamepadhelpers.cpp
)
target_include_directories(test_gamepadhelpers PRIVATE
  ${SRC_DIR}/modules/input/gamepad
)
target_link_libraries(test_gamepadhelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME GamepadHelpers COMMAND test_gamepadhelpers)

# OverlayHelpers tests (pure helpers extracted from SelectionOverlayManager)
add_executable(test_overlayhelpers
  modules/overlay/test_overlayhelpers.cpp
  ${SRC_DIR}/modules/input/overlay/overlayhelpers.cpp
)
target_include_directories(test_overlayhelpers PRIVATE
  ${SRC_DIR}/modules/input/overlay
)
target_link_libraries(test_overlayhelpers
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)
add_test(NAME OverlayHelpers COMMAND test_overlayhelpers)

# AttractHelpers tests (pure helpers extracted from AttractManager)
add_executable(test_attracthelpers
  modules/attract/test_attracthelpers.cpp
  ${SRC_DIR}/modules/input/attract/attracthelpers.cpp
)
target_include_directories(test_attracthelpers PRIVATE
  ${SRC_DIR}/modules/input/attract
)
target_link_libraries(test_attracthelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME AttractHelpers COMMAND test_attracthelpers)

# SelectionRestoreHelpers tests (pure helpers extracted from SelectionRestoreCoordinator)
add_executable(test_selectionrestorehelpers
  modules/restore/test_selectionrestorehelpers.cpp
  ${SRC_DIR}/modules/data/restore/selectionrestorehelpers.cpp
)
target_include_directories(test_selectionrestorehelpers PRIVATE
  ${SRC_DIR}/modules/data/restore
)
target_link_libraries(test_selectionrestorehelpers
  Qt6::Core
  Qt6::Test
)
add_test(NAME SelectionRestoreHelpers COMMAND test_selectionrestorehelpers)

# DetailPageHelpers tests (pure helpers extracted from DetailPageManager)
add_executable(test_detailpagehelpers
  modules/detailpage/test_detailpagehelpers.cpp
  ${SRC_DIR}/modules/media/detailpage/detailpagehelpers.cpp
  ${SRC_DIR}/utils/db/itemartwork.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
)
target_include_directories(test_detailpagehelpers PRIVATE
  ${SRC_DIR}/modules/media/detailpage
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)
target_link_libraries(test_detailpagehelpers
  Qt6::Core
  Qt6::Gui
  Qt6::Sql
  Qt6::Test
)
add_test(NAME DetailPageHelpers COMMAND test_detailpagehelpers)

# Integration test harness for UI-coordinator managers.
# Only built when the per-area OBJECT libs are available (i.e. when
# tests/CMakeLists.txt is added from the top-level CMakeLists.txt rather than
# configured stand-alone). kartend_core is a representative sentinel.
if (TARGET kartend_core)
  add_subdirectory(integration)
endif()
