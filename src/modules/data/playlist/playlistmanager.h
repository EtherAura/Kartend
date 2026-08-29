#ifndef PLAYLISTMANAGER_H
#define PLAYLISTMANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QThread>

#include "errorutils.h"
#include "iplaylistmanager.h"
#include "smartfilter.h"

struct CollectionConfig;

// PlaylistRow / PlaylistItemRef now live in iplaylistmanager.h so the data
// contract travels with the interface.

/// Owns CRUD against the v10 `playlists` + `playlist_items` tables. All
/// operations run on the main thread against a dedicated SQLite connection
/// (the writes are infrequent and small enough that pushing them onto the
/// QueryManager worker buys nothing). MainWindow synthesises a
/// CollectionConfig per loadAll() row at startup so playlists nest into the
/// existing hierarchy / appear as tiles like ordinary subcollections;
/// QueryManager handles fetch via a separate code path keyed on
/// CollectionConfig::isPlaylist.
class PlaylistManager : public IPlaylistManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(PlaylistManager)
public:
  explicit PlaylistManager(QObject *parent = nullptr);
  ~PlaylistManager() override;

  /// Opens the dedicated main-thread connection on the same SQLite file the
  /// rest of the app uses. Idempotent — subsequent calls re-open if the
  /// previous connection was closed. Returns true when the connection is
  /// usable for reads/writes.
  bool initialize() override;

  // ─── Playlist CRUD ────────────────────────────────────────────────────────

  /// Inserts a new playlist row and returns its generated id. `name` is taken
  /// verbatim (caller is responsible for trimming / non-empty validation);
  /// `parentCollectionUuid` empty parks the playlist at the root level.
  /// `reservedKind` is normally empty — reserved for built-in playlists
  /// (follow-up).
  ErrorUtils::Result<QString> createPlaylist(const QString &name,
                                             const QString &parentCollectionUuid = QString(),
                                             const QString &reservedKind = QString()) override;

  /// Create a smart playlist whose items are evaluated from `filter` on
  /// each open. Distinct from `createPlaylist` so the call site is
  /// explicit at every callsite — adding items to a smart playlist via
  /// `addItem` is silently ignored by the QueryManager scope branch
  /// (smart playlists ignore `playlist_items` entirely), so we want the
  /// API to make the static-vs-smart choice obvious. Reserved smart
  /// playlists are not currently supported.
  ErrorUtils::Result<QString>
  createSmartPlaylist(const QString &name, const SmartFilter::FilterSet &filterSet,
                      const QString &parentCollectionUuid = QString()) override;

  /// Replace the filter on an existing smart playlist, stamp updated_at,
  /// and emit playlistsChanged so the synthesizer re-renders the tile and
  /// the QueryManager scope cache invalidates on next open.
  bool updateSmartFilter(const QString &id, const SmartFilter::FilterSet &filterSet) override;

  /// Load and parse the persisted SmartFilter for a playlist. Returns an
  /// error if the playlist is unknown, not a smart playlist, or if the
  /// stored JSON is malformed. UI consumers should bail rather than
  /// fabricating a default filter — the user expects to see what they
  /// configured.
  [[nodiscard]] ErrorUtils::Result<SmartFilter::FilterSet>
  loadSmartFilter(const QString &id) const override;

  /// Updates `name` and stamps `updated_at`. Returns false if the id is
  /// unknown or the write fails (errors logged).
  bool renamePlaylist(const QString &id, const QString &newName) override;

  /// Removes the playlist row and (via FK ON DELETE CASCADE) its items.
  /// Reserved playlists (reserved_kind != '') are refused — the favorites
  /// playlist and future built-ins must outlive the user's
  /// menu choices so we don't have to re-create them on next launch and lose
  /// every starred item along the way.
  bool deletePlaylist(const QString &id) override;

  // ─── Playlist item CRUD ───────────────────────────────────────────────────

  /// Appends a (sourceCollectionUuid, sourcePath) reference at the end of the
  /// playlist. Duplicate refs are rejected so a single right-click "add to
  /// playlist" is idempotent — the chooser dialog stays simple. Returns true
  /// on insert, false when the row was already present or the write failed.
  bool addItem(const QString &playlistId, const QString &sourceCollectionUuid,
               const QString &sourcePath) override;

  /// Removes every row matching (playlistId, sourceCollectionUuid, sourcePath)
  /// and re-densifies remaining positions so position is always 0..N-1. Returns
  /// true when at least one row was deleted.
  bool removeItem(const QString &playlistId, const QString &sourceCollectionUuid,
                  const QString &sourcePath) override;

  // ─── Loaders (used by the synthesizer + QueryManager) ─────────────────────

  /// Returns every row from `playlists`, suitable for synthesizing virtual
  /// CollectionConfigs at startup. Order is unspecified — callers that need a
  /// stable order should sort by name.
  [[nodiscard]] QList<PlaylistRow> loadAll() const override;

  /// Returns the items of a single playlist in position order. Empty for
  /// unknown playlist ids or DB failures (errors logged).
  [[nodiscard]] QList<PlaylistItemRef> loadItems(const QString &playlistId) const override;

  /// Convenience for the context menu: returns true when the (uuid, path) pair
  /// is already present in the playlist. Cheap because of
  /// idx_playlist_items_lookup.
  [[nodiscard]] bool containsItem(const QString &playlistId, const QString &sourceCollectionUuid,
                                  const QString &sourcePath) const override;

  // ─── Import / export ──────────────────────────────────────

  /// Writes a playlist as a JSON document at `outPath`. The format is the
  /// canonical Kartend serialization — round-trips losslessly through
  /// importFromJson(), preserving the (source_collection_uuid, source_path)
  /// pairs so the imported playlist resolves to the same items even when
  /// filesystem paths shift relative to the user's setup. Returns the count
  /// of items written on success.
  [[nodiscard]] ErrorUtils::Result<int> exportToJson(const QString &playlistId,
                                                     const QString &outPath) const override;

  /// Writes a playlist as a basic M3U (the path-per-line dialect, with an
  /// `#EXTM3U` header and `#EXTINF:0,<basename>` titles). Lossy compared to
  /// JSON — collection_uuid and added_at metadata are dropped — but the file
  /// is interchangeable with media players. Returns the count of items
  /// written on success.
  [[nodiscard]] ErrorUtils::Result<int> exportToM3U(const QString &playlistId,
                                                    const QString &outPath) const override;

  /// Reads a JSON document produced by exportToJson() and creates a new
  /// playlist with its contents. Returns the new playlist id; the supplied
  /// `nameOverride` (when non-empty) wins over the name embedded in the file
  /// so users can disambiguate when re-importing. `outSkipped` (when non-null)
  /// receives the count of dropped items (empty-path, duplicate, or
  /// over-long/invalid refs) so callers can surface "N items skipped" the way
  /// importFromM3U does (Kartend-2sg2t).
  [[nodiscard]] ErrorUtils::Result<QString> importFromJson(const QString &inPath,
                                                           const QString &nameOverride = QString(),
                                                           int *outSkipped = nullptr) override;

  /// Reads an M3U file and creates a new playlist from its entries. Each path
  /// is resolved against the live `items` table; entries that don't currently
  /// match any indexed item are skipped (the count of skipped lines is
  /// available via `outSkipped` for caller-facing diagnostics). `outAmbiguous`
  /// (when non-null) receives the count of paths that resolved to more than one
  /// collection — the lowest-uuid pick may have bound them to the wrong source
  /// collection, so the caller can warn the user (Kartend-93ju5). Returns the
  /// new playlist id.
  [[nodiscard]] ErrorUtils::Result<QString> importFromM3U(const QString &inPath,
                                                          const QString &playlistName,
                                                          int *outSkipped = nullptr,
                                                          int *outAmbiguous = nullptr) override;

  // ─── Favorites built-in ────────────────────────────────────

  /// Returns the id of the unique playlist with reserved_kind='favorites',
  /// creating it (with the default `defaultName`) if it does not yet exist.
  /// Idempotent — always returns the same id within a process lifetime once a
  /// row exists. Empty string on database error (errors logged).
  QString
  ensureFavoritesPlaylist(const QString &defaultName = QStringLiteral("Favorites")) override;

  /// Cached id of the favorites playlist (populated lazily by
  /// ensureFavoritesPlaylist or any prior loadAll() that surfaced a row with
  /// reserved_kind='favorites'). Empty when no favorites row has been seen
  /// yet — call ensureFavoritesPlaylist() first if you need a guarantee.
  [[nodiscard]] QString favoritesPlaylistId() const override { return m_favoritesId; }

private:
  // Thread-affinity guard. PlaylistManager owns a QSqlDatabase that Qt's SQL
  // drivers explicitly forbid sharing across threads. Every public method
  // (load*, *Playlist, *Item, import/export, ensureFavoritesPlaylist) must
  // be invoked on the main thread that owns this object. Compiles out in
  // release; fail-fast in debug. Mirrors QueryManager::assertOwnerThread()
  // and InteractionStateHolder::assertOwnerThread().
  void assertOwnerThread() const {
    Q_ASSERT_X(thread() == QThread::currentThread(), "PlaylistManager",
               "PlaylistManager method invoked from a non-owner thread; the "
               "playlist QSqlDatabase is dedicated to the main thread — push "
               "the call back via a queued connection or signal.");
  }

  QSqlDatabase m_db;
  QString m_connectionName;
  QString m_favoritesId; // Cached id of the reserved favorites playlist.
};

#endif // PLAYLISTMANAGER_H
