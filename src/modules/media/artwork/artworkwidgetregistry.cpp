#include "artworkwidgetregistry.h"

#include "itemwidget.h"
#include "propertyutils.h"

#include <algorithm>
#include <QApplication>
#include <QMutexLocker>

ArtworkWidgetRegistry::ArtworkWidgetRegistry(QObject *parent) : QObject(parent) {}

void ArtworkWidgetRegistry::track(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  if (widget->property(PropertyKeys::TrackedByArtwork).toBool()) {
    return;
  }
  widget->setProperty(PropertyKeys::TrackedByArtwork, true);
  connect(widget, &QObject::destroyed, this, [this](QObject *obj) {
    if (QApplication::closingDown()) {
      return;
    }
    auto *widgetPtr = qobject_cast<ItemWidget *>(obj);
    if (!widgetPtr) {
      return;
    }
    removeAllEntriesFor(widgetPtr);
  });
}

void ArtworkWidgetRegistry::markLoaded(ItemWidget *widget, const QString &path) {
  if (!widget) {
    return;
  }
  QMutexLocker locker(&m_mutex);
  m_widgetToPath[widget] = path;
  if (!m_loadedSet.contains(widget)) {
    m_loaded.append(widget);
    m_loadedSet.insert(widget);
  }
}

bool ArtworkWidgetRegistry::isLoaded(ItemWidget *widget) const {
  if (!widget) {
    return false;
  }
  QMutexLocker locker(&m_mutex);
  return m_loadedSet.contains(widget);
}

bool ArtworkWidgetRegistry::hasArtworkFor(ItemWidget *widget) const {
  if (!widget) {
    return false;
  }
  QMutexLocker locker(&m_mutex);
  return m_widgetToPath.contains(widget);
}

QList<QPair<ItemWidget *, QString>> ArtworkWidgetRegistry::loadedEntries() const {
  QList<QPair<ItemWidget *, QString>> out;
  out.reserve(m_loaded.size());
  for (const QPointer<ItemWidget> &widget : m_loaded) {
    if (widget) {
      out.append({widget.data(), m_widgetToPath.value(widget.data())});
    }
  }
  return out;
}

QString ArtworkWidgetRegistry::pathFor(ItemWidget *widget) const {
  if (!widget) {
    return {};
  }
  QMutexLocker locker(&m_mutex);
  return m_widgetToPath.value(widget);
}

void ArtworkWidgetRegistry::enqueuePending(const ArtworkInfo &info) {
  QMutexLocker locker(&m_mutex);

  // Kartend-b8qe.3 coalesce: if an entry for the same (widget, artworkPath)
  // is already pending, skip the append — rapid scroll past the same item
  // would otherwise queue dozens of identical requests that all do the same
  // disk read on the worker pool.
  for (const auto &existing : m_pending) {
    if (existing.mediaItem == info.mediaItem && existing.artworkPath == info.artworkPath) {
      return;
    }
  }

  // Kartend-b8qe.3 cap: hard limit on queue depth so a long rapid scroll
  // doesn't pile up thousands of stale requests. Drop the oldest entry on
  // overflow — older entries are most likely already off-screen by the time
  // the queue fills, so FIFO eviction trades stale work for current work.
  if (m_pending.size() >= kMaxPending) {
    m_pending.removeFirst();
  }

  m_pending.append(info);
}

int ArtworkWidgetRegistry::pendingCount() const {
  QMutexLocker locker(&m_mutex);
  return m_pending.size();
}

bool ArtworkWidgetRegistry::isPendingFor(ItemWidget *widget, const QString &path) const {
  if (!widget) {
    return false;
  }
  QMutexLocker locker(&m_mutex);
  return std::ranges::any_of(m_pending, [&](const ArtworkInfo &info) {
    return info.mediaItem == widget && info.artworkPath == path;
  });
}

QList<ArtworkInfo> ArtworkWidgetRegistry::takePending() {
  QMutexLocker locker(&m_mutex);
  QList<ArtworkInfo> out;
  out.swap(m_pending);
  return out;
}

void ArtworkWidgetRegistry::setPending(QList<ArtworkInfo> list) {
  QMutexLocker locker(&m_mutex);
  m_pending = std::move(list);
}

void ArtworkWidgetRegistry::removeAllEntriesFor(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  QMutexLocker locker(&m_mutex);
  m_loaded.removeAll(widget);
  m_loadedSet.remove(widget);
  m_widgetToPath.remove(widget);
  for (int i = m_pending.size() - 1; i >= 0; --i) {
    if (m_pending[i].mediaItem == widget) {
      m_pending.removeAt(i);
    }
  }
}

void ArtworkWidgetRegistry::clearAll() {
  QMutexLocker locker(&m_mutex);
  m_loaded.clear();
  m_loadedSet.clear();
  m_widgetToPath.clear();
  m_pending.clear();
}

void ArtworkWidgetRegistry::blockSignalsAndClearAll() {
  // Snapshot the tracked widgets and clear our maps under the lock, then call
  // QObject::blockSignals() on the snapshot outside it — touching QObject
  // internals while holding m_mutex invites lock-ordering surprises
  // (Kartend-cmao). QList is copy-on-write so the snapshot is cheap, and
  // QPointer null-checks guard a widget destroyed in between.
  QList<QPointer<ItemWidget>> widgets;
  {
    QMutexLocker locker(&m_mutex);
    widgets = m_loaded;
    m_loaded.clear();
    m_loadedSet.clear();
    m_widgetToPath.clear();
    m_pending.clear();
  }
  for (const QPointer<ItemWidget> &widget : widgets) {
    if (widget) {
      widget->blockSignals(true);
    }
  }
}

void ArtworkWidgetRegistry::clearLoadedOnly() {
  QMutexLocker locker(&m_mutex);
  m_loaded.clear();
  m_loadedSet.clear();
}

void ArtworkWidgetRegistry::clearLoadedAndPending() {
  QMutexLocker locker(&m_mutex);
  m_loaded.clear();
  m_loadedSet.clear();
  m_pending.clear();
}

void ArtworkWidgetRegistry::clearPendingOnly() {
  QMutexLocker locker(&m_mutex);
  m_pending.clear();
}

QString ArtworkWidgetRegistry::artworkTypeOverrideFor(const QString &fullPath) const {
  QMutexLocker locker(&m_mutex);
  return m_typeOverrides.value(fullPath);
}

void ArtworkWidgetRegistry::setArtworkTypeOverride(const QString &fullPath, const QString &type) {
  QMutexLocker locker(&m_mutex);
  if (type.isEmpty()) {
    m_typeOverrides.remove(fullPath);
  } else {
    m_typeOverrides.insert(fullPath, type);
  }
}

void ArtworkWidgetRegistry::clearArtworkTypeOverrides() {
  QMutexLocker locker(&m_mutex);
  m_typeOverrides.clear();
}
