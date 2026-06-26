#include "playlistmanager.h"

#include "playlistserializer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

#include <optional>

#include "connectionpragmas.h"
#include "dbtxn.h"
#include "errorutils.h"
#include "pathutils.h"
#include "uiconstants/database.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {

// Upper bounds on user-/file-supplied strings before they reach the TEXT NOT
// NULL columns. A crafted or corrupt JSON envelope could otherwise carry
// megabyte-sized paths or garbage UUID strings straight into playlist_items /
// playlists (Kartend-2sg2t). These caps are generous relative to any real
// filesystem path or display name but bound worst-case row size.
constexpr int kMaxSourcePathLength = 4096;
// source_collection_uuid is NOT a QUuid: collection identity is a 40-char SHA-1
// hex digest (CollectionUtils::computeCollectionUuid). A QUuid::fromString
// round-trip would therefore reject every real ref, so we bound the length only
// — generous over the 40-char digest while still capping worst-case row size.
constexpr int kMaxUuidLength = 128;
constexpr int kMaxPlaylistNameLength = 512;

// Validate a (uuid, path) item ref before it is persisted. Caps both string
// lengths so a crafted/corrupt envelope cannot inject megabyte-sized values
// into the TEXT NOT NULL columns. Returns true when the ref is acceptable.
// Empty path/uuid handling stays with the callers (path-empty is a skip; an
// empty uuid is a legitimate "no source collection" ref), so this only rejects
// over-long values (Kartend-2sg2t). Note: collection uuids are SHA-1 hex
// digests, not RFC-4122 UUIDs, so no QUuid::fromString well-formedness check is
// applied — it would reject every production ref.
bool validateItemRef(const QString &uuid, const QString &path) {
  if (path.length() > kMaxSourcePathLength) {
    return false;
  }
  if (uuid.length() > kMaxUuidLength) {
    return false;
  }
  return true;
}

QString isoNow() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString newPlaylistId() {
  // QUuid::createUuid() is the same RNG everything else in the codebase uses
  // for opaque ids; trim the curly braces so the value plays nicely as a
  // foreign key value in URLs / debug logs.
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Shared INSERT for playlist_items — one statement string + one positional
// bind so the four insert sites (addItem, removeItem re-densify, importFromJson,
// importFromM3U) can't drift on column order (Kartend audit D-09). Uses
// bindValue(pos) rather than addBindValue so it also works when a single
// prepared query is reused across a loop (the removeItem re-densify path).
const QString kInsertPlaylistItemSql =
    QStringLiteral("INSERT INTO playlist_items (playlist_id, position, "
                   "source_collection_uuid, source_path, added_at) "
                   "VALUES (?, ?, ?, ?, ?)");

void bindPlaylistItemRow(QSqlQuery &q, const QString &playlistId, int position,
                         const QString &sourceCollectionUuid, const QString &sourcePath,
                         const QString &addedAt) {
  q.bindValue(0, playlistId);
  q.bindValue(1, position);
  q.bindValue(2, sourceCollectionUuid);
  q.bindValue(3, sourcePath);
  q.bindValue(4, addedAt);
}

} // namespace

PlaylistManager::PlaylistManager(QObject *parent)
    : IPlaylistManager(parent), m_connectionName(QStringLiteral("kartend_playlists_main")) {}

