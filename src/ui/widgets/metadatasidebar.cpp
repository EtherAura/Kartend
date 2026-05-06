// Displays file metadata, artwork preview, and item details in the sidebar
// panel.
#include <algorithm>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
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
#include "metadatasidebar.h"
#include "pathutils.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"
#include "videoutils.h"

#include <QPointer>
#include <QPolygon>

// Creates metadata sidebar with scrollable layout for displaying item
// information and artwork
MetadataSidebar::MetadataSidebar(QWidget *parent) : QWidget(parent), ui(new Ui::MetadataSidebar) {
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

  setFixedWidth(UIConstants::Sidebar::FIXED_WIDTH);

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
  m_videoStartTimer->setInterval(UIConstants::Sidebar::VIDEO_PREVIEW_DEBOUNCE_MS);
  connect(m_videoStartTimer, &QTimer::timeout, this, [this]() {
    if (m_pendingVideoPath.isEmpty() || !m_videoPreview) {
      return;
    }
    ui->artworkDisplay->hide();
    m_videoPreview->show();
    m_videoPreview->playVideo(m_pendingVideoPath);
  });

  clearMetadata();
}

MetadataSidebar::~MetadataSidebar() {
  delete ui;
}

// Sets metadata fields and loads centered artwork from the configured
// artwork directory or a sibling "artwork" directory if present
void MetadataSidebar::setMetadata(const QString &filePath, const QString &itemName,
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
  if (m_activeTab != SidebarTab::Item) {
    return;
  }
  applyItemViewVisibility(true);
  ui->titleLabel->setText(tr("Item Information"));
  ui->itemNameLabel->setText(tr("Name:"));

  ui->itemNameValue->setText(itemName);
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
}

// Clears all metadata fields. When a collection summary has been cached
// (Kartend-3mn) the sidebar renders that instead of the legacy "No item
// selected" placeholder; this lets every existing clearMetadata() call site
// pick up the new no-selection display without per-caller plumbing.
void MetadataSidebar::clearMetadata() {
  m_hasItemDisplayed = false;

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
}

void MetadataSidebar::setCollectionSummary(const CollectionSummary &summary) {
  m_collectionSummary = summary;
  // Re-render whenever the sidebar is currently in the no-selection state
  // so first-time application (during applySidebarStateForCollection) and
  // background refreshes (scan completion, settings save) both land. While
  // an item is selected we just update the cache and apply on next clear.
  if (!m_hasItemDisplayed) {
    clearMetadata();
  }
}

