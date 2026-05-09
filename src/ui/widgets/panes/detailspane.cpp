// Displays file metadata, artwork preview, and item details in the sidebar
// panel.
#include <algorithm>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "artworkpreviewoverlay.h"
#include "detailspane.h"
#include "detailspaneresizegrip.h"
#include "extensionutils.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"
#include "videoutils.h"

#include <QPointer>
#include <QPolygon>

// Creates metadata sidebar with scrollable layout for displaying item
// information and artwork
DetailsPane::DetailsPane(QWidget *parent) : QWidget(parent), ui(new Ui::DetailsPane) {
  ui->setupUi(this);
  setAutoFillBackground(true);
  // bug #6: stop mouse events on the sidebar from propagating up
  // to ancestors. The previous Overlay-mode implementation relied on Qt's
  // default hit-testing alone, but the sidebar sits as a sibling of the grid
  // — a moveEvent that crossed from the grid into the sidebar could still
  // race the grid's hover-select tracking. WA_NoMousePropagation makes the
  // sidebar a solid stop for unhandled mouse events.
  setAttribute(Qt::WA_NoMousePropagation);

  QPalette pal = palette();
  pal.setColor(QPalette::Window, pal.color(QPalette::Window));
  setPalette(pal);

  ui->scrollArea->setAutoFillBackground(true);
  QPalette scrollPalette = ui->scrollArea->palette();
  scrollPalette.setColor(QPalette::Window, palette().color(QPalette::Window));
  ui->scrollArea->setPalette(scrollPalette);

  ui->contentWidget->setAutoFillBackground(true);
  QPalette contentPalette = ui->contentWidget->palette();
  contentPalette.setColor(QPalette::Window, palette().color(QPalette::Window));
  ui->contentWidget->setPalette(contentPalette);

  setFixedWidth(UIConstants::DetailsPane::FIXED_WIDTH);

  // bug #2: hide the inner scrollbar entirely. With the sidebar
  // sized to the viewport, the content layout almost always fits — and when
  // it doesn't, the user can still mouse-wheel to scroll. A native bar
  // competing with the main grid's scrollbar in non-maximized windows was
  // visually noisy, which is the actual user complaint.
  ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Insert a preview video widget into the artwork pane, sized to match the
  // artwork display. Hidden by default; shown only when a preview video is
  // found for the current selection.
  m_videoPreview = new VideoPreviewWidget(this);
  m_videoPreview->setFixedSize(UIConstants::Metadata::ARTWORK_SIZE,
                               UIConstants::Metadata::ARTWORK_SIZE);
  m_videoPreview->hide();
  if (auto *artworkParentLayout =
          qobject_cast<QVBoxLayout *>(ui->artworkDisplay->parentWidget()->layout())) {
    int idx = artworkParentLayout->indexOf(ui->artworkDisplay);
    if (idx >= 0) {
      artworkParentLayout->insertWidget(idx + 1, m_videoPreview);
    } else {
      artworkParentLayout->addWidget(m_videoPreview);
    }
  }

  setupTabBar();

  // The grip controller owns every piece of state the previous in-line
  // implementation kept on DetailsPane (drag flags + start positions).
  // Lock state and dock position are kept in sync by applyAppearance.
  m_resizeGrip = new DetailsPaneResizeGrip(this, this);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::widthDragged, this, &DetailsPane::widthDragged);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::widthCommitted, this, &DetailsPane::widthCommitted);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::heightDragged, this, &DetailsPane::heightDragged);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::heightCommitted, this,
          &DetailsPane::heightCommitted);

  // center the artwork-section header, artwork preview, video
  // preview, and item-name label/value. Everything else stays flush-left
  // so long values (paths, sizes) read naturally. itemNameValue also needs
  // its *text* alignment centered + wordWrap so a long name wraps inside
  // the column instead of being cut off at the right edge.
  ui->itemNameValue->setAlignment(Qt::AlignHCenter);
  ui->itemNameValue->setWordWrap(true);
  applyContentAlignment();
  applyPreviewSize();

  // Debounce timer: avoid loading a video for every transient selection
  // change while the user is scrolling. Single-shot, restarted on each new
  // selection that has a video.
  m_videoStartTimer = new QTimer(this);
  m_videoStartTimer->setSingleShot(true);
  m_videoStartTimer->setInterval(UIConstants::DetailsPane::VIDEO_PREVIEW_DEBOUNCE_MS);
  connect(m_videoStartTimer, &QTimer::timeout, this, [this]() {
    if (m_pendingVideoPath.isEmpty() || !m_videoPreview) {
      return;
    }
    // Video preview is item-only chrome — File / Collection tabs hide
    // the artwork section entirely. Skip start-up there so we don't
    // burn QMediaPlayer resources on tabs that never render the widget.
    if (m_activeTab != DetailsPaneTab::Item) {
      return;
    }
    // In vertical dock the video replaces the artwork (cramped narrow panel
    // can't host both stacked). In horizontal dock the video and artwork
    // sit side-by-side inside m_hPreviewLayout — both stay visible based
    // on availability, handled by updateHorizontalView().
    const bool horizontal = CollectionUtils::isDetailsPaneHorizontal(m_position);
    if (!horizontal) {
      ui->artworkDisplay->hide();
    }
    m_videoPreview->show();
    m_videoPreview->playVideo(m_pendingVideoPath);
    if (horizontal) {
      updateHorizontalView();
    }
  });

  clearMetadata();
}

