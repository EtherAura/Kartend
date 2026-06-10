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

`src/modules/data/scraper/core/bundledcredentials.cpp` ships a small XOR-obfuscated
blob containing the default API credentials for built-in scrapers. The header is
honest about what this is: protection against `strings(1)` casual harvesting and
nothing more. The deployed binary contains both the obfuscated bytes and the
deobfuscation routine, so anyone with `objdump` / `radare2` / a debugger can
recover the embedded keys in minutes. Do not treat this as a defect — it is the
deliberate trade-off for shipping a working out-of-the-box scraper experience.

If you operate a deployment where credential leakage matters, supply your own
keys via the Scraper Credentials dialog (Settings → Scraper) and disable the
bundled fallback. The QtKeychain integration (built when `KARTEND_HAVE_QTKEYCHAIN`
is defined) stores per-user credentials in the platform keyring rather than on
disk in plaintext.

Because user-supplied credentials always override the bundled fallback, the
embedded provider account can be **rotated or revoked** without breaking
existing installs — a leaked bundled key is a quota/abuse concern for that one
shared account, not a user-data exposure.

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
