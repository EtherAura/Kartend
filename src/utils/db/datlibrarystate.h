#ifndef DATLIBRARYSTATE_H
#define DATLIBRARYSTATE_H

#include <functional>
#include <optional>

#include <QList>
#include <QSet>
#include <QString>

#include "errorutils.h"

class QSqlDatabase;

/// Persistence for DAT-library scan state (Kartend-m6qsb.5): which proposals
/// the user dismissed with "don't ask again". Schema v19 table
/// `dat_library_dismissal`, keyed by the DAT's canonical path with the mtime
/// it was dismissed at — an updated DAT (new mtime) becomes proposable again,
/// since a refreshed catalogue is a genuinely new suggestion.
///
/// Free functions taking `QSqlDatabase &` first, matching the other
/// src/utils/db stores. Not a QObject.
namespace DatLibraryState {

/// Dismissal keys as "canonicalPath|mtimeMs" strings — the same key the
/// library scanner builds for each candidate DAT, so filtering is one
/// QSet::contains per file.
[[nodiscard]] ErrorUtils::Result<QSet<QString>> loadDismissalKeys(QSqlDatabase &db);

/// Record (or refresh) a dismissal for `canonicalPath` at `mtimeMs`.
[[nodiscard]] ErrorUtils::Result<bool> addDismissal(QSqlDatabase &db, const QString &canonicalPath,
                                                    qint64 mtimeMs);

/// Build the composite key the scanner and store share.
[[nodiscard]] inline QString dismissalKey(const QString &canonicalPath, qint64 mtimeMs) {
  return canonicalPath + QLatin1Char('|') + QString::number(mtimeMs);
}

/// Where a library DAT came from (Kartend-m6qsb.26), recorded at download/import
/// time so a later "check for updates" pass can ask the source for a newer
/// revision. Schema v21 table `dat_library_provenance`, keyed by canonical path.
struct Provenance {
  QString canonicalPath;
  QString source;   ///< "nointro" / "redump" / "" (unknown — e.g. generic import).
  QString slug;     ///< Redump system slug; empty for No-Intro.
  int systemId = 0; ///< No-Intro system id; 0 for Redump.
  QString version;  ///< Revision at fetch time (No-Intro pack date / Redump header version).
  qint64 updatedAtMs = 0;
};

/// Record (or replace) the provenance of the DAT at `p.canonicalPath`.
[[nodiscard]] ErrorUtils::Result<bool> recordProvenance(QSqlDatabase &db, const Provenance &p);

/// Load the provenance for one DAT, or nullopt when none was recorded (e.g. a
/// DAT that predates provenance tracking, or a hand-copied file).
[[nodiscard]] ErrorUtils::Result<std::optional<Provenance>> loadProvenance(QSqlDatabase &db,
                                                                           const QString &path);

/// All recorded provenance rows — the input to a "check for updates" pass.
[[nodiscard]] ErrorUtils::Result<QList<Provenance>> loadAllProvenance(QSqlDatabase &db);

/// True when `latestVersion` differs from `storedVersion` (and both are
/// non-empty): a different revision at the source means an update is available.
/// Deliberately "different", not "strictly newer" — these sources don't
/// publish downgrades, and version strings (No-Intro pack dates, Redump header
/// versions) aren't a uniform orderable format. Empty on either side = "can't
/// tell" = no update flagged.
[[nodiscard]] bool isUpdateAvailable(const QString &storedVersion, const QString &latestVersion);

/// The subset of `all` for which the source reports a newer revision
/// (Kartend-m6qsb.31). `fetchLatest(p)` returns the source's current version
/// (empty = couldn't determine — skipped, never flagged). Entries with an
/// empty source or empty stored version are skipped too. Each returned entry's
/// `version` is set to the freshly-fetched latest, so the caller can persist it
/// after re-downloading. Pure given the fetcher — the network lives in the
/// caller's `fetchLatest`, so the selection logic is unit-testable with a stub.
[[nodiscard]] QList<Provenance>
outdatedAmong(const QList<Provenance> &all,
              const std::function<QString(const Provenance &)> &fetchLatest);

} // namespace DatLibraryState

#endif // DATLIBRARYSTATE_H