DetailsPane::~DetailsPane() {
  delete ui;
}

// Sets metadata fields and loads centered artwork from the configured
// artwork directory or a sibling "artwork" directory if present
void DetailsPane::setMetadata(const QString &filePath, const QString &itemName,
                              const QString &artworkDirectory, const QString &videoDirectory) {
  if (filePath.isEmpty()) {
    clearMetadata();
    return;
  }

  m_hasItemDisplayed = true;
  m_currentItemName = itemName;
  // setExtendedMetadata refills this when the metadata is applied; reset
  // here so a stale title from a previous selection doesn't persist on the
  // Item tab if the new item has no canonical title.
  m_currentMetadataTitle.clear();
  ui->itemNameValue->setText(itemName);
  // Always populate file info, regardless of which tab is active — the
  // File tab needs it, and the Item-tab paint path won't show it. Doing
  // it unconditionally means a tab switch surfaces the correct data
  // without re-running the manager's selection pipeline.
  updateFileInfo(filePath);

  QFileInfo fileInfo(filePath);
  const QString baseName = fileInfo.completeBaseName();

  // Drop the previous selection's cached artwork so applyPreviewSize falls
  // back to the empty placeholder while the new image is resolved.
  m_artworkSource = QPixmap();
  m_primaryArtworkPath.clear();
  applyPreviewSize();

  // Try collection's artwork directory first if provided
  if (!artworkDirectory.isEmpty()) {
    loadArtwork(baseName, artworkDirectory);
  } else {
    // Fallback to sibling "artwork" directory
    const QDir fileDir = fileInfo.dir();
    const QString siblingArtworkDir = fileDir.absolutePath() + "/artwork";
    loadArtwork(baseName, siblingArtworkDir);
  }

  // Resolve and (debounced) start preview video. Always reset the artwork
  // pane back to the artwork display first; the timer will swap to the video
  // widget once the debounce elapses if a video was found.
  showArtworkOnly();
  const QString videoPath =
      videoDirectory.isEmpty() ? QString() : VideoUtils::findVideoForFile(filePath, videoDirectory);
  schedulePreviewVideo(videoPath);

  // Defer all section visibility to applyTabVisibility() so each tab
  // ends up with its own distinct widget set.
  applyTabVisibility();
}

// Clears per-item state and re-asserts visibility. Each tab now owns its
// own no-selection display (Item: "No item selected" placeholder, File:
// "-" placeholders, Collection: summary regardless of selection), so the
// dispatch happens in applyTabVisibility() rather than here.
void DetailsPane::clearMetadata() {
  m_hasItemDisplayed = false;
  m_currentItemName.clear();
  m_currentMetadataTitle.clear();

  // Tear down item-only chrome (artwork preview, video, gallery, details
  // rows, manual button) regardless of which mode we land in.
  schedulePreviewVideo(QString());
  showArtworkOnly();
  setManualFile(QString());
  if (m_detailsContainer) {
    clearDetailsSection();
    m_detailsContainer->hide();
  }
  setArtworkEditEnabled(false);
  setArtworkGallery({});

  // Reset textual placeholders the Item and File tabs use when no item
  // is selected. The Collection tab ignores these and re-renders its
  // summary inside applyTabVisibility().
  ui->itemNameValue->setText(tr("No item selected"));
  m_currentFilePath.clear();
  ui->filePathValue->setText("-");
  ui->filePathValue->setToolTip(QString());
  ui->fileSizeValue->setText("-");
  ui->lastModifiedValue->setText("-");
  ui->fileExtensionValue->setText("-");
  m_artworkSource = QPixmap();
  m_primaryArtworkPath.clear();
  applyPreviewSize();

  applyTabVisibility();
}

