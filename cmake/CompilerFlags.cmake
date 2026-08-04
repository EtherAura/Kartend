# Compiler/linker flag policy: the warning set, the per-platform hardening /
# release-perf / LTO / PGO flag matrix, and the kartend_apply_area_flags()
# helper that applies the per-area-library compile options in ONE place.
#
# Extracted from the root CMakeLists.txt (Kartend-9fdsd). Behaviour-preserving:
# the flag-matrix variables are computed with the same conditionals as before,
# and kartend_apply_area_flags() applies to every lib in KARTEND_AREA_LIBS the
# exact same options the old hand-written `foreach(_area ...)` loops did — but
# now from a single source of truth so a flag can't be applied to five of six
# area libs and silently skipped on the sixth.
#
# This module is include()d early (after project() + options, before
# add_subdirectory(src)) so the helper + flag variables exist; the variables
# depend only on compiler/platform identity, not on any target. The helper is
# CALLED from the root after the area libs are defined.

# ── Flag-matrix variables ────────────────────────────────────────────────────
# Release flags split into:
#   HARDENING_*    — always-on for non-Debug. Distro packagers need these.
#   RELEASE_PERF_* — high-performance Release-only. -march=native makes the
#                    binary non-portable, so KARTEND_PORTABLE_RELEASE skips it
#                    (and -O3/fast-math) for distro packaging.

# ── LTO flavour: ONE source of truth ─────────────────────────────────────────
# Kartend-f51g3. `-flto=auto` is a GCC spelling meaning "parallel LTO with
# nproc jobs", but Clang's driver expands it to `-flto=full` — monolithic,
# whole-program LTO. Verified: `clang++ -flto=auto -c x.cpp -###` emits
# "-flto=full", against "-flto=thin" for `-flto=thin`.
#
# On macOS Release that meant the app AND each of ~200 test executables did a
# whole-program LTO link of all six area libs against Qt, three at a time
# (the Build step runs `cmake --build --parallel $(sysctl -n hw.ncpu)`) on a
# 3-vCPU / 7 GB macos-14 runner. ld64 died with SIGABRT:
#   clang: error: unable to execute command: Abort trap: 6
#   clang: error: linker command failed due to signal
# ThinLTO keeps cross-TU optimisation but emits per-TU summaries and runs
# codegen in parallel instead of building one monolithic module, which is
# what makes its peak link memory tractable. ld64 supports it natively.
#
# Scoped to Apple on purpose: the Linux and Windows legs are not failing, and
# switching their LTO flavour would shift Release codegen for no reason.
#
# This single variable feeds the compile side AND every link site below. The
# flavour MUST agree across them — bitcode emitted for one flavour is not what
# the other's link step expects — so it is deliberately not spelled out
# separately at each site. MSVC ignores it (it uses /GL + /LTCG instead).
if (APPLE)
  set(KARTEND_LTO_FLAG -flto=thin)
else()
  set(KARTEND_LTO_FLAG -flto=auto)
endif()

if (MSVC)
  # MSVC equivalents of the GNU/ELF hardening set:
  #   /sdl       — additional security-development-lifecycle checks (extra
  #                buffer-overflow detection beyond /GS, which is on by
  #                default)
  #   /guard:cf  — Control Flow Guard; compile-side and link-side both
  #                required
  #   /DYNAMICBASE, /NXCOMPAT, /HIGHENTROPYVA — linker defaults on modern
  #                MSVC, listed explicitly so the intent mirrors the GNU
  #                -pie/-Wl,-z,relro/-Wl,-z,now set.
  # PIE / FORTIFY_SOURCE have no PE/MSVC analogue (ASLR is OS-managed via
  # /DYNAMICBASE; there's no fortified libc to wrap).
  set(HARDENING_COMPILE_OPTS /sdl /guard:cf)
  set(HARDENING_LINK_OPTS /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)

  # KARTEND_PORTABLE_RELEASE is a no-op on MSVC — there's no -march=native
  # equivalent and vectorization is already on at /O2. /GL + /LTCG is the
  # MSVC LTO pair; the /LTCG companion lives in KARTEND_LTO_LINK_OPTIONS
  # below so the integration-test exe picks it up too.
  set(RELEASE_PERF_COMPILE_OPTS /O2 /GL /DQT_NO_DEBUG)
  set(RELEASE_PERF_LINK_OPTS /LTCG /OPT:REF /OPT:ICF)
