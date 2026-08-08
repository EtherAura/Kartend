# Auto-split fragment of the former tests/CMakeLists.txt monolith
# (Kartend-n1hpy.8). include()'d from tests/CMakeLists.txt in order, so it
# runs in the tests/ directory scope: SOURCES paths stay relative to tests/,
# and the kartend_add_test helper + shared vars defined earlier are in scope.
# Do not add_subdirectory() this file — it is an include() fragment.

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

# Onboarding wizards (first-run + library onboarding): page sequencing, the
# media-page Next gate (mandatory fields + directory-must-exist), and the
# Finish handlers that fold fields into the Result the caller persists.
kartend_add_test(NAME OnboardingWizards
  SOURCES ui/dialogs/test_onboardingwizards.cpp
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

# ScraperCredentialsDialog (modal wrapper around the shared filterless
# ScraperCredentialsPanel): single-panel embedding, working-copy isolation
# (Cancel discards, Save commits), field composition + echo gating,
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

# SettingsModel::currentWorkingCollection(): the canonical working-row guard
# every settings panel's load()/save() path uses — nullptr for each unwired
# model part / out-of-range index, live row (write-through) when valid.
kartend_add_test(NAME SettingsModel
  SOURCES ui/dialogs/test_settingsmodel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SettingsTreeHelpers: pure tree-mutation/sync logic extracted from the
# SettingsDialog tree partials — per-category appearance/layout copy
# allowlist, mask-aware bulk apply over both lists, subcollection
# inheritance, parent-combo row models (self/cycle vs playlist exclusion),
# duplicate name validation + config scrubbing, alias-name propagation, and
# the post-drop reparent walk (real offscreen QTreeWidget).
kartend_add_test(NAME SettingsDialogTreeHelpers
  SOURCES ui/dialogs/test_settingsdialogtreehelpers.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# TreeManager: the SettingsDialog tree controller against a real (never
# shown) CollectionTreeWidget + fake SettingsTreeHost — rebuild hierarchy /
# index maps, stale-parent orphan repair, linked-appearance mirrors, ancestor
# expansion, rename commit (lists + mirrors + aliases + dirty flag, blank
# revert, rename-back), selection-switch guard, drop resync + signal.
kartend_add_test(NAME TreeManager
  SOURCES ui/dialogs/test_treemanager.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SettingsDialog dirty tracking: one representative field per
# check*Changes bucket feeding hasUnsavedChanges() (spacing mapping, stubbed
# extension/dimension/background buckets via their absorbing panels, tree
# rename, parent combo, colors/effects, list mode, default-launcher index,
# general-settings whole-struct compare), each with a revert-to-clean pin.
kartend_add_test(NAME SettingsDialogChecks
  SOURCES ui/dialogs/test_settingsdialogchecks.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SettingsDialog browse partial: the recursive content-import pipeline on
# real QTemporaryDir trees (guard rails, subcollection creation + template
# inheritance + artwork matching, decline path, traversal-unsafe name skip);
# modals answered by a zero-interval timer queue. Native file pickers
# (browseLauncher/Core/MediaDir) are not driven — platform dialogs.
kartend_add_test(NAME SettingsDialogBrowse
  SOURCES ui/dialogs/test_settingsdialogbrowse.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScrapeResultSelectionModel: tree-building, check-cascade, and per-item
# selection/dedup logic, exercised with standalone widgets now that the model
# is decoupled from ScrapeResultDialog (Kartend-hhv2u).
kartend_add_test(NAME ScrapeResultSelectionModel
  SOURCES ui/dialogs/test_scraperesultselectionmodel.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ScrapeResultDialogUnified runtime, driven against a real ScraperService
# with scripted stub providers: Scrape-click selection → CollectionJob
# translation (uuid / artwork-dir re-resolution, media-filter +
# write-metadata derivation), the interactive pickerNeeded candidate combo +
# live-metadata rendering, the Apply hop (applyResult callback, _metadata
# stripping, queue advance to unifiedScrapeFinished), the error-details
# modal (zero-interval driver) with its re-scrape-failed re-queue grouped by
# owning collection, and the quota-exhausted finish message. Rebuilt queues
# are asserted via the service's synchronous pending-scrape.json snapshot.
kartend_add_test(NAME ScrapeResultDialogUnified
  SOURCES ui/dialogs/test_scraperesultdialogunified.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# SingleItemScrapeView: candidate-list rendering (subtitle/score label
# format, single-result auto-hide), the detail fetch lifecycle
# (loading/loaded/failed signals, per-row cache, stale-callback guard for
# superseded rows), detail-HTML field rows + escaping, and the media
# checkbox rows with shared-scope hints. Driven standalone with a stub
# provider — no dialog, no network.
kartend_add_test(NAME SingleItemScrapeView
  SOURCES ui/dialogs/test_singleitemview.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# TransferRateFormat: the shared bytes/elapsed → "KiB/s"/"MiB/s" readout used
# by both scrape result dialogs (header-only, resolved via the directory-level
# include fallback).
kartend_add_test(NAME TransferRateFormat
  SOURCES ui/dialogs/test_transferrateformat.cpp
  LINK Qt6::Core
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

# LauncherImportDialog: the Steam scope selector (Kartend-el5st) — per-scope
# counts on each row, default tick following the selected scope, and an
# explicit user tick surviving a scope change.
kartend_add_test(NAME LauncherImportDialog
  SOURCES ui/dialogs/test_launcherimportdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# LauncherEditorDialog: launcher() trim mapping, preset fill-and-lock /
# Inline unlock, retroarch-gated core-row visibility, detected-core combo
# from a temp override dir (keeps the host RetroArch install out).
kartend_add_test(NAME LauncherEditorDialog
  SOURCES ui/dialogs/test_launchereditordialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ErrorDialog: severity → window-title mapping, per-error-code guidance
# suffix, details/copy gating on context payload, Show/Hide Details toggle,
# clipboard payload, and showCriticalError's Continue/Quit rewiring + exec
# result mapping (zero-interval modal driver).
kartend_add_test(NAME ErrorDialog
  SOURCES ui/dialogs/test_errordialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ShortcutsDialog: section rendering, keybinding/gamepad/modifier text
# sourced from a fake IMainWindow host's GeneralSettings, the
# useHomeView-gated row, and the showEvent repopulate (no duplication).
kartend_add_test(NAME ShortcutsDialog
  SOURCES ui/dialogs/test_shortcutsdialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# CommandPaletteDialog: caller-order empty-query listing, fuzzy filter +
# rank ordering, category rendering, search-field arrow-key forwarding, and
# the Enter contract (accept first, run the original-index command).
kartend_add_test(NAME CommandPaletteDialog
  SOURCES ui/dialogs/test_commandpalettedialog.cpp
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# StatisticsDialog shell against a real on-disk migrated SQLite file (the
# service half is covered by utils/db/test_statisticsservice): async
# snapshot→tree rendering, uuid→label mapping incl. the deleted fallback,
# navigate payload stash, live history toggle persist, and the Reset flow
# (confirm modal + real SQL + refresh). The IDatabaseManager double only
# names the db path — gather() reads real SQLite.
kartend_add_test(NAME StatisticsDialog
  SOURCES ui/dialogs/test_statisticsdialog.cpp
          integration/mocks/mockdatabasemanager.h
          integration/mocks/mocksettingsmanager.h
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_statisticsdialog PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# SettingsDialog live-save debounce (base color / fonts / splash panels):
# an edit burst applies in-memory immediately but coalesces into ONE
# GeneralSettings disk write; Cancel re-persists the baseline and the
# pending debounce never fires; Save/OK's full save supersedes the flush.
# Driven with a QWidget+IMainWindow fake host and a counting
# ISettingsManager injected via ApplicationContext.
kartend_add_test(NAME SettingsDialogLiveSave
  SOURCES ui/dialogs/test_settingsdialoglivesave.cpp
          integration/mocks/mocksettingsmanager.h
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_settingsdialoglivesave PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# SettingsDialog unsaved-changes resolution for general settings: OK+Discard
# must persist the dialog-open baseline (not the discarded edits), Save must
# persist the edits on both close paths — including reject with no collection
# selected, where handleSaveCollection can't carry the general-settings save.
# Same harness shape as SettingsDialogLiveSave (fake IMainWindow host,
# counting ISettingsManager, zero-interval modal driver).
kartend_add_test(NAME SettingsDialogDiscard
  SOURCES ui/dialogs/test_settingsdialogdiscard.cpp
          integration/mocks/mocksettingsmanager.h
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_settingsdialogdiscard PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

# SettingsDialogController post-dialog reconciliation: mid-session saves
# (persistent Save button / drag-drop reparent / recursive import) persist
# immediately, so a later Cancel must still run uuid migration + orphan purge
# for what was persisted — and a cancelled session with no save must stay a
# pure no-op. Driven through the createSettingsDialog factory seam with a
# scripted ISettingsDialog fake and a recording IDatabaseManager double.
kartend_add_test(NAME SettingsDialogController
  SOURCES ui/dialogs/test_settingsdialogcontroller.cpp
          integration/mocks/mockdatabasemanager.h
          integration/mocks/mocksettingsmanager.h
  LINK kartend_ui kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)
target_include_directories(test_settingsdialogcontroller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/integration/mocks)

