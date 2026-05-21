# ---------------------------------------------------------------------------
# Per-area OBJECT libraries.
#
# Splits the prior monolithic kartend_lib OBJECT library into one library
# per top-level module. The split delivers:
#
#   - **Build parallelism**: areas compile in parallel; touching a file in
#     module X rebuilds only that area's .o files.
#   - **AUTOMOC scope per area**: each lib's autogen directory contains only
#     its own moc_*.cpp / ui_*.h artifacts, so module ownership of generated
#     code matches the source layout.
#   - **Architectural documentation**: the per-area target_link_libraries
#     declarations below name the *intended* layering even where the
#     codebase still has legacy upward edges (modules/* -> ui/* via
#     detailspane.h, settingsdialog.h, applysettingsdialog.h, etc.).
#
# Hard target_link_libraries layering enforcement is deferred: today the
# codebase has roughly two dozen modules/*->ui/* includes that would form
# CMake-level cycles once both directions are wired. Removing those edges
# requires extracting per-widget role interfaces (IDetailsPane, etc.) the
# same way IMainWindow was extracted for ui->core. Until then, every
# per-area lib reaches the full include surface through kartend_utils so the
# build can continue to compile while the structure migrates.
#
# `check-layering.py` (utils/ + widgets/ foundation invariants) remains the
# cheap enforcement guardrail; promoting it to compiler-enforced layering is
# a follow-up to the IDetailsPane / dialog-interface extraction work.
# ---------------------------------------------------------------------------
add_library(kartend_utils OBJECT)
# kartend_api is OBJECT (not INTERFACE) so AUTOMOC runs on the role
# interface headers it owns. At least one (IScrollManager) is a QObject —
# AUTOMOC must produce moc_iscrollmanager.cpp here, otherwise concrete
# implementations like ScrollManager lose their parent's meta-object info.
add_library(kartend_api OBJECT)
add_library(kartend_chrome OBJECT)
add_library(kartend_data OBJECT)
add_library(kartend_input OBJECT)
add_library(kartend_media OBJECT)
add_library(kartend_behavior OBJECT)
add_library(kartend_ui OBJECT)
add_library(kartend_core OBJECT)