PlaylistManager::~PlaylistManager() {
  assertOwnerThread();
  if (m_db.isValid()) {
    QString name = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
}

bool PlaylistManager::initialize() {
  assertOwnerThread();
  if (m_db.isOpen()) {
    return true;
  }

  // Mirror DatabaseManager::initDatabase() — same media.db file under
  // AppDataLocation, separate Qt SQL connection name so the main-thread
  // playlist queries don't trip over the existing main-thread metadata
  // connection or the worker-thread query connection.
  QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!QDir().mkpath(dbPath)) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                               "Failed to create database directory", "PlaylistManager::initialize")
            .withDetails(QStringLiteral("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    return false;
  }

  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_db.setDatabaseName(dbPath + QStringLiteral("/media.db"));
  if (!m_db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to open database for playlists",
                                      "PlaylistManager::initialize")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }

  // Foreign keys must be enabled per-connection; the cascade in
  // playlist_items relies on it. Kartend-67wo: routed through the shared
  // PRAGMA helper so future additions to the standard set (e.g. a future
  // mmap_size setting) reach this connection too. journal_mode (WAL) is a
  // database-wide setting persisted in the file header by the primary openers,
  // so it is intentionally left off here. busy_timeout, by contrast, is
  // per-connection and is NOT inherited: without it this secondary writer takes
  // an immediate SQLITE_BUSY (no retry) whenever a scan / metadata write holds
  // the lock, surfacing as a silent playlist-edit failure — so give it the main
  // connection's timeout, sized to tolerate long scan-worker write transactions
  // (Kartend-3uls).
  MediaDbConnectionInit::PragmaConfig cfg;
  cfg.enableForeignKeys = true;
  cfg.enableWalWithFallback = false;
  cfg.setSynchronousNormal = false;
  cfg.busyTimeoutMs = UIConstants::Database::MAIN_BUSY_TIMEOUT_MS;
  MediaDbConnectionInit::applyPragmas(m_db, cfg, QStringLiteral("PlaylistManager::initialize"));

  // Create the playlist tables locally (idempotent CREATE TABLE IF NOT EXISTS)
  // rather than re-running the full schema migration suite. The full migration
  // touches `items` / `items_fts`, which only exist after DatabaseManager has
  // finished initialising — running it from this connection in isolation
  // (e.g. unit tests, or a startup-order surprise) produces a flood of "no
  // such table: items" errors. The DDL here mirrors the v10 migration so the
  // production startup path (which does run full migrations) and the
  // standalone path stay schema-compatible.
  QSqlQuery ddl(m_db);
  if (!ddl.exec("CREATE TABLE IF NOT EXISTS playlists ("
                "id TEXT PRIMARY KEY, "
                "name TEXT NOT NULL, "
                "icon TEXT NOT NULL DEFAULT '', "
                "parent_collection_uuid TEXT NOT NULL DEFAULT '', "
                "reserved_kind TEXT NOT NULL DEFAULT '', "
                "created_at TEXT NOT NULL DEFAULT '', "
                "updated_at TEXT NOT NULL DEFAULT '', "
                "is_smart INTEGER NOT NULL DEFAULT 0, "
                "smart_filter TEXT NOT NULL DEFAULT ''"
                ")")) {
    auto err =
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to ensure playlists table",
                              "PlaylistManager::initialize")
            .withDetails(ddl.lastError().text());
    ErrorUtils::logError(err);
  }
  // Mirror the v11 ensureColumn calls so a long-lived install that already
  // has a v10 playlists table picks up the smart-playlist columns when the
  // standalone PlaylistManager opens its connection (the full migration
  // suite normally handles this via DatabaseManager, but the standalone
  // path stays schema-compatible by repeating the additive columns here).
  ddl.exec("ALTER TABLE playlists ADD COLUMN is_smart INTEGER NOT NULL DEFAULT 0");
  ddl.exec("ALTER TABLE playlists ADD COLUMN smart_filter TEXT NOT NULL DEFAULT ''");
  if (!ddl.exec("CREATE TABLE IF NOT EXISTS playlist_items ("
                "playlist_id TEXT NOT NULL, "
                "position INTEGER NOT NULL, "
                "source_collection_uuid TEXT NOT NULL, "
                "source_path TEXT NOT NULL, "
                "added_at TEXT NOT NULL DEFAULT '', "
                "PRIMARY KEY (playlist_id, position), "
                "FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE"
                ")")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to ensure playlist_items table",
                                     "PlaylistManager::initialize")
                   .withDetails(ddl.lastError().text());
    ErrorUtils::logError(err);
  }
  ddl.exec("CREATE INDEX IF NOT EXISTS idx_playlist_items_lookup "
           "ON playlist_items(source_collection_uuid, source_path)");
  ddl.exec("CREATE INDEX IF NOT EXISTS idx_playlist_items_playlist "
           "ON playlist_items(playlist_id)");
  return true;
}

ErrorUtils::Result<QString> PlaylistManager::createPlaylist(const QString &name,
                                                            const QString &parentCollectionUuid,
                                                            const QString &reservedKind) {
  assertOwnerThread();
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::createPlaylist");
  }
  if (name.trimmed().isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist name is empty",
                               "PlaylistManager::createPlaylist");
  }
  if (name.trimmed().length() > kMaxPlaylistNameLength) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist name is too long",
                               "PlaylistManager::createPlaylist");
  }

  const QString id = newPlaylistId();
  const QString now = isoNow();

  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "INSERT INTO playlists (id, name, icon, parent_collection_uuid, reserved_kind, "
      "created_at, updated_at) VALUES (?, ?, '', ?, ?, ?, ?)"));
  q.addBindValue(id);
  q.addBindValue(name.trimmed());
  // QSqlQuery::addBindValue(QString()) binds NULL, which collides with the
  // NOT NULL constraint on parent_collection_uuid / reserved_kind. Coerce
  // null QStrings to "" so the default-argument call sites don't have to.
  q.addBindValue(parentCollectionUuid.isNull() ? QString("") : parentCollectionUuid);
  q.addBindValue(reservedKind.isNull() ? QString("") : reservedKind);
  q.addBindValue(now);
  q.addBindValue(now);
  if (!q.exec()) {
    auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to create playlist",
                                   "PlaylistManager::createPlaylist")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return err;
  }
  emit playlistsChanged();
  return id;
}

