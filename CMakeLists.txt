cmake_minimum_required(VERSION 3.20)

project(kartend
  VERSION 0.0.1
  LANGUAGES CXX
)

option(MAINTENANCE "Maintenance build mode (-Werror on)" OFF)
option(USE_PGO "Enable Profile-Guided Optimization" OFF)
option(PGO_GENERATE "Generate PGO profile data" OFF)
option(PGO_USE "Use existing PGO profile data" OFF)
option(PGO_PROFILE_DIR "Directory for PGO profile data" "${CMAKE_BINARY_DIR}/pgo_profiles")
option(ENABLE_SANITIZERS "Enable ASan+UBSan in Debug builds" OFF)
option(ENABLE_CCACHE "Use ccache for faster rebuilds (if available)" ON)

if (ENABLE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if (CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
  else()
    message(STATUS "ccache not found; continuing without it")
  endif()
else()
  message(STATUS "ccache disabled (ENABLE_CCACHE=OFF)")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Ensure UIC can find .ui files referenced as ui_*.h
set(CMAKE_AUTOUIC_SEARCH_PATHS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets
)

# Qt6 required
set(QT_REQUIRED_COMPONENTS Core Gui Widgets Sql Concurrent)
find_package(Qt6 COMPONENTS ${QT_REQUIRED_COMPONENTS} REQUIRED)

# Optional: Qt Gamepad support
find_package(Qt6 COMPONENTS Gamepad QUIET)

# Optional: SDL2 fallback gamepad backend (used when Qt6::Gamepad unavailable)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
  pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (ENABLE_SANITIZERS)
  if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "ENABLE_SANITIZERS is only supported on GCC/Clang")
  else()
    # Debug-only to avoid fighting the aggressive Release flags (LTO, -O3,
    # -fomit-frame-pointer, etc.).
    add_compile_options(
      $<$<CONFIG:Debug>:-fsanitize=address,undefined>
      $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
      $<$<CONFIG:Debug>:-fno-optimize-sibling-calls>
    )
    add_link_options(
      $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
  endif()
endif()

if (NOT DEFINED BUILD_DATE)
  set(BUILD_DATE "unknown")
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SOURCES
  ${SRC_DIR}/core/main.cpp
  ${SRC_DIR}/core/mainwindow.cpp
  ${SRC_DIR}/core/menucontroller.cpp
  ${SRC_DIR}/modules/animation/animationmanager.cpp
  ${SRC_DIR}/modules/application/applicationmanager.cpp
  ${SRC_DIR}/modules/artwork/artworkmanager.cpp
  ${SRC_DIR}/modules/cache/cachemanager.cpp
  ${SRC_DIR}/modules/database/databasemanager.cpp
  ${SRC_DIR}/modules/event/eventmanager.cpp
  ${SRC_DIR}/modules/interaction/interactionmanager.cpp
  ${SRC_DIR}/modules/interaction/arrownavigationhandler.cpp
  ${SRC_DIR}/modules/interaction/interactionstateholder.cpp
  ${SRC_DIR}/modules/gamepad/gamepadmanager.cpp
  ${SRC_DIR}/modules/keyboard/keyboardmanager.cpp
  ${SRC_DIR}/modules/keyboard/alphabeticnavigationhandler.cpp
  ${SRC_DIR}/modules/launch/launchmanager.cpp
  ${SRC_DIR}/modules/mouse/mousemanager.cpp
  ${SRC_DIR}/modules/navigation/navigationmanager.cpp
  ${SRC_DIR}/modules/overlay/selectionoverlaymanager.cpp
  ${SRC_DIR}/modules/overlay/searchloadingoverlay.cpp
  ${SRC_DIR}/modules/query/querymanager.cpp
  ${SRC_DIR}/modules/restore/selectionrestoremanager.cpp
  ${SRC_DIR}/modules/scroll/scrollmanager.cpp
  ${SRC_DIR}/modules/scroll/arrowkeyscrollhelper.cpp
  ${SRC_DIR}/modules/scroll/virtualcontainermanager.cpp
  ${SRC_DIR}/modules/scroll/selectioncoordinator.cpp
  ${SRC_DIR}/modules/scroll/scrolleventhandler.cpp
  ${SRC_DIR}/modules/scroll/gridlayoutcalculator.cpp
  ${SRC_DIR}/modules/scroll/itemwidgetfactory.cpp
  ${SRC_DIR}/modules/scroll/scrolldatamanager.cpp
  ${SRC_DIR}/modules/scroll/presearchstatemanager.cpp
  ${SRC_DIR}/modules/filter/filtermanager.cpp
  ${SRC_DIR}/modules/widgetpool/widgetpoolmanager.cpp
  ${SRC_DIR}/modules/search/searchmanager.cpp
  ${SRC_DIR}/modules/selection/selectionmanager.cpp
  ${SRC_DIR}/modules/session/sessionmanager.cpp
  ${SRC_DIR}/modules/settings/settingsmanager.cpp
  ${SRC_DIR}/modules/sidebar/sidebarmanager.cpp
  ${SRC_DIR}/modules/viewport/viewportmanager.cpp
  ${SRC_DIR}/ui/dialogs/errordialog.cpp
  ${SRC_DIR}/ui/dialogs/settingsdialog.cpp
  ${SRC_DIR}/ui/dialogs/shortcutsdialog.cpp
  ${SRC_DIR}/ui/widgets/metadatasidebar.cpp
  ${SRC_DIR}/ui/widgets/itemwidget.cpp
  ${SRC_DIR}/ui/widgets/loadingoverlay.cpp
  ${SRC_DIR}/ui/widgets/listheaderwidget.cpp
  ${SRC_DIR}/ui/widgets/artworkpreviewoverlay.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/settingsutils.cpp
  ${SRC_DIR}/utils/extensionutils.cpp
  ${SRC_DIR}/utils/timerutils.cpp
  ${SRC_DIR}/utils/collectionutils.cpp
  ${SRC_DIR}/utils/artworkutils.cpp
  ${SRC_DIR}/utils/dbmigrations.cpp
)

set(HEADERS
  ${SRC_DIR}/core/mainwindow.h
  ${SRC_DIR}/core/menucontroller.h
  ${SRC_DIR}/modules/animation/animationmanager.h
  ${SRC_DIR}/modules/application/applicationmanager.h
  ${SRC_DIR}/modules/artwork/artworkmanager.h
  ${SRC_DIR}/modules/cache/cachemanager.h
  ${SRC_DIR}/modules/database/databasemanager.h
  ${SRC_DIR}/modules/event/eventmanager.h
  ${SRC_DIR}/modules/interaction/interactionmanager.h
  ${SRC_DIR}/modules/interaction/arrownavigationhandler.h
  ${SRC_DIR}/modules/interaction/interactionstateholder.h
  ${SRC_DIR}/modules/gamepad/gamepadmanager.h
  ${SRC_DIR}/modules/keyboard/keyboardmanager.h
  ${SRC_DIR}/modules/keyboard/alphabeticnavigationhandler.h
  ${SRC_DIR}/modules/launch/launchmanager.h
  ${SRC_DIR}/modules/mouse/mousemanager.h
  ${SRC_DIR}/modules/navigation/navigationmanager.h
  ${SRC_DIR}/modules/navigation/navigationstackmanager.h
  ${SRC_DIR}/modules/overlay/selectionoverlaymanager.h
  ${SRC_DIR}/modules/overlay/searchloadingoverlay.h
  ${SRC_DIR}/modules/query/querymanager.h
  ${SRC_DIR}/modules/restore/selectionrestoremanager.h
  ${SRC_DIR}/modules/scroll/scrollmanager.h
  ${SRC_DIR}/modules/scroll/arrowkeyscrollhelper.h
  ${SRC_DIR}/modules/scroll/virtualcontainermanager.h
  ${SRC_DIR}/modules/scroll/selectioncoordinator.h
  ${SRC_DIR}/modules/scroll/scrolleventhandler.h
  ${SRC_DIR}/modules/scroll/gridlayoutcalculator.h
  ${SRC_DIR}/modules/scroll/itemwidgetfactory.h
  ${SRC_DIR}/modules/scroll/scrolldatamanager.h
  ${SRC_DIR}/modules/scroll/presearchstatemanager.h
  ${SRC_DIR}/modules/scroll/selectionstatetracker.h
  ${SRC_DIR}/modules/filter/filtermanager.h
  ${SRC_DIR}/modules/widgetpool/widgetpoolmanager.h
  ${SRC_DIR}/modules/search/searchmanager.h
  ${SRC_DIR}/modules/selection/selectionmanager.h
  ${SRC_DIR}/modules/session/sessionmanager.h
  ${SRC_DIR}/modules/settings/settingsmanager.h
  ${SRC_DIR}/modules/sidebar/sidebarmanager.h
  ${SRC_DIR}/modules/viewport/viewportmanager.h
  ${SRC_DIR}/ui/uiconstants.h
  ${SRC_DIR}/ui/dialogs/errordialog.h
  ${SRC_DIR}/ui/dialogs/settingsdialog.h
  ${SRC_DIR}/ui/dialogs/shortcutsdialog.h
  ${SRC_DIR}/ui/widgets/metadatasidebar.h
  ${SRC_DIR}/ui/widgets/itemwidget.h
  ${SRC_DIR}/ui/widgets/loadingoverlay.h
  ${SRC_DIR}/ui/widgets/listheaderwidget.h
  ${SRC_DIR}/ui/widgets/artworkpreviewoverlay.h
  ${SRC_DIR}/utils/collectionutils.h
  ${SRC_DIR}/utils/configvalidation.h
  ${SRC_DIR}/utils/searchutils.h
  ${SRC_DIR}/utils/stringutils.h
  ${SRC_DIR}/utils/settingsutils.h
  ${SRC_DIR}/utils/pathutils.h
  ${SRC_DIR}/utils/gridutils.h
  ${SRC_DIR}/utils/extensionutils.h
  ${SRC_DIR}/utils/timerutils.h
  ${SRC_DIR}/utils/propertyutils.h
  ${SRC_DIR}/utils/errorutils.h
  ${SRC_DIR}/utils/applicationcontext.h
  ${SRC_DIR}/utils/stateutils.h
  ${SRC_DIR}/utils/setuputils.h
  ${SRC_DIR}/utils/artworkutils.h
  ${SRC_DIR}/utils/adaptivebatcher.h
  ${SRC_DIR}/utils/forwarddecl.h
  ${SRC_DIR}/utils/dbmigrations.h
)

set(UI_FILES
  ${SRC_DIR}/core/mainwindow.ui
  ${SRC_DIR}/ui/dialogs/settingsdialog.ui
  ${SRC_DIR}/ui/widgets/metadatasidebar.ui
  ${SRC_DIR}/ui/widgets/itemwidget.ui
)

set(RESOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/resources.qrc
)

add_executable(kartend)
target_sources(kartend PRIVATE
  ${SOURCES}
  ${HEADERS}
  ${UI_FILES}
  ${RESOURCES}
)

target_link_libraries(kartend
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
)

if (Qt6Gamepad_FOUND)
  target_link_libraries(kartend PRIVATE Qt6::Gamepad)
  target_compile_definitions(kartend PRIVATE KARTEND_HAS_QT_GAMEPAD=1)
elseif(TARGET PkgConfig::SDL2)
  target_link_libraries(kartend PRIVATE PkgConfig::SDL2)
  target_compile_definitions(kartend PRIVATE KARTEND_HAS_SDL2_GAMEPAD=1)
  message(STATUS "Qt6::Gamepad not found; using SDL2 gamepad backend")
else()
  message(STATUS "No gamepad backend found (Qt6::Gamepad or SDL2); building without gamepad support")
endif()

target_compile_definitions(kartend
  PRIVATE
    APP_NAME="kartend"
    APP_DISPLAY_NAME="Kartend"
    APP_VERSION="${PROJECT_VERSION}"
    APP_AUTHOR="EtherAura"
    BUILD_DATE="${BUILD_DATE}"
)

target_include_directories(kartend PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${SRC_DIR}
  ${SRC_DIR}/core
  ${SRC_DIR}/modules/animation
  ${SRC_DIR}/modules/application
  ${SRC_DIR}/modules/artwork
  ${SRC_DIR}/modules/cache
  ${SRC_DIR}/modules/database
  ${SRC_DIR}/modules/event
  ${SRC_DIR}/modules/gamepad
  ${SRC_DIR}/modules/interaction
  ${SRC_DIR}/modules/keyboard
  ${SRC_DIR}/modules/launch
  ${SRC_DIR}/modules/mouse
  ${SRC_DIR}/modules/navigation
  ${SRC_DIR}/modules/overlay
  ${SRC_DIR}/modules/query
  ${SRC_DIR}/modules/restore
  ${SRC_DIR}/modules/filter
  ${SRC_DIR}/modules/widgetpool
  ${SRC_DIR}/modules/scroll
  ${SRC_DIR}/modules/search
  ${SRC_DIR}/modules/selection
  ${SRC_DIR}/modules/session
  ${SRC_DIR}/modules/settings
  ${SRC_DIR}/modules/sidebar
  ${SRC_DIR}/modules/viewport
  ${SRC_DIR}/ui
  ${SRC_DIR}/ui/widgets
  ${SRC_DIR}/ui/dialogs
  ${SRC_DIR}/utils
)

target_compile_options(kartend PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-unused-lambda-capture)

# Keep GCC-only intent with Clang analogue on RISC-V
if (CMAKE_SYSTEM_PROCESSOR MATCHES "[Rr][Ii][Ss][Cc][Vv]")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(kartend PRIVATE -mno-direct-extern-access)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(kartend PRIVATE -fno-direct-access-external-data)
  endif()
endif()

# Release flags
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(RELEASE_COMPILE_OPTS -O3 -ffast-math -march=native -flto=auto -fomit-frame-pointer -funroll-loops -fno-signed-zeros -fno-trapping-math -fno-math-errno -fno-semantic-interposition -ftree-vectorize -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -DQT_NO_DEBUG -fmerge-all-constants -fdelete-null-pointer-checks)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(RELEASE_COMPILE_OPTS -O3 -ffast-math -march=native -flto=auto -fomit-frame-pointer -funroll-loops -fno-signed-zeros -fno-trapping-math -fno-math-errno -fno-semantic-interposition -ftree-vectorize -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -DQT_NO_DEBUG -fmerge-all-constants -fdelete-null-pointer-checks)
else()
  set(RELEASE_COMPILE_OPTS -O3 -ffast-math -march=native -flto=auto -fomit-frame-pointer -funroll-loops -fno-signed-zeros -fno-trapping-math -fno-math-errno -fno-semantic-interposition -ftree-vectorize -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -DQT_NO_DEBUG -fmerge-all-constants -fdelete-null-pointer-checks)
endif()
set(RELEASE_LINK_OPTS -fuse-ld=lld -flto=auto -Wl,-O3 -Wl,--gc-sections -Wl,--as-needed -Wl,-z,now -Wl,-z,relro -pie)
target_compile_options(kartend PRIVATE $<$<CONFIG:Release>:${RELEASE_COMPILE_OPTS}>)
target_link_options(kartend PRIVATE $<$<CONFIG:Release>:${RELEASE_LINK_OPTS}>)

# PGO configuration
if(USE_PGO)
    if(PGO_GENERATE)
        message(STATUS "PGO: Generating profile data in ${PGO_PROFILE_DIR}")
        list(APPEND RELEASE_COMPILE_OPTS -fprofile-generate=${PGO_PROFILE_DIR})
        list(APPEND RELEASE_LINK_OPTS -fprofile-generate=${PGO_PROFILE_DIR})
    elseif(PGO_USE)
        message(STATUS "PGO: Using profile data from ${PGO_PROFILE_DIR}")
        list(APPEND RELEASE_COMPILE_OPTS -fprofile-use=${PGO_PROFILE_DIR} -fprofile-correction)
        list(APPEND RELEASE_LINK_OPTS -fprofile-use=${PGO_PROFILE_DIR})
    endif()
endif()

# Strip in Release unless MAINTENANCE
if (NOT MAINTENANCE)
  target_link_options(kartend PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_compile_definitions(kartend PRIVATE $<$<CONFIG:Release>:NDEBUG>)

# Debug-only linker map file output (fixed name)
target_link_options(kartend PRIVATE
  $<$<CONFIG:Debug>:-Wl,-Map,${CMAKE_BINARY_DIR}/../../../.backups/reports/kartend.map>
)

# Install (Linux)
if (UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS kartend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES src/assets/kartend.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES src/assets/icon.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps RENAME kartend.svg)
endif()

# MAINTENANCE adds -Werror
if (MAINTENANCE)
  target_compile_options(kartend PRIVATE -Werror)
endif()

# Tests (optional)
option(BUILD_TESTS "Build unit tests" OFF)
if (BUILD_TESTS)
  # Ensure CTest is enabled at the top-level so `ctest` run from the main build
  # directory discovers tests defined in subdirectories.
  enable_testing()
  add_subdirectory(tests)
endif()