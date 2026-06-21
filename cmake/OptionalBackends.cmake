# Optional-backend discovery: gamepad (Qt6::Gamepad / SDL2 fallback), zstd,
# libarchive, and QtKeychain — including the MSVC FetchContent + header-shim
# block for QtKeychain.
#
# Extracted verbatim from the root CMakeLists.txt (Kartend-9fdsd) to keep the
# root file navigable. Behaviour-preserving: this module is include()d from
# the root at the same point the blocks used to live, in the same directory
# scope, so every find_package / pkg_check_modules result, the
# KARTEND_LIBARCHIVE_TARGET normalization, and the Qt6Keychain::Qt6Keychain
# alias land in exactly the same variables/targets as before.
#
# Consumes (set in the root before include): KARTEND_MIN_QT_VERSION,
# KARTEND_ENABLE_QT_GAMEPAD, KARTEND_ENABLE_SDL2_GAMEPAD, KARTEND_ENABLE_ZSTD,
# KARTEND_ENABLE_LIBARCHIVE. PkgConfig is discovered here.
# Produces: PkgConfig::SDL2 / PkgConfig::ZSTD / PkgConfig::LIBARCHIVE targets
# (when found), KARTEND_LIBARCHIVE_TARGET, Qt6Gamepad_FOUND, and the
# Qt6Keychain::Qt6Keychain target/alias.

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

# Optional: libarchive for in-process archive-member hashing (DAT auditor +
# scraper). Falls back to shelling out to 7z/unzip/bsdtar when not found.
# Two discovery routes feed one normalized KARTEND_LIBARCHIVE_TARGET:
#   1. pkg-config — Linux apt's libarchive-dev ships libarchive.pc, so this is
#      the route every Linux CI/dev build takes.
#   2. find_package(LibArchive) — CMake's bundled module, used when pkg-config
#      can't see libarchive: MSVC/vcpkg has no libarchive.pc on the default
#      search path (vcpkg's toolchain exposes it through FindLibArchive's
#      cmake-wrapper instead), and macOS reaches the SDK/system libarchive the
#      same way. Tried second so the pkg-config IMPORTED_TARGET wins where both
#      exist.
if (KARTEND_ENABLE_LIBARCHIVE)
  if (PkgConfig_FOUND)
    pkg_check_modules(LIBARCHIVE QUIET IMPORTED_TARGET libarchive)
  endif()
  if (TARGET PkgConfig::LIBARCHIVE)
    set(KARTEND_LIBARCHIVE_TARGET PkgConfig::LIBARCHIVE)
  else()
    find_package(LibArchive QUIET)
    if (LibArchive_FOUND)
      set(KARTEND_LIBARCHIVE_TARGET LibArchive::LibArchive)
    endif()
  endif()
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

# MSVC fallback: install-qt-action doesn't ship Qt6Keychain and the
# vcpkg port's transitive Qt6 dep collides with install-qt-action's Qt
# (a single CMake project can't reconcile two Qt6 trees). FetchContent
# builds upstream qtkeychain 0.15.x against the already-discovered Qt6
# instead — small build cost (~30s on Windows CI), no extra runtime
# deps (the Windows backend uses Win32 Credential Manager APIs from
# advapi32 directly), and the resulting target aliases cleanly to the
# Qt6Keychain::Qt6Keychain name the src/CMakeLists conditional expects.
# Gated to MSVC (not generic WIN32) because qtkeychain's CMakeLists
# adds MSVC-syntax `/utf-8` unconditionally — MinGW chokes on it.
# MinGW Windows builds can still pick qtkeychain up via find_package
# if the host installs a mingw qtkeychain package.
if (MSVC AND NOT TARGET Qt6Keychain::Qt6Keychain)
  include(FetchContent)
  FetchContent_Declare(qtkeychain
    GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
    # Upstream switched off the v-prefix between v0.14.0 and 0.15.0.
    # 0.15.0 (ad7344c) is recent enough to have current Qt6 fixes and
    # old enough to be well-shaken-down by downstreams.
    # Manual-bump pin: FetchContent tags are outside Dependabot's ecosystems
    # (it watches Actions + pip), and this touches credential storage — review
    # for a newer tag during the quarterly dependency sweep.
    GIT_TAG 0.15.0
    GIT_SHALLOW TRUE
  )
  # Upstream toggles. BUILD_WITH_QT6 picks the Qt6 build path; the rest
  # trim payload (we don't ship qtkeychain's tests, translations, or
  # public installs — it's purely a static-ish dep linked into kartend).
  set(BUILD_WITH_QT6     ON  CACHE BOOL "" FORCE)
  set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
  set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
  set(QTKEYCHAIN_STATIC  ON  CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(qtkeychain)
  # Upstream exports the build target as `qt6keychain` (lowercase).
  # Bridge to the namespaced alias the rest of the codebase queries
  # for via TARGET Qt6Keychain::Qt6Keychain.
  if (TARGET qt6keychain AND NOT TARGET Qt6Keychain::Qt6Keychain)
    # Header layout shim: the installed Linux qtkeychain-qt6-dev package
    # puts keychain.h under <prefix>/include/qt6keychain/, so the
    # codebase #include is <qt6keychain/keychain.h>. Upstream's source
    # tree has the file at qtkeychain/keychain.h — FetchContent doesn't
    # run the install step, so the qt6keychain/ namespace dir doesn't
    # exist anywhere in the include search path. Stage the header into
    # a generated qt6keychain/ subdir of the build tree and push that
    # location onto qt6keychain's INTERFACE_INCLUDE_DIRECTORIES so
    # every consumer that links against the alias picks the right
    # search path up automatically.
    set(_qkc_shim_dir "${CMAKE_CURRENT_BINARY_DIR}/qtkeychain-include-shim")
    file(MAKE_DIRECTORY "${_qkc_shim_dir}/qt6keychain")
    configure_file(
      "${qtkeychain_SOURCE_DIR}/qtkeychain/keychain.h"
      "${_qkc_shim_dir}/qt6keychain/keychain.h"
      COPYONLY
    )
    # $<BUILD_INTERFACE:...> keeps the path out of any export/install
    # surface — qt6keychain's CMakeLists doesn't install in this
    # FetchContent path, but the generator-expression wrap is what
    # CMake requires for any build-tree-only include directory.
    target_include_directories(qt6keychain INTERFACE
      "$<BUILD_INTERFACE:${_qkc_shim_dir}>")
    unset(_qkc_shim_dir)

    add_library(Qt6Keychain::Qt6Keychain ALIAS qt6keychain)
  endif()
endif()
