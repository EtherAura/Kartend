// Sibling translation unit for DetailsPane (horizontal-dock).
// Hosts the dedicated horizontal-view layout: setup, gallery rebuild, and
// the per-tab refresh. Member access only — these remain DetailsPane
// methods, just split out to keep detailspane.cpp scannable.

#include <QFontMetrics>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QtConcurrent/QtConcurrentRun>
#include <QToolButton>
#include <QVBoxLayout>

#include "collectionutils.h"
#include "detailspane.h"
#include "detailspanegalleryview.h"
#include "extensionutils.h"
#include "ui_detailspane.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"

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
  // path inherits the same palette WindowText as the metadata
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
  m_hEditButton->setVisible(m_galleryView && m_galleryView->isEditEnabled());
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

  // The primary "{artworkDirectory}/{baseName}.{ext}" file is synthesized
  // into the gallery's entry list by setArtworkGallery, so it iterates
  // through here alongside the typed-subdirectory thumbs — no special
  // prepend needed, and the click handler below wires it to the preview
  // overlay just like any other entry.
  if (!m_galleryView) {
    if (m_hGalleryHost) {
      m_hGalleryHost->setVisible(false);
    }
    return;
  }
  for (const GalleryEntry &entry : m_galleryView->entries()) {
    if (entry.isVideo) continue; // Live preview tile handles video.
    // Non-image entries (e.g. a `.pdf` manual surfaced via a custom
    // artwork-link row) must NEVER reach QImage(path) below — Qt's
    // libqpdf imageformats plugin is PDFium-backed and abort()s on
    // some inputs, crashing the whole process (Kartend-wquq). Same
    // gate the worker-side artworkloaddispatcher already uses.
    if (!ExtensionUtils::isDecodableImagePath(entry.path)) continue;
    // Path-keyed cache to avoid re-decoding on every selection change.
    // On hit: set the icon synchronously (fast path).
    // On miss: create the button with an empty icon, kick off async
    // decode, populate cache + icon when the worker delivers. Pre-fix
    // this loop synchronously decoded N images on cache miss — the
    // 2495ms galleryUi outlier (Kartend-5ux9) was here, since
    // setArtworkGallery calls rebuildHorizontalGallery on every refresh
    // regardless of dock orientation (so the horizontal view stays
    // warm for fast dock-switching).
    auto *btn = new QToolButton(m_hGalleryHost);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(thumbSize, thumbSize);
    btn->setIconSize(QSize(iconSize, iconSize));
    btn->setToolTip(entry.label);
    btn->setAccessibleName(entry.label);
    const GalleryEntry capturedEntry = entry;
    connect(btn, &QToolButton::clicked, this, [this, capturedEntry]() {
      if (m_galleryView) m_galleryView->openPreview(capturedEntry);
    });

    QPixmap pixmap = m_galleryPixmapCache.value(entry.path);
    if (!pixmap.isNull()) {
      btn->setIcon(QIcon(pixmap));
    } else {
      // Async decode. QPointer<QToolButton> guards against the next
      // rebuildHorizontalGallery deleting the button before this load
      // completes — stale results dropped silently. QPointer<DetailsPane>
      // guards the cache write against pane teardown mid-flight.
      QPointer<QToolButton> btnGuard(btn);
      QPointer<DetailsPane> selfGuard(this);
      const QString path = entry.path;
      auto *watcher = new QFutureWatcher<QImage>(this);
      connect(watcher, &QFutureWatcherBase::finished, this,
              [this, watcher, btnGuard, selfGuard, path]() {
                watcher->deleteLater();
                if (!selfGuard) return;
                const QImage img = watcher->result();
                if (img.isNull()) {
                  if (btnGuard) btnGuard->deleteLater();
                  return;
                }
                const QPixmap pix = QPixmap::fromImage(img);
                // Populate the cache so subsequent rebuilds (dock toggle,
                // selection bounce) hit the synchronous fast path.
                if (m_galleryPixmapCacheOrder.size() >= m_galleryPixmapCacheCap) {
                  const QString oldest = m_galleryPixmapCacheOrder.takeFirst();
                  m_galleryPixmapCache.remove(oldest);
                }
                m_galleryPixmapCache.insert(path, pix);
                m_galleryPixmapCacheOrder.append(path);
                if (btnGuard) btnGuard->setIcon(QIcon(pix));
              });
      watcher->setFuture(QtConcurrent::run([path]() { return QImage(path); }));
    }
    m_hGalleryLayout->addWidget(btn);
    addedThumb = true;
  }
  if (m_hGalleryHost) {
    m_hGalleryHost->setVisible(addedThumb);
  }
}

void DetailsPane::updateHorizontalView() {
  if (!m_horizontalView) return;
  // only run when actually in horizontal dock. If the user
  // toggled to T/B then back to L/R, m_horizontalView still exists (hidden)
  // — and previously this method was overriding m_videoPreview's size to
  // horizontalPreviewSize(), which broke vertical-mode video playback
  // because the widget was sized for the wrong axis.
  if (!CollectionUtils::isDetailsPaneHorizontal(m_position)) return;

  // tab-driven visibility — each tab now owns a distinct payload.
  // Item: video preview, name, gallery (no path / size / modified rows).
  // File: name + path + size / modified / type rows (no preview / gallery).
  // Collection: collection name only (summary text rows belong to the
  //   vertical view's m_detailsContainer; the horizontal strip stays terse).
  const bool isItemTab = (m_activeTab == DetailsPaneTab::Item);
  const bool isFileTab = (m_activeTab == DetailsPaneTab::File);

  // Name. The vertical view's itemNameValue text is already retargeted per
  // tab by applyTabVisibility (raw filename on File tab, metadata title on
  // Item, collection name on Collection) — mirror that here.
  if (m_hName && ui) {
    const QString name = ui->itemNameValue->text();
    m_hName->setText(name.isEmpty() ? tr("(no selection)") : name);
  }
  // Path: only on the File tab now. The Item tab no longer surfaces raw
  // file paths — that lives on the dedicated File view.
  if (m_hPath) {
    if (isFileTab) {
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
  // Size / modified / type rows belong to the File tab.
  if (m_hMetaContainer) m_hMetaContainer->setVisible(isFileTab);
  // Edit button is item-only chrome — gating on the collection's
  // artwork-edit permission only matters when the gallery is visible.
  if (m_hEditButton) {
    const bool editEnabled = m_galleryView && m_galleryView->isEditEnabled();
    m_hEditButton->setVisible(isItemTab && editEnabled);
  }

  // Live video preview tile — Item-tab only.
  const int previewSize = horizontalPreviewSize();
  if (m_videoPreview) {
    m_videoPreview->setFixedSize(previewSize, previewSize);
    const bool hasVideo = !m_pendingVideoPath.isEmpty();
    if (hasVideo && isItemTab) {
      const bool wasHidden = !m_videoPreview->isVisible();
      m_videoPreview->show();
      if (wasHidden) {
        m_videoPreview->playVideo(m_pendingVideoPath);
      }
    } else {
      m_videoPreview->hide();
    }
  }
  if (m_hPreviewArea) {
    const bool hasVideo = m_videoPreview && !m_pendingVideoPath.isEmpty();
    m_hPreviewArea->setVisible(isItemTab && hasVideo);
  }
  // Gallery thumbs are item-only too. Hide the host on other tabs;
  // rebuildHorizontalGallery handles the populated case.
  if (isItemTab) {
    rebuildHorizontalGallery();
  } else if (m_hGalleryHost) {
    m_hGalleryHost->hide();
  }
}
