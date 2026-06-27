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

