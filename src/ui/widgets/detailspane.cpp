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
#include "extensionutils.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "detailspane.h"
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
  // Kartend-63e bug #6: stop mouse events on the sidebar from propagating up
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

  // Kartend-63e bug #2: hide the inner scrollbar entirely. With the sidebar
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

  // Kartend-xcfr: center the artwork-section header, artwork preview, video
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

  // Coming back from collection-summary mode (Kartend-3mn) — restore the
  // item-view chrome that was hidden while showing collection details.
  m_hasItemDisplayed = true;
  // Kartend-63e: only restore the per-item chrome if the user is on the
  // Item tab. Collection / File tabs keep their own visibility regardless of
  // selection — `m_hasItemDisplayed` is still tracked so a switch back to
  // Item picks up the latest selection.
  if (m_activeTab != DetailsPaneTab::Item) {
    return;
  }
  applyItemViewVisibility(true);
  ui->titleLabel->setText(tr("Item Information"));
  ui->itemNameLabel->setText(tr("Name:"));

  ui->itemNameValue->setText(itemName);
  m_currentItemName = itemName;
  updateFileInfo(filePath);

  QFileInfo fileInfo(filePath);
  const QString baseName = fileInfo.completeBaseName();

  // Drop the previous selection's cached artwork so applyPreviewSize falls
  // back to the empty placeholder while the new image is resolved.
  m_artworkSource = QPixmap();
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

  ui->titleLabel->show();
  ui->artworkLabel->show();
  ui->artworkDisplay->show();
  ui->itemNameLabel->show();
  ui->itemNameValue->show();
  ui->filePathLabel->show();
  ui->filePathValue->show();
  ui->fileSizeLabel->show();
  ui->fileSizeValue->show();
  ui->lastModifiedLabel->show();
  ui->lastModifiedValue->show();
  ui->fileExtensionLabel->show();
  ui->fileExtensionValue->show();
  // Kartend-u2gx: mirror the new selection into the dedicated horizontal view
  // so users on T/B dock see the live data without a tab/dock toggle.
  updateHorizontalView();
}

// Clears all metadata fields. When a collection summary has been cached
// (Kartend-3mn) the sidebar renders that instead of the legacy "No item
// selected" placeholder; this lets every existing clearMetadata() call site
// pick up the new no-selection display without per-caller plumbing.
void DetailsPane::clearMetadata() {
  m_hasItemDisplayed = false;
  m_currentItemName.clear();

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

  if (m_collectionSummary.isValid()) {
    renderCollectionSummary();
    return;
  }

  applyItemViewVisibility(true);
  ui->titleLabel->setText(tr("Item Information"));
  ui->itemNameLabel->setText(tr("Name:"));
  ui->itemNameValue->setText(tr("No item selected"));
  m_currentFilePath.clear();
  ui->filePathValue->setText("-");
  ui->filePathValue->setToolTip(QString());
  ui->fileSizeValue->setText("-");
  ui->lastModifiedValue->setText("-");
  ui->fileExtensionValue->setText("-");
  m_artworkSource = QPixmap();
  applyPreviewSize();
  updateHorizontalView();
}

void DetailsPane::setCollectionSummary(const CollectionSummary &summary) {
  m_collectionSummary = summary;
  // Re-render whenever the sidebar is currently in the no-selection state
  // so first-time application (during applySidebarStateForCollection) and
  // background refreshes (scan completion, settings save) both land. While
  // an item is selected we just update the cache and apply on next clear.
  if (!m_hasItemDisplayed) {
    clearMetadata();
  }
}

void DetailsPane::applyItemViewVisibility(bool visible) {
  // Per-item chrome that doesn't apply to collection summaries. Artwork +
  // file-info sections collapse together so the sidebar doesn't leave a
  // bare "Artwork" header above a hidden image.
  ui->artworkLabel->setVisible(visible);
  ui->artworkDisplay->setVisible(visible);
  if (m_videoPreview && !visible) {
    m_videoPreview->hide();
  }
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
  applyItemViewVisibility(false);
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

  // Kartend-ekaa: pull the just-built summary rows under the active sidebar-
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

  // Kartend-xcfr: wrap the full path across multiple lines instead of
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
  // Kartend-63e: opaque tab bar so the sidebar pattern doesn't bleed through
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
  // Lazy-build the File-tab placeholder on first switch so we don't pay for
  // it when the user never visits that tab.
  if (m_activeTab == DetailsPaneTab::File && !m_filePlaceholder && ui->contentWidget) {
    if (auto *cl = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout())) {
      m_filePlaceholder = new QLabel(tr("No custom view configured."), ui->contentWidget);
      m_filePlaceholder->setAlignment(Qt::AlignCenter);
      m_filePlaceholder->setWordWrap(true);
      m_filePlaceholder->setStyleSheet("color: palette(mid); padding: 32px 16px;");
      cl->addWidget(m_filePlaceholder);
    }
  }

  switch (m_activeTab) {
  case DetailsPaneTab::Item:
    if (m_filePlaceholder) m_filePlaceholder->hide();
    if (m_hasItemDisplayed) {
      applyItemViewVisibility(true);
    } else {
      // Falls back to the collection summary when no item is selected, the
      // existing behavior the rest of the codebase already exercises.
      renderCollectionSummary();
    }
    break;
  case DetailsPaneTab::Collection:
    if (m_filePlaceholder) m_filePlaceholder->hide();
    renderCollectionSummary();
    break;
  case DetailsPaneTab::File:
    applyItemViewVisibility(false);
    if (m_filePlaceholder) m_filePlaceholder->show();
    // Hide the title + collection summary chrome too so the placeholder
    // sits alone.
    ui->titleLabel->setVisible(false);
    ui->itemNameLabel->setVisible(false);
    ui->itemNameValue->setVisible(false);
    break;
  }

  if (m_activeTab != DetailsPaneTab::File) {
    ui->titleLabel->setVisible(true);
    ui->itemNameLabel->setVisible(true);
    ui->itemNameValue->setVisible(true);
  }
  // Kartend-u2gx: tab change can re-title labels (Name → Collection) and
  // toggle item visibility — reflect that in the horizontal view.
  updateHorizontalView();
}

