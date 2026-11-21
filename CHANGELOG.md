# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **View → Show Status Bar hides the strip along the bottom of the window.**
  After a background job such as a launcher sync reported there, the empty
  bar stayed for the rest of the session. The choice is remembered across
  restarts; while the bar is hidden, those messages are not shown.

### Fixed

- ScreenScraper no longer imports another platform's details or artwork just
  because a game title matches. A matching ROM hash still identifies the game.
- **The toolbar keeps the titlebar colour after a desktop colour change.** In a
  collection set to use its own toolbar colour with none chosen, the toolbar
  could turn the window background colour, or an older titlebar colour, after
  the desktop colours changed and you visited another collection. It stayed
  that way until the desktop colours changed again. The top row of the
  collection tree now always matches the toolbar, including while selected.

## [0.0.23] - 2026-11-20

### Added

- **Kartend now has a FreeBSD 15 amd64 package.** Install the native package from
  the release page with `pkg install`. The release workflow checks a fresh
  Kart import and launches an item from the installed desktop app.

### Security

- The Windows downloads and Linux AppImage build with libarchive 3.8.11.
  Packaging checks the version of the library included in each download.

### Fixed

- Importing a Kart into a fresh library now initializes its database.
- **The item you had selected is selected again when Kartend starts.** With
  "Remember selection per collection" turned on, Kartend saved the selected
  item when you closed it but started on the first item of that collection
  the next time, whenever all of the collection's items fit on screen. It now
  starts on the item you left selected.
- **A platform wallpaper you chose stays when you scrape again.** Scraping a
  platform could replace a wallpaper you had picked from its own scraped
  artwork with another scraped image. A wallpaper marked as your choice is now
  kept; the newly scraped image is still saved so you can pick it later.
- **The AppImage runs under the name `kartend`.** Started from the AppImage,
  Kartend showed up as `AppRun.wrapped` in process lists, in its usage line
  and as its window class, so `pgrep kartend` or a window rule for Kartend did
  not find it. It now runs as `kartend`, as the other packages do.

## [0.0.22] - 2026-11-18

### Added

- **Kartend now comes as an AppImage.** Each release has a
  `Kartend-<version>-x86_64.AppImage`: one file that runs on Fedora,
  openSUSE, the Steam Deck and any other distribution without a Kartend
  package, as long as it has glibc 2.35 or newer (Ubuntu 22.04, Debian 12
  and later). Make it executable and run it; nothing is installed and no
  FUSE library is needed. It carries its own Qt 6.8 with X11 and Wayland
  support and FFmpeg for video and audio, and has the same optional
  features as the `.deb`: gamepad input, zstd compression for `.kart`
  packages, in-process archive reading and keychain storage for scraper
  credentials. Like the other downloads it comes with a `.sha256`, an SBOM
  and a build provenance attestation.
- **ES-DE imports now bring in the game videos ES-DE downloaded.** A video
  saved in ES-DE's `videos` media folder (for example `.mp4` or `.mkv`) is
  copied into the imported game's video folder, next to its cover and other
  art, so it plays in the details pane like a scraped video. A video you
  already have for that game is never replaced. Whether a given video plays
  still depends on the codecs your system's media backend supports.

### Fixed

- **Exported .kart files import again when games share artwork.** A
  collection where several files share a name, such as a `Game.bin` and
  `Game.cue` pair, put the shared artwork, video and manual into the bundle
  once per file, and importing that bundle then failed with a duplicate
  entry error. Each shared file is now stored once, and every game still
  shows it.
- **Exporting a .kart file works again on builds without zstd.** A Kartend
  build made without zstd support still asked for zstd compression, so
  exporting any collection with a compressible file (such as a `.bin`,
  `.cue` or `.txt`) failed. Those builds now compress with zlib instead.
  Builds with zstd are unchanged.
- **Heroic imports now use the titles and covers you edited in Heroic.**
  Heroic 2.22 lets you rename a game and pick its own card or cover image,
  but imported collections kept showing the store's title and art. A
  renamed game now arrives under your name. A custom image is used when it
  is hosted on the Epic, GOG or Amazon art servers Kartend already
  downloads Heroic covers from, preferring your card image over your cover
  image. A custom image from anywhere else, or a file on your computer,
  is skipped and the store's own art is kept, so editing a game never
  leaves it without a cover.
- **More archives containing symbolic links are refused before extraction.**
  When 7-Zip was used to check a zip or 7z archive, a symbolic link whose
  permissions did not end in an execute bit (for example `lrw-r--r--`) was
  not recognised, so the archive was treated as safe. Such archives are now
  refused before anything is extracted, like other archives with links.

- **Very long titles ending in an emoji or other special character get a
  valid file name.** When an imported game's stub file or a multi-disc
  playlist was named after a title long enough to be shortened, the cut
  could land in the middle of an emoji or a similar character and leave half
  of it in the name, so the file on disk did not match the name Kartend had
  computed. The shortened name now drops the incomplete character instead.

- **The launch preview shows the launcher exactly as it would run when the
  command cannot be built.** If the launcher path had spaces before or after
  it and the command could not be assembled (for example because of an
  unclosed quote in the launch parameters), the preview showed the path with
  those spaces, although a launch ignores them. It now shows the same
  trimmed path a launch would use.

- **A file name containing a launch placeholder no longer breaks the
  launch.** When a file's name or path itself contained text such as `%f`,
  `%1` or `%core`, that text was replaced again after it had been inserted
  into the launch command. A launcher could then receive a mangled title or
  file path, or no media file at all, and the launch preview showed the same
  wrong command. Only the placeholders you wrote in the launcher's
  parameters are now replaced, and the file name is passed exactly as it is.

