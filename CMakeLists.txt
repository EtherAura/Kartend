cmake_minimum_required(VERSION 3.20)

project(kartend
  VERSION 0.0.4
  LANGUAGES CXX
)

set(KARTEND_APP_ID "io.github.EtherAura.Kartend")

option(MAINTENANCE "Maintenance build mode (-Werror on)" OFF)
option(USE_PGO "Enable Profile-Guided Optimization" OFF)
option(PGO_GENERATE "Generate PGO profile data" OFF)
option(PGO_USE "Use existing PGO profile data" OFF)
option(PGO_PROFILE_DIR "Directory for PGO profile data" "${CMAKE_BINARY_DIR}/pgo_profiles")
option(ENABLE_SANITIZERS "Enable ASan+UBSan in Debug builds" OFF)
option(ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation in Debug builds" OFF)
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
set(QT_REQUIRED_COMPONENTS Core Gui Widgets Sql Concurrent Multimedia MultimediaWidgets)
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

if (ENABLE_COVERAGE)
  if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "ENABLE_COVERAGE is only supported on GCC/Clang")
  else()
    # Debug-only: full coverage instrumentation requires no optimization
    # to map back to source lines reliably.
    add_compile_options(
      $<$<CONFIG:Debug>:--coverage>
      $<$<CONFIG:Debug>:-fprofile-arcs>
      $<$<CONFIG:Debug>:-ftest-coverage>
      $<$<CONFIG:Debug>:-O0>
      $<$<CONFIG:Debug>:-g>
    )
    add_link_options(
      $<$<CONFIG:Debug>:--coverage>
    )
  endif()
endif()

if (NOT DEFINED BUILD_DATE)
  set(BUILD_DATE "unknown")
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# All compilation of Kartend's source tree happens through this OBJECT library
# so it can be reused by both the main executable and the integration-test
# harness (see tests/integration/CMakeLists.txt). All `target_sources(...)`
# calls under src/*/CMakeLists.txt reference ${KARTEND_LIB_TARGET}.
set(KARTEND_LIB_TARGET kartend_lib)
add_library(${KARTEND_LIB_TARGET} OBJECT)
# OBJECT lib is consumed by both the PIE executable and the test harness, so
# its objects must be position-independent.
set_target_properties(${KARTEND_LIB_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src)

add_executable(kartend ${SRC_DIR}/core/main.cpp)

target_link_libraries(${KARTEND_LIB_TARGET}
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)
target_link_libraries(kartend PRIVATE ${KARTEND_LIB_TARGET})

if (Qt6Gamepad_FOUND)
  target_link_libraries(${KARTEND_LIB_TARGET} PUBLIC Qt6::Gamepad)
  target_compile_definitions(${KARTEND_LIB_TARGET} PUBLIC KARTEND_HAS_QT_GAMEPAD=1)
elseif(TARGET PkgConfig::SDL2)
  target_link_libraries(${KARTEND_LIB_TARGET} PUBLIC PkgConfig::SDL2)
  target_compile_definitions(${KARTEND_LIB_TARGET} PUBLIC KARTEND_HAS_SDL2_GAMEPAD=1)
  message(STATUS "Qt6::Gamepad not found; using SDL2 gamepad backend")
else()
  message(STATUS "No gamepad backend found (Qt6::Gamepad or SDL2); building without gamepad support")
endif()

target_compile_definitions(${KARTEND_LIB_TARGET}
  PUBLIC
    APP_ID="${KARTEND_APP_ID}"
    APP_NAME="kartend"
    APP_DISPLAY_NAME="Kartend"
    APP_VERSION="${PROJECT_VERSION}"
    APP_AUTHOR="EtherAura"
    BUILD_DATE="${BUILD_DATE}"
)

target_include_directories(${KARTEND_LIB_TARGET} PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${SRC_DIR}
  ${SRC_DIR}/core
  ${SRC_DIR}/modules/animation
  ${SRC_DIR}/modules/application
  ${SRC_DIR}/modules/artwork
  ${SRC_DIR}/modules/attract
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

target_compile_options(${KARTEND_LIB_TARGET} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-unused-lambda-capture)

# Keep GCC-only intent with Clang analogue on RISC-V
if (CMAKE_SYSTEM_PROCESSOR MATCHES "[Rr][Ii][Ss][Cc][Vv]")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${KARTEND_LIB_TARGET} PRIVATE -mno-direct-extern-access)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${KARTEND_LIB_TARGET} PRIVATE -fno-direct-access-external-data)
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
set(RELEASE_LINK_OPTS -flto=auto -Wl,-O3 -Wl,--gc-sections -Wl,--as-needed -Wl,-z,now -Wl,-z,relro -pie)
# lld is only compatible with Clang LTO objects; GCC LTO (GIMPLE) requires
# the GNU linker or gold.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(PREPEND RELEASE_LINK_OPTS -fuse-ld=lld)
endif()
target_compile_options(${KARTEND_LIB_TARGET} PRIVATE $<$<CONFIG:Release>:${RELEASE_COMPILE_OPTS}>)
target_link_options(kartend PRIVATE $<$<CONFIG:Release>:${RELEASE_LINK_OPTS}>)

# LTO objects in the OBJECT library are unreadable to a non-LTO link step, so
# every consumer of kartend_lib (the main exe AND the integration test binary)
# must link with the matching LTO and linker flags. Surface the minimum set as
# INTERFACE link options so they propagate automatically.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_link_options(${KARTEND_LIB_TARGET} INTERFACE
    $<$<CONFIG:Release>:-flto=auto>
    $<$<CONFIG:Release>:-fuse-ld=lld>
  )
else()
  target_link_options(${KARTEND_LIB_TARGET} INTERFACE
    $<$<CONFIG:Release>:-flto=auto>
  )
endif()

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

target_compile_definitions(${KARTEND_LIB_TARGET} PRIVATE $<$<CONFIG:Release>:NDEBUG>)

# Debug-only linker map file output (fixed name)
# Only emit when the target directory exists (local dev builds create it via
# build.sh; CI runners do not).
set(_MAP_DIR "${CMAKE_BINARY_DIR}/../../../.backups/reports")
cmake_path(NORMAL_PATH _MAP_DIR)
if (IS_DIRECTORY "${_MAP_DIR}")
  target_link_options(kartend PRIVATE
    $<$<CONFIG:Debug>:-Wl,-Map,${_MAP_DIR}/kartend.map>
  )
endif()

# Install (Linux)
if (UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS kartend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES src/assets/${KARTEND_APP_ID}.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES src/assets/icon.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
          RENAME ${KARTEND_APP_ID}.svg)
  install(FILES src/assets/${KARTEND_APP_ID}.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
endif()

# MAINTENANCE adds -Werror
if (MAINTENANCE)
  target_compile_options(${KARTEND_LIB_TARGET} PRIVATE -Werror)
endif()

# Tests (optional)
option(BUILD_TESTS "Build unit tests" OFF)
if (BUILD_TESTS)
  # Ensure CTest is enabled at the top-level so `ctest` run from the main build
  # directory discovers tests defined in subdirectories.
  enable_testing()
  add_subdirectory(tests)
endif()
