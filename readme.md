# Kartend

[![CI](https://github.com/EtherAura/Kartend/actions/workflows/build.yml/badge.svg)](https://github.com/EtherAura/Kartend/actions/workflows/build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/EtherAura/Kartend?include_prereleases)](https://github.com/EtherAura/Kartend/releases)

Collection & Artwork Frontend for KDE

**Kartend** (German: *Carding*) — Carding is a mechanical process that aligns cotton, wool or other fibers in the manufacture of textiles. Kartend is a highly customizable frontend that will allow you to organize, manage, and launch your files.

## Demos

### Launching items

https://github.com/user-attachments/assets/7d3baa37-2cee-468d-98a3-4593e4b6ed6e

### Settings

https://github.com/user-attachments/assets/e31ba6f6-f523-4fe5-a647-2af05299e389

## Features

- **Virtual Scrolling** — Efficiently handles collections with thousands of items through widget pooling
- **Async Artwork Loading** — Parallel image loading with QtConcurrent and intelligent caching
- **Collection Hierarchy** — Organize collections into parent-child relationships with subcollection navigation
- **Persistent Sessions** — Remembers selection state and scroll position across application restarts
- **Configurable Launchers** — Launch media with custom emulators, RetroArch cores, or native applications
- **Keyboard Navigation** — Full keyboard support with arrow keys, alphabetic jumping, and search
- **Smooth Animations** — Glide animations for selection and scroll with configurable easing

## Dependencies

- **CMake** 3.20+
- **Qt6** (Core, Gui, Widgets, Sql, Concurrent)
- **C++23** compiler (Clang or GCC)
- **lld** linker (for release builds)
- **Ninja** (recommended; build script uses it by default when available)
- **ccache** (optional; speeds up rebuilds)

### Debian/Ubuntu

```bash
sudo apt install clang cmake lld ninja-build ccache qt6-base-dev libqt6sql6-sqlite
```

## Building

The repository ships with a build script at `.scripts/build.sh` that
configures CMake, picks the best available generator (Ninja → Make), wires
up `ccache` and `lld` if they are installed, and prunes stale build trees.
Use it for everyday builds — direct `cmake` invocations are also supported
and documented in [docs/building.md](docs/building.md).

### Quick start

```bash
.scripts/build.sh
```

With no flags this produces an optimized release binary at
`build/ninja-release/kartend` (or `build/make-release/kartend` when Ninja
is not available). Subsequent runs are incremental.

### Common workflows

| Goal | Command |
|------|---------|
| Optimized release build (default) | `.scripts/build.sh` |
| Debug build (keeps `qDebug`/`qWarning` output) | `.scripts/build.sh --debug` |
| Build + run unit tests | `.scripts/build.sh --tests --run-tests` |
| Fast dev iteration (no reports/archive) | `.scripts/build.sh --debug --tests --run-tests --fast` |
| Profile-guided optimization (two passes) | `.scripts/build.sh --pgo` |
| Sanitizers (ASan/UBSan) | `.scripts/build.sh --sanitize` |
| Strict checks: warnings-as-errors, clang-tidy, clang-format | `.scripts/build.sh --maintenance` |
| Apply safe clang-tidy + format fixes | `.scripts/build.sh --maintenance --apply-fixes --format-apply` |
| Build then install (auto-elevates with sudo/doas if needed) | `.scripts/build.sh --install` |
| Force a clean reconfigure | `.scripts/build.sh --clean` |
| Force Make instead of Ninja | `.scripts/build.sh --make` |

Run `.scripts/build.sh --help` for the full option list.

### What the script does

- Detects `ninja`, `lld`, and `ccache` and enables them automatically (use
  `--make` or `--no-ccache` to opt out).
- Creates a build directory named after the generator and mode
  (e.g. `build/ninja-release`, `build/ninja-debug`, `build/sanitize`),
  so multiple build flavors can coexist without clobbering each other.
- Reuses the existing build directory for incremental rebuilds; pass
  `--clean` to start from scratch.
- After a successful release build, writes a source archive to
  `.backups/*.tar.gz` and assembles source/UI reports under
  `.backups/reports/` — disable with `--no-archive`, `--no-reports`, or
  the combined `--fast` shorthand.
- With `--install`, runs `cmake --install` against the build directory,
  honoring `DESTDIR` and re-invoking under `sudo`/`doas` when the
  configured prefix isn't writable by the current user.

### Manual CMake build

If you'd rather not use the script:

```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja-release --parallel "$(nproc)"
```

For more advanced configuration (PGO details, sanitizer flags, custom
toolchains, CI parity), see [docs/building.md](docs/building.md).


## Installation

After building, install the application system-wide:

```bash
cd build/ninja-release
sudo cmake --install .
```

This installs:
- `kartend` binary to `/usr/local/bin/`
- Desktop entry to `/usr/local/share/applications/`
- Icon to `/usr/local/share/pixmaps/`

To install to a custom location:

```bash
sudo cmake --install . --prefix /opt/kartend
```

Or run directly from the build directory without installing:

```bash
./build/ninja-release/kartend
```

## Uninstalling

CMake does not provide a built-in uninstall target by default, but it does
write an `install_manifest.txt` file in your build directory listing every
installed file.

To uninstall a system-wide install:

```bash
cd build/ninja-release
sudo sh -c 'while IFS= read -r f; do rm -f -- "$f"; done < install_manifest.txt'
```

If you installed with a custom prefix (e.g. `--prefix /opt/kartend`), run the
same command from the corresponding build directory that generated that install.

## Getting Started

New to Kartend? See [docs/usage.md](docs/usage.md) for a first-run
walkthrough (adding a collection, browsing, launching, organizing
subcollections).

Hitting a snag? See [docs/troubleshooting.md](docs/troubleshooting.md) for
fixes to common issues (empty collections, missing artwork, launcher
problems, performance tuning).

## Testing

See [docs/testing.md](docs/testing.md) for unit test documentation.

## Configuration

See [docs/configuration.md](docs/configuration.md) for detailed configuration documentation.

Configuration is stored in `~/.config/kartend/kartend.cfg` (INI format). Collections can be configured via the Settings Dialog or by editing the file directly.

## Keyboard Shortcuts

Defaults — all navigation keys are user-rebindable in **Settings → General** (a complete in-app reference is available via **F1** or **Help → Shortcuts**).

### Navigation

| Key                         | Action                                  |
|-----------------------------|-----------------------------------------|
| `←` / `→` / `↑` / `↓`       | Move selection                          |
| `Enter` / `Return`          | Launch / enter subcollection            |
| `Escape`                    | Back / close overlay                    |
| `Home` / `End`              | Jump to first / last item               |
| `Page Up` / `Page Down`     | Alphabetic jump (previous/next letter)  |
| `/`                         | Focus search bar                        |

### View

| Key              | Action                                          |
|------------------|-------------------------------------------------|
| `Ctrl + +`       | Increase grid width (more items per row)        |
| `Ctrl + -`       | Decrease grid width                             |
| `F5`             | Refresh current view                            |
| `Ctrl + F5`      | Rescan collection (force re-read from disk)     |
| `F8`             | Toggle metadata sidebar                         |
| `F9`             | Toggle menu bar                                 |
| `F10`            | Toggle toolbar                                  |
| `F11`            | Toggle fullscreen                               |

### Application

| Key              | Action                  |
|------------------|-------------------------|
| `F1`             | Show keyboard shortcuts |
| `Ctrl + ,`       | Open settings           |
| `Ctrl + Q`       | Exit                    |

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.


## License

See LICENSE.

---

*The sculpture is already complete within the marble block, before I start my work. It is already there, I just have to chisel away the superfluous material.*

Founded 07/20/2025
