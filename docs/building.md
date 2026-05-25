# Building

> See [Build modes](#build-modes) below for the full flag matrix
> (`--debug`, `--maintenance`, `--sanitize`, …).

## Dependencies

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
- **vcpkg** in manifest mode — point at the [`vcpkg.json`](../vcpkg.json) at the repo root via `CMAKE_TOOLCHAIN_FILE`; SDL2 and zstd auto-install on the first cmake configure.

Qt6Keychain is not currently wired up on Windows (vcpkg's port collides
with `install-qt-action`'s Qt6 — see the open follow-up). The scraper
credential layer falls back to plaintext-INI in `%APPDATA%/kartend` when
it can't find QtKeychain, same as a Linux build without
`qtkeychain-qt6-dev` installed.

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
| `--apply-fixes` | Apply safe clang-tidy auto-fixes in-place. | Cleaning up after a large refactor when the lint report is repetitive. | **Modifies tracked source files**; the auto-fixer has been known to mangle `src/utils/collectionutils.h` and headers under `src/ui/uiconstants/` — inspect `git diff` after. |
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

