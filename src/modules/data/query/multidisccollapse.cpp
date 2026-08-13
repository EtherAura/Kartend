// Database/disk half of multi-disc grouping — see multidisccollapse.h for the
// placement rationale (post-pass over the staged rows, not a filter inside the
// streaming walk) and the regeneration rules that fall out of it.
#include "multidisccollapse.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "dbtxn.h"
#include "errorutils.h"
#include "itemmetadata.h"
#include "pathutils.h"

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace MultiDiscCollapse {

namespace {

// Length of the directory digest appended to a playlist file name when two
// releases in different folders share a base title. Eight hex characters is
// ~4 billion buckets over a set that is realistically single-digit — the
// collision risk is negligible and the name stays readable.
constexpr int DIR_DIGEST_CHARS = 8;

void reportQueryFailure(const QString &what, const QSqlQuery &query) {
  ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed, what,
                                             QStringLiteral("MultiDiscCollapse"))
                           .withDetails(query.lastError().text()));
}

/// "?, ?, ?" for an IN-list of @p count binds.
[[nodiscard]] QString placeholders(int count) {
  QStringList marks;
  marks.reserve(count);
  for (int i = 0; i < count; ++i) {
    marks.append(QStringLiteral("?"));
  }
  return marks.join(QStringLiteral(", "));
}

/// Absolute paths of staged rows whose file name could carry a disc tag.
///
/// Narrowed in SQL rather than materialising every staged path: the whole point
/// of the streaming pipeline is that a large library never has its file list in
/// memory at once, and this pass must not quietly undo that. A disc marker only
/// ever appears inside a parenthesised or bracketed tag group
/// (MultiDisc::parse), so a name with neither bracket cannot group and does not
/// need to be read. `name` is the staged completeBaseName, which still carries
/// the tags — only the extension was stripped.
[[nodiscard]] QStringList stagedGroupCandidates(QSqlDatabase &db) {
  QStringList out;
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT path FROM scanned_items "
                             "WHERE name LIKE '%(%' OR name LIKE '%[%'"))) {
    reportQueryFailure(QStringLiteral("Failed to read staged multi-disc candidates"), q);
    return out;
  }
  while (q.next()) {
    out.append(q.value(0).toString());
  }
  return out;
}

/// Playlist file name per group, disambiguated deterministically.
///
/// Two releases that share a base title under different folders would otherwise
/// both want "<Title>.m3u" in the collection's single managed directory. The
/// suffix is derived from the release's directory and applied to EVERY member
/// of a colliding set — never "first one wins" — because group order follows
/// the filesystem walk, which is not stable across scans on the parallel
/// recursive arm. A run-order-dependent name would rewrite both playlists on
/// every scan for no reason.
[[nodiscard]] QStringList playlistFileNames(const QList<MultiDisc::Group> &groups) {
  QHash<QString, int> plainNameCounts;
  QStringList plainNames;
  plainNames.reserve(groups.size());
  for (const MultiDisc::Group &g : groups) {
    const QString name = MultiDisc::m3uFileNameFor(g.base);
    plainNames.append(name);
    ++plainNameCounts[name.toLower()];
  }

  QStringList out;
  out.reserve(groups.size());
  for (int i = 0; i < groups.size(); ++i) {
    if (plainNameCounts.value(plainNames.at(i).toLower()) < 2) {
      out.append(plainNames.at(i));
      continue;
    }
    const QString dir = QFileInfo(groups.at(i).files.first()).absolutePath();
    const QString digest =
        QString::fromLatin1(QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Sha1)
                                .toHex()
                                .left(DIR_DIGEST_CHARS));
    out.append(MultiDisc::m3uFileNameFor(groups.at(i).base + QStringLiteral(" ~") + digest));
  }
  return out;
}

