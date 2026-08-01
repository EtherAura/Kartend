#include "overlayzorderregistry.h"

#include <algorithm>

#include <QWidget>

OverlayZOrderRegistry::OverlayZOrderRegistry(QObject *parent) : QObject(parent) {}

OverlayZOrderRegistry::~OverlayZOrderRegistry() = default;

void OverlayZOrderRegistry::purgeDestroyed() {
  m_entries.removeIf([](const Entry &e) { return e.widget.isNull(); });
}

void OverlayZOrderRegistry::registerOverlay(QWidget *overlay, Layer layer) {
  if (!overlay) {
    return;
  }
  purgeDestroyed();
  // Idempotent: drop any existing entry first so a re-registration moves the
  // overlay into its new layer's bracket instead of mutating a stale slot.
  m_entries.removeIf([overlay](const Entry &e) { return e.widget.data() == overlay; });
  // m_entries must stay sorted ascending by layer — restack() iterates in
  // stored order and bringToFront()'s insertion assumes it. Insert before the
  // first entry whose layer strictly exceeds the new one, i.e. at the end of
  // the new layer's bracket (mirrors bringToFront's insertion point).
  auto insertAt = std::find_if(m_entries.begin(), m_entries.end(),
                               [layer](const Entry &e) { return e.layer > layer; });
  m_entries.insert(insertAt, Entry{overlay, layer});
}

void OverlayZOrderRegistry::unregisterOverlay(QWidget *overlay) {
  if (!overlay) {
    return;
  }
  m_entries.removeIf([overlay](const Entry &e) { return e.widget.data() == overlay; });
}

void OverlayZOrderRegistry::bringToFront(QWidget *overlay) {
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

void OverlayZOrderRegistry::restack() {
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
