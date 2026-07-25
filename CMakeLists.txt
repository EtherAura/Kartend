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
# Kartend-bkidv: line-tables-only debug info. OFF by default so a developer
# debugging a sanitizer finding locally keeps full DWARF (variable inspection
# in gdb); CI's instrumented jobs turn it ON to stay under the runner disk
# ceiling. See the KARTEND_SLIM_DEBUG_INFO block further down for the numbers.
option(KARTEND_SLIM_DEBUG_INFO "Emit line tables only (-g1) instead of full DWARF" OFF)
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
option(KARTEND_ENABLE_LIBARCHIVE "Allow auto-detecting libarchive for in-process archive hashing" ON)

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

# MSVC + Ninja Debug PDB contention (Kartend audit h4j5s). Debug/RelWithDebInfo
# default to /Zi (separate .pdb) and Ninja compiles a target's TUs in parallel,
# so multiple cl.exe race to open one .pdb -> "fatal error C1041 ... if multiple
# CL.EXE write to the same .PDB file, please use /FS". An earlier add_compile_options(/FS)
# attempt did NOT reach the qtkeychain FetchContent dependency (a directory
# COMPILE_OPTIONS property doesn't propagate into the sub-build), so qt6keychain
# kept failing. Switch /Zi -> /Z7 (debug info embedded in each .obj, no .pdb at
# all -> contention is impossible) in the GLOBAL per-config flag strings, which
# every target inherits including FetchContent deps. Done before the qtkeychain
# FetchContent below. CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded is the modern
# spelling but needs CMake 3.25 and we floor at 3.20.
if (MSVC)
  foreach(_flagvar
      CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
      CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    string(REPLACE "/Zi" "/Z7" ${_flagvar} "${${_flagvar}}")
  endforeach()
endif()

# Optional-backend discovery (gamepad / zstd / libarchive / QtKeychain incl.
# the MSVC FetchContent + header-shim block) lives in a dedicated module
# (Kartend-9fdsd). Behaviour-identical: include()d here in the same scope and
# at the same point the inline blocks used to sit.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OptionalBackends.cmake)

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

# Kartend-bkidv: shrink the instrumented build trees.
#
# An instrumented Debug build links ~240 test executables, and that tree
# outgrew the smaller half of the GitHub-hosted runner fleet — Kartend-dbkuy
# established that the ubuntu-24.04 pool mixes 72 GB and 145 GB root disks,
# and the same commit failed on a 72 GB runner with "ld: final link failed:
# No space left on device" while passing on a 145 GB one.
#
# Measured on the largest test binary (test_statisticsdialog, Debug+ASan,
# GCC 15 locally; section sizes via readelf):
#
#   -g (today)      513.5 MiB   .debug_info 147.6  link ~8.6s
#   -g -gz=zlib     413.1 MiB   .debug_info  70.6  link ~17-28s
#   -g1             343.5 MiB   .debug_info   2.7  link ~5.9s
#
# So -g1 wins on both axes and compressed debug sections lose on both: -gz
# buys 1.24x for roughly DOUBLE the link time (x240 executables), while -g1
# buys 1.49x and links FASTER because there is far less DWARF to copy. Don't
# re-try -gz here — it was measured, not assumed.
#
# What is left after -g1 is not debug info at all: .data 139.3 MiB (ASan
# global redzones) + .text 39.1 + .rodata 24.3. That ~203 MiB floor per
# executable is why Kartend-wjgjy (link the app code once as a shared library
# instead of statically into all ~240 test exes) is the real structural fix.
#
# Stack traces are unaffected: verified on the CI toolchain (Ubuntu 24.04)
# that they stay identical down to file:line:col under -g1 for GCC 13 + ASan,
# Clang 18 + ASan and Clang 18 + TSan. -g1 keeps line tables and drops only
# local-variable DWARF. It stays opt-in so a developer debugging a sanitizer
# finding in gdb still gets full DWARF by default.
#
# DO NOT ENABLE THIS FOR THE TSan JOBS. Traces surviving is not the same as
# SUPPRESSIONS surviving, which is what actually bit us (run 30158366991, 3
# failures). tests/suppressions/tsan.txt anchors Group D2 on frames that exist
# only as INLINED template frames — `race:NonPromiseTaskResolver` matches the
# class-qualified name of an inlined frame in qtconcurrentstoredfunctioncall.h.
# -g1 drops the class-scope DWARF the symbolizer needs to qualify those frames,
# so they come back as a bare `run`, the anchor stops matching, and documented
# QtConcurrent false positives fire as real failures. The traces looked fine —
# they were just spelled differently enough to miss the suppression.
#
# ASan/LSan is genuinely safe, not merely lucky: tests/suppressions/lsan.txt
# anchors on out-of-line symbols (QThreadPool::QThreadPool,
# ScanService::ScanService) and library names, all resolvable from .symtab
# without inline reconstruction.
#
# The trade never favoured TSan anyway — its tree is ~3.5 GB against ASan's
# ~28 GB, so it was never the job near the disk ceiling.
if (KARTEND_SLIM_DEBUG_INFO)
  if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "KARTEND_SLIM_DEBUG_INFO is only supported on GCC/Clang")
  else()
    # Appended after CMAKE_CXX_FLAGS_DEBUG's own -g; the last -g<level> on the
    # command line wins, so this demotes it rather than fighting it.
    add_compile_options($<$<CONFIG:Debug>:-g1>)
  endif()
