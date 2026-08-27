// Scan-side metadata sidecar hydration — see scanmetadata.h for why the
// sidecars are read here rather than at scrape time.
#include "scanmetadata.h"

#include <optional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

#include "dbtxn.h"
#include "errorutils.h"
#include "itemmetadata.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScanMetadata {

namespace {

/// Item rows read per round, matching ScanArtwork's batch. Keeps peak memory
/// flat on a large library; the SELECT's bind count does not vary with it.
constexpr int kHydrateBatchRows = 512;

/// Ceiling on one sidecar file. These are a few hundred bytes of JSON; the
/// directory is user-writable, so a file wearing the .json name is not
/// necessarily one of ours and must not be read whole on trust.
constexpr qint64 kMaxSidecarBytes = 1LL * 1024 * 1024;

void reportQueryFailure(const QString &what, const QSqlQuery &query) {
  ErrorUtils::logError(
      ErrorContext::warning(ErrorCode::DatabaseQueryFailed, what, QStringLiteral("ScanMetadata"))
          .withDetails(query.lastError().text()));
}

/// baseName -> absolute sidecar path for `<artworkDirectory>/metadata`.
/// Listed ONCE so the per-item cost is a hash lookup rather than a stat.
QHash<QString, QString> listSidecars(const QString &artworkDirectory) {
  QHash<QString, QString> byBaseName;
  if (artworkDirectory.trimmed().isEmpty()) {
    return byBaseName;
  }
  QDir dir(QDir(artworkDirectory).filePath(QStringLiteral("metadata")));
  if (!dir.exists()) {
    return byBaseName; // never scraped, or no sidecars written yet
  }
  const QFileInfoList entries =
      dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable);
  byBaseName.reserve(entries.size());
  for (const QFileInfo &entry : entries) {
    byBaseName.insert(entry.completeBaseName(), entry.absoluteFilePath());
  }
  return byBaseName;
}

/// Parse one sidecar into a row for (@p collectionUuid, @p path).
///
/// Mirrors writeMetadataSidecar's shape in scrapepersistence.cpp: flat string
/// fields, `runtimeSeconds` as a number, `tags` as an inline array and
/// `customFields` as an object. Returns nullopt when the file is unreadable,
/// oversized, not a JSON object, or carries no title — a sidecar without a
/// title is what the writer skips, so one here is not a record we wrote.
std::optional<ItemMetadataStore::ItemMetadata>
parseSidecar(const QString &sidecarPath, const QString &collectionUuid, const QString &path) {
  QFile file(sidecarPath);
  if (file.size() > kMaxSidecarBytes || !file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    return std::nullopt;
  }
  const QJsonObject obj = doc.object();

  ItemMetadataStore::ItemMetadata row;
  row.collectionUuid = collectionUuid;
  row.path = path;
  const auto str = [&obj](const char *key) {
    return obj.value(QLatin1String(key)).toString().trimmed();
  };
  row.title = str("title");
  if (row.title.isEmpty()) {
    return std::nullopt;
  }
  row.description = str("description");
  row.genre = str("genre");
  row.developer = str("developer");
  row.publisher = str("publisher");
  row.releaseDate = str("releaseDate");
  row.contentRating = str("contentRating");
  row.players = str("players");
  row.source = str("source");
  if (const QJsonValue runtime = obj.value(QStringLiteral("runtimeSeconds")); runtime.isDouble()) {
    row.runtimeSeconds = runtime.toInt(-1);
  }
  // Both are stored as JSON TEXT in the row, and the sidecar already holds
  // them as real JSON — re-serialise rather than hand-build the strings, so
  // the escaping rule is Qt's on both sides.
  if (const QJsonValue tags = obj.value(QStringLiteral("tags")); tags.isArray()) {
    row.tags = QString::fromUtf8(QJsonDocument(tags.toArray()).toJson(QJsonDocument::Compact));
  }
  if (const QJsonValue custom = obj.value(QStringLiteral("customFields")); custom.isObject()) {
    row.customFields =
        QString::fromUtf8(QJsonDocument(custom.toObject()).toJson(QJsonDocument::Compact));
  }
  return row;
}

} // namespace

int hydrateFromSidecars(QSqlDatabase &db, int &txnDepth, const QString &artworkDirectory,
                        const QString &collectionUuid) {
  if (!db.isOpen() || collectionUuid.trimmed().isEmpty()) {
    return 0;
  }
  const QHash<QString, QString> sidecars = listSidecars(artworkDirectory);
  if (sidecars.isEmpty()) {
    return 0; // the common case: nothing was ever scraped into this collection
  }

  KartendDb::DbTransaction txn(db, txnDepth, "ScanMetadata::hydrateFromSidecars");
  if (!txn.activeOrReport(QStringLiteral("Failed to start sidecar hydration transaction"),
                          ErrorUtils::Severity::Warning)) {
    return 0;
  }

  // Items with NO metadata row. The LEFT JOIN is what enforces the precedence
  // rule in the header — an item that already has a row is never a candidate,
  // so nothing here can overwrite a scrape or a hand edit.
  QSqlQuery sel(db);
  if (!sel.prepare(QStringLiteral(
          "SELECT i.rowid, i.path FROM items i "
          "LEFT JOIN item_metadata m ON m.collection_uuid = i.collection_uuid AND m.path = i.path "
          "WHERE i.collection_uuid = ? AND m.path IS NULL AND i.rowid > ? "
          "ORDER BY i.rowid LIMIT ?"))) {
    reportQueryFailure(QStringLiteral("Failed to prepare sidecar hydration read"), sel);
    return 0;
  }

  qint64 lastRowId = 0;
  int written = 0;
  for (;;) {
    sel.bindValue(0, collectionUuid);
    sel.bindValue(1, lastRowId);
    sel.bindValue(2, kHydrateBatchRows);
    if (!sel.exec()) {
      reportQueryFailure(QStringLiteral("Failed to read items for sidecar hydration"), sel);
      return written;
    }

    // Drain before writing: the saves below touch item_metadata, which the
    // SELECT above joins against — same reason ScanArtwork drains first.
    QList<std::pair<qint64, QString>> candidates;
    while (sel.next()) {
      const qint64 rowId = sel.value(0).toLongLong();
      if (rowId > lastRowId) {
        lastRowId = rowId;
      }
      candidates.append({rowId, sel.value(1).toString()});
    }
    if (candidates.isEmpty()) {
      break;
    }

    for (const auto &[rowId, path] : candidates) {
      Q_UNUSED(rowId)
      const auto sidecar = sidecars.constFind(QFileInfo(path).completeBaseName());
      if (sidecar == sidecars.constEnd()) {
        continue;
      }
      const auto row = parseSidecar(sidecar.value(), collectionUuid, path);
      if (!row) {
        continue;
      }
      if (auto saved = ItemMetadataStore::save(db, *row); saved.isError()) {
        ErrorUtils::logError(saved.error());
        continue;
      }
      ++written;
    }
  }

  if (written > 0 && !txn.commit()) {
    return 0;
  }
  return written;
}

} // namespace ScanMetadata