- **A console photo from Wikidata is kept even when it is the only thing
  Wikidata has.** When a collection's Wikidata entry had a photo of the
  console but no logo, description or other details, the scrape reported
  "not found" and threw the photo away. The photo is now saved with the
  collection's other shared artwork. Your collection's icon, header logo
  and background stay as they were.

- **A console's units sold from Wikidata is kept even when it is the only
  thing Wikidata has.** When a collection's Wikidata entry listed how many
  units the console sold but had no logo, photo, description or other
  details, the scrape reported "not found" and dropped the number. The
  units sold figure is now kept with the collection's details.

- **Launches are no longer forgotten while the library sits idle after a
  scan.** If the item grid had just counted its items and a background
  scan then saved changes, the next launch could fail to record its play
  count, its history entry and, for tracked launches, its play time. The
  app retried for about half a minute and then gave up. These are now
  saved on the first try.

- **Cover art keeps loading after start-up in large combined
  collections.** In a collection that shows its subcollections' items,
  with more than about a thousand of them, tiles that scrolled into view
  a few seconds after start-up could stay placeholders for the rest of
  the session, until a refresh (F5). Attract mode, which moves through
  the grid slowly, ended up filling the whole screen with them. Fetching
  store details for a collection imported from Steam or Flatpak threw
  away Kartend's list of which artwork files exist, even when it saved
  nothing (a game the store has no page for is looked up again at every
  start), and those tiles only ever consulted that list. They now ask
  for it to be rebuilt when it cannot answer, and the store fetch only
  throws it away when it actually saved new artwork.
- **List rows ask for their artwork folder to be scanned.** In list view,
  a row whose artwork lookup comes back empty while its artwork folder has
  not been scanned yet now requests a background scan of that folder,
  instead of skipping the direct check and only waiting for a scan some
  other part of Kartend might start. Rows whose folder has already been
  scanned request nothing.
- **Declining to store a scraper password in the clear keeps the one you
  already had.** When the system keychain could not save a changed
  scraper password and you declined storing it unencrypted, a password
  that was already stored unencrypted was replaced by a reference to a
  keychain entry that did not exist, so it was lost, and the warning
  about unencrypted passwords disappeared. The previously stored password
  and the warning now stay as they were. Later saves keep trying the
  keychain with the new password and never store it unencrypted while
  the keychain still fails; once the keychain works again, the new
  password is stored there and the warning clears when no unencrypted
  password is left.
- **Scraper passwords survive a keychain that is locked at start-up.** If
  the system keychain was locked or unavailable when Kartend started, a
  scraper password stored there could not be read, and the next time any
  setting was saved (a toolbar toggle, for example) the stored password
  was deleted. Saving now leaves such a password untouched, in the
  keychain and without a warning, so it is available again the next time
  Kartend starts with the keychain unlocked. A password you clear yourself
  is still removed.
- **A scraper password stored in the keychain stays there while the
  keychain is locked.** Saving any setting (a toolbar toggle, for example)
  while the system keychain was locked or unavailable could move a scraper
  password that was stored securely, and had not been changed, into the
  settings file unencrypted. Such a password is now left in the keychain
  as it is, without asking and without a warning. This also holds after
  you declined to store a changed password unencrypted and then changed it
  back.
- **No repeated question about a scraper password that is already
  unencrypted.** While the system keychain was locked or unavailable,
  saving settings in the Settings or scraper credentials dialog asked
  again whether to store a scraper password unencrypted even though you
  had not changed it and it was already stored that way. Answering no did
  not move it to the keychain either. Kartend now keeps such a password as
  it is without asking, along with the warning about unencrypted
  passwords, and still moves it into the keychain on the first save after
  the keychain works again. A password you change is still asked about.
- **A scraper password is saved to the system keychain even while Kartend
  is closing.** If Kartend was told to quit while a dialog was open (at
  logout or shutdown, with Ctrl+C in a terminal, or while the first-run
  wizard was showing), the save that followed could not reach the
  keychain. A scraper password that was new, or still stored unencrypted,
  stayed unencrypted in the settings file, with a warning that the
  keychain "did not respond within 5000 ms" although nothing had waited
  that long. A password you had removed was left behind in the keychain.
  Kartend now reaches the keychain in that moment too. A password that
  was already in the keychain and unchanged was not affected.

## [0.0.21] - 2026-11-12

### Fixed

- **A subcollection view keeps its items after saving settings.** While
  you were browsing a subcollection, saving the settings dialog (or
  navigating somewhere that rebuilds the layout) treated the
  subcollection's name as a search, so most of its items and its
  subcollection and folder rows disappeared. The view now keeps showing
  the subcollection's items and every subcollection and folder row.
- **Clearing a search keeps "hide items without artwork" in effect.** In
  a collection that hides items without artwork, clearing a search could
  size the grid for every item, including the hidden ones, leaving empty
  slots at the end and a scroll area that ran past the last visible item.
  The grid now counts only the items that are actually shown.

## Older releases

Releases 0.0.1 through 0.0.20 are archived in
[docs/changelogs/v0.0.x.md](docs/changelogs/v0.0.x.md).

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.23...HEAD
[0.0.23]: https://github.com/EtherAura/Kartend/compare/v0.0.22...v0.0.23
[0.0.22]: https://github.com/EtherAura/Kartend/compare/v0.0.21...v0.0.22
[0.0.21]: https://github.com/EtherAura/Kartend/compare/v0.0.20...v0.0.21
