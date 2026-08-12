# Auto-split fragment of the former tests/CMakeLists.txt monolith
# (Kartend-n1hpy.8). include()'d from tests/CMakeLists.txt in order, so it
# runs in the tests/ directory scope: SOURCES paths stay relative to tests/,
# and the kartend_add_test helper + shared vars defined earlier are in scope.
# Do not add_subdirectory() this file — it is an include() fragment.

# DatAudit engine tests (Catalogue + classify + run + buildCatalogue)
kartend_add_test(NAME DatAuditRunner
  SOURCES modules/dataudit/test_datauditrunner.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAuditDownloadService provenance-seam tests (Kartend-ahf3d stage 1). Real
# in-memory SQLite via the injected accessor; network paths are runtime-gated.
kartend_add_test(NAME DatAuditDownloadService
  SOURCES modules/dataudit/test_datauditdownloadservice.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAuditProfileStore CRUD / snapshot / provenance tests (Kartend-ahf3d stage
# 2). Real on-disk media.db under a sandboxed AppDataLocation, built via the
# production DatabaseSchema::createTables path. No DB mocking.
kartend_add_test(NAME DatAuditProfileStore
  SOURCES modules/dataudit/test_datauditprofilestore.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAuditProfileController profile CRUD/persist tests (Kartend-n1hpy.3). The
# insert-vs-update persist decision + pre-write DatRef metadata refresh, lifted
# off the DatAuditAuditPage QWidget, round-trip through a real sandboxed
# media.db without constructing the widget. No DB mocking.
kartend_add_test(NAME DatAuditProfileController
  SOURCES modules/dataudit/test_datauditprofilecontroller.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAuditRunController off-thread audit-run tests (Kartend-ahf3d stage 3).
# Drives a real audit over a real DAT + scan dir and asserts the finished()
# signal delivers the engine output on the caller's thread. No DB mocking.
kartend_add_test(NAME DatAuditRunController
  SOURCES modules/dataudit/test_datauditruncontroller.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAudit layout probe tests (folder-structure detection, Kartend-m6qsb.6)
kartend_add_test(NAME DatAuditLayout
  SOURCES modules/dataudit/test_datauditlayout.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAudit export tests (CSV + fixdat + miss-list)
kartend_add_test(NAME DatAuditExport
  SOURCES modules/dataudit/test_datauditexport.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAudit fix engine tests (plan + apply + undo over real temp files)
kartend_add_test(NAME DatAuditFix
  SOURCES modules/dataudit/test_datauditfix.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DatAudit results table model (rows / columns / status filter)
kartend_add_test(NAME DatAuditModel
  SOURCES modules/dataudit/test_datauditmodel.cpp
  LINK kartend_data kartend_api kartend_utils
)

# Kartend-34lab: the audit browser models — global tree rollup, game-list
# aggregation + completeness filter, ROM detail catalogue<->result join.
kartend_add_test(NAME DatAuditBrowserModels
  SOURCES modules/dataudit/test_datauditbrowsermodels.cpp
  LINK kartend_data kartend_api kartend_utils
)

# BatchScrapeRunner tests (async batch scrape state machine)
# Compiles scrapepersistence + its db-store dependencies so the runner
# can exercise its `applyScrapedItem` integration path without a real
# QSqlDatabase. Uses the same mock IDatabaseManager from
# tests/integration/mocks/ that test_scrapepersistence does. The write
# worker + databaseschema sources are linked so the runner's lazy-init
# code path (ensureWriteWorkerStarted) resolves at link time even though
# the null-DB tests skip it at runtime.
kartend_add_test(NAME BatchScrapeRunner
  SOURCES modules/scraper/test_batchscraperunner.cpp
          integration/mocks/mockdatabasemanager.h
  LINK kartend_data kartend_api kartend_utils
)
target_include_directories(test_batchscraperunner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

kartend_add_test(NAME ScraperServiceResume
  SOURCES modules/scraper/test_scraperservice_resume.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ScrapeLogger tests (message-handler tee of kartend.scrape* to scrape.log)
kartend_add_test(NAME ScrapeLogger
  SOURCES utils/app/test_scrapelogger.cpp
  LINK kartend_utils
)

# RomHasher tests (streaming MD5/SHA1 for SS hash-based ROM ID)
kartend_add_test(NAME RomHasher
  SOURCES utils/fs/test_romhasher.cpp
  LINK kartend_utils
)

# ArchiveRepack tests (rewrite a zip renaming inner entries; libarchive round-trip)
kartend_add_test(NAME ArchiveRepack
  SOURCES utils/fs/test_archiverepack.cpp
  LINK kartend_utils
)

# ArchiveSafety tests (pre-extraction symlink / path-escape scan)
kartend_add_test(NAME ArchiveSafety
  SOURCES utils/fs/test_archivesafety.cpp
  LINK kartend_utils
)

# RetroArchUtils tests (retroarch.cfg parsing + libretro core discovery)
kartend_add_test(NAME RetroArchUtils
  SOURCES utils/fs/test_retroarchutils.cpp
  LINK kartend_utils
)

# Launcher-import discovery utilities (Kartend-wuq2c): the .kartlink stub
# format plus the three read-only launcher-library readers.
kartend_add_test(NAME KartLink
  SOURCES utils/fs/test_kartlink.cpp
  LINK kartend_utils
)

kartend_add_test(NAME SteamLibrary
  SOURCES utils/fs/test_steamlibrary.cpp
  LINK kartend_utils
)

kartend_add_test(NAME SteamAppInfo
  SOURCES utils/fs/test_steamappinfo.cpp
  LINK kartend_utils
)

kartend_add_test(NAME FlatpakLibrary
  SOURCES utils/fs/test_flatpaklibrary.cpp
  LINK kartend_utils
)

kartend_add_test(NAME LutrisLibrary
  SOURCES utils/fs/test_lutrislibrary.cpp
  LINK kartend_utils
)

# The additional launcher-import sources (Kartend-4cff2): the .desktop parsing
# shared by the Flatpak and XDG scans, the menu scan itself, and the Heroic /
# itch / Bottles readers.
kartend_add_test(NAME DesktopEntry
  SOURCES utils/fs/test_desktopentry.cpp
  LINK kartend_utils
)

kartend_add_test(NAME XdgGamesLibrary
  SOURCES utils/fs/test_xdggameslibrary.cpp
  LINK kartend_utils
)

kartend_add_test(NAME HeroicLibrary
  SOURCES utils/fs/test_heroiclibrary.cpp
  LINK kartend_utils
)

kartend_add_test(NAME ItchLibrary
  SOURCES utils/fs/test_itchlibrary.cpp
  LINK kartend_utils
)

# Kartend-ilkne: EsdeLibrary. The cases encode what a real ES-DE 3.4.1 install
# writes — notably that gamelist.xml is a metadata sidecar rather than the game
# list, which a hand-written fixture would have got backwards.
kartend_add_test(NAME EsdeLibrary
  SOURCES utils/fs/test_esdelibrary.cpp
  LINK kartend_utils
)

kartend_add_test(NAME BottlesLibrary
  SOURCES utils/fs/test_bottleslibrary.cpp
  LINK kartend_utils
)

# KdeColorScheme tests
# Includes resources.qrc so the test binary's `:/themes/` lookups
# resolve to the same bundled .colors files the production app sees;
# without this, discover_includesBundledSchemes would always fail.
kartend_add_test(NAME KdeColorScheme
  SOURCES utils/view/test_kdecolorscheme.cpp
          ${SRC_DIR}/utils/view/kdecolorscheme.cpp
          ${SRC_DIR}/assets/resources.qrc
  LINK Qt6::Core Qt6::Gui
)

# Kartend-a0d6c: SystemThemeWatcher detects a runtime kdeglobals change (KDE
# accent / colour scheme) and emits a debounced themeChanged().
kartend_add_test(NAME SystemThemeWatcher
  SOURCES utils/view/test_systemthemewatcher.cpp
          ${SRC_DIR}/utils/view/systemthemewatcher.cpp
  LINK Qt6::Core
)

# Kartend-q40q0: WCAG contrast arithmetic + the lightness repair applied to
# the audit's text-bearing absolute-lightness derivations (breadcrumb link,
# opt-in title tint, validation-error red). Pure colour math over QColor.
kartend_add_test(NAME ColorContrast
  SOURCES utils/view/test_colorcontrast.cpp
          ${SRC_DIR}/utils/view/colorcontrast.cpp
  LINK Qt6::Core Qt6::Gui
)
target_include_directories(test_colorcontrast PRIVATE
  ${SRC_DIR}/utils/view
)
target_include_directories(test_kdecolorscheme PRIVATE
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/utils/uiconstants
)

# SmartFilter tests
kartend_add_test(NAME SmartFilter
  SOURCES utils/db/test_smartfilter.cpp
  LINK kartend_utils
)

# SmartPlaylistEvaluator tests
kartend_add_test(NAME SmartPlaylistEvaluator
  SOURCES utils/db/test_smartplaylistevaluator.cpp
  LINK kartend_utils
)

# UsageStatsStore tests
kartend_add_test(NAME UsageStatsStore
  SOURCES utils/db/test_usagestatsstore.cpp
  LINK kartend_utils
)

# KartendDb::DbTransaction contract tests against real SQLite (Kartend-c0dwd):
# commit/rollback, nested-guard deferral via a shared depth counter, the
# depth-less always-outermost ctor, and failed-BEGIN reporting.
# MigratedDb fixture self-test (the shared "open a migrated, sandboxed
# SQLite" one-liner for suites; production bootstrap, no DB mocking).
kartend_add_test(NAME MigratedDb
  SOURCES support/test_migrateddb.cpp
  LINK kartend_data kartend_api kartend_utils
)

kartend_add_test(NAME DbTxn
  SOURCES utils/db/test_dbtxn.cpp
  LINK kartend_utils
)

# KartendDb::escapeLike contract + real-SQLite ESCAPE round trip
# (Kartend-joird): underscore/percent in user text must stop acting as
# LIKE wildcards across the search / subfolder / smart-playlist paths.
kartend_add_test(NAME SqlLike
  SOURCES utils/db/test_sqllike.cpp
  LINK kartend_utils
)

# HistoryStore tests
kartend_add_test(NAME HistoryStore
  SOURCES utils/db/test_historystore.cpp
  LINK kartend_utils
)

# StatisticsService tests (Kartend-vy7d7): the off-thread aggregation that backs
# the Statistics dialog. gather() opens its own read-only connection to a db
# FILE, so these run against a real on-disk SQLite database (QTemporaryDir) —
# writer seeds via the migration chain, gather() reads the same file, and the
# returned StatsSnapshot is asserted field-by-field. No DB mocking.
kartend_add_test(NAME StatisticsService
  SOURCES utils/db/test_statisticsservice.cpp
  LINK kartend_utils
)

# BulkEditService tests: the off-thread bulk-edit pipeline behind the bulk-edit
# tool. run() opens its own connection to a db FILE and commits every row write
# inside one transaction, so like StatisticsService these run against a real
# on-disk SQLite database (QTemporaryDir) — writer seeds via the migration
# chain, run() writes the same file, and the Outcome + persisted rows +
# cancel-rollback atomicity are asserted. No DB mocking.
kartend_add_test(NAME BulkEditService
  SOURCES utils/db/test_bulkeditservice.cpp
  LINK kartend_utils
)

# ArtworkWizardService tests: the off-thread prep behind the artwork wizard.
# prepare() opens its own read-only connection to a db FILE and snapshots the
# artwork directory, so like BulkEditService these run against a real on-disk
# SQLite database (QTemporaryDir) — a writer seeds the items table, prepare()
# reads the same file, and the queue / directory snapshot / dbOk are asserted.
# No DB mocking.
kartend_add_test(NAME ArtworkWizardService
  SOURCES utils/db/test_artworkwizardservice.cpp
  LINK kartend_utils
)

# VariantGrouping tests (Kartend-skhk). Pure same-basename grouper.
kartend_add_test(NAME VariantGrouping
  SOURCES utils/db/test_variantgrouping.cpp
  LINK kartend_utils
)

# KartPreflight tests (Kartend-fr4z). Pure helpers + report assembly so the
# UI dialog has something to render. Reuses kartsuspiciouspaths.cpp so the
# allowlist branch can be exercised without dragging in KartManager itself.
kartend_add_test(NAME KartPreflight
  SOURCES modules/kart/test_kartpreflight.cpp
  LINK kartend_data kartend_api kartend_utils
)

# KartManifest tests (JSON round-trip for the .kart packaging format)
kartend_add_test(NAME KartManifest
  SOURCES modules/kart/test_kartmanifest.cpp
  LINK kartend_data kartend_api kartend_utils
)

# KartReader tests (read + extract + sha256 verification)
kartend_add_test(NAME KartReader
  SOURCES modules/kart/test_kartreader.cpp
  LINK kartend_data kartend_api kartend_utils
)
if (TARGET PkgConfig::ZSTD)
  target_link_libraries(test_kartreader PRIVATE PkgConfig::ZSTD)
  target_compile_definitions(test_kartreader PRIVATE KARTEND_HAS_ZSTD=1)
endif()

# KartWriter tests (export collection + zstd compression)
kartend_add_test(NAME KartWriter
  SOURCES modules/kart/test_kartwriter.cpp
  LINK kartend_data kartend_api kartend_utils
)
if (TARGET PkgConfig::ZSTD)
  target_link_libraries(test_kartwriter PRIVATE PkgConfig::ZSTD)
  target_compile_definitions(test_kartwriter PRIVATE KARTEND_HAS_ZSTD=1)
endif()

# KartMerge tests (merge policy + metadata persistence)
kartend_add_test(NAME KartMerge
  SOURCES modules/kart/test_kartmerge.cpp
  LINK kartend_data kartend_api kartend_utils
)

