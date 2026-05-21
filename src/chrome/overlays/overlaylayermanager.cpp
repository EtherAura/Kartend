#include "overlaylayermanager.h"

#include <algorithm>

#include <QWidget>

OverlayLayerManager::OverlayLayerManager(QObject *parent) : QObject(parent) {}

OverlayLayerManager::~OverlayLayerManager() = default;

void OverlayLayerManager::purgeDestroyed() {
  m_entries.removeIf([](const Entry &e) { return e.widget.isNull(); });
}

void OverlayLayerManager::registerOverlay(QWidget *overlay, Layer layer) {
  if (!overlay) {
    return;
  }
  purgeDestroyed();
  // Idempotent: update the layer assignment in place if already present.
  for (Entry &e : m_entries) {
    if (e.widget.data() == overlay) {
      e.layer = layer;
      return;
    }
  }
  m_entries.append(Entry{overlay, layer});
}

void OverlayLayerManager::unregisterOverlay(QWidget *overlay) {
  if (!overlay) {
    return;
  }
  m_entries.removeIf([overlay](const Entry &e) { return e.widget.data() == overlay; });
}

void OverlayLayerManager::bringToFront(QWidget *overlay) {
  if (!overlay) {
    return;
  }
  purgeDestroyed();
  // Unregistered overlay: preserve legacy behaviour by raising directly.
  // Lets construction-time edge cases (an overlay's show*() running before
  // MainWindow's registration step) keep working without crashing.
  auto it = std::find_if(m_entries.begin(), m_entries.end(),
                         [overlay](const Entry &e) { return e.widget.data() == overlay; });
  if (it == m_entries.end()) {
    overlay->raise();
    return;
  }
  // Move the requested overlay to the back of its layer's bracket so a
  // sibling within the same layer that was raised later doesn't shadow it
  // on the next restack pass.
  Entry hot = *it;
  m_entries.erase(it);
  // Find the insertion point: the first entry whose layer strictly
  // exceeds the requested layer. Inserting just before that puts the hot
  // entry at the end of its layer's bracket.
  auto insertAt = std::find_if(m_entries.begin(), m_entries.end(),
                               [hot](const Entry &e) { return e.layer > hot.layer; });
  m_entries.insert(insertAt, hot);
  restack();
}

void OverlayLayerManager::restack() {
  purgeDestroyed();
  // Iterate in stored order (already z-order ascending because the entries
  // are kept sorted by Layer with within-layer last-bringToFront-wins
  // insertion). raise() makes each one top-most as it runs, so the final
  // entry ends up on top after the loop.
  for (const Entry &e : std::as_const(m_entries)) {
    if (QWidget *w = e.widget.data()) {
      w->raise();
    }
  }
}