void DetailsPane::applyAppearance(const CollectionConfig &collection) {
  // Kartend-u2gx: m_position must be set BEFORE setActiveTab(), because
  // applyTabVisibility() consults it to decide whether the section-title
  // chrome (titleLabel / artworkLabel / fileInfoTitle) should be hidden in
  // T/B dock. Calling setActiveTab first would run that visibility logic
  // against the previous orientation and the user would see ghost headers
  // on the first paint after a position change.
  m_widthLocked = collection.sidebarWidthLocked;
  m_position = collection.sidebarPosition;
  // Kartend-u2gx: flip the inner content layout direction + scrollbar policy
  // to match the active dock edge. Done before setActiveTab so the wrappers
  // and chrome state are in place when applyTabVisibility runs.
  applyDockOrientation();
  setActiveTab(collection.sidebarActiveTab);
  // Mouse tracking is required so we can change the cursor over the grip
  // strip without waiting for a click.
  setMouseTracking(!m_widthLocked);
  if (m_widthLocked) {
    unsetCursor();
  }
  // Kartend-u2gx: install/uninstall ourselves as event filter on inner widgets
  // so grip-zone clicks that would otherwise be eaten by them get routed back
  // here. Done every applyAppearance to handle the lock toggle. Mouse tracking
  // is enabled on these widgets too so cursor hints work without a click.
  const auto installFilter = [this](QWidget *w) {
    if (!w) return;
    if (m_widthLocked) {
      w->removeEventFilter(this);
      w->setMouseTracking(false);
      w->unsetCursor();
    } else {
      w->installEventFilter(this);
      w->setMouseTracking(true);
    }
  };
  if (ui->scrollArea) {
    installFilter(ui->scrollArea);
    installFilter(ui->scrollArea->viewport());
  }
  installFilter(ui->contentWidget);
  // Kartend-u2gx: in horizontal dock the .ui's scrollArea is hidden and the
  // dedicated m_horizontalView (plus its children) sits in front of every
  // pixel of the pane — without filtering them, grip-zone clicks get eaten
  // by the horizontal-view children and the resize handle effectively
  // doesn't exist in T/B + Horizontal-scrolling mode (Kartend-yl0t bug). We
  // walk findChildren so any future widget added in setupHorizontalView is
  // covered without having to repeat their names here.
  if (m_horizontalView) {
    installFilter(m_horizontalView);
    for (QWidget *w : m_horizontalView->findChildren<QWidget *>()) {
      installFilter(w);
    }
  }
  m_bgType = collection.sidebarBackgroundType;
  m_bgColor = QColor(collection.sidebarBackgroundColor);
  m_bgPattern = collection.sidebarPattern;
  m_patternIntensity = std::clamp(collection.sidebarPatternIntensity, 0, 100);
  m_patternColor = QColor(collection.sidebarPatternColor);
  if (collection.sidebarBackgroundImage.isEmpty()) {
    m_bgImage = QPixmap();
  } else {
    m_bgImage = QPixmap(collection.sidebarBackgroundImage);
  }

  // Make children transparent so the painted background shows through.
  // The default .ui sets autoFillBackground on each of these so the sidebar
  // draws as a solid palette(Window); flipping them off lets paintEvent
  // own the visual.
  setAutoFillBackground(false);
  if (ui->scrollArea) {
    ui->scrollArea->setAutoFillBackground(false);
    if (auto *vp = ui->scrollArea->viewport()) {
      vp->setAutoFillBackground(false);
      vp->setStyleSheet("background: transparent;");
    }
  }
  if (ui->contentWidget) {
    ui->contentWidget->setAutoFillBackground(false);
  }

  // Plumb text + accent color through the contentWidget palette so the
  // existing palette(highlight) / palette(windowtext) stylesheet hooks pick
  // them up without re-styling each label.
  if (ui->contentWidget) {
    QPalette pal = ui->contentWidget->palette();
    if (!collection.sidebarTextColor.isEmpty()) {
      const QColor textColor(collection.sidebarTextColor);
      if (textColor.isValid()) {
        pal.setColor(QPalette::WindowText, textColor);
        pal.setColor(QPalette::Text, textColor);
      }
    }
    if (!collection.sidebarAccentColor.isEmpty()) {
      const QColor accent(collection.sidebarAccentColor);
      if (accent.isValid()) {
        pal.setColor(QPalette::Highlight, accent);
      }
    }
    ui->contentWidget->setPalette(pal);
  }

  // Kartend-ekaa: per-collection sidebar font override. Layered on top of the
  // designer-set baseline so reverting (empty family + 0 size) restores the
  // original .ui look without us walking detailspane.ui at runtime.
  applySidebarFont(collection.sidebarFontFamily, collection.sidebarFontPointSize);

  // Kartend-63e: bubble backgrounds for readability over patterned bg.
  // The bubble color is RGB-only; the user-controlled opacity is layered
  // on top via the matching *Opacity field. When the color is blank, fall
  // back to a sensible default derived from the other sidebar colors so
  // bubbles always give some contrast without forcing a manual pick:
  //   • Header → accent color (or palette highlight).
  //   • Section → darker variant of the sidebar background color (or
  //     palette window darkened).
  auto composeBubble = [&](const QString &hex, int opacity, const QColor &fallback) -> QColor {
    QColor c = hex.isEmpty() ? fallback : QColor(hex);
    if (!c.isValid()) c = fallback;
    c.setAlpha(std::clamp(opacity, 0, 255));
    return c;
  };
  // Section fallback uses palette(Mid) — same color the missing-artwork
  // item placeholder paints as its bg, so the value rows tint to match the
  // item tiles. Header fallback is a slightly darker shade for hierarchy
  // *and* readability: a Highlight-colored header on top of a brighter
  // Highlight-derived bubble made the windowtext text disappear on dark
  // themes. A darker Mid keeps light text legible.
  QColor sectionFallback = QColor(collection.sidebarBackgroundColor);
  if (!sectionFallback.isValid()) sectionFallback = palette().color(QPalette::Mid);
  const QColor headerFallback = sectionFallback.darker(115);

  const QColor header = composeBubble(collection.sidebarHeaderBgColor,
                                      collection.sidebarHeaderBgOpacity, headerFallback);
  const QColor section = composeBubble(collection.sidebarSectionBgColor,
                                       collection.sidebarSectionBgOpacity, sectionFallback);
  applyBubbleStyles(header.alpha() == 0 ? QString() : header.name(QColor::HexArgb),
                    section.alpha() == 0 ? QString() : section.name(QColor::HexArgb));

  // Kartend-xcfr: re-anchor every layout item to AlignHCenter after the
  // appearance pass. The bubble stylesheet's polish step + any sections
  // inserted since the last call would otherwise drop back to flush-left.
  applyContentAlignment();

  update();
}

bool DetailsPane::isOnGrip(const QPoint &posInWidget) const {
  if (m_widthLocked) {
    return false;
  }
  // Kartend-u2gx: the grip lives on the inner edge — the side facing the grid.
  // Right dock → left edge; Left dock → right edge; Top dock → bottom edge;
  // Bottom dock → top edge.
  const int grip = UIConstants::DetailsPane::RESIZE_GRIP_PX;
  switch (m_position) {
  case DetailsPanePosition::Left:
    return posInWidget.x() >= width() - grip;
  case DetailsPanePosition::Top:
    return posInWidget.y() >= height() - grip;
  case DetailsPanePosition::Bottom:
    return posInWidget.y() < grip;
  case DetailsPanePosition::Right:
  default:
    return posInWidget.x() < grip;
  }
}