void DetailsPane::setCollectionSummary(const CollectionSummary &summary) {
  m_collectionSummary = summary;
  // Re-render right away when the user is currently viewing the
  // Collection tab (so live edits in settings or fresh scan results land
  // immediately). On Item/File tabs the cache is updated silently and
  // applies the next time the user switches to Collection.
  if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  }
}

void DetailsPane::setArtworkSectionVisible(bool visible) {
  // Artwork header + preview tile + (when hiding) the live video widget.
  // Artwork and file-info no longer travel together — each tab decides
  // independently what to show.
  ui->artworkLabel->setVisible(visible);
  ui->artworkDisplay->setVisible(visible);
  if (m_videoPreview && !visible) {
    m_videoPreview->hide();
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
}

void DetailsPane::renderCollectionSummary() {
  setArtworkSectionVisible(false);
  setFileInfoRowsVisible(false);
  if (m_galleryContainer) {
    m_galleryContainer->hide();
  }
  ui->titleLabel->setText(tr("Collection Information"));
  ui->itemNameLabel->setText(tr("Collection:"));
  ui->itemNameValue->setText(m_collectionSummary.name);

  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  clearDetailsSection();

  if (!m_collectionSummary.type.trimmed().isEmpty()) {
    appendDetailRow(tr("Type"), m_collectionSummary.type);
  }
  if (m_collectionSummary.itemCount >= 0) {
    appendDetailRow(tr("Items"), QString::number(m_collectionSummary.itemCount));
  }
  appendDetailRow(tr("Last scanned"), formatLastScanned(m_collectionSummary.lastScanned));
  if (!m_collectionSummary.parentName.trimmed().isEmpty()) {
    appendDetailRow(tr("Parent"), m_collectionSummary.parentName);
  }
  appendDetailRow(tr("Media"), m_collectionSummary.mediaDirectory, /*wrap=*/true);
  appendDetailRow(tr("Artwork"), m_collectionSummary.artworkDirectory, /*wrap=*/true);
  appendDetailRow(tr("Video"), m_collectionSummary.videoDirectory, /*wrap=*/true);
  appendDetailRow(tr("Manuals"), m_collectionSummary.manualDirectory, /*wrap=*/true);
  if (!m_collectionSummary.extensions.isEmpty()) {
    appendDetailRow(tr("Extensions"), m_collectionSummary.extensions.join(QStringLiteral(", ")),
                    /*wrap=*/true);
  }

  // pull the just-built summary rows under the active sidebar-
  // font override so the no-selection view doesn't render in a different font
  // than the per-item view.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);

  m_detailsContainer->show();
}

QString DetailsPane::formatLastScanned(const QDateTime &lastScanned) {
  if (!lastScanned.isValid()) {
    return tr("never");
  }
  return lastScanned.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

// Updates file information fields including size, modification date, and file
// type
void DetailsPane::updateFileInfo(const QString &filePath) {
  QFileInfo fileInfo(filePath);

  if (!fileInfo.exists()) {
    ui->filePathValue->setText("File not found");
    ui->fileSizeValue->setText("-");
    ui->lastModifiedValue->setText("-");
    ui->fileExtensionValue->setText("-");
    return;
  }

  // wrap the full path across multiple lines instead of
  // eliding it. wordWrap on a path-like string with no spaces falls back
  // to per-character wrapping at the cell width, so the user sees the
  // entire path even on a narrow sidebar. The tooltip is kept for parity
  // with the previous elide-based UI.
  m_currentFilePath = filePath;
  ui->filePathValue->setWordWrap(true);
  updateFilePathDisplay();
  ui->filePathValue->setToolTip(filePath);

  ui->fileSizeValue->setText(formatFileSize(fileInfo.size()));
  ui->lastModifiedValue->setText(fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));

  QString extension = fileInfo.suffix().toUpper();
  if (extension.isEmpty()) {
    extension = "Unknown";
  }
  ui->fileExtensionValue->setText(extension + " file");
}

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
  ui->itemNameLabel->setVisible(true);
  ui->itemNameValue->setVisible(true);

  switch (m_activeTab) {
  case DetailsPaneTab::Item: {
    // "What is this?" — artwork preview, video preview, gallery,
    // extended metadata + usage stats. No filesystem rows.
    ui->titleLabel->setText(tr("Item Information"));
    ui->itemNameLabel->setText(tr("Name:"));
    setArtworkSectionVisible(true);
    setFileInfoRowsVisible(false);
    // Hide the gallery + details containers up front; they may still
    // hold data from a prior Collection-tab render (m_detailsContainer
    // is shared with renderCollectionSummary). The per-item setters
    // (setArtworkGallery / setExtendedMetadata / setUsageStats /
    // setManualFile) will repopulate and re-show on the manager's
    // tab-change re-push, so this avoids a flash of stale rows.
    if (m_galleryContainer) m_galleryContainer->hide();
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
    renderCollectionSummary();
    break;
  case DetailsPaneTab::File:
    // Pure filesystem view — name + path/size/modified/extension.
    // No artwork, no video, no gallery, no extended metadata.
    ui->titleLabel->setText(tr("File Information"));
    ui->itemNameLabel->setText(tr("Name:"));
    ui->itemNameValue->setText(m_currentItemName.isEmpty() ? tr("No item selected")
                                                           : m_currentItemName);
    setArtworkSectionVisible(false);
    setFileInfoRowsVisible(true);
    if (m_galleryContainer) m_galleryContainer->hide();
    if (m_detailsContainer) m_detailsContainer->hide();
    break;
  }
  // tab change can re-title labels (Name → Collection) and
  // toggle item visibility — reflect that in the horizontal view.
  updateHorizontalView();
}

int DetailsPane::previewBoxSize() const {
  if (!ui) {
    return UIConstants::Metadata::ARTWORK_SIZE;
  }
  // Vertical dock (the only case the original .ui artwork preview is shown
  // in — the dedicated horizontal view has its own preview tile). Tracks
  // the scroll area's viewport width minus 28px (10 + 10 layout margins +
  // 8px slack). Floored at 80 so an unsized panel doesn't collapse the box.
  int viewportW =
      (ui->scrollArea && ui->scrollArea->viewport()) ? ui->scrollArea->viewport()->width() : 0;
  if (viewportW <= 0 && ui->contentWidget) {
    viewportW = ui->contentWidget->width();
  }
  return qMax(80, viewportW - 28);
}

int DetailsPane::currentGalleryThumbSize() const {
  // Vertical dock uses the .ui's compact constant. Horizontal dock has its
  // own dedicated gallery inside m_horizontalView and ignores this.
  return UIConstants::Metadata::GALLERY_THUMB_SIZE;
}

void DetailsPane::applyGalleryThumbSize() {
  if (!m_galleryLayout) {
    return;
  }
  const int thumbSize = currentGalleryThumbSize();
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  for (int i = 0; i < m_galleryLayout->count(); ++i) {
    if (auto *btn = qobject_cast<QToolButton *>(m_galleryLayout->itemAt(i)->widget())) {
      btn->setFixedSize(thumbSize, thumbSize);
      btn->setIconSize(QSize(iconSize, iconSize));
    }
  }
}

void DetailsPane::applyDockOrientation() {
  if (!ui) {
    return;
  }
  const bool horizontal = CollectionUtils::isDetailsPaneHorizontal(m_position);
  // dedicated horizontal layout. Vertical dock keeps the.ui's
  // scrollArea-driven content; horizontal dock swaps in m_horizontalView,
  // a custom QHBoxLayout designed from scratch for a wide-and-short strip.
  // The vertical layout is left completely untouched so toggling back is
  // lossless.
  if (horizontal) {
    if (!m_horizontalView) {
      setupHorizontalView();
    }
    // Move the live video preview into the horizontal view so we don't have
    // two QMediaPlayer instances. The artworkDisplay STAYS in the vertical
    // contentLayout (it gets hidden along with scrollArea) — the user wants
    // the primary artwork rendered as a gallery thumb next to the video,
    // not as a separate big preview tile.
    if (m_hPreviewLayout && m_videoPreview && m_hPreviewLayout->indexOf(m_videoPreview) == -1) {
      m_hPreviewLayout->addWidget(m_videoPreview);
    }
    if (ui->scrollArea) ui->scrollArea->hide();
    if (m_horizontalView) m_horizontalView->show();
    updateHorizontalView();
  } else {
    if (m_horizontalView) m_horizontalView->hide();
    if (ui->scrollArea) ui->scrollArea->show();
    // Restore the video preview to its .ui-derived slot in contentLayout
    // (immediately after artworkDisplay).
    if (auto *cl = qobject_cast<QBoxLayout *>(ui->contentWidget->layout())) {
      if (m_videoPreview && ui->artworkDisplay && cl->indexOf(m_videoPreview) == -1) {
        const int artIdx = cl->indexOf(ui->artworkDisplay);
        cl->insertWidget(artIdx >= 0 ? artIdx + 1 : -1, m_videoPreview);
      }
    }
    if (ui->scrollArea) {
      // bug #2: vertical scrollbar suppressed even when content
      // overflows — wheel scroll still works. Restore the original .ui
      // behavior on L/R.
      ui->scrollArea->setWidgetResizable(true);
      ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
  }
  // Resize the preview boxes against the new orientation.
  applyPreviewSize();
}

void DetailsPane::applyPreviewSize() {
  // in horizontal dock the dedicated horizontal view owns the
  // sizing of its own preview tile (video only). The artworkDisplay stays
  // in the hidden vertical scrollArea so its size is irrelevant here.
  const bool horizontal = CollectionUtils::isDetailsPaneHorizontal(m_position);
  if (horizontal) {
    if (m_horizontalView && m_videoPreview) {
      const int previewSize = horizontalPreviewSize();
      m_videoPreview->setFixedSize(previewSize, previewSize);
    }
    return;
  }
  const int size = previewBoxSize();
  if (ui && ui->artworkDisplay) {
    ui->artworkDisplay->setFixedSize(size, size);
  }
  if (m_videoPreview) {
    m_videoPreview->setFixedSize(size, size);
  }
  // Vertical dock: scale gallery thumbs alongside the preview.
  applyGalleryThumbSize();
  if (!ui || !ui->artworkDisplay) {
    return;
  }
  // Re-render the cached source pixmap at the new size so the artwork stays
  // crisp under both shrink and grow. Falls back to a flat placeholder when
  // no artwork is loaded so the frame still paints.
  if (!m_artworkSource.isNull()) {
    QPixmap scaled =
        m_artworkSource.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap centered(size, size);
    centered.fill(palette().color(QPalette::Base));
    QPainter painter(&centered);
    const int x = (size - scaled.width()) / 2;
    const int y = (size - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);
    painter.end();
    ui->artworkDisplay->setPixmap(centered);
  } else {
    QPixmap empty(size, size);
    empty.fill(palette().color(QPalette::Mid));
    ui->artworkDisplay->setPixmap(empty);
  }
}

void DetailsPane::applyContentAlignment() {
  // Only the artwork-section header, artwork preview, video preview, and
  // the "Name:" label sit on the sidebar's center axis. The item name
  // *value* is intentionally NOT in this list — pairing layout-item
  // AlignHCenter with wordWrap=true makes Qt size the label to its
  // un-wrapped sizeHint and center it (overflowing both cell edges), which
  // hides the middle of long names instead of wrapping them. Letting the
  // value label fill the cell width keeps wrap behavior intact; its own
  // text alignment (set in the constructor) handles centering inside the
  // wrapped block.
  auto *contentLayout = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }
  const QList<QWidget *> centered = {ui->artworkLabel, ui->artworkDisplay, ui->itemNameLabel,
                                     m_videoPreview};
  for (QWidget *w : centered) {
    if (w && contentLayout->indexOf(w) >= 0) {
      contentLayout->setAlignment(w, Qt::AlignHCenter);
    }
  }
}

void DetailsPane::pausePreviewVideo() {
  // Cancel any debounced start so a delayed playVideo() doesn't fire under
  // the overlay. Soft-pause via hide() — the widget's hideEvent pauses the
  // QMediaPlayer but keeps the loaded source so resumePreviewVideo() can
  // pick up at the same playback position. m_pendingVideoPath is preserved
  // for the same reason: the resume path uses it when the prior schedule
  // was caught mid-debounce (path set but not yet loaded into the widget).
  if (m_videoStartTimer) {
    m_videoStartTimer->stop();
  }
  if (m_videoPreview) {
    m_videoPreview->hide();
  }
  // Restore the static artwork display so the sidebar isn't a black square
  // while the overlay is on top.
  ui->artworkDisplay->show();
}

void DetailsPane::resumePreviewVideo() {
  if (!m_videoPreview) {
    return;
  }
  if (m_videoPreview->hasLoadedSource()) {
    // showEvent inside VideoPreviewWidget calls QMediaPlayer::play() — and
    // the player was left in PausedState by hideEvent, so play() resumes
    // from the same position rather than restarting. Hide the static
    // artwork in vertical dock since the two share the same slot; in
    // horizontal dock they sit side-by-side and artwork visibility is
    // owned by the scrollArea (already hidden by applyDockOrientation).
    if (!CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      ui->artworkDisplay->hide();
    }
    m_videoPreview->show();
  } else if (!m_pendingVideoPath.isEmpty() && m_videoStartTimer) {
    // The pause caught us mid-debounce. Re-arm the timer so the original
    // play schedule fires again on the now-visible widget.
    m_videoStartTimer->start();
  }
}

bool DetailsPane::togglePreviewVideoPause() {
  if (!m_videoPreview || !m_videoPreview->hasLoadedSource()) {
    return false;
  }
  m_videoPreview->togglePauseResume();
  return true;
}

void DetailsPane::updateFilePathDisplay() {
  if (m_currentFilePath.isEmpty()) {
    return;
  }
  // set the full path; QLabel wordWrap=true (set in
  // updateFileInfo) handles per-character wrapping so the entire path is
  // visible without truncation.
  ui->filePathValue->setText(m_currentFilePath);
}

void DetailsPane::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateFilePathDisplay();
  // track the artwork + video preview to the sidebar's current
  // content width so a width-drag (or initial show) reflows the previews
  // instead of leaving them pinned at the .ui's 200px design size.
  applyPreviewSize();
  // re-elide path label + rescale preview tile in horizontal
  // dock when the panel is resized.
  if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
    updateHorizontalView();
  }
}