ErrorUtils::Result<QString>
PlaylistManager::createSmartPlaylist(const QString &name, const SmartFilter::Filter &filter,
                                     const QString &parentCollectionUuid) {
  assertOwnerThread();
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::createSmartPlaylist");
  }
  if (name.trimmed().isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist name is empty",
                               "PlaylistManager::createSmartPlaylist");
  }
  if (name.trimmed().length() > kMaxPlaylistNameLength) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist name is too long",
                               "PlaylistManager::createSmartPlaylist");
  }

  const QString id = newPlaylistId();
  const QString now = isoNow();
  const QString filterJson = SmartFilter::toJsonString(filter);

  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "INSERT INTO playlists (id, name, icon, parent_collection_uuid, reserved_kind, "
      "created_at, updated_at, is_smart, smart_filter) VALUES (?, ?, '', ?, '', ?, ?, 1, ?)"));
  q.addBindValue(id);
  q.addBindValue(name.trimmed());
  q.addBindValue(parentCollectionUuid.isNull() ? QString("") : parentCollectionUuid);
  q.addBindValue(now);
  q.addBindValue(now);
  q.addBindValue(filterJson);
  if (!q.exec()) {
    auto err =
        ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to create smart playlist",
                            "PlaylistManager::createSmartPlaylist")
            .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return err;
  }
  emit playlistsChanged();
  return id;
}

bool PlaylistManager::updateSmartFilter(const QString &id, const SmartFilter::Filter &filter) {
  assertOwnerThread();
  if (!m_db.isOpen() || id.isEmpty()) {
    return false;
  }
  const QString filterJson = SmartFilter::toJsonString(filter);
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "UPDATE playlists SET smart_filter = ?, updated_at = ? WHERE id = ? AND is_smart = 1"));
  q.addBindValue(filterJson);
  q.addBindValue(isoNow());
  q.addBindValue(id);
  if (!q.exec() || q.numRowsAffected() <= 0) {
    auto err =
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to update smart filter",
                              "PlaylistManager::updateSmartFilter")
            .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  emit playlistsChanged();
  return true;
}

ErrorUtils::Result<SmartFilter::Filter> PlaylistManager::loadSmartFilter(const QString &id) const {
  assertOwnerThread();
  if (!m_db.isOpen() || id.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Database closed or empty id",
                               "PlaylistManager::loadSmartFilter");
  }
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("SELECT is_smart, smart_filter FROM playlists WHERE id = ?"));
  q.addBindValue(id);
  if (!q.exec() || !q.next()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Playlist not found",
                               "PlaylistManager::loadSmartFilter");
  }
  if (q.value(0).toInt() == 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist is not a smart playlist",
                               "PlaylistManager::loadSmartFilter");
  }
  return SmartFilter::fromJsonString(q.value(1).toString());
}

bool PlaylistManager::renamePlaylist(const QString &id, const QString &newName) {
  assertOwnerThread();
  if (!m_db.isOpen() || id.isEmpty() || newName.trimmed().isEmpty()) {
    return false;
  }
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("UPDATE playlists SET name = ?, updated_at = ? WHERE id = ?"));
  q.addBindValue(newName.trimmed());
  q.addBindValue(isoNow());
  q.addBindValue(id);
  if (!q.exec() || q.numRowsAffected() <= 0) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to rename playlist",
                                     "PlaylistManager::renamePlaylist")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  emit playlistsChanged();
  return true;
}

bool PlaylistManager::deletePlaylist(const QString &id) {
  assertOwnerThread();
  if (!m_db.isOpen() || id.isEmpty()) {
    return false;
  }

  // refuse to drop reserved playlists. Built-ins (favorites and
  // any future named slot) must outlive the user's menu choices — if we let
  // the favorites row vanish, the next ensureFavoritesPlaylist() would
  // recreate it under a fresh id, dropping every starred reference along the
  // way and silently breaking favorite-aware UIs that cached the previous id.
  QSqlQuery probe(m_db);
  probe.prepare(QStringLiteral("SELECT reserved_kind FROM playlists WHERE id = ?"));
  probe.addBindValue(id);
  if (probe.exec() && probe.next() && !probe.value(0).toString().isEmpty()) {
    auto err =
        ErrorContext::warning(ErrorCode::InvalidArgument, "Refusing to delete reserved playlist",
                              "PlaylistManager::deletePlaylist")
            .withDetails(QStringLiteral("Reserved kind: %1").arg(probe.value(0).toString()));
    ErrorUtils::logError(err);
    return false;
  }

  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("DELETE FROM playlists WHERE id = ?"));
  q.addBindValue(id);
  if (!q.exec() || q.numRowsAffected() <= 0) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to delete playlist",
                                     "PlaylistManager::deletePlaylist")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  emit playlistsChanged();
  return true;
}