void DetailsPane::mousePressEvent(QMouseEvent *event) {
  if (!m_widthLocked && event->button() == Qt::LeftButton && isOnGrip(event->pos())) {
    if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      m_heightDragging = true;
      m_dragStartHeight = height();
      m_dragStartY = event->globalPosition().toPoint().y();
    } else {
      m_widthDragging = true;
      m_dragStartWidth = width();
      m_dragStartX = event->globalPosition().toPoint().x();
    }
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void DetailsPane::mouseMoveEvent(QMouseEvent *event) {
  if (m_widthDragging) {
    const int dx = event->globalPosition().toPoint().x() - m_dragStartX;
    // Right-anchored sidebar: drag-left increases width, drag-right shrinks.
    // Left-anchored: drag-right increases width.
    int candidate =
        m_position == DetailsPanePosition::Left ? m_dragStartWidth + dx : m_dragStartWidth - dx;
    candidate = std::max(candidate, UIConstants::DetailsPane::MIN_WIDTH);
    emit widthDragged(candidate);
    event->accept();
    return;
  }
  if (m_heightDragging) {
    // Kartend-u2gx: Top dock grows by dragging down (positive dy); Bottom dock
    // grows by dragging up (negative dy → height increases).
    const int dy = event->globalPosition().toPoint().y() - m_dragStartY;
    int candidate =
        m_position == DetailsPanePosition::Top ? m_dragStartHeight + dy : m_dragStartHeight - dy;
    candidate = std::max(candidate, UIConstants::DetailsPane::MIN_HEIGHT);
    emit heightDragged(candidate);
    event->accept();
    return;
  }
  if (!m_widthLocked) {
    if (isOnGrip(event->pos())) {
      setCursor(CollectionUtils::isDetailsPaneHorizontal(m_position) ? Qt::SplitVCursor
                                                                     : Qt::SplitHCursor);
    } else {
      setCursor(Qt::ArrowCursor);
    }
  }
  QWidget::mouseMoveEvent(event);
}

void DetailsPane::mouseReleaseEvent(QMouseEvent *event) {
  if (m_widthDragging && event->button() == Qt::LeftButton) {
    m_widthDragging = false;
    emit widthCommitted(width());
    event->accept();
    return;
  }
  if (m_heightDragging && event->button() == Qt::LeftButton) {
    m_heightDragging = false;
    emit heightCommitted(height());
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void DetailsPane::leaveEvent(QEvent *event) {
  if (!m_widthDragging && !m_heightDragging) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

bool DetailsPane::eventFilter(QObject *watched, QEvent *event) {
  if (m_widthLocked) {
    return QWidget::eventFilter(watched, event);
  }
  // Kartend-u2gx: forward press/move/release in the grip zone from any inner
  // widget back to ourselves. mapTo(this, ...) translates the source widget's
  // local coords into DetailsPane coords so isOnGrip() and the existing
  // drag math both work unmodified.
  auto *child = qobject_cast<QWidget *>(watched);
  if (!child) {
    return QWidget::eventFilter(watched, event);
  }
  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton) break;
    const QPoint posInPanel = child->mapTo(this, me->position().toPoint());
    if (!isOnGrip(posInPanel)) break;
    if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      m_heightDragging = true;
      m_dragStartHeight = height();
      m_dragStartY = me->globalPosition().toPoint().y();
    } else {
      m_widthDragging = true;
      m_dragStartWidth = width();
      m_dragStartX = me->globalPosition().toPoint().x();
    }
    me->accept();
    return true;
  }
  case QEvent::MouseMove: {
    if (!m_widthDragging && !m_heightDragging) {
      // Update the cursor when hovering over the grip zone via a child
      // widget — without this the user gets no visual cue that the grip
      // is reachable from inside the scroll area / content widget.
      auto *me = static_cast<QMouseEvent *>(event);
      const QPoint posInPanel = child->mapTo(this, me->position().toPoint());
      if (isOnGrip(posInPanel)) {
        child->setCursor(CollectionUtils::isDetailsPaneHorizontal(m_position) ? Qt::SplitVCursor
                                                                              : Qt::SplitHCursor);
      } else {
        child->unsetCursor();
      }
      break;
    }
    auto *me = static_cast<QMouseEvent *>(event);
    if (m_widthDragging) {
      const int dx = me->globalPosition().toPoint().x() - m_dragStartX;
      int candidate =
          m_position == DetailsPanePosition::Left ? m_dragStartWidth + dx : m_dragStartWidth - dx;
      candidate = std::max(candidate, UIConstants::DetailsPane::MIN_WIDTH);
      emit widthDragged(candidate);
    } else {
      const int dy = me->globalPosition().toPoint().y() - m_dragStartY;
      int candidate =
          m_position == DetailsPanePosition::Top ? m_dragStartHeight + dy : m_dragStartHeight - dy;
      candidate = std::max(candidate, UIConstants::DetailsPane::MIN_HEIGHT);
      emit heightDragged(candidate);
    }
    me->accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (m_widthDragging && me->button() == Qt::LeftButton) {
      m_widthDragging = false;
      emit widthCommitted(width());
      me->accept();
      return true;
    }
    if (m_heightDragging && me->button() == Qt::LeftButton) {
      m_heightDragging = false;
      emit heightCommitted(height());
      me->accept();
      return true;
    }
    break;
  }
  default:
    break;
  }
  return QWidget::eventFilter(watched, event);
}

