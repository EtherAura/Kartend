# Kartend

Collection & Artwork Frontend for KDE

**Kartend** (German: *Carding*) — Carding is a mechanical process that aligns cotton, wool or other fibers in the manufacture of textiles. Kartend is a highly customizable frontend that will allow you to organize, manage, and launch your files.

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

### Debian/Ubuntu

```bash
sudo apt install clang cmake lld qt6-base-dev libqt6sql6-sqlite
```

### Fedora

```bash
sudo dnf install clang cmake lld qt6-qtbase-devel
```

### Arch Linux

```bash
sudo pacman -S clang cmake lld qt6-base
```

## Building

```bash
.scripts/build.sh
```

This produces an optimized release build at `build/release/kartend`.

For build script options, manual CMake builds, and advanced configuration, see [docs/building.md](docs/building.md).

## Installation

After building, install the application system-wide:

```bash
cd build/release
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
./build/release/kartend
```

## Testing

See [docs/testing.md](docs/testing.md) for unit test documentation.

## Configuration

See [docs/configuration.md](docs/configuration.md) for detailed configuration documentation.

Configuration is stored in `~/.config/kartend/kartend.cfg` (INI format). Collections can be configured via the Settings Dialog or by editing the file directly.

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.


## License

TBD

---

*The sculpture is already complete within the marble block, before I start my work. It is already there, I just have to chisel away the superfluous material.*

Founded 07/20/2025