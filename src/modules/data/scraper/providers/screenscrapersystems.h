#ifndef SCREENSCRAPERSYSTEMS_H
#define SCREENSCRAPERSYSTEMS_H

#include <QList>
#include <QString>
#include <QStringList>

/// Pure helpers for ScreenScraper.fr systemeid handling.
///
/// The system catalog itself is NOT defined here — this module owns
/// neither the SS systemeid list nor any platform names. The catalog
/// comes from SS's own `systemesListe.php` API at runtime (see the
/// dynamic-cache follow-up); the autodetect helper takes the live
/// list as a parameter so this header stays platform-name-free.
namespace ScreenScraperSystems {

/// One entry from a system's `medias` array (Kartend-xny9o). SS ships a
/// complete, hashed media catalog inside the systemesListe response we
/// already download once per TTL — 32 distinct types across ~7.6k entries
/// — so callers can know which platform art exists BEFORE spending a
/// request on it.
///
/// **No URL is stored, deliberately.** Every `medias[].url` SS returns
/// arrives with devid / devpassword / ssid / sspassword already
/// interpolated, so persisting one verbatim would write the dev password
/// into the on-disk cache in cleartext. `token` + `video` + the owning
/// system's id are everything a caller needs to rebuild the URL against
/// whatever credentials are current at fetch time.
struct Media {
  /// SS `type` — the canonical asset tag ("wheel", "logo-monochrome",
  /// "background", "icon", "bezel-16-9", ...). Lowercased.
  QString type;
  /// The `media=` query token, region qualifier included ("wheel(wor)").
  /// SS resolves region fallback itself — a system can carry an `eu` row
  /// whose token is `wheel(wor)` — so the token is kept verbatim rather
  /// than reassembled from type + region.
  QString token;
  /// SS `region` ("wor", "us", "jp", "eu", ...). May be empty.
  QString region;
  /// SS `support`. Free-form; empty when SS omits it.
  QString support;
  /// SS `format` — the extension SS serves ("png", "svg", "mp4"). Lets a
  /// caller name the file without guessing from the URL suffix.
  QString format;
  /// SS-supplied checksums, verbatim. A rescrape can compare these against
  /// what is already on disk instead of re-downloading unchanged art.
  QString crc;
  QString md5;
  QString sha1;
  /// True when SS served this entry from mediaVideoSysteme.php rather than
  /// mediaSysteme.php — the two endpoints take the same systemeid + media
  /// params but are not interchangeable. An unrecognised endpoint is
  /// recorded as false; the type/hashes stay usable either way.
  bool video = false;

  bool operator==(const Media &other) const = default;
};

/// One row from the SS catalog. All fields are populated by the
/// dynamic-list fetcher; the autodetect heuristic reads them but
/// doesn't store them.
struct System {
  /// SS systemeid (their internal numeric identifier for the
  /// platform).
  int id = -1;
  /// Display name for the dropdown UI. Sourced from SS's `nom_eu` /
  /// `nom_us` field at fetch time.
  QString displayName;
  /// Lowercased file extensions for this platform. Sourced from
  /// SS's `extensions` field (a comma-separated string in their
  /// API response — splitter lives in the cache fetcher).
  QStringList extensions;
  /// Lowercased name aliases — every `nom_*` variant SS exposes
  /// (eu / us / recalbox / retropie / launchbox / hyperspin /
  /// hyperspinmusic / etc.). The recalbox / retropie / launchbox
  /// shorthand entries are the typical lowercase tags users put in
  /// collection names, so they make autodetect land on the right
  /// systemeid without a hand-curated lookup table.
  QStringList aliases;

  // ---- Fields below are retained verbatim from the systemesListe
  // response (Kartend-xny9o). They cost nothing to keep — the response
  // already carries them — and none of them participate in autodetect.

  /// SS `compagnie` — the platform's manufacturer, as a free-text NAME
  /// ("SEGA", "Nintendo", "Commodore"). **There is no company id here**,
  /// and SS exposes no listing endpoint to resolve one from the name, so
  /// this does not join to the `companyid` that jeuInfos reports for a
  /// game's publisher/developer (Kartend-13co2). Empty for roughly a
  /// third of the catalog — absence is normal, not an error.
  QString company;
  /// SS `type` — the platform's category ("Console", "Arcade",
  /// "Ordinateur", "Console Portable", "Flipper", ...). SS's own French
  /// strings, kept untranslated: this is catalog data, not UI copy, and
  /// translating at parse time would make the value untraceable back to
  /// the response it came from.
  QString systemType;
  /// SS `datedebut` / `datefin` — production start/end, usually a bare
  /// year ("1988"). Kept as text because SS is not consistent about the
  /// shape and an empty value is common.
  QString startDate;
  QString endDate;
  /// SS `romtype` ("rom") and `supporttype` ("cartouche", ...).
  QString romType;
  QString supportType;
  /// SS `medias` — the per-system art catalog. See Media for why no URL
  /// is stored. Empty when SS omits the array.
  QList<Media> media;
};

/// Best-guess systemeid given the collection's `name`, free-form
/// `type` tag, the lowercased extensions it scans for, and the
/// runtime-supplied `systems` catalog. Returns -1 when no candidate
/// scores high enough OR when the catalog is empty (no hardcoded
/// fallback — caller treats -1 as "unknown system" and either
/// prompts the user or passes systemeid=0 to SS).
///
/// Scoring: each matched alias is +2 points (whole-word match so
/// short tags don't falsely substring-match longer ones); each
/// matched extension is +1.
[[nodiscard]] int autodetect(const QString &collectionName, const QString &collectionType,
                             const QStringList &extensions, const QList<System> &systems);

/// Look up a system by id within the supplied catalog; returns
/// nullptr when no entry matches.
[[nodiscard]] const System *find(const QList<System> &systems, int systemeid);

} // namespace ScreenScraperSystems

#endif // SCREENSCRAPERSYSTEMS_H