void DetailsPane::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  const QColor fallbackBase = palette().color(QPalette::Window);
  const QColor baseColor = m_bgColor.isValid() ? m_bgColor : fallbackBase;

  switch (m_bgType) {
  case DetailsPaneBackgroundType::Color:
    painter.fillRect(rect(), baseColor);
    break;
  case DetailsPaneBackgroundType::Image:
    painter.fillRect(rect(), baseColor);
    if (!m_bgImage.isNull()) {
      // Scale to cover the sidebar while preserving aspect ratio. Centered
      // crop matches the main-view background-position: center semantics.
      const QSize target = size();
      QPixmap scaled =
          m_bgImage.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      const int x = (target.width() - scaled.width()) / 2;
      const int y = (target.height() - scaled.height()) / 2;
      painter.drawPixmap(x, y, scaled);
    }
    break;
  case DetailsPaneBackgroundType::Pattern: {
    // Single full-size pattern (no tiling, no vertical gradient).
    // Pass our own palette Mid so the pattern picks up any palette overrides
    // inherited from an ancestor (itemsPage / m_mainContentWidget). Without
    // this the static helper used QApplication::palette() Mid, which on
    // some setups is visibly lighter than the items grid's effective Mid.
    const QColor mid = palette().color(QPalette::Mid);
    const QPixmap tile =
        ItemWidget::buildPlaceholderTile(width(), height(), /*cornerRadius=*/0,
                                         /*applyGradient=*/false, mid,
                                         /*lineAlphaScale=*/m_patternIntensity / 100.0);
    if (!tile.isNull()) {
      painter.drawPixmap(0, 0, tile);
    } else {
      painter.fillRect(rect(), baseColor);
    }
    // Optional user-controlled tint on top. Lines are already dimmed via
    // lineAlphaScale=0.5 above, so no default overlay is needed; users who
    // want extra darkening can set sidebarPatternColor.
    if (m_patternColor.isValid() && m_patternColor.alpha() > 0) {
      painter.fillRect(rect(), m_patternColor);
    }
    break;
  }
  }

  // Kartend-63e / Kartend-u2gx: paint a guideline on the inner (grid-facing)
  // edge while the resize lock is off so the user can see where to grab.
  // Skipped during the live drag so the line doesn't smear. A 1px line was
  // too easy to miss on T/B docks, where the items area's horizontal
  // scrollbar lives right next to the grip — bump it to a 2px band and
  // overlay a short central tick so the handle reads as a deliberate
  // affordance rather than a stray highlight pixel.
  if (!m_widthLocked && !m_widthDragging && !m_heightDragging) {
    QColor edgeColor = palette().color(QPalette::Highlight);
    edgeColor.setAlpha(180);
    QColor tickColor = edgeColor;
    tickColor.setAlpha(255);
    const int w = width();
    const int h = height();
    auto drawHandle = [&](int x1, int y1, int x2, int y2, bool horizontalEdge) {
      painter.setPen(QPen(edgeColor, 2));
      painter.drawLine(x1, y1, x2, y2);
      // Center tick: a short perpendicular bar (~24px) drawn full-alpha so
      // the handle is visible even when the band sits behind a scrollbar
      // shadow on dark themes.
      painter.setPen(QPen(tickColor, 2));
      const int tickHalf = 12;
      if (horizontalEdge) {
        const int cx = w / 2;
        painter.drawLine(cx - tickHalf, y1, cx + tickHalf, y1);
      } else {
        const int cy = h / 2;
        painter.drawLine(x1, cy - tickHalf, x1, cy + tickHalf);
      }
    };
    switch (m_position) {
    case DetailsPanePosition::Left:
      drawHandle(w - 1, 0, w - 1, h, /*horizontalEdge=*/false);
      break;
    case DetailsPanePosition::Top:
      drawHandle(0, h - 1, w, h - 1, /*horizontalEdge=*/true);
      break;
    case DetailsPanePosition::Bottom:
      drawHandle(0, 0, w, 0, /*horizontalEdge=*/true);
      break;
    case DetailsPanePosition::Right:
    default:
      drawHandle(0, 0, 0, h, /*horizontalEdge=*/false);
      break;
    }
  }

  QWidget::paintEvent(event);
}

void DetailsPane::captureLabelFontBaselines() {
  // Sweep every current QLabel in the subtree and record any that we haven't
  // seen yet. Each label's *current* font is taken as its baseline — this is
  // correct because we only call this from applySidebarFont before applying
  // the override, so the snapshot is the un-overridden designer/code font.
  // Dead pointers from previous item changes are dropped here so the list
  // doesn't grow unbounded across many selection changes.
  m_labelFontBaselines.removeIf([](const LabelFontBaseline &b) { return b.label.isNull(); });
  QSet<QLabel *> known;
  known.reserve(m_labelFontBaselines.size());
  for (const auto &b : m_labelFontBaselines) {
    if (b.label) {
      known.insert(b.label.data());
    }
  }
  const auto labels = findChildren<QLabel *>();
  for (QLabel *lbl : labels) {
    if (lbl && !known.contains(lbl)) {
      m_labelFontBaselines.append({QPointer<QLabel>(lbl), lbl->font()});
    }
  }
  m_labelFontBaselinesCaptured = true;
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

int DetailsPane::horizontalPreviewSize() const {
  // Available vertical space = panel height − tabBar height − outer margins
  // (m_horizontalView's QHBoxLayout has 8px top + 8px bottom = 16px).
  // Floored at 80 so an unsized panel doesn't render postage-stamp previews.
  int avail = height();
  if (m_tabBar) avail -= m_tabBar->height();
  avail -= 16;
  return qMax(80, avail);
}

void DetailsPane::setupHorizontalView() {
  if (m_horizontalView) {
    return;
  }
  m_horizontalView = new QWidget(this);
  auto *outer = new QHBoxLayout(m_horizontalView);
  outer->setContentsMargins(8, 8, 8, 8);
  outer->setSpacing(16);
  outer->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // Preview tile: holds m_videoPreview, reparented in via applyDockOrientation.
  // Sits at the far left.
  m_hPreviewArea = new QWidget(m_horizontalView);
  m_hPreviewLayout = new QHBoxLayout(m_hPreviewArea);
  m_hPreviewLayout->setContentsMargins(0, 0, 0, 0);
  m_hPreviewLayout->setSpacing(8);
  m_hPreviewLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  outer->addWidget(m_hPreviewArea);

  // Gallery host immediately after preview, so artwork thumbs (including the
  // primary one) sit flush to the left next to the video.
  m_hGalleryHost = new QWidget(m_horizontalView);
  m_hGalleryLayout = new QHBoxLayout(m_hGalleryHost);
  m_hGalleryLayout->setContentsMargins(0, 0, 0, 0);
  m_hGalleryLayout->setSpacing(UIConstants::Metadata::GALLERY_THUMB_SPACING);
  m_hGalleryLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  outer->addWidget(m_hGalleryHost);

  // Info column: item name (large + bold) above an elided file path.
  auto *infoCol = new QVBoxLayout();
  infoCol->setContentsMargins(0, 0, 0, 0);
  infoCol->setSpacing(4);
  infoCol->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_hName = new QLabel(m_horizontalView);
  {
    QFont f = m_hName->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.4);
    m_hName->setFont(f);
  }
  m_hName->setStyleSheet("color: palette(highlight);");
  m_hName->setWordWrap(true);
  infoCol->addWidget(m_hName);
  m_hPath = new QLabel(m_horizontalView);
  // Kartend-u2gx: path inherits the same palette WindowText as the metadata
  // value labels so it reads as a continuation of the info card, not a
  // dimmed secondary line.
  m_hPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
  infoCol->addWidget(m_hPath);
  infoCol->addStretch(1);
  outer->addLayout(infoCol);

  // Metadata grid wrapped in a container so we can hide it cleanly on
  // non-Item tabs (a bare QFormLayout has no widget handle to toggle).
  m_hMetaContainer = new QWidget(m_horizontalView);
  auto *metaForm = new QFormLayout(m_hMetaContainer);
  metaForm->setContentsMargins(0, 0, 0, 0);
  metaForm->setHorizontalSpacing(8);
  metaForm->setVerticalSpacing(4);
  metaForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  metaForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  m_hSizeValue = new QLabel(m_hMetaContainer);
  m_hModifiedValue = new QLabel(m_hMetaContainer);
  m_hTypeValue = new QLabel(m_hMetaContainer);
  metaForm->addRow(tr("Size:"), m_hSizeValue);
  metaForm->addRow(tr("Modified:"), m_hModifiedValue);
  metaForm->addRow(tr("Type:"), m_hTypeValue);
  outer->addWidget(m_hMetaContainer);

  // Stretch eats the slack between the gallery and the trailing Edit button
  // so Edit pins to the far right of the panel.
  outer->addStretch(1);

  m_hEditButton = new QPushButton(tr("Edit"), m_horizontalView);
  m_hEditButton->setCursor(Qt::PointingHandCursor);
  m_hEditButton->setToolTip(tr("Pick override files for any artwork type (standard or custom)."));
  m_hEditButton->setVisible(m_galleryEditEnabled);
  connect(m_hEditButton, &QPushButton::clicked, this, &DetailsPane::editArtworkRequested);
  outer->addWidget(m_hEditButton);

  // Insert into mainLayout below tabBar / scrollArea so it can be toggled
  // into view.
  if (auto *mainLayout = qobject_cast<QVBoxLayout *>(layout())) {
    mainLayout->addWidget(m_horizontalView);
  }
  m_horizontalView->hide();
}