bool PlaylistManager::addItem(const QString &playlistId, const QString &sourceCollectionUuid,
                              const QString &sourcePath) {
  assertOwnerThread();
  if (!m_db.isOpen() || playlistId.isEmpty() || sourcePath.isEmpty()) {
    return false;
  }
  if (!validateItemRef(sourceCollectionUuid, sourcePath)) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::InvalidArgument,
                                               "Rejecting over-long playlist item ref",
                                               "PlaylistManager::addItem")
                             .withDetails(QStringLiteral("path length: %1, uuid length: %2")
                                              .arg(sourcePath.length())
                                              .arg(sourceCollectionUuid.length())));
    return false;
  }
  if (containsItem(playlistId, sourceCollectionUuid, sourcePath)) {
    return false; // Idempotent: the chooser surface treats re-add as a no-op.
  }

  // Wrap the position read + insert + touch in a transaction so the three
  // statements are atomic: two interleaved addItem calls (or a re-entrant edit
  // during a batch add) can't both compute the same MAX(position)+1 and collide
  // on the (playlist_id, position) PK, and a half-applied add can't leave the
  // insert without its updated_at stamp. Mirrors removeItem (Kartend-du3m).
  // Depth-less guard: PlaylistManager owns its connection and never nests
  // (Kartend-l94tw); every early-return below rolls back via the dtor.
  KartendDb::DbTransaction txn(m_db, "PlaylistManager::addItem");
  if (!txn.activeOrReport("Failed to begin add-item transaction", ErrorUtils::Severity::Warning)) {
    return false;
  }

  // Append at the end. We compute the next position as MAX(position)+1 so
  // ordering survives concurrent writes without needing a separate counter.
  QSqlQuery posQuery(m_db);
  posQuery.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_items "
                                  "WHERE playlist_id = ?"));
  posQuery.addBindValue(playlistId);
  if (!posQuery.exec() || !posQuery.next()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to compute next playlist position",
                                     "PlaylistManager::addItem")
                   .withDetails(posQuery.lastError().text());
    ErrorUtils::logError(err);
    return false; // guard dtor rolls back
  }
  const int nextPosition = posQuery.value(0).toInt();

  QSqlQuery insert(m_db);
  insert.prepare(kInsertPlaylistItemSql);
  bindPlaylistItemRow(insert, playlistId, nextPosition, sourceCollectionUuid, sourcePath, isoNow());
  if (!insert.exec()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to insert playlist item", "PlaylistManager::addItem")
                   .withDetails(insert.lastError().text());
    ErrorUtils::logError(err);
    return false; // guard dtor rolls back
  }

  // Stamp the parent's updated_at so a future "sort playlists by recency"
  // (follow-up) can rely on it. Best-effort: a failed touch does not abort the
  // transaction in SQLite, and the stamp is non-essential.
  QSqlQuery touch(m_db);
  touch.prepare(QStringLiteral("UPDATE playlists SET updated_at = ? WHERE id = ?"));
  touch.addBindValue(isoNow());
  touch.addBindValue(playlistId);
  touch.exec();

  if (!txn.commitOrReport("Failed to commit add-item transaction", ErrorUtils::Severity::Warning)) {
    return false; // guard dtor rolls back the aborted transaction (no-op)
  }

  emit playlistsChanged();
  return true;
}