/// Delete every .m3u in @p playlistDir that this scan did not generate.
///
/// This is the cleanup arm for a release that lost a disc (dropping below the
/// two-file threshold that makes it a group), was renamed, or was deleted from
/// disk outright. Scoped to the collection's own managed directory and to the
/// .m3u extension, so nothing else can be caught by it.
void sweepOrphanPlaylists(const QString &playlistDir, const QSet<QString> &keep) {
  QDir dir(playlistDir);
  if (!dir.exists()) {
    return;
  }
  const QStringList existing =
      dir.entryList(QStringList{QStringLiteral("*.m3u")}, QDir::Files | QDir::NoSymLinks);
  for (const QString &name : existing) {
    if (keep.contains(name)) {
      continue;
    }
    if (!QFile::remove(dir.filePath(name))) {
      qCWarning(lcQueryManager) << "MultiDiscCollapse: failed to remove stale playlist"
                                << dir.filePath(name);
    }
  }
}

/// items.artwork_path for each of @p paths that has one.
[[nodiscard]] QHash<QString, QString> artworkPathsFor(QSqlDatabase &db, const QString &uuid,
                                                      const QStringList &paths) {
  QHash<QString, QString> out;
  if (paths.isEmpty()) {
    return out;
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT path, artwork_path FROM items "
                           "WHERE collection_uuid = ? AND path IN (%1)")
                .arg(placeholders(paths.size())));
  q.addBindValue(uuid);
  for (const QString &p : paths) {
    q.addBindValue(p);
  }
  if (!q.exec()) {
    reportQueryFailure(QStringLiteral("Failed to read member artwork paths"), q);
    return out;
  }
  while (q.next()) {
    const QString artwork = q.value(1).toString();
    if (!artwork.isEmpty()) {
      out.insert(q.value(0).toString(), artwork);
    }
  }
  return out;
}

[[nodiscard]] MultiDisc::DiscMetadata toDiscMetadata(const ItemMetadataStore::ItemMetadata &row,
                                                     const QString &artworkPath) {
  MultiDisc::DiscMetadata disc;
  disc.artworkPath = artworkPath;
  disc.notes = row.notes;
  disc.sourceUrl = row.sourceUrl;
  disc.rating = row.rating;
  disc.tags = ItemMetadataStore::parseTags(row.tags);
  disc.customFields = ItemMetadataStore::parseCustomFields(row.customFields);
  disc.pinned = row.isPinned;
  disc.hidden = row.isHidden;
  disc.continueLater = row.continueLater;
  return disc;
}

/// Reconcile the members' stored metadata into what the collapsed item should
/// carry. Read before the apply phase, which prunes the rows this walks.
[[nodiscard]] MultiDisc::DiscMetadata
readMergedMemberMetadata(QSqlDatabase &db, const QString &uuid, const QStringList &memberPaths) {
  const QHash<QString, QString> artwork = artworkPathsFor(db, uuid, memberPaths);
  const auto loaded = ItemMetadataStore::loadBatch(db, uuid, memberPaths);
  if (loaded.isError()) {
    ErrorUtils::logError(loaded.error());
  }

  QList<MultiDisc::DiscMetadata> discs;
  discs.reserve(memberPaths.size());
  for (const QString &path : memberPaths) {
    const ItemMetadataStore::ItemMetadata row =
        loaded.isOk() ? loaded.value().value(path) : ItemMetadataStore::ItemMetadata{};
    discs.append(toDiscMetadata(row, artwork.value(path)));
  }
  return MultiDisc::mergeDiscs(discs);
}

/// The staged attributes the collapsed row inherits from its members.
struct MemberRollup {
  /// Media-dir-relative directory the members live in ("." at the root).
  QString relativeDir;
  /// Most recent member mtime — a change to any disc is a change to the release.
  QDateTime lastModified;
  /// Total bytes across every disc, which is the number a person sorting by
  /// size actually means by "how big is this release".
  qint64 totalSize = 0;
  bool valid = false;
};

[[nodiscard]] MemberRollup rollUpStagedMembers(QSqlDatabase &db, const QStringList &memberPaths) {
  MemberRollup rollup;
  if (memberPaths.isEmpty()) {
    return rollup;
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT rel_path, last_modified, file_size FROM scanned_items "
                           "WHERE path IN (%1)")
                .arg(placeholders(memberPaths.size())));
  for (const QString &p : memberPaths) {
    q.addBindValue(p);
  }
  if (!q.exec()) {
    reportQueryFailure(QStringLiteral("Failed to read staged multi-disc members"), q);
    return rollup;
  }
  while (q.next()) {
    if (rollup.relativeDir.isEmpty()) {
      rollup.relativeDir = QFileInfo(q.value(0).toString()).path();
    }
    const QDateTime mtime = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
    if (mtime.isValid() && (!rollup.lastModified.isValid() || mtime > rollup.lastModified)) {
      rollup.lastModified = mtime;
    }
    rollup.totalSize += q.value(2).toLongLong();
    rollup.valid = true;
  }
  return rollup;
}