// Formats file size into human-readable string with appropriate units (KB, MB,
// GB)
auto DetailsPane::formatFileSize(qint64 bytes) -> QString {
  const qint64 kiloBytes = UIConstants::Metadata::FILE_SIZE_KB;
  const qint64 megaBytes = kiloBytes * UIConstants::Metadata::FILE_SIZE_KB;
  const qint64 gigaBytes = megaBytes * UIConstants::Metadata::FILE_SIZE_KB;

  if (bytes >= gigaBytes) {
    return QString::number(bytes / static_cast<double>(gigaBytes), 'f', 2) + " GB";
  }
  if (bytes >= megaBytes) {
    return QString::number(bytes / static_cast<double>(megaBytes), 'f', 2) + " MB";
  }
  if (bytes >= kiloBytes) {
    return QString::number(bytes / static_cast<double>(kiloBytes), 'f', 2) + " KB";
  }
  return QString::number(bytes) + " bytes";
}

// Load artwork from specified directory
void DetailsPane::loadArtwork(const QString &baseName, const QString &artworkDirectory) {
  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return;
  }

  const QStringList &bases = ExtensionUtils::imageBaseExtensions();
  for (const QString &ext : bases) {
    const QString &lower = ext;
    const QString upper = ext.toUpper();

    QString artworkPath = artworkDir.absoluteFilePath(baseName + "." + lower);
    if (!QFile::exists(artworkPath)) {
      artworkPath = artworkDir.absoluteFilePath(baseName + "." + upper);
    }
    if (QFile::exists(artworkPath)) {
      QPixmap artwork(artworkPath);
      if (!artwork.isNull()) {
        // Cache the original-resolution pixmap so applyPreviewSize can
        // re-render at the new dimension on sidebar resize without
        // re-reading from disk.
        m_artworkSource = artwork;
        m_primaryArtworkPath = artworkPath;
        applyPreviewSize();
        updateHorizontalView();
        return; // Found and loaded artwork
      }
    }
  }
}

