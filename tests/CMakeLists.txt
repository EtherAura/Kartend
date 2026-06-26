# Kartend Tests
cmake_minimum_required(VERSION 3.20)

# Find Qt6 Test module
find_package(Qt6 REQUIRED COMPONENTS Test Gui Widgets)

# Enable testing
enable_testing()

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
# Tests migrated by Kartend-oeuf link the relevant kartend_<area>
# OBJECT lib(s) and pick up include directories from those libs'
# INTERFACE properties. A small set of header-only / Qt-only smoke
# tests still resolves their #includes through this directory-level
# fallback — they're trivial enough that wiring an area lib in would
# be more code than the test itself.
include_directories(
  ${SRC_DIR}/api
  ${SRC_DIR}/utils
  ${SRC_DIR}/utils/app
  ${SRC_DIR}/utils/db
  ${SRC_DIR}/utils/fs
  ${SRC_DIR}/utils/text
  ${SRC_DIR}/utils/threading
  ${SRC_DIR}/utils/view
  ${SRC_DIR}/ui
)

# ---------------------------------------------------------------------------
# STATIC twins of the per-area OBJECT libs — TEST linking only (Kartend-j0yin).
#
# The app target keeps linking the OBJECT libs directly (Kartend-q3vfq kept
# them OBJECT for layering enforcement; nothing here touches that). For the
# ~160 test executables OBJECT libs are pessimal, though: linking an OBJECT
# lib forces EVERY one of its .o files into the executable — no
# dead-stripping — so each test binary embedded the full object set of every
# linked area (a 13 GB debug tests/ dir, link-dominated edit-test cycles).
#
# Each twin archives the SAME object files ($<TARGET_OBJECTS:...> — objects
# compile exactly once, shared with the app link). A STATIC archive restores
# normal per-member linker semantics: only the object files that resolve a
# referenced symbol are pulled into the test binary.
#
# The INTERFACE link from the twin back to its OBJECT lib forwards the full
# usage surface (include dirs, compile definitions, transitive Qt deps, the
# Release LTO INTERFACE_LINK_OPTIONS on kartend_utils) WITHOUT putting raw
# .o files on consumers' link lines: OBJECT libs reached through
# INTERFACE_LINK_LIBRARIES contribute usage requirements only (verified —
# the consumer's link line contains just the archive).
#
# Qt caveat: an archive member whose only contribution is a static-init side
# effect (qrc resource registration, namespace-scope qRegisterMetaType) is
# dropped when nothing references a symbol in it. AUTOMOC is safe — each
# area's moc TUs land in one mocs_compilation.o whose meta-object symbols are
# referenced by the class TUs that get pulled in. If a future test trips the
# static-init case, link that single test against the OBJECT libs instead
# (kartend_add_test falls back automatically when no _static twin exists).
if (TARGET kartend_utils)
  foreach(_kartend_area IN LISTS KARTEND_AREA_LIBS)
    add_library(${_kartend_area}_static STATIC
      $<TARGET_OBJECTS:${_kartend_area}>)
    target_link_libraries(${_kartend_area}_static INTERFACE ${_kartend_area})
  endforeach()
  # Second pass (all twins exist now): mirror each OBJECT lib's inter-area
  # PUBLIC dependency DAG onto its twin. Without these edges, cross-archive
  # symbols (e.g. kartend_data's querymanagercache.cpp.o referencing
  # lcSearchDiag() from kartend_utils) go unresolved: OBJECT libs reached
  # transitively contribute usage requirements ONLY — never their objects —
  # so the archives were the sole symbol providers but had no declared
  # ordering, and ld's single-pass archive scan dropped late-requested
  # members. With twin→twin edges CMake topologically sorts and repeats the
  # archives on the link line exactly like any static-library DAG.
  foreach(_kartend_area IN LISTS KARTEND_AREA_LIBS)
    get_target_property(_kartend_area_deps ${_kartend_area} INTERFACE_LINK_LIBRARIES)
    if (_kartend_area_deps)
      foreach(_kartend_dep IN LISTS _kartend_area_deps)
        if (_kartend_dep IN_LIST KARTEND_AREA_LIBS)
          target_link_libraries(${_kartend_area}_static INTERFACE ${_kartend_dep}_static)
        endif()
      endforeach()
    endif()
  endforeach()
endif()

