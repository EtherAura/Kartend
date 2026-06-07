#ifndef ARTWORKWIDGETREGISTRY_H
#define ARTWORKWIDGETREGISTRY_H

#include "artworkmanager.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class ItemWidget;

/// Per-widget bookkeeping for the artwork pipeline. Owns four cohesive maps
/// behind one internal mutex:
///   - loaded set (which widgets currently display loaded artwork),
///   - widget→path map (latest loaded path per widget — used for staleness
///     detection during pool recycling),
///   - pending queue (artwork requests not yet dispatched to a worker),
///   - per-item artwork-type overrides (shift+middle-click cycle state).
///
/// The registry is a QObject so it owns the QObject::destroyed connections
/// installed by track() — when a widget is deleted, those connections fire
/// and remove every entry referencing it. Parent the registry to its
/// ArtworkManager so destruction order is well-defined.
class ArtworkWidgetRegistry : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ArtworkWidgetRegistry)

public:
  explicit ArtworkWidgetRegistry(QObject *parent = nullptr);

  /// Connect a destroyed-cleanup handler to @p widget. Idempotent — guarded
  /// by a Qt property flag so repeated calls don't stack handlers.
  void track(ItemWidget *widget);

  /// Mark @p widget as currently displaying @p path. Adds to the loaded set
  /// (deduplicated) and updates the widget→path map.
  void markLoaded(ItemWidget *widget, const QString &path);

  [[nodiscard]] bool isLoaded(ItemWidget *widget) const;
  [[nodiscard]] bool hasArtworkFor(ItemWidget *widget) const;
  [[nodiscard]] QString pathFor(ItemWidget *widget) const;

  /// Append @p info to the pending queue. Kartend-b8qe.3 added backpressure:
  /// coalesces with any existing entry for the same (widget, artworkPath)
  /// pair to dedupe rapid-scroll re-queueing, and evicts the oldest entry
  /// (FIFO) once the queue would exceed kMaxPending — caps growth during
  /// long rapid scrolls past 5000+ items.
  void enqueuePending(const ArtworkInfo &info);
  [[nodiscard]] bool isPendingFor(ItemWidget *widget, const QString &path) const;
  [[nodiscard]] int pendingCount() const;

  /// Kartend-b8qe.3: upper bound on m_pending after coalesce/append. Chosen
  /// generously to cover an oversized viewport plus its prefetch window
  /// without false-positively dropping a load the user actually needs.
  /// FIFO-evicted on overflow inside enqueuePending — oldest entries are
  /// most likely already off-screen during a long scroll.
  static constexpr int kMaxPending = 300;

  /// Snapshot the pending queue and clear it atomically — caller may then
  /// partition / process / write back. setPending() restores whatever
  /// remains.
  [[nodiscard]] QList<ArtworkInfo> takePending();
  void setPending(QList<ArtworkInfo> list);

  /// Drop every entry referencing @p widget across loaded / path / pending.
  /// Used by destroyed handlers, pool recycling, and addPendingArtwork's
  /// stale-state clear.
  void removeAllEntriesFor(ItemWidget *widget);

  /// Clear loaded + path + pending. Leaves type overrides intact.
  void clearAll();

  /// Block signals on every currently-loaded widget, then clearAll().
  /// Atomic w.r.t. concurrent enqueues. Used during widget-tear-down where
  /// dangling signal emissions would target poolerd-recycled widgets.
  void blockSignalsAndClearAll();

  /// Drop only the loaded set. Path map + pending stay.
  void clearLoadedOnly();

  /// Drop loaded + pending. Path map stays — addPendingArtwork uses it to
  /// detect stale entries on the next request.
  void clearLoadedAndPending();

  /// Drop only the pending queue. Used when ceasing to schedule new work
  /// (e.g. silent-load stopping) but currently-displayed artwork should
  /// stay visible.
  void clearPendingOnly();

  [[nodiscard]] QString artworkTypeOverrideFor(const QString &fullPath) const;
  /// Set the override for @p fullPath, or remove it when @p type is empty
  /// (the legacy/primary artwork sentinel).
  void setArtworkTypeOverride(const QString &fullPath, const QString &type);
  void clearArtworkTypeOverrides();

private:
  mutable QMutex m_mutex;
  QList<QPointer<ItemWidget>> m_loaded;
  // O(1) membership mirror of m_loaded for isLoaded()/markLoaded(): the QList is
  // kept (its QPointers allow safe iteration in blockSignalsAndClearAll) and
  // updated in lockstep. Raw ItemWidget* keys like m_widgetToPath —
  // removeAllEntriesFor (destroyed handler / pool recycling) keeps both clean.
  QSet<ItemWidget *> m_loadedSet;
  QHash<ItemWidget *, QString> m_widgetToPath;
  QList<ArtworkInfo> m_pending;
  QHash<QString, QString> m_typeOverrides;
};

#endif // ARTWORKWIDGETREGISTRY_H
