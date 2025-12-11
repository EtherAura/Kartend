# Building

## Build Script Options

The build script (`.scripts/build.sh`) supports the following options:

| Flag | Description |
|------|-------------|
| `--debug` | Debug build with symbols and linker map file |
| `--maintenance` | Enables `-Werror`, runs clang-tidy, cppcheck, and code formatting checks |
| `--apply-fixes` | Auto-apply clang-tidy fixes (requires `--maintenance`) |
| `--format-check` | Check code formatting without applying changes (requires `--maintenance`) |
| `--format-apply` | Auto-apply clang-format fixes (requires `--maintenance`) |
| `--pgo` | Two-pass Profile-Guided Optimization build |
| `--pgo-generate` | First PGO pass: generate profile data |
| `--pgo-use` | Second PGO pass: optimize using collected profile |

### Examples

```bash
# Debug build for development
.scripts/build.sh --debug

# Maintenance build with all checks
.scripts/build.sh --maintenance

# Maintenance with auto-fixes
.scripts/build.sh --maintenance --apply-fixes --format-apply

# PGO optimized build (automated two-pass)
.scripts/build.sh --pgo
```

## Manual Build

For manual CMake builds without the script:

```bash
# Create build directory
mkdir -p build/release && cd build/release

# Configure with CMake
cmake ../.. -DCMAKE_BUILD_TYPE=Release

# Build with all available cores
make -j$(nproc)

# Run the application
./kartend
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | — | `Release` or `Debug` |
| `MAINTENANCE` | `OFF` | Enable `-Werror` for CI/maintenance builds |
| `BUILD_TESTS` | `OFF` | Build unit test executables |
| `USE_PGO` | `OFF` | Enable Profile-Guided Optimization |
| `PGO_GENERATE` | `OFF` | Generate PGO profile data |
| `PGO_USE` | `OFF` | Use existing PGO profile data |
| `PGO_PROFILE_DIR` | `build/pgo_profiles` | Directory for PGO profile data |

Example with options:

```bash
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DMAINTENANCE=ON
```

## Debug Build

```bash
mkdir -p build/debug && cd build/debug
cmake ../.. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Debug builds include symbols and generate a linker map file at `.backups/reports/kartend.map`.

## Ninja Support

If Ninja is installed, the build script will automatically use it instead of Make for faster builds. To use Ninja manually:

```bash
mkdir -p build/release && cd build/release
cmake ../.. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

## Profile-Guided Optimization (PGO)

PGO builds optimize the binary based on actual runtime behavior. The `--pgo` flag automates the two-pass process:

1. **Generate pass**: Builds with instrumentation, runs the application to collect profile data
2. **Use pass**: Rebuilds using the collected profile for optimized code paths

For manual PGO:

```bash
# Pass 1: Generate profile data
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DUSE_PGO=ON -DPGO_GENERATE=ON
make -j$(nproc)
# Run the application to generate profile data...

# Pass 2: Use profile data
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DUSE_PGO=ON -DPGO_USE=ON
make -j$(nproc)
```