else()
  # GCC/Clang. Stack protector is portable; -D_FORTIFY_SOURCE / -fPIE /
  # -Wl,-z,* / -pie are ELF-only — MinGW's ld rejects -z, and PIE has no
  # PE counterpart. Gate them behind NOT WIN32 so MinGW cross-builds link
  # cleanly. On MinGW the equivalent runtime mitigations (ASLR, DEP,
  # high-entropy VA) are binutils defaults — no extra flags required.
  set(HARDENING_COMPILE_OPTS -fstack-protector-strong)
  set(HARDENING_LINK_OPTS)
  if (NOT WIN32)
    list(APPEND HARDENING_COMPILE_OPTS -D_FORTIFY_SOURCE=2 -fPIE)
  endif()
  # GNU-ld hardening flags: Apple's ld rejects -Wl,-z,* options (and applies
  # its own load-time mitigations), so gate the link-side set out on macOS.
  if (NOT WIN32 AND NOT APPLE)
    list(APPEND HARDENING_LINK_OPTS -Wl,-z,now -Wl,-z,relro -pie)
  endif()

  if (KARTEND_PORTABLE_RELEASE)
    set(RELEASE_PERF_COMPILE_OPTS ${KARTEND_LTO_FLAG} -DQT_NO_DEBUG)
    set(RELEASE_PERF_LINK_OPTS ${KARTEND_LTO_FLAG} -Wl,--gc-sections -Wl,--as-needed)
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
      -O3 -march=native ${KARTEND_LTO_FLAG}
      -fomit-frame-pointer -funroll-loops
      -fno-semantic-interposition -ftree-vectorize
      -DQT_NO_DEBUG
    )
    set(RELEASE_PERF_LINK_OPTS ${KARTEND_LTO_FLAG} -Wl,-O3 -Wl,--gc-sections -Wl,--as-needed)
  endif()

  # Apple's ld rejects the GNU-ld perf options (-Wl,-O3 / --gc-sections /
  # --as-needed); keep only the portable LTO flag on macOS.
  if (APPLE)
    set(RELEASE_PERF_LINK_OPTS ${KARTEND_LTO_FLAG})
  endif()

  # lld is only compatible with Clang LTO objects; GCC LTO (GIMPLE) requires
  # the GNU linker or gold. Apple's default ld already handles Clang LTO, and
  # -fuse-ld=lld is unsupported there, so skip it on macOS.
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT APPLE)
    list(PREPEND RELEASE_PERF_LINK_OPTS -fuse-ld=lld)
  endif()
endif()

# LTO link options for every consumer (main exe + integration tests + the
# small tests/test_*.cpp binaries that link a subset of the per-area libs):
# LTO objects in the per-area OBJECT libs are unreadable to a non-LTO link
# step, so the matching LTO + linker flags must propagate. With the
# kartend_lib INTERFACE aggregator deleted (Kartend-w1wv), these hang off
# kartend_utils's INTERFACE_LINK_OPTIONS — every per-area lib PUBLIC-links
# kartend_utils per the DAG in src/CMakeLists.txt, so any consumer linking
# ANY area lib transitively inherits these flags. Tested under clang-18
# Release where small test binaries link direct area-lib .o subsets
# (Kartend-eyzv).
if (MSVC)
  # /GL is on the compile side via RELEASE_PERF_COMPILE_OPTS above; /LTCG
  # is the matching link-side flag and must propagate to every test exe
  # that consumes the LTO'd OBJECT libs.
  set(KARTEND_LTO_LINK_OPTIONS
    $<$<CONFIG:Release>:/LTCG>
  )
else()
  set(KARTEND_LTO_LINK_OPTIONS
    $<$<CONFIG:Release>:${KARTEND_LTO_FLAG}>
  )
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT APPLE)
    list(APPEND KARTEND_LTO_LINK_OPTIONS $<$<CONFIG:Release>:-fuse-ld=lld>)
  endif()
endif()