// Apply horzontal scrolling policy
void DetailsPane::setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy) {
  if (ui->scrollArea) {
    ui->scrollArea->setHorizontalScrollBarPolicy(policy);
    ui->scrollArea->updateGeometry();
    QApplication::processEvents();
  }
}

// Stops any current preview video and shows the static artwork display
// instead. Called whenever selection changes (before the debounce timer
// resolves) and when metadata is cleared.
void DetailsPane::showArtworkOnly() {
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }
  ui->artworkDisplay->show();
}

// Schedule preview video playback after the debounce interval. Passing an
// empty path cancels any pending playback.
void DetailsPane::schedulePreviewVideo(const QString &videoPath) {
  m_pendingVideoPath = videoPath;
  if (!m_videoStartTimer) {
    return;
  }
  m_videoStartTimer->stop();
  if (!videoPath.isEmpty()) {
    m_videoStartTimer->start();
  }
}

// Lazily construct the Details section. Appended once to the bottom of the
// content layout; subsequent calls reuse the existing widgets so we do not
// churn the layout on every selection change.
void DetailsPane::ensureDetailsSection() {
  if (m_detailsContainer) {
    return;
  }
  auto *contentLayout = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  m_detailsContainer = new QWidget(ui->contentWidget);
  auto *outer = new QVBoxLayout(m_detailsContainer);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  auto *separator = new QFrame(m_detailsContainer);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  separator->setStyleSheet("color: palette(mid);");
  outer->addWidget(separator);

  m_detailsTitle = new QLabel(tr("Details"), m_detailsContainer);
  QFont titleFont = m_detailsTitle->font();
  titleFont.setBold(true);
  titleFont.setPointSize(11);
  m_detailsTitle->setFont(titleFont);
  m_detailsTitle->setStyleSheet("color: palette(highlight); padding: 4px 0px;");
  // tag for the bubble-bg stylesheet so the dynamic Details
  // header gets the same header bubble as the static section titles.
  m_detailsTitle->setProperty("sidebarRole", "header");
  outer->addWidget(m_detailsTitle);

  // Manual button is owned by the outer Details layout (above the per-row
  // sub-layout) so clearDetailsSection() — called on every selection change
  // before rows are rebuilt — does not destroy and recreate it. Visibility
  // is driven by setManualFile().
  m_manualButton = new QPushButton(tr("Open Manual"), m_detailsContainer);
  m_manualButton->setCursor(Qt::PointingHandCursor);
  m_manualButton->hide();
  connect(m_manualButton, &QPushButton::clicked, this, &DetailsPane::openCurrentManual);
  outer->addWidget(m_manualButton);

  m_detailsLayout = new QVBoxLayout();
  m_detailsLayout->setSpacing(UIConstants::Metadata::LABEL_SPACING);
  outer->addLayout(m_detailsLayout);

  contentLayout->addWidget(m_detailsContainer);
  m_detailsContainer->hide();
}