# All OBJECT libs are consumed by both the PIE executable and the test
# harness, so their objects must be position-independent.
set_target_properties(
  kartend_utils kartend_api kartend_chrome kartend_data kartend_input
  kartend_media kartend_behavior kartend_ui kartend_core
  PROPERTIES POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# Include surface, Qt linkage, and compile definitions are all published by
# kartend_utils PUBLIC so every per-area lib (which links kartend_utils
# below) inherits them via INTERFACE_INCLUDE_DIRECTORIES /
# INTERFACE_LINK_LIBRARIES propagation. Mirrors the prior monolithic
# KARTEND_LIB_TARGET PUBLIC surface — the structural split preserves
# behaviour while preparing the ground for tighter per-area scoping once
# the residual modules/*->ui/* upward edges are interface-broken.
# ---------------------------------------------------------------------------
# AUTOUIC generates ui_*.h headers under each OBJECT lib's autogen include
# directory (e.g. kartend_chrome_autogen/include/ui_itemwidget.h). Cross-lib
# header inclusion — for instance, behavior's applicationmanager.cpp pulling
# artworkmanager.h which transitively #includes itemwidget.h which #includes
# ui_itemwidget.h — needs every autogen dir on the global compile include
# path so the include resolves regardless of which area owns the .ui file.
target_include_directories(kartend_utils PUBLIC
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/src/kartend_utils_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_api_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_chrome_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_data_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_input_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_media_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_behavior_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_ui_autogen/include
  ${CMAKE_BINARY_DIR}/src/kartend_core_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/api
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/behavior/application
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/cache
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/database
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/dat
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/kart
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/playlist
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/query
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/restore
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/scraper/core
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/scraper/parsers
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/scraper/providers
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/session
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/animation
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/attract
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/event
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/filter
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/gamepad
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/interaction
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/keyboard
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/launch
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/mouse
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/navigation
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/overlay
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/scroll
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/search
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/selection
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/viewport
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/widgetpool
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/media/artwork
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/media/detailpage
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/media/detailspane
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/collection
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/kart
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/launcher
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/scraper
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings
  # NOTE: src/ui/uiconstants/ is intentionally NOT on the include path.
  # Several uiconstants sub-headers (detailspane.h, gamepad.h, navigation.h,
  # etc.) collide by basename with widget / dialog / manager headers, and
  # the uiconstants files are referenced as `uiconstants/<name>.h` from
  # src/ui/uiconstants.h. Keeping the dir off the search path preserves the
  # single-source resolution for the colliding basenames.
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets/overlays
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets/panes
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/app
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/db
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/fs
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/text
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/threading
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/view
  ${CMAKE_CURRENT_SOURCE_DIR}/chrome/items
  ${CMAKE_CURRENT_SOURCE_DIR}/chrome/media
  ${CMAKE_CURRENT_SOURCE_DIR}/chrome/overlays
)

# Qt6 link deps published through kartend_utils PUBLIC — every per-area lib
# below inherits via `target_link_libraries(<area> PUBLIC kartend_utils)`.
target_link_libraries(kartend_utils PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Sql
  Qt6::Concurrent
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::Network
)

# Every other per-area lib inherits the include surface, Qt comps, and
# downstream compile defs by linking kartend_utils. No inter-area
# target_link_libraries below to avoid forming a CMake cycle with the
# residual modules/*->ui/* upward edges (a follow-up issue extracts the
# missing role interfaces so true compiler-enforced layering becomes
# possible).
#
# Visibility is PUBLIC where the lib's public headers re-expose utils types
# (e.g. interface signatures take a PathUtils result, a manager header
# references a SmartFilter, a settings panel embeds a SettingsHelper); those
# headers force downstream consumers to see the utils include surface. The
# libs whose public headers only include Qt + autogen headers are linked
# PRIVATE so utils stays out of their consumer interface. The aggregator
# INTERFACE-links kartend_utils directly, so the executable and test harness
# still pull the full utils surface either way; PRIVATE here is the
# precondition for tightening per-area target_link_libraries-enforced
# layering once the residual upward edges are interface-broken.
target_link_libraries(kartend_api PUBLIC kartend_utils)
target_link_libraries(kartend_chrome PRIVATE kartend_utils)
target_link_libraries(kartend_data PUBLIC kartend_utils)
target_link_libraries(kartend_input PUBLIC kartend_utils)
target_link_libraries(kartend_media PUBLIC kartend_utils)
target_link_libraries(kartend_behavior PRIVATE kartend_utils)
target_link_libraries(kartend_ui PUBLIC kartend_utils)
target_link_libraries(kartend_core PUBLIC kartend_utils)

# Aggregator wires the per-area OBJECT libs through INTERFACE linkage so
# downstream consumers (the executable and the integration test harness)
# pull every .o file by linking just ${KARTEND_LIB_TARGET}.
target_link_libraries(${KARTEND_LIB_TARGET} INTERFACE
  kartend_utils
  kartend_api
  kartend_chrome
  kartend_data
  kartend_input
  kartend_media
  kartend_behavior
  kartend_ui
  kartend_core
)

# Cross-lib AUTOMOC / AUTOUIC build ordering.
#
# Because we intentionally avoid an inter-area target_link_libraries DAG
# (the residual modules/*->ui/* upward edges would cycle through it), ninja
# doesn't natively know that kartend_core's compile of mainwindow.cpp
# depends on kartend_ui's AUTOUIC having produced ui_detailspane.h, or
# that kartend_input's interactionmanager.cpp needs ui_detailspane.h
# similarly. Declare add_dependencies on the relevant `_autogen` UTILITY
# targets so ninja serializes the autogen phase before the dependent
# compile step. The dependency direction is downward-only (a consumer
# higher in the layered DAG can depend on a producer lower / sibling, but
# never the reverse) to avoid CMake-level cycles.
add_dependencies(kartend_ui kartend_chrome_autogen)
add_dependencies(kartend_data kartend_chrome_autogen)
add_dependencies(kartend_input kartend_chrome_autogen kartend_ui_autogen)
add_dependencies(kartend_media kartend_chrome_autogen kartend_ui_autogen)
add_dependencies(kartend_behavior
  kartend_chrome_autogen
  kartend_ui_autogen
  kartend_input_autogen
  kartend_media_autogen
)
add_dependencies(kartend_core
  kartend_chrome_autogen
  kartend_ui_autogen
  kartend_input_autogen
  kartend_media_autogen
  kartend_behavior_autogen
  kartend_data_autogen
)

# ---------------------------------------------------------------------------
# Recurse into subdirectories — each populates the appropriate area lib via
# target_sources(<area-target> PRIVATE ...).
# ---------------------------------------------------------------------------
add_subdirectory(api)
add_subdirectory(assets)
add_subdirectory(chrome)
add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(ui)
add_subdirectory(utils)