bool PlaylistManager::removeItem(const QString &playlistId, const QString &sourceCollectionUuid,
                                 const QString &sourcePath) {
  assertOwnerThread();
  if (!m_db.isOpen() || playlistId.isEmpty()) {
    return false;
  }
  // Wrap in a transaction so the delete + re-densify either both apply or
  // neither — a half-applied delete would leave gap holes that confuse
  // ORDER BY position consumers. Depth-less guard (Kartend-l94tw): every
  // early-return below rolls back via the dtor.
  KartendDb::DbTransaction txn(m_db, "PlaylistManager::removeItem");
  if (!txn.activeOrReport("Failed to begin remove-item transaction",
                          ErrorUtils::Severity::Warning)) {
    return false;
  }

  QSqlQuery del(m_db);
  del.prepare(QStringLiteral(
      "DELETE FROM playlist_items WHERE playlist_id = ? AND source_collection_uuid = ? "
      "AND source_path = ?"));
  del.addBindValue(playlistId);
  del.addBindValue(sourceCollectionUuid);
  del.addBindValue(sourcePath);
  if (!del.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to delete playlist item",
                                               "PlaylistManager::removeItem")
                             .withDetails(del.lastError().text()));
    return false; // guard dtor rolls back
  }
  if (del.numRowsAffected() <= 0) {
    return false; // item not present — benign no-op (no row removed)
  }

  // Re-densify positions to 0..N-1. The PK on (playlist_id, position) means we
  // can't UPDATE the surviving rows in place (intermediate states would
  // collide), so dump them into a temp ordering, wipe, and re-insert.
  QSqlQuery surviving(m_db);
  surviving.prepare(
      QStringLiteral("SELECT source_collection_uuid, source_path, added_at FROM playlist_items "
                     "WHERE playlist_id = ? ORDER BY position"));
  surviving.addBindValue(playlistId);
  if (!surviving.exec()) {
    return false; // guard dtor rolls back
  }

  struct Row {
    QString uuid;
    QString path;
    QString addedAt;
  };
  QList<Row> rows;
  while (surviving.next()) {
    rows.push_back({surviving.value(0).toString(), surviving.value(1).toString(),
                    surviving.value(2).toString()});
  }

  QSqlQuery wipe(m_db);
  wipe.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ?"));
  wipe.addBindValue(playlistId);
  if (!wipe.exec()) {
    return false; // guard dtor rolls back
  }

  QSqlQuery reinsert(m_db);
  reinsert.prepare(kInsertPlaylistItemSql);
  for (int i = 0; i < rows.size(); ++i) {
    bindPlaylistItemRow(reinsert, playlistId, i, rows[i].uuid, rows[i].path, rows[i].addedAt);
    if (!reinsert.exec()) {
      return false; // guard dtor rolls back
    }
  }

  // Previously a silent failure; the guard now logs it (Kartend-l94tw).
  if (!txn.commitOrReport("Failed to commit remove-item transaction",
                          ErrorUtils::Severity::Warning)) {
    return false; // guard dtor rolls back the aborted transaction (no-op)
  }

  QSqlQuery touch(m_db);
  touch.prepare(QStringLiteral("UPDATE playlists SET updated_at = ? WHERE id = ?"));
  touch.addBindValue(isoNow());
  touch.addBindValue(playlistId);
  touch.exec();

  emit playlistsChanged();
  return true;
}

QList<PlaylistRow> PlaylistManager::loadAll() const {
  assertOwnerThread();
  QList<PlaylistRow> result;
  if (!m_db.isOpen()) {
    return result;
  }
  QSqlQuery q(m_db);
  if (!q.exec(QStringLiteral("SELECT id, name, icon, parent_collection_uuid, reserved_kind, "
                             "created_at, updated_at, is_smart, smart_filter FROM playlists"))) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to load playlists",
                                     "PlaylistManager::loadAll")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return result;
  }
  while (q.next()) {
    PlaylistRow row;
    row.id = q.value(0).toString();
    row.name = q.value(1).toString();
    row.icon = q.value(2).toString();
    row.parentCollectionUuid = q.value(3).toString();
    row.reservedKind = q.value(4).toString();
    row.createdAt = q.value(5).toString();
    row.updatedAt = q.value(6).toString();
    row.isSmart = q.value(7).toInt() != 0;
    row.smartFilterJson = q.value(8).toString();
    result.push_back(row);
  }
  return result;
}

QList<PlaylistItemRef> PlaylistManager::loadItems(const QString &playlistId) const {
  assertOwnerThread();
  QList<PlaylistItemRef> result;
  if (!m_db.isOpen() || playlistId.isEmpty()) {
    return result;
  }
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "SELECT position, source_collection_uuid, source_path, added_at FROM playlist_items "
      "WHERE playlist_id = ? ORDER BY position"));
  q.addBindValue(playlistId);
  if (!q.exec()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to load playlist items", "PlaylistManager::loadItems")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return result;
  }
  while (q.next()) {
    PlaylistItemRef ref;
    ref.position = q.value(0).toInt();
    ref.sourceCollectionUuid = q.value(1).toString();
    ref.sourcePath = q.value(2).toString();
    ref.addedAt = q.value(3).toString();
    result.push_back(ref);
  }
  return result;
}

bool PlaylistManager::containsItem(const QString &playlistId, const QString &sourceCollectionUuid,
                                   const QString &sourcePath) const {
  assertOwnerThread();
  if (!m_db.isOpen() || playlistId.isEmpty()) {
    return false;
  }
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral(
      "SELECT 1 FROM playlist_items WHERE playlist_id = ? AND source_collection_uuid = ? "
      "AND source_path = ? LIMIT 1"));
  q.addBindValue(playlistId);
  q.addBindValue(sourceCollectionUuid);
  q.addBindValue(sourcePath);
  return q.exec() && q.next();
}