void DetailsPane::clearDetailsSection() {
  if (!m_detailsLayout) {
    return;
  }
  while (QLayoutItem *child = m_detailsLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
}

void DetailsPane::appendDetailRow(const QString &label, const QString &value, bool wrap) {
  if (!m_detailsLayout || value.trimmed().isEmpty()) {
    return;
  }
  auto *labelWidget = new QLabel(label + ":", m_detailsContainer);
  QFont labelFont = labelWidget->font();
  labelFont.setBold(true);
  labelWidget->setFont(labelFont);
  labelWidget->setStyleSheet("color: palette(windowtext); padding: 2px 0px;");

  auto *valueWidget = new QLabel(value, m_detailsContainer);
  valueWidget->setStyleSheet("color: palette(windowtext); padding: 2px 0px 8px 12px;");
  valueWidget->setWordWrap(wrap);
  valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // tag the value so the bubble-bg stylesheet picks it up.
  valueWidget->setProperty("sidebarRole", "value");

  m_detailsLayout->addWidget(labelWidget);
  m_detailsLayout->addWidget(valueWidget);
}

void DetailsPane::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  // Cache the canonical title so a tab switch back to Item can pick it up
  // without re-running the manager's selection pipeline. We do this even
  // on non-Item tabs because the cache is what lets applyTabVisibility()
  // restore the title later.
  m_currentMetadataTitle = metadata.title;

  // The Details container is shared with renderCollectionSummary on the
  // Collection tab — clearing/populating it from the per-item path would
  // wipe the just-built summary rows. Only the Item tab needs this body
  // to run; the File tab skips Details entirely (its content is the
  // file-info rows in the .ui).
  if (m_activeTab != DetailsPaneTab::Item) {
    return;
  }

  if (metadata.isEmpty()) {
    if (m_detailsContainer) {
      clearDetailsSection();
      // Keep the container visible only if a manual button is active;
      // otherwise hide it so the bottom of the sidebar stays clean.
      if (!m_manualButton || !m_manualButton->isVisible()) {
        m_detailsContainer->hide();
      }
    }
    return;
  }

  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  clearDetailsSection();

  if (!metadata.title.isEmpty()) {
    ui->itemNameValue->setText(metadata.title);
  }

  appendDetailRow(tr("Description"), metadata.description, /*wrap=*/true);
  appendDetailRow(tr("Genre"), metadata.genre);
  appendDetailRow(tr("Developer"), metadata.developer);
  appendDetailRow(tr("Publisher"), metadata.publisher);
  appendDetailRow(tr("Release date"), metadata.releaseDate);
  appendDetailRow(tr("Rating"), metadata.contentRating);
  appendDetailRow(tr("Players"), metadata.players);
  appendDetailRow(tr("Runtime"), formatRuntime(metadata.runtimeSeconds));
  appendDetailRow(tr("Tags"), formatTags(metadata.tags), /*wrap=*/true);

  // User-defined custom fields. Rendered after the structured
  // fields so they appear as a contiguous block at the bottom of Details.
  // parseCustomFields() returns rows in alphabetical key order for stable
  // display regardless of edit history.
  const auto customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);
  for (const auto &pair : customFields) {
    appendDetailRow(pair.first, pair.second, /*wrap=*/true);
  }

  // re-apply the active sidebar-font override so the just-
  // appended detail rows pick up the same font as the static labels. The
  // override falls back to a no-op when no override is in effect.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);

  m_detailsContainer->show();
}

