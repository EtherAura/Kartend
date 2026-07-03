# Auto-split fragment of the former tests/CMakeLists.txt monolith
# (Kartend-n1hpy.8). include()'d from tests/CMakeLists.txt in order, so it
# runs in the tests/ directory scope: SOURCES paths stay relative to tests/,
# and the kartend_add_test helper + shared vars defined earlier are in scope.
# Do not add_subdirectory() this file — it is an include() fragment.

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

# ItemMetadataActionController — curation-action orchestration (Kartend audit T-01).
# The mock IDatabaseManager is a Q_OBJECT, so list its header in SOURCES (AUTOMOC
# generates its moc/vtable) — same pattern test_scrapepersistence uses.
kartend_add_test(NAME ItemMetadataActionController
  SOURCES modules/interaction/test_itemmetadataactioncontroller.cpp
          integration/mocks/mockdatabasemanager.h
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_itemmetadataactioncontroller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# ArrowNavigationHandler — arrow-key grid-traversal orchestration wiring
# (Kartend audit 0ta4e). The selection math itself is covered by
# test_keyboardmanager / test_selectionhelpers.
kartend_add_test(NAME ArrowNavigationHandler
  SOURCES modules/interaction/test_arrownavigationhandler.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# EventHelpers tests (pure helpers extracted from EventManager)
kartend_add_test(NAME EventHelpers
  SOURCES modules/event/test_eventhelpers.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# WheelEventHandler — selection-delta math + onAnimationFinished cleanup /
# scrollEnded contract (Kartend audit 4yktu). Pure index arithmetic via the
# shared FakeScrollManager + StubSelectionManager (header-only against the api/
# interfaces); friend-drives the private applySelectionDelta / onAnimationFinished.
kartend_add_test(NAME WheelEventHandler
  SOURCES modules/event/test_wheeleventhandler.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_wheeleventhandler PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# EventManagerMouse — mouse-dispatch signal-emission + parent-chain logic
# (Kartend audit 4yktu): double-click / right / middle click, the wheel modal +
# foreign-window gates, the shared modalInputGateActive() predicate,
# itemWidgetForObject / visualIndexForWidget. The
# MockDatabaseManager is a Q_OBJECT, so list its header in SOURCES so AUTOMOC
# picks it up (same as the ItemMetadataActionController / TitleCountsHelpers
# blocks); needed only for the db-collection-index double-click case.
kartend_add_test(NAME EventManagerMouse
  SOURCES modules/event/test_eventmanagermouse.cpp
          integration/mocks/mockdatabasemanager.h
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_eventmanagermouse PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# HoverScrollHandler — deterministic guard / state surface only (Kartend audit
# 4yktu): handleEvent early gates, button-held-MouseMove skip, non-ItemWidget
# keep-dwell branch, clearPendingScroll reset, destroyed-widget commit guard.
# The dwell -> commit -> continuous-scroll core is cursor/geometry-gated and
# stays in the manual-verification tier.
kartend_add_test(NAME HoverScrollHandler
  SOURCES modules/event/test_hoverscrollhandler.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_hoverscrollhandler PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

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
target_include_directories(test_mousemanager PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

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

# PreSearchStateCache save/restore/discard lifecycle (Kartend audit T-06)
kartend_add_test(NAME PreSearchStateCache
  SOURCES modules/scroll/test_presearchstatecache.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils Qt6::Widgets
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

