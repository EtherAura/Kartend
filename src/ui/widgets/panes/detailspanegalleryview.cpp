// Vertical-dock media gallery row that hangs below the artwork preview
// pane. Owns the section-row widgets, the thumb buttons, and the
// click-to-preview overlay. Horizontal-dock gallery lives in its own
// rebuild path (detailspanehorizontal.cpp) and only borrows entries() +
// makeVideoPlaceholder() from this helper.
#include "detailspanegalleryview.h"

#include "artworkpreviewoverlay.h"
#include "detailspane.h"
#include "extensionutils.h"
#include "imagedecodeutils.h"
#include "overlayscrollbars.h"
#include "overlayzorderregistry.h"
#include "uiconstants/icons.h"
#include "uiconstants/metadata.h"
#include "uiconstants/timing.h"
#include "videothumbnailextractor.h"

#include <functional>
#include <memory>

#include <QElapsedTimer>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLoggingCategory>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTimer>

#include "loggingcategories.h"
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QSize>
#include <QtConcurrent/QtConcurrentRun>
#include <QToolButton>
#include <QVBoxLayout>

DetailsPaneGalleryView::DetailsPaneGalleryView(QObject *parent) : QObject(parent) {}

void DetailsPaneGalleryView::setHost(DetailsPane *host) {
  m_host = host;
}

void DetailsPaneGalleryView::setHostAnchors(QWidget *contentWidget, QWidget *artworkAnchor,
                                            QWidget *videoAnchor) {
  m_contentWidget = contentWidget;
  m_artworkAnchor = artworkAnchor;
  m_videoAnchor = videoAnchor;
}

void DetailsPaneGalleryView::setScrollIdlePredicate(std::function<bool()> predicate) {
  m_scrollIdle = std::move(predicate);
}

void DetailsPaneGalleryView::setLayerManager(OverlayZOrderRegistry *manager) {
  m_layerManager = manager;
  // If the overlay already exists, register it now so its next show goes
  // through the coordinated z-order path. Otherwise openPreview()'s lazy
  // construction handles registration.
  if (m_layerManager && m_overlay) {
    m_layerManager->registerOverlay(m_overlay, OverlayZOrderRegistry::Layer::ArtworkPreview);
    m_overlay->setLayerManager(m_layerManager);
  }
}

void DetailsPaneGalleryView::prewarmSection() {
  // Public entry point that just runs the private lazy builder. See header
  // comment for rationale (Kartend-jxp5).
  ensureSection();
}