QString PlaylistManager::ensureFavoritesPlaylist(const QString &defaultName) {
  assertOwnerThread();
  if (!m_favoritesId.isEmpty()) {
    return m_favoritesId;
  }
  if (!m_db.isOpen()) {
    // Kartend-cywbk: every failure path logs so a silent "starring does
    // nothing" degrade (the empty id is later swallowed by addItem) is
    // diagnosable from the call site rather than only at the DB layer.
    ErrorUtils::logError(ErrorContext::warning(
        ErrorCode::DatabaseNotOpen, "Cannot ensure favorites playlist: database not open",
        "PlaylistManager::ensureFavoritesPlaylist"));
    return QString();
  }

  // Probe for an existing reserved row. The reserved_kind column is unique-
  // by-convention rather than by SQL constraint (a UNIQUE on reserved_kind
  // would block the empty-string default that user playlists carry); a stray
  // duplicate is harmless because we just take the first.
  QSqlQuery probe(m_db);
  if (!probe.exec("SELECT id FROM playlists WHERE reserved_kind = 'favorites' LIMIT 1")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to probe for favorites playlist",
                                     "PlaylistManager::ensureFavoritesPlaylist")
                   .withDetails(probe.lastError().text());
    ErrorUtils::logError(err);
    return QString();
  }
  if (probe.next()) {
    m_favoritesId = probe.value(0).toString();
    return m_favoritesId;
  }

  // Doesn't exist yet — create it. Uses the same insert path as user-created
  // playlists so the playlistsChanged signal propagates to the synthesizer
  // and the row appears in the sidebar without a restart.
  auto created = createPlaylist(defaultName, QString(), QStringLiteral("favorites"));
  if (created.isError()) {
    // Kartend-cywbk: createPlaylist logs the underlying DB failure, but log
    // again at this call site so the bootstrap-failure root cause (which
    // otherwise degrades into a permanent, invisible "stars don't persist" for
    // the rest of the session) is unambiguous in the logs.
    ErrorUtils::logError(
        ErrorContext::warning(created.error().code,
                              "Failed to create favorites playlist; favorites will not persist",
                              "PlaylistManager::ensureFavoritesPlaylist")
            .withDetails(created.error().message));
    return QString();
  }
  m_favoritesId = created.value();
  return m_favoritesId;
}

// ============================================================================
// Import / export
// ============================================================================

namespace {

PlaylistRow loadPlaylistRow(QSqlDatabase &db, const QString &id) {
  PlaylistRow row;
  QSqlQuery q(db);
  q.prepare("SELECT id, name, icon, parent_collection_uuid, reserved_kind, "
            "created_at, updated_at, is_smart, smart_filter FROM playlists WHERE id = ?");
  q.addBindValue(id);
  if (q.exec() && q.next()) {
    row.id = q.value(0).toString();
    row.name = q.value(1).toString();
    row.icon = q.value(2).toString();
    row.parentCollectionUuid = q.value(3).toString();
    row.reservedKind = q.value(4).toString();
    row.createdAt = q.value(5).toString();
    row.updatedAt = q.value(6).toString();
    row.isSmart = q.value(7).toInt() != 0;
    row.smartFilterJson = q.value(8).toString();
  }
  return row;
}

} // namespace

ErrorUtils::Result<int> PlaylistManager::exportToJson(const QString &playlistId,
                                                      const QString &outPath) const {
  assertOwnerThread();
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::exportToJson");
  }
  if (playlistId.isEmpty() || outPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "playlistId and outPath are required",
                               "PlaylistManager::exportToJson");
  }
  // const_cast: loadPlaylistRow takes a non-const QSqlDatabase& because Qt's
  // QSqlQuery constructor wants a mutable reference, but the operation is
  // logically a read.
  PlaylistRow row = loadPlaylistRow(const_cast<QSqlDatabase &>(m_db), playlistId);
  if (row.id.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist not found",
                               "PlaylistManager::exportToJson");
  }
  // Format + file I/O live in PlaylistSerializer (Kartend-k4alv); this method
  // keeps only the DB reads feeding it.
  return PlaylistSerializer::exportJson(row, loadItems(playlistId), outPath);
}

ErrorUtils::Result<int> PlaylistManager::exportToM3U(const QString &playlistId,
                                                     const QString &outPath) const {
  assertOwnerThread();
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::exportToM3U");
  }
  if (playlistId.isEmpty() || outPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "playlistId and outPath are required",
                               "PlaylistManager::exportToM3U");
  }
  return PlaylistSerializer::exportM3U(loadItems(playlistId), outPath);
}

