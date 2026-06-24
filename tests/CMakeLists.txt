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
  ${SRC_DIR}/utils
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)

# ---------------------------------------------------------------------------
# STATIC twins of the per-area OBJECT libs — TEST linking only (Kartend-j0yin).
#
# The app target keeps linking the OBJECT libs directly (Kartend-q3vfq kept
# them OBJECT for layering enforcement; nothing here touches that). For the
# ~160 test executables OBJECT libs are pessimal, though: linking an OBJECT
# lib forces EVERY one of its .o files into the executable — no
# dead-stripping — so each test binary embedded the full object set of every
# linked area (a 13 GB debug tests/ dir, link-dominated edit-test cycles).
#
# Each twin archives the SAME object files ($<TARGET_OBJECTS:...> — objects
# compile exactly once, shared with the app link). A STATIC archive restores
# normal per-member linker semantics: only the object files that resolve a
# referenced symbol are pulled into the test binary.
#
# The INTERFACE link from the twin back to its OBJECT lib forwards the full
# usage surface (include dirs, compile definitions, transitive Qt deps, the
# Release LTO INTERFACE_LINK_OPTIONS on kartend_utils) WITHOUT putting raw
# .o files on consumers' link lines: OBJECT libs reached through
# INTERFACE_LINK_LIBRARIES contribute usage requirements only (verified —
# the consumer's link line contains just the archive).
#
# Qt caveat: an archive member whose only contribution is a static-init side
# effect (qrc resource registration, namespace-scope qRegisterMetaType) is
# dropped when nothing references a symbol in it. AUTOMOC is safe — each
# area's moc TUs land in one mocs_compilation.o whose meta-object symbols are
# referenced by the class TUs that get pulled in. If a future test trips the
# static-init case, link that single test against the OBJECT libs instead
# (kartend_add_test falls back automatically when no _static twin exists).
if (TARGET kartend_utils)
  foreach(_kartend_area IN LISTS KARTEND_AREA_LIBS)
    add_library(${_kartend_area}_static STATIC
      $<TARGET_OBJECTS:${_kartend_area}>)
    target_link_libraries(${_kartend_area}_static INTERFACE ${_kartend_area})
  endforeach()
  # Second pass (all twins exist now): mirror each OBJECT lib's inter-area
  # PUBLIC dependency DAG onto its twin. Without these edges, cross-archive
  # symbols (e.g. kartend_data's querymanagercache.cpp.o referencing
  # lcSearchDiag() from kartend_utils) go unresolved: OBJECT libs reached
  # transitively contribute usage requirements ONLY — never their objects —
  # so the archives were the sole symbol providers but had no declared
  # ordering, and ld's single-pass archive scan dropped late-requested
  # members. With twin→twin edges CMake topologically sorts and repeats the
  # archives on the link line exactly like any static-library DAG.
  foreach(_kartend_area IN LISTS KARTEND_AREA_LIBS)
    get_target_property(_kartend_area_deps ${_kartend_area} INTERFACE_LINK_LIBRARIES)
    if (_kartend_area_deps)
      foreach(_kartend_dep IN LISTS _kartend_area_deps)
        if (_kartend_dep IN_LIST KARTEND_AREA_LIBS)
          target_link_libraries(${_kartend_area}_static INTERFACE ${_kartend_dep}_static)
        endif()
      endforeach()
    endif()
  endforeach()
endif()

# ---------------------------------------------------------------------------
# kartend_add_test(NAME <CTestName> SOURCES <files...> LINK <libs...>)
#
# Collapses the per-test add_executable + target_link_libraries + add_test
# boilerplate. The build target (and binary) name is derived from the first
# SOURCES entry's basename (tests/foo/test_x.cpp -> test_x), which preserves
# every pre-helper target name — irregular extras (set_tests_properties,
# target_include_directories, target_compile_definitions, conditional links)
# keep working as trailing statements referencing that target.
#
# LINK entries naming an area lib are rewritten to the lib's _static twin
# (see above); anything else (Qt6::*, ${KARTEND_AREA_LIBS} expansions) passes
# through. Qt6::Test is always appended — don't list it.
#
# NOTE for greppers: the nightly benchmark workflow derives its target list
# from literal `add_executable(bench_` lines in this file, so the benchmark
# blocks below stay explicit; .scripts/check-test-mapping.py only requires
# each test_*.cpp basename to appear somewhere in a tests/**/CMakeLists.txt,
# which SOURCES entries satisfy.
function(kartend_add_test)
  cmake_parse_arguments(KAT "" "NAME" "SOURCES;LINK" ${ARGN})
  if (NOT KAT_NAME OR NOT KAT_SOURCES)
    message(FATAL_ERROR "kartend_add_test: NAME and SOURCES are required")
  endif()
  if (KAT_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "kartend_add_test(${KAT_NAME}): unexpected args: ${KAT_UNPARSED_ARGUMENTS}")
  endif()
  list(GET KAT_SOURCES 0 _kat_first_source)
  get_filename_component(_kat_target "${_kat_first_source}" NAME_WE)
  add_executable(${_kat_target} ${KAT_SOURCES})
  set(_kat_libs "")
  foreach(_kat_lib IN LISTS KAT_LINK)
    if (TARGET ${_kat_lib}_static)
      list(APPEND _kat_libs ${_kat_lib}_static)
    else()
      list(APPEND _kat_libs ${_kat_lib})
    endif()
  endforeach()
  target_link_libraries(${_kat_target} PRIVATE ${_kat_libs} Qt6::Test)
  add_test(NAME ${KAT_NAME} COMMAND ${_kat_target})