void DetailsPane::rebuildHorizontalGallery() {
  if (!m_hGalleryLayout) return;
  // Clear existing thumbs.
  while (QLayoutItem *child = m_hGalleryLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) w->deleteLater();
    delete child;
  }
  // Thumbs scale to ~70% of the preview tile so they read as supporting
  // siblings rather than competing for visual weight with the main preview.
  // Floored at the .ui's compact constant to stay clickable on tiny panels.
  const int thumbSize =
      qMax(UIConstants::Metadata::GALLERY_THUMB_SIZE, (horizontalPreviewSize() * 7) / 10);
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = qMax(24, thumbSize - (padding * 2));

  bool addedThumb = false;

  // Kartend-u2gx: prepend the primary artwork as the first thumb. The grid
  // tile and m_artworkSource use the same loadArtwork resolution path —
  // which differs from ItemArtworkStore::resolveArtworkPath used by the
  // gallery entries (the latter only finds files inside typed sub-
  // directories like artwork/box/, artwork/screenshot/, etc.). Rendering
  // m_artworkSource here means the user always sees the same artwork the
  // grid shows, even when the typed-subdirectory layout is missing.
  if (!m_artworkSource.isNull()) {
    auto *primaryBtn = new QToolButton(m_hGalleryHost);
    primaryBtn->setAutoRaise(true);
    primaryBtn->setCursor(Qt::PointingHandCursor);
    primaryBtn->setFixedSize(thumbSize, thumbSize);
    primaryBtn->setIconSize(QSize(iconSize, iconSize));
    primaryBtn->setIcon(QIcon(m_artworkSource));
    primaryBtn->setToolTip(tr("Artwork"));
    primaryBtn->setAccessibleName(tr("Artwork"));
    m_hGalleryLayout->addWidget(primaryBtn);
    addedThumb = true;
  }

  // Track which paths we've already added so the typed-subdirectory thumbs
  // below don't duplicate the primary if it happens to live in one of them.
  // The primary thumb has no path stored on it; we approximate by skipping
  // any entry whose pixmap matches the first added thumb's pixmap by file
  // path. Since m_artworkSource isn't keyed to a path, we skip this dedupe
  // for now and accept the rare double thumb.

  for (const GalleryEntry &entry : m_galleryEntries) {
    if (entry.isVideo) continue; // Live preview tile handles video.
    QPixmap pixmap(entry.path);
    if (pixmap.isNull()) continue;
    auto *btn = new QToolButton(m_hGalleryHost);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(thumbSize, thumbSize);
    btn->setIconSize(QSize(iconSize, iconSize));
    btn->setIcon(QIcon(pixmap));
    btn->setToolTip(entry.label);
    btn->setAccessibleName(entry.label);
    const GalleryEntry capturedEntry = entry;
    connect(btn, &QToolButton::clicked, this,
            [this, capturedEntry]() { openGalleryPreview(capturedEntry); });
    m_hGalleryLayout->addWidget(btn);
    addedThumb = true;
  }
  if (m_hGalleryHost) {
    m_hGalleryHost->setVisible(addedThumb);
  }
}

