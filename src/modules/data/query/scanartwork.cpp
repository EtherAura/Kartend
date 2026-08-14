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
#include "itemartwork.h"

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

/// Every per-item MANUAL artwork link in @p collectionUuid that could supply a
/// cover, as item path -> (artwork_type -> manual_path).
///
/// Read in ONE query rather than per staged row: `item_artwork` only holds rows
/// the user created by hand, so this is empty for most libraries and a handful
/// of rows for the rest — far cheaper than a correlated lookup per item, and it
/// keeps the row loop below free of SQL. Types outside the cover cascade
/// (`logo`, custom types) are filtered out in SQL so gallery-only links never
/// reach resolveCoverPath.
[[nodiscard]] QHash<QString, QHash<QString, QString>>
loadManualCoverLinks(QSqlDatabase &db, const QString &collectionUuid) {
  QHash<QString, QHash<QString, QString>> byPath;
  if (collectionUuid.isEmpty()) {
    return byPath;
  }
  const QStringList coverTypes = ArtworkUtils::coverSubdirPriority();
  if (coverTypes.isEmpty()) {
    return byPath;
  }

  QStringList placeholders;
  placeholders.reserve(coverTypes.size());
  for (int i = 0; i < coverTypes.size(); ++i) {
    placeholders.append(QStringLiteral("?"));
  }
  QSqlQuery q(db);
  if (!q.prepare(QStringLiteral("SELECT path, artwork_type, manual_path FROM item_artwork "
                                "WHERE collection_uuid = ? AND manual_path IS NOT NULL "
                                "AND manual_path != '' AND artwork_type IN (") +
                 placeholders.join(QStringLiteral(",")) + QStringLiteral(")"))) {
    reportQueryFailure(QStringLiteral("Failed to prepare manual artwork-link read"), q);
    return byPath;
  }
  q.addBindValue(collectionUuid);
  for (const QString &type : coverTypes) {
    q.addBindValue(type);
  }
  if (!q.exec()) {
    // Degrade to auto-discovery only rather than failing the scan: a missing
    // manual layer under-reports a few items, an aborted pass under-reports
    // every one of them.
    reportQueryFailure(QStringLiteral("Failed to read manual artwork links"), q);
    return byPath;
  }
  while (q.next()) {
    const QString path = q.value(0).toString();
    if (path.isEmpty()) {
      continue;
    }
    byPath[path].insert(q.value(1).toString(), q.value(2).toString());
  }
  return byPath;
}

} // namespace

int resolveStagedArtwork(QSqlDatabase &db, int &txnDepth, const QString &artworkDirectory,
                         const QString &collectionUuid) {
  if (!db.isOpen()) {
    return 0;
  }

  // The manual layer is read FIRST because it decides whether there is any work
  // at all for a collection with no artwork directory: a link is an absolute
  // path the user picked, so it resolves without one.
  const QHash<QString, QHash<QString, QString>> manualLinks =
      loadManualCoverLinks(db, collectionUuid);

  QHash<QString, QString> byKey;
  const QStringList lookupDirs = ArtworkUtils::artworkLookupDirectories(artworkDirectory);
  if (!artworkDirectory.trimmed().isEmpty() && !lookupDirs.isEmpty()) {
    // REFRESH, not warm. A blocking walk because this pass has to give a
    // COMPLETE answer — a cold cascade would silently record "artless" for a
    // fully-arted library — and an unconditional one because a listing cached
    // earlier in the session predates whatever the user has done to the artwork
    // directory since, cached negatives included. It is bounded work: one walk
    // of the flat root plus the typed cover subdirs, once per scan, never per
    // item, and it leaves the cache warm and current for the grid that follows.
    ArtworkUtils::DirectoryCache::instance().refreshDirectories(lookupDirs);
    byKey = ArtworkUtils::buildArtworkPathMap(artworkDirectory);
  }

  if (byKey.isEmpty() && manualLinks.isEmpty()) {
    // No cover anywhere in the cascade and nothing hand-linked. Every staged
    // row stays NULL, which the apply writes over whatever the previous scan
    // recorded — so a library whose artwork directory was emptied clears
    // rather than keeps ghosts.
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
      const QString path = sel.value(1).toString();
      // One rule, shared with the save-time write-through: a manual link on a
      // cover type wins when its file still exists, else the auto-discovered
      // cover stands. The stat only happens for items the user hand-linked —
      // manualLinks is empty for the rest, and resolveCoverPath short-circuits.
      QString artwork =
          ItemArtworkStore::resolveCoverPath(manualLinks.value(path), resolveForPath(byKey, path));
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
