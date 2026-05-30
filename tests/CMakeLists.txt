# Kartend Tests
cmake_minimum_required(VERSION 3.20)

# Find Qt6 Test module
find_package(Qt6 REQUIRED COMPONENTS Test Gui Widgets)

# Enable testing
enable_testing()

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
# Tests migrated by Kartend-oeuf link the relevant kartend_<area>
# OBJECT lib(s) and pick up include directories from those libs'
# INTERFACE properties. A small set of header-only / Qt-only smoke
# tests still resolves their #includes through this directory-level
# fallback — they're trivial enough that wiring an area lib in would
# be more code than the test itself.
include_directories(
  ${SRC_DIR}/api
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)

# GridLayoutCalculator tests
# Source lives in src/modules/input/scroll/, compiled into the
# kartend_input OBJECT lib. Linking the area lib (Kartend-oeuf) lets a
# .cpp rename or split land in src/ without us hand-updating this list.
add_executable(test_gridlayoutcalculator
  modules/scroll/test_gridlayoutcalculator.cpp
)
target_link_libraries(test_gridlayoutcalculator PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME GridLayoutCalculator COMMAND test_gridlayoutcalculator)

# ScrollDataStore tests (synthetic Home view tile population — Kartend-83iu)
add_executable(test_scrolldatamanager
  modules/scroll/test_scrolldatamanager.cpp
)
target_link_libraries(test_scrolldatamanager PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME ScrollDataStore COMMAND test_scrolldatamanager)

# ViewportHelpers tests (small-movement threshold, force-immediate combine)
add_executable(test_viewporthelpers
  modules/viewport/test_viewporthelpers.cpp
)
target_link_libraries(test_viewporthelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME ViewportHelpers COMMAND test_viewporthelpers)

# AnimationManager static helper tests (pure functions: duration calc,
# target Y/X computation, visibility scroll target).
add_executable(test_animationmanager
  modules/animation/test_animationmanager.cpp
)
target_link_libraries(test_animationmanager PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME AnimationManager COMMAND test_animationmanager)

# InteractionStateHolder tests
add_executable(test_interactionstateholder
  modules/interaction/test_interactionstateholder.cpp
)
target_link_libraries(test_interactionstateholder PRIVATE kartend_utils Qt6::Test)
add_test(NAME InteractionStateHolder COMMAND test_interactionstateholder)

# LaunchManager tests (security validation)
add_executable(test_launchmanager
  modules/launch/test_launchmanager.cpp
)
target_link_libraries(test_launchmanager PRIVATE kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME LaunchManager COMMAND test_launchmanager)

# PathUtils tests
add_executable(test_pathutils
  utils/fs/test_pathutils.cpp
)
target_link_libraries(test_pathutils PRIVATE kartend_utils Qt6::Test)
add_test(NAME PathUtils COMMAND test_pathutils)

# VideoUtils tests
add_executable(test_videoutils
  utils/view/test_videoutils.cpp
)
target_link_libraries(test_videoutils PRIVATE kartend_utils Qt6::Test)
add_test(NAME VideoUtils COMMAND test_videoutils)

# ExtensionUtils tests (image-decode allowlist guard — keeps scraped .pdf
# manuals out of QImageReader / Qt's abort()-prone PDF image plugin)
add_executable(test_extensionutils
  utils/fs/test_extensionutils.cpp
)
target_link_libraries(test_extensionutils PRIVATE kartend_utils Qt6::Test)
add_test(NAME ExtensionUtils COMMAND test_extensionutils)

# SearchQueryParser tests (Kartend-9qv2). Pure string -> struct, no DB.
add_executable(test_searchqueryparser
  utils/text/test_searchqueryparser.cpp
)
target_link_libraries(test_searchqueryparser PRIVATE kartend_utils Qt6::Test)
add_test(NAME SearchQueryParser COMMAND test_searchqueryparser)

# CollectionFilesystemWatcher tests (Kartend-kr1g). Pure-helper subdir
# enumeration plus the configure() reconcile path. Uses QTemporaryDir for
# scratch trees so no production paths are touched.
add_executable(test_collectionfilesystemwatcher
  modules/watcher/test_collectionfilesystemwatcher.cpp
)
target_link_libraries(test_collectionfilesystemwatcher PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME CollectionFilesystemWatcher COMMAND test_collectionfilesystemwatcher)

