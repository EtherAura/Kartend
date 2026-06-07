# Building

> See [Build modes](#build-modes) below for the full flag matrix
> (`--debug`, `--maintenance`, `--sanitize`, …).

## Requirements

- **CMake 3.20+**
- **C++23 compiler** — Clang 16+ or GCC 12+
- **Qt 6.4 LTS or later** — CI pins Qt 6.4.2; newer versions build
  fine, but APIs introduced after 6.4 (`QDateTime::setTimeSpec`,
  `QTimeZone::UTC`, `QHash::insert(key, std::move(value))` with real
  move semantics) are off-limits so the code stays portable to the
  pinned CI Qt.

Required Qt components: `Core Gui Widgets Sql Concurrent Multimedia
MultimediaWidgets Network LinguistTools`. Optional: `Gamepad` (auto-
detected; SDL2 is the fallback gamepad backend), `Qt6Keychain` (auto-
detected; falls back to plaintext-INI credential storage when absent).

Cold release builds take roughly **5–10 minutes** on modern hardware
the first time through; ccache-hit incrementals finish in ~30 seconds.

## Dependencies

### Arch / Manjaro

```bash
sudo pacman -S cmake ninja clang lld ccache \
  qt6-base qt6-multimedia qt6-tools qt6-svg qtkeychain-qt6
```

Optional gamepad backends: `qt6-gamepad` (if available in your repos)
or `sdl2`.

### Fedora

```bash
sudo dnf install cmake ninja-build clang lld ccache \
  qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qttools-devel \
  qt6-qtsvg-devel qt5-qt5compat-devel qtkeychain-qt6-devel
```

Optional gamepad backend: `SDL2-devel`.

### Debian / Ubuntu

```bash
sudo apt install clang cmake lld ninja-build ccache \
  qt6-base-dev qt6-multimedia-dev libqt6sql6-sqlite \
  qt6-tools-dev qt6-l10n-tools qt6keychain-dev
```

Optional gamepad backend: `libsdl2-dev`.

### Maintenance / lint tooling (optional)

`.scripts/build.sh --maintenance` (CONTRIBUTING step 4) runs the static-analysis
gate. Each tool **self-skips if absent** and the run prints a ran-vs-skipped
summary at the end — so install whatever you want enforced locally. Otherwise
`--maintenance` returns success while running nothing and drift surfaces only in
CI.

- **clang-format** — pinned to **v19** to match CI. Install `clang-format-19`
  (e.g. from [apt.llvm.org](https://apt.llvm.org) on Debian/Ubuntu) or run the
  check through the `kartend-ci` container (see [ci-local.md](ci-local.md)). The
  build script no-ops if no v19 binary is found, so a newer system
  `clang-format` won't reformat against a different style. This is the one check
  CI fails hard on.
- **clang-tidy** — ships with the LLVM/Clang tooling (`clang-tools-extra` on
  Fedora, `clang-tools` on Debian/Ubuntu).
- **cppcheck** — package `cppcheck` on all distros.
- **include-what-you-use** — package `include-what-you-use`; provides the
  `iwyu_tool` wrapper the gate invokes.

### Gentoo

```bash
emerge --ask dev-build/cmake dev-build/ninja llvm-core/clang llvm-core/lld \
  dev-qt/qtbase:6 dev-qt/qtmultimedia:6 dev-qt/qttools:6 dev-util/ccache
```

### Windows (MSVC)

Native Windows builds use **MSVC 2019 or 2022 + Qt 6.7 LTS + Ninja**.
The CI job pulls Qt through `install-qt-action` and the optional
native deps (SDL2 gamepad backend, zstd compression) through vcpkg —
both are reproducible locally:

- **Qt 6.7 LTS** — install via the official Qt online installer
  (component `Qt 6.7.x` → `MSVC 2019 64-bit`), or via [`aqtinstall`](https://github.com/miurahr/aqtinstall):
  `pip install aqtinstall && aqt install-qt windows desktop 6.7.3 win64_msvc2019_64 --modules qtmultimedia qtimageformats`.
- **Visual Studio 2022 Build Tools** with the `Desktop development with C++` workload (provides MSVC, the Windows SDK, and Ninja).
- **vcpkg** in manifest mode — point at the [`vcpkg.json`](../../vcpkg.json) at the repo root via `CMAKE_TOOLCHAIN_FILE`; SDL2 and zstd auto-install on the first cmake configure.

Qt6Keychain is wired up on MSVC builds via `FetchContent` against
upstream `frankosterfeld/qtkeychain` 0.15.x — install-qt-action
doesn't ship it and vcpkg's port pulls in a second Qt6 that collides
with the install-qt-action one, so the project builds it itself
against the already-discovered Qt6 instead. Scraper credentials end
up in the Windows Credential Manager via QtKeychain's
`WriteCredentialW` backend (matching the Linux behaviour with
`qtkeychain-qt6-dev` installed). MinGW Windows builds still take the
plaintext-INI fallback because qtkeychain's CMakeLists adds the
MSVC-only `/utf-8` flag unconditionally; the fallback path is the
same one Linux uses without `qtkeychain-qt6-dev` installed.

`.scripts/build.sh` is bash-only; Windows contributors invoke
`cmake` directly per [Manual Build](#manual-build) below. The
`.scripts/build-windows-cross.sh` helper cross-compiles to Windows from
Linux via Docker (Fedora `mingw64-qt6`) for fast iteration without a
Windows host — useful for spotting portability bugs early, but the
release artifact ships from CI's MSVC build.

## Build modes

The build script (`.scripts/build.sh`) is the canonical entry point for
every build. The table below enumerates **every** flag it parses, what it
does, when to reach for it, and what it leaves behind.

### Build-type flags

| Flag | Purpose | When to use | Side effects |
|------|---------|-------------|--------------|
| *(none)* | Release build. | Default for routine work and CI. | Writes `build/<gen>-release/`; honors `ccache`. |
| `--debug` | Debug build (keeps `qDebug`/`qWarning` output, no `-O`, symbols on). | Local development; reproducing a runtime bug; before stepping in a debugger. | Writes `build/<gen>-debug/`; emits a linker map at `.backups/reports/kartend.map`; binaries are large and unoptimized. |
| `--relwithdebinfo` | Release-with-debug-symbols build. | Profiling a release binary; producing a perf/`gdb`-friendly artifact without the cost of `--debug`. | Writes `build/<gen>-relwithdebinfo/`. |
| `--sanitize` (alias `--sanitizers`) | Debug build + ASan/UBSan instrumentation. | Investigating a use-after-free, uninit-read, signed-overflow, or undefined-behavior crash. **Not** for routine work — binaries run noticeably slower. | Writes `build/<gen>-sanitize/`; runtime ~2–3× slower; sanitizer reports go to stderr. |
| `--maintenance` | Release build + static-analysis pipeline (clang-tidy, cppcheck, IWYU, clang-format). | Pre-push hygiene; CI-style lint pass. | Writes `build/<gen>-maintenance/`; lint reports go to `build/<gen>-maintenance/logs/`; **enables `-Werror`** — warnings break the build. |
| `--coverage` | gcov-instrumented build. **Implies `--debug --tests`.** | Measuring test coverage; investigating uncovered branches. | Writes `.gcno`/`.gcda` files into the build dir; binaries run slower; coverage is meaningless unless you also pass `--run-tests` to populate counters. |
| `--pgo` | Two-pass PGO: instrumented build, runs the binary to collect a profile, rebuilds optimized against the profile. | Releasing a tuned binary; before measuring final perf. | Writes `build/<gen>-release-pgo/` and `build/<gen>-release-pgo/pgo_profiles/`; wipes the profile dir before the generate pass so stale `.profraw` from a previous run can't bias the optimised build; first pass binary is slow (instrumented). |
| `--pgo-generate` | Just the PGO generate pass (instrumented build, no second pass). | Manual two-pass workflow when you want to control profile collection yourself. | Same dir as `--pgo`; wipes `pgo_profiles/` before configure so the upcoming user run produces only fresh profiles; leaves an instrumented binary. |
| `--pgo-use` | Just the PGO use pass (consumes existing profile in the build dir's `pgo_profiles/`). | Companion to `--pgo-generate` after you've collected a profile. | Same dir as `--pgo`; fails if the profile dir is empty. Does NOT wipe — that's the generate pass's job. |

### Lint / format flags (require `--maintenance`)

| Flag | Purpose | When to use | Side effects |
|------|---------|-------------|--------------|
| `--apply-fixes` | Apply safe clang-tidy auto-fixes in-place. | Cleaning up after a large refactor when the lint report is repetitive. | **Modifies tracked source files**; the auto-fixer has been known to mangle headers under `src/utils/uiconstants/` — inspect `git diff` after. |
| `--format-check` | clang-format dry-run; non-zero exit if anything would change. | CI gate; pre-push verification. | No file writes. |
| `--format-apply` | clang-format in-place. | Routine pre-push cleanup. | **Modifies tracked source files**. Requires clang-format 19 on PATH (the system v21 drifts). |

### Test flags

| Flag | Purpose | When to use | Side effects |
|------|---------|-------------|--------------|
| `--tests` | Configure with `-DKARTEND_BUILD_TESTS=ON`. | Whenever you need a test binary to exist. | Builds the test executables; no test execution by itself. |
| `--run-tests` | Run `ctest` after a successful build. Requires `--tests`. | Routine "did I break anything" check after edits. | Executes the full suite; test logs go to `build/<gen>-*/Testing/`. |

### Install flags

| Flag | Purpose | When to use | Side effects |
|------|---------|-------------|--------------|
| `--install` | `cmake --install` after the build. | Installing a system-wide build. | **Auto-elevates with `sudo`/`doas`** when the prefix needs root; honors `DESTDIR`. |
| `--uninstall` | Run the `uninstall` target on the most recent build dir. | Reverting a `--install`. | Reads `install_manifest.txt`; **deletes installed files**. |
| `--prefix=PATH` | `-DCMAKE_INSTALL_PREFIX=PATH`. | Non-default install target (e.g. `~/.local`). | Only affects subsequent `--install`. |

### Generator / build-dir flags

| Flag | Purpose | When to use | Side effects |
|------|---------|-------------|--------------|
| `--ninja` | Force Ninja generator. | When Ninja is installed but you want to be explicit. | Build dir prefix becomes `build/ninja-*`. |
| `--make` | Force Unix Makefiles generator. | Ninja missing or incompatible. | Build dir prefix becomes `build/make-*`. |
| `--incremental` | Reuse existing build dir (default). | Default. | None — describes the default. |
| `--clean` | `rm -rf` the build dir before configuring. | Suspected stale cache; after large `CMakeLists.txt` edits. | **Deletes the build directory.** |
| `--keep-builds` | Don't prune other script-created build dirs. | Keeping `--debug` and `--release` dirs side-by-side. | Skips the auto-prune that runs by default. |
| `--jobs=N` | Override build parallelism (default: `nproc`). | Constrained CI agents; avoiding OOM on big TUs. | None. |

### Reporting / dependency flags

| Flag | Purpose | When to use | Side effects |
|------|---------|-------------|--------------|
| `--archive` | Create a source `.tar.gz` under `.backups/`. | Producing a release artifact. | Writes `.backups/kartend-<version>.tar.gz`. |
| `--reports` | Assemble source/UI reports under `.backups/reports/`. | Generating diagnostic bundles. | Writes `.backups/reports/`. |
| `--no-ccache` | Disable `ccache` even if installed. | Debugging compiler-cache misses; suspected stale cached objects. | First build is slower; subsequent builds are slower than the ccache-enabled default. |
| `--clang` | Force Clang/LLD for release builds (default: system compiler). | Reproducing a Clang-only warning/CI failure. | None beyond toolchain selection. |

### Output Directories

Build directories are separated by generator to avoid CMake generator mismatch issues:

- Ninja builds: `build/ninja-<mode>` (default when Ninja is available)
- Make builds: `build/make-<mode>` (when using `--make` or when Ninja is unavailable)

Modes include: `release`, `debug`, `sanitize`, `maintenance`, `release-pgo`.

### Default Behavior Notes

- Incremental builds are the default (the script will not delete the build directory unless you pass `--clean`).
- Unit tests are opt-in: tests are only configured/built when you pass `--tests`.
- Reports and source archives are off by default; use `--reports` and/or `--archive` to opt in.
- The script prunes other script-created build directories by default; use `--keep-builds` to keep multiple build dirs. "Script-created" means dirs that contain either the `.kartend-build-dir` marker file (left by the script on first prepare) OR a `CMakeCache.txt` (left by any CMake invocation). Hand-rolled scratch dirs under `build/` that contain neither are left alone. Stray `build/*.log` files are also swept on each run.

### Examples

```bash
# Debug build for development
.scripts/build.sh --debug

# Fast dev loop (no reports/archive)
.scripts/build.sh --debug --tests --run-tests

# Release build + tests + run the full suite
.scripts/build.sh --tests --run-tests

# Keep both debug and release build directories
.scripts/build.sh --debug --keep-builds

# Maintenance build with all checks
.scripts/build.sh --maintenance

# Maintenance with auto-fixes
.scripts/build.sh --maintenance --apply-fixes --format-apply

# PGO optimized build (automated two-pass)
.scripts/build.sh --pgo

# Force a clean rebuild (removes the build dir)
.scripts/build.sh --clean
```

## Manual Build

For manual CMake builds without the script:

```bash
# Configure (recommended: Ninja)
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build/ninja-release --parallel $(nproc)

# Run
./build/ninja-release/kartend
```

### Windows (MSVC + Ninja + vcpkg)

From a **Developer Command Prompt for VS 2022** (so `cl.exe` and
`ninja.exe` are on `PATH`) with `Qt6_DIR` pointing at the
`...\6.7.x\msvc2019_64\lib\cmake\Qt6` directory (or `QT_ROOT_DIR`
exported by `aqtinstall`):

```powershell
# Configure — Ninja over the Visual Studio generator because msbuild
# can race AUTOUIC vs. header compilation. The vcpkg toolchain pulls
# SDL2 and zstd from vcpkg.json on first configure.
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DKARTEND_PORTABLE_RELEASE=ON `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build --parallel

# Bundle Qt + plugin DLLs next to the exe so it runs anywhere
mkdir dist; Copy-Item build\kartend.exe dist\
windeployqt --release --no-translations dist\kartend.exe

# Copy the vcpkg-built optional DLLs (windeployqt only walks Qt's)
Copy-Item build\vcpkg_installed\x64-windows\bin\SDL2.dll dist\
Copy-Item build\vcpkg_installed\x64-windows\bin\zstd.dll dist\

# Run
.\dist\kartend.exe
```

Add `-DKARTEND_BUILD_TESTS=ON` and `ctest --test-dir build` to run the
test suite. A few tests need Windows-specific portability work and are
currently excluded in CI (see the build.yml's `windows-build-test`
job for the up-to-date exclude list).

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | — | `Release`, `Debug`, or `RelWithDebInfo` |
| `KARTEND_MAINTENANCE` | `OFF` | Enable `-Werror` for CI/maintenance builds |
| `KARTEND_BUILD_TESTS` | `OFF` | Build unit test executables |
| `KARTEND_ENABLE_CCACHE` | `ON` | Use `ccache` if available (faster rebuilds) |
| `KARTEND_ENABLE_SANITIZERS` | `OFF` | Enable ASan+UBSan (requires `Debug`; configure errors otherwise) |
| `KARTEND_ENABLE_COVERAGE` | `OFF` | Enable gcov/lcov instrumentation (Debug only) |
| `KARTEND_PORTABLE_RELEASE` | `OFF` | Drop `-march=native`/`-O3`/fast-math for distro packaging; keeps LTO + hardening |
| `KARTEND_LINKER_MAP` | `OFF` | Emit `kartend.map` next to `.backups/reports/` in Debug builds |
| `KARTEND_USE_PGO` | `OFF` | Enable Profile-Guided Optimization |
| `KARTEND_PGO_GENERATE` | `OFF` | Generate PGO profile data |
| `KARTEND_PGO_USE` | `OFF` | Use existing PGO profile data |
| `KARTEND_PGO_PROFILE_DIR` | `build/pgo_profiles` | Directory for PGO profile data |

Example with options:

```bash
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DKARTEND_BUILD_TESTS=ON -DKARTEND_MAINTENANCE=ON
```

### ccache

If `ccache` is installed, the project will automatically use it by default.

To explicitly disable it (e.g., for debugging compiler issues):

```bash
cmake ../.. -DKARTEND_ENABLE_CCACHE=OFF
```

## Debug Build

```bash
cmake -S . -B build/ninja-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ninja-debug --parallel $(nproc)
```

Debug builds include symbols and generate a linker map file at `.backups/reports/kartend.map`.

## Ninja Support

If Ninja is installed, the build script will use it by default for faster builds.

Recommended manual Ninja build (separate directory):

```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARTEND_BUILD_TESTS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/ninja-release --parallel $(nproc)
ctest --test-dir build/ninja-release --output-on-failure
```

## Profile-Guided Optimization (PGO)

PGO builds optimize the binary based on actual runtime behavior. The `--pgo` flag automates the two-pass process:

1. **Generate pass**: Builds with instrumentation, runs the application to collect profile data
2. **Use pass**: Rebuilds using the collected profile for optimized code paths

For manual PGO:

```bash
# Pass 1: Generate profile data
cmake -S . -B build/ninja-pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARTEND_USE_PGO=ON -DKARTEND_PGO_GENERATE=ON
cmake --build build/ninja-pgo --parallel $(nproc)
# Run the application to generate profile data...

# Pass 2: Use profile data
cmake -S . -B build/ninja-pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARTEND_USE_PGO=ON -DKARTEND_PGO_USE=ON
cmake --build build/ninja-pgo --parallel $(nproc)
```

## Linting Configuration

The maintenance build runs several static analysis tools. Configuration files in the project root customize their behavior:

### clang-tidy (`.clang-tidy`)

Configured to focus on actionable warnings, disabling noisy style checks:

- **Enabled**: `clang-analyzer-*`, `bugprone-*`, `performance-*`, `modernize-*`, `readability-*`, `misc-*`
- **Disabled**: Trailing return types, identifier naming/length, magic numbers, braces around statements, and other style-only checks

### IWYU (`.iwyu.imp`)

Include-What-You-Use mapping file for Qt6. Maps Qt internal headers to public equivalents to reduce false positives.

**Note**: IWYU with Qt is known to be noisy. "Remove" suggestions are more reliable than "add" suggestions. The "add" suggestions often reference Qt internal headers that should not be included directly.

### cppcheck

Uses built-in checks with `--enable=all`. Focuses on real bugs, memory issues, and performance problems.

### Linting Logs

Maintenance build logs are written to `build/ninja-maintenance/logs/`:

| Log File | Tool | Priority |
|----------|------|----------|
| `cppcheck.log` | cppcheck | High - real bugs |
| `clang-tidy.log` | clang-tidy | Medium - code quality |
| `iwyu.log` | IWYU | Low - verify before applying |
| `clang-format.log` | clang-format | Style only |

## Reproducing CI Locally

The full GitHub Actions pipeline (`build.yml` + `coverage.yml`) can be
run on your own machine via [`act`](https://github.com/nektos/act), which
executes each workflow inside a Docker container that emulates a
GitHub-hosted ubuntu-24.04 runner. This catches CI breakage before
pushing and is much faster than waiting for a remote run.

### Prerequisites

- Docker (or Podman with a `docker` shim).
- `act` **0.2.86 or newer** — earlier versions don't support the Node 24
  runtime that `actions/cache@v5` requires. Distros tend to ship older
  builds; install upstream if needed:

  ```bash
  curl -sL https://github.com/nektos/act/releases/download/v0.2.88/act_Linux_x86_64.tar.gz \
    | tar -xz -C "$HOME/.local/bin" act
  ```

The repo ships an `.actrc` that pins:

- The runner image (`ghcr.io/catthehacker/ubuntu:full-24.04`) so installed
  packages, locale, and pre-shipped tooling match what `runs-on:
  ubuntu-24.04` actually provides on GHA.
- Container resource caps (`--cpus=4 --memory=16g`) matching GHA's
  ubuntu-24.04 runner spec, so timing-dependent issues (TSan races,
  flaky tests) surface at the same rate they do on CI.
- Artifact server path (`/tmp/act-artifacts`) so `actions/cache@v5`
  persists ccache state across `act` invocations.

### Usage

The wrapper script `.scripts/ci-local.sh` provides friendly subcommands:

```bash
.scripts/ci-local.sh                 # full pipeline, sequential (~1-1.5 hr first run)
.scripts/ci-local.sh build           # all four matrix cells (Release/Debug × gcc/clang)
.scripts/ci-local.sh build:rel:gcc   # one matrix cell
.scripts/ci-local.sh no-zstd         # build-no-zstd job
.scripts/ci-local.sh asan            # sanitizers (ASan/UBSan) job
.scripts/ci-local.sh tsan            # thread-sanitizer job
.scripts/ci-local.sh coverage        # coverage job
.scripts/ci-local.sh tidy            # maintenance-check (clang-tidy/format/cppcheck/IWYU)
.scripts/ci-local.sh list            # show available jobs
.scripts/ci-local.sh shell           # interactive container with the build environment
.scripts/ci-local.sh -- <act args>   # passthrough, e.g. -- -j build --verbose
```

The first `act` invocation pulls the ~17GB runner image (one-time);
subsequent runs reuse it. With `--reuse` enabled (default in `.actrc`),
apt-installed packages and the ccache volume survive between
invocations, so a re-run of any single job typically takes minutes
rather than tens of minutes.

### Known divergences from real CI

A few things deliberately don't reproduce locally and shouldn't worry
you when they fail in `act`:

- **`coverage` upload step**: `actions/upload-artifact@v7` sends a
  `mime_type` field that act's mock artifact server doesn't recognize
  (`Error decode request body: proto: ... unknown field "mime_type"`).
  Coverage measurement, floor enforcement, and HTML generation succeed;
  only the upload at the end fails. Real GHA uses GitHub's actual
  artifact API — works fine there.
- **Cross-runner ccache pollution**: GHA can schedule a build on one
  Azure VM and run the cached artifact on another with a different CPU.
  That class of failure (the AVX-512 SIGILL we saw historically) cannot
  be reproduced with `act` because everything runs on your single host
  CPU. Mitigated permanently by `KARTEND_PORTABLE_RELEASE=ON` in CI's
  Release builds.

## Windows MSVC pitfalls

The Linux CI matrix and `.scripts/build-windows-cross.sh` (Fedora
`mingw64-qt6` cross-compile) catch most portability bugs, but the
**MSVC** build is its own animal — `act` can't reproduce
`windows-latest` (Windows containers don't run on Linux hosts), the
MSVC compiler isn't redistributable, and the MinGW cross-compile
deliberately skips MSVC-only code paths. The list below collects
every Windows-CI failure mode we've hit, with the symptom on the left
and the fix pattern on the right.

| Symptom in CI | Cause | Fix pattern |
|---|---|---|
| `error C1083: Cannot open include file: 'ui_*.h'` during the build step on `windows-latest` | Visual Studio generator's msbuild parallelizes header compilation before AUTOUIC has produced `ui_*.h`. | Use `-G Ninja` on Windows (matches every Linux job); ilammy/msvc-dev-cmd@v1 exports the MSVC env so `cl.exe` finds Ninja. |
| `'/utf-8': linker input file not found` on a third-party CMake build | Upstream's CMakeLists adds the MSVC-syntax `/utf-8` flag unconditionally; MinGW's `g++` rejects it. | Gate the dep to `if (MSVC)` rather than `if (WIN32)`, OR strip `/utf-8` from the target's compile options after `FetchContent_MakeAvailable`. |
| `Target X INTERFACE_INCLUDE_DIRECTORIES property contains path "<build>/..." which is prefixed in the build directory` at configure time | CMake refuses raw build-tree paths on exportable target interfaces. | Wrap the path: `target_include_directories(X INTERFACE "$<BUILD_INTERFACE:${path}>")`. |
| `'std::quick_exit' is not a member of 'std'` | C11 `quick_exit` is absent from MinGW's libstdc++ AND MSVC's STL — the Windows CRT has no `quick_exit`. | `std::_Exit(0)` (also C++11) — same effect for skipping atexit / destructors. |
| Linker can't resolve `SDL_main` on the Windows build | SDL2's `sdl2.pc` injects `-Dmain=SDL_main`, renaming `int main` at the preprocessor; the renamed function then gets C++ name mangling and `SDL2main`'s C-language `WinMain` can't find it. | Declare `extern "C" int main(...)` (no-op on POSIX since `main` already has C linkage). |
| MinGW's `ld` rejects `-Wl,-z,now` / `-Wl,-z,relro` / `-pie` / `-fPIE` / `_FORTIFY_SOURCE` | ELF-only hardening flags don't apply to PE/COFF. | Gate the ELF set behind `NOT WIN32` inside the non-MSVC branch (in `CMakeLists.txt`). Equivalent runtime mitigations (ASLR, DEP, high-entropy VA) are MSVC linker defaults or set via `/sdl /guard:cf`. |
| `QFileInfo::isExecutable()` returns false on a unix-perm-set fixture file | Windows decides executability by extension (`.exe`/`.bat`/`.cmd`/`.com`), not by a unix-style `ExeOwner` perm bit. | Create the test fixture with a `.bat` extension on Windows, with `@echo off\r\nexit /b 0\r\n` content; keep the POSIX shim on Linux via `#ifdef Q_OS_WIN`. |
| Test passes on Linux, mysteriously fails on Windows in the read path after a writer-test passes | `QTemporaryFile.close()` then re-open by name through another `QFile::open(ReadOnly)` lands against a pending delete or share lock the close didn't fully release. | Use `flush()` instead of `close()` and let the temp file's destructor clean up. Or use `QTemporaryDir` + a fixed `QFile` inside it (the pattern the matching writer test uses). |
| `NSIS error: invalid VIProductVersion format, should be X.X.X.X` | NSIS requires exactly 4 dotted integer components in `VIProductVersion`. `X.Y.Z` (3 ints) or pre-release suffixes (`X.Y.Z-rc1`) get rejected. | Always pad to 4: `VIProductVersion "${VERSION}.0"` for `0.0.10` semver; reject hyphenated pre-release tags entirely or strip the suffix before substitution. |
| `error C1083: Cannot open include file: 'qt6keychain/keychain.h'` after a `FetchContent_MakeAvailable` of a third-party Qt addon | The installed package layout (`<prefix>/include/qt6keychain/`) differs from the upstream source tree (`<repo>/qtkeychain/`). FetchContent doesn't run the install step, so the namespaced directory never materializes. | `configure_file` the header into a generated shim directory under the build tree, then push that dir onto the target's `INTERFACE_INCLUDE_DIRECTORIES` (wrapped in `$<BUILD_INTERFACE:>`). See the qtkeychain shim in `CMakeLists.txt`. |
| `qttools` / `qtsvg` / `qttranslations` "packages not found while parsing XML of package information" in `jurplel/install-qt-action` | These are part of the qtbase tarball, not optional addon modules. | Only list **actual** aqt addons in the `modules:` field (e.g., `qtmultimedia qtimageformats`). qtbase brings the rest. |

### What our local tooling does and doesn't catch

`.scripts/build-windows-cross.sh` (MinGW-w64 + Qt 6 via Fedora 41
mingw64 packages, in Docker) catches most of the **POSIX-incompatible
code** issues — `std::quick_exit`, ELF flag gating, the SDL2 main
hijack, missing-DLL build failures. It runs in under 15 minutes after
the first image build.

It deliberately doesn't catch anything inside `if (MSVC)` / MSVC-only
code paths, and the test suite isn't run under Wine because Wine
reproduction was unreliable (KartReader's actual failure mode at
real-MSVC ctest didn't reproduce under Wine — the test hung
instead).

The PR-side `windows-build-test` CI job on `windows-latest` is the
canonical signal. PR-side green is necessary but **not sufficient**
— conditional CMake (`if (MSVC AND NOT TARGET ...)`) and conditional
sources (`#ifdef KARTEND_HAVE_X`) can take a different path on
the post-merge main run than the PR head, so wait for the post-merge
main run to also go green before declaring a Windows change shipped.

### Higher-fidelity options (not currently in tree)

- **`clang-cl` cross-compile** — Clang with the MSVC-compatible
  driver can target the MSVC ABI without booting Windows. Catches
  most MSVC-flag-syntax issues (`/utf-8`, `BUILD_INTERFACE` checks)
  because `MSVC` evaluates true. **Gotcha**: needs the MSVC headers
  and libs to link against, which Microsoft doesn't redistribute.
  Projects that do this typically bundle their own MSVC installation
  via [`msvc-wine`](https://github.com/mstorsjo/msvc-wine), which
  requires a user-side install step and is fragile across Wine
  versions. Not worth the maintenance burden for a single-developer
  project unless MSVC iteration becomes a daily bottleneck.

- **Windows VM** (QEMU/libvirt with a Windows 11 evaluation image +
  MSVC Build Tools): highest fidelity, slowest dev loop. Setup is
  ~1 hour once; per-iteration is "boot VM, mount source, cmake
  configure, build, test." Genuinely the right tool for the
  remaining ctest failures (VideoUtils, ItemMetadata, ItemArtwork,
  CliArgs, QueryManagerBrokenSymlinks) — those fail at runtime on
  real Windows, which neither MinGW cross-compile nor `clang-cl`
  can simulate.

- **Two-stage Windows CI**: split the existing `windows-build-test`
  into a fast `configure` stage (~3 min, with strict CMake warnings
  promoted to errors) and a full build+test stage. Catches CMake-side
  issues (`BUILD_INTERFACE`, missing headers at configure time)
  before paying for the full Qt install + compile. Probably worth
  doing if we see another configure-time-only failure; the data so
  far is ~50/50 configure vs compile failures, so the saving is
  real but not huge.

