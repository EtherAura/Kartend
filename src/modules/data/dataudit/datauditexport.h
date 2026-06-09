#ifndef DATAUDITEXPORT_H
#define DATAUDITEXPORT_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "dataudittypes.h"

/// Export helpers turning audit results into the file formats the prior-art
/// tools standardized on. Pure (no I/O) so they are fully unit-testable; the
/// caller writes the bytes via QSaveFile.
namespace DatAudit {

/// Flat CSV, one row per AuditRow, with a header. Columns:
/// status,game,expected_name,actual_name,file_path,size,crc,md5,sha1. Fields
/// are escaped per RFC 4180 (quote-wrapped when they contain a comma / quote /
/// newline; embedded quotes doubled). The "filter the Status column in a
/// spreadsheet" triage view (igir's report shape).
[[nodiscard]] QByteArray toCsv(const QList<AuditRow> &rows);

/// A Logiqx-XML "fixdat": a <datafile> containing only the Missing entries, so
/// it round-trips into any DAT tool to fetch exactly the gaps. `datName`
/// labels the header. Re-parses cleanly via DatLookup::parseLogiqxDat. The
/// single highest-leverage export — turns "what's missing" into a re-runnable
/// acquisition target.
[[nodiscard]] QByteArray toFixdat(const QList<AuditRow> &rows,
                                  const QString &datName = QStringLiteral("Kartend audit"));

/// Plain-text "miss list": one missing entry's canonical name per line
/// (gameName, falling back to expectedName), sorted and de-duplicated — a
/// simple re-acquisition shopping list.
[[nodiscard]] QString toMissList(const QList<AuditRow> &rows);

} // namespace DatAudit

#endif // DATAUDITEXPORT_H
