#include "playlistmanager.h"

#include <QDateTime>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {

QString isoNow() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString newPlaylistId() {
  // QUuid::createUuid() is the same RNG everything else in the codebase uses
  // for opaque ids; trim the curly braces so the value plays nicely as a
  // foreign key value in URLs / debug logs.
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

PlaylistManager::PlaylistManager(QObject *parent)
    : QObject(parent), m_connectionName(QStringLiteral("kartend_playlists_main")) {}

PlaylistManager::~PlaylistManager() {
  if (m_db.isValid()) {
    QString name = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
}

bool PlaylistManager::initialize() {
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
  // playlist_items relies on it.
  QSqlQuery pragma(m_db);
  pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

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
                "updated_at TEXT NOT NULL DEFAULT ''"
                ")")) {
    auto err =
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to ensure playlists table",
                              "PlaylistManager::initialize")
            .withDetails(ddl.lastError().text());
    ErrorUtils::logError(err);
  }
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
  if (!m_db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistManager::createPlaylist");
  }
  if (name.trimmed().isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist name is empty",
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

bool PlaylistManager::renamePlaylist(const QString &id, const QString &newName) {
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
  if (!m_db.isOpen() || id.isEmpty()) {
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
  if (!m_db.isOpen() || playlistId.isEmpty() || sourcePath.isEmpty()) {
    return false;
  }
  if (containsItem(playlistId, sourceCollectionUuid, sourcePath)) {
    return false; // Idempotent: the chooser surface treats re-add as a no-op.
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
    return false;
  }
  const int nextPosition = posQuery.value(0).toInt();

  QSqlQuery insert(m_db);
  insert.prepare(
      QStringLiteral("INSERT INTO playlist_items (playlist_id, position, source_collection_uuid, "
                     "source_path, added_at) VALUES (?, ?, ?, ?, ?)"));
  insert.addBindValue(playlistId);
  insert.addBindValue(nextPosition);
  insert.addBindValue(sourceCollectionUuid);
  insert.addBindValue(sourcePath);
  insert.addBindValue(isoNow());
  if (!insert.exec()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to insert playlist item", "PlaylistManager::addItem")
                   .withDetails(insert.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }

  // Stamp the parent's updated_at so a future "sort playlists by recency"
  // (Kartend-vlm7 follow-up) can rely on it.
  QSqlQuery touch(m_db);
  touch.prepare(QStringLiteral("UPDATE playlists SET updated_at = ? WHERE id = ?"));
  touch.addBindValue(isoNow());
  touch.addBindValue(playlistId);
  touch.exec();

  emit playlistsChanged();
  return true;
}

bool PlaylistManager::removeItem(const QString &playlistId, const QString &sourceCollectionUuid,
                                 const QString &sourcePath) {
  if (!m_db.isOpen() || playlistId.isEmpty()) {
    return false;
  }
  // Wrap in a transaction so the delete + re-densify either both apply or
  // neither — a half-applied delete would leave gap holes that confuse
  // ORDER BY position consumers.
  if (!m_db.transaction()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to begin remove-item transaction",
                                     "PlaylistManager::removeItem")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }

  QSqlQuery del(m_db);
  del.prepare(QStringLiteral(
      "DELETE FROM playlist_items WHERE playlist_id = ? AND source_collection_uuid = ? "
      "AND source_path = ?"));
  del.addBindValue(playlistId);
  del.addBindValue(sourceCollectionUuid);
  del.addBindValue(sourcePath);
  if (!del.exec() || del.numRowsAffected() <= 0) {
    m_db.rollback();
    return false;
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
    m_db.rollback();
    return false;
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
    m_db.rollback();
    return false;
  }

  QSqlQuery reinsert(m_db);
  reinsert.prepare(
      QStringLiteral("INSERT INTO playlist_items (playlist_id, position, source_collection_uuid, "
                     "source_path, added_at) VALUES (?, ?, ?, ?, ?)"));
  for (int i = 0; i < rows.size(); ++i) {
    reinsert.bindValue(0, playlistId);
    reinsert.bindValue(1, i);
    reinsert.bindValue(2, rows[i].uuid);
    reinsert.bindValue(3, rows[i].path);
    reinsert.bindValue(4, rows[i].addedAt);
    if (!reinsert.exec()) {
      m_db.rollback();
      return false;
    }
  }

  if (!m_db.commit()) {
    m_db.rollback();
    return false;
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
  QList<PlaylistRow> result;
  if (!m_db.isOpen()) {
    return result;
  }
  QSqlQuery q(m_db);
  if (!q.exec(QStringLiteral("SELECT id, name, icon, parent_collection_uuid, reserved_kind, "
                             "created_at, updated_at FROM playlists"))) {
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
    result.push_back(row);
  }
  return result;
}

QList<PlaylistItemRef> PlaylistManager::loadItems(const QString &playlistId) const {
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
