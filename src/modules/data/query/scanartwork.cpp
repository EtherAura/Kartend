// Scan-side artwork resolution — see scanartwork.h for why the column is
// filled here rather than resolved inside the DB-side predicates.
#include "scanartwork.h"

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <utility>

#include "artworkutils.h"
#include "dbtxn.h"
#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScanArtwork {

namespace {

/// Staged rows read per round. Keeps the pass's peak memory flat on a
/// hundred-thousand-item library, in the spirit of the streaming walk it runs
/// behind. Not bind-derived: the SELECT takes two binds regardless of batch
/// size and the UPDATE is executed one row at a time off a single prepared
/// statement, so the SQLite variable limit is not in play here.
constexpr int kResolveBatchRows = 512;

void reportQueryFailure(const QString &what, const QSqlQuery &query) {
  ErrorUtils::logError(
      ErrorContext::warning(ErrorCode::DatabaseQueryFailed, what, QStringLiteral("ScanArtwork"))
          .withDetails(query.lastError().text()));
}

/// The cover for one staged row, or empty.
///
/// Probes the two name keys findArtworkForFileCached probes, in its order: the
/// extension-stripped stem first, then the full file name (art literally named
/// "Title.iso.png" backs item "Title.iso" through the second key). Both are
/// derived from the staged absolute path rather than the staged `name` column,
/// because every render-time caller derives them from `items.path` — the two
/// agree for ordinary files and the path is the one that stays right for
/// dotfiles, whose staged `name` falls back to the full file name.
[[nodiscard]] QString resolveForPath(const QHash<QString, QString> &byKey, const QString &path) {
  const QFileInfo info(path);
  const QString baseName = info.completeBaseName();
  const QString fileName = info.fileName();
  if (!baseName.isEmpty()) {
    const QString hit = byKey.value(ArtworkUtils::baseMatchKey(baseName));
    if (!hit.isEmpty()) {
      return hit;
    }
  }
  if (fileName == baseName) {
    return {};
  }
  return byKey.value(ArtworkUtils::baseMatchKey(fileName));
}

} // namespace

int resolveStagedArtwork(QSqlDatabase &db, int &txnDepth, const QString &artworkDirectory) {
  if (!db.isOpen() || artworkDirectory.trimmed().isEmpty()) {
    return 0;
  }

  const QStringList lookupDirs = ArtworkUtils::artworkLookupDirectories(artworkDirectory);
  if (lookupDirs.isEmpty()) {
    return 0;
  }
  // REFRESH, not warm. A blocking walk because this pass has to give a
  // COMPLETE answer — a cold cascade would silently record "artless" for a
  // fully-arted library — and an unconditional one because a listing cached
  // earlier in the session predates whatever the user has done to the artwork
  // directory since, cached negatives included. It is bounded work: one walk of
  // the flat root plus the typed cover subdirs, once per scan, never per item,
  // and it leaves the cache warm and current for the grid that follows.
  ArtworkUtils::DirectoryCache::instance().refreshDirectories(lookupDirs);

  const QHash<QString, QString> byKey = ArtworkUtils::buildArtworkPathMap(artworkDirectory);
  if (byKey.isEmpty()) {
    // No cover anywhere in the cascade. Every staged row stays NULL, which the
    // apply writes over whatever the previous scan recorded — so a library
    // whose artwork directory was emptied clears rather than keeps ghosts.
    return 0;
  }

  KartendDb::DbTransaction txn(db, txnDepth, "ScanArtwork::resolveStagedArtwork");
  if (!txn.activeOrReport(QStringLiteral("Failed to start staged-artwork transaction"),
                          ErrorUtils::Severity::Warning)) {
    return 0;
  }

  QSqlQuery sel(db);
  if (!sel.prepare(QStringLiteral("SELECT rowid, path FROM scanned_items "
                                  "WHERE rowid > ? ORDER BY rowid LIMIT ?"))) {
    reportQueryFailure(QStringLiteral("Failed to prepare staged-artwork read"), sel);
    return 0; // guard dtor rolls back
  }
  QSqlQuery upd(db);
  if (!upd.prepare(QStringLiteral("UPDATE scanned_items SET artwork_path = ? WHERE rowid = ?"))) {
    reportQueryFailure(QStringLiteral("Failed to prepare staged-artwork write"), upd);
    return 0;
  }

  qint64 lastRowId = 0;
  int stored = 0;
  for (;;) {
    sel.bindValue(0, lastRowId);
    sel.bindValue(1, kResolveBatchRows);
    if (!sel.exec()) {
      reportQueryFailure(QStringLiteral("Failed to read staged rows for artwork resolution"), sel);
      return 0;
    }

    // Drain the cursor fully before writing: the UPDATEs below touch the very
    // table this SELECT reads, and keyset pagination only stays correct
    // because rowids are never rewritten.
    QList<std::pair<qint64, QString>> hits;
    int rowsRead = 0;
    while (sel.next()) {
      ++rowsRead;
      const qint64 rowId = sel.value(0).toLongLong();
      if (rowId > lastRowId) {
        lastRowId = rowId;
      }
      QString artwork = resolveForPath(byKey, sel.value(1).toString());
      if (!artwork.isEmpty()) {
        hits.append({rowId, std::move(artwork)});
      }
    }
    if (rowsRead == 0) {
      break;
    }

    for (const auto &hit : hits) {
      upd.bindValue(0, hit.second);
      upd.bindValue(1, hit.first);
      if (!upd.exec()) {
        reportQueryFailure(QStringLiteral("Failed to store staged artwork path"), upd);
        return 0;
      }
      ++stored;
    }
  }

  if (!txn.commitOrReport(QStringLiteral("Failed to commit staged artwork paths"),
                          ErrorUtils::Severity::Warning)) {
    return 0;
  }
  return stored;
}

} // namespace ScanArtwork
