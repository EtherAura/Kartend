# Auto-split fragment of the former tests/CMakeLists.txt monolith
# (Kartend-n1hpy.8). include()'d from tests/CMakeLists.txt in order, so it
# runs in the tests/ directory scope: SOURCES paths stay relative to tests/,
# and the kartend_add_test helper + shared vars defined earlier are in scope.
# Do not add_subdirectory() this file — it is an include() fragment.

# ScrapeJobGrouping tests (shell-parent scrape routing — Kartend-7dng)
kartend_add_test(NAME ScrapeJobGrouping
  SOURCES modules/scraper/test_scrapejobgrouping.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ConfigValidation tests (schema + cross-collection diagnostics)
kartend_add_test(NAME ConfigValidation
  SOURCES utils/fs/test_configvalidation.cpp
  LINK kartend_utils
)

# KeyboardManager tests (static selection helpers)
# Only the selection-helper TU is linked; it has no dependencies on
# ScrollManager / InteractionStateHolder / etc., keeping the test isolated.
kartend_add_test(NAME KeyboardManager
  SOURCES modules/keyboard/test_keyboardmanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# NavigationStackManager tests (header-only data class powering NavigationManager)
kartend_add_test(NAME NavigationStackManager
  SOURCES modules/navigation/test_navigationstackmanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# DbMigrations tests (sqlite in-memory, no UI)
kartend_add_test(NAME DbMigrations
  SOURCES utils/db/test_dbmigrations.cpp
  LINK kartend_utils
)

# FileHashCache tests (schema v16, in-memory sqlite)
kartend_add_test(NAME FileHashCache
  SOURCES utils/db/test_filehashcache.cpp
  LINK kartend_utils
)

# DatAuditProfile CRUD tests (schema v17, in-memory sqlite)
kartend_add_test(NAME DatAuditProfile
  SOURCES utils/db/test_datauditprofile.cpp
  LINK kartend_utils
)

# DatLibraryState tests (dismissal persistence, Kartend-m6qsb.5)
kartend_add_test(NAME DatLibraryState
  SOURCES utils/db/test_datlibrarystate.cpp
  LINK kartend_utils
)

# ItemMetadata storage round-trip tests
kartend_add_test(NAME ItemMetadata
  SOURCES utils/db/test_itemmetadata.cpp
  LINK kartend_utils
)

# Per-item LRU cache (in-memory; no DB required)
kartend_add_test(NAME ItemMetadataCache
  SOURCES utils/db/test_itemmetadatacache.cpp
  LINK kartend_utils
)

# ItemArtwork storage + standard-type resolution tests
kartend_add_test(NAME ItemArtwork
  SOURCES utils/db/test_itemartwork.cpp
  LINK kartend_utils
)

# Scrape persistence tests
# Compiles the SUT plus itemartwork.cpp (for isStandardType) and the
# inline MockDatabaseManager from tests/integration/mocks. No DB / no
# network — pure file IO + capturing-mock IDatabaseManager.
kartend_add_test(NAME ScrapePersistence
  SOURCES modules/scraper/test_scrapepersistence.cpp
          integration/mocks/mockdatabasemanager.h
  LINK kartend_data kartend_api kartend_utils
)
target_include_directories(test_scrapepersistence PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# Direct (worker-thread) scrape persistence tests
# Exercises Scraper::saveScrapedMetadataDirect against a real :memory:
# SQLite. This is the path BatchScrapeRunner's write worker runs on its
# own thread (Kartend-gnkb); the IDatabaseManager-backed
# saveScrapedMetadata is covered separately by test_scrapepersistence
# above. Both share the private mergeScrapedIntoExisting helper, but the
# direct path adds connection-validity guards and uses ItemMetadataStore /
# ItemArtworkStore directly rather than via the cache-invalidating
# IDatabaseManager facades.
kartend_add_test(NAME ScrapePersistenceDirect
  SOURCES modules/scraper/test_scrapepersistence_direct.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScrapeAssetDedup — pure dedup / CRC-short-circuit helpers extracted from
# ScrapeResultDialog (Kartend-dpehr). Exercises the shared/per-game asset
# probes, local-file hashing, SS hash-hint URL building, and the short-circuit
# sentinel check with no QDialog.
kartend_add_test(NAME ScrapeAssetDedup
  SOURCES modules/scraper/test_scrapeassetdedup.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScraperRetryPolicy — pure transient-classification + backoff math for the
# bounded jeuInfos retry (Kartend-1rtrt). No network: the provider's
# timer-driven re-dispatch consumes these decisions.
kartend_add_test(NAME ScraperRetryPolicy
  SOURCES modules/scraper/test_scraperretrypolicy.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScrapeDownloadDispatcher — non-UI dedup/CRC/async-fetch orchestration
# extracted from ScrapeResultDialog (Kartend-dpehr). Driven by a fake provider,
# no QDialog: covers tally / partial-success skip / hash short-circuit /
# progress cadence / no-provider.
kartend_add_test(NAME ScrapeDownloadDispatcher
  SOURCES modules/scraper/test_scrapedownloaddispatcher.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScrapeWriteWorker connection-lifecycle + cross-thread write tests
# Exercises Scraper::ScrapeWriteWorker (the per-worker QSqlDatabase the
# BatchScrapeRunner write worker drives on its own QThread) against a REAL
# on-disk SQLite file under an isolated QStandardPaths sandbox. Covers that
# a queued performWrite persists the metadata + non-standard artwork rows,
# that a guard-rejected write leaves no partial metadata row, and that the
# worker's connection name is removed on closeConnection AND on destruction
# (no leaked QSqlDatabase handle). No DB mocking (docs/dev/testing.md).
kartend_add_test(NAME ScrapeWriteWorker
  SOURCES modules/scraper/test_scrapewriteworker.cpp
  LINK kartend_data kartend_api kartend_utils
)

# BatchScrapeRunner end-to-end integration with a real DatabaseManager
# Confirms the gnkb wiring (ScrapeWriteWorker on a separate thread + its
# own QSqlDatabase + IDatabaseManager::invalidateMetadataCacheItem hop
# back to the main thread) actually invalidates the metadata cache after
# a write. Without this assertion a regression in onWriteCompleted's
# invalidate call would silently leave the sidebar showing pre-scrape
# stale data and only surface as a user-reported bug. Source list
# mirrors test_databasemanager (real DB stack) plus the scraper sources.
kartend_add_test(NAME BatchScrapeRunnerIntegration
  SOURCES modules/scraper/test_batchscraperunner_integration.cpp
          modules/scraper/stubmetadataprovider.h
  LINK kartend_data kartend_api kartend_utils
)

# MusicBrainz parser tests
kartend_add_test(NAME MusicBrainzParser
  SOURCES modules/scraper/test_musicbrainzparser.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper system cache tests (parser + on-disk round-trip)
kartend_add_test(NAME ScreenScraperSystemCache
  SOURCES modules/scraper/test_screenscrapersystemcache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper systems autodetect tests
kartend_add_test(NAME ScreenScraperSystems
  SOURCES modules/scraper/test_screenscrapersystems.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper parser tests
kartend_add_test(NAME ScreenScraperParser
  SOURCES modules/scraper/test_screenscraperparser.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper quota tracker tests (Kartend-vyz9u): ssuser quota-block parsing
# + no-op-preserves-prior-snapshot behaviour, no HTTP.
kartend_add_test(NAME ScreenScraperQuota
  SOURCES modules/scraper/test_screenscraperquota.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper systems-catalog cache / stale-fallback decision tests
kartend_add_test(NAME ScreenScraperCatalog
  SOURCES modules/scraper/test_screenscrapercatalog.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper media-type catalog cache tests (Kartend-vyz9u): mediasJeuListe
# parse, case-insensitive lookup, and on-disk round-trip.
kartend_add_test(NAME ScreenScraperMediaTypeCache
  SOURCES modules/scraper/test_screenscrapermediatypecache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraper provider request-URL/param construction tests (Kartend-hsboz):
# jeuInfos.php credential placement + system/rom/hash param mapping, no HTTP.
kartend_add_test(NAME ScreenScraperProvider
  SOURCES modules/scraper/test_screenscraperprovider.cpp
  LINK kartend_data kartend_api kartend_utils
)

# TMDB parser tests
kartend_add_test(NAME TmdbParser
  SOURCES modules/scraper/test_tmdbparser.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Open Library parser tests
kartend_add_test(NAME OpenLibraryParser
  SOURCES modules/scraper/test_openlibraryparser.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Provider request-construction tests (searchUrl building + metadata) for
# the API-backed providers and the generic WebSearchProvider. Qt6::Network
# arrives transitively via kartend_utils — the provider ctors touch
# HttpClient::instance() (lazy singleton, no traffic).
kartend_add_test(NAME ProviderRequests
  SOURCES modules/scraper/test_providerrequests.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Provider orchestration tests (Kartend-ym7z3): the request -> parse ->
# callback chain of MusicBrainz / Open Library / TMDB through the
# ProviderBase test fetch seam with canned JSON — request shape, error
# propagation, and guard paths. No network, no QNetworkAccessManager.
kartend_add_test(NAME ProviderOrchestration
  SOURCES modules/scraper/test_providerorchestration.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScreenScraperRegion: No-Intro region heuristic (pure, network-free).
kartend_add_test(NAME ScreenScraperRegion
  SOURCES modules/scraper/test_screenscraperregion.cpp
  LINK kartend_data kartend_api kartend_utils
)

# MetadataProviderRegistry tests
# Must compile the MusicBrainz provider too because the registry's
# builtIn() instantiates one. Qt6::Network is needed because the
# provider's ctor calls HttpClient::instance() (lazy singleton init,
# no network traffic during the test) which #includes QNetworkAccessManager.
kartend_add_test(NAME MetadataProviderRegistry
  SOURCES modules/scraper/test_metadataproviderregistry.cpp
  LINK kartend_data kartend_api kartend_utils
)
target_compile_definitions(test_metadataproviderregistry PRIVATE
  APP_VERSION="${PROJECT_VERSION}"
)

# HttpClient tests (Kartend-zy10 response-size cap). Runs a local
# QTcpServer that floods bytes after a minimal HTTP response header and
# verifies HttpClient aborts the reply once the cap is exceeded. The
# control-case test asserts a small response under the cap is delivered
# in full. Pure Qt::Network — no QHttpServer dependency (which isn't in
# Kartend's required Qt components and isn't guaranteed to be in CI).
kartend_add_test(NAME HttpClient
  SOURCES modules/scraper/test_httpclient.cpp
  LINK kartend_data kartend_api kartend_utils
)

# LauncherProbe tests
kartend_add_test(NAME LauncherProbe
  SOURCES utils/fs/test_launcherprobe.cpp
  LINK kartend_utils
)

# LauncherUtils::launcherPathIssues — drives the items-toolbar warning
# badge tooltip + the launch-gate dialog body (Kartend-w2n0).
kartend_add_test(NAME LauncherPathIssues
  SOURCES utils/app/test_launcherpathissues.cpp
  LINK kartend_utils
)

# DatLookup tests (Logiqx + MAME listxml DAT parsers + indexed store)
kartend_add_test(NAME DatLookup
  SOURCES modules/dat/test_datlookup.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatCache tests (on-disk sqlite cache for parsed DAT files)
kartend_add_test(NAME DatCache
  SOURCES modules/dat/test_datcache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatCollectionMatch tests (pure DAT-to-collection match scoring)
kartend_add_test(NAME DatCollectionMatch
  SOURCES modules/dat/test_datcollectionmatch.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatLibraryScan tests (watched-folder proposals, Kartend-m6qsb.5)
kartend_add_test(NAME DatLibraryScan
  SOURCES modules/dat/test_datlibraryscan.cpp
  LINK kartend_data kartend_api kartend_utils
)

# NoIntroParse tests (DAT-o-MATIC page parsing, Kartend-m6qsb.16)
kartend_add_test(NAME NoIntroParse
  SOURCES modules/dat/test_nointroparse.cpp
  LINK kartend_data kartend_api kartend_utils
)

# NoIntroDownloader tests (pack extraction; network flow is runtime-gated)
kartend_add_test(NAME NoIntroDownloader
  SOURCES modules/dat/test_nointrodownloader.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatPackImport tests (source-agnostic import, Kartend-m6qsb.22)
kartend_add_test(NAME DatPackImport
  SOURCES modules/dat/test_datpackimport.cpp
  LINK kartend_data kartend_api kartend_utils
)

# RedumpParse tests (redump.org systems-index parsing, Kartend-m6qsb.23)
kartend_add_test(NAME RedumpParse
  SOURCES modules/dat/test_redumpparse.cpp
  LINK kartend_data kartend_api kartend_utils
)

