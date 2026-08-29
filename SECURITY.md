# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Kartend, please report it responsibly.

**Preferred — [open a private security advisory](https://github.com/EtherAura/Kartend/security/advisories/new).**
GitHub private vulnerability reporting is enabled on this repository. The report
is private to you and the maintainer until an advisory is published, it keeps the
report, the discussion and the fix on a single auditable thread, and it is the
route that can request a CVE and credit you by name when the advisory goes out.

**Fallback — etheraura@protonmail.com.** Use this if you would rather not go
through GitHub, or do not have an account. It reaches the same person; it simply
leaves no shared record either of us can refer back to.

Please **do not** open a regular issue for a suspected vulnerability — issues are
world-readable the moment they are filed.

Please include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You will receive an acknowledgment within 48 hours on whichever channel you used. Security issues will be prioritized and a fix released as soon as practical.

## Repository Security Posture

Enabled on this repository, and verifiable from the outside:

| Control | Status |
|---------|--------|
| [Private vulnerability reporting](https://github.com/EtherAura/Kartend/security/advisories/new) | Enabled |
| Secret scanning | Enabled |
| Secret scanning push protection | Enabled |
| Dependabot security updates | Enabled |
| [Build provenance attestations](https://github.com/EtherAura/Kartend/attestations) on release assets | Enabled |
| CycloneDX SBOM published per release | Enabled |

## Verifying a Release Download

Every release asset that this project *builds* — the source tarball, the
`.deb`, the Windows `.zip` and the Windows `setup.exe` — carries a
[Sigstore-backed build provenance attestation](https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds)
recording which workflow, at which commit, produced those exact bytes.
Check a download with the GitHub CLI:

```bash
gh attestation verify kartend_<version>_amd64.deb --repo EtherAura/Kartend
```

A pass means the file came from this repository's release workflow and has
not been altered since. This is stronger than the `.sha256` sidecars beside
each asset, which only prove a file matches a hash published on the same
page — anyone who can replace the asset can replace the sidecar too.

**Provenance is not code signing.** It tells you where a file came from; it
does not put a publisher certificate on it. The Windows builds remain
unsigned and SmartScreen still warns on first launch. The `PKGBUILD` and
`.ebuild` recipes are not attested either — they are source files copied
out of the tree, not artifacts this pipeline built.

## Software Bill of Materials

Each release also publishes a [CycloneDX](https://cyclonedx.org/) SBOM, and
each SBOM is attested the same way the binaries are — a bill of materials
you cannot trace back to its build is worth little:

| Asset | Covers |
|-------|--------|
| `Kartend-<version>-linux-x64.cdx.json` | The `.deb` and the source tarball — the same source at the same commit, with the `.deb` carrying the resolved dependency data. |
| `Kartend-<version>-windows-x64.cdx.json` | The portable `.zip` and the installer — both ship the same staged tree, so one document covers the Qt runtime and the vcpkg-built SDL2 and zstd libraries bundled beside the executable. |

The Windows document is the one that closes a real gap: a Windows user has
no package manager to ask what is inside the download, whereas on Debian
`dpkg` already answers most of the same question.

## Security Considerations

Kartend opens **no listening socket** — it has no server, no local IPC endpoint,
and no D-Bus service. Its only network use is outbound HTTPS from the scraper
client and DAT/artwork downloads. There is no remote attack surface to reach.

Kartend launches user-configured external processes. The launch module includes:
- Executable path validation and permission checks
- Pseudo-filesystem blocklist for the launcher executable — `/proc`, `/sys`, and
  `/dev` are rejected both as written and after symlink canonicalization
  (`LaunchManager::validateLauncherPath`). This is a targeted guard against
  paths like `/proc/self/exe`, **not** a general "system directory" or
  privileged-path blocklist: `/root`, `/etc`, and `/usr` are not on it, because
  a launcher the user's own account can execute is by definition already within
  that account's reach.
- TOCTOU mitigation with re-validation before execution
- Argument list passing (no shell interpolation)

Argument-list passing protects a launcher the *user* configured. It does not
help when the launcher configuration itself is untrusted input, which is the
case for an imported `.kart` package: the package names both the program and
its arguments, so choosing a shell as the program defeats the absence of one.
Import therefore treats the whole launcher block — launcher path, core path
and launch parameters, primary and additional — as untrusted regardless of
where the paths point. Every field the package supplies is listed to the user
verbatim and requires explicit confirmation before the collection is
registered, on every import route including drag-and-drop; the safe-prefix
allowlist only ranks the severity of each row, and the preflight dialog's
all-clear is reachable only for a package that carries no launcher block at
all. Values that name a shell or interpreter, carry an inline-command flag, or
resolve inside the extracted package tree are called out specifically. The
headless import has no one to ask, so it refuses a package-shipped launcher,
core or path argument unless `--allow-untrusted-launcher` is passed.
Extraction never sets an execute bit on payload files, which is enforced by
test.

SQL queries use parameterized binding throughout — including the dynamically
sized `IN (...)` clauses, which build `?` placeholder lists and bind each value.
The only string-interpolated SQL is table/column identifiers from compile-time
constants, which SQL cannot parameterize. FTS input is sanitized to an allowlist
(letters, digits, underscore; everything else becomes a separator) before being
bound as a parameter.

## Bundled Scraper Credentials — Security Theater

`src/modules/data/scraper/core/bundledcredentials.cpp` ships the shared
ScreenScraper.fr **developer** credential pair: `dev_id` in plain text (it
appears in every API URL anyway) and `dev_password` behind a single-byte XOR.
The obfuscation is not secrecy: the deployed binary contains both the
obfuscated bytes and the deobfuscation routine, so anyone with `objdump` /
`radare2` / a debugger can recover the password in minutes. It exists solely
so `strings(1)` over the binary doesn't hand it out. Do not treat this as a
defect — it is the deliberate trade-off for shipping a working out-of-the-box
scraper experience, and the same trade-off other open-source scrapers make.

**Blast radius**: the dev account is shared by every Kartend install.
ScreenScraper rejects every API request without a valid dev pair, so a leak
that gets the shared account throttled or banned degrades or breaks
ScreenScraper scraping for **all users** — including those with their own
member account — until a release rotates the bundled pair. No user data or
user credentials are exposed; the exposure is quota/abuse against that one
shared account.

**Mitigations available to users** (user-supplied values take precedence over
the bundled fallback):

- Your own ScreenScraper **member** account (Settings → Scrapers →
  ScreenScraper.fr, or the standalone Scraper credentials dialog) raises your
  per-account quota, but still rides on the shared dev pair.
- Your own **dev** pair (issued via the ScreenScraper forum's development
  section) fully replaces the bundled one: set `screenscraper/dev_id=` and
  `screenscraper/dev_password=` under `[Scrapers]` directly in the INI. These
  keys are deliberately not surfaced in the UI (users kept mis-pasting member
  credentials into them), and saving the credentials UI clears them — re-add
  them after a UI save if you rely on this override.

The QtKeychain integration (built when `KARTEND_HAVE_QTKEYCHAIN` is defined)
stores user-supplied credentials in the platform keyring rather than on disk
in plaintext.

The real fix — a server-side proxy minting per-client tokens so no shared
secret ships in the binary — is **deferred unless abuse appears**: it would
add hosting cost and a single point of failure that field precedent shows is
unnecessary in practice.

## Launcher Path TOCTOU — Accepted Residual Risk

`LaunchManager` canonicalizes and re-validates the launcher executable path
immediately before `QProcess::start` (sensitive-directory blocklist, symlink
canonicalization, character-level checks). POSIX offers no atomic
open-and-exec, so a sufficiently privileged local attacker could in principle
swap a symlink in the window between that final check and `start()`. This is an
**accepted residual risk**, not a defect: exploiting it requires write access
to the user's own config directory / launcher path, which is itself
user-controlled configuration — an attacker with that access can already edit
the launcher command outright. The re-validation is defense-in-depth that
narrows, but cannot fully close, the window.

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.0.x   | ✅        |