# ---------------------------------------------------------------------------
# kartend_add_test(NAME <CTestName> SOURCES <files...> LINK <libs...>)
#
# Collapses the per-test add_executable + target_link_libraries + add_test
# boilerplate. The build target (and binary) name is derived from the first
# SOURCES entry's basename (tests/foo/test_x.cpp -> test_x), which preserves
# every pre-helper target name — irregular extras (set_tests_properties,
# target_include_directories, target_compile_definitions, conditional links)
# keep working as trailing statements referencing that target.
#
# LINK entries naming an area lib are rewritten to the lib's _static twin
# (see above); anything else (Qt6::*, ${KARTEND_AREA_LIBS} expansions) passes
# through. Qt6::Test is always appended — don't list it.
#
# NOTE for greppers: the nightly benchmark workflow derives its target list
# from literal `add_executable(bench_` lines in this file, so the benchmark
# blocks below stay explicit; .scripts/check-test-mapping.py only requires
# each test_*.cpp basename to appear somewhere in a tests/**/CMakeLists.txt,
# which SOURCES entries satisfy.
function(kartend_add_test)
  cmake_parse_arguments(KAT "" "NAME" "SOURCES;LINK" ${ARGN})
  if (NOT KAT_NAME OR NOT KAT_SOURCES)
    message(FATAL_ERROR "kartend_add_test: NAME and SOURCES are required")
  endif()
  if (KAT_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "kartend_add_test(${KAT_NAME}): unexpected args: ${KAT_UNPARSED_ARGUMENTS}")
  endif()
  list(GET KAT_SOURCES 0 _kat_first_source)
  get_filename_component(_kat_target "${_kat_first_source}" NAME_WE)
  add_executable(${_kat_target} ${KAT_SOURCES})
  set(_kat_libs "")
  foreach(_kat_lib IN LISTS KAT_LINK)
    if (TARGET ${_kat_lib}_static)
      list(APPEND _kat_libs ${_kat_lib}_static)
    else()
      list(APPEND _kat_libs ${_kat_lib})
    endif()
  endforeach()
  target_link_libraries(${_kat_target} PRIVATE ${_kat_libs} Qt6::Test)
  add_test(NAME ${KAT_NAME} COMMAND ${_kat_target})
endfunction()

# ── Per-area test registrations (Kartend-n1hpy.8) ─────────────────────────
# The former ~1670-line registration body was split into ordered include()
# fragments under tests/cmake/. include() (NOT add_subdirectory) keeps the
# tests/ directory scope, so SOURCES paths stay relative to tests/, the
# kartend_add_test helper + the _kartend_collectionutils_* shared vars + the
# final per-test HOME foreach below all keep working unchanged. Fragments are
# included in their original order, so the configure result is identical to
# the monolith.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/01-helpers-and-widgets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/02-ui-dialogs.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/03-data-core.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/04-scraper-and-dat.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/05-dataudit-kart-and-db.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/06-input-helpers-and-core.cmake)

# Integration test harness for UI-coordinator managers.
# Only built when the per-area OBJECT libs are available (i.e. when
# tests/CMakeLists.txt is added from the top-level CMakeLists.txt rather than
# configured stand-alone). kartend_core is a representative sentinel.
if (TARGET kartend_core)
  add_subdirectory(integration)
endif()

# Kartend-kkeur: isolate every test's on-disk state.
#
# Most tests call QStandardPaths::setTestModeEnabled(true), which routes the
# config/data/cache locations under a single shared ~/.qttest tree (derived
# from $HOME). That isolates tests from the real user profile but NOT from
# each other: under `ctest -j` the parallel test processes all write the same
# kartend .ini and SQLite db, so they intermittently clobber each other —
# surfacing as a DatabaseManager subprocess abort or a PerDomainSignals /
# SettingsRoundtrip assertion failure that passes on a serial rerun.
#
# Give each test its own HOME so ~/.qttest resolves to a per-test directory;
# cross-process collisions become impossible while full parallelism is kept.
# This covers tests registered in THIS directory; the integration subdir does
# the same for its own. APPEND (not set) so tests that already set ENVIRONMENT
# (e.g. QT_QPA_PLATFORM=offscreen) keep it.
get_property(_kartend_tests DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS _kartend_tests)
  set(_test_home "${CMAKE_BINARY_DIR}/test-homes/${_test}")
  file(MAKE_DIRECTORY "${_test_home}")
  set_property(TEST ${_test} APPEND PROPERTY ENVIRONMENT "HOME=${_test_home}")
endforeach()
