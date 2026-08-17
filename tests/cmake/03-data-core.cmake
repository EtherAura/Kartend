# Auto-split fragment of the former tests/CMakeLists.txt monolith
# (Kartend-n1hpy.8). include()'d from tests/CMakeLists.txt in order, so it
# runs in the tests/ directory scope: SOURCES paths stay relative to tests/,
# and the kartend_add_test helper + shared vars defined earlier are in scope.
# Do not add_subdirectory() this file — it is an include() fragment.

# GridUtils tests
kartend_add_test(NAME GridUtils
  SOURCES utils/view/test_gridutils.cpp
  LINK Qt6::Core Qt6::Gui
)

# CacheManager tests
kartend_add_test(NAME CacheManager
  SOURCES modules/cache/test_cachemanager.cpp
  LINK kartend_data kartend_api kartend_utils
)

# CacheDiskStorage cancellation / drain-with-budget tests (Kartend-3qyih).
# Exercises the cooperative-cancel token + bounded-drain shutdown of the disk
# write pool directly — the path the LSan QThreadPool suppression covers, which
# test_cachemanager only touched indirectly via the in-memory orchestrator.
kartend_add_test(NAME CacheDiskStorage
  SOURCES modules/cache/test_cachediskstorage.cpp
  LINK kartend_data kartend_api kartend_utils
)

# PlaylistManager tests
kartend_add_test(NAME PlaylistManager
  SOURCES modules/playlist/test_playlistmanager.cpp
  LINK kartend_data kartend_api kartend_utils
)

# PlaylistSerializer round-trip + malformed-input tests (Kartend-c0dwd):
# pins the v2 JSON envelope, v1 fallback, and the Extended-M3U dialect.
kartend_add_test(NAME PlaylistSerializer
  SOURCES modules/playlist/test_playlistserializer.cpp
  LINK kartend_data kartend_api kartend_utils
)

# SessionManager tests
kartend_add_test(NAME SessionManager
  SOURCES modules/session/test_sessionmanager.cpp
  LINK kartend_data kartend_api kartend_utils
)