void DetailsPane::updateHorizontalView() {
  if (!m_horizontalView) return;
  // Kartend-u2gx: only run when actually in horizontal dock. If the user
  // toggled to T/B then back to L/R, m_horizontalView still exists (hidden)
  // — and previously this method was overriding m_videoPreview's size to
  // horizontalPreviewSize(), which broke vertical-mode video playback
  // because the widget was sized for the wrong axis.
  if (!CollectionUtils::isDetailsPaneHorizontal(m_position)) return;

  // Kartend-u2gx: tab-driven visibility — hide item-specific content on
  // Collection / File tabs so flipping tabs has a real visual effect.
  // Item tab: full strip (preview, info, metadata, gallery, edit).
  // Collection tab: just the collection name (other fields are item-only).
  // File tab: only the placeholder text.
  const bool isItemTab = (m_activeTab == DetailsPaneTab::Item);
  const bool isFileTab = (m_activeTab == DetailsPaneTab::File);

  // Mirror the .ui's itemNameValue text — it already gets retitled per tab
  // (item name on Item tab, collection name on Collection tab) and per
  // selection state, so the horizontal view picks up whatever the vertical
  // view is currently showing.
  if (m_hName && ui) {
    if (isFileTab) {
      m_hName->setText(tr("Custom file view"));
    } else {
      const QString name = ui->itemNameValue->text();
      m_hName->setText(name.isEmpty() ? tr("(no selection)") : name);
    }
  }
  // Path: copy from the .ui's filePathValue (already populated by
  // updateFileInfo). Eliding to ~600px keeps the strip readable; long paths
  // wrap to a second line via the label's wordWrap when set.
  if (m_hPath) {
    if (isItemTab) {
      const QString fullPath = ui ? ui->filePathValue->text() : QString();
      QFontMetrics fm(m_hPath->font());
      const int budget = qMax(200, m_horizontalView->width() / 3);
      m_hPath->setText(fm.elidedText(fullPath, Qt::ElideMiddle, budget));
      m_hPath->setToolTip(fullPath);
      m_hPath->setVisible(true);
    } else {
      m_hPath->setVisible(false);
    }
  }
  if (m_hSizeValue && ui) m_hSizeValue->setText(ui->fileSizeValue->text());
  if (m_hModifiedValue && ui) m_hModifiedValue->setText(ui->lastModifiedValue->text());
  if (m_hTypeValue && ui) m_hTypeValue->setText(ui->fileExtensionValue->text());
  if (m_hMetaContainer) m_hMetaContainer->setVisible(isItemTab);
  // Gallery (artwork thumbs) and Edit button stay visible across tabs —
  // the user wants the same media row regardless of which tab is active.
  // rebuildHorizontalGallery decides the actual visibility based on
  // whether any thumbs were successfully built.
  if (m_hEditButton) {
    m_hEditButton->setVisible(m_galleryEditEnabled);
  }

  // Live video preview tile — only widget in the preview area now. Artwork
  // (including the primary) lives in the gallery strip per user request.
  // Re-show + restart playback if hidden; vertical-mode tab changes can call
  // applyItemViewVisibility(false) which hides it without re-firing the
  // debounce timer.
  const int previewSize = horizontalPreviewSize();
  if (m_videoPreview) {
    m_videoPreview->setFixedSize(previewSize, previewSize);
    const bool hasVideo = !m_pendingVideoPath.isEmpty();
    if (hasVideo) {
      const bool wasHidden = !m_videoPreview->isVisible();
      m_videoPreview->show();
      if (wasHidden) {
        m_videoPreview->playVideo(m_pendingVideoPath);
      }
    } else {
      m_videoPreview->hide();
    }
  }
  // Preview area visibility tracks ONLY whether there's a video to play —
  // it stays visible across Item/Collection/File tabs so the user sees the
  // same media presence on every tab.
  if (m_hPreviewArea) {
    const bool hasVideo = m_videoPreview && !m_pendingVideoPath.isEmpty();
    m_hPreviewArea->setVisible(hasVideo);
  }
  rebuildHorizontalGallery();
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
  // Kartend-u2gx: dedicated horizontal layout. Vertical dock keeps the .ui's
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
    if (m_hPreviewLayout && m_videoPreview &&
        m_hPreviewLayout->indexOf(m_videoPreview) == -1) {
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
      // Kartend-63e bug #2: vertical scrollbar suppressed even when content
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
  // Kartend-u2gx: in horizontal dock the dedicated horizontal view owns the
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
  const QList<QWidget *> centered = {
      ui->artworkLabel, ui->artworkDisplay, ui->itemNameLabel, m_videoPreview};
  for (QWidget *w : centered) {
    if (w && contentLayout->indexOf(w) >= 0) {
      contentLayout->setAlignment(w, Qt::AlignHCenter);
    }
  }
}

void DetailsPane::applySidebarFont(const QString &family, int pointSize) {
  m_activeSidebarFontFamily = family.trimmed();
  m_activeSidebarFontPointSize = pointSize;
  captureLabelFontBaselines();
  // Kartend-7eff: scale the override (or each label's baseline pointSize when
  // the user hasn't picked one) by the active text-zoom multiplier so the
  // sidebar tracks Ctrl+= / Ctrl+- alongside the rest of the UI.
  const int zoomedOverride = MainWindow::zoomedFontSize(m_activeSidebarFontPointSize);
  for (const auto &baseline : m_labelFontBaselines) {
    QLabel *lbl = baseline.label.data();
    if (!lbl) {
      continue;
    }
    QFont f = baseline.font;
    if (!m_activeSidebarFontFamily.isEmpty()) {
      f.setFamily(m_activeSidebarFontFamily);
    }
    if (zoomedOverride > 0) {
      f.setPointSize(zoomedOverride);
    } else if (baseline.font.pointSize() > 0) {
      // No explicit override → scale the baseline so the title-vs-body
      // hierarchy from the .ui file is preserved while still tracking zoom.
      f.setPointSize(MainWindow::zoomedFontSize(baseline.font.pointSize()));
    }
    if (lbl->font() != f) {
      lbl->setFont(f);
    }
  }
}

void DetailsPane::applyBubbleStyles(const QString &headerHex, const QString &sectionHex) {
  if (!ui->contentWidget) {
    return;
  }
  // Build a stylesheet that selects the existing static-label objectNames in
  // detailspane.ui. Dynamic detail rows are tagged via objectName at
  // creation in appendDetailRow / ensureGallerySection so they pick up the
  // same bubble. Empty hex disables that bubble (no rule emitted).
  QString sheet;
  const auto qcolorOrEmpty = [](const QString &hex) {
    if (hex.isEmpty()) return QColor();
    QColor c(hex);
    return c.isValid() ? c : QColor();
  };
  const QColor headerColor = qcolorOrEmpty(headerHex);
  const QColor sectionColor = qcolorOrEmpty(sectionHex);

  if (headerColor.isValid()) {
    const QString rgba = QString("rgba(%1,%2,%3,%4)")
                             .arg(headerColor.red())
                             .arg(headerColor.green())
                             .arg(headerColor.blue())
                             .arg(headerColor.alpha());
    sheet += QString("QLabel#titleLabel, QLabel#artworkLabel, QLabel#fileInfoTitle, "
                     "QLabel[sidebarRole=\"header\"] { "
                     "background-color: %1; border-radius: 6px; padding: 4px 8px; }")
                 .arg(rgba);
  }
  if (sectionColor.isValid()) {
    const QString rgba = QString("rgba(%1,%2,%3,%4)")
                             .arg(sectionColor.red())
                             .arg(sectionColor.green())
                             .arg(sectionColor.blue())
                             .arg(sectionColor.alpha());
    sheet += QString("QLabel#itemNameValue, QLabel#filePathValue, QLabel#fileSizeValue, "
                     "QLabel#lastModifiedValue, QLabel#fileExtensionValue, "
                     "QLabel[sidebarRole=\"value\"] { "
                     "background-color: %1; border-radius: 6px; padding: 4px 8px; }")
                 .arg(rgba);
  }
  ui->contentWidget->setStyleSheet(sheet);
  // Force a polish so newly-applied stylesheet rules take effect on already
  // visible labels — without this, the first switch from blank → set bg
  // sometimes leaves the labels unstyled until the next layout event.
  ui->contentWidget->style()->polish(ui->contentWidget);
}

void DetailsPane::pausePreviewVideo() {
  // Cancel any debounced start so a tab/selection change immediately before
  // the overlay opened doesn't fire a delayed playVideo() under the overlay.
  if (m_videoStartTimer) {
    m_videoStartTimer->stop();
  }
  m_pendingVideoPath.clear();
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }
  // Restore the static artwork display so the sidebar isn't a black square
  // while the overlay is on top.
  ui->artworkDisplay->show();
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
  // Kartend-xcfr: set the full path; QLabel wordWrap=true (set in
  // updateFileInfo) handles per-character wrapping so the entire path is
  // visible without truncation.
  ui->filePathValue->setText(m_currentFilePath);
}