# ThemePreset tests (Kartend-y3rg). JSON round-trip + file I/O + apply.
add_executable(test_themepreset
  utils/app/test_themepreset.cpp
)
target_link_libraries(test_themepreset PRIVATE kartend_utils Qt6::Test)
add_test(NAME ThemePreset COMMAND test_themepreset)

# PresentationProfile tests (Kartend-6pp5). JSON round-trip + apply + registry.
add_executable(test_presentationprofile
  utils/app/test_presentationprofile.cpp
)
target_link_libraries(test_presentationprofile PRIVATE kartend_utils Qt6::Test)
add_test(NAME PresentationProfile COMMAND test_presentationprofile)

# CollectionHealth tests (Kartend-lprv). Pure analyzer over fixture rows.
add_executable(test_collectionhealth
  utils/db/test_collectionhealth.cpp
)
target_link_libraries(test_collectionhealth PRIVATE kartend_utils Qt6::Test)
add_test(NAME CollectionHealth COMMAND test_collectionhealth)

# MetadataQueue tests (Kartend-gtly). Pure analyzer over a metadata loader closure.
add_executable(test_metadataqueue
  utils/db/test_metadataqueue.cpp
)
target_link_libraries(test_metadataqueue PRIVATE kartend_utils Qt6::Test)
add_test(NAME MetadataQueue COMMAND test_metadataqueue)

# ArtworkCandidates tests (Kartend-y02i). Pure ranker, no filesystem.
add_executable(test_artworkcandidates
  utils/view/test_artworkcandidates.cpp
)
target_link_libraries(test_artworkcandidates PRIVATE kartend_utils Qt6::Test)
add_test(NAME ArtworkCandidates COMMAND test_artworkcandidates)

# FuzzyMatch tests (Kartend-581t). Pure subsequence score, no DB.
add_executable(test_fuzzymatch
  utils/text/test_fuzzymatch.cpp
)
target_link_libraries(test_fuzzymatch PRIVATE kartend_utils Qt6::Test)
add_test(NAME FuzzyMatch COMMAND test_fuzzymatch)

# BulkEdit tests (Kartend-hrff). Pure logic over ItemMetadata, no DB.
add_executable(test_bulkedit
  utils/db/test_bulkedit.cpp
)
target_link_libraries(test_bulkedit PRIVATE kartend_utils Qt6::Test)
add_test(NAME BulkEdit COMMAND test_bulkedit)