/// Swap the members' staged rows for the one row the .m3u will become.
///
/// rel_path is built from the members' own relative directory rather than from
/// the .m3u's real location: the playlist lives in Kartend's data directory,
/// but the item it stands for belongs in the folder its discs are in, and
/// rel_path is what the subfolder / virtual-folder queries browse by.
[[nodiscard]] bool replaceStagedMembers(QSqlDatabase &db, const CollapsedGroup &group) {
  const MemberRollup rollup = rollUpStagedMembers(db, group.memberPaths);
  if (!rollup.valid) {
    return false;
  }

  QSqlQuery del(db);
  del.prepare(QStringLiteral("DELETE FROM scanned_items WHERE path IN (%1)")
                  .arg(placeholders(group.memberPaths.size())));
  for (const QString &p : group.memberPaths) {
    del.addBindValue(p);
  }
  if (!del.exec()) {
    reportQueryFailure(QStringLiteral("Failed to unstage multi-disc members"), del);
    return false;
  }

  const QString fileName = QFileInfo(group.playlistPath).fileName();
  const QString relativePath =
      (rollup.relativeDir.isEmpty() || rollup.relativeDir == QStringLiteral("."))
          ? fileName
          : rollup.relativeDir + QLatin1Char('/') + fileName;
  const QDateTime lastModified =
      rollup.lastModified.isValid() ? rollup.lastModified : QDateTime::fromSecsSinceEpoch(0);

  QSqlQuery ins(db);
  ins.prepare(QStringLiteral("INSERT OR REPLACE INTO scanned_items "
                             "(path, rel_path, name, last_modified, file_size) "
                             "VALUES (?, ?, ?, ?, ?)"));
  ins.addBindValue(group.playlistPath);
  ins.addBindValue(relativePath);
  ins.addBindValue(group.base);
  ins.addBindValue(lastModified.toString(Qt::ISODate));
  ins.addBindValue(rollup.totalSize);
  if (!ins.exec()) {
    reportQueryFailure(QStringLiteral("Failed to stage multi-disc playlist row"), ins);
    return false;
  }
  return true;
}

} // namespace

QList<CollapsedGroup> collapseStagedGroups(QSqlDatabase &db, int &txnDepth,
                                           const QString &collectionUuid,
                                           const QString &playlistDir) {
  if (!db.isOpen() || collectionUuid.isEmpty() || playlistDir.isEmpty()) {
    return {};
  }

  const QList<MultiDisc::Group> groups = MultiDisc::group(stagedGroupCandidates(db));
  if (groups.isEmpty()) {
    // Nothing groups any more — every playlist still sitting in the managed
    // directory belongs to a release that lost a disc or vanished from disk.
    sweepOrphanPlaylists(playlistDir, {});
    return {};
  }

  const QStringList fileNames = playlistFileNames(groups);
  QSet<QString> generated;
  QList<CollapsedGroup> collapsed;
  collapsed.reserve(groups.size());

  for (int i = 0; i < groups.size(); ++i) {
    const MultiDisc::Group &group = groups.at(i);
    const QString playlistPath = QDir(playlistDir).filePath(fileNames.at(i));
    const QByteArray body = MultiDisc::buildM3uContents(group.files, playlistDir).toUtf8();
    if (!PathUtils::atomicWriteFile(playlistPath, body)) {
      // atomicWriteFile already logged the failing stage. Leave this release's
      // members staged as individual items rather than dropping them: an item
      // per disc is a worse presentation than one per release, but an item
      // pointing at a playlist that does not exist is a broken library.
      continue;
    }
    generated.insert(fileNames.at(i));

    CollapsedGroup entry;
    entry.playlistPath = playlistPath;
    entry.base = group.base;
    entry.memberPaths = group.files;
    entry.merged = readMergedMemberMetadata(db, collectionUuid, group.files);
    collapsed.append(entry);
  }

  sweepOrphanPlaylists(playlistDir, generated);

  // One transaction for the whole staging rewrite: a failure part-way must not
  // leave the table holding a playlist row whose members are still staged (a
  // duplicate item) or members deleted with no playlist row to replace them
  // (silent data loss on the apply's prune step). Rolling back means the scan
  // proceeds exactly as if grouping were off, which is always safe.
  KartendDb::DbTransaction txn(db, txnDepth, "MultiDiscCollapse::collapseStagedGroups");
  if (!txn.activeOrReport(QStringLiteral("Failed to start multi-disc collapse transaction"),
                          ErrorUtils::Severity::Warning)) {
    return {};
  }
  for (const CollapsedGroup &group : collapsed) {
    if (!replaceStagedMembers(db, group)) {
      return {}; // guard dtor rolls back the partial rewrite
    }
  }
  if (!txn.commitOrReport(QStringLiteral("Failed to commit multi-disc collapse"),
                          ErrorUtils::Severity::Warning)) {
    return {};
  }

  return collapsed;
}

