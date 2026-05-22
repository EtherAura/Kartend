# Scraper Credentials & the OS Keychain

Kartend stores the credentials you enter for online metadata scrapers
(usernames, passwords, API keys) in the operating system's secure
credential store whenever it can. The plaintext fields in
`~/.config/Kartend/settings.ini` then contain only an `@keychain`
sentinel — the real secret never touches your config file.

If the secure store is unavailable (the dependency wasn't found at build
time, or there's no Secret Service / Keychain daemon running at runtime),
Kartend falls back to writing the credential to the INI file in
plaintext. Older installations that predate the keychain integration
also live in this fallback state until you re-save the credential.

## What it is and why it matters

Without keychain integration, scraper credentials persist in
`settings.ini` as readable text:

```ini
[ScraperOverrides/ScreenScraper]
sspassword=mySecretPassword
```

The INI file is mode 0600 on Unix and lives in the user-local config
directory, but plaintext-on-disk is still a smaller security boundary
than the OS keychain would give. With keychain integration enabled and a
secret-service backend running, the same setting becomes:

```ini
[ScraperOverrides/ScreenScraper]
sspassword=@keychain
```

The real password lives inside the OS keychain entry
`io.github.EtherAura.Kartend.scrapers` and is unlocked by your normal
desktop-session credentials.

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
existing credentials will be in plaintext under
`~/.config/Kartend/settings.ini`. The migration happens on the next save
of the Settings dialog:

1. Open **Settings → Scrapers**.
2. Re-enter the same credential (or just click **Save** if Kartend
   pre-fills it — the dialog reads the plaintext value, you only need
   to push it back through the save path).
3. Save the dialog. Kartend writes the secret to the keychain and
   replaces the INI value with the `@keychain` sentinel.

You can verify the migration worked by inspecting
`~/.config/Kartend/settings.ini`: the credential fields should now
contain only `@keychain`, no plaintext.

## Falling back to plaintext

If you prefer the plaintext storage (e.g. for portable installations, or
because no keychain daemon is available), build Kartend without the
QtKeychain package present — the build script omits the link and the
sentinel is never written. You can also delete the keychain entry from
your platform's secret store; on next save, Kartend will detect that the
lookup fails and write the credential plainly.

## Troubleshooting

`kartend.scraper.http` logs at info level capture the SSL config on the
first scraper request, but the keychain layer logs at warning level only
when something goes wrong. To see the keychain code path:

```
QT_LOGGING_RULES="kartend.scraper.http=true" kartend
```

If your platform has a working keychain backend but Kartend keeps using
the plaintext fallback, check:

- `KARTEND_HAVE_QTKEYCHAIN` is defined in your build (run `kartend
  --version` or look at the configure log).
- A Secret Service / Keychain daemon is reachable from the session that
  Kartend runs in. On Linux this means having `gnome-keyring-daemon` or
  `kwalletd6` running.
- The keychain is unlocked. Some distributions auto-lock the keychain on
  screensaver — the lookup blocks for up to 5 seconds before Kartend
  gives up and uses the plaintext fallback.
