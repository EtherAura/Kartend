// Sibling translation unit for DetailsPane: built-in tab-bar management.
// Owns the tab bar's construction (setupTabBar), the manager-driven switch
// (setActiveTab), and the per-tab section dispatch (applyTabVisibility plus
// the setArtworkSectionVisible / setFileInfoRowsVisible toggles it drives).
// Mechanical move out of detailspane.cpp — no state changed hands; every
// member referenced here still lives on DetailsPane.

#include "detailspane.h"
#include "detailspanegalleryview.h"
#include "ui_detailspane.h"
#include "videopreviewwidget.h"

#include <QSignalBlocker>
#include <QTabBar>
#include <QVBoxLayout>

void DetailsPane::setupTabBar() {
  // The .ui file's mainLayout is the QVBoxLayout that holds scrollArea.
  // Find it, create the tab bar, and insert at index 0.
  auto *mainLayout = qobject_cast<QVBoxLayout *>(layout());
  if (!mainLayout) {
    return;
  }
  m_tabBar = new QTabBar(this);
  m_tabBar->setExpanding(true);
  m_tabBar->setDocumentMode(true);
  m_tabBar->addTab(tr("Item"));
  m_tabBar->addTab(tr("Collection"));
  m_tabBar->addTab(tr("File"));
  // opaque tab bar so the sidebar pattern doesn't bleed through
  // the gaps above/below the tabs. Without this, the patternEvent's full-
  // sidebar fill leaks into the tab strip's transparent regions.
  m_tabBar->setAutoFillBackground(true);
  mainLayout->insertWidget(0, m_tabBar);

  connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
    DetailsPaneTab newTab = DetailsPaneTab::Item;
    if (index == static_cast<int>(DetailsPaneTab::Collection))
      newTab = DetailsPaneTab::Collection;
    else if (index == static_cast<int>(DetailsPaneTab::File))
      newTab = DetailsPaneTab::File;
    if (newTab == m_activeTab) {
      return;
    }
    m_activeTab = newTab;
    applyTabVisibility();
    emit activeTabChanged(newTab);
  });
}

void DetailsPane::setActiveTab(DetailsPaneTab tab) {
  if (m_activeTab == tab && m_tabBar && m_tabBar->currentIndex() == static_cast<int>(tab)) {
    return;
  }
  m_activeTab = tab;
  if (m_tabBar) {
    QSignalBlocker blocker(m_tabBar);
    m_tabBar->setCurrentIndex(static_cast<int>(tab));
  }
  applyTabVisibility();
}