void DetailsPane::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
  // Item-tab only. Usage stats append to m_detailsContainer alongside
  // the extended-metadata rows; on Collection tab the same container
  // holds the summary, on File tab it's hidden — pushing rows there
  // would either clobber the summary or leak item data into a hidden box.
  if (m_activeTab != DetailsPaneTab::Item) {
    return;
  }
  // Tracking columns default to zero/empty for items that have never been
  // launched; treat them as "no rows to add" so the Details section stays
  // hidden on bare items.
  if (stats.isEmpty()) {
    return;
  }
  // The Details section may already be hidden if extended metadata was empty
  // — reveal it for usage rows alone so first-launched items still surface
  // play_count without scraper data.
  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  if (stats.playCount > 0) {
    appendDetailRow(tr("Play count"), QString::number(stats.playCount));
  }
  if (!stats.lastPlayed.isEmpty()) {
    appendDetailRow(tr("Last played"), UsageStatsStore::formatTimestamp(stats.lastPlayed));
  }
  if (stats.totalPlaySeconds > 0) {
    appendDetailRow(tr("Time played"), UsageStatsStore::formatDuration(stats.totalPlaySeconds));
  }
  // same rationale as setExtendedMetadata — pull the new rows
  // under the active sidebar font.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);
  m_detailsContainer->show();
}

