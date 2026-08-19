# Auto-split fragment of the former tests/CMakeLists.txt monolith
# (Kartend-n1hpy.8). include()'d from tests/CMakeLists.txt in order, so it
# runs in the tests/ directory scope: SOURCES paths stay relative to tests/,
# and the kartend_add_test helper + shared vars defined earlier are in scope.
# Do not add_subdirectory() this file — it is an include() fragment.

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

# CmdExeQuoting tests: the two-layer cmd.exe/CommandLineToArgvW escaping the
# Windows batch-launcher spawn path uses. Pure string transform, exercised on
# every platform.
kartend_add_test(NAME CmdExeQuoting
  SOURCES modules/launch/test_cmdexequoting.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# LaunchCommandBuilder tests: the pure command-construction half split out of
# launchmanager.cpp (boundary-aware %1/%f and %core substitution, %collection%
# expansion, preset resolution feeding the builder, append-media-path
# fallback). No process is spawned; LaunchManager's delegating statics stay
# covered by the LaunchManager test above.
kartend_add_test(NAME LaunchCommandBuilder
  SOURCES modules/launch/test_launchcommandbuilder.cpp
  LINK kartend_input kartend_data kartend_chrome kartend_api kartend_utils
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

# DetailsFormat tests (header-only pure formatters for the Details section /
# detail page; DetailsPane & DetailPageOverlay keep thin wrappers over these)
kartend_add_test(NAME DetailsFormat
  SOURCES utils/view/test_detailsformat.cpp
  LINK Qt6::Core
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

# LauncherManifestWatcher tests (Kartend-5vuqy). Debounce coalescing and the
# re-add-after-vanish reconcile, driven through notifyChangeForTesting so the
# suite never waits on real filesystem event delivery.
kartend_add_test(NAME LauncherManifestWatcher
  SOURCES modules/watcher/test_launchermanifestwatcher.cpp
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

# SearchPreset tests (Kartend-jklv4). Same registry shape as the profiles
# above: JSON round-trip + apply/snapshot + name-keyed registry helpers.
kartend_add_test(NAME SearchPreset
  SOURCES utils/app/test_searchpreset.cpp
  LINK kartend_utils
)

# CollectionTreeModel tests (Kartend-ob1c9). Pure structure builder for the
# collection tree panel: nesting, alias-parent duplication (DAG), grouped
# playlists, cycle/bounds hardening.
kartend_add_test(NAME CollectionTreeModel
  SOURCES utils/app/test_collectiontreemodel.cpp
  LINK kartend_utils
)

# The desktop titlebar colour feeding the chrome tint (2026-08-18).
kartend_add_test(NAME KdeColorSchemeTitlebar
  SOURCES utils/app/test_kdecolorscheme_titlebar.cpp
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

# ArtworkMirror tests (Kartend-j5amz): mirroredArtworkDirectory, the shared
# decision behind "which directory holds this item's cover" when a collection
# mirrors media subfolders into its artwork folder. Covers the case the grid
# and cover flow both got wrong — an item living OUTSIDE the media directory
# (a generated multi-disc playlist, a canonicalized path behind a symlinked
# media folder), where the relative path is a "../.." chain that resolved
# outside the artwork tree. Pure string work, no filesystem.
kartend_add_test(NAME ArtworkMirror
  SOURCES utils/view/test_artworkmirror.cpp
  LINK kartend_utils
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
# wheel/key/mouse → signal emission, gallery owner bookkeeping, and the
# paint-free center-rect maintenance that positions the video preview).
# Pulls in VideoPreviewWidget + VideoThumbnailExtractor because the widget's
# ctor constructs the preview child and connects to the extractor singleton —
# those are linked (and the preview child is shown by the geometry tests) but
# never asked to play, so the QtMultimedia backend is not actually started
# during the test.
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

# Kartend-e7xte: DetailsPane's file-info rows take the async stat result
# whenever they are showing — the Item tab's unscraped fallback shows the same
# rows the File tab does, and a tab-only gate left them on '…' for good.
# Constructed headlessly and queried by objectName; never shown.
kartend_add_test(NAME DetailsPaneFileInfo
  SOURCES ui/widgets/test_detailspanefileinfo.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
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

# DatAuditProfilePanel: the saved-profile combo + CRUD row lifted off
# DatAuditAuditPage. Selection flows announce profileChanged / unsavedSelected
# against a real sandboxed media.db; constructed headlessly, never shown.
kartend_add_test(NAME DatAuditProfilePanel
  SOURCES ui/dialogs/test_datauditprofilepanel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# DatAuditAuditPage: the DAT Manager's Audit page against a real sandboxed
# media.db — construction inventory (input lists, filter presets, idle run
# controls) and both openForCollection flows (unlinked seed with silent
# "(unsaved)" row; linked-profile adoption with the caller's working
# media-dir/DAT list overriding the saved derivation).
kartend_add_test(NAME DatAuditAuditPage
  SOURCES ui/dialogs/test_datauditauditpage.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# DatAuditDownloadPage: network-free paths only — construction gating
# (No-Intro form up, Redump hidden, download/cancel/progress idle) and the
# Load guard rejecting unparseable system ids without dispatching a fetch.
kartend_add_test(NAME DatAuditDownloadPage
  SOURCES ui/dialogs/test_datauditdownloadpage.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# DatAuditLibraryPage: pure-view contract — placeholder/path label
# round-trip, review/check-updates/import gating setters (busy relabel),
# and every button→intent-signal wire.
kartend_add_test(NAME DatAuditLibraryPage
  SOURCES ui/dialogs/test_datauditlibrarypage.cpp
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