void applyMergedMetadata(QSqlDatabase &db, const QString &collectionUuid,
                         const QList<CollapsedGroup> &groups) {
  if (!db.isOpen()) {
    return;
  }

  for (const CollapsedGroup &group : groups) {
    const auto existing = ItemMetadataStore::load(db, collectionUuid, group.playlistPath);
    if (existing.isError()) {
      ErrorUtils::logError(existing.error());
      continue;
    }
    const ItemMetadataStore::ItemMetadata &before = existing.value();

    // The collapsed item's own row goes in FIRST, which under mergeDiscs' rule
    // (first non-empty wins) makes it authoritative over every disc. An edit
    // the user made on the collapsed item therefore survives every later
    // rescan, while a disc still fills anything the collapsed item leaves
    // empty.
    const MultiDisc::DiscMetadata merged =
        MultiDisc::mergeDiscs({toDiscMetadata(before, QString()), group.merged});

    ItemMetadataStore::ItemMetadata after = before;
    after.collectionUuid = collectionUuid;
    after.path = group.playlistPath;
    after.notes = merged.notes;
    after.sourceUrl = merged.sourceUrl;
    after.rating = merged.rating;
    after.tags = ItemMetadataStore::serializeTags(merged.tags);
    after.customFields = ItemMetadataStore::serializeCustomFields(merged.customFields);
    after.isPinned = merged.pinned;
    after.isHidden = merged.hidden;
    after.continueLater = merged.continueLater;

    // Only write when the merge actually changed something. save() always
    // refreshes updated_at, so an unconditional write would restamp every
    // collapsed item on every rescan and make "recently edited" meaningless.
    if (after != before) {
      const auto saved = ItemMetadataStore::save(db, after);
      if (saved.isError()) {
        ErrorUtils::logError(saved.error());
      }
    }

    if (merged.artworkPath.isEmpty()) {
      continue;
    }
    // Fill-missing-never-overwrite, same rule as the scalar merge above: art
    // already resolved for the collapsed item wins over a disc's.
    QSqlQuery art(db);
    art.prepare(QStringLiteral("UPDATE items SET artwork_path = ? "
                               "WHERE collection_uuid = ? AND path = ? "
                               "AND (artwork_path IS NULL OR artwork_path = '')"));
    art.addBindValue(merged.artworkPath);
    art.addBindValue(collectionUuid);
    art.addBindValue(group.playlistPath);
    if (!art.exec()) {
      reportQueryFailure(QStringLiteral("Failed to apply merged multi-disc artwork"), art);
    }
  }
}

bool discardPlaylists(const QString &playlistDir) {
  if (playlistDir.isEmpty()) {
    return true;
  }
  QDir dir(playlistDir);
  if (!dir.exists()) {
    return true;
  }
  if (!dir.removeRecursively()) {
    qCWarning(lcQueryManager) << "MultiDiscCollapse: failed to remove managed playlist directory"
                              << playlistDir;
    return false;
  }
  return true;
}

} // namespace MultiDiscCollapse