# QueryManager cancellation semantics test
kartend_add_test(NAME QueryManagerCancelScan
  SOURCES modules/query/test_querymanager_cancel_scan.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# items_fts readiness pipeline regression tests (Kartend-4i5e4): the v3
# trigger/backfill interplay let FTS 'delete' ops target never-indexed rows
# (external-content corruption — rescan upserts failed with SQLITE_CORRUPT on
# upgraded installs). Drives the real QueryManager init + ensureItemsFtsReady
# slots against an on-disk SQLite db across upgrade / fresh-install /
# legacy-self-heal flows.
kartend_add_test(NAME QueryManagerFtsBackfill
  SOURCES modules/query/test_querymanager_fts_backfill.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScanWorkController cancel contract (Kartend-t2my8): requestCancel() must
# not drop queued runnables — the scan drain loop balances an inFlight count
# that only the runnables themselves decrement.
kartend_add_test(NAME ScanWorkController
  SOURCES modules/query/test_scanworkcontroller.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScanService persist/prune round-trip tests (Kartend-7vubk): drive
# scan -> saveItemsToDatabase -> prune against a real on-disk SQLite db seeded
# with the production schema, covering insert, deletion-pruning, and the
# cancel-is-a-no-op consistency guarantee.
kartend_add_test(NAME ScanServicePersist
  SOURCES modules/query/test_scanservice_persist.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Multi-disc scan integration (Kartend-3mq7v): the collapse post-pass that
# rewrites the staged rows of a multi-disc release into one .m3u-backed item,
# plus the regenerate/sweep rules when discs come and go and the cleanup when
# the per-collection setting is turned off. Real on-disk SQLite, same
# standalone ScanService wiring as ScanServicePersist above.
kartend_add_test(NAME MultiDiscCollapse
  SOURCES modules/query/test_multidisccollapse.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Scan-side artwork resolution (Kartend-guyc5): items.artwork_path — the column
# behind every DB-side artwork predicate — was never written by either persist
# pipeline, so smart playlists, the has:/missing:artwork tokens, Collection
# Health and the artwork review queue all reported arted items as artless.
# Asserts the stored path equals what the render path resolves for the same
# item, across the lookup cascade and the multi-disc disc-marked fallback.
kartend_add_test(NAME ScanArtwork
  SOURCES modules/query/test_scanartwork.cpp
  LINK kartend_data kartend_api kartend_utils
)

# QueryManager cache generation-token tests (Kartend-z8i0c): the pure
# QueryCacheHash digests that drive count/range cache hit/miss/invalidate.
kartend_add_test(NAME QueryManagerCache
  SOURCES modules/query/test_querymanagercache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: broken symlinks survive a scan/prune cycle (Kartend-9mxh).
# Reuses the same query-stack source list as QueryManagerCancelScan.
kartend_add_test(NAME QueryManagerBrokenSymlinks
  SOURCES modules/query/test_querymanager_broken_symlinks.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Contract test (Kartend-de4ft): a statement-cache hit returns the SAME
# still-prepared statement (finish + positional rebind), a bind-then-bail
# caller cannot poison the next use, and finishAll releases open cursors
# without discarding the compiled statements.
kartend_add_test(NAME PreparedStatementCache
  SOURCES modules/query/test_preparedstatementcache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: items.path stores the ABSOLUTE path and items.rel_path the
# media-dir-relative form (Kartend-4te3); the v13 reconcile rewrites pre-v13
# relative-path rows in place. Reuses the same query-stack source list as
# QueryManagerBrokenSymlinks.
kartend_add_test(NAME QueryManagerAbsPath
  SOURCES modules/query/test_querymanager_abspath.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test (Kartend-h7xnr.11): loadItemDetail's preview-video lookup
# follows the documented two-root chain — the explicit videoDirectory first,
# then {artworkDir}/video/, the single-root layout the scraper writes to.
kartend_add_test(NAME QueryManagerItemDetailVideo
  SOURCES modules/query/test_querymanager_itemdetail_video.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# The badge state-flags read runs on the query worker (Kartend-h7xnr.6):
# fetchItemStateFlagsForCollection sees rows committed on another connection,
# returns only flagged rows scoped to the requested uuid, echoes the uuid for
# stale-reply dropping, and still replies (empty) for an empty uuid.
kartend_add_test(NAME QueryManagerStateFlags
  SOURCES modules/query/test_querymanager_stateflags.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: same-named files across collections count separately
# under queryIncludeAllCollections / showAllSubcollectionItems (Kartend-oyi2).
# Reuses the same query-stack source list as QueryManagerCancelScan.
kartend_add_test(NAME QueryManagerCrossCollectionCount
  SOURCES modules/query/test_querymanager_cross_collection_count.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: a rescan that changes an existing collection's item set must
# invalidate the query worker's sorted-items cache (Kartend-6r4g2).
kartend_add_test(NAME QueryManagerCacheInvalidation
  SOURCES modules/query/test_querymanager_cache_invalidation.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# QueryManagerCrossCollectionTokens (Kartend-jb6d). Locks in that the
# structured-token filters apply when queryIncludeAllCollections=true so a
# future refactor of the cross-mode SQL path can't quietly drop the
# WHERE clauses.
kartend_add_test(NAME QueryManagerCrossCollectionTokens
  SOURCES modules/query/test_querymanager_cross_collection_tokens.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

kartend_add_test(NAME QueryManagerShellCollectionSort
  SOURCES modules/query/test_querymanager_shell_collection_sort.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Large-collection paging test (Kartend-1yev5): fetchItemsRange ordering and
# page boundaries at 50k seeded rows, where OFFSET / sorted-cache regressions
# that are invisible at small N would surface.
kartend_add_test(NAME QueryManagerFetchRangePaging
  SOURCES modules/query/test_querymanager_fetchrange_paging.cpp
          support/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test (Kartend-m9r1s): sortFiles takes optional caller-supplied
# mtime/size maps for the Date/Size sort modes and only stats paths missing
# from them; ordering semantics must stay identical to the historical
# stat-everything path. Also covers buildSortMetadata's re-keying contract.
kartend_add_test(NAME QueryManagerSortFiles
  SOURCES modules/query/test_querymanager_sortfiles.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Regression test (Kartend-4fmu1): the canonical-path dedup cache survives
# across flattened-subcollection loads via the persistent (path, mtime)-keyed
# map on QueryManager; seed/harvest helpers validate by mtime so touched
# files re-resolve while unchanged ones skip the per-item realpath walk.
# Kartend-mhgzq: ported to Windows. The earlier startup-crash no longer
# reproduces (MSVC 2022 / Qt 6.7.3); the only POSIX-specific bit, symlink
# canonicalization, is guarded in-test (harvestRoundTrips QSKIPs when
# QFile::link cannot make a filesystem-resolvable symlink, e.g. Windows .lnk).
kartend_add_test(NAME QueryManagerCanonicalCache
  SOURCES modules/query/test_querymanager_canonicalcache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatabaseManager lifecycle / threading tests
# Reuses the same query-stack source list as QueryManagerCancelScan and adds
# the databasemanager TUs. Validates worker-thread shutdown, SQL connection
# cleanup, and thread-safe path-resolution helpers.
kartend_add_test(NAME DatabaseManager
  SOURCES modules/database/test_databasemanager.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatabaseManager failure injection: unopenable media.db (directory / 0000
# perms), garbage-bytes media.db (lazy open, first-statement NOTADB, no
# destructive rewrite), and the clearCollection throw branch driven by
# dropping the items table through an inspector connection (transaction
# rollback proof). Real SQLite; the injections poison the file/schema only.
kartend_add_test(NAME DatabaseManagerFailures
  SOURCES modules/database/test_databasemanager_failures.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Regression test (Kartend-ardm7): FileMapCache::resolveRelativeFilePath
# resolves leaf-name / media-dir-relative entries from the reverse index
# built on items load instead of linear-scanning the caller's fileNames map
# per unresolved entry (which made widget creation / subcollection filtering
# O(items^2)).
kartend_add_test(NAME FileMapCacheResolve
  SOURCES modules/database/test_filemapcache.cpp
  LINK kartend_data kartend_api kartend_utils
)

# CachedCountsService: stale-completion drop via monotonic generation token.
# Small surface — only needs the service TU, the session interface header,
# and collectionutils for CollectionConfig.
kartend_add_test(NAME CachedCountsService
  SOURCES modules/database/test_cachedcountsservice.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ArtworkManager tests (cancellation, dedup, QPointer race)
kartend_add_test(NAME ArtworkManager
  SOURCES modules/artwork/test_artworkmanager.cpp
  LINK kartend_media kartend_data kartend_chrome kartend_api kartend_utils
)

# Viewport scheduler math (Kartend-nzjxm): the prioritization-band statics
# extracted from viewportartworkscheduler.cpp's anonymous namespace —
# determineBatchSize, partitionByViewport (geometry banding), computeViewports.
kartend_add_test(NAME ViewportSchedulerMath
  SOURCES modules/artwork/test_viewportschedulermath.cpp
  LINK kartend_media kartend_data kartend_chrome kartend_api kartend_utils Qt6::Widgets
)

# ArtworkWidgetRegistry tests (enqueuePending coalesce + kMaxPending cap)
kartend_add_test(NAME ArtworkWidgetRegistry
  SOURCES modules/artwork/test_artworkwidgetregistry.cpp
  LINK kartend_media kartend_data kartend_chrome kartend_api kartend_utils
)

# ArtworkPathCatalog tests (collection-tree walk + cyclic-parent guard, Kartend-tqg3r)
kartend_add_test(NAME ArtworkPathCatalog
  SOURCES modules/artwork/test_artworkpathcatalog.cpp
  LINK kartend_media kartend_data kartend_chrome kartend_api kartend_utils
)

# Silent-load gating math: the cooldown gate + idle-aware batch-size pickers
# extracted from artworksilentloading.cpp as pure statics for direct testing.
kartend_add_test(NAME SilentLoadGating
  SOURCES modules/artwork/test_silentloadgating.cpp
  LINK kartend_media kartend_data kartend_chrome kartend_api kartend_utils
)

# StringUtils tests (header-only)
kartend_add_test(NAME StringUtils
  SOURCES utils/text/test_stringutils.cpp
  LINK Qt6::Core
)

# CliArgs tests (--collection startup override + --import-kart/--to
# path sanitization). pathutils.cpp is compiled in because cliargs.cpp
# routes --import-kart / --to through PathUtils::expandAndValidateCliPath.
kartend_add_test(NAME CliArgs
  SOURCES utils/app/test_cliargs.cpp
  LINK kartend_utils
)

# FFmpeg hardware-decode backend policy (Kartend-0vnvo): keeps Qt off the
# `vulkan` decode backend, which leaks a DRM render-node fd + Mesa worker
# thread per QMediaPlayer::setSource().
kartend_add_test(NAME MediaBackendConfig
  SOURCES utils/app/test_mediabackendconfig.cpp
  LINK kartend_utils
)

# Threading utils (Kartend-qsujk): AdaptiveBatcher's timing -> batch-size
# adaptation, and ThreadPoolUtils::shutdownWithBudget's leak-vs-delete branch
# (the UAF-avoiding path from Kartend-7vrx). Pure logic + a bounded blocking
# task; no Qt GUI needed.
kartend_add_test(NAME ThreadingUtils
  SOURCES utils/threading/test_threadingutils.cpp
  LINK kartend_utils
)

# CollectionConfig::operator== field coverage (guards against a new field being
# added to the struct but forgotten in operator==).
kartend_add_test(NAME CollectionConfig
  SOURCES utils/app/test_collectionconfig.cpp
  LINK kartend_utils
)

# SettingsUtils export/import atomic copy/replace round-trip (Kartend-g2ox).
kartend_add_test(NAME SettingsUtils
  SOURCES utils/app/test_settingsutils.cpp
  LINK kartend_utils
)

# GeneralSettings equality + save-time normalization — the contract the
# settings dialog's whole-struct dirty-check depends on (Kartend-6oqat).
kartend_add_test(NAME GeneralSettingsEquality
  SOURCES utils/app/test_generalsettingsequality.cpp
  LINK kartend_utils
)

# Field-level save->load round-trip tests for every per-domain settings
# persistence unit in src/utils/app/collection (Kartend-dwxuc). One slot per
# domain: every persisted field set non-default, saved to a fresh INI, loaded
# back, compared via the struct's operator== — catches asymmetric key typos
# and save/load default drift that silently lose user settings.
kartend_add_test(NAME SettingsPersistenceRoundtrip
  SOURCES utils/app/test_settingspersistence_roundtrip.cpp
  LINK kartend_utils
)

# TitleFilter tests (per-collection regex title cleanup)
kartend_add_test(NAME TitleFilter
  SOURCES utils/text/test_titlefilter.cpp
  LINK kartend_utils
)
# concurrentApplyAndRebuildIsRaceFree spins 4 reader + 1 writer hammer threads;
# under ctest -jN oversubscription on few-core CI runners (observed on macOS
# Release) they get descheduled and miss the 60s post-stop join watchdog, after
# which the stack writer QThread destructs while still running and aborts the
# process ("Subprocess aborted"). RUN_SERIAL runs this test with no co-scheduled
# binaries so the hammers get the whole runner and drain in ~1s (it passes alone
# in <1s), removing the oversubscription without loosening the race-freedom
# check or the watchdog.
set_tests_properties(TitleFilter PROPERTIES RUN_SERIAL TRUE)

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
  # typehelpers' resolveCollectionTileArtwork (Kartend-ob1c9.1) delegates its
  # parent-dir fallback to ArtworkUtils, which pulls the three TUs below.
  ${SRC_DIR}/utils/view/artworkutils.cpp
  ${SRC_DIR}/utils/fs/extensionutils.cpp
  ${SRC_DIR}/utils/fs/multidisc.cpp
  ${SRC_DIR}/utils/app/loggingcategories.cpp
)
set(_kartend_collectionutils_test_includes
  ${SRC_DIR}/utils
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)

# alignment + viewtype + index validation + categorization + identity + context
kartend_add_test(NAME CollectionUtilsAlignment
  SOURCES utils/app/test_collectionutils_alignment.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets Qt6::Concurrent
)
target_include_directories(test_collectionutils_alignment PRIVATE
  ${_kartend_collectionutils_test_includes}
)

# computeCollectionUuid(CollectionConfig&) overload + indexForUuid / findByUuid
# (Kartend audit D-07)
kartend_add_test(NAME CollectionUtilsUuid
  SOURCES utils/app/test_collectionutils_uuid.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets Qt6::Concurrent
)
target_include_directories(test_collectionutils_uuid PRIVATE
  ${_kartend_collectionutils_test_includes}
)

# ancestorIndexChain + hierarchyCache + descendant walks + applyCollectionRemoval
kartend_add_test(NAME CollectionUtilsAncestry
  SOURCES utils/app/test_collectionutils_ancestry.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets Qt6::Concurrent
)
target_include_directories(test_collectionutils_ancestry PRIVATE
  ${_kartend_collectionutils_test_includes}
)

# wouldCreateCircularReference (corrupt-data + degenerate-cycle handling)
kartend_add_test(NAME CollectionUtilsCycles
  SOURCES utils/app/test_collectionutils_cycles.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets Qt6::Concurrent
)
target_include_directories(test_collectionutils_cycles PRIVATE
  ${_kartend_collectionutils_test_includes}
)

# resolvedCollectionIcon — the shared collectionIcon seam (Kartend-dkh90)
# Qt6::Concurrent: pulled in by artworkutils.cpp (see the shared source list).
kartend_add_test(NAME CollectionUtilsIcon
  SOURCES utils/app/test_collectionutils_icon.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets Qt6::Concurrent
)
target_include_directories(test_collectionutils_icon PRIVATE
  ${_kartend_collectionutils_test_includes}
)

