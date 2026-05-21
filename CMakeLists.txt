cmake_minimum_required(VERSION 3.20)

# Project version is sourced from the top-level VERSION file (single source of
# truth across CMakeLists.txt, PKGBUILD, ebuild filename, AppStream metainfo,
# and Flatpak yaml). See .scripts/bump-version.sh and release.yml's
# 'Verify tag matches version' step.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" KARTEND_VERSION_RAW)
string(STRIP "${KARTEND_VERSION_RAW}" KARTEND_VERSION)
if (NOT KARTEND_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "VERSION file contains '${KARTEND_VERSION}' — expected a semver MAJOR.MINOR.PATCH triple.")
endif()

project(kartend
  VERSION ${KARTEND_VERSION}
  LANGUAGES CXX
)

set(KARTEND_APP_ID "io.github.EtherAura.Kartend")

# All project options are KARTEND_-prefixed to namespace them away from the
# CMake/3rd-party global option space.
option(KARTEND_MAINTENANCE "Maintenance build mode (-Werror on)" OFF)
option(KARTEND_BUILD_TESTS "Build unit tests" OFF)
option(KARTEND_ENABLE_CCACHE "Use ccache for faster rebuilds (if available)" ON)
option(KARTEND_ENABLE_SANITIZERS "Enable ASan+UBSan in Debug builds" OFF)
# TSan is mutually exclusive with ASan (the runtime libraries conflict). Keep
# them as separate options and FATAL_ERROR if both are requested.
option(KARTEND_ENABLE_TSAN "Enable ThreadSanitizer in Debug builds" OFF)
option(KARTEND_ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation in Debug builds" OFF)
option(KARTEND_USE_PGO "Enable Profile-Guided Optimization" OFF)
option(KARTEND_PGO_GENERATE "Generate PGO profile data" OFF)
option(KARTEND_PGO_USE "Use existing PGO profile data" OFF)
set(KARTEND_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo_profiles" CACHE PATH "Directory for PGO profile data")
# Distro/portable releases: keep release-class hardening + LTO without
# -march=native or -O3/fast-math. Set this when packaging (PKGBUILD, ebuild,
# Flatpak) so binaries run on every host CPU of the target arch.
option(KARTEND_PORTABLE_RELEASE "Use portable release flags (no -march=native, no -O3)" OFF)
# Linker map files: opt-in. Used by .scripts/build.sh which exports the map
# under .backups/reports during local debug runs.
option(KARTEND_LINKER_MAP "Emit a linker .map file in Debug builds" OFF)
# Optional backend toggles. Default ON (auto-detect); distros that want to
# guarantee a particular backend can flip the unwanted one OFF.
option(KARTEND_ENABLE_QT_GAMEPAD "Allow auto-detecting Qt6::Gamepad" ON)
option(KARTEND_ENABLE_SDL2_GAMEPAD "Allow auto-detecting SDL2 gamepad fallback" ON)
option(KARTEND_ENABLE_ZSTD "Allow auto-detecting libzstd for kart compression" ON)

if (KARTEND_ENABLE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if (CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
  else()
    message(STATUS "ccache not found; continuing without it")
  endif()
else()
  message(STATUS "ccache disabled (KARTEND_ENABLE_CCACHE=OFF)")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Ensure UIC can find .ui files referenced as ui_*.h
set(CMAKE_AUTOUIC_SEARCH_PATHS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets/panes
  ${CMAKE_CURRENT_SOURCE_DIR}/src/chrome/items
)

# Qt6 required. 6.4 is the floor: project uses QTimeZone::utc() which was
# added in 6.4 (see kartend's usagestats module).
set(KARTEND_MIN_QT_VERSION 6.4)
set(QT_REQUIRED_COMPONENTS Core Gui Widgets Sql Concurrent Multimedia MultimediaWidgets Network LinguistTools)
find_package(Qt6 ${KARTEND_MIN_QT_VERSION} COMPONENTS ${QT_REQUIRED_COMPONENTS} REQUIRED)

# Optional: Qt Gamepad support
if (KARTEND_ENABLE_QT_GAMEPAD)
  find_package(Qt6 ${KARTEND_MIN_QT_VERSION} COMPONENTS Gamepad QUIET)
endif()

# Optional: SDL2 fallback gamepad backend (used when Qt6::Gamepad unavailable)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND AND KARTEND_ENABLE_SDL2_GAMEPAD)
  pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
endif()

# Optional: zstd compression for the Kart packaging system.
# Falls back to Qt's built-in qCompress (zlib) when not found.
if (PkgConfig_FOUND AND KARTEND_ENABLE_ZSTD)
  pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
endif()

# Optional: QtKeychain for secure scraper credential storage. Without it,
# the [Scrapers] group is persisted to QSettings as plaintext (the legacy
# behaviour). With it, credential values are stored in the platform
# secret service (GNOME Keyring, KWallet, Windows Credential Manager,
# macOS Keychain) and the INI file holds only the index of which
# provider/field pairs exist. QtKeychain itself ships an
# insecureFallback mode for headless / no-secret-service systems, so the
# runtime degrades gracefully rather than refusing to start.
find_package(Qt6Keychain QUIET)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (KARTEND_ENABLE_SANITIZERS AND KARTEND_ENABLE_TSAN)
  message(FATAL_ERROR
    "KARTEND_ENABLE_SANITIZERS (ASan+UBSan) and KARTEND_ENABLE_TSAN are "
    "mutually exclusive — their runtime libraries conflict. Pick one.")
endif()

if (KARTEND_ENABLE_SANITIZERS)
  if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "KARTEND_ENABLE_SANITIZERS is only supported on GCC/Clang")
  elseif (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR
      "KARTEND_ENABLE_SANITIZERS=ON requires -DCMAKE_BUILD_TYPE=Debug "
      "(got '${CMAKE_BUILD_TYPE}'). The flags are gated on Debug to avoid "
      "fighting Release LTO/-O3/-fomit-frame-pointer.")
  else()
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

if (KARTEND_ENABLE_TSAN)
  if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "KARTEND_ENABLE_TSAN is only supported on GCC/Clang")
  elseif (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR
      "KARTEND_ENABLE_TSAN=ON requires -DCMAKE_BUILD_TYPE=Debug "
      "(got '${CMAKE_BUILD_TYPE}'). TSan needs unoptimized builds for usable "
      "reports.")
  else()
    add_compile_options(
      $<$<CONFIG:Debug>:-fsanitize=thread>
      $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
      $<$<CONFIG:Debug>:-O1>
    )
    add_link_options(
      $<$<CONFIG:Debug>:-fsanitize=thread>
    )
  endif()
endif()

if (KARTEND_ENABLE_COVERAGE)
  if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "KARTEND_ENABLE_COVERAGE is only supported on GCC/Clang")
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

# Aggregator INTERFACE library that pulls every per-area OBJECT lib into the
# main executable + integration test harness. The per-area libs (kartend_utils,
# kartend_api, kartend_chrome, kartend_data, kartend_input, kartend_media,
# kartend_behavior, kartend_ui, kartend_core) are declared in
# src/CMakeLists.txt with a PUBLIC target_link_libraries DAG that enforces
# downward-only edges at the library level. Replaces the prior monolithic
# OBJECT library that hid module boundaries behind a single target.
set(KARTEND_LIB_TARGET kartend_lib)
add_library(${KARTEND_LIB_TARGET} INTERFACE)

add_subdirectory(src)

# Convenience handle for the per-area OBJECT libs declared in
# src/CMakeLists.txt. Used by the blocks below to apply compile / link
# options uniformly across every area.
set(KARTEND_AREA_LIBS
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

add_executable(kartend ${SRC_DIR}/core/main.cpp)

# Link every per-area OBJECT lib directly so their .o files end up in the
# executable. OBJECT libraries do NOT propagate through INTERFACE_LINK_-
# LIBRARIES — ${KARTEND_LIB_TARGET} aggregates them for documentation /
# usage-requirement plumbing, but the actual .o pull-in must be explicit
# here (and in the integration-test binary; see tests/integration/CMakeLists.txt).
target_link_libraries(kartend PRIVATE
  ${KARTEND_LIB_TARGET}
  ${KARTEND_AREA_LIBS}
)

set(KARTEND_TRANSLATION_INSTALL_SUBDIR "${APP_NAME}/translations")
set(KARTEND_TRANSLATION_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/translations")

# Re-extract source strings via:
#   cmake --build <build> --target update_translations
# Each per-area OBJECT lib owns its own sources, so SOURCE_TARGETS must
# enumerate every area lib (not just the INTERFACE aggregator, which has no
# sources of its own) for lupdate to scan all tr() calls.
qt_add_translations(kartend
  SOURCE_TARGETS
    ${KARTEND_AREA_LIBS}
    kartend
  TS_FILES translations/kartend_en.ts
  QM_FILES_OUTPUT_VARIABLE KARTEND_TRANSLATION_QM_FILES
  QM_OUTPUT_DIRECTORY "${KARTEND_TRANSLATION_BUILD_DIR}"
)
target_compile_definitions(kartend PRIVATE
  APP_TRANSLATIONS_SUBDIR="${KARTEND_TRANSLATION_INSTALL_SUBDIR}"
  APP_BUILD_TRANSLATIONS_DIR="${KARTEND_TRANSLATION_BUILD_DIR}"
)

# Optional gamepad backend — pushed onto kartend_utils PUBLIC so the headers
# and KARTEND_HAS_* compile defines propagate to every per-area lib through
# the same path as the core Qt comps. Lifting them to a single per-area lib
# would compile-fail neighbours whose headers reference GamepadManager types.
if (Qt6Gamepad_FOUND)
  target_link_libraries(kartend_utils PUBLIC Qt6::Gamepad)
  target_compile_definitions(kartend_utils PUBLIC KARTEND_HAS_QT_GAMEPAD=1)
elseif(TARGET PkgConfig::SDL2)
  target_link_libraries(kartend_utils PUBLIC PkgConfig::SDL2)
  target_compile_definitions(kartend_utils PUBLIC KARTEND_HAS_SDL2_GAMEPAD=1)
  message(STATUS "Qt6::Gamepad not found; using SDL2 gamepad backend")
else()
  message(STATUS "No gamepad backend found (Qt6::Gamepad or SDL2); building without gamepad support")
endif()

if (TARGET PkgConfig::ZSTD)
  # zstd is consumed by KartWriter / KartReader (src/modules/data/kart/) but
  # KARTEND_HAS_ZSTD is read from #ifdef gates in lower layers too — keep
  # both on kartend_utils PUBLIC so the propagation path matches the other
  # optional backends above.
  target_link_libraries(kartend_utils PUBLIC PkgConfig::ZSTD)
  target_compile_definitions(kartend_utils PUBLIC KARTEND_HAS_ZSTD=1)
  message(STATUS "zstd found; Kart compression will use zstd")
else()
  message(STATUS "zstd not found; Kart compression will use zlib (qCompress)")
endif()

# APP_* defines are read from every layer (logging, error reporting,
# update checks, AboutDialog). Push them onto kartend_utils PUBLIC so they
# propagate to every per-area lib through the link DAG.
target_compile_definitions(kartend_utils
  PUBLIC
    APP_ID="${KARTEND_APP_ID}"
    APP_NAME="kartend"
    APP_DISPLAY_NAME="Kartend"
    APP_VERSION="${PROJECT_VERSION}"
    APP_AUTHOR="EtherAura"
    BUILD_DATE="${BUILD_DATE}"
)

# Warning flags applied uniformly across every area OBJECT lib (PRIVATE
# because warnings shouldn't leak through INTERFACE_COMPILE_OPTIONS to
# external consumers like Qt's autogen units).
foreach(_area ${KARTEND_AREA_LIBS})
  target_compile_options(${_area} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

# Keep GCC-only intent with Clang analogue on RISC-V
if (CMAKE_SYSTEM_PROCESSOR MATCHES "[Rr][Ii][Ss][Cc][Vv]")
  foreach(_area ${KARTEND_AREA_LIBS})
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${_area} PRIVATE -mno-direct-extern-access)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options(${_area} PRIVATE -fno-direct-access-external-data)
    endif()
  endforeach()
endif()

# Release flags split into:
#   HARDENING_*    — always-on for non-Debug. Distro packagers need these.
#   RELEASE_PERF_* — high-performance Release-only. -march=native makes the
#                    binary non-portable, so KARTEND_PORTABLE_RELEASE skips it
#                    (and -O3/fast-math) for distro packaging.
set(HARDENING_COMPILE_OPTS -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE)
set(HARDENING_LINK_OPTS -Wl,-z,now -Wl,-z,relro -pie)

if (KARTEND_PORTABLE_RELEASE)
  set(RELEASE_PERF_COMPILE_OPTS -flto=auto -DQT_NO_DEBUG)
  set(RELEASE_PERF_LINK_OPTS -flto=auto -Wl,--gc-sections -Wl,--as-needed)
else()
  # Removed flags (the perf gain on a Qt frontend is in the noise, the risk
  # is real):
  #   -ffast-math: defines __FAST_MATH__ which can poison transitive headers
  #     (some libraries #error on it) and changes IEEE-754 rounding;
  #     -fno-signed-zeros / -fno-trapping-math / -fno-math-errno are part of
  #     the same family.
  #   -fdelete-null-pointer-checks: legal C++ pessimization that has caused
  #     CVE-class bugs in mainstream projects when assumptions about UB
  #     differ between compiler versions.
  #   -fmerge-all-constants: violates the C/C++ standard's "distinct
  #     addresses for distinct objects" guarantee; rarely matters in
  #     practice but breaks identity comparisons of string literals.
  set(RELEASE_PERF_COMPILE_OPTS
    -O3 -march=native -flto=auto
    -fomit-frame-pointer -funroll-loops
    -fno-semantic-interposition -ftree-vectorize
    -DQT_NO_DEBUG
  )
  set(RELEASE_PERF_LINK_OPTS -flto=auto -Wl,-O3 -Wl,--gc-sections -Wl,--as-needed)
endif()

# lld is only compatible with Clang LTO objects; GCC LTO (GIMPLE) requires
# the GNU linker or gold.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(PREPEND RELEASE_PERF_LINK_OPTS -fuse-ld=lld)
endif()

# RPATH: portable bundles (Flatpak, AppImage) ship Qt and other transitive
# libraries inside the bundle alongside the executable. Embed $ORIGIN-relative
# install RPATH so the binary resolves them without LD_LIBRARY_PATH. System
# package builds (PKGBUILD, ebuild, distro CI) use the distro's standard
# library paths and want NO embedded RPATH — leaving the install RPATH unset
# is the right default there. AUTO_USE_LINK_PATH is disabled so the build
# tree's link paths don't leak into the installed binary.
if (KARTEND_PORTABLE_RELEASE)
  set_target_properties(kartend PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../lib;\$ORIGIN/../lib64"
    INSTALL_RPATH_USE_LINK_PATH FALSE
    BUILD_WITH_INSTALL_RPATH FALSE
    SKIP_BUILD_RPATH FALSE
  )
endif()

# Hardening (non-Debug) + release perf options applied to every per-area
# OBJECT lib so the generated .o files participate in LTO and pick up the
# same security flags as the main exe link.
foreach(_area ${KARTEND_AREA_LIBS})
  target_compile_options(${_area} PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:${HARDENING_COMPILE_OPTS}>
    $<$<CONFIG:Release>:${RELEASE_PERF_COMPILE_OPTS}>
  )
endforeach()
target_link_options(kartend PRIVATE
  $<$<NOT:$<CONFIG:Debug>>:${HARDENING_LINK_OPTS}>
  $<$<CONFIG:Release>:${RELEASE_PERF_LINK_OPTS}>
)

# LTO objects in the per-area OBJECT libs are unreadable to a non-LTO link
# step, so every consumer (the main exe AND the integration test binary)
# must link with the matching LTO and linker flags. Surface the minimum set
# as INTERFACE_LINK_OPTIONS on the aggregator so they propagate automatically
# to anything that links ${KARTEND_LIB_TARGET}.
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

# PGO: applied to each area lib (must come AFTER target_compile_options
# above used genex substitution — applying via separate target_*_options calls
# accumulates rather than relying on dead list(APPEND) text substitution).
if (KARTEND_USE_PGO)
  if (KARTEND_PGO_GENERATE)
    message(STATUS "PGO: Generating profile data in ${KARTEND_PGO_PROFILE_DIR}")
    foreach(_area ${KARTEND_AREA_LIBS})
      target_compile_options(${_area} PRIVATE
        $<$<CONFIG:Release>:-fprofile-generate=${KARTEND_PGO_PROFILE_DIR}>)
    endforeach()
    target_link_options(kartend PRIVATE
      $<$<CONFIG:Release>:-fprofile-generate=${KARTEND_PGO_PROFILE_DIR}>)
  elseif (KARTEND_PGO_USE)
    message(STATUS "PGO: Using profile data from ${KARTEND_PGO_PROFILE_DIR}")
    foreach(_area ${KARTEND_AREA_LIBS})
      target_compile_options(${_area} PRIVATE
        $<$<CONFIG:Release>:-fprofile-use=${KARTEND_PGO_PROFILE_DIR}>
        $<$<CONFIG:Release>:-fprofile-correction>)
    endforeach()
    target_link_options(kartend PRIVATE
      $<$<CONFIG:Release>:-fprofile-use=${KARTEND_PGO_PROFILE_DIR}>)
  endif()
endif()

# NOTE: Release builds intentionally do NOT pass `-Wl,-s` to the linker.
# Distro packaging (Arch makepkg, Gentoo with FEATURES=splitdebug, Debian
# debug-symbol packages) does its own stripping and typically wants
# unstripped input so it can split debug info into a separate package.
# Pre-stripping here yields empty debug splits and makes downstream
# packagers' jobs harder. Flatpak and AppImage builders also strip on
# their own. Local installers can pass --strip themselves if desired.

foreach(_area ${KARTEND_AREA_LIBS})
  target_compile_definitions(${_area} PRIVATE $<$<CONFIG:Release>:NDEBUG>)
endforeach()

# Debug-only linker map file output. Opt-in via -DKARTEND_LINKER_MAP=ON; the
# map is written to the build directory so sandboxed/chroot builders
# (Flatpak, distro CI) can use the option without needing the source tree's
# parent dir to be writable. Copy the map out post-build if you want it
# alongside reports — build.sh does not assume any particular location.
if (KARTEND_LINKER_MAP)
  set(_MAP_DIR "${CMAKE_BINARY_DIR}/maps")
  file(MAKE_DIRECTORY "${_MAP_DIR}")
  target_link_options(kartend PRIVATE
    $<$<CONFIG:Debug>:-Wl,-Map,${_MAP_DIR}/kartend.map>
  )
endif()

# Install (Linux)
if (UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS kartend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/${KARTEND_APP_ID}.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES src/assets/icon.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
          RENAME ${KARTEND_APP_ID}.svg)
  install(FILES packaging/${KARTEND_APP_ID}.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
  install(FILES ${KARTEND_TRANSLATION_QM_FILES}
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${KARTEND_TRANSLATION_INSTALL_SUBDIR})
endif()

# Uninstall: read install_manifest.txt and remove the listed files. Honors
# DESTDIR. Run via `cmake --build <build-dir> --target uninstall`.
if (NOT TARGET uninstall)
  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Removing files listed in install_manifest.txt"
    VERBATIM)
endif()

# KARTEND_MAINTENANCE adds -Werror to every area lib's compile step.
if (KARTEND_MAINTENANCE)
  foreach(_area ${KARTEND_AREA_LIBS})
    target_compile_options(${_area} PRIVATE -Werror)
  endforeach()
endif()

# Tests (optional; declared at top with other options)
if (KARTEND_BUILD_TESTS)
  # Ensure CTest is enabled at the top-level so `ctest` run from the main build
  # directory discovers tests defined in subdirectories.
  enable_testing()
  add_subdirectory(tests)
endif()

# CPack: enables `cpack -G TGZ` for binary tarballs and `cpack --config
# CPackSourceConfig.cmake -G TGZ` for source tarballs. The tag-triggered
# release workflow uses `git archive` directly, so CPack is here for
# ad-hoc prerelease bundling and downstream packaging assistance.
set(CPACK_PACKAGE_NAME "kartend")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "EtherAura")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Qt6 KDE frontend for organizing and launching multimedia collections")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/EtherAura/Kartend")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/readme.md")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "Kartend-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
  "/\\\\.git/"
  "/\\\\.beads/"
  "/\\\\.cache/"
  "/\\\\.claude/"
  "/\\\\.obsidian/"
  "/\\\\.vscode/"
  "/build/"
  "/Testing/"
  "/\\\\.backups/"
  "\\\\.dolt/"
  "\\\\.swp$"
  "~$"
)
include(CPack)
