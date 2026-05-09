// Vertical-dock media gallery row that hangs below the artwork preview
// pane. Owns the section-row widgets, the thumb buttons, and the
// click-to-preview overlay. Horizontal-dock gallery lives in its own
// rebuild path (detailspanehorizontal.cpp) and only borrows entries() +
// makeVideoPlaceholder() from this helper.
#include "detailspanegalleryview.h"

#include "artworkpreviewoverlay.h"
#include "detailspane.h"
#include "ui_detailspane.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

DetailsPaneGalleryView::DetailsPaneGalleryView(QObject *parent) : QObject(parent) {}

void DetailsPaneGalleryView::setHost(DetailsPane *host) {
  m_host = host;
}

void DetailsPaneGalleryView::ensureSection() {
  if (m_container || !m_host) {
    return;
  }
  Ui::DetailsPane *ui = m_host->ui;
  auto *contentLayout = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  m_container = new QWidget(ui->contentWidget);
  auto *outer = new QVBoxLayout(m_container);
  outer->setContentsMargins(0, UIConstants::Metadata::LABEL_SPACING, 0,
                            UIConstants::Metadata::LABEL_SPACING);
  outer->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  // Title row pairs the section heading with the per-item edit affordance.
  // The button is short ("Edit") with a tooltip carrying the longer
  // description so a narrow / zoomed sidebar doesn't clip the label.
  auto *titleRow = new QHBoxLayout();
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  auto *title = new QLabel(tr("Media gallery"), m_container);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  title->setFont(titleFont);
  title->setStyleSheet("color: palette(windowtext); padding: 2px 0px;");
  titleRow->addWidget(title);

  m_editButton = new QPushButton(tr("Edit"), m_container);
  m_editButton->setCursor(Qt::PointingHandCursor);
  m_editButton->setToolTip(
      tr("Pick override files for any artwork type (standard or custom)."));
  m_editButton->setVisible(m_editEnabled);
  connect(m_editButton, &QPushButton::clicked, this, &DetailsPaneGalleryView::editRequested);
  titleRow->addWidget(m_editButton);
  // Trailing stretch so any slack in the row goes to the right of the Edit
  // button rather than between title and button.
  titleRow->addStretch(1);
  outer->addLayout(titleRow);

  // Horizontal layout wrapped in a plain widget; if more than ~4 thumbs are
  // present they wrap by virtue of QLayout's setAlignment.
  m_thumbsHost = new QWidget(m_container);
  m_thumbLayout = new QHBoxLayout(m_thumbsHost);
  m_thumbLayout->setContentsMargins(0, 0, 0, 0);
  m_thumbLayout->setSpacing(UIConstants::Metadata::GALLERY_THUMB_SPACING);
  m_thumbLayout->setAlignment(Qt::AlignLeft);
  outer->addWidget(m_thumbsHost);

  // Insert just below the artwork preview pane (and the dynamically-added
  // video preview, if any) so all visual artwork stays clustered. Falling
  // back to "append" keeps the section visible if the layout shape changes.
  int insertIndex = -1;
  if (auto *artworkParentLayout =
          qobject_cast<QVBoxLayout *>(ui->artworkDisplay->parentWidget()->layout())) {
    if (artworkParentLayout == contentLayout) {
      const int videoIdx =
          m_host->m_videoPreview ? contentLayout->indexOf(m_host->m_videoPreview) : -1;
      const int artIdx = contentLayout->indexOf(ui->artworkDisplay);
      const int anchor = videoIdx >= 0 ? videoIdx : artIdx;
      if (anchor >= 0) {
        insertIndex = anchor + 1;
      }
    }
  }
  if (insertIndex >= 0) {
    contentLayout->insertWidget(insertIndex, m_container);
  } else {
    contentLayout->addWidget(m_container);
  }
  m_container->hide();
}

