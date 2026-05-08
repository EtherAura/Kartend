# Building

## Dependencies

### Gentoo

```bash
emerge --ask dev-build/cmake dev-build/ninja llvm-core/clang llvm-core/lld \
  dev-qt/qtbase:6 dev-qt/qtmultimedia:6 dev-qt/qttools:6 dev-util/ccache
```

## Build Script Options

The build script (`.scripts/build.sh`) supports the following options:

| Flag | Description |
|------|-------------|
| *(default)* | Uses Ninja if available, uses incremental builds, uses `ccache` if installed |
| `--debug` | Debug build with symbols and linker map file |
| `--sanitize` | Debug build with ASan/UBSan enabled |
| `--maintenance` | Enables `-Werror`, runs clang-tidy, cppcheck, and code formatting checks |
| `--apply-fixes` | Auto-apply clang-tidy fixes (requires `--maintenance`) |
| `--format-check` | Check code formatting without applying changes (requires `--maintenance`) |
| `--format-apply` | Auto-apply clang-format fixes (requires `--maintenance`) |
| `--tests` | Configure with `-DKARTEND_BUILD_TESTS=ON` |
| `--run-tests` | Run `ctest` after a successful build (requires `--tests`) |
| `--pgo` | Two-pass Profile-Guided Optimization build |
| `--pgo-generate` | First PGO pass: generate profile data |
| `--pgo-use` | Second PGO pass: optimize using collected profile |
| `--ninja` | Force Ninja generator |
| `--make` | Force Unix Makefiles generator |
| `--incremental` | Reuse existing build directory (default) |
| `--clean` | Remove the build directory before configuring |
| `--keep-builds` | Keep other build directories (skip auto-prune) |
| `--reports` | Assemble reports into `.backups/reports` (off by default) |
| `--archive` | Create `.backups/*.tar.gz` source archives (off by default) |
| `--no-ccache` | Disable ccache even if installed |
| `--clang` | Force Clang/LLD toolchain for a release build (default: system compiler) |
| `--install` | Run `cmake --install` after build (auto-elevates with sudo/doas) |
| `--uninstall` | Remove files from the most recent install (reads `install_manifest.txt`) |
| `--prefix=PATH` | Pass `-DCMAKE_INSTALL_PREFIX=PATH` to configure |
| `--jobs=N` | Override `-j` parallelism for the build step (default: `nproc`) |

### Output Directories

Build directories are separated by generator to avoid CMake generator mismatch issues:

- Ninja builds: `build/ninja-<mode>` (default when Ninja is available)
- Make builds: `build/make-<mode>` (when using `--make` or when Ninja is unavailable)

Modes include: `release`, `debug`, `sanitize`, `maintenance`, `release-pgo`.

### Default Behavior Notes

- Incremental builds are the default (the script will not delete the build directory unless you pass `--clean`).
- Unit tests are opt-in: tests are only configured/built when you pass `--tests`.
- Reports and source archives are off by default; use `--reports` and/or `--archive` to opt in.
- The script prunes other script-created build directories by default; use `--keep-builds` to keep multiple build dirs.

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

