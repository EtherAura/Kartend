# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Kartend, please report it responsibly.

**Email:** etheraura@protonmail.com

Please include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You will receive an acknowledgment within 48 hours. Security issues will be prioritized and a fix released as soon as practical.

## Security Considerations

Kartend launches user-configured external processes. The launch module includes:
- Executable path validation and permission checks
- Sensitive directory blacklisting (system paths, `/root`, etc.)
- TOCTOU mitigation with re-validation before execution
- Argument list passing (no shell interpolation)

SQL queries use parameterized binding throughout; FTS input is sanitized.

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
