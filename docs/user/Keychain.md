# Scraper Credentials & the OS Keychain

This page covers credential storage. For the scrape workflow itself —
single-item vs batch, rescrape modes, DAT-based identification, ROM
quotas, resume-after-crash — see [Scraper](Scraper.md).

Kartend stores the credentials you enter for online metadata scrapers
(usernames, passwords, API keys) in the operating system's secure
credential store whenever it can. The credential fields in
`~/.config/kartend/kartend.cfg` then contain only an `@keychain`
sentinel — the real secret never touches your config file.

If the secure store is unavailable (the dependency wasn't found at build
time, or there's no Secret Service / Keychain daemon running at runtime),
Kartend falls back to writing the credential to the INI file in
plaintext. Older installations that predate the keychain integration
also live in this fallback state until you re-save the credential.

## What it is and why it matters

Credentials live in the `[Scrapers]` group of `kartend.cfg`, one key
per field, named `<providerId>/<fieldName>`. Without keychain
integration they persist as readable text:

```ini
[Scrapers]
screenscraper/dev_id=myDevId
screenscraper/dev_password=myDevPassword
screenscraper/user_id=myUserId
screenscraper/user_password=mySecretPassword
```

The INI file is clamped to mode 0600 on Unix after every save that may
have touched it, and it lives in the user-local config directory — but
plaintext-on-disk is still a smaller security boundary than the OS
keychain would give. With keychain integration enabled and a
secret-service backend running, the same stanza becomes:

```ini
[Scrapers]
screenscraper/dev_id=@keychain
screenscraper/dev_password=@keychain
screenscraper/user_id=@keychain
screenscraper/user_password=@keychain
```

The real values live inside the OS keychain under the service name
`io.github.EtherAura.Kartend.scrapers`, keyed by that same
`<providerId>/<fieldName>` string, and are unlocked by your normal
desktop-session credentials.

If a value reads `@keychain` but the lookup fails — the backend went
away, or the entry was wiped externally — Kartend treats the credential
as *missing* and logs a warning. It deliberately never hands the
literal string `@keychain` to a provider as if it were a password.

## Installing the dependency

Kartend uses [QtKeychain](https://github.com/frankosterfeld/qtkeychain) (the
Qt 6 fork, `Qt6Keychain`). CMake auto-detects it via `find_package(Qt6Keychain QUIET)`; if found, the
build defines `KARTEND_HAVE_QTKEYCHAIN` and links the secure path.

### Linux

- **Debian / Ubuntu**: `sudo apt install qtkeychain-qt6-dev`
- **Fedora**: `sudo dnf install qt6-qtkeychain-devel`
- **Arch**: `sudo pacman -S qtkeychain-qt6`
- **Gentoo**: `sudo emerge dev-libs/qtkeychain`

Linux additionally needs a running Secret Service backend (GNOME Keyring
or KWallet); most desktop environments ship one by default. Headless
servers without a graphical session won't have one — in that case
Kartend stays on the plaintext fallback.

### macOS

`brew install qtkeychain` installs the dependency; QtKeychain uses the
macOS Keychain transparently.

### Windows

The vcpkg port `qtkeychain` provides it. QtKeychain uses Credential
Manager on Windows.

## Migrating from plaintext

If you upgraded Kartend from a version without keychain support, your
existing credentials will be in plaintext in the `[Scrapers]` group of
`~/.config/kartend/kartend.cfg`. Migration needs no retyping: every
save of the Settings dialog rewrites the whole `[Scrapers]` group and
pushes each credential back through the keychain write path, so a
legacy plaintext value is promoted automatically.

1. Open **Settings → Scrapers**.
2. Save the dialog. Kartend writes the secrets to the keychain and
   replaces the INI values with the `@keychain` sentinel.

The same pass sweeps the other direction: a credential you removed in
the dialog has its keychain entry deleted rather than left orphaned.

You can verify the migration worked by inspecting
`~/.config/kartend/kartend.cfg`: the `[Scrapers]` fields should now
contain only `@keychain`, no plaintext.

## Falling back to plaintext

If you prefer the plaintext storage (e.g. for portable installations, or
because no keychain daemon is available), build Kartend without the
QtKeychain package present — the build script omits the link and the
sentinel is never written. You can also delete the keychain entry from
your platform's secret store; on next save, Kartend will detect that the
lookup fails and write the credential plainly.

## Troubleshooting

**Look at the banner first.** When a keychain write fails, Kartend
records the reason in `[Scrapers] credentialDemotionReason` and shows a
non-modal warning banner in **Settings → Scrapers** naming the failure.
The marker is recomputed on every save, so once the keychain is working
again the next save clears it by itself — you don't need to reset
anything. That banner exists precisely so you don't have to read logs
for this.

If you do want the log, the keychain layer logs under the
`kartend.settingsmanager` category (not a scraper category — the code
lives in the settings manager):

```
QT_LOGGING_RULES="kartend.settingsmanager=true" kartend
```

If your platform has a working keychain backend but Kartend keeps using
the plaintext fallback, check:

- `KARTEND_HAVE_QTKEYCHAIN` was defined at build time. Look at the CMake
  configure log — `kartend --version` prints only the version string and
  reports nothing about build flags.
- A Secret Service / Keychain daemon is reachable from the session that
  Kartend runs in. On Linux this means having `gnome-keyring-daemon` or
  `kwalletd6` running.
- The keychain is unlocked. Some distributions auto-lock the keychain on
  screensaver. Kartend puts a 5-second watchdog around each lookup
  (QtKeychain has no timeout of its own). On a **write**, hitting that
  timeout demotes the credential to plaintext and raises the banner; on
  a **read**, it treats the credential as missing — it does not fall
  back to a plaintext value.