void DetailsPaneGalleryView::ensureSection() {
  if (m_container || !m_host || !m_contentWidget) {
    return;
  }
  auto *contentLayout = qobject_cast<QVBoxLayout *>(m_contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  // Perf trace (Kartend-jxp5) — measures the lazy widget construction cost.
  // Pre-jxp5 this fired on the user's first sidebar refresh that had gallery
  // entries (~2.5s outlier on the user's library). Post-jxp5 it fires from
  // DetailsPane's constructor via prewarmSection(), so the cost lands in
  // startup where it's invisible. The timer stays in case future changes
  // re-introduce a regression here.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  QElapsedTimer perfWall;
  if (perfTrace) {
    perfWall.start();
  }

  m_container = new QWidget(m_contentWidget);
  // Object name lets applyBubbleStyles paint a backdrop behind the
  // whole gallery section (title row + thumb strip) so it visually
  // matches the metadata card below the description.
  m_container->setObjectName(QStringLiteral("galleryBackdrop"));
  auto *outer = new QVBoxLayout(m_container);
  // Inner padding so the backdrop has breathing room around the title
  // and thumbs. Bottom intentionally flush — the gallery scrollbar
  // already sits at the bottom of its viewport, so any extra padding
  // there reads as a "gap" between the gallery and the description
  // tile below it. The contentLayout spacing supplies the actual
  // inter-section gap.
  outer->setContentsMargins(6, 3, 6, 0);
  outer->setSpacing(2);

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
  // Padding kept in sync with the appendScrollingDescription "Description"
  // label so the two section headers (gallery / description) read as a
  // matched pair — same font weight, same vertical metrics.
  title->setStyleSheet("color: palette(windowtext); padding: 0px;");
  titleRow->addWidget(title);

  // Icon-only edit affordance: a small monochrome folder glyph (Breeze's
  // `folder-symbolic`, with `folder` as fallback). The tooltip carries
  // the longer description that the textual "Edit" label used to. Flat
  // + fixed-size keeps the chrome unobtrusive next to the title.
  m_editButton = new QPushButton(m_container);
  m_editButton->setCursor(Qt::PointingHandCursor);
  m_editButton->setIcon(
      UIConstants::Icons::fromTheme({"folder-symbolic", UIConstants::Icons::FOLDER}));
  m_editButton->setIconSize(QSize(16, 16));
  m_editButton->setFixedSize(20, 20);
  m_editButton->setFlat(true);
  m_editButton->setToolTip(tr("Pick override files for any artwork type (standard or custom)."));
  m_editButton->setVisible(m_editEnabled);
  connect(m_editButton, &QPushButton::clicked, this, &DetailsPaneGalleryView::editRequested);
  titleRow->addWidget(m_editButton);
  // Trailing stretch so any slack in the row goes to the right of the Edit
  // button rather than between title and button.
  titleRow->addStretch(1);
  outer->addLayout(titleRow);

  // Horizontal layout wrapped in its own QScrollArea so a rich scrape
  // (ScreenScraper can return 20+ artwork types for one game) doesn't
  // push the parent contentWidget past the sidebar viewport width.
  // Without this internal scroll, the gallery's HBoxLayout sizeHint
  // would grow the outer contentWidget horizontally (it's resizable),
  // shifting AlignHCenter-positioned widgets like the artwork tile
  // and video preview hundreds of pixels off-screen to the right.
  // Internal scroll keeps the gallery self-contained: it grows
  // horizontally inside itself with its own scrollbar / drag-scroll,
  // and the sidebar's contentWidget stays at viewport width.
  auto *thumbScroll = new QScrollArea(m_container);
  thumbScroll->setWidgetResizable(true);
  thumbScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  thumbScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  thumbScroll->setFrameShape(QFrame::NoFrame);
  // Built lazily, so it misses the sweep applyAppearance ran at the last
  // collection switch — take the pane's standing intent now, or this one
  // strip keeps a scrollbar in a pane the user hid.
  OverlayScrollbars::setScrollbarMode(thumbScroll,
                                      m_host ? m_host->scrollbarMode() : ScrollbarMode::Show);
  thumbScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  // Fix the height to one thumb tile + a few px slack for the
  // horizontal scrollbar; otherwise the QScrollArea wants to grow
  // vertically to fit its viewport.
  thumbScroll->setFixedHeight(UIConstants::Metadata::GALLERY_THUMB_SIZE + 10);
  // Strip the QScrollArea's own background so the parent's sidebar
  // bg shows through — otherwise the QScrollArea defaults to
  // palette(base) which renders as a dark rectangle behind the
  // slim scrollbar track.
  thumbScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
  thumbScroll->viewport()->setAutoFillBackground(false);
  // Slim horizontal scrollbar so the gallery row stays compact. Targets
  // only this scroll area's bar (objectName ancestor) so app-wide
  // scrollbars are untouched. ~6px tall track, no arrow buttons.
  // Handle uses `palette(button)` so it matches the standard scrollbar
  // face across themes; the track uses palette(window) so the strip
  // behind the handle reads as the same color as the surrounding
  // sidebar instead of the QScrollArea's default dark base.
  thumbScroll->horizontalScrollBar()->setStyleSheet(
      "QScrollBar:horizontal { height: 6px; background: palette(window); margin: 0; }"
      "QScrollBar::handle:horizontal { background: palette(button); border-radius: 3px;"
      " min-width: 24px; }"
      "QScrollBar::handle:horizontal:hover { background: palette(highlight); }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; height: 0; }"
      "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: "
      "palette(window); }");
  m_thumbsHost = new QWidget;
  m_thumbLayout = new QHBoxLayout(m_thumbsHost);
  m_thumbLayout->setContentsMargins(0, 0, 0, 0);
  m_thumbLayout->setSpacing(UIConstants::Metadata::GALLERY_THUMB_SPACING);
  m_thumbLayout->setAlignment(Qt::AlignLeft);
  thumbScroll->setWidget(m_thumbsHost);
  outer->addWidget(thumbScroll);

  // Insert just below the artwork preview pane (and the dynamically-added
  // video preview, if any) so all visual artwork stays clustered. Falling
  // back to "append" keeps the section visible if the layout shape changes.
  int insertIndex = -1;
  if (m_artworkAnchor && m_artworkAnchor->parentWidget()) {
    if (auto *artworkParentLayout =
            qobject_cast<QVBoxLayout *>(m_artworkAnchor->parentWidget()->layout())) {
      if (artworkParentLayout == contentLayout) {
        const int videoIdx = m_videoAnchor ? contentLayout->indexOf(m_videoAnchor) : -1;
        const int artIdx = contentLayout->indexOf(m_artworkAnchor);
        const int anchor = videoIdx >= 0 ? videoIdx : artIdx;
        if (anchor >= 0) {
          insertIndex = anchor + 1;
        }
      }
    }
  }
  if (insertIndex >= 0) {
    contentLayout->insertWidget(insertIndex, m_container);
  } else {
    contentLayout->addWidget(m_container);
  }
  m_container->hide();

  if (perfTrace) {
    qCDebug(lcPerfTrace).nospace()
        << "DetailsPaneGalleryView::ensureSection: builtMs=" << perfWall.elapsed();
  }
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
  // The File tab stays gallery-free (pure filesystem view). Item AND
  // Collection tabs show it: collection surfaces feed scraped system
  // images through the same strip since Kartend-5b5r1 (user decision
  // 2026-08-23). Keep the section visible when the user can still edit
  // links so the "Edit" button stays available for items with no current
  // artwork — that's the exact case where adding a manual link matters.
  if (activeTab == DetailsPaneTab::File) {
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
  // Per-phase perf timers (Kartend-jxp5 follow-up). The galleryUi outer
  // timer in performSidebarMetadataUpdate showed a ~3.4s outlier on the
  // FIRST gallery population — even after eager ensureSection (which
  // landed at ~0ms). The cost has to be in clearThumbs / button-creation /
  // applyVisibility's show() cascade. Each timer below logs separately
  // when total >5ms so we can pinpoint the actual hotspot.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  qint64 perfClearMs = 0, perfLoopMs = 0, perfVisMs = 0;
  QElapsedTimer perfWall;
  if (perfTrace) perfWall.start();
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    clearThumbs();
    if (perfTrace) perfClearMs = t.elapsed();
  }

  const int thumbSize = UIConstants::Metadata::GALLERY_THUMB_SIZE;
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  // Inner-loop perf accumulators (Kartend-jxp5 round 3 chase). Round 2
  // showed loop=2248 with entries=2 — ~1.1s per iteration. Need to
  // identify which specific call inside is slow on first use; suspects
  // are QToolButton construction (style polish?), the async-dispatch
  // path (QtConcurrent::run cold-start?), or m_thumbLayout->addWidget
  // (layout reflow?).
  qint64 perfBtnCtorMs = 0, perfBtnCfgMs = 0, perfDispatchMs = 0, perfAddMs = 0;
  QElapsedTimer perfLoopTimer;
  if (perfTrace) perfLoopTimer.start();
  for (const DetailsPane::GalleryEntry &entry : m_entries) {
    // Gallery entries can include non-image media surfaced through the
    // custom-artwork-link path (e.g. a `.pdf` manual stored under a
    // user-defined type). QImage(path) on the decode worker below
    // routes a `.pdf` to Qt's libqpdf imageformats plugin, which is
    // PDFium-backed and abort()s — taking down the whole process
    // (Kartend-wquq). Skip the tile entirely so a non-image entry
    // never reaches QImage. Videos take a separate branch below.
    if (!entry.isVideo && !ExtensionUtils::isDecodableImagePath(entry.path)) {
      continue;
    }
    QPixmap initialPixmap;
    if (entry.isVideo) {
      // Prefer a cached extracted frame; otherwise show a placeholder and
      // request async extraction. Cached null pixmaps mean a prior
      // extraction failed — keep the placeholder rather than thrashing.
      auto *extractor = VideoThumbnailExtractor::instance();
      if (extractor->hasCacheEntry(entry.path)) {
        initialPixmap = extractor->cached(entry.path);
      }
      if (initialPixmap.isNull()) {
        initialPixmap = makeVideoPlaceholder(iconSize);
      }
    }
    // For non-video entries the icon starts EMPTY (a transparent QPixmap of
    // the right size keeps the button's geometry stable so the layout
    // doesn't reflow when the real icon arrives). The actual QImage decode
    // runs on a worker (Kartend-5ux9 fix); the watcher's finished slot
    // promotes QImage → QPixmap on the main thread and sets the icon.
    // Pre-fix this loop blocked the main thread for ~3.4 seconds on the
    // first gallery population (cold OS cache + ~10 box-art-size decodes).

    QToolButton *button = nullptr;
    {
      QElapsedTimer t;
      if (perfTrace) t.start();
      button = new QToolButton(m_container);
      if (perfTrace) perfBtnCtorMs += t.elapsed();
    }
    {
      QElapsedTimer t;
      if (perfTrace) t.start();
      button->setAutoRaise(true);
      button->setCursor(Qt::PointingHandCursor);
      button->setFixedSize(thumbSize, thumbSize);
      button->setIconSize(QSize(iconSize, iconSize));
      if (!initialPixmap.isNull()) {
        button->setIcon(QIcon(initialPixmap));
      }
      button->setToolTip(entry.label);
      button->setAccessibleName(entry.label);
      if (perfTrace) perfBtnCfgMs += t.elapsed();
    }

    // Capture entry by value so the click handler keeps working after
    // the caller's list goes out of scope. Single click swaps the
    // sidebar's main preview tile to this artwork — the old behaviour
    // (open the full-screen artworkpreviewoverlay) is still available
    // by clicking the main preview tile directly.
    const DetailsPane::GalleryEntry capturedEntry = entry;
    // The tile carries its own entry so a non-mouse caller (the gamepad's
    // ringed selection) can act on exactly this artwork without having to
    // re-derive an index into the entry list.
    button->setProperty("kartendGalleryPath", capturedEntry.path);
    button->setProperty("kartendGalleryIsVideo", capturedEntry.isVideo);
    connect(button, &QToolButton::clicked, this, [this, capturedEntry]() {
      if (m_host) m_host->showMainPreviewForEntry(capturedEntry);
    });

    if (entry.isVideo) {
      // Request async frame extraction. Receiver is the button itself so
      // the connection auto-disconnects when the button is destroyed on
      // the next gallery rebuild — no manual cleanup needed.
      const QString videoPath = entry.path;
      const int targetIconSize = iconSize;
      connect(VideoThumbnailExtractor::instance(), &VideoThumbnailExtractor::frameReady, button,
              [button, videoPath](const QString &p, const QPixmap &pix) {
                if (p != videoPath || pix.isNull()) {
                  return;
                }
                const QPixmap scaled = pix.scaled(targetIconSize, targetIconSize,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                button->setIcon(QIcon(scaled));
              });
      // Defer the requestFrame trigger so the sidebar refresh returns
      // immediately. VideoThumbnailExtractor::requestFrame triggers
      // ensureMediaPipeline (lazy QMediaPlayer + QAudioOutput + QVideoSink
      // construction on first use, ~3s) + m_player->setSource (cold-disk
      // file open, ~50-100ms even when warm). QMediaPlayer is GUI-thread
      // bound so we can't QtConcurrent it.
      //
      // Round 8 (Kartend-9q8d): also wait for scroll-idle before firing.
      // The host's predicate (set by DetailsPaneManager) reflects the
      // global scroll/animation state; if a scroll is mid-glide, retry
      // in 50ms. shared_ptr<function> wrapper lets the lambda re-arm
      // itself recursively without nesting captures. QPointer guard on
      // the gallery view auto-cancels the chain when clearThumbs runs
      // (next gallery rebuild), so a stale chain can't fire after the
      // entry it belonged to is gone.
      QPointer<DetailsPaneGalleryView> selfGuard(this);
      auto fire = std::make_shared<std::function<void()>>();
      *fire = [selfGuard, videoPath, fire]() {
        if (!selfGuard) return;
        // Scroll-idle gate: the host-injected predicate (setScrollIdlePredicate)
        // replaced the old friend reach into the host's artwork controller.
        // Never wired / null keeps the "always idle" default.
        if (selfGuard->m_scrollIdle && !selfGuard->m_scrollIdle()) {
          // Re-arm the recursive retry chain in 50ms while a scroll is
          // mid-glide — see the block comment above for the full rationale.
          QTimer::singleShot(UIConstants::Timing::UI_SETTLE_RETRY_MS, selfGuard.data(), *fire);
          return;
        }
        VideoThumbnailExtractor::instance()->requestFrame(videoPath);
      };
      // Kick off the chain on the next event-loop tick so this thumbnail
      // request doesn't stall the gallery-rebuild loop that constructed it.
      QTimer::singleShot(0, this, *fire);
    } else {
      // Async image-thumb load. QPointer guards against the next
      // clearThumbs deleting the button before this load completes —
      // worker continues to run but the result is dropped. QFutureWatcher
      // parented to `this` (the gallery view) so the gallery view's
      // destruction cleanly stops watching.
      QElapsedTimer dispatchTimer;
      if (perfTrace) dispatchTimer.start();
      QPointer<QToolButton> buttonGuard(button);
      const QString path = entry.path;
      const int targetIconSize = iconSize;
      auto *watcher = new QFutureWatcher<QImage>(this);
      connect(watcher, &QFutureWatcherBase::finished, this, [watcher, buttonGuard]() {
        watcher->deleteLater();
        if (!buttonGuard) return;
        const QImage img = watcher->result();
        if (img.isNull()) {
          // File vanished between rebuild and decode — drop the
          // button rather than show an empty tile. Same intent as
          // the pre-fix synchronous `continue` skip.
          buttonGuard->deleteLater();
          return;
        }
        // Scale on the worker thread already? We could do that
        // inside the QtConcurrent lambda for further main-thread
        // savings, but the scale is fast (<1ms for typical
        // thumb sizes) and keeping it here makes the icon-size
        // change path (applyThumbSize) consistent.
        const QPixmap scaled = QPixmap::fromImage(img).scaled(
            targetIconSize, targetIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        buttonGuard->setIcon(QIcon(scaled));
      });
      watcher->setFuture(
          QtConcurrent::run([path]() { return ImageDecodeUtils::loadCapped(path); }));
      if (perfTrace) perfDispatchMs += dispatchTimer.elapsed();
    }

    {
      QElapsedTimer t;
      if (perfTrace) t.start();
      m_thumbLayout->addWidget(button);
      if (perfTrace) perfAddMs += t.elapsed();
    }
  }
  if (perfTrace) perfLoopMs = perfLoopTimer.elapsed();

  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    applyVisibility(activeTab);
    if (perfTrace) perfVisMs = t.elapsed();
  }

  if (perfTrace) {
    const qint64 total = perfWall.elapsed();
    if (total > 5) {
      qCDebug(lcPerfTrace).nospace()
          << "DetailsPaneGalleryView::rebuildThumbs: total=" << total << " (clear=" << perfClearMs
          << " loop=" << perfLoopMs << " [btnCtor=" << perfBtnCtorMs << " btnCfg=" << perfBtnCfgMs
          << " dispatch=" << perfDispatchMs << " add=" << perfAddMs << "] applyVis=" << perfVisMs
          << ") entries=" << m_entries.size();
    }
  }
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
      const int insertAt = (!m_entries.isEmpty() && m_entries.first().isVideo) ? 1 : 0;
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

void DetailsPaneGalleryView::openPreviewForPath(const QString &path, bool isVideo) {
  DetailsPane::GalleryEntry entry;
  entry.path = path;
  entry.isVideo = isVideo;
  openPreview(entry);
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
    // Register with the central z-order coordinator so the fullscreen
    // preview stacks at its documented ArtworkPreview layer. Without this
    // the overlay's show path falls back to raw raise(), and any registered
    // overlay's later bringToFront()/restack() buries it. Late-bound: a
    // setLayerManager() call before this lazy construction is handled here,
    // one after it re-registers in setLayerManager() itself.
    if (m_layerManager) {
      m_layerManager->registerOverlay(m_overlay, OverlayZOrderRegistry::Layer::ArtworkPreview);
      m_overlay->setLayerManager(m_layerManager);
    }
    connect(m_overlay, &ArtworkPreviewOverlay::visibilityChanged, this,
            &DetailsPaneGalleryView::overlayVisibilityChanged);
  }
  if (entry.isVideo) {
    m_overlay->showVideoAtPath(entry.path);
  } else {
    m_overlay->showArtworkAtPath(entry.path);
  }
  // Hand the overlay the same strip the sidebar shows, so Left/Right (and
  // the gamepad directions routed to them) cycle artwork inside expand
  // mode. Without this the overlay had a single image and nothing to
  // cycle, and the directions fell through to the grid behind it
  // (field report 2026-08-18).
  QList<ArtworkPreviewOverlay::GalleryEntry> strip;
  strip.reserve(m_entries.size());
  for (const DetailsPane::GalleryEntry &e : m_entries) {
    strip.append({e.label, e.path, e.isVideo});
  }
  m_overlay->setGalleryEntries(strip);
}