void DetailsPaneGalleryView::clearThumbs() {
  if (!m_thumbLayout) {
    return;
  }
  while (QLayoutItem *child = m_thumbLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
}

void DetailsPaneGalleryView::applyVisibility(DetailsPaneTab activeTab) {
  if (!m_container) {
    return;
  }
  const bool hasThumbs = m_thumbLayout && m_thumbLayout->count() > 0;
  if (m_thumbsHost) {
    m_thumbsHost->setVisible(hasThumbs);
  }
  // Gallery is item-only chrome — hide it on File / Collection regardless
  // of contents. Keep the section visible when the user can still edit
  // links so the "Edit" button stays available for items with no current
  // artwork — that's the exact case where adding a manual link matters.
  if (activeTab != DetailsPaneTab::Item) {
    m_container->hide();
    return;
  }
  if (hasThumbs || m_editEnabled) {
    m_container->show();
  } else {
    m_container->hide();
  }
}

void DetailsPaneGalleryView::rebuildThumbs(DetailsPaneTab activeTab) {
  if (!m_container || !m_thumbLayout) {
    return;
  }
  clearThumbs();

  const int thumbSize = UIConstants::Metadata::GALLERY_THUMB_SIZE;
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  for (const DetailsPane::GalleryEntry &entry : m_entries) {
    QPixmap pixmap;
    if (entry.isVideo) {
      // Prefer a cached extracted frame; otherwise show a placeholder and
      // request async extraction. Cached null pixmaps mean a prior
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

    auto *button = new QToolButton(m_container);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(thumbSize, thumbSize);
    button->setIconSize(QSize(iconSize, iconSize));
    button->setIcon(QIcon(pixmap));
    button->setToolTip(entry.label);
    button->setAccessibleName(entry.label);

    // Capture entry by value so the click handler keeps working after the
    // caller's list goes out of scope.
    const DetailsPane::GalleryEntry capturedEntry = entry;
    connect(button, &QToolButton::clicked, this,
            [this, capturedEntry]() { openPreview(capturedEntry); });

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

    m_thumbLayout->addWidget(button);
  }

  applyVisibility(activeTab);
}

void DetailsPaneGalleryView::setEntries(const QList<DetailsPane::GalleryEntry> &entries,
                                        const QString &primaryArtworkPath,
                                        DetailsPaneTab activeTab) {
  m_entries = entries;
  // Synthesize a thumb for the primary artwork (legacy
  // "{artworkDirectory}/{baseName}.{ext}" layout) whenever loadArtwork
  // resolved one. Inserted right after any leading video entry so the
  // video stays first; deduped by path so a typed-subdirectory thumb
  // pointing at the same file isn't doubled up.
  if (!primaryArtworkPath.isEmpty()) {
    bool alreadyPresent = false;
    for (const DetailsPane::GalleryEntry &e : m_entries) {
      if (e.path == primaryArtworkPath) {
        alreadyPresent = true;
        break;
      }
    }
    if (!alreadyPresent) {
      const int insertAt =
          (!m_entries.isEmpty() && m_entries.first().isVideo) ? 1 : 0;
      m_entries.insert(insertAt, {tr("Artwork"), primaryArtworkPath, /*isVideo=*/false});
    }
  }
  if (m_entries.isEmpty()) {
    if (m_container) {
      clearThumbs();
      applyVisibility(activeTab);
    }
    return;
  }
  ensureSection();
  rebuildThumbs(activeTab);
}

void DetailsPaneGalleryView::setEditEnabled(bool enabled, DetailsPaneTab activeTab) {
  m_editEnabled = enabled;
  if (enabled) {
    ensureSection();
  }
  if (m_editButton) {
    m_editButton->setVisible(enabled);
  }
  applyVisibility(activeTab);
}

void DetailsPaneGalleryView::applyTabState(DetailsPaneTab activeTab) {
  applyVisibility(activeTab);
}

void DetailsPaneGalleryView::hideSection() {
  if (m_container) {
    m_container->hide();
  }
}

void DetailsPaneGalleryView::applyThumbSize(int thumbSize) {
  if (!m_thumbLayout) {
    return;
  }
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  for (int i = 0; i < m_thumbLayout->count(); ++i) {
    if (auto *btn = qobject_cast<QToolButton *>(m_thumbLayout->itemAt(i)->widget())) {
      btn->setFixedSize(thumbSize, thumbSize);
      btn->setIconSize(QSize(iconSize, iconSize));
    }
  }
}

QPixmap DetailsPaneGalleryView::makeVideoPlaceholder(int iconSize) const {
  // Simple play triangle on a muted-tile background — visible at thumb
  // size without needing a separate image asset.
  if (iconSize <= 0 || !m_host) {
    return {};
  }
  QPixmap pix(iconSize, iconSize);
  pix.fill(m_host->palette().color(QPalette::Mid));
  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(m_host->palette().color(QPalette::Window));
  painter.setPen(Qt::NoPen);
  const int margin = iconSize / 4;
  QPolygon triangle;
  triangle << QPoint(margin, margin) << QPoint(margin, iconSize - margin)
           << QPoint(iconSize - margin, iconSize / 2);
  painter.drawPolygon(triangle);
  return pix;
}

void DetailsPaneGalleryView::openPreview(const DetailsPane::GalleryEntry &entry) {
  if (entry.path.isEmpty() || !m_host) {
    return;
  }
  if (!m_overlay) {
    // Parent to the top-level window so the overlay can cover the full UI
    // (the sidebar itself is a narrow strip). window() may be the sidebar
    // itself in unparented test scenarios; fall back to the host so the
    // overlay still has a parent.
    QWidget *overlayParent = m_host->window();
    if (!overlayParent || overlayParent == m_host) {
      overlayParent = m_host;
    }
    m_overlay = new ArtworkPreviewOverlay(overlayParent);
    connect(m_overlay, &ArtworkPreviewOverlay::visibilityChanged, this,
            &DetailsPaneGalleryView::overlayVisibilityChanged);
  }
  if (entry.isVideo) {
    m_overlay->showVideoAtPath(entry.path);
  } else {
    m_overlay->showArtworkAtPath(entry.path);
  }
}