void DetailsPane::applyTabVisibility() {
  // Title row + name row are part of every tab — only the labels' text
  // and the supporting sections (artwork, file-info, gallery, details)
  // change. Set them visible up front so individual cases only need to
  // toggle the parts that differ.
  ui->titleLabel->setVisible(true);
  ui->itemNameValue->setVisible(true);

  switch (m_activeTab) {
  case DetailsPaneTab::Item: {
    // "What is this?" — artwork preview, video preview, gallery,
    // extended metadata + usage stats. No filesystem rows.
    ui->titleLabel->setText(tr("Item Information"));
    if (ui->editMetadataButton) ui->editMetadataButton->setVisible(m_hasItemDisplayed);
    setArtworkSectionVisible(true);
    setFileInfoRowsVisible(false);
    // Hide the gallery + details containers up front; they may still
    // hold data from a prior Collection-tab render (m_detailsContainer
    // is shared with renderCollectionSummary). The per-item setters
    // (setArtworkGallery / setExtendedMetadata / setUsageStats /
    // setManualFile) will repopulate and re-show on the manager's
    // tab-change re-push, so this avoids a flash of stale rows.
    if (m_galleryView) m_galleryView->hideSection();
    if (m_detailsContainer) m_detailsContainer->hide();
    // Prefer the canonical metadata title when one is known; fall back to
    // the raw filename-derived itemName.
    const QString name =
        m_currentMetadataTitle.isEmpty() ? m_currentItemName : m_currentMetadataTitle;
    if (!m_hasItemDisplayed) {
      ui->itemNameValue->setText(tr("No item selected"));
    } else {
      ui->itemNameValue->setText(name.isEmpty() ? tr("No item selected") : name);
    }
    break;
  }
  case DetailsPaneTab::Collection:
    // Collection summary — independent of selection. renderCollectionSummary
    // toggles its own section visibility (hides artwork, file info, gallery)
    // and populates the Details container with summary rows.
    if (ui->editMetadataButton) ui->editMetadataButton->setVisible(false);
    renderCollectionSummary();
    break;
  case DetailsPaneTab::File:
    // Pure filesystem view — name + path/size/modified/extension.
    // No artwork, no video, no gallery, no extended metadata.
    ui->titleLabel->setText(tr("File Information"));
    if (ui->editMetadataButton) ui->editMetadataButton->setVisible(false);
    ui->itemNameValue->setText(m_currentItemName.isEmpty() ? tr("No item selected")
                                                           : m_currentItemName);
    setArtworkSectionVisible(false);
    setFileInfoRowsVisible(true);
    // Re-apply the cached async stat result: if the worker resolved while this
    // tab was hidden, the size/modified labels would otherwise be stuck on the
    // '…' placeholder until the next selection (Kartend-kujy5).
    applyFileStatDisplay();
    if (m_galleryView) m_galleryView->hideSection();
    if (m_detailsContainer) m_detailsContainer->hide();
    break;
  }
  // tab change can re-title labels (Name → Collection) and
  // toggle item visibility — reflect that in the horizontal view.
  updateHorizontalView();
}

void DetailsPane::setArtworkSectionVisible(bool visible) {
  // Artwork preview tile + (when hiding) the live video widget.
  // The "Artwork" header label was removed from the .ui to compact the
  // Item tab — visibility now only toggles the tile and the live video
  // widget. Artwork and file-info no longer travel together — each tab
  // decides independently what to show.
  // Keep the static artwork tile hidden while a preview video is
  // currently playing — otherwise QVBoxLayout would stack both
  // widgets vertically (artwork above video) and the live preview
  // ends up below the scroll fold. The video occupies the artwork
  // slot for as long as it has a loaded source. setMetadata /
  // applyTabVisibility get called multiple times per selection
  // (manager refreshes, post-scrape updates), and we hit this code
  // path on each one — without the video-aware branch the artwork
  // re-appears over the video on every refresh.
  const bool videoPlaying = m_videoPlayback.videoPreview &&
                            m_videoPlayback.videoPreview->isVisible() &&
                            !m_videoPlayback.videoPreview->currentVideoPath().isEmpty();
  ui->artworkDisplay->setVisible(visible && !videoPlaying);
  if (m_videoPlayback.videoPreview && !visible) {
    m_videoPlayback.videoPreview->hide();
  }
}

void DetailsPane::setFileInfoRowsVisible(bool visible) {
  ui->fileInfoTitle->setVisible(visible);
  ui->filePathLabel->setVisible(visible);
  ui->filePathValue->setVisible(visible);
  ui->fileSizeLabel->setVisible(visible);
  ui->fileSizeValue->setVisible(visible);
  ui->lastModifiedLabel->setVisible(visible);
  ui->lastModifiedValue->setVisible(visible);
  ui->fileExtensionLabel->setVisible(visible);
  ui->fileExtensionValue->setVisible(visible);
  // Static-UI separators travel with the file-info section. On Item
  // tab they would otherwise paint as orphaned hairlines between the
  // gallery and the description (separator2) or above the artwork
  // tile (separator1) — both unnecessary now that bubble backdrops
  // delineate sections.
  if (ui->separator1) ui->separator1->setVisible(visible);
  if (ui->separator2) ui->separator2->setVisible(visible);
}