# PlaceholderWarmer tests
# Links Qt6::Gui so QPixmap (returned from the injected TileFactory) can be
# constructed; QTEST_MAIN spins up QGuiApplication off the back of that.
# Forces the offscreen platform via add_test PROPERTIES so the binary runs
# headless on CI without an X/Wayland session.
add_executable(test_placeholderwarmer
  utils/view/test_placeholderwarmer.cpp
)
target_link_libraries(test_placeholderwarmer PRIVATE kartend_utils Qt6::Test)
add_test(NAME PlaceholderWarmer COMMAND test_placeholderwarmer)
set_tests_properties(PlaceholderWarmer PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# WidgetPoolManager tests
add_executable(test_widgetpoolmanager
  modules/widgetpool/test_widgetpoolmanager.cpp
)
target_link_libraries(test_widgetpoolmanager PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME WidgetPoolManager COMMAND test_widgetpoolmanager)

# EmptyStateWidget tests
add_executable(test_emptystatewidget
  modules/navigation/test_emptystatewidget.cpp
)
target_link_libraries(test_emptystatewidget PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME EmptyStateWidget COMMAND test_emptystatewidget)

# CoverFlowWidget tests (selection clamping, gesture state transitions,
# wheel/key/mouse → signal emission, gallery owner bookkeeping). Pulls in
# VideoPreviewWidget + VideoThumbnailExtractor because the widget's ctor
# constructs the preview child and connects to the extractor singleton —
# those are linked but never asked to play, so the QtMultimedia backend is
# not actually started during the test.
add_executable(test_coverflowwidget
  ui/widgets/test_coverflowwidget.cpp
)
target_link_libraries(test_coverflowwidget PRIVATE kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME CoverFlowWidget COMMAND test_coverflowwidget)

# Kartend-liye: PathStatusGlyph attaches a trailing-action warning glyph to a
# QLineEdit. Test links kartend_ui (for the helper) + kartend_utils (for
# PathStatus + pathStatusDescription).
add_executable(test_pathstatusglyph
  ui/widgets/test_pathstatusglyph.cpp
)
target_link_libraries(test_pathstatusglyph PRIVATE kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME PathStatusGlyph COMMAND test_pathstatusglyph)

# GridUtils tests
add_executable(test_gridutils utils/view/test_gridutils.cpp)
target_link_libraries(test_gridutils Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME GridUtils COMMAND test_gridutils)

# CacheManager tests
add_executable(test_cachemanager
  modules/cache/test_cachemanager.cpp
)
target_link_libraries(test_cachemanager PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME CacheManager COMMAND test_cachemanager)

# PlaylistManager tests
add_executable(test_playlistmanager
  modules/playlist/test_playlistmanager.cpp
)
target_link_libraries(test_playlistmanager PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME PlaylistManager COMMAND test_playlistmanager)

# SessionManager tests
add_executable(test_sessionmanager
  modules/session/test_sessionmanager.cpp
)
target_link_libraries(test_sessionmanager PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME SessionManager COMMAND test_sessionmanager)

# QueryManager cancellation semantics test
add_executable(test_querymanager_cancel_scan
  modules/query/test_querymanager_cancel_scan.cpp
  modules/query/workersignalspy.h
)
target_link_libraries(test_querymanager_cancel_scan PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryManagerCancelScan COMMAND test_querymanager_cancel_scan)

# Regression test: broken symlinks survive a scan/prune cycle (Kartend-9mxh).
# Reuses the same query-stack source list as QueryManagerCancelScan.
add_executable(test_querymanager_broken_symlinks
  modules/query/test_querymanager_broken_symlinks.cpp
  modules/query/workersignalspy.h
)
target_link_libraries(test_querymanager_broken_symlinks PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryManagerBrokenSymlinks COMMAND test_querymanager_broken_symlinks)

# Regression test: items.path stores the ABSOLUTE path and items.rel_path the
# media-dir-relative form (Kartend-4te3); the v13 reconcile rewrites pre-v13
# relative-path rows in place. Reuses the same query-stack source list as
# QueryManagerBrokenSymlinks.
add_executable(test_querymanager_abspath
  modules/query/test_querymanager_abspath.cpp
  modules/query/workersignalspy.h
)
target_link_libraries(test_querymanager_abspath PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryManagerAbsPath COMMAND test_querymanager_abspath)

# Regression test: same-named files across collections count separately
# under queryIncludeAllCollections / showAllSubcollectionItems (Kartend-oyi2).
# Reuses the same query-stack source list as QueryManagerCancelScan.
add_executable(test_querymanager_cross_collection_count
  modules/query/test_querymanager_cross_collection_count.cpp
  modules/query/workersignalspy.h
)
target_link_libraries(test_querymanager_cross_collection_count PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryManagerCrossCollectionCount COMMAND test_querymanager_cross_collection_count)

# QueryManagerCrossCollectionTokens (Kartend-jb6d). Locks in that the
# structured-token filters apply when queryIncludeAllCollections=true so a
# future refactor of the cross-mode SQL path can't quietly drop the
# WHERE clauses.
add_executable(test_querymanager_cross_collection_tokens
  modules/query/test_querymanager_cross_collection_tokens.cpp
  modules/query/workersignalspy.h
)
target_link_libraries(test_querymanager_cross_collection_tokens PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryManagerCrossCollectionTokens COMMAND test_querymanager_cross_collection_tokens)

add_executable(test_querymanager_shell_collection_sort
  modules/query/test_querymanager_shell_collection_sort.cpp
  modules/query/workersignalspy.h
)
target_link_libraries(test_querymanager_shell_collection_sort PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryManagerShellCollectionSort COMMAND test_querymanager_shell_collection_sort)

# DatabaseManager lifecycle / threading tests
# Reuses the same query-stack source list as QueryManagerCancelScan and adds
# the databasemanager TUs. Validates worker-thread shutdown, SQL connection
# cleanup, and thread-safe path-resolution helpers.
add_executable(test_databasemanager
  modules/database/test_databasemanager.cpp
)
target_link_libraries(test_databasemanager PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME DatabaseManager COMMAND test_databasemanager)

# CachedCountsService: stale-completion drop via monotonic generation token.
# Small surface — only needs the service TU, the session interface header,
# and collectionutils for CollectionConfig.
add_executable(test_cachedcountsservice
  modules/database/test_cachedcountsservice.cpp
)
target_link_libraries(test_cachedcountsservice PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME CachedCountsService COMMAND test_cachedcountsservice)

# ArtworkManager tests (cancellation, dedup, QPointer race)
add_executable(test_artworkmanager
  modules/artwork/test_artworkmanager.cpp
)
target_link_libraries(test_artworkmanager PRIVATE kartend_media kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME ArtworkManager COMMAND test_artworkmanager)

# StringUtils tests (header-only)
add_executable(test_stringutils utils/text/test_stringutils.cpp)
target_link_libraries(test_stringutils Qt6::Core Qt6::Test)
add_test(NAME StringUtils COMMAND test_stringutils)

# CliArgs tests (--collection startup override + --import-kart/--to
# path sanitization). pathutils.cpp is compiled in because cliargs.cpp
# routes --import-kart / --to through PathUtils::expandAndValidateCliPath.
add_executable(test_cliargs
  utils/app/test_cliargs.cpp
)
target_link_libraries(test_cliargs PRIVATE kartend_utils Qt6::Test)
add_test(NAME CliArgs COMMAND test_cliargs)

# CollectionConfig::operator== field coverage (guards against a new field being
# added to the struct but forgotten in operator==).
add_executable(test_collectionconfig
  utils/app/test_collectionconfig.cpp
)
target_link_libraries(test_collectionconfig PRIVATE kartend_utils Qt6::Test)
add_test(NAME CollectionConfig COMMAND test_collectionconfig)

# SettingsUtils export/import atomic copy/replace round-trip (Kartend-g2ox).
add_executable(test_settingsutils
  utils/app/test_settingsutils.cpp
)
target_link_libraries(test_settingsutils PRIVATE kartend_utils Qt6::Test)
add_test(NAME SettingsUtils COMMAND test_settingsutils)

# TitleFilter tests (per-collection regex title cleanup)
add_executable(test_titlefilter
  utils/text/test_titlefilter.cpp
)
target_link_libraries(test_titlefilter PRIVATE kartend_utils Qt6::Test)
add_test(NAME TitleFilter COMMAND test_titlefilter)

# CollectionUtils tests — split by concern into three sibling binaries so
# CTest's per-binary output names the failing concern (enum conversion vs
# tree walk vs cycle detection) and so future additions live in their
# cohesive home rather than accreting into a single mega-file.
set(_kartend_collectionutils_test_sources
  ${SRC_DIR}/utils/app/collection/hierarchyhelpers.cpp
  ${SRC_DIR}/utils/app/collection/typehelpers.cpp
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
  utils/app/test_collectionutils_alignment.cpp
  ${_kartend_collectionutils_test_sources}
)
target_include_directories(test_collectionutils_alignment PRIVATE
  ${_kartend_collectionutils_test_includes}
)
target_link_libraries(test_collectionutils_alignment Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtilsAlignment COMMAND test_collectionutils_alignment)

# ancestorIndexChain + hierarchyCache + descendant walks + applyCollectionRemoval
add_executable(test_collectionutils_ancestry
  utils/app/test_collectionutils_ancestry.cpp
  ${_kartend_collectionutils_test_sources}
)
target_include_directories(test_collectionutils_ancestry PRIVATE
  ${_kartend_collectionutils_test_includes}
)
target_link_libraries(test_collectionutils_ancestry Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME CollectionUtilsAncestry COMMAND test_collectionutils_ancestry)

# wouldCreateCircularReference (corrupt-data + degenerate-cycle handling)
add_executable(test_collectionutils_cycles
  utils/app/test_collectionutils_cycles.cpp
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
)
target_link_libraries(test_scrapejobgrouping PRIVATE kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME ScrapeJobGrouping COMMAND test_scrapejobgrouping)

# ConfigValidation tests (schema + cross-collection diagnostics)
add_executable(test_configvalidation
  utils/fs/test_configvalidation.cpp
)
target_link_libraries(test_configvalidation PRIVATE kartend_utils Qt6::Test)
add_test(NAME ConfigValidation COMMAND test_configvalidation)

# KeyboardManager tests (static selection helpers)
# Only the selection-helper TU is linked; it has no dependencies on
# ScrollManager / InteractionStateHolder / etc., keeping the test isolated.
add_executable(test_keyboardmanager
  modules/keyboard/test_keyboardmanager.cpp
)
target_link_libraries(test_keyboardmanager PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME KeyboardManager COMMAND test_keyboardmanager)

# NavigationStackManager tests (header-only data class powering NavigationManager)
add_executable(test_navigationstackmanager
  modules/navigation/test_navigationstackmanager.cpp
)
target_link_libraries(test_navigationstackmanager PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME NavigationStackManager COMMAND test_navigationstackmanager)

# DbMigrations tests (sqlite in-memory, no UI)
add_executable(test_dbmigrations
  utils/db/test_dbmigrations.cpp
)
target_link_libraries(test_dbmigrations PRIVATE kartend_utils Qt6::Test)
add_test(NAME DbMigrations COMMAND test_dbmigrations)

# ItemMetadata storage round-trip tests
add_executable(test_itemmetadata
  utils/db/test_itemmetadata.cpp
)
target_link_libraries(test_itemmetadata PRIVATE kartend_utils Qt6::Test)
add_test(NAME ItemMetadata COMMAND test_itemmetadata)

# Per-item LRU cache (in-memory; no DB required)
add_executable(test_itemmetadatacache
  utils/db/test_itemmetadatacache.cpp
)
target_link_libraries(test_itemmetadatacache PRIVATE kartend_utils Qt6::Test)
add_test(NAME ItemMetadataCache COMMAND test_itemmetadatacache)

# ItemArtwork storage + standard-type resolution tests
add_executable(test_itemartwork
  utils/db/test_itemartwork.cpp
)
target_link_libraries(test_itemartwork PRIVATE kartend_utils Qt6::Test)
add_test(NAME ItemArtwork COMMAND test_itemartwork)

# Scrape persistence tests
# Compiles the SUT plus itemartwork.cpp (for isStandardType) and the
# inline MockDatabaseManager from tests/integration/mocks. No DB / no
# network — pure file IO + capturing-mock IDatabaseManager.
add_executable(test_scrapepersistence
  modules/scraper/test_scrapepersistence.cpp
  integration/mocks/mockdatabasemanager.h
)
target_include_directories(test_scrapepersistence PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)
target_link_libraries(test_scrapepersistence PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
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
)
target_link_libraries(test_scrapepersistence_direct PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
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
)
target_link_libraries(test_batchscraperunner_integration PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME BatchScrapeRunnerIntegration
         COMMAND test_batchscraperunner_integration)

# MusicBrainz parser tests
add_executable(test_musicbrainzparser
  modules/scraper/test_musicbrainzparser.cpp
)
target_link_libraries(test_musicbrainzparser PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME MusicBrainzParser COMMAND test_musicbrainzparser)

# ScreenScraper system cache tests (parser + on-disk round-trip)
add_executable(test_screenscrapersystemcache
  modules/scraper/test_screenscrapersystemcache.cpp
)
target_link_libraries(test_screenscrapersystemcache PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME ScreenScraperSystemCache COMMAND test_screenscrapersystemcache)

# ScreenScraper systems autodetect tests
add_executable(test_screenscrapersystems
  modules/scraper/test_screenscrapersystems.cpp
)
target_link_libraries(test_screenscrapersystems PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME ScreenScraperSystems COMMAND test_screenscrapersystems)

# ScreenScraper parser tests
add_executable(test_screenscraperparser
  modules/scraper/test_screenscraperparser.cpp
)
target_link_libraries(test_screenscraperparser PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME ScreenScraperParser COMMAND test_screenscraperparser)

# TMDB parser tests
add_executable(test_tmdbparser
  modules/scraper/test_tmdbparser.cpp
)
target_link_libraries(test_tmdbparser PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME TmdbParser COMMAND test_tmdbparser)

# Open Library parser tests
add_executable(test_openlibraryparser
  modules/scraper/test_openlibraryparser.cpp
)
target_link_libraries(test_openlibraryparser PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME OpenLibraryParser COMMAND test_openlibraryparser)

# MetadataProviderRegistry tests
# Must compile the MusicBrainz provider too because the registry's
# builtIn() instantiates one. Qt6::Network is needed because the
# provider's ctor calls HttpClient::instance() (lazy singleton init,
# no network traffic during the test) which #includes QNetworkAccessManager.
add_executable(test_metadataproviderregistry
  modules/scraper/test_metadataproviderregistry.cpp
)
target_link_libraries(test_metadataproviderregistry PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
target_compile_definitions(test_metadataproviderregistry PRIVATE
  APP_VERSION="${PROJECT_VERSION}"
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
)
target_link_libraries(test_httpclient PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME HttpClient COMMAND test_httpclient)

# LauncherProbe tests
add_executable(test_launcherprobe
  utils/fs/test_launcherprobe.cpp
)
target_link_libraries(test_launcherprobe PRIVATE kartend_utils Qt6::Test)
add_test(NAME LauncherProbe COMMAND test_launcherprobe)

# LauncherUtils::launcherPathIssues — drives the items-toolbar warning
# badge tooltip + the launch-gate dialog body (Kartend-w2n0).
add_executable(test_launcherpathissues
  utils/app/test_launcherpathissues.cpp
)
target_link_libraries(test_launcherpathissues PRIVATE kartend_utils Qt6::Test)
add_test(NAME LauncherPathIssues COMMAND test_launcherpathissues)

# DatLookup tests (Logiqx + MAME listxml DAT parsers + indexed store)
add_executable(test_datlookup
  modules/dat/test_datlookup.cpp
)
target_link_libraries(test_datlookup PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME DatLookup COMMAND test_datlookup)

# DatCache tests (on-disk sqlite cache for parsed DAT files)
add_executable(test_datcache
  modules/dat/test_datcache.cpp
)
target_link_libraries(test_datcache PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
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
  integration/mocks/mockdatabasemanager.h
)
target_include_directories(test_batchscraperunner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)
target_link_libraries(test_batchscraperunner PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME BatchScrapeRunner COMMAND test_batchscraperunner)

add_executable(test_scraperservice_resume
  modules/scraper/test_scraperservice_resume.cpp
)
target_link_libraries(test_scraperservice_resume PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME ScraperServiceResume COMMAND test_scraperservice_resume)

# ScrapeLogger tests (message-handler tee of kartend.scrape* to scrape.log)
add_executable(test_scrapelogger
  utils/app/test_scrapelogger.cpp
)
target_link_libraries(test_scrapelogger PRIVATE kartend_utils Qt6::Test)
add_test(NAME ScrapeLogger COMMAND test_scrapelogger)

# RomHasher tests (streaming MD5/SHA1 for SS hash-based ROM ID)
add_executable(test_romhasher
  utils/fs/test_romhasher.cpp
)
target_link_libraries(test_romhasher PRIVATE kartend_utils Qt6::Test)
add_test(NAME RomHasher COMMAND test_romhasher)

# RetroArchUtils tests (retroarch.cfg parsing + libretro core discovery)
add_executable(test_retroarchutils
  utils/fs/test_retroarchutils.cpp
)
target_link_libraries(test_retroarchutils PRIVATE kartend_utils Qt6::Test)
add_test(NAME RetroArchUtils COMMAND test_retroarchutils)

# KdeColorScheme tests
# Includes resources.qrc so the test binary's `:/themes/` lookups
# resolve to the same bundled .colors files the production app sees;
# without this, discover_includesBundledSchemes would always fail.
add_executable(test_kdecolorscheme
  utils/view/test_kdecolorscheme.cpp
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
  utils/db/test_smartfilter.cpp
)
target_link_libraries(test_smartfilter PRIVATE kartend_utils Qt6::Test)
add_test(NAME SmartFilter COMMAND test_smartfilter)

# SmartPlaylistEvaluator tests
add_executable(test_smartplaylistevaluator
  utils/db/test_smartplaylistevaluator.cpp
)
target_link_libraries(test_smartplaylistevaluator PRIVATE kartend_utils Qt6::Test)
add_test(NAME SmartPlaylistEvaluator COMMAND test_smartplaylistevaluator)

# UsageStatsStore tests
add_executable(test_usagestatsstore
  utils/db/test_usagestatsstore.cpp
)
target_link_libraries(test_usagestatsstore PRIVATE kartend_utils Qt6::Test)
add_test(NAME UsageStatsStore COMMAND test_usagestatsstore)

# HistoryStore tests
add_executable(test_historystore
  utils/db/test_historystore.cpp
)
target_link_libraries(test_historystore PRIVATE kartend_utils Qt6::Test)
add_test(NAME HistoryStore COMMAND test_historystore)

# VariantGrouping tests (Kartend-skhk). Pure same-basename grouper.
add_executable(test_variantgrouping
  utils/db/test_variantgrouping.cpp
)
target_link_libraries(test_variantgrouping PRIVATE kartend_utils Qt6::Test)
add_test(NAME VariantGrouping COMMAND test_variantgrouping)

# KartPreflight tests (Kartend-fr4z). Pure helpers + report assembly so the
# UI dialog has something to render. Reuses kartsuspiciouspaths.cpp so the
# allowlist branch can be exercised without dragging in KartManager itself.
add_executable(test_kartpreflight
  modules/kart/test_kartpreflight.cpp
)
target_link_libraries(test_kartpreflight PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME KartPreflight COMMAND test_kartpreflight)

# KartManifest tests (JSON round-trip for the .kart packaging format)
add_executable(test_kartmanifest
  modules/kart/test_kartmanifest.cpp
)
target_link_libraries(test_kartmanifest PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME KartManifest COMMAND test_kartmanifest)

# KartReader tests (read + extract + sha256 verification)
add_executable(test_kartreader
  modules/kart/test_kartreader.cpp
)
target_link_libraries(test_kartreader PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
if (TARGET PkgConfig::ZSTD)
  target_link_libraries(test_kartreader PRIVATE PkgConfig::ZSTD)
  target_compile_definitions(test_kartreader PRIVATE KARTEND_HAS_ZSTD=1)
endif()
add_test(NAME KartReader COMMAND test_kartreader)

# KartWriter tests (export collection + zstd compression)
add_executable(test_kartwriter
  modules/kart/test_kartwriter.cpp
)
target_link_libraries(test_kartwriter PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
if (TARGET PkgConfig::ZSTD)
  target_link_libraries(test_kartwriter PRIVATE PkgConfig::ZSTD)
  target_compile_definitions(test_kartwriter PRIVATE KARTEND_HAS_ZSTD=1)
endif()
add_test(NAME KartWriter COMMAND test_kartwriter)

# KartMerge tests (merge policy + metadata persistence)
add_executable(test_kartmerge
  modules/kart/test_kartmerge.cpp
)
target_link_libraries(test_kartmerge PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME KartMerge COMMAND test_kartmerge)

# SearchUtils smoke tests (header-only enum/struct)
add_executable(test_searchutils utils/text/test_searchutils.cpp)
target_include_directories(test_searchutils PRIVATE ${SRC_DIR}/utils)
target_link_libraries(test_searchutils Qt6::Core Qt6::Test)
add_test(NAME SearchUtils COMMAND test_searchutils)

# NavigationHelpers tests (pure helpers extracted)
add_executable(test_navigationhelpers
  modules/navigation/test_navigationhelpers.cpp
)
target_link_libraries(test_navigationhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME NavigationHelpers COMMAND test_navigationhelpers)

# InteractionHelpers tests (pure helpers extracted)
add_executable(test_interactionhelpers
  modules/interaction/test_interactionhelpers.cpp
)
target_link_libraries(test_interactionhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME InteractionHelpers COMMAND test_interactionhelpers)

# EventHelpers tests (pure helpers extracted from EventManager)
add_executable(test_eventhelpers
  modules/event/test_eventhelpers.cpp
)
target_link_libraries(test_eventhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME EventHelpers COMMAND test_eventhelpers)

# SettingsHelpers tests (pure helpers extracted from SettingsManager)
add_executable(test_settingshelpers
  modules/settings/test_settingshelpers.cpp
)
target_link_libraries(test_settingshelpers PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
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
)
target_link_libraries(test_keyboardhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME KeyboardHelpers COMMAND test_keyboardhelpers)

# SearchHelpers tests (pure helpers extracted)
add_executable(test_searchhelpers
  modules/search/test_searchhelpers.cpp
)
target_link_libraries(test_searchhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME SearchHelpers COMMAND test_searchhelpers)

# MouseHelpers tests (pure helpers extracted)
add_executable(test_mousehelpers
  modules/mouse/test_mousehelpers.cpp
)
target_link_libraries(test_mousehelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME MouseHelpers COMMAND test_mousehelpers)

# SelectionHelpers tests (pure helpers extracted)
add_executable(test_selectionhelpers
  modules/selection/test_selectionhelpers.cpp
)
target_link_libraries(test_selectionhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME SelectionHelpers COMMAND test_selectionhelpers)

# ScrollHelpers tests (pure helpers extracted)
add_executable(test_scrollhelpers
  modules/scroll/test_scrollhelpers.cpp
)
target_link_libraries(test_scrollhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME ScrollHelpers COMMAND test_scrollhelpers)

# QueryHelpers tests (pure helpers extracted)
add_executable(test_queryhelpers
  modules/query/test_queryhelpers.cpp
)
target_link_libraries(test_queryhelpers PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME QueryHelpers COMMAND test_queryhelpers)

# FilterHelpers tests (pure helpers extracted)
add_executable(test_filterhelpers
  modules/filter/test_filterhelpers.cpp
)
target_link_libraries(test_filterhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME FilterHelpers COMMAND test_filterhelpers)

# Filter hot-path benchmarks. Labelled "benchmark" so default ctest runs skip
# them — invoke with `ctest -L benchmark` to track regression. Walltime
# numbers vary by hardware; compare against a stored baseline.
add_executable(bench_filterhelpers
  benchmarks/bench_filterhelpers.cpp
)
target_link_libraries(bench_filterhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME BenchFilterHelpers COMMAND bench_filterhelpers)
set_tests_properties(BenchFilterHelpers PROPERTIES LABELS "benchmark")# GamepadHelpers tests (pure helpers extracted from GamepadManager)
add_executable(test_gamepadhelpers
  modules/gamepad/test_gamepadhelpers.cpp
)
target_link_libraries(test_gamepadhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME GamepadHelpers COMMAND test_gamepadhelpers)

# OverlayHelpers tests (pure helpers extracted from SelectionOverlayManager)
add_executable(test_overlayhelpers
  modules/overlay/test_overlayhelpers.cpp
)
target_link_libraries(test_overlayhelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME OverlayHelpers COMMAND test_overlayhelpers)

# AttractHelpers tests (pure helpers extracted from AttractManager)
add_executable(test_attracthelpers
  modules/attract/test_attracthelpers.cpp
)
target_link_libraries(test_attracthelpers PRIVATE kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME AttractHelpers COMMAND test_attracthelpers)

# SelectionRestoreHelpers tests (pure helpers extracted from SelectionRestoreCoordinator)
add_executable(test_selectionrestorehelpers
  modules/restore/test_selectionrestorehelpers.cpp
)
target_link_libraries(test_selectionrestorehelpers PRIVATE kartend_data kartend_api kartend_utils Qt6::Test)
add_test(NAME SelectionRestoreHelpers COMMAND test_selectionrestorehelpers)

# DetailPageHelpers tests (pure helpers extracted from DetailPageManager)
add_executable(test_detailpagehelpers
  modules/detailpage/test_detailpagehelpers.cpp
)
target_link_libraries(test_detailpagehelpers PRIVATE kartend_media kartend_data kartend_chrome kartend_api kartend_utils Qt6::Test)
add_test(NAME DetailPageHelpers COMMAND test_detailpagehelpers)

# Integration test harness for UI-coordinator managers.
# Only built when the per-area OBJECT libs are available (i.e. when
# tests/CMakeLists.txt is added from the top-level CMakeLists.txt rather than
# configured stand-alone). kartend_core is a representative sentinel.
if (TARGET kartend_core)
  add_subdirectory(integration)
endif()