void DetailsPane::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateFilePathDisplay();
  // Kartend-xcfr: track the artwork + video preview to the sidebar's current
  // content width so a width-drag (or initial show) reflows the previews
  // instead of leaving them pinned at the .ui's 200px design size.
  applyPreviewSize();
  // Kartend-u2gx: re-elide path label + rescale preview tile in horizontal
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
  // Kartend-63e: tag for the bubble-bg stylesheet so the dynamic Details
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
  // Kartend-63e: tag the value so the bubble-bg stylesheet picks it up.
  valueWidget->setProperty("sidebarRole", "value");

  m_detailsLayout->addWidget(labelWidget);
  m_detailsLayout->addWidget(valueWidget);
}

void DetailsPane::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
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

  // If a scraped title is present, override the file-derived itemName so the
  // user sees the canonical title rather than the rom file stem.
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

  // User-defined custom fields (Kartend-hpln). Rendered after the structured
  // fields so they appear as a contiguous block at the bottom of Details.
  // parseCustomFields() returns rows in alphabetical key order for stable
  // display regardless of edit history.
  const auto customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);
  for (const auto &pair : customFields) {
    appendDetailRow(pair.first, pair.second, /*wrap=*/true);
  }

  // Kartend-ekaa: re-apply the active sidebar-font override so the just-
  // appended detail rows pick up the same font as the static labels. The
  // override falls back to a no-op when no override is in effect.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);

  m_detailsContainer->show();
}

void DetailsPane::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
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
  // Kartend-ekaa: same rationale as setExtendedMetadata — pull the new rows
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

void DetailsPane::ensureGallerySection() {
  if (m_galleryContainer) {
    return;
  }
  auto *contentLayout = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  m_galleryContainer = new QWidget(ui->contentWidget);
  auto *outer = new QVBoxLayout(m_galleryContainer);
  outer->setContentsMargins(0, UIConstants::Metadata::LABEL_SPACING, 0,
                            UIConstants::Metadata::LABEL_SPACING);
  outer->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  // Title row pairs the section heading with the per-item edit affordance
  // (Kartend-53vk). The button is short ("Edit") with a tooltip carrying
  // the longer description so a narrow / zoomed sidebar doesn't clip the
  // label — the previous "Edit links…" got cut off at the user's font zoom.
  auto *titleRow = new QHBoxLayout();
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  auto *title = new QLabel(tr("Media gallery"), m_galleryContainer);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  title->setFont(titleFont);
  title->setStyleSheet("color: palette(windowtext); padding: 2px 0px;");
  titleRow->addWidget(title);
  // Kartend-u2gx: Edit button stays directly to the right of the title with
  // a small gap, instead of being pushed to the far right by a stretch.
  // The previous addStretch made the button's location depend on the
  // section's allocated width — which varies between vertical and horizontal
  // dock — so users saw the button "move" between layouts.

  m_galleryEditButton = new QPushButton(tr("Edit"), m_galleryContainer);
  m_galleryEditButton->setCursor(Qt::PointingHandCursor);
  m_galleryEditButton->setToolTip(
      tr("Pick override files for any artwork type (standard or custom)."));
  m_galleryEditButton->setVisible(m_galleryEditEnabled);
  connect(m_galleryEditButton, &QPushButton::clicked, this, &DetailsPane::editArtworkRequested);
  titleRow->addWidget(m_galleryEditButton);
  // Trailing stretch so any slack in the row goes to the right of the Edit
  // button rather than between title and button.
  titleRow->addStretch(1);
  outer->addLayout(titleRow);

  // Horizontal layout wrapped in a plain widget; if more than ~4 thumbs are
  // present they wrap onto the next row by virtue of QLayout's setAlignment.
  // A QScrollArea was considered but adds vertical chrome that fights the
  // sidebar's own scroll area.
  m_galleryThumbsHost = new QWidget(m_galleryContainer);
  m_galleryLayout = new QHBoxLayout(m_galleryThumbsHost);
  m_galleryLayout->setContentsMargins(0, 0, 0, 0);
  m_galleryLayout->setSpacing(UIConstants::Metadata::GALLERY_THUMB_SPACING);
  m_galleryLayout->setAlignment(Qt::AlignLeft);
  outer->addWidget(m_galleryThumbsHost);

  // Insert just below the artwork preview pane (and the dynamically-added
  // video preview, if any) so all visual artwork stays clustered. Falling
  // back to "append" keeps the section visible if the layout shape changes.
  int insertIndex = -1;
  if (auto *artworkParentLayout =
          qobject_cast<QVBoxLayout *>(ui->artworkDisplay->parentWidget()->layout())) {
    if (artworkParentLayout == contentLayout) {
      const int videoIdx = m_videoPreview ? contentLayout->indexOf(m_videoPreview) : -1;
      const int artIdx = contentLayout->indexOf(ui->artworkDisplay);
      const int anchor = videoIdx >= 0 ? videoIdx : artIdx;
      if (anchor >= 0) {
        insertIndex = anchor + 1;
      }
    }
  }
  if (insertIndex >= 0) {
    contentLayout->insertWidget(insertIndex, m_galleryContainer);
  } else {
    contentLayout->addWidget(m_galleryContainer);
  }
  m_galleryContainer->hide();
}

