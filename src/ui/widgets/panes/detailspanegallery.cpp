// Sibling translation unit for DetailsPane: thin delegates that route the
// public gallery API (setArtworkGallery, setArtworkEditEnabled,
// applyGalleryThumbSize) through DetailsPaneGalleryView. The helper owns
// every gallery widget the previous in-line implementation kept on
// DetailsPane plus the click-to-preview overlay.

#include "detailspane.h"
#include "detailspanegalleryview.h"
#include "uiconstants.h"

#include <QPushButton>

void DetailsPane::setArtworkGallery(const QList<GalleryEntry> &entries) {
  if (m_galleryView) {
    m_galleryView->setEntries(entries, m_primaryArtworkPath, m_activeTab);
  }
  // Keep the dedicated horizontal view's gallery in sync.
  rebuildHorizontalGallery();
}

void DetailsPane::setArtworkEditEnabled(bool enabled) {
  if (m_galleryView) {
    m_galleryView->setEditEnabled(enabled, m_activeTab);
  }
  // Mirror the trailing horizontal-view edit button so both track the
  // per-item edit permission in lockstep.
  if (m_hEditButton) {
    m_hEditButton->setVisible(enabled);
  }
  rebuildHorizontalGallery();
}

void DetailsPane::applyGalleryThumbSize() {
  if (m_galleryView) {
    m_galleryView->applyThumbSize(currentGalleryThumbSize());
  }
}
