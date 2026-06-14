#ifndef DATLIBRARYSTATE_H
#define DATLIBRARYSTATE_H

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

} // namespace DatLibraryState

#endif // DATLIBRARYSTATE_H