# ── Per-area flag application (single source of truth) ────────────────────────
# kartend_apply_area_flags(<lib> [<lib> ...])
#
# Applies, to each named area OBJECT lib, the full uniform compile-option set
# that the root file previously spread across six hand-written
# `foreach(_area ${KARTEND_AREA_LIBS})` loops:
#   1. warning flags (PRIVATE — warnings shouldn't leak through
#      INTERFACE_COMPILE_OPTIONS to external consumers like Qt autogen units)
#   2. RISC-V extern-access intent (GCC -mno-direct-extern-access / Clang
#      analogue)
#   3. hardening (non-Debug) + release-perf (Release / RelWithDebInfo) compile
#      opts
#   4. NDEBUG in Release
#   5. -Werror / /WX in maintenance builds
#   6. PGO generate/use compile opts when KARTEND_USE_PGO is set
#
# Collapsing the six loops into one call means a flag added here lands on
# EVERY area lib or none — it can no longer be applied to five and silently
# skipped on the sixth (the build-correctness footgun in Kartend-9fdsd).
#
# NOTE: the per-area-uniform compile options live here; the target-SPECIFIC
# applications the loops were interleaved with (the kartend executable's
# link options, kartend_utils PUBLIC defines/links, the PGO link options on
# the kartend exe) stay inline in the root — they are single-target, not a
# per-area sweep, so they don't belong in this helper.
function(kartend_apply_area_flags)
  foreach(_area ${ARGN})
    # 1. Warnings.
    # -Wshadow + -Wimplicit-fallthrough added in Kartend-bxfv as the first
    # pair of higher-floor warnings the codebase is already clean against.
    # Additional candidates (-Wcast-align, -Wformat=2, -Wnull-dereference,
    # -Wdouble-promotion, -Wold-style-cast, -Wnon-virtual-dtor) need their
    # own follow-up sweep before enabling.
    # MSVC: /W4 is the closest analogue to -Wall -Wextra; /permissive- enforces
    # standards conformance (no Microsoft extensions). Specific MSVC noise
    # (C4127 from Qt's signal/slot macros, etc.) is left untouched until it
    # actually fires — same don't-suppress-blindly stance as the GCC list.
    if (MSVC)
      target_compile_options(${_area} PRIVATE /W4 /permissive-)
    else()
      target_compile_options(${_area} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wimplicit-fallthrough)
    endif()

    # 2. Keep GCC-only intent with Clang analogue on RISC-V.
    if (NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "[Rr][Ii][Ss][Cc][Vv]")
      if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${_area} PRIVATE -mno-direct-extern-access)
      elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${_area} PRIVATE -fno-direct-access-external-data)
      endif()
    endif()

    # 3. Hardening (non-Debug) + release-perf options so the generated .o
    # files participate in LTO and pick up the same security flags as the
    # main exe link. RELEASE_PERF (LTO + QT_NO_DEBUG + --gc-sections/--as-needed;
    # no -march, so portable) also applies to RelWithDebInfo so the
    # --relwithdebinfo "for profiling" build measures a binary representative
    # of the shipped Release rather than an un-LTO'd, asserts-on one.
    target_compile_options(${_area} PRIVATE
      $<$<NOT:$<CONFIG:Debug>>:${HARDENING_COMPILE_OPTS}>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:${RELEASE_PERF_COMPILE_OPTS}>
    )

    # 4. PGO compile opts. Ordered here (after hardening/perf, before NDEBUG)
    # to mirror the original root file's loop sequence. Applied via
    # target_compile_options (genex-guarded) rather than list(APPEND) text
    # substitution — the latter would be dead because RELEASE_PERF_* was
    # already substituted into the flags above. MSVC PGO (/GENPROFILE
    # //USEPROFILE) is rejected up front in the root.
    if (KARTEND_USE_PGO AND NOT MSVC)
      if (KARTEND_PGO_GENERATE)
        target_compile_options(${_area} PRIVATE
          $<$<CONFIG:Release>:-fprofile-generate=${KARTEND_PGO_PROFILE_DIR}>)
      elseif (KARTEND_PGO_USE)
        target_compile_options(${_area} PRIVATE
          $<$<CONFIG:Release>:-fprofile-use=${KARTEND_PGO_PROFILE_DIR}>
          $<$<CONFIG:Release>:-fprofile-correction>)
      endif()
    endif()

    # 5. NDEBUG in Release.
    target_compile_definitions(${_area} PRIVATE $<$<CONFIG:Release>:NDEBUG>)

    # 6. KARTEND_MAINTENANCE adds -Werror (or /WX on MSVC).
    if (KARTEND_MAINTENANCE)
      if (MSVC)
        target_compile_options(${_area} PRIVATE /WX)
      else()
        target_compile_options(${_area} PRIVATE -Werror)
      endif()
    endif()
  endforeach()
endfunction()
