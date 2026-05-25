<div align="center">

<img src="src/assets/icon.svg" alt="Kartend" width="128" height="128" />

# Kartend

**Collection &amp; Artwork Frontend for KDE**

[![CI](https://github.com/EtherAura/Kartend/actions/workflows/build.yml/badge.svg)](https://github.com/EtherAura/Kartend/actions/workflows/build.yml)
[![Coverage](https://github.com/EtherAura/Kartend/actions/workflows/coverage.yml/badge.svg)](https://github.com/EtherAura/Kartend/actions/workflows/coverage.yml)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/EtherAura/Kartend?include_prereleases)](https://github.com/EtherAura/Kartend/releases)

A highly customizable Qt6 frontend for organizing, browsing, and launching
multimedia collections — videos, audio, reference material, and anything
else you can hand to a launcher.

[**Wiki**](https://github.com/EtherAura/Kartend/wiki) ·
[**Getting Started**](https://github.com/EtherAura/Kartend/wiki/Getting-Started) ·
[**Configuration**](https://github.com/EtherAura/Kartend/wiki/Configuration-Reference) ·
[**Input &amp; Controls**](https://github.com/EtherAura/Kartend/wiki/Input-and-Controls) ·
[**Troubleshooting**](https://github.com/EtherAura/Kartend/wiki/Troubleshooting)

</div>

---

> *Kartend* (German: *carding*) — the mechanical process that aligns cotton,
> wool, or other fibres in the manufacture of textiles. Kartend aligns the
> sprawl of a personal media library into something you actually want to
> open.

## Demos

#### Launching items

https://github.com/user-attachments/assets/7d3baa37-2cee-468d-98a3-4593e4b6ed6e

#### Settings

https://github.com/user-attachments/assets/e31ba6f6-f523-4fe5-a647-2af05299e389

## Features

- **Virtual scrolling** — handles thousands of items via widget pooling
- **Async artwork** — parallel `QtConcurrent` pipeline with intelligent caching
- **Hierarchical collections** — parents, alias parents, shell groupings, subcollection navigation
- **Persistent sessions** — selection, scroll position, and view state restored across restarts
- **Configurable launchers** — per-collection or per-item, with archive extraction and presets
- **Keyboard, mouse &amp; gamepad** — fully rebindable; alphabetic jump; search-as-you-type
- **Theming** — image / video backgrounds, vignette, parallax, backdrop blur, fonts, tints
- **Smooth animations** — glide selection and scroll with configurable easing

The full feature tour lives in the [user guide](https://github.com/EtherAura/Kartend/wiki).

## Install

Every [release](https://github.com/EtherAura/Kartend/releases/latest)
ships a binary `.deb` plus the canonical Arch and Gentoo recipes:

| Platform | Asset | Install |
|--------|-------|---------|
| Debian / Ubuntu | `kartend_<version>_amd64.deb` | `sudo apt install ./kartend_<version>_amd64.deb` |
| Arch Linux | `PKGBUILD` | Drop in a clean dir, `makepkg -si` |
| Gentoo | `kartend-<version>.ebuild` | Place under your local overlay's category dir; `emerge kartend` |
| Windows (installer) | `Kartend-<version>-windows-x64-setup.exe` | Run the installer; Apps & Features-managed uninstall |
| Windows (portable) | `Kartend-<version>-windows-x64.zip` | Unzip anywhere, run `kartend.exe` |

The `.deb` is built on Ubuntu 24.04 with the same flags this project's
[`packaging/PKGBUILD`](packaging/PKGBUILD) uses; runtime deps are
resolved against the stock Qt6 stack in Debian Trixie and
Ubuntu 24.04+. Older distros — or anyone who wants PGO, sanitizers,
or a `9999` Gentoo live build — should build from source via the
[quick start](#quick-start-from-source) below.

Both Windows artifacts are built on `windows-latest` with MSVC 2022 +
Qt 6.7 LTS and ship Qt's DLLs alongside `kartend.exe` (windeployqt-
bundled). The installer puts Kartend under `%ProgramFiles%\Kartend`
with a Start Menu entry and registers with Apps & Features for normal
uninstall; the portable `.zip` unzips anywhere and needs no admin.
Windows SmartScreen may warn on first launch since the artifacts are
unsigned — click **More info → Run anyway**. Code signing is on the
roadmap.

The release page also carries the source tarball
(`Kartend-<version>.tar.gz`) and a `.sha256` for each asset.

## Quick start (from source)

```bash
# Debian / Ubuntu
sudo apt install clang cmake lld ninja-build ccache \
  qt6-base-dev qt6-multimedia-dev libqt6sql6-sqlite

git clone https://github.com/EtherAura/Kartend.git
cd Kartend
.scripts/build.sh                # optimized release build
.scripts/build.sh --install      # build + install (auto-elevates)
```

The [`.scripts/build.sh`](.scripts/build.sh) wrapper auto-detects
Ninja, lld, and ccache, and keeps each flavor in its own tree
(`build/ninja-release`, `build/ninja-debug`, …). Run
`.scripts/build.sh --help` for the full flag list, or see
[docs/building.md](docs/building.md) for PGO, sanitizers, custom
toolchains, manual CMake, packaging, and uninstall.

### Dependencies

CMake 3.20+, a C++23 compiler (Clang or GCC), and Qt6 (Core, Gui,
Widgets, Sql, Concurrent, Multimedia, MultimediaWidgets). Optional:
**Qt6 Gamepad** *or* **SDL2** (gamepad backend, auto-detected) and
**zstd** (auto-detected; falls back to zlib via `qCompress`).

## Documentation

- **User guide / wiki** — [github.com/EtherAura/Kartend/wiki](https://github.com/EtherAura/Kartend/wiki)
- **Build &amp; packaging** — [docs/building.md](docs/building.md)
- **Architecture** — [docs/architecture.md](docs/architecture.md)
- **Testing** — [docs/testing.md](docs/testing.md)
- **Contributing** — [CONTRIBUTING.md](CONTRIBUTING.md)
- **Security policy** — [SECURITY.md](SECURITY.md)

Configuration is stored as INI at `~/.config/kartend/kartend.cfg`,
editable from the **Settings** dialog or by hand.

## License

[GPL-3.0-only](LICENSE).

---

<div align="center">

*The sculpture is already complete within the marble block, before I start my work.*<br>
*It is already there, I just have to chisel away the superfluous material.*

Founded 07 / 20 / 2025

</div>