ErrorUtils::Result<QString> PlaylistManager::importFromJson(const QString &inPath,
                                                            const QString &nameOverride,
                                                            int *outSkipped) {
  assertOwnerThread();
  if (outSkipped) {
    *outSkipped = 0;
  }
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::importFromJson");
  }
  // Envelope parsing lives in PlaylistSerializer (Kartend-k4alv); this method
  // keeps the CRUD + transactional item insert.
  auto parsed = PlaylistSerializer::parseJsonFile(inPath);
  if (parsed.isError()) {
    return parsed.error();
  }
  const QString name =
      nameOverride.trimmed().isEmpty() ? parsed.value().name : nameOverride.trimmed();
  if (name.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist name is empty",
                               "PlaylistManager::importFromJson");
  }
  const QString parentUuid = parsed.value().parentCollectionUuid;

  // v2: smart playlist round-trip. When is_smart is true and the embedded
  // smart_filter parses cleanly, recreate as a smart playlist (no items
  // are imported — they'll be evaluated on first open). v1 documents
  // never carry these fields so they fall through to the static path.
  if (parsed.value().isSmart) {
    auto filterResult = SmartFilter::fromJsonString(parsed.value().smartFilterJson);
    if (filterResult.isError()) {
      return filterResult.error();
    }
    return createSmartPlaylist(name, filterResult.value(), parentUuid);
  }

  auto created = createPlaylist(name, parentUuid);
  if (created.isError()) {
    return created.error();
  }
  const QString &newId = created.value();

  // Kartend-fd7xl: insert all items in a SINGLE transaction so a mid-loop
  // failure or crash can't leave a half-populated playlist the user sees as
  // "imported". On any failure we roll the items back AND drop the freshly
  // created (now-empty) playlist so the import fails cleanly rather than
  // leaving an orphan. (addItem opens its own per-item transaction, so we
  // insert directly here instead of looping it — positions are a dense 0-based
  // sequence for the fresh playlist, and an in-memory set reproduces addItem's
  // idempotent (uuid,path) de-duplication.)
  //
  // The DbTransaction guard (Kartend-l94tw) lives inside the immediately
  // invoked lambda so its rollback runs BEFORE the deletePlaylist cleanup —
  // deletePlaylist issues its own DELETE, which must not execute inside the
  // still-open (about to be rolled back) import transaction.
  //
  // Kartend-2sg2t: mirror importFromM3U's skip accounting — empty-path,
  // duplicate, and over-long/invalid item refs are counted so the caller can
  // surface "N items skipped" instead of the import silently dropping them
  // while still reporting success.
  int skipped = 0;
  const auto insertFailure = [&]() -> std::optional<ErrorUtils::ErrorContext> {
    KartendDb::DbTransaction txn(m_db, "PlaylistManager::importFromJson");
    if (!txn.active()) {
      return txn.beginError("Failed to begin playlist import transaction", nullptr,
                            ErrorUtils::Severity::Error);
    }
    const QString addedAt = isoNow();
    QSet<QString> seen;
    int position = 0;
    for (const PlaylistSerializer::ParsedPlaylist::Item &entry : parsed.value().items) {
      const QString &uuid = entry.sourceCollectionUuid;
      const QString &path = entry.sourcePath;
      if (path.isEmpty()) {
        ++skipped;
        continue;
      }
      if (!validateItemRef(uuid, path)) {
        ++skipped;
        ErrorUtils::logError(
            ErrorContext::warning(ErrorCode::InvalidArgument,
                                  "Skipping over-long/invalid imported playlist item ref",
                                  "PlaylistManager::importFromJson")
                .withDetails(QStringLiteral("path length: %1, uuid length: %2")
                                 .arg(path.length())
                                 .arg(uuid.length())));
        continue;
      }
      const QString dedupKey = uuid + QLatin1Char('\x1f') + path;
      if (seen.contains(dedupKey)) {
        ++skipped;
        continue;
      }
      seen.insert(dedupKey);

      QSqlQuery insert(m_db);
      insert.prepare(kInsertPlaylistItemSql);
      bindPlaylistItemRow(insert, newId, position++, uuid, path, addedAt);
      if (!insert.exec()) {
        auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                       "Failed to insert imported playlist item",
                                       "PlaylistManager::importFromJson")
                       .withDetails(insert.lastError().text());
        ErrorUtils::logError(err);
        return err; // guard dtor rolls back
      }
    }
    if (!txn.commit()) {
      auto err =
          txn.commitError("Failed to commit playlist import", nullptr, ErrorUtils::Severity::Error);
      ErrorUtils::logError(err);
      return err;
    }
    return std::nullopt;
  }();
  if (insertFailure) {
    (void)deletePlaylist(newId);
    return *insertFailure;
  }
  emit playlistsChanged();
  if (outSkipped) {
    *outSkipped = skipped;
  }
  return newId;
}

