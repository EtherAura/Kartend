#ifndef IPLAYLISTMANAGER_H
#define IPLAYLISTMANAGER_H

#include <QList>
#include <QObject>
#include <QString>

#include "errorutils.h"
#include "smartfilter.h"

/// Stores everything the synthesizer needs to materialize a playlist as a
/// virtual CollectionConfig in MainWindow::m_collections.
/// `parentCollectionUuid` empty means the playlist sits at the root level;
/// otherwise it nests under whichever real collection has that uuid.
struct PlaylistRow {
  QString id;
  QString name;
  QString icon;
  QString parentCollectionUuid;
  QString reservedKind; // '' for user playlists; reserved for built-ins (e.g. favorites).
  QString createdAt;
  QString updatedAt;
  /// True when the playlist is filter-driven; items rebuild on each open
  /// from `smartFilterJson`. False is the original static-playlist
  /// behaviour. Persisted in playlists.is_smart (v11).
  bool isSmart = false;
  /// SmartFilter JSON payload when isSmart is true; empty string
  /// otherwise. Persisted in playlists.smart_filter (v11). Callers that
  /// want a typed Filter should call IPlaylistManager::loadSmartFilter
  /// instead of parsing this directly.
  QString smartFilterJson;
};

/// One entry in a playlist, in playlist position order. The reference is
/// (sourceCollectionUuid, sourcePath) — the same key used by item_metadata
/// and item_artwork — so a playlist survives item id renumbering across
/// rescans and lets a single playlist mix media from any source collection.
struct PlaylistItemRef {
  int position = -1;
  QString sourceCollectionUuid;
  QString sourcePath;
  QString addedAt;
};

/**
 * @brief Abstract interface to the playlist store.
 *
 * Exposes the full PlaylistManager API plus its `playlistsChanged` signal
 * so callers wired through ApplicationContext can be backed by either the
 * real (SQLite-backed) implementation or a test double.
 *
 * A QObject interface (like IDatabaseManager) because the contract
 * includes a signal — the row/item struct types live here so the data
 * contract travels with the interface.
 */
class IPlaylistManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(IPlaylistManager)
public:
  using QObject::QObject;
  ~IPlaylistManager() override = default;

  virtual bool initialize() = 0;

  // ─── Playlist CRUD ────────────────────────────────────────────────────────
  virtual ErrorUtils::Result<QString>
  createPlaylist(const QString &name, const QString &parentCollectionUuid = QString(),
                 const QString &reservedKind = QString()) = 0;
  virtual ErrorUtils::Result<QString>
  createSmartPlaylist(const QString &name, const SmartFilter::Filter &filter,
                      const QString &parentCollectionUuid = QString()) = 0;
  virtual bool updateSmartFilter(const QString &id, const SmartFilter::Filter &filter) = 0;
  [[nodiscard]] virtual ErrorUtils::Result<SmartFilter::Filter>
  loadSmartFilter(const QString &id) const = 0;
  virtual bool renamePlaylist(const QString &id, const QString &newName) = 0;
  virtual bool deletePlaylist(const QString &id) = 0;

  // ─── Playlist item CRUD ───────────────────────────────────────────────────
  virtual bool addItem(const QString &playlistId, const QString &sourceCollectionUuid,
                       const QString &sourcePath) = 0;
  virtual bool removeItem(const QString &playlistId, const QString &sourceCollectionUuid,
                          const QString &sourcePath) = 0;

  // ─── Loaders ──────────────────────────────────────────────────────────────
  [[nodiscard]] virtual QList<PlaylistRow> loadAll() const = 0;
  [[nodiscard]] virtual QList<PlaylistItemRef> loadItems(const QString &playlistId) const = 0;
  [[nodiscard]] virtual bool containsItem(const QString &playlistId,
                                          const QString &sourceCollectionUuid,
                                          const QString &sourcePath) const = 0;

  // ─── Import / export ──────────────────────────────────────────────────────
  [[nodiscard]] virtual ErrorUtils::Result<int> exportToJson(const QString &playlistId,
                                                             const QString &outPath) const = 0;
  [[nodiscard]] virtual ErrorUtils::Result<int> exportToM3U(const QString &playlistId,
                                                            const QString &outPath) const = 0;
  [[nodiscard]] virtual ErrorUtils::Result<QString>
  importFromJson(const QString &inPath, const QString &nameOverride = QString(),
                 int *outSkipped = nullptr) = 0;
  [[nodiscard]] virtual ErrorUtils::Result<QString> importFromM3U(const QString &inPath,
                                                                  const QString &playlistName,
                                                                  int *outSkipped = nullptr,
                                                                  int *outAmbiguous = nullptr) = 0;

  // ─── Favorites built-in ───────────────────────────────────────────────────
  virtual QString
  ensureFavoritesPlaylist(const QString &defaultName = QStringLiteral("Favorites")) = 0;
  [[nodiscard]] virtual QString favoritesPlaylistId() const = 0;

signals:
  /// Emitted after any successful create/rename/delete/add/remove so
  /// MainWindow can re-synthesize the playlist CollectionConfigs and
  /// rebuild the hierarchy cache. Carries no payload — listeners read the
  /// latest state via loadAll().
  void playlistsChanged();
};

#endif // IPLAYLISTMANAGER_H