void DetailsPane::clearGallerySection() {
  if (!m_galleryLayout) {
    return;
  }
  while (QLayoutItem *child = m_galleryLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
}

void DetailsPane::setArtworkGallery(const QList<GalleryEntry> &entries) {
  // Kartend-u2gx: cache the raw entries so the dedicated horizontal view
  // can render its own gallery from the same list (with video filtered out
  // since the inline preview already plays it).
  m_galleryEntries = entries;
  if (entries.isEmpty()) {
    if (m_galleryContainer) {
      clearGallerySection();
      if (m_galleryThumbsHost) {
        m_galleryThumbsHost->hide();
      }
      // Keep the section visible when the user can still edit links so
      // the "Edit links…" button stays available for items with no current
      // artwork — that's the exact case where adding a manual link matters
      // most. When edit is disabled we fall back to the legacy "hide
      // section entirely" behaviour.
      if (m_galleryEditEnabled) {
        m_galleryContainer->show();
      } else {
        m_galleryContainer->hide();
      }
    }
    // Kartend-u2gx: also clear the horizontal gallery so a previous
    // selection's thumbs don't linger when the new selection has no
    // artwork. The Edit button (if enabled) is rebuilt by rebuildHorizontalGallery.
    rebuildHorizontalGallery();
    return;
  }

  ensureGallerySection();
  if (!m_galleryContainer || !m_galleryLayout) {
    return;
  }
  clearGallerySection();

  // Kartend-u2gx: pick the thumb size based on dock orientation so a horizontal
  // dock builds proportionally-sized thumbs from the start (a later resize
  // call would correct the sizing too, but this avoids a flash of small
  // thumbs at first paint).
  const int thumbSize = currentGalleryThumbSize();
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  for (const GalleryEntry &entry : entries) {
    QPixmap pixmap;
    if (entry.isVideo) {
      // Prefer a cached extracted frame; otherwise show a placeholder and
      // request async extraction. Cached *null* pixmaps mean a prior
      // extraction failed — keep the placeholder rather than thrashing.
      auto *extractor = VideoThumbnailExtractor::instance();
      if (extractor->hasCacheEntry(entry.path)) {
        pixmap = extractor->cached(entry.path);
      }
      if (pixmap.isNull()) {
        pixmap = makeVideoPlaceholder(iconSize);
      }
    } else {
      pixmap = QPixmap(entry.path);
      if (pixmap.isNull()) {
        // Skip rows whose file vanished between load and render. Don't add
        // a broken-image placeholder — the user gets a tighter gallery and
        // can still launch the file from the main viewport.
        continue;
      }
    }

    auto *button = new QToolButton(m_galleryContainer);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(thumbSize, thumbSize);
    button->setIconSize(QSize(iconSize, iconSize));
    button->setIcon(QIcon(pixmap));
    button->setToolTip(entry.label);
    button->setAccessibleName(entry.label);

    // Capture entry by value so the click handler keeps working after the
    // caller's list goes out of scope.
    const GalleryEntry capturedEntry = entry;
    connect(button, &QToolButton::clicked, this,
            [this, capturedEntry]() { openGalleryPreview(capturedEntry); });

    if (entry.isVideo) {
      // Request async frame extraction. Receiver is the button itself so
      // the connection auto-disconnects when the button is destroyed on
      // the next gallery rebuild — no manual cleanup needed.
      const QString videoPath = entry.path;
      const int targetIconSize = iconSize;
      connect(VideoThumbnailExtractor::instance(), &VideoThumbnailExtractor::frameReady, button,
              [button, videoPath, targetIconSize](const QString &p, const QPixmap &pix) {
                if (p != videoPath || pix.isNull()) {
                  return;
                }
                const QPixmap scaled = pix.scaled(targetIconSize, targetIconSize,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                button->setIcon(QIcon(scaled));
              });
      VideoThumbnailExtractor::instance()->requestFrame(videoPath);
    }

    m_galleryLayout->addWidget(button);
  }

  // If every entry failed to load we end up with an empty gallery. When
  // the user can edit links we keep the section visible (so the affordance
  // remains reachable); otherwise hide it to avoid leaving a bare title.
  if (m_galleryLayout->count() == 0) {
    if (m_galleryThumbsHost) {
      m_galleryThumbsHost->hide();
    }
    if (m_galleryEditEnabled) {
      m_galleryContainer->show();
    } else {
      m_galleryContainer->hide();
    }
    return;
  }
  if (m_galleryThumbsHost) {
    m_galleryThumbsHost->show();
  }
  m_galleryContainer->show();
  // Kartend-u2gx: keep the dedicated horizontal view's gallery in sync.
  rebuildHorizontalGallery();
}

void DetailsPane::setArtworkEditEnabled(bool enabled) {
  m_galleryEditEnabled = enabled;
  if (enabled) {
    ensureGallerySection();
  }
  if (m_galleryEditButton) {
    m_galleryEditButton->setVisible(enabled);
  }
  // When the section was previously hidden (e.g. an item with no artwork
  // and edit disabled), turning edit back on should reveal at least the
  // title + button so the user has somewhere to click. Conversely, when we
  // disable editing on a section that has no thumbnails, hide it again.
  if (m_galleryContainer) {
    const bool hasThumbs = m_galleryLayout && m_galleryLayout->count() > 0;
    if (enabled || hasThumbs) {
      m_galleryContainer->show();
    } else {
      m_galleryContainer->hide();
    }
  }
  // Kartend-u2gx: rebuild the horizontal gallery + sync the trailing Edit
  // button so both track the per-item edit permission in lockstep.
  if (m_hEditButton) {
    m_hEditButton->setVisible(enabled);
  }
  rebuildHorizontalGallery();
}

QPixmap DetailsPane::makeVideoPlaceholder(int iconSize) const {
  // Simple play triangle on a muted-tile background — visible at thumb
  // size without needing a separate image asset. Not themeable beyond
  // palette colors, which is fine for an initial-extraction placeholder.
  if (iconSize <= 0) {
    return {};
  }
  QPixmap pix(iconSize, iconSize);
  pix.fill(palette().color(QPalette::Mid));
  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(palette().color(QPalette::Window));
  painter.setPen(Qt::NoPen);
  const int margin = iconSize / 4;
  QPolygon triangle;
  triangle << QPoint(margin, margin) << QPoint(margin, iconSize - margin)
           << QPoint(iconSize - margin, iconSize / 2);
  painter.drawPolygon(triangle);
  return pix;
}

void DetailsPane::openGalleryPreview(const GalleryEntry &entry) {
  if (entry.path.isEmpty()) {
    return;
  }
  if (!m_galleryOverlay) {
    // Parent to the top-level window so the overlay can cover the full UI
    // (the sidebar itself is a narrow strip). window() may be the sidebar
    // itself in unparented test scenarios; fall back to `this` so the
    // overlay still has a parent.
    QWidget *overlayParent = window();
    if (!overlayParent || overlayParent == this) {
      overlayParent = this;
    }
    m_galleryOverlay = new ArtworkPreviewOverlay(overlayParent);
    // Kartend-63e bug #7: forward overlay visibility so DetailsPaneManager can
    // lower the sidebar while the overlay is showing.
    connect(m_galleryOverlay, &ArtworkPreviewOverlay::visibilityChanged, this,
            &DetailsPane::galleryOverlayVisibilityChanged);
  }
  if (entry.isVideo) {
    m_galleryOverlay->showVideoAtPath(entry.path);
  } else {
    m_galleryOverlay->showArtworkAtPath(entry.path);
  }
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