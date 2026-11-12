# Code signing policy

Kartend's Windows builds (`kartend.exe` in the portable `.zip`, and the
`Kartend-<version>-windows-x64-setup.exe` installer) are code-signed so
Windows can tell where they came from.

Free code signing provided by [SignPath.io](https://about.signpath.io/),
certificate by [SignPath Foundation](https://signpath.org/).

> **Status:** Kartend has applied to the SignPath Foundation's programme
> for open-source projects. Until signing is switched on, the Windows
> builds are unsigned and SmartScreen warns on first launch. Every
> release already carries a build provenance attestation, so a download
> can be checked either way — see
> [Verifying a Release Download](../../SECURITY.md#verifying-a-release-download).

## What is signed

Only files built from this repository's own source, by its release
workflow (`.github/workflows/release.yml`) on GitHub-hosted runners:
`kartend.exe` and the setup `.exe`. Nothing is signed by hand, and no
third-party binary is signed.

## Team roles

| Role | Members |
|------|---------|
| Committers and reviewers | [EtherAura](https://github.com/EtherAura) |
| Approvers | [EtherAura](https://github.com/EtherAura) |

Kartend is developed with AI assistance (see
[How this is built](../../readme.md#how-this-is-built)). A change is
written by the maintainer or by Kartend's automated development lane,
which works only on issues the maintainer has filed. Every change reaches
`main` through a pull request that has passed the test suite,
clang-format, clang-tidy, the layering linter and CI; a lane change is
reviewed before it is committed, and a change to the build, release or CI
files needs the maintainer's own approval. Every signing request is
approved by hand by an approver.

## Privacy policy

This program will not transfer any information to other networked
systems unless specifically requested by the user or the person
installing or operating it.

Kartend has no telemetry, crash reporting or automatic update check. It
goes online only when you ask it to: when you scrape metadata or artwork,
or download or check for updates of DAT files. It then sends the service
you chose what that service needs to look an item up (for example a
title, or a file's name, size and checksums) and, for ScreenScraper, the
account you entered. These services' own policies apply:

- [ScreenScraper](https://www.screenscraper.fr/)
- [The Movie Database (TMDB)](https://www.themoviedb.org/privacy-policy)
- [MusicBrainz and the Cover Art Archive (MetaBrainz)](https://metabrainz.org/privacy)
- [Open Library (Internet Archive)](https://archive.org/about/terms.php)
- [Steam Store (Valve)](https://store.steampowered.com/privacy_agreement/)
- [Flathub](https://flathub.org/)
- [Wikidata, Wikimedia Commons and Wikipedia (Wikimedia Foundation)](https://foundation.wikimedia.org/wiki/Policy:Privacy_policy)
- [Redump](http://redump.org/) and [No-Intro (DAT-o-MATIC)](https://datomatic.no-intro.org/), for DAT files