endif()

if (NOT DEFINED BUILD_DATE)
  set(BUILD_DATE "unknown")
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Compiler/linker flag policy (Kartend-9fdsd): defines the flag-matrix
# variables (HARDENING_* / RELEASE_PERF_* / KARTEND_LTO_LINK_OPTIONS) and the
# kartend_apply_area_flags() helper that applies the per-area-library compile
# options as a single source of truth. The variables depend only on
# compiler/platform identity (stable after project()), and the helper is
# CALLED below once the area libs exist — so including the module here is
# behaviour-identical to the old inline blocks.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerFlags.cmake)

add_subdirectory(src)

# Convenience handle for the per-area OBJECT libs declared in
# src/CMakeLists.txt. Used by the blocks below to apply compile / link
# options uniformly across every area, and to wire every .o file into the
# executable + integration-test binary (OBJECT libs don't propagate through
# INTERFACE_LINK_LIBRARIES — the .o pull-in must be explicit per consumer).
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

target_link_libraries(kartend PRIVATE ${KARTEND_AREA_LIBS})

# Windows resource: app icon + VERSIONINFO. Configured at generator
# time so the embedded version stays in sync with the VERSION file
# (single source of truth via the project() command above). Both MSVC
# (cl.exe / rc.exe) and MinGW (windres) handle .rc through CMake's
# RC language with no extra build-script glue.
if (WIN32)
  enable_language(RC)
  set(KARTEND_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/icon.ico")
  set(KARTEND_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
  set(KARTEND_VERSION_MINOR ${PROJECT_VERSION_MINOR})
  set(KARTEND_VERSION_PATCH ${PROJECT_VERSION_PATCH})
  set(KARTEND_APP_AUTHOR "EtherAura")
  set(KARTEND_APP_NAME "kartend")
  set(KARTEND_APP_DISPLAY_NAME "Kartend")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/assets/resources.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/src/assets/resources.rc"
    @ONLY
  )
  target_sources(kartend PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/src/assets/resources.rc"
  )
endif()

# Literal "kartend" — NOT ${APP_NAME}, which is only a compile-define string
# (APP_NAME="kartend" below), never a CMake variable, so ${APP_NAME} expanded to
# empty and collapsed this to "/translations". Installed builds then dropped .qm
# files under <datarootdir>//translations and QStandardPaths::locateAll() with a
# leading-slash subpath could never find them (Kartend-v94u7).
set(KARTEND_TRANSLATION_INSTALL_SUBDIR "kartend/translations")
set(KARTEND_TRANSLATION_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/translations")

# Re-extract source strings via:
#   cmake --build <build> --target update_translations
# Each per-area OBJECT lib owns its own sources, so SOURCE_TARGETS must
# enumerate every area lib (not just the INTERFACE aggregator, which has no
# sources of its own) for lupdate to scan all tr() calls.
#
# Kartend-7oct: English is the source locale, so kartend_en.ts is maintained
# by lupdate as a translator seed but never compiled to a .qm — Qt's tr()
# already returns the source text when no .qm matches the active locale, so
# an "all-English-source, no-translations" .qm just bloats the install. Other
# languages are picked up by the glob and run through lrelease.
file(GLOB KARTEND_NON_SOURCE_TS_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/translations/kartend_*.ts")
list(REMOVE_ITEM KARTEND_NON_SOURCE_TS_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/translations/kartend_en.ts")

qt_add_lupdate(kartend
  SOURCE_TARGETS
    ${KARTEND_AREA_LIBS}
    kartend
  TS_FILES
    translations/kartend_en.ts
    ${KARTEND_NON_SOURCE_TS_FILES}
)

if(KARTEND_NON_SOURCE_TS_FILES)
  qt_add_lrelease(kartend
    TS_FILES ${KARTEND_NON_SOURCE_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE KARTEND_TRANSLATION_QM_FILES
    QM_OUTPUT_DIRECTORY "${KARTEND_TRANSLATION_BUILD_DIR}"
  )
else()
  # No non-source .ts files yet; the install(FILES ...) rule below becomes a
  # no-op until the first translation lands.
  set(KARTEND_TRANSLATION_QM_FILES "")
endif()
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

if (KARTEND_LIBARCHIVE_TARGET)
  # libarchive is consumed by RomHasher (src/utils/fs/) which lives in
  # kartend_utils; KARTEND_HAS_LIBARCHIVE gates the in-process member hasher
  # there. PUBLIC so the define propagates like the other optional backends.
  # The target is either pkg-config's PkgConfig::LIBARCHIVE (Linux) or
  # FindLibArchive's LibArchive::LibArchive (MSVC/vcpkg + macOS).
  target_link_libraries(kartend_utils PUBLIC ${KARTEND_LIBARCHIVE_TARGET})
  target_compile_definitions(kartend_utils PUBLIC KARTEND_HAS_LIBARCHIVE=1)
  message(STATUS "libarchive found (${KARTEND_LIBARCHIVE_TARGET}); archive ROMs hashed in-process")
else()
  message(STATUS "libarchive not found; archive ROMs hashed via 7z/unzip/bsdtar")
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

# Per-area compile-flag application — single source of truth (Kartend-9fdsd).
# kartend_apply_area_flags() (cmake/CompilerFlags.cmake) applies, to every
# area OBJECT lib, the full uniform set the root used to spread across six
# hand-written foreach(_area ...) loops: warnings, the RISC-V extern-access
# intent, hardening (non-Debug) + release-perf (Release/RelWithDebInfo)
# compile opts, NDEBUG in Release, -Werror in maintenance, and the PGO
# generate/use compile opts. Collapsing them into one call means a flag lands
# on EVERY area lib or none — it can't be applied to five and silently skipped
# on the sixth. The flag-matrix variables (HARDENING_* / RELEASE_PERF_* /
# KARTEND_LTO_LINK_OPTIONS) it reads are computed in that same module.
#
# The PGO STATUS/FATAL_ERROR diagnostics and the target-SPECIFIC link/define
# applications the old loops were interleaved with (the kartend exe link
# options, kartend_utils PUBLIC interface link options, the PGO link options
# on the kartend exe) stay inline below — they are single-target, not a
# per-area sweep.
kartend_apply_area_flags(${KARTEND_AREA_LIBS})

# RPATH: portable bundles (Flatpak, AppImage) ship Qt and other transitive
# libraries inside the bundle alongside the executable. Embed $ORIGIN-relative
# install RPATH so the binary resolves them without LD_LIBRARY_PATH. System
# package builds (PKGBUILD, ebuild, distro CI) use the distro's standard
# library paths and want NO embedded RPATH — leaving the install RPATH unset
# is the right default there. AUTO_USE_LINK_PATH is disabled so the build
# tree's link paths don't leak into the installed binary.
# Windows/PE has no RPATH concept — DLL search uses the exe's directory,
# %PATH%, and the SafeDllSearchMode rules. Portable bundles on Windows
# co-locate DLLs next to the exe via windeployqt, so no per-target hint
# is needed.
if (KARTEND_PORTABLE_RELEASE AND NOT WIN32)
  set_target_properties(kartend PROPERTIES
    INSTALL_RPATH "\$ORIGIN/../lib;\$ORIGIN/../lib64"
    INSTALL_RPATH_USE_LINK_PATH FALSE
    BUILD_WITH_INSTALL_RPATH FALSE
    SKIP_BUILD_RPATH FALSE
  )
endif()

# The per-area hardening/release-perf COMPILE options are applied by
# kartend_apply_area_flags() above (cmake/CompilerFlags.cmake). The matching
# LINK options on the kartend EXECUTABLE are target-specific, so they stay
# here.
target_link_options(kartend PRIVATE
  $<$<NOT:$<CONFIG:Debug>>:${HARDENING_LINK_OPTS}>
  $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:${RELEASE_PERF_LINK_OPTS}>
)

# LTO objects in the per-area OBJECT libs are unreadable to a non-LTO link
# step, so every consumer (main exe + integration tests + the small
# tests/test_*.cpp binaries that link a subset of the per-area libs) must
# link with the matching LTO and linker flags. With the kartend_lib
# INTERFACE aggregator deleted (Kartend-w1wv), the previous propagation
# path (aggregator's INTERFACE_LINK_OPTIONS) is gone. Hang the options
# off kartend_utils's INTERFACE_LINK_OPTIONS instead — every per-area lib
# PUBLIC-links kartend_utils per the DAG in src/CMakeLists.txt, so any
# consumer linking ANY area lib transitively inherits these flags through
# the chain. Tested under clang-18 Release where small test binaries link
# direct area-lib .o subsets (Kartend-eyzv). KARTEND_LTO_LINK_OPTIONS is
# computed in cmake/CompilerFlags.cmake (single source of truth with the
# compile-side LTO flags).
target_link_options(kartend_utils INTERFACE ${KARTEND_LTO_LINK_OPTIONS})

# PGO: the per-area COMPILE opts (-fprofile-generate / -fprofile-use) are
# applied by kartend_apply_area_flags() above. The STATUS/FATAL_ERROR
# diagnostics and the kartend-executable LINK options are target-specific /
# one-shot, so they stay here.
if (KARTEND_USE_PGO)
  if (MSVC)
    # MSVC PGO uses /GENPROFILE / /USEPROFILE (compile and link side) plus
    # a pgosweep run between generate and use. Not yet wired here — track
    # as a follow-up once the GCC/Clang path is exercised on Windows.
    message(FATAL_ERROR
      "KARTEND_USE_PGO is not yet supported on MSVC. Use GCC/Clang for PGO, "
      "or file a follow-up to add the /GENPROFILE / /USEPROFILE pair.")
  endif()
  if (KARTEND_PGO_GENERATE)
    message(STATUS "PGO: Generating profile data in ${KARTEND_PGO_PROFILE_DIR}")
    target_link_options(kartend PRIVATE
      $<$<CONFIG:Release>:-fprofile-generate=${KARTEND_PGO_PROFILE_DIR}>)
  elseif (KARTEND_PGO_USE)
    message(STATUS "PGO: Using profile data from ${KARTEND_PGO_PROFILE_DIR}")
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

# (NDEBUG in Release is applied per-area by kartend_apply_area_flags() above.)

# Debug-only linker map file output. Opt-in via -DKARTEND_LINKER_MAP=ON; the
# map is written to the build directory so sandboxed/chroot builders
# (Flatpak, distro CI) can use the option without needing the source tree's
# parent dir to be writable. Copy the map out post-build if you want it
# alongside reports — build.sh does not assume any particular location.
if (KARTEND_LINKER_MAP)
  set(_MAP_DIR "${CMAKE_BINARY_DIR}/maps")
  file(MAKE_DIRECTORY "${_MAP_DIR}")
  if (MSVC)
    target_link_options(kartend PRIVATE
      $<$<CONFIG:Debug>:/MAP:${_MAP_DIR}/kartend.map>
    )
  else()
    target_link_options(kartend PRIVATE
      $<$<CONFIG:Debug>:-Wl,-Map,${_MAP_DIR}/kartend.map>
    )
  endif()
endif()

# Install rules (Linux) + CPack/DEB packaging config live in a dedicated
# module (Kartend-9fdsd), include()d at the end of this file. The uninstall
# custom target stays here because it's referenced by build.sh's --uninstall
# path and is not packaging config per se.

# Uninstall: read install_manifest.txt and remove the listed files. Honors
# DESTDIR. Run via `cmake --build <build-dir> --target uninstall`.
if (NOT TARGET uninstall)
  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Removing files listed in install_manifest.txt"
    VERBATIM)
endif()

# (KARTEND_MAINTENANCE's -Werror / /WX is applied per-area by
# kartend_apply_area_flags() above.)

# Tests (optional; declared at top with other options)
if (KARTEND_BUILD_TESTS)
  # Ensure CTest is enabled at the top-level so `ctest` run from the main build
  # directory discovers tests defined in subdirectories.
  enable_testing()
  add_subdirectory(tests)
endif()

# Install rules (Linux) + CPack / Debian packaging config live in a dedicated
# module (Kartend-9fdsd). Included last so every install() call and the CPACK_*
# variables it sets are visible to its trailing include(CPack), and so the
# install rules run after the tests subdirectory has been added (no
# cross-dependency, behaviour-identical to the previous inline ordering).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake)
