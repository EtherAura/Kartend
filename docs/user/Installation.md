# Installation

[Releases](https://github.com/EtherAura/Kartend/releases/latest) include Linux,
Windows and FreeBSD 15 amd64 downloads, plus the Arch and Gentoo recipes.

| Platform | Asset | Install |
|--------|-------|---------|
| Debian / Ubuntu | `kartend_<version>_amd64.deb` | `sudo apt install ./kartend_<version>_amd64.deb` |
| Fedora, openSUSE, Steam Deck, any other Linux | `Kartend-<version>-x86_64.AppImage` | Make it executable and run it; see [AppImage](#appimage) |
| Arch Linux | `PKGBUILD` | Drop in a clean dir, `makepkg -si` |
| Gentoo | `kartend-<version>.ebuild` | Place under your local overlay's category dir; `emerge kartend` |
| FreeBSD 15 amd64 | `kartend-<version>-freebsd15-amd64.pkg` | As root, run `pkg install ./kartend-<version>-freebsd15-amd64.pkg` |
| Windows (installer) | `Kartend-<version>-windows-x64-setup.exe` | Run the installer; Apps & Features-managed uninstall |
| Windows (portable) | `Kartend-<version>-windows-x64.zip` | Unzip anywhere, run `kartend.exe` |

The Windows builds are unsigned for now, so SmartScreen warns on first launch —
**More info → Run anyway** (see the
[Code signing policy](https://github.com/EtherAura/Kartend/wiki/Code-Signing-Policy)). Older distros, PGO, sanitizers and a `9999`
Gentoo live build all mean building from source.

Every built asset carries a build provenance attestation, so you can check
a download actually came from this repository's release workflow rather
than a mirror or a rebuild — see
[Verifying a Release Download](../../SECURITY.md#verifying-a-release-download):

```bash
gh attestation verify kartend_<version>_amd64.deb --repo EtherAura/Kartend
gh attestation verify kartend-<version>-freebsd15-amd64.pkg --repo EtherAura/Kartend
gh attestation verify Kartend-<version>-x86_64.AppImage --repo EtherAura/Kartend
```

Each release also ships a CycloneDX SBOM (`*.cdx.json`) listing what is
bundled inside the Linux and Windows downloads, the AppImage included.

## AppImage

The AppImage is one file that runs without installing anything. It is the
download for Fedora, openSUSE, the Steam Deck and any other distribution
that has no Kartend package.

```bash
chmod +x Kartend-<version>-x86_64.AppImage
./Kartend-<version>-x86_64.AppImage
```

In a file manager: open the file's properties, allow it to run as a
program, then double-click it.

- **What it needs.** A 64-bit x86 system with glibc 2.35 or newer, which
  means Ubuntu 22.04, Debian 12, Fedora 36, openSUSE Leap 15.6 and anything
  more recent, plus the graphics, font and sound libraries every desktop
  install already has (OpenGL, fontconfig, FreeType, ALSA, X11 or Wayland).
  On an older distribution, build from source.
- **What it brings.** Its own Qt 6.8 with X11 and Wayland support, FFmpeg
  for video and audio, and the same optional features as the `.deb`:
  gamepad input, zstd compression for `.kart` packages, in-process archive
  reading and keychain storage for scraper credentials. Because it uses the
  Qt it carries, its widgets can look different from a distribution package
  that follows your desktop's Qt theme.
- **No FUSE library needed.** The AppImage mounts itself where the system
  allows that. Where it cannot (in a container, for example), start it with
  `--appimage-extract-and-run`, or unpack it once with `--appimage-extract`
  and run `squashfs-root/AppRun`.
- **Your library and settings** live in the same places as with any other
  install (see [File locations](File-Locations.md)), so you can switch
  between the AppImage and a package and keep everything.
- **Updating and removing.** The AppImage does not update itself: download
  the newer file and delete the old one. Deleting the file removes Kartend;
  your library and settings stay where they are.
- **Application menu.** An AppImage adds no menu entry by itself. Start it
  from a file manager or a terminal, or let an AppImage manager for your
  desktop add the entry.

## Build from source

See [Building](../dev/building.md), including dependencies and install flags.

## Uninstalling

Remove packaged builds with the package manager that installed them. The Windows installer provides an Apps & Features entry; remove a portable build by deleting its extracted folder, and the AppImage by deleting the file. Your library and configuration are separate: see [File locations](File-Locations.md) before removing personal data.
