// Sibling translation unit for DetailsPane (media gallery section).
// Hosts the per-item gallery widget that shows multi-thumbnail artwork
// (legacy primary + per-type subdirectories + video stills) below the
// artwork preview pane: section construction/teardown, populate from a
// QList<GalleryEntry>, edit-affordance toggle, video placeholder
// rendering, and click-to-preview wiring. Member access only — these
// remain DetailsPane methods, just split out to keep detailspane.cpp
// scannable.

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

#include "artworkpreviewoverlay.h"
#include "detailspane.h"
#include "ui_detailspane.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"

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
  // The button is short ("Edit") with a tooltip carrying
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
  // Edit button stays directly to the right of the title with
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
  // cache the raw entries so the dedicated horizontal view
  // can render its own gallery from the same list (with video filtered out
  // since the inline preview already plays it).
  m_galleryEntries = entries;
  // Synthesize a thumb for the primary artwork (legacy
  // "{artworkDirectory}/{baseName}.{ext}" layout) whenever loadArtwork
  // resolved one. Inserted right after any leading video entry so the video
  // stays first; deduped by path so a typed-subdirectory thumb pointing at
  // the same file isn't doubled up.
  if (!m_primaryArtworkPath.isEmpty()) {
    bool alreadyPresent = false;
    for (const GalleryEntry &e : m_galleryEntries) {
      if (e.path == m_primaryArtworkPath) {
        alreadyPresent = true;
        break;
      }
    }
    if (!alreadyPresent) {
      const int insertAt =
          (!m_galleryEntries.isEmpty() && m_galleryEntries.first().isVideo) ? 1 : 0;
      m_galleryEntries.insert(insertAt, {tr("Artwork"), m_primaryArtworkPath, /*isVideo=*/false});
    }
  }
  if (m_galleryEntries.isEmpty()) {
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
      if (m_galleryEditEnabled && m_activeTab == DetailsPaneTab::Item) {
        m_galleryContainer->show();
      } else {
        m_galleryContainer->hide();
      }
    }
    // also clear the horizontal gallery so a previous
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

  // pick the thumb size based on dock orientation so a horizontal
  // dock builds proportionally-sized thumbs from the start (a later resize
  // call would correct the sizing too, but this avoids a flash of small
  // thumbs at first paint).
  const int thumbSize = currentGalleryThumbSize();
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  for (const GalleryEntry &entry : m_galleryEntries) {
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
    if (m_galleryEditEnabled && m_activeTab == DetailsPaneTab::Item) {
      m_galleryContainer->show();
    } else {
      m_galleryContainer->hide();
    }
    return;
  }
  if (m_galleryThumbsHost) {
    m_galleryThumbsHost->show();
  }
  // Gallery is item-only chrome — hide it on File / Collection regardless
  // of contents. Vertical view's container is reset in applyTabVisibility,
  // but a fresh setArtworkGallery() call could re-show it without this.
  if (m_activeTab == DetailsPaneTab::Item) {
    m_galleryContainer->show();
  } else {
    m_galleryContainer->hide();
  }
  // keep the dedicated horizontal view's gallery in sync.
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
    if ((enabled || hasThumbs) && m_activeTab == DetailsPaneTab::Item) {
      m_galleryContainer->show();
    } else {
      m_galleryContainer->hide();
    }
  }
  // rebuild the horizontal gallery + sync the trailing Edit
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
    // bug #7: forward overlay visibility so DetailsPaneManager can
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