ErrorUtils::Result<QString> PlaylistManager::importFromM3U(const QString &inPath,
                                                           const QString &playlistName,
                                                           int *outSkipped, int *outAmbiguous) {
  assertOwnerThread();
  if (outSkipped) {
    *outSkipped = 0;
  }
  if (outAmbiguous) {
    *outAmbiguous = 0;
  }
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::importFromM3U");
  }
  // File reading lives in PlaylistSerializer (Kartend-k4alv); read the entry
  // paths up front so an unreadable file errors out before any CRUD.
  auto paths = PlaylistSerializer::readM3UPaths(inPath);
  if (paths.isError()) {
    return paths.error();
  }

  const QString name = playlistName.trimmed().isEmpty() ? QFileInfo(inPath).completeBaseName()
                                                        : playlistName.trimmed();
  auto created = createPlaylist(name);
  if (created.isError()) {
    return created.error();
  }
  const QString &newId = created.value();

  // Resolve each path via the live items table — playlist_items is keyed by
  // (source_collection_uuid, source_path), so we have to know which source
  // collection owns the path to round-trip correctly. Paths the items table
  // doesn't recognise are skipped (counted in *outSkipped) rather than
  // stored as broken refs; the user can re-import once collections have
  // been rescanned and the paths become resolvable.
  //
  // Kartend-o84pt: M3U carries no collection identity (unlike the lossless JSON
  // format, which stores source_collection_uuid), so a path that exists in more
  // than one collection is genuinely ambiguous on import. ORDER BY makes the
  // pick DETERMINISTIC (lowest uuid) instead of letting SQLite's row order
  // decide, and we log the ambiguity so the round-trip's lossiness is visible
  // rather than silent.
  QSqlQuery resolve(m_db);
  resolve.prepare("SELECT collection_uuid FROM items WHERE path = ? ORDER BY collection_uuid");

  // Kartend-k695z: resolve everything FIRST, then insert in one transaction —
  // mirroring importFromJson (Kartend-fd7xl). The old per-line addItem() loop
  // opened one transaction + one MAX(position) probe + one playlistsChanged
  // emission per line, and a mid-loop DB failure left a half-imported
  // playlist presented as success.
  struct ResolvedEntry {
    QString uuid;
    QString path;
  };
  QList<ResolvedEntry> resolved;
  resolved.reserve(paths.value().size());
  QSet<QString> seen;
  int skipped = 0;
  int ambiguous = 0;
  for (const QString &line : paths.value()) {
    // Kartend-2sg2t: bound the file-supplied path before it reaches the DB so a
    // crafted M3U cannot inject a megabyte-sized source_path. (The resolved
    // uuid comes from the trusted items table, so the path is the only
    // attacker-controlled half here.)
    if (!validateItemRef(QString(), line)) {
      ++skipped;
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::InvalidArgument,
                                                 "M3U import: skipping over-long path",
                                                 "PlaylistManager::importFromM3U")
                               .withDetails(QStringLiteral("path length: %1").arg(line.length())));
      continue;
    }
    resolve.bindValue(0, line);
    if (!resolve.exec() || !resolve.next()) {
      ++skipped;
      continue;
    }
    const QString uuid = resolve.value(0).toString();
    if (resolve.next()) {
      ++ambiguous;
      ErrorUtils::logError(
          ErrorContext::warning(ErrorCode::InvalidArgument,
                                "M3U import: path exists in multiple collections; M3U can't "
                                "disambiguate, using the lowest collection uuid",
                                "PlaylistManager::importFromM3U")
              .withDetails(line));
    }
    // Reproduce addItem's idempotent (uuid, path) de-duplication in memory.
    const QString dedupKey = uuid + QLatin1Char('\x1f') + line;
    if (seen.contains(dedupKey)) {
      continue;
    }
    seen.insert(dedupKey);
    resolved.append(ResolvedEntry{uuid, line});
  }

  // Guard scoped inside the immediately invoked lambda for the same reason as
  // importFromJson above: its rollback must run before deletePlaylist.
  const auto insertFailure = [&]() -> std::optional<ErrorUtils::ErrorContext> {
    KartendDb::DbTransaction txn(m_db, "PlaylistManager::importFromM3U");
    if (!txn.active()) {
      return txn.beginError("Failed to begin M3U import transaction", nullptr,
                            ErrorUtils::Severity::Error);
    }
    const QString addedAt = isoNow();
    int position = 0;
    for (const ResolvedEntry &entry : resolved) {
      QSqlQuery insert(m_db);
      insert.prepare(kInsertPlaylistItemSql);
      bindPlaylistItemRow(insert, newId, position++, entry.uuid, entry.path, addedAt);
      if (!insert.exec()) {
        auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                       "Failed to insert imported M3U playlist item",
                                       "PlaylistManager::importFromM3U")
                       .withDetails(insert.lastError().text());
        ErrorUtils::logError(err);
        return err; // guard dtor rolls back
      }
    }
    if (!txn.commit()) {
      auto err = txn.commitError("Failed to commit M3U playlist import", nullptr,
                                 ErrorUtils::Severity::Error);
      ErrorUtils::logError(err);
      return err;
    }
    return std::nullopt;
  }();
  if (insertFailure) {
    (void)deletePlaylist(newId);
    return *insertFailure;
  }
  emit playlistsChanged();

  if (outSkipped) {
    *outSkipped = skipped;
  }
  // Kartend-93ju5: surface how many paths resolved ambiguously (existed in more
  // than one collection, so the lowest-uuid pick may have bound them to the
  // wrong source collection) so the import caller / UI can warn the user that
  // resolution was lossy rather than reporting an unqualified success.
  if (outAmbiguous) {
    *outAmbiguous = ambiguous;
  }
  return newId;
}