QString DetailsPane::formatRuntime(int seconds) {
  if (seconds < 0) {
    return {};
  }
  const int hours = seconds / 3600;
  const int minutes = (seconds % 3600) / 60;
  const int secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
  }
  if (minutes > 0) {
    return QStringLiteral("%1m %2s").arg(minutes).arg(secs, 2, 10, QChar('0'));
  }
  return QStringLiteral("%1s").arg(secs);
}

void DetailsPane::ensureManualButton() {
  // Manual button lives inside the Details container's outer layout. Build
  // the container on-demand so an item with only a manual (no extended
  // metadata) still gets a visible button.
  ensureDetailsSection();
}

void DetailsPane::setManualFile(const QString &manualPath) {
  m_manualPath = manualPath;
  const bool hasManual = !manualPath.isEmpty();
  // Manual button is item-only chrome. On other tabs the button stays
  // hidden regardless of whether the underlying item has a manual.
  if (m_activeTab != DetailsPaneTab::Item) {
    if (m_manualButton) {
      m_manualButton->setVisible(false);
    }
    return;
  }
  if (hasManual) {
    ensureManualButton();
  }
  if (!m_manualButton) {
    return;
  }
  m_manualButton->setVisible(hasManual);
  if (hasManual) {
    m_manualButton->setToolTip(manualPath);
    if (m_detailsContainer) {
      m_detailsContainer->show();
    }
  } else {
    m_manualButton->setToolTip(QString());
    // Only hide the container when there are also no detail rows; a
    // populated row layout means setExtendedMetadata wants it visible.
    if (m_detailsContainer && m_detailsLayout && m_detailsLayout->count() == 0) {
      m_detailsContainer->hide();
    }
  }
}

void DetailsPane::openCurrentManual() {
  if (m_manualPath.isEmpty()) {
    return;
  }
  // QDesktopServices::openUrl wraps xdg-open on Linux / open on macOS /
  // ShellExecute on Windows, so the user's default handler for the file
  // type takes over (Okular for PDF, web browser for HTML, etc.).
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_manualPath));
}

QString DetailsPane::formatTags(const QString &raw) {
  // Accept either a JSON array string or a comma-separated list. We do not
  // pull in QJsonDocument here to keep this widget lightweight; the Details
  // section just renders whatever the source provides with light cleanup.
  QString trimmed = raw.trimmed();
  if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
    trimmed.chop(1);
    trimmed.remove(0, 1);
    trimmed.replace('"', QString());
  }
  QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
  for (QString &p : parts) {
    p = p.trimmed();
  }
  parts.removeAll(QString());
  return parts.join(", ");
}