void MetadataSidebar::applyItemViewVisibility(bool visible) {
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

void MetadataSidebar::renderCollectionSummary() {
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

QString MetadataSidebar::formatLastScanned(const QDateTime &lastScanned) {
  if (!lastScanned.isValid()) {
    return tr("never");
  }
  return lastScanned.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

// Updates file information fields including size, modification date, and file
// type
void MetadataSidebar::updateFileInfo(const QString &filePath) {
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

void MetadataSidebar::setupTabBar() {
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
    SidebarTab newTab = SidebarTab::Item;
    if (index == static_cast<int>(SidebarTab::Collection))
      newTab = SidebarTab::Collection;
    else if (index == static_cast<int>(SidebarTab::File))
      newTab = SidebarTab::File;
    if (newTab == m_activeTab) {
      return;
    }
    m_activeTab = newTab;
    applyTabVisibility();
    emit activeTabChanged(newTab);
  });
}

void MetadataSidebar::setActiveTab(SidebarTab tab) {
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

void MetadataSidebar::applyTabVisibility() {
  // Lazy-build the File-tab placeholder on first switch so we don't pay for
  // it when the user never visits that tab.
  if (m_activeTab == SidebarTab::File && !m_filePlaceholder && ui->contentWidget) {
    if (auto *cl = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout())) {
      m_filePlaceholder = new QLabel(tr("No custom view configured."), ui->contentWidget);
      m_filePlaceholder->setAlignment(Qt::AlignCenter);
      m_filePlaceholder->setWordWrap(true);
      m_filePlaceholder->setStyleSheet("color: palette(mid); padding: 32px 16px;");
      cl->addWidget(m_filePlaceholder);
    }
  }

  switch (m_activeTab) {
  case SidebarTab::Item:
    if (m_filePlaceholder) m_filePlaceholder->hide();
    if (m_hasItemDisplayed) {
      applyItemViewVisibility(true);
    } else {
      // Falls back to the collection summary when no item is selected, the
      // existing behavior the rest of the codebase already exercises.
      renderCollectionSummary();
    }
    break;
  case SidebarTab::Collection:
    if (m_filePlaceholder) m_filePlaceholder->hide();
    renderCollectionSummary();
    break;
  case SidebarTab::File:
    applyItemViewVisibility(false);
    if (m_filePlaceholder) m_filePlaceholder->show();
    // Hide the title + collection summary chrome too so the placeholder
    // sits alone.
    ui->titleLabel->setVisible(false);
    ui->itemNameLabel->setVisible(false);
    ui->itemNameValue->setVisible(false);
    break;
  }

  if (m_activeTab != SidebarTab::File) {
    ui->titleLabel->setVisible(true);
    ui->itemNameLabel->setVisible(true);
    ui->itemNameValue->setVisible(true);
  }
}

void MetadataSidebar::applyAppearance(const CollectionConfig &collection) {
  setActiveTab(collection.sidebarActiveTab);
  m_widthLocked = collection.sidebarWidthLocked;
  m_position = collection.sidebarPosition;
  // Mouse tracking is required so we can change the cursor over the grip
  // strip without waiting for a click.
  setMouseTracking(!m_widthLocked);
  if (m_widthLocked) {
    unsetCursor();
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
  // original .ui look without us walking metadatasidebar.ui at runtime.
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

bool MetadataSidebar::isOnGrip(const QPoint &posInWidget) const {
  if (m_widthLocked) {
    return false;
  }
  // 6px-wide hot zone on the inner edge so the user has a forgiving target.
  static constexpr int GRIP_WIDTH = 6;
  if (m_position == SidebarPosition::Left) {
    return posInWidget.x() >= width() - GRIP_WIDTH;
  }
  return posInWidget.x() < GRIP_WIDTH;
}

void MetadataSidebar::mousePressEvent(QMouseEvent *event) {
  if (!m_widthLocked && event->button() == Qt::LeftButton && isOnGrip(event->pos())) {
    m_widthDragging = true;
    m_dragStartWidth = width();
    m_dragStartX = event->globalPosition().toPoint().x();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void MetadataSidebar::mouseMoveEvent(QMouseEvent *event) {
  if (m_widthDragging) {
    const int dx = event->globalPosition().toPoint().x() - m_dragStartX;
    // Right-anchored sidebar: drag-left increases width, drag-right shrinks.
    // Left-anchored: drag-right increases width.
    int candidate =
        m_position == SidebarPosition::Left ? m_dragStartWidth + dx : m_dragStartWidth - dx;
    candidate =
        std::clamp(candidate, UIConstants::Sidebar::MIN_WIDTH, UIConstants::Sidebar::MAX_WIDTH);
    emit widthDragged(candidate);
    event->accept();
    return;
  }
  if (!m_widthLocked) {
    setCursor(isOnGrip(event->pos()) ? Qt::SplitHCursor : Qt::ArrowCursor);
  }
  QWidget::mouseMoveEvent(event);
}

void MetadataSidebar::mouseReleaseEvent(QMouseEvent *event) {
  if (m_widthDragging && event->button() == Qt::LeftButton) {
    m_widthDragging = false;
    emit widthCommitted(width());
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void MetadataSidebar::leaveEvent(QEvent *event) {
  if (!m_widthDragging) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

void MetadataSidebar::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  const QColor fallbackBase = palette().color(QPalette::Window);
  const QColor baseColor = m_bgColor.isValid() ? m_bgColor : fallbackBase;

  switch (m_bgType) {
  case SidebarBackgroundType::Color:
    painter.fillRect(rect(), baseColor);
    break;
  case SidebarBackgroundType::Image:
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
  case SidebarBackgroundType::Pattern: {
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

  // Kartend-63e: paint a visible 1px guideline on the inner edge when the
  // width is unlocked so the user can see where to grab. Skipped while
  // dragging so the line doesn't smear during the live resize.
  if (!m_widthLocked && !m_widthDragging) {
    QColor edgeColor = palette().color(QPalette::Highlight);
    edgeColor.setAlpha(160);
    painter.setPen(QPen(edgeColor, 1));
    if (m_position == SidebarPosition::Left) {
      painter.drawLine(width() - 1, 0, width() - 1, height());
    } else {
      painter.drawLine(0, 0, 0, height());
    }
  }

  QWidget::paintEvent(event);
}

void MetadataSidebar::captureLabelFontBaselines() {
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

int MetadataSidebar::previewBoxSize() const {
  if (!ui) {
    return UIConstants::Metadata::ARTWORK_SIZE;
  }
  // Prefer the scroll area's viewport width — the contentWidget tracks it
  // via widgetResizable=true, but during the first resize pass the viewport
  // is the authoritative source. Subtract 28px (10 + 10 layout margins +
  // 8px slack) so the preview doesn't render flush against the sidebar
  // edge. Floor at a usable minimum so a freshly-constructed sidebar
  // (width=0 before show) doesn't render a collapsed frame.
  int viewportW =
      (ui->scrollArea && ui->scrollArea->viewport()) ? ui->scrollArea->viewport()->width() : 0;
  if (viewportW <= 0 && ui->contentWidget) {
    viewportW = ui->contentWidget->width();
  }
  return qMax(80, viewportW - 28);
}

void MetadataSidebar::applyPreviewSize() {
  const int size = previewBoxSize();
  if (ui && ui->artworkDisplay) {
    ui->artworkDisplay->setFixedSize(size, size);
  }
  if (m_videoPreview) {
    m_videoPreview->setFixedSize(size, size);
  }
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

void MetadataSidebar::applyContentAlignment() {
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

void MetadataSidebar::applySidebarFont(const QString &family, int pointSize) {
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

void MetadataSidebar::applyBubbleStyles(const QString &headerHex, const QString &sectionHex) {
  if (!ui->contentWidget) {
    return;
  }
  // Build a stylesheet that selects the existing static-label objectNames in
  // metadatasidebar.ui. Dynamic detail rows are tagged via objectName at
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

void MetadataSidebar::pausePreviewVideo() {
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

bool MetadataSidebar::togglePreviewVideoPause() {
  if (!m_videoPreview || !m_videoPreview->hasLoadedSource()) {
    return false;
  }
  m_videoPreview->togglePauseResume();
  return true;
}

void MetadataSidebar::updateFilePathDisplay() {
  if (m_currentFilePath.isEmpty()) {
    return;
  }
  // Kartend-xcfr: set the full path; QLabel wordWrap=true (set in
  // updateFileInfo) handles per-character wrapping so the entire path is
  // visible without truncation.
  ui->filePathValue->setText(m_currentFilePath);
}

void MetadataSidebar::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateFilePathDisplay();
  // Kartend-xcfr: track the artwork + video preview to the sidebar's current
  // content width so a width-drag (or initial show) reflows the previews
  // instead of leaving them pinned at the .ui's 200px design size.
  applyPreviewSize();
}

// Formats file size into human-readable string with appropriate units (KB, MB,
// GB)
auto MetadataSidebar::formatFileSize(qint64 bytes) -> QString {
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
void MetadataSidebar::loadArtwork(const QString &baseName, const QString &artworkDirectory) {
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
        return; // Found and loaded artwork
      }
    }
  }
}

// Apply horzontal scrolling policy
void MetadataSidebar::setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy) {
  if (ui->scrollArea) {
    ui->scrollArea->setHorizontalScrollBarPolicy(policy);
    ui->scrollArea->updateGeometry();
    QApplication::processEvents();
  }
}

// Stops any current preview video and shows the static artwork display
// instead. Called whenever selection changes (before the debounce timer
// resolves) and when metadata is cleared.
void MetadataSidebar::showArtworkOnly() {
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }
  ui->artworkDisplay->show();
}

// Schedule preview video playback after the debounce interval. Passing an
// empty path cancels any pending playback.
void MetadataSidebar::schedulePreviewVideo(const QString &videoPath) {
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
void MetadataSidebar::ensureDetailsSection() {
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
  connect(m_manualButton, &QPushButton::clicked, this, &MetadataSidebar::openCurrentManual);
  outer->addWidget(m_manualButton);

  m_detailsLayout = new QVBoxLayout();
  m_detailsLayout->setSpacing(UIConstants::Metadata::LABEL_SPACING);
  outer->addLayout(m_detailsLayout);

  contentLayout->addWidget(m_detailsContainer);
  m_detailsContainer->hide();
}

void MetadataSidebar::clearDetailsSection() {
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

void MetadataSidebar::appendDetailRow(const QString &label, const QString &value, bool wrap) {
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

void MetadataSidebar::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
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

void MetadataSidebar::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
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

QString MetadataSidebar::formatRuntime(int seconds) {
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

void MetadataSidebar::ensureManualButton() {
  // Manual button lives inside the Details container's outer layout. Build
  // the container on-demand so an item with only a manual (no extended
  // metadata) still gets a visible button.
  ensureDetailsSection();
}

void MetadataSidebar::setManualFile(const QString &manualPath) {
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

void MetadataSidebar::openCurrentManual() {
  if (m_manualPath.isEmpty()) {
    return;
  }
  // QDesktopServices::openUrl wraps xdg-open on Linux / open on macOS /
  // ShellExecute on Windows, so the user's default handler for the file
  // type takes over (Okular for PDF, web browser for HTML, etc.).
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_manualPath));
}

void MetadataSidebar::ensureGallerySection() {
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
  titleRow->addStretch(1);

  m_galleryEditButton = new QPushButton(tr("Edit"), m_galleryContainer);
  m_galleryEditButton->setCursor(Qt::PointingHandCursor);
  m_galleryEditButton->setToolTip(
      tr("Pick override files for any artwork type (standard or custom)."));
  m_galleryEditButton->setVisible(m_galleryEditEnabled);
  connect(m_galleryEditButton, &QPushButton::clicked, this, &MetadataSidebar::editArtworkRequested);
  titleRow->addWidget(m_galleryEditButton);
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

void MetadataSidebar::clearGallerySection() {
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

void MetadataSidebar::setArtworkGallery(const QList<GalleryEntry> &entries) {
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
    return;
  }

  ensureGallerySection();
  if (!m_galleryContainer || !m_galleryLayout) {
    return;
  }
  clearGallerySection();

  const int thumbSize = UIConstants::Metadata::GALLERY_THUMB_SIZE;
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
}

void MetadataSidebar::setArtworkEditEnabled(bool enabled) {
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
}

QPixmap MetadataSidebar::makeVideoPlaceholder(int iconSize) const {
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

void MetadataSidebar::openGalleryPreview(const GalleryEntry &entry) {
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
    // Kartend-63e bug #7: forward overlay visibility so SidebarManager can
    // lower the sidebar while the overlay is showing.
    connect(m_galleryOverlay, &ArtworkPreviewOverlay::visibilityChanged, this,
            &MetadataSidebar::galleryOverlayVisibilityChanged);
  }
  if (entry.isVideo) {
    m_galleryOverlay->showVideoAtPath(entry.path);
  } else {
    m_galleryOverlay->showArtworkAtPath(entry.path);
  }
}

QString MetadataSidebar::formatTags(const QString &raw) {
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