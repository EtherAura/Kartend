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