endfunction()

# GridLayoutCalculator tests
# Source lives in src/modules/input/scroll/, compiled into the
# kartend_input OBJECT lib. Linking the area lib (Kartend-oeuf) lets a
# .cpp rename or split land in src/ without us hand-updating this list.
kartend_add_test(NAME GridLayoutCalculator
  SOURCES modules/scroll/test_gridlayoutcalculator.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# VirtualScrollEngine range math (Kartend-tacgk): the pure overscan/clamp band
# arithmetic (VirtualScrollMath::neededIndexBand) extracted from the engine,
# plus the GridLayoutCalculator::getVisibleRowRange edge cases it consumes.
kartend_add_test(NAME VirtualScrollEngine
  SOURCES modules/scroll/test_virtualscrollengine.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScrollDataStore tests (synthetic Home view tile population — Kartend-83iu)
kartend_add_test(NAME ScrollDataStore
  SOURCES modules/scroll/test_scrolldatamanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CoverFlowController view-type gating + null-safe no-op contract (Kartend-kv0wa)
kartend_add_test(NAME CoverFlowController
  SOURCES modules/scroll/test_coverflowcontroller.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ViewportHelpers tests (small-movement threshold, force-immediate combine)
kartend_add_test(NAME ViewportHelpers
  SOURCES modules/viewport/test_viewporthelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# AnimationManager static helper tests (pure functions: duration calc,
# target Y/X computation, visibility scroll target).
kartend_add_test(NAME AnimationManager
  SOURCES modules/animation/test_animationmanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# InteractionStateHolder tests
kartend_add_test(NAME InteractionStateHolder
  SOURCES modules/interaction/test_interactionstateholder.cpp
  LINK kartend_utils
)

# LaunchManager tests (security validation)
kartend_add_test(NAME LaunchManager
  SOURCES modules/launch/test_launchmanager.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# PathUtils tests
kartend_add_test(NAME PathUtils
  SOURCES utils/fs/test_pathutils.cpp
  LINK kartend_utils
)

# VideoUtils tests
kartend_add_test(NAME VideoUtils
  SOURCES utils/view/test_videoutils.cpp
  LINK kartend_utils
)

# ExtensionUtils tests (image-decode allowlist guard — keeps scraped .pdf
# manuals out of QImageReader / Qt's abort()-prone PDF image plugin)
kartend_add_test(NAME ExtensionUtils
  SOURCES utils/fs/test_extensionutils.cpp
  LINK kartend_utils
)

# SearchQueryParser tests (Kartend-9qv2). Pure string -> struct, no DB.
kartend_add_test(NAME SearchQueryParser
  SOURCES utils/text/test_searchqueryparser.cpp
  LINK kartend_utils
)

# CollectionFilesystemWatcher tests (Kartend-kr1g). Pure-helper subdir
# enumeration plus the configure() reconcile path. Uses QTemporaryDir for
# scratch trees so no production paths are touched.
kartend_add_test(NAME CollectionFilesystemWatcher
  SOURCES modules/watcher/test_collectionfilesystemwatcher.cpp
  LINK kartend_data kartend_api kartend_utils
)

# ThemePreset tests (Kartend-y3rg). JSON round-trip + file I/O + apply.
kartend_add_test(NAME ThemePreset
  SOURCES utils/app/test_themepreset.cpp
  LINK kartend_utils
)

# PresentationProfile tests (Kartend-6pp5). JSON round-trip + apply + registry.
kartend_add_test(NAME PresentationProfile
  SOURCES utils/app/test_presentationprofile.cpp
  LINK kartend_utils
)

# CollectionHealth tests (Kartend-lprv). Pure analyzer over fixture rows.
kartend_add_test(NAME CollectionHealth
  SOURCES utils/db/test_collectionhealth.cpp
  LINK kartend_utils
)

# MetadataQueue tests (Kartend-gtly). Pure analyzer over a metadata loader closure.
kartend_add_test(NAME MetadataQueue
  SOURCES utils/db/test_metadataqueue.cpp
  LINK kartend_utils
)

# DirectoryCache contract tests (Kartend-s723v / Kartend-bjrw1): queue-on-cold,
# positive lookup, externally-dropped-file miss-probe, negative caching.
kartend_add_test(NAME DirectoryCache
  SOURCES utils/view/test_directorycache.cpp
  LINK kartend_utils
)

# ArtworkCandidates tests (Kartend-y02i). Pure ranker, no filesystem.
kartend_add_test(NAME ArtworkCandidates
  SOURCES utils/view/test_artworkcandidates.cpp
  LINK kartend_utils
)

# ArtworkCompose tests (Kartend-63wg): composeArtworkCard scale/center/mask.
kartend_add_test(NAME ArtworkCompose
  SOURCES utils/view/test_artworkcompose.cpp
  LINK kartend_utils Qt6::Gui
)

# FuzzyMatch tests (Kartend-581t). Pure subsequence score, no DB.
kartend_add_test(NAME FuzzyMatch
  SOURCES utils/text/test_fuzzymatch.cpp
  LINK kartend_utils
)

# BulkEdit tests (Kartend-hrff). Pure logic over ItemMetadata, no DB.
kartend_add_test(NAME BulkEdit
  SOURCES utils/db/test_bulkedit.cpp
  LINK kartend_utils
)

# PlaceholderWarmer tests
# Links Qt6::Gui so QPixmap (returned from the injected TileFactory) can be
# constructed; QTEST_MAIN spins up QGuiApplication off the back of that.
# Forces the offscreen platform via add_test PROPERTIES so the binary runs
# headless on CI without an X/Wayland session.
kartend_add_test(NAME PlaceholderWarmer
  SOURCES utils/view/test_placeholderwarmer.cpp
  LINK kartend_utils
)
set_tests_properties(PlaceholderWarmer PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# WidgetPoolManager tests
kartend_add_test(NAME WidgetPoolManager
  SOURCES modules/widgetpool/test_widgetpoolmanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# EmptyStateWidget tests
kartend_add_test(NAME EmptyStateWidget
  SOURCES modules/navigation/test_emptystatewidget.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CoverFlowWidget tests (selection clamping, gesture state transitions,
# wheel/key/mouse → signal emission, gallery owner bookkeeping). Pulls in
# VideoPreviewWidget + VideoThumbnailExtractor because the widget's ctor
# constructs the preview child and connects to the extractor singleton —
# those are linked but never asked to play, so the QtMultimedia backend is
# not actually started during the test.
kartend_add_test(NAME CoverFlowWidget
  SOURCES ui/widgets/test_coverflowwidget.cpp
  LINK kartend_chrome kartend_api kartend_utils
)

# Kartend-a0d6c: ItemWidget title-tint follows the system accent
# (QPalette::Highlight) and the placeholder-tile cache invalidates on an
# accent change so a runtime KDE accent shift re-tints missing-artwork tiles.
kartend_add_test(NAME ItemPlaceholderTint
  SOURCES ui/widgets/test_itemplaceholdertint.cpp
  LINK kartend_chrome kartend_api kartend_utils
)

# Kartend-liye: PathStatusGlyph attaches a trailing-action warning glyph to a
# QLineEdit. Test links kartend_ui (for the helper) + kartend_utils (for
# PathStatus + pathStatusDescription).
kartend_add_test(NAME PathStatusGlyph
  SOURCES ui/widgets/test_pathstatusglyph.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-x9mkif.3: DatAuditProfileDialog "Linked collection" picker value
# semantics (preselect / none-default / orphan-preserve / pick / clear).
# Constructed headlessly and queried via profile(); never shown or exec()'d.
kartend_add_test(NAME DatAuditProfileDialog
  SOURCES ui/dialogs/test_datauditprofiledialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-34lab: the browser page must build without crashing (construction-
# order regression: filter checkbox toggled→applyFilters fired mid-build).
kartend_add_test(NAME DatAuditBrowserPage
  SOURCES ui/dialogs/test_datauditbrowserpage.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-m6qsb.18: DAT-library review optional-association combo
kartend_add_test(NAME DatLibraryReviewDialog
  SOURCES ui/dialogs/test_datlibraryreviewdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-m6qsb.20: audit-result status tint mapping
kartend_add_test(NAME DatAuditResultDelegate
  SOURCES ui/dialogs/test_datauditresultdelegate.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-4mqkof: ConfigurationPanel DAT-audit wiring — the "last audited" label
# computes the canonical collection UUID + renders relative time, and the "Open
# DAT Audit…" button routes through the SettingsModel hook with the current
# index. Driven headlessly with stub hooks; the panel is never shown.
kartend_add_test(NAME ConfigurationPanel
  SOURCES ui/dialogs/test_configurationpanel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-ztc64: ScraperCredentialsPanel's non-modal "credentials stored
# unencrypted" banner — hidden by default, shown/cleared via
# setStorageDemotionNotice, and state carries across setProvider rebuilds.
# Driven headlessly; the panel is never shown.
kartend_add_test(NAME ScraperCredentialsPanel
  SOURCES ui/dialogs/test_scrapercredentialspanel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-0eeuk: src/ui/dialogs/item/ logic tests. Each dialog is constructed
# headlessly (never shown or exec()'d) and driven through its public API +
# child widgets; decision/mapping/validation logic only, no pixel assertions.

# StarRatingWidget: setRating clamp contract, change-only signal emission,
# and the click-x → half/full-star value math (events synthesized directly).
kartend_add_test(NAME StarRatingWidget
  SOURCES ui/dialogs/test_starratingwidget.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# BulkEditDialog: dialog-to-store Choice mapping — action combo carries the
# full BulkEdit::Action set as userData, parameter gating via
# actionRequiresParameter, Apply blocked on blank tag, trimmed result().
# (The store half, BulkEdit::applyAction, is covered by utils/db/test_bulkedit.)
kartend_add_test(NAME BulkEditDialog
  SOURCES ui/dialogs/test_bulkeditdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# EditMetadataDialog: EditMetadataPayload round-trip, comma-tag parsing
# (split/trim/drop-empties), URL trimming, rating widget + clear wiring,
# custom-fields add/remove (descending multi-row removal).
kartend_add_test(NAME EditMetadataDialog
  SOURCES ui/dialogs/test_editmetadatadialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# MetadataReviewDialog: queue-cursor decisions — skip advance, cancelled edit
# keeps cursor (no reload), partial save refreshes missing fields in place,
# full save drops the entry, exhaustion disables actions.
kartend_add_test(NAME MetadataReviewDialog
  SOURCES ui/dialogs/test_metadatareviewdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# VariantGroupingDialog: group→tree mapping (UserRole path payloads, leaf
# ".ext" labels), summary aggregation, leaf-only launch/reveal callback gating.
kartend_add_test(NAME VariantGroupingDialog
  SOURCES ui/dialogs/test_variantgroupingdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CollectionHealthDialog: issue-count aggregation across the three report
# categories, sample-overflow "and N more" math, launcher-issue formatting,
# header HTML escaping.
kartend_add_test(NAME CollectionHealthDialog
  SOURCES ui/dialogs/test_collectionhealthdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# LaunchPreviewDialog: display-command quoting/escaping, buildOk fallback,
# exists/MISSING + unresolved-executable branches, archive-extraction line
# visibility, warnings list vs all-clear.
kartend_add_test(NAME LaunchPreviewDialog
  SOURCES ui/dialogs/test_launchpreviewdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ArtworkWizardDialog: candidate-list population from the injected provider,
# empty-state gating, pick-success advance vs pick-failure stay, skip /
# exhaustion handling. Browse (native QFileDialog) is not driven.
kartend_add_test(NAME ArtworkWizardDialog
  SOURCES ui/dialogs/test_artworkwizarddialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-0eeuk (mission 2): src/ui/dialogs/collection/ + kart/ logic tests.
# Same headless pattern as the item/ block above.

# CollectionPickerDialog: parent-link → tree mapping, exclusion reparenting,
# initialChecked → sorted selectedIndices round trip, cascade/partial
# semantics, Select All/None, orphaned-parent fallback to root.
kartend_add_test(NAME CollectionPickerDialog
  SOURCES ui/dialogs/test_collectionpickerdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CollectionRemover: confirm-prompt gating (copy variants), subtree removal +
# parent-index remap, vanished-name scrub, expanded-state restore through the
# old→new map, follow-on selection, last-collection recreate flow. Driven
# against a fake CollectionRemoverHost + in-memory SettingsModel; the modal
# QMessageBox is answered by a zero-interval timer.
kartend_add_test(NAME CollectionRemover
  SOURCES ui/dialogs/test_collectionremover.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CreateCollectionDialog: traversal-safe name gate on OK, type→scraper
# auto-association + manual-pick freeze, conditional ScreenScraper/core rows
# with stale-value guards, accept()-time path security validation.
kartend_add_test(NAME CreateCollectionDialog
  SOURCES ui/dialogs/test_createcollectiondialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CreateSmartPlaylistDialog: kind→Filter construction (limits, extension
# normalisation, days, uuid, trimmed needle), Ok gate on name + required
# parameter, setInitialFilter edit-flow preload.
kartend_add_test(NAME CreateSmartPlaylistDialog
  SOURCES ui/dialogs/test_createsmartplaylistdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ItemArtworkLinksDialog: override-map round trip with blank-drop, auto-path
# fill vs promotion guard, clear action, standard/custom label rendering.
kartend_add_test(NAME ItemArtworkLinksDialog
  SOURCES ui/dialogs/test_itemartworklinksdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# KartMergeDialog: Skip default, Skip/Overwrite accept paths, two-stage
# Merge reveal/commit, full 14-row checkbox→MergePolicy bijection,
# apply-to-all flag.
kartend_add_test(NAME KartMergeDialog
  SOURCES ui/dialogs/test_kartmergedialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# KartPreflightDialog: all-clear banner vs grouped issues tree (per-group
# counts, launcherCheckLabel statuses), summary table facts + HTML escaping.
kartend_add_test(NAME KartPreflightDialog
  SOURCES ui/dialogs/test_kartpreflightdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# KartProgressDialog: fraction clamp, one-shot cancel arm, markFinished
# repurpose of the cancel button into accept-only Close.
kartend_add_test(NAME KartProgressDialog
  SOURCES ui/dialogs/test_kartprogressdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Kartend-0eeuk (mission 3): src/ui/dialogs/scraper/ + launcher/ logic tests.
# Same headless pattern as missions 1-2. ScraperCredentialsPanel is covered
# separately above (Kartend-ztc64); ScrapeJobGrouping already has a full
# suite at modules/scraper/test_scrapejobgrouping.cpp (Kartend-7dng).

# MediaTypeCheckboxBuilder: 33-entry lowercase key set (incl. the synthetic
# _metadata gate), default-on set {_metadata, front}, Select all / none
# bulk toggles.
kartend_add_test(NAME MediaTypeCheckboxBuilder
  SOURCES ui/dialogs/test_mediatypecheckboxbuilder.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# FlowLayout: deterministic wrap math with fixed-size chips —
# heightForWidth row counts, margin inclusion, setGeometry positions,
# count/itemAt/takeAt contract, minimumSize/sizeHint coupling.
kartend_add_test(NAME FlowLayout
  SOURCES ui/dialogs/test_flowlayout.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScraperProvidersDialog: one row per registry provider (id under
# UserRole), credentials-column derivation from scraper.credentials, and
# the Test-query activation rendering searchUrl into the status label.
kartend_add_test(NAME ScraperProvidersDialog
  SOURCES ui/dialogs/test_scraperprovidersdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScraperCredentialsDialog (legacy modal): field composition + echo gating,
# pre-population, Save's trimmed write-through, empty-value key/provider
# cleanup, legacy dev_* scrub. Panel internals covered by Kartend-ztc64.
kartend_add_test(NAME ScraperCredentialsDialog
  SOURCES ui/dialogs/test_scrapercredentialsdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScraperSettingsPanel: silent load() round-trip, preset snapping +
# Custom demotion rules, batch-spin independence, rescrape/hash-mode
# conditional visibility, load() clamping, save() flush.
kartend_add_test(NAME ScraperSettingsPanel
  SOURCES ui/dialogs/test_scrapersettingspanel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScrapeResultSelectionModel: tree-building, check-cascade, and per-item
# selection/dedup logic, exercised with standalone widgets now that the model
# is decoupled from ScrapeResultDialog (Kartend-hhv2u).
kartend_add_test(NAME ScrapeResultSelectionModel
  SOURCES ui/dialogs/test_scraperesultselectionmodel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# GamepadCaptureController: capture state machine against a real
# GamepadManager (binding capture is signal-driven, no SDL hardware needed)
# via a stub IInteractionManager — arm/disarm UI sync, button routing,
# same-target toggle, retarget, null-widget no-ops, missing-backend box.
kartend_add_test(NAME GamepadCaptureController
  SOURCES ui/dialogs/test_gamepadcapturecontroller.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# LauncherChooserDialog: default-index preselection rules, double-click
# accept, and the static choose() mapping (empty list / cancel → -1,
# accept → row at accept time) answered by a zero-interval modal driver.
kartend_add_test(NAME LauncherChooserDialog
  SOURCES ui/dialogs/test_launcherchooserdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# LauncherEditorDialog: launcher() trim mapping, preset fill-and-lock /
# Inline unlock, retroarch-gated core-row visibility, detected-core combo
# from a temp override dir (keeps the host RetroArch install out).
kartend_add_test(NAME LauncherEditorDialog
  SOURCES ui/dialogs/test_launchereditordialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

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
          modules/query/workersignalspy.h
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
          modules/query/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: items.path stores the ABSOLUTE path and items.rel_path the
# media-dir-relative form (Kartend-4te3); the v13 reconcile rewrites pre-v13
# relative-path rows in place. Reuses the same query-stack source list as
# QueryManagerBrokenSymlinks.
kartend_add_test(NAME QueryManagerAbsPath
  SOURCES modules/query/test_querymanager_abspath.cpp
          modules/query/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: same-named files across collections count separately
# under queryIncludeAllCollections / showAllSubcollectionItems (Kartend-oyi2).
# Reuses the same query-stack source list as QueryManagerCancelScan.
kartend_add_test(NAME QueryManagerCrossCollectionCount
  SOURCES modules/query/test_querymanager_cross_collection_count.cpp
          modules/query/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Regression test: a rescan that changes an existing collection's item set must
# invalidate the query worker's sorted-items cache (Kartend-6r4g2).
kartend_add_test(NAME QueryManagerCacheInvalidation
  SOURCES modules/query/test_querymanager_cache_invalidation.cpp
          modules/query/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# QueryManagerCrossCollectionTokens (Kartend-jb6d). Locks in that the
# structured-token filters apply when queryIncludeAllCollections=true so a
# future refactor of the cross-mode SQL path can't quietly drop the
# WHERE clauses.
kartend_add_test(NAME QueryManagerCrossCollectionTokens
  SOURCES modules/query/test_querymanager_cross_collection_tokens.cpp
          modules/query/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

kartend_add_test(NAME QueryManagerShellCollectionSort
  SOURCES modules/query/test_querymanager_shell_collection_sort.cpp
          modules/query/workersignalspy.h
  LINK kartend_data kartend_api kartend_utils
)

# Large-collection paging test (Kartend-1yev5): fetchItemsRange ordering and
# page boundaries at 50k seeded rows, where OFFSET / sorted-cache regressions
# that are invisible at small N would surface.
kartend_add_test(NAME QueryManagerFetchRangePaging
  SOURCES modules/query/test_querymanager_fetchrange_paging.cpp
          modules/query/workersignalspy.h
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
if(NOT WIN32)
  # Dies at startup on the Windows CI leg (0.06s, no output — before the
  # QtTest banner) and its subject is POSIX canonical-path/symlink
  # semantics; QFile::link on Windows makes .lnk shortcuts, not symlinks.
  # Root-causing + porting is Kartend-mhgzq.
  kartend_add_test(NAME QueryManagerCanonicalCache
    SOURCES modules/query/test_querymanager_canonicalcache.cpp
    LINK kartend_data kartend_api kartend_utils
  )
endif()

# DatabaseManager lifecycle / threading tests
# Reuses the same query-stack source list as QueryManagerCancelScan and adds
# the databasemanager TUs. Validates worker-thread shutdown, SQL connection
# cleanup, and thread-safe path-resolution helpers.
kartend_add_test(NAME DatabaseManager
  SOURCES modules/database/test_databasemanager.cpp
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
  LINK Qt6::Core Qt6::Widgets
)
target_include_directories(test_collectionutils_alignment PRIVATE
  ${_kartend_collectionutils_test_includes}
)

# ancestorIndexChain + hierarchyCache + descendant walks + applyCollectionRemoval
kartend_add_test(NAME CollectionUtilsAncestry
  SOURCES utils/app/test_collectionutils_ancestry.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets
)
target_include_directories(test_collectionutils_ancestry PRIVATE
  ${_kartend_collectionutils_test_includes}
)

# wouldCreateCircularReference (corrupt-data + degenerate-cycle handling)
kartend_add_test(NAME CollectionUtilsCycles
  SOURCES utils/app/test_collectionutils_cycles.cpp
          ${_kartend_collectionutils_test_sources}
  LINK Qt6::Core Qt6::Widgets
)
target_include_directories(test_collectionutils_cycles PRIVATE
  ${_kartend_collectionutils_test_includes}
)

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

# DatAudit engine tests (Catalogue + classify + run + buildCatalogue)
kartend_add_test(NAME DatAuditRunner
  SOURCES modules/dataudit/test_datauditrunner.cpp
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

# RetroArchUtils tests (retroarch.cfg parsing + libretro core discovery)
kartend_add_test(NAME RetroArchUtils
  SOURCES utils/fs/test_retroarchutils.cpp
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

# SearchUtils smoke tests (header-only enum/struct)
kartend_add_test(NAME SearchUtils
  SOURCES utils/text/test_searchutils.cpp
  LINK Qt6::Core
)
target_include_directories(test_searchutils PRIVATE ${SRC_DIR}/utils)

# NavigationHelpers tests (pure helpers extracted)
kartend_add_test(NAME NavigationHelpers
  SOURCES modules/navigation/test_navigationhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# InteractionHelpers tests (pure helpers extracted)
kartend_add_test(NAME InteractionHelpers
  SOURCES modules/interaction/test_interactionhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# EventHelpers tests (pure helpers extracted from EventManager)
kartend_add_test(NAME EventHelpers
  SOURCES modules/event/test_eventhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SettingsHelpers tests (pure helpers extracted from SettingsManager)
kartend_add_test(NAME SettingsHelpers
  SOURCES modules/settings/test_settingshelpers.cpp
  LINK kartend_data kartend_api kartend_utils
)

# CollectionHierarchyBuilder — pure parent-linking algorithm from the INI
# section hash (Kartend-93vuk). No SettingsManager: roots-first ordering,
# parent/grandchild path resolution, orphan drop.
kartend_add_test(NAME CollectionHierarchyBuilder
  SOURCES modules/settings/test_collectionhierarchybuilder.cpp
  LINK kartend_data kartend_api kartend_utils
)

# CollectionConfig::operator== field-coverage test (Kartend-bztw8). Pure value
# type lives in kartend_utils; guards against a future field being added to the
# struct but forgotten in operator== (breaks settings-dialog dirty-tracking).
kartend_add_test(NAME CollectionConfigEquality
  SOURCES modules/settings/test_collectionconfigequality.cpp
  LINK kartend_utils
)

# Per-cluster qHash field-coverage test (Kartend-lc58a). The save/diff baseline
# fingerprints each leaf cluster instead of keeping full CollectionConfig copies;
# this guards against a field added to a cluster's operator== but forgotten in
# its qHash (a silently-dropped edit). Pure value types — links kartend_utils.
kartend_add_test(NAME CollectionDiffFingerprint
  SOURCES modules/settings/test_collectiondifffingerprint.cpp
  LINK kartend_utils
)

# SettingsManager migration / schemaVersion tests (Kartend-39g9). Links
# against the full kartend object-lib aggregate because SettingsManager
# pulls in a wide dependency footprint (path/error/config-validation utils,
# manager interfaces). Fixtures live alongside the test source so each
# build picks them up via the KARTEND_TEST_FIXTURES_DIR compile define.
kartend_add_test(NAME SettingsMigration
  SOURCES modules/settings/test_settingsmigration.cpp
  LINK ${KARTEND_AREA_LIBS} Qt6::Core Qt6::Gui Qt6::Sql Qt6::Widgets
)
target_compile_definitions(test_settingsmigration PRIVATE
  KARTEND_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/modules/settings/fixtures"
)
target_include_directories(test_settingsmigration PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)

# Strict round-trip preservation of unknown per-collection INI keys.
# Guards against the version-skew data-loss class where a newer build's
# key is silently dropped when an older build loads + saves the same file.
kartend_add_test(NAME SettingsRoundtrip
  SOURCES modules/settings/test_settingsroundtrip.cpp
  LINK ${KARTEND_AREA_LIBS} Qt6::Core Qt6::Gui Qt6::Sql Qt6::Widgets
)
target_include_directories(test_settingsroundtrip PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)

# Per-domain *Changed signals from saveCollections — emit-on-diff
# coverage. Pairs with the contract in src/api/isettingsmanager.h.
kartend_add_test(NAME PerDomainSignals
  SOURCES modules/settings/test_perdomainsignals.cpp
  LINK ${KARTEND_AREA_LIBS} Qt6::Core Qt6::Gui Qt6::Sql Qt6::Widgets
)
target_include_directories(test_perdomainsignals PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)

# Kartend-ztc64: credential-storage demotion flag — INI round trip of the
# [Scrapers]/credentialDemotionReason marker, the credential-walk skip for
# slash-less meta keys, the plaintext (demoted/legacy) load path, and the
# clear-on-clean-save + credentialStorageDemotionChanged emission contract.
kartend_add_test(NAME CredentialDemotion
  SOURCES modules/settings/test_credentialdemotion.cpp
  LINK ${KARTEND_AREA_LIBS} Qt6::Core Qt6::Gui Qt6::Sql Qt6::Widgets
)
target_include_directories(test_credentialdemotion PRIVATE
  ${CMAKE_BINARY_DIR}/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_core_autogen/include
  ${CMAKE_BINARY_DIR}/kartend_ui_autogen/include
)

# KeyboardHelpers tests (pure helpers extracted from KeyboardManager)
kartend_add_test(NAME KeyboardHelpers
  SOURCES modules/keyboard/test_keyboardhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SearchHelpers tests (pure helpers extracted)
kartend_add_test(NAME SearchHelpers
  SOURCES modules/search/test_searchhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# MouseHelpers tests (pure helpers extracted)
kartend_add_test(NAME MouseHelpers
  SOURCES modules/mouse/test_mousehelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# MouseManager unit tests (Kartend-ior0a): hold-scroll state machine, click-
# hold timers (driven via friend access, no real waits), wheel-step
# computation over synthesized QWheelEvents, and the static widget-finding
# helpers against real ItemWidgets under the offscreen QPA.
kartend_add_test(NAME MouseManager
  SOURCES modules/mouse/test_mousemanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SelectionHelpers tests (pure helpers extracted)
kartend_add_test(NAME SelectionHelpers
  SOURCES modules/selection/test_selectionhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScrollHelpers tests (pure helpers extracted)
kartend_add_test(NAME ScrollHelpers
  SOURCES modules/scroll/test_scrollhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ArrowKeyScrollHelper tests (pure centering math extracted from
# calculateCenterTarget into the centerTargetFor static)
kartend_add_test(NAME ArrowKeyScrollHelper
  SOURCES modules/scroll/test_arrowkeyscrollhelper.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SelectionCoordinator — scroll-module selection state + movement analysis
# (Kartend-93vuk). Widget-free: pure analyzeMovement, setSelectedIndex
# emit/direction, callback-injected rectForIndex, reset.
kartend_add_test(NAME SelectionCoordinator
  SOURCES modules/scroll/test_selectioncoordinator.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# TextZoom tests (process-wide zoom math: setPercent/primeFromSettings clamping,
# zoomedFontSize identity/scale/floor)
kartend_add_test(NAME TextZoom
  SOURCES utils/view/test_textzoom.cpp
  LINK kartend_utils
)

# QueryHelpers tests (pure helpers extracted)
kartend_add_test(NAME QueryHelpers
  SOURCES modules/query/test_queryhelpers.cpp
  LINK kartend_data kartend_api kartend_utils
)

# FilterHelpers tests (pure helpers extracted)
kartend_add_test(NAME FilterHelpers
  SOURCES modules/filter/test_filterhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# Filter hot-path benchmarks. Labelled "benchmark" so a regression run can
# target them with `ctest -L benchmark`. NOTE: a LABELS property alone does NOT
# exclude a test from the default set, so a plain `ctest` still runs this
# (~2.6s, harmless); pass `ctest -LE benchmark` to skip it (Kartend-egd3a).
# Walltime numbers vary by hardware; compare against a stored baseline.
#
# Benchmark blocks stay as explicit add_executable() (not kartend_add_test):
# .github/workflows/nightly-tsan.yml greps `add_executable(bench_` from this
# file to derive its build-target list. They still link the _static twins.
add_executable(bench_filterhelpers
  benchmarks/bench_filterhelpers.cpp
)
target_link_libraries(bench_filterhelpers PRIVATE kartend_input_static kartend_data_static kartend_chrome_static kartend_api_static kartend_utils_static Qt6::Test)
add_test(NAME BenchFilterHelpers COMMAND bench_filterhelpers)
set_tests_properties(BenchFilterHelpers PROPERTIES LABELS "benchmark")

# Per-scroll-frame band math benchmark.
add_executable(bench_virtualscrollmath
  benchmarks/bench_virtualscrollmath.cpp
)
target_link_libraries(bench_virtualscrollmath PRIVATE kartend_input_static kartend_data_static kartend_chrome_static kartend_api_static kartend_utils_static Qt6::Test)
add_test(NAME BenchVirtualScrollMath COMMAND bench_virtualscrollmath)
set_tests_properties(BenchVirtualScrollMath PROPERTIES LABELS "benchmark")

# Per-keystroke command-palette fuzzy-scoring benchmark.
add_executable(bench_fuzzymatch
  benchmarks/bench_fuzzymatch.cpp
)
target_link_libraries(bench_fuzzymatch PRIVATE kartend_utils_static Qt6::Test)
add_test(NAME BenchFuzzyMatch COMMAND bench_fuzzymatch)
set_tests_properties(BenchFuzzyMatch PROPERTIES LABELS "benchmark")

# Artwork path-resolution benchmarks (Kartend-w9s8e): per-tile scroll cost of
# ArtworkUtils::findArtworkForFile / findArtworkForFileCached against a
# synthetic 2000-file temp-dir artwork tree (hit/miss, warm/cold cache).
add_executable(bench_artworkresolve
  benchmarks/bench_artworkresolve.cpp
)
target_link_libraries(bench_artworkresolve PRIVATE kartend_utils_static Qt6::Test)
add_test(NAME BenchArtworkResolve COMMAND bench_artworkresolve)
set_tests_properties(BenchArtworkResolve PROPERTIES LABELS "benchmark")

# Collection-load sort benchmarks (Kartend-w9s8e):
# QueryManagerInternal::sortFiles over 10k synthetic paths, name modes plus
# the map-backed Date/Size fast path (Kartend-m9r1s).
add_executable(bench_sortfiles
  benchmarks/bench_sortfiles.cpp
)
target_link_libraries(bench_sortfiles PRIVATE kartend_data_static kartend_api_static kartend_utils_static Qt6::Test)
add_test(NAME BenchSortFiles COMMAND bench_sortfiles)
set_tests_properties(BenchSortFiles PROPERTIES LABELS "benchmark")

# GamepadHelpers tests (pure helpers extracted from GamepadManager)
kartend_add_test(NAME GamepadHelpers
  SOURCES modules/gamepad/test_gamepadhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# GamepadManager tests (input-translation layer: button dispatch, binding
# capture, suspend gating, keyboard-repeat handshake — Kartend-npoh2)
kartend_add_test(NAME GamepadManager
  SOURCES modules/gamepad/test_gamepadmanager.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# OverlayHelpers tests (pure helpers extracted from SelectionOverlayManager)
kartend_add_test(NAME OverlayHelpers
  SOURCES modules/overlay/test_overlayhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# AttractHelpers tests (pure helpers extracted from AttractManager)
kartend_add_test(NAME AttractHelpers
  SOURCES modules/attract/test_attracthelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SelectionRestoreHelpers tests (pure helpers extracted from SelectionRestoreCoordinator)
kartend_add_test(NAME SelectionRestoreHelpers
  SOURCES modules/restore/test_selectionrestorehelpers.cpp
  LINK kartend_data kartend_api kartend_utils
)

# DetailPageHelpers tests (pure helpers extracted from DetailPageManager)
kartend_add_test(NAME DetailPageHelpers
  SOURCES modules/detailpage/test_detailpagehelpers.cpp
  LINK kartend_media kartend_data kartend_chrome kartend_api kartend_utils
)

# GridWidthDebouncer — three-stage debounced grid-width pipeline (Kartend-tu2hq,
# first standalone src/core unit test). The class is self-contained (only
# TimerUtils from kartend_utils), so its .cpp is compiled directly rather than
# linking kartend_core, which would drag in the whole MainWindow closure.
kartend_add_test(NAME GridWidthDebouncer
  SOURCES core/test_gridwidthdebouncer.cpp
          ${SRC_DIR}/core/gridwidthdebouncer.cpp
  LINK kartend_utils
)
target_include_directories(test_gridwidthdebouncer PRIVATE ${SRC_DIR}/core)

# TitleCountsHelpers — window-title renderer extracted from MainWindow
# (Kartend-tu2hq). Compiles titlecountshelpers.cpp directly (avoids the
# kartend_core/MainWindow closure) and links the area libs its deps need
# (LoadingOverlay from kartend_input, CollectionUtils/StringUtils from
# kartend_utils). Uses the inline MockDatabaseManager from integration/mocks.
kartend_add_test(NAME TitleCountsHelpers
  SOURCES core/test_titlecountshelpers.cpp
          ${SRC_DIR}/core/titlecountshelpers.cpp
          integration/mocks/mockdatabasemanager.h
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Widgets
)
target_include_directories(test_titlecountshelpers PRIVATE
  ${SRC_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# Integration test harness for UI-coordinator managers.
# Only built when the per-area OBJECT libs are available (i.e. when
# tests/CMakeLists.txt is added from the top-level CMakeLists.txt rather than
# configured stand-alone). kartend_core is a representative sentinel.
if (TARGET kartend_core)
  add_subdirectory(integration)
endif()

# Kartend-kkeur: isolate every test's on-disk state.
#
# Most tests call QStandardPaths::setTestModeEnabled(true), which routes the
# config/data/cache locations under a single shared ~/.qttest tree (derived
# from $HOME). That isolates tests from the real user profile but NOT from
# each other: under `ctest -j` the parallel test processes all write the same
# kartend .ini and SQLite db, so they intermittently clobber each other —
# surfacing as a DatabaseManager subprocess abort or a PerDomainSignals /
# SettingsRoundtrip assertion failure that passes on a serial rerun.
#
# Give each test its own HOME so ~/.qttest resolves to a per-test directory;
# cross-process collisions become impossible while full parallelism is kept.
# This covers tests registered in THIS directory; the integration subdir does
# the same for its own. APPEND (not set) so tests that already set ENVIRONMENT
# (e.g. QT_QPA_PLATFORM=offscreen) keep it.
get_property(_kartend_tests DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS _kartend_tests)
  set(_test_home "${CMAKE_BINARY_DIR}/test-homes/${_test}")
  file(MAKE_DIRECTORY "${_test_home}")
  set_property(TEST ${_test} APPEND PROPERTY ENVIRONMENT "HOME=${_test_home}")
endforeach()
