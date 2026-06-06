# ---------------------------------------------------------------------------
# Per-area OBJECT libraries with compiler-enforced layering DAG.
#
# Each top-level module is its own OBJECT library. Inter-area dependencies
# are declared via target_link_libraries(<area> PUBLIC <dep>) so CMake (and
# AUTOMOC/AUTOUIC ordering) sees the layered DAG explicitly. The PUBLIC
# include surface of each lib publishes only the dirs it owns; consumers
# inherit lower-layer includes through the link DAG, not through a global
# umbrella.
#
# The DAG (top → bottom):
#
#   kartend_core      (executable entry, MainWindow + controllers)
#     └─ kartend_behavior  (ApplicationManager — top orchestrator)
#          └─ kartend_ui   (dialogs, widget panes, DetailsPaneManager)
#               ├─ kartend_input   (NavigationManager, InteractionManager, ...)
#               │    └─ kartend_data  (Database, Settings, Scraper, ...)
#               │         └─ kartend_api (role interfaces)
#               │              └─ kartend_utils (foundation utils + Qt)
#               ├─ kartend_data
#               └─ kartend_chrome  (ItemWidget, CoverFlow — neutral chrome)
#                    └─ kartend_api / kartend_utils
#               (kartend_media sits at the same layer as input — uses chrome,
#                api, data, utils; ui does not depend on media.)
#
# The historical kartend_lib INTERFACE aggregator was deleted alongside
# enforcement; the executable and integration test now link the per-area
# OBJECT libs directly (OBJECT libs don't propagate .o files through
# INTERFACE_LINK_LIBRARIES, so the exe must list each area).
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
# kartend_utils (foundation): utils dirs + Qt + the uiconstants namespace.
#
# src/utils/ is on utils's PUBLIC include path so every layer can resolve
# `#include "uiconstants/<name>.h"` — uiconstants/ holds pure compile-time
# constants used from every layer (timing, colors, animation, navigation,
# etc.) and is a foundation namespace. It now lives under utils/ (relocated
# from ui/ — the last layering exception, dropped). uiconstants/ itself is
# deliberately NOT added to the include path directly; consumers reference
# its headers as `uiconstants/<name>.h` so the prefix keeps the constant
# headers visually distinct from same-named widget/manager headers and
# guards against any future basename collision.
# ---------------------------------------------------------------------------
target_include_directories(kartend_utils PUBLIC
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/src/kartend_utils_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/utils
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/app
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/db
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/fs
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/text
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/threading
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/view
)
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

# ---------------------------------------------------------------------------
# kartend_api: role interfaces (IDatabaseManager, ISettingsManager, ...).
# ---------------------------------------------------------------------------
target_include_directories(kartend_api PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_api_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/api
)
target_link_libraries(kartend_api PUBLIC kartend_utils)

# ---------------------------------------------------------------------------
# kartend_chrome: neutral chrome widgets (ItemWidget, CoverFlow, overlays).
# Sits between utils/api and the higher layers so input/media can include
# its widgets without dragging in ui/.
# ---------------------------------------------------------------------------
target_include_directories(kartend_chrome PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_chrome_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/chrome/items
  ${CMAKE_CURRENT_SOURCE_DIR}/chrome/media
  ${CMAKE_CURRENT_SOURCE_DIR}/chrome/overlays
)
target_link_libraries(kartend_chrome PUBLIC kartend_api kartend_utils)

# ---------------------------------------------------------------------------
# kartend_data: persistence + queries + scraper (DatabaseManager, Settings,
# Scraper providers, Cache, Sessions, Restore, Kart, Playlist).
# ---------------------------------------------------------------------------
target_include_directories(kartend_data PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_data_autogen/include
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
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/data/watcher
)
target_link_libraries(kartend_data PUBLIC kartend_api kartend_utils)
# Conditional secure-credentials backend. When Qt6Keychain isn't available
# at configure time the data layer keeps the legacy plaintext-INI path
# unchanged (KARTEND_HAVE_QTKEYCHAIN is undefined) — see settingsmanager
# scraper-credential read/write code.
if (TARGET Qt6Keychain::Qt6Keychain)
  target_link_libraries(kartend_data PRIVATE Qt6Keychain::Qt6Keychain)
  target_compile_definitions(kartend_data PRIVATE KARTEND_HAVE_QTKEYCHAIN)
endif()

# ---------------------------------------------------------------------------
# kartend_input: navigation + interaction + scroll + selection + gamepad
# + keyboard + mouse + filter + attract + animation + viewport + launch.
# Uses chrome widgets (ItemWidget) and data services (Database, Settings,
# Restore).
# ---------------------------------------------------------------------------
target_include_directories(kartend_input PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_input_autogen/include
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
)
target_link_libraries(kartend_input PUBLIC kartend_data kartend_chrome kartend_api kartend_utils)

# ---------------------------------------------------------------------------
# kartend_media: artwork + detail page. Sits at the same layer as input.
# ---------------------------------------------------------------------------
target_include_directories(kartend_media PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_media_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/media/artwork
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/media/detailpage
)
target_link_libraries(kartend_media PUBLIC kartend_data kartend_chrome kartend_api kartend_utils)

# ---------------------------------------------------------------------------
# kartend_ui: dialogs, widget panes, DetailsPaneManager. Above input/data
# because dialogs (SettingsDialog) reference InteractionManager, etc.
# Includes the controllers/ subdir added in Kartend-uk5z when
# DetailsPaneManager moved out of media/.
# ---------------------------------------------------------------------------
target_include_directories(kartend_ui PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_ui_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/controllers/detailspanemanager
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/collection
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/item
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/kart
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/launcher
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/scraper
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/scraper/result
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings/appearance
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings/artwork
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings/behavior
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings/collections
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings/core
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs/settings/launchers
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets/overlays
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets/panes
)
target_link_libraries(kartend_ui PUBLIC kartend_input kartend_data kartend_chrome kartend_api kartend_utils)

# ---------------------------------------------------------------------------
# kartend_behavior: ApplicationManager. Top orchestration layer below core.
# ---------------------------------------------------------------------------
target_include_directories(kartend_behavior PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_behavior_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/behavior/application
)
target_link_libraries(kartend_behavior PUBLIC
  kartend_ui kartend_media kartend_input kartend_data kartend_chrome kartend_api kartend_utils
)

# ---------------------------------------------------------------------------
# kartend_core: MainWindow + per-area controllers (scrollevents, dbevents,
# scraper, menu). Top of the DAG.
# ---------------------------------------------------------------------------
target_include_directories(kartend_core PUBLIC
  ${CMAKE_BINARY_DIR}/src/kartend_core_autogen/include
  ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(kartend_core PUBLIC
  kartend_behavior kartend_ui kartend_media kartend_input kartend_data kartend_chrome kartend_api kartend_utils
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
