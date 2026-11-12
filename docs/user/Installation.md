# Installation

Every [release](https://github.com/EtherAura/Kartend/releases/latest)
ships a binary `.deb` plus the canonical Arch and Gentoo recipes:

| Platform | Asset | Install |
|--------|-------|---------|
| Debian / Ubuntu | `kartend_<version>_amd64.deb` | `sudo apt install ./kartend_<version>_amd64.deb` |
| Arch Linux | `PKGBUILD` | Drop in a clean dir, `makepkg -si` |
| Gentoo | `kartend-<version>.ebuild` | Place under your local overlay's category dir; `emerge kartend` |
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
```

Each release also ships a CycloneDX SBOM (`*.cdx.json`) listing what is
bundled inside the Linux and Windows downloads.

## Build from source

See [Building](../dev/building.md), including dependencies and install flags.

## Uninstalling

Remove packaged builds with the package manager that installed them. The Windows installer provides an Apps & Features entry; remove a portable build by deleting its extracted folder. Your library and configuration are separate: see [File locations](File-Locations.md) before removing personal data.
