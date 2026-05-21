// Displays file metadata, artwork preview, and item details in the sidebar
// panel.
#include <algorithm>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLoggingCategory>

#include "itemartwork.h"
#include "loggingcategories.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QStyle>
#include <QTabBar>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "artworkpreviewoverlay.h"
#include "detailspane.h"
#include "detailspanegalleryview.h"
#include "detailspaneresizegrip.h"
#include "extensionutils.h"
#include "itemwidget.h"
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
  m_videoPlayback.videoPreview = new VideoPreviewWidget(this);
  m_videoPlayback.videoPreview->setFixedSize(UIConstants::Metadata::ARTWORK_SIZE,
                                             UIConstants::Metadata::ARTWORK_SIZE);
  m_videoPlayback.videoPreview->hide();
  if (auto *artworkParentLayout =
          qobject_cast<QVBoxLayout *>(ui->artworkDisplay->parentWidget()->layout())) {
    // Force-centre both the artwork QLabel and the video preview in
    // the parent QVBoxLayout. The .ui declares the QLabel with
    // min=max=200x200 but no layout-item alignment, so QVBoxLayout's
    // default stretch-then-clamp behaviour leaves it pinned to the
    // left edge of the sidebar — visibly off-centre in any pane
    // wider than the artwork box. setAlignment(widget, AlignHCenter)
    // overrides the per-item alignment without touching the .ui.
    artworkParentLayout->setAlignment(ui->artworkDisplay, Qt::AlignHCenter);
    int idx = artworkParentLayout->indexOf(ui->artworkDisplay);
    if (idx >= 0) {
      artworkParentLayout->insertWidget(idx + 1, m_videoPlayback.videoPreview, 0, Qt::AlignHCenter);
    } else {
      artworkParentLayout->addWidget(m_videoPlayback.videoPreview, 0, Qt::AlignHCenter);
    }
  }

  // Arrow-key cycling between gallery entries. Click on the main
  // preview tile (artwork QLabel or video widget) gives it focus
  // via StrongFocus; eventFilter then catches Key_Left / Key_Right
  // and routes them to cycleMainPreview. Other keys fall through to
  // the focused widget's defaults so e.g. Escape, Tab, alphanumeric
  // search bindings keep working from elsewhere.
  ui->artworkDisplay->setFocusPolicy(Qt::StrongFocus);
  m_videoPlayback.videoPreview->setFocusPolicy(Qt::StrongFocus);
  ui->artworkDisplay->installEventFilter(this);
  m_videoPlayback.videoPreview->installEventFilter(this);

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

  // Vertical-dock media gallery view. Lazy-builds its widgets on the first
  // setEntries() call. Forwards its public events (Edit click + overlay
  // visibility transitions) as DetailsPane signals.
  m_galleryView = new DetailsPaneGalleryView(this);
  m_galleryView->setHost(this);
  connect(m_galleryView, &DetailsPaneGalleryView::editRequested, this,
          &DetailsPane::editArtworkRequested);
  connect(m_galleryView, &DetailsPaneGalleryView::overlayVisibilityChanged, this,
          &DetailsPane::galleryOverlayVisibilityChanged);

  // itemNameValue now lives in the compact title row (right-aligned next
  // to the section heading). The original block centered it under the
  // artwork tile and wrapped long names; in the new layout we want a
  // single line that elides on overflow rather than expanding the row.
  ui->itemNameValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  ui->itemNameValue->setWordWrap(false);
  applyContentAlignment();
  applyPreviewSize();

  // Debounce timer: avoid loading a video for every transient selection
  // change while the user is scrolling. Single-shot, restarted on each new
  // selection that has a video.
  m_videoPlayback.videoStartTimer = new QTimer(this);
  m_videoPlayback.videoStartTimer->setSingleShot(true);
  m_videoPlayback.videoStartTimer->setInterval(UIConstants::DetailsPane::VIDEO_PREVIEW_DEBOUNCE_MS);
  connect(m_videoPlayback.videoStartTimer, &QTimer::timeout, this, [this]() {
    if (m_videoPlayback.pendingVideoPath.isEmpty() || !m_videoPlayback.videoPreview) {
      return;
    }
    // Video preview is item-only chrome — File / Collection tabs hide
    // the artwork section entirely. Skip start-up there so we don't
    // burn QMediaPlayer resources on tabs that never render the widget.
    if (m_activeTab != DetailsPaneTab::Item) {
      return;
    }
    // Defer if a scroll animation is currently mid-glide. playVideo's
    // m_player->stop()+setSource()+play() chain blocks the GUI thread
    // ~100ms while QMediaPlayer/GStreamer initializes the new pipeline.
    // Hitting that mid-animation visibly stutters the still-running
    // scroll. Re-arm the timer with a short interval and re-check on
    // next fire — once the animation truly settles, the predicate
    // returns false and we proceed (Kartend-9q8d round 6).
    if (m_videoPlayback.scrollIdlePredicate && !m_videoPlayback.scrollIdlePredicate()) {
      m_videoPlayback.videoStartTimer->start(50);
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
    m_videoPlayback.videoPreview->show();
    m_videoPlayback.videoPreview->playVideo(m_videoPlayback.pendingVideoPath);
    if (horizontal) {
      updateHorizontalView();
    }
  });

  // Pre-warm the gallery section's lazy widget construction so the cost
  // (~2.5s on a slow filesystem — Kartend-jxp5) lands in startup instead
  // of the user's first-click critical path. Section is hidden until
  // setEntries populates it; prewarming has no UI consequence beyond the
  // up-front allocation. Safe to call after m_videoPlayback.videoPreview is constructed
  // because ensureSection's insertion-index calculation reads it as the
  // anchor for placing the gallery container below the video tile.
  if (m_galleryView) {
    m_galleryView->prewarmSection();
  }

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

  // Per-phase perf timers (Kartend-5ux9 follow-up) — the outer
  // perfTrace showed setMeta=200-240ms tail even after loadArtwork went
  // async, so the cost is in some other call inside this function.
  // Each phase below logs separately; we only emit the breakdown when
  // KARTEND_PERF_TRACE=1.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  qint64 perfFileInfoMs = 0, perfPreviewSize1Ms = 0, perfLoadArtworkMs = 0;
  qint64 perfVideo1Ms = 0, perfVideo2Ms = 0, perfArtworkOnlyMs = 0;
  qint64 perfSchedulePvMs = 0, perfTabVisMs = 0;

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
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    updateFileInfo(filePath);
    if (perfTrace) perfFileInfoMs = t.elapsed();
  }

  QFileInfo fileInfo(filePath);
  const QString baseName = fileInfo.completeBaseName();

  // Drop the previous selection's cached artwork so applyPreviewSize falls
  // back to the empty placeholder while the new image is resolved.
  m_artworkSource = QPixmap();
  m_primaryArtworkPath.clear();
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    applyPreviewSize();
    if (perfTrace) perfPreviewSize1Ms = t.elapsed();
  }

  // Try collection's artwork directory first if provided
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    if (!artworkDirectory.isEmpty()) {
      loadArtwork(baseName, artworkDirectory);
    } else {
      // Fallback to sibling "artwork" directory
      const QDir fileDir = fileInfo.dir();
      const QString siblingArtworkDir = fileDir.absolutePath() + "/artwork";
      loadArtwork(baseName, siblingArtworkDir);
    }
    if (perfTrace) perfLoadArtworkMs = t.elapsed();
  }

  // Resolve preview video. Lookup priority:
  // (1) the collection's `videoDirectory` (power-user override), then
  // (2) `{artworkDirectory}/video/` — where scraped videos land under the
  // single-root layout.
  QString videoPath;
  if (!videoDirectory.isEmpty()) {
    QElapsedTimer t;
    if (perfTrace) t.start();
    videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
    if (perfTrace) perfVideo1Ms = t.elapsed();
  }
  if (videoPath.isEmpty() && !artworkDirectory.isEmpty()) {
    QElapsedTimer t;
    if (perfTrace) t.start();
    videoPath = VideoUtils::findVideoForFile(filePath, QDir(artworkDirectory).filePath("video"));
    if (perfTrace) perfVideo2Ms = t.elapsed();
  }
  // Static category at warning level so this trace stays silent
  // during normal navigation (fires on every selection move). Opt in
  // via `KARTEND_LOG_RULES=kartend.video.debug=true`.
  static const QLoggingCategory lcVideo("kartend.video", QtWarningMsg);
  qCDebug(lcVideo) << "preview lookup item=" << filePath << "videoDir=" << videoDirectory
                   << "artworkDir=" << artworkDirectory << "resolved=" << videoPath;

  // Only churn the video preview when the resolved path actually
  // changed for THIS item. setMetadata fires multiple times for the
  // same selection (manager refreshes after artwork load, post-scrape
  // updates, hover events, etc.); without this guard each invocation
  // hides the video widget, which triggers VideoPreviewWidget's
  // hideEvent → m_player->pause(), then the 500ms debounce re-shows +
  // re-plays — a glitchy ping-pong the user sees as "video stops
  // playing". When the path is unchanged AND the widget is currently
  // showing the same video, we leave the playback alone entirely.
  const QString currentVideoPath =
      m_videoPlayback.videoPreview ? m_videoPlayback.videoPreview->currentVideoPath() : QString();
  const bool videoUnchanged = !videoPath.isEmpty() && videoPath == currentVideoPath &&
                              m_videoPlayback.videoPreview &&
                              m_videoPlayback.videoPreview->isVisible();
  if (!videoUnchanged) {
    {
      QElapsedTimer t;
      if (perfTrace) t.start();
      showArtworkOnly();
      if (perfTrace) perfArtworkOnlyMs = t.elapsed();
    }
    {
      QElapsedTimer t;
      if (perfTrace) t.start();
      schedulePreviewVideo(videoPath);
      if (perfTrace) perfSchedulePvMs = t.elapsed();
    }
  }

  // Defer all section visibility to applyTabVisibility() so each tab
  // ends up with its own distinct widget set.
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    applyTabVisibility();
    if (perfTrace) perfTabVisMs = t.elapsed();
  }

  if (perfTrace) {
    const qint64 phaseSum = perfFileInfoMs + perfPreviewSize1Ms + perfLoadArtworkMs + perfVideo1Ms +
                            perfVideo2Ms + perfArtworkOnlyMs + perfSchedulePvMs + perfTabVisMs;
    if (phaseSum > 5) {
      qCDebug(lcPerfTrace).nospace()
          << "DetailsPane::setMetadata phases: sum=" << phaseSum << " (fileInfo=" << perfFileInfoMs
          << " previewSize1=" << perfPreviewSize1Ms << " loadArtwork=" << perfLoadArtworkMs
          << " video1=" << perfVideo1Ms << " video2=" << perfVideo2Ms
          << " artworkOnly=" << perfArtworkOnlyMs << " schedulePv=" << perfSchedulePvMs
          << " tabVis=" << perfTabVisMs << ") path=" << filePath;
    }
  }
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

void DetailsPane::renderCollectionSummary() {
  setArtworkSectionVisible(false);
  setFileInfoRowsVisible(false);
  if (m_galleryView) {
    m_galleryView->hideSection();
  }
  ui->titleLabel->setText(tr("Collection Information"));
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

  // Collection summaries are short and static — there is no marquee and
  // on this tab the metadata card is the only content. Left uncapped,
  // m_metadataScroll's Expanding policy stretches the styled backdrop
  // bubble to the full sidebar height; on a sparse summary (a not-yet-
  // scraped subcollection shows just Items + Last scanned) that reads as
  // an oversized empty card. Cap the scroll area to the rows' real,
  // wrap-aware height so the bubble hugs the summary and matches a
  // scraped collection's tighter card. clearDetailsSection lifts the cap
  // again for the Item tab.
  if (m_metadataScroll && m_metadataBackdrop) {
    if (QLayout *inner = m_metadataBackdrop->layout()) {
      inner->activate();
    }
    const int width = m_metadataScroll->viewport() ? m_metadataScroll->viewport()->width() : 0;
    if (width > 0) {
      // heightForWidth resolves the wrapped path rows; sizeHint suffices
      // when no row wraps. Width unknown (pane not yet shown) → leave the
      // card uncapped rather than risk clipping a row.
      const int contentHeight = m_metadataBackdrop->hasHeightForWidth()
                                    ? m_metadataBackdrop->heightForWidth(width)
                                    : m_metadataBackdrop->sizeHint().height();
      m_metadataScroll->setMaximumHeight(contentHeight);
    }
  }

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
  // Synchronous prelude: everything that doesn't need a stat() — path
  // display + extension parsing. These run instantly and keep the labels
  // populated while the async exists/size/lastModified worker is in flight.
  m_currentFilePath = filePath;
  ui->filePathValue->setWordWrap(true);
  updateFilePathDisplay();
  ui->filePathValue->setToolTip(filePath);

  // Show placeholders for the stat-derived fields until the worker delivers.
  // On slow mounts (network/USB) a single QFileInfo::exists/size/lastModified
  // can take 50-260ms; pre-fix this dominated DetailsPane::setMetadata's
  // total cost (Kartend-5ux9 measurement run).
  ui->fileSizeValue->setText(QStringLiteral("…"));
  ui->lastModifiedValue->setText(QStringLiteral("…"));

  QString extension = QFileInfo(filePath).suffix().toUpper();
  if (extension.isEmpty()) {
    extension = "Unknown";
  }
  ui->fileExtensionValue->setText(extension + " file");

  // Async stat() phase. Generation counter drops stale results when a
  // newer selection has started before this worker delivers (otherwise
  // the previous item's size would land in the new item's panel after a
  // rapid click-through). QFutureWatcher parented to `this` so a pane
  // teardown auto-disconnects the lambda.
  struct StatResult {
    bool exists = false;
    qint64 size = 0;
    QDateTime lastModified;
  };
  const quint64 myGen = ++m_fileInfoGen;
  auto *watcher = new QFutureWatcher<StatResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, myGen]() {
    watcher->deleteLater();
    if (myGen != m_fileInfoGen) return;
    const StatResult res = watcher->result();
    if (!res.exists) {
      ui->filePathValue->setText("File not found");
      ui->fileSizeValue->setText("-");
      ui->lastModifiedValue->setText("-");
      ui->fileExtensionValue->setText("-");
      return;
    }
    ui->fileSizeValue->setText(formatFileSize(res.size));
    ui->lastModifiedValue->setText(res.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
  });
  watcher->setFuture(QtConcurrent::run([filePath]() {
    QFileInfo fi(filePath);
    StatResult r;
    r.exists = fi.exists();
    if (r.exists) {
      r.size = fi.size();
      r.lastModified = fi.lastModified();
    }
    return r;
  }));
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
  ui->itemNameValue->setVisible(true);

  switch (m_activeTab) {
  case DetailsPaneTab::Item: {
    // "What is this?" — artwork preview, video preview, gallery,
    // extended metadata + usage stats. No filesystem rows.
    ui->titleLabel->setText(tr("Item Information"));
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
    renderCollectionSummary();
    break;
  case DetailsPaneTab::File:
    // Pure filesystem view — name + path/size/modified/extension.
    // No artwork, no video, no gallery, no extended metadata.
    ui->titleLabel->setText(tr("File Information"));
    ui->itemNameValue->setText(m_currentItemName.isEmpty() ? tr("No item selected")
                                                           : m_currentItemName);
    setArtworkSectionVisible(false);
    setFileInfoRowsVisible(true);
    if (m_galleryView) m_galleryView->hideSection();
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
  // Vertical dock (the only case the original .ui artwork preview is
  // shown in — the dedicated horizontal view has its own preview
  // tile). Tracks the scroll area's viewport width minus the
  // contentLayout's left+right margins (10+10=20) so the tile uses
  // the full pane width edge-to-edge. Capped at ARTWORK_SIZE_MAX so
  // very wide sidebars don't blow the tile up beyond a reasonable
  // limit.
  int viewportW =
      (ui->scrollArea && ui->scrollArea->viewport()) ? ui->scrollArea->viewport()->width() : 0;
  if (viewportW <= 0 && ui->contentWidget) {
    viewportW = ui->contentWidget->width();
  }
  return qBound(80, viewportW - 20, UIConstants::Metadata::ARTWORK_SIZE_MAX);
}

int DetailsPane::currentGalleryThumbSize() const {
  // Vertical dock uses the .ui's compact constant. Horizontal dock has its
  // own dedicated gallery inside m_horizontalView and ignores this.
  return UIConstants::Metadata::GALLERY_THUMB_SIZE;
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
    if (m_hPreviewLayout && m_videoPlayback.videoPreview &&
        m_hPreviewLayout->indexOf(m_videoPlayback.videoPreview) == -1) {
      m_hPreviewLayout->addWidget(m_videoPlayback.videoPreview);
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
      if (m_videoPlayback.videoPreview && ui->artworkDisplay &&
          cl->indexOf(m_videoPlayback.videoPreview) == -1) {
        const int artIdx = cl->indexOf(ui->artworkDisplay);
        cl->insertWidget(artIdx >= 0 ? artIdx + 1 : -1, m_videoPlayback.videoPreview);
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
    if (m_horizontalView && m_videoPlayback.videoPreview) {
      const int previewSize = horizontalPreviewSize();
      m_videoPlayback.videoPreview->setFixedSize(previewSize, previewSize);
    }
    return;
  }
  const int width = previewBoxSize();
  // Tile shape is fixed (same dimensions for every item) so the layout
  // doesn't reshuffle between selections. Width comes from the
  // sidebar's available room; height is derived from the pane viewport
  // so very tall sidebars don't stretch the tile to dominate the
  // pane. 3:4 portrait works well for the most common case (game box
  // art); landscape art letterboxes top + bottom but a fixed tile
  // shape beats a dynamic one for visual stability.
  int viewportH = 0;
  if (ui->scrollArea && ui->scrollArea->viewport()) {
    viewportH = ui->scrollArea->viewport()->height();
  }
  if (viewportH <= 0 && ui->contentWidget) {
    viewportH = ui->contentWidget->height();
  }
  // Cap the tile at ~45% of the pane height so the description + the
  // metadata card below it still get usable space without scrolling.
  const int idealHeight = (width * 4) / 3;
  const int height = viewportH > 0 ? std::min(idealHeight, viewportH * 45 / 100) : idealHeight;

  // Video has no known aspect ratio until the first frame loads, so it
  // takes the same tile and the sink letterboxes internally.
  if (m_videoPlayback.videoPreview) {
    m_videoPlayback.videoPreview->setFixedSize(width, height);
  }
  // Vertical dock: scale gallery thumbs alongside the preview.
  applyGalleryThumbSize();
  if (!ui || !ui->artworkDisplay) {
    return;
  }
  // Hide the tile entirely when there's nothing to show — the
  // viewport-derived size grows the empty rectangle past 300px tall, which
  // dominates the unscraped Item tab as a giant Mid-colored slab.
  if (m_artworkSource.isNull()) {
    ui->artworkDisplay->setPixmap(QPixmap());
    ui->artworkDisplay->hide();
    return;
  }
  ui->artworkDisplay->setFixedSize(width, height);
  ui->artworkDisplay->show();
  QPixmap scaled =
      m_artworkSource.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap centered(width, height);
  centered.fill(palette().color(QPalette::Base));
  QPainter painter(&centered);
  const int x = (width - scaled.width()) / 2;
  const int y = (height - scaled.height()) / 2;
  painter.drawPixmap(x, y, scaled);
  painter.end();
  ui->artworkDisplay->setPixmap(centered);
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
  const QList<QWidget *> centered = {ui->artworkDisplay, m_videoPlayback.videoPreview};
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
  // pick up at the same playback position. m_videoPlayback.pendingVideoPath is preserved
  // for the same reason: the resume path uses it when the prior schedule
  // was caught mid-debounce (path set but not yet loaded into the widget).
  if (m_videoPlayback.videoStartTimer) {
    m_videoPlayback.videoStartTimer->stop();
  }
  if (m_videoPlayback.videoPreview) {
    m_videoPlayback.videoPreview->hide();
  }
  // Restore the static artwork display so the sidebar isn't a black square
  // while the overlay is on top.
  ui->artworkDisplay->show();
}

void DetailsPane::resumePreviewVideo() {
  if (!m_videoPlayback.videoPreview) {
    return;
  }
  if (m_videoPlayback.videoPreview->hasLoadedSource()) {
    // showEvent inside VideoPreviewWidget calls QMediaPlayer::play() — and
    // the player was left in PausedState by hideEvent, so play() resumes
    // from the same position rather than restarting. Hide the static
    // artwork in vertical dock since the two share the same slot; in
    // horizontal dock they sit side-by-side and artwork visibility is
    // owned by the scrollArea (already hidden by applyDockOrientation).
    if (!CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      ui->artworkDisplay->hide();
    }
    m_videoPlayback.videoPreview->show();
  } else if (!m_videoPlayback.pendingVideoPath.isEmpty() && m_videoPlayback.videoStartTimer) {
    // The pause caught us mid-debounce. Re-arm the timer so the original
    // play schedule fires again on the now-visible widget.
    m_videoPlayback.videoStartTimer->start();
  }
}

bool DetailsPane::togglePreviewVideoPause() {
  if (!m_videoPlayback.videoPreview || !m_videoPlayback.videoPreview->hasLoadedSource()) {
    return false;
  }
  m_videoPlayback.videoPreview->togglePauseResume();
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

  // Phase 1 (synchronous, on main thread): collect ALL candidate paths
  // that exist on disk, in priority order. Each QFile::exists is a stat
  // call (cheap). We can't commit to a single resolved path here because
  // some candidates may be present but undecodable — e.g. a 0-byte
  // failed-scrape `.jpg` shadowing a valid `.png`. The original
  // synchronous loop would QPixmap-construct each candidate inline and
  // continue on null; this version preserves that contract by handing
  // the whole list to the worker so it can do the decode-to-find
  // walk off-thread.
  QStringList candidatePaths;
  for (const QString &ext : ExtensionUtils::imageBaseExtensions()) {
    QString lowerCandidate = artworkDir.absoluteFilePath(baseName + "." + ext);
    if (QFile::exists(lowerCandidate)) {
      candidatePaths.append(lowerCandidate);
    }
    QString upperCandidate = artworkDir.absoluteFilePath(baseName + "." + ext.toUpper());
    if (upperCandidate != lowerCandidate && QFile::exists(upperCandidate)) {
      candidatePaths.append(upperCandidate);
    }
  }
  // Fall back to typed-subdir scraped art when no top-level mirror exists.
  // Long-standing behaviour gap (Kartend-wppu): for collections whose
  // artworkDirectory points at a "wide" Artwork/ tree (parent of typed
  // subdirs) rather than a single typed subdir like Artwork/covers/, items
  // without a top-level {baseName}.{ext} mirror would render with a blank
  // primary tile even though scraped art existed under front/, box/,
  // screenshot/, etc. Walk the standard types in gallery display priority
  // (front first) and use the first available file. ItemArtworkStore
  // already owns the {artworkDirectory}/{type}/{baseName}.{ext} probe so
  // we just iterate.
  if (candidatePaths.isEmpty()) {
    for (const QString &type : ItemArtworkStore::standardTypes()) {
      const QString sub = ItemArtworkStore::findStandardArtwork(baseName, artworkDirectory, type);
      if (!sub.isEmpty()) {
        candidatePaths.append(sub);
        break;
      }
    }
  }
  if (candidatePaths.isEmpty()) {
    return; // Nothing to load — placeholder stays.
  }
  // Tentative primary-artwork path so setArtworkGallery (called later in
  // the same refresh pass) can synthesize the primary-cover thumb in the
  // gallery strip. If the worker ends up choosing a different candidate
  // (because the tentative one fails to decode), the callback updates
  // m_primaryArtworkPath to match the actual resolved path.
  m_primaryArtworkPath = candidatePaths.first();

  // Phase 2 (async, on QThreadPool): walk the candidate list and return
  // the first one that successfully decodes. Pre-fix this was a
  // synchronous QPixmap(path) on the main thread (50-280ms per fresh
  // selection on slow filesystems). Moving the walk off-thread keeps the
  // UI responsive; the user sees the placeholder briefly then the real
  // cover. Same pattern as ScrapeResultDialog::appendThumbAsync
  // (Kartend-j5lc). QImage is thread-safe; QPixmap::fromImage runs on
  // the main thread in the watcher's finished slot.
  //
  // Generation counter handles rapid-click races: if a newer loadArtwork
  // call has bumped m_artworkLoadGen by the time this worker finishes,
  // the older result is dropped instead of overwriting the currently
  // displayed item's pixmap. QFutureWatcher parented to `this` so a
  // pane destroy auto-disconnects the lambda and drops the result.
  using LoadResult = QPair<QString, QImage>;
  const quint64 myGen = ++m_artworkLoadGen;
  auto *watcher = new QFutureWatcher<LoadResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, myGen]() {
    watcher->deleteLater();
    if (myGen != m_artworkLoadGen) return;
    const LoadResult res = watcher->result();
    if (res.second.isNull()) return;
    // The worker picks the first decodable candidate; update
    // m_primaryArtworkPath to match in case the tentative path
    // we set above wasn't the chosen one.
    m_primaryArtworkPath = res.first;
    // Cache the original-resolution pixmap so applyPreviewSize
    // can re-render at the new dimension on sidebar resize
    // without re-reading from disk.
    m_artworkSource = QPixmap::fromImage(res.second);
    applyPreviewSize();
    updateHorizontalView();
  });
  watcher->setFuture(QtConcurrent::run([candidatePaths]() -> LoadResult {
    for (const QString &p : candidatePaths) {
      QImage img(p);
      if (!img.isNull()) {
        return {p, img};
      }
    }
    return {QString(), QImage()};
  }));
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
  if (m_videoPlayback.videoPreview) {
    // Skip the stop()+hide() when there's nothing to stop. Pre-fix the
    // unconditional QMediaPlayer::stop() blocked the GUI thread for
    // ~85-100ms per selection on items where the player had been left in
    // a pipeline-initialized state by a previous schedulePreviewVideo —
    // GStreamer's pipeline state transition isn't free. The user perceived
    // this as a per-click stutter (Kartend-2c7c). isVisible OR a non-empty
    // currentVideoPath means the player has something to tear down;
    // otherwise the call is a no-op anyway.
    const bool needsStop = m_videoPlayback.videoPreview->isVisible() ||
                           !m_videoPlayback.videoPreview->currentVideoPath().isEmpty();
    if (needsStop) {
      m_videoPlayback.videoPreview->stop();
      m_videoPlayback.videoPreview->hide();
    }
  }
  ui->artworkDisplay->show();
}

// Schedule preview video playback after the debounce interval. Passing an
// empty path cancels any pending playback.
void DetailsPane::schedulePreviewVideo(const QString &videoPath) {
  m_videoPlayback.pendingVideoPath = videoPath;
  if (!m_videoPlayback.videoStartTimer) {
    return;
  }
  m_videoPlayback.videoStartTimer->stop();
  if (!videoPath.isEmpty()) {
    // Explicit interval — the timer callback may shorten the next
    // re-arm to 50ms when a scroll-active deferral happens
    // (Kartend-9q8d round 6). Without specifying the interval here,
    // schedulePreviewVideo would inherit the deferral interval and
    // start firing every 50ms instead of waiting the full debounce.
    m_videoPlayback.videoStartTimer->start(UIConstants::DetailsPane::VIDEO_PREVIEW_DEBOUNCE_MS);
  }
}

void DetailsPane::setScrollIdlePredicate(std::function<bool()> predicate) {
  m_videoPlayback.scrollIdlePredicate = std::move(predicate);
}

bool DetailsPane::isScrollIdle() const {
  return !m_videoPlayback.scrollIdlePredicate || m_videoPlayback.scrollIdlePredicate();
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
  // Slightly looser stacking: description tile and metadata card read
  // as distinct blocks with a clear breath between them, rather than
  // butting up against each other.
  outer->setSpacing(5);

  // No section heading: the metadata rows speak for themselves below
  // the description, and the previous "Details" label only widened
  // the gap between the gallery scrollbar and the first row.

  // Manual button is constructed up front so it can be referenced from
  // setManualFile / clearDetailsSection without lifecycle juggling, but
  // it's only laid out at the *bottom* of the details section (below
  // the metadata scroll). That placement is intentional: when the
  // button was above the description, scraped items with a manual
  // pushed the description ~28px below the gallery, making the gap
  // between gallery and description read as much larger than it
  // actually was.
  m_manualButton = new QPushButton(tr("Open Manual"), m_detailsContainer);
  m_manualButton->setCursor(Qt::PointingHandCursor);
  m_manualButton->hide();
  connect(m_manualButton, &QPushButton::clicked, this, &DetailsPane::openCurrentManual);

  // Metadata grid lives inside a QFrame (the styled backdrop) which is
  // itself nested in a QScrollArea. The QScrollArea claims the leftover
  // vertical space below the description and clips anything that
  // overflows — an auto-scroll timer marquees through the rows so the
  // outer details pane never needs to be scrolled.
  m_metadataBackdrop = new QFrame;
  m_metadataBackdrop->setObjectName(QStringLiteral("metadataBackdrop"));
  m_metadataBackdrop->setFrameShape(QFrame::NoFrame);
  auto *gridHost = new QVBoxLayout(m_metadataBackdrop);
  gridHost->setContentsMargins(6, 4, 6, 4);
  gridHost->setSpacing(0);
  m_detailsLayout = new QGridLayout();
  m_detailsLayout->setContentsMargins(0, 0, 0, 0);
  m_detailsLayout->setHorizontalSpacing(10);
  m_detailsLayout->setVerticalSpacing(6);
  // Equal stretch on both columns so short pairs (Genre / Players, etc.)
  // get balanced halves instead of one taking the slack.
  m_detailsLayout->setColumnStretch(0, 1);
  m_detailsLayout->setColumnStretch(1, 1);
  gridHost->addLayout(m_detailsLayout);
  // Trailing stretch keeps the rows top-aligned when the card has more
  // vertical room than the rows need.
  gridHost->addStretch(1);

  m_metadataScroll = new QScrollArea(m_detailsContainer);
  m_metadataScroll->setObjectName(QStringLiteral("metadataScroll"));
  m_metadataScroll->setFrameShape(QFrame::NoFrame);
  m_metadataScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_metadataScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_metadataScroll->setWidgetResizable(true);
  m_metadataScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  m_metadataScroll->viewport()->setAutoFillBackground(false);
  m_metadataScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
  m_metadataScroll->setWidget(m_metadataBackdrop);
  outer->addWidget(m_metadataScroll, /*stretch=*/1);
  // Manual button anchored below the scrolling metadata so it doesn't
  // disturb the gallery→description hand-off at the top of the section.
  outer->addWidget(m_manualButton);

  contentLayout->addWidget(m_detailsContainer, /*stretch=*/1);
  m_detailsContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  m_detailsContainer->hide();

  // Auto-scroll driver for the metadata block. One QTimer runs for the
  // pane's lifetime; it's a no-op when the inner card fits the
  // viewport. clearDetailsSection snaps the scroll position back to 0
  // so each new selection starts at the top.
  m_metadataAutoScrollTimer = new QTimer(this);
  m_metadataAutoScrollTimer->setInterval(120);
  m_metadataAutoScrollPause = 16;
  connect(m_metadataAutoScrollTimer, &QTimer::timeout, this, [this]() {
    if (!m_metadataScroll) return;
    auto *bar = m_metadataScroll->verticalScrollBar();
    if (!bar || bar->maximum() <= 0) {
      m_metadataAutoScrollPause = 16; // re-arm pause for when content grows
      return;
    }
    if (m_metadataAutoScrollPause > 0) {
      --m_metadataAutoScrollPause;
      return;
    }
    if (bar->value() >= bar->maximum()) {
      bar->setValue(0);
      m_metadataAutoScrollPause = 25;
      return;
    }
    bar->setValue(bar->value() + 1);
  });
  m_metadataAutoScrollTimer->start();
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
  // Reset the grid cursor so the next rebuild starts in the top-left
  // cell rather than continuing wherever the previous selection left off.
  m_detailsRow = 0;
  m_detailsCol = 0;
  // Description widgets live outside m_metadataScroll so they aren't
  // touched by the grid clear above; nuke them here so the next
  // appendScrollingDescription rebuilds with the new selection's text.
  if (m_descriptionScroll) {
    m_descriptionScroll->deleteLater();
    m_descriptionScroll = nullptr;
  }
  if (m_descriptionLabel) {
    m_descriptionLabel->deleteLater();
    m_descriptionLabel = nullptr;
  }
  // Snap the metadata auto-scroller back to the top + pause so the
  // freshly-built rows get the same "rest then start scrolling" cycle
  // a fresh selection would.
  if (m_metadataScroll) {
    if (auto *bar = m_metadataScroll->verticalScrollBar()) {
      bar->setValue(0);
    }
    // Drop any height cap a previous Collection-tab render applied. The
    // Item tab's metadata card is meant to fill the leftover space
    // below the description (the auto-scroller marquees any overflow);
    // renderCollectionSummary re-applies the cap when it needs it.
    m_metadataScroll->setMaximumHeight(QWIDGETSIZE_MAX);
  }
  m_metadataAutoScrollPause = 16;
}

void DetailsPane::appendDetailRow(const QString &label, const QString &value, bool wrap) {
  if (!m_detailsLayout || value.trimmed().isEmpty()) {
    return;
  }
  // Render label + value as one inline QLabel ("Genre: Action"). The label
  // prefix uses the per-collection sidebar accent (palette(Highlight) is
  // plumbed in applyAppearance); the value text uses the default text
  // color. Single line per pair keeps the metadata block compact and lets
  // two short pairs share a row in the two-column grid.
  auto *rowLabel = new QLabel(m_detailsContainer);
  rowLabel->setTextFormat(Qt::RichText);
  // QTextDocument (rich text) won't resolve `palette(highlight)` — we
  // need a concrete hex color for the span style. Pull it from the
  // detail container's palette so the per-collection accent override
  // wins. (applyBubbleStyles rebuilds the value rows on appearance
  // change, so this snapshot stays fresh.)
  const QColor accent = m_detailsContainer->palette().color(QPalette::Highlight);
  rowLabel->setText(QStringLiteral("<span style=\"color:%1;\"><b>%2:</b></span> %3")
                        .arg(accent.name(), label.toHtmlEscaped(), value.toHtmlEscaped()));
  // Color only — DO NOT set `padding: 0px` here. An inline padding
  // declaration on a QLabel that also matches the global
  // `QLabel[sidebarRole="value"]` rule overrides the bubble's padding
  // entirely, which means the text would render flush against the
  // bubble edge. Leaving padding unset lets the global 5px 12px (plus
  // any explicit left-only padding added in applyBubbleStyles) win.
  rowLabel->setStyleSheet("color: palette(windowtext);");
  rowLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rowLabel->setWordWrap(wrap);
  rowLabel->setProperty("sidebarRole", "value");
  rowLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // Decide whether this row fits as a half-width cell or needs the full
  // sidebar width. wrap=true is the explicit "I will be long" caller
  // signal; the heuristic kicks in otherwise. Measure the bold label
  // prefix and the regular-weight value separately because the bubble
  // renders the prefix bold (so a plain fontMetrics call underestimates
  // the rendered width). Subtract the per-cell bubble padding from the
  // half-column budget so we don't over-pack rows the bubble would clip.
  const int viewportW =
      (ui->scrollArea && ui->scrollArea->viewport()) ? ui->scrollArea->viewport()->width() : 0;
  // 28 = contentLayout l+r margin, 8 = column gap, 32 = 2*16 bubble padding.
  const int halfW = qMax(0, (viewportW - 28 - 8 - 32) / 2);
  QFont boldFont = rowLabel->font();
  boldFont.setBold(true);
  const QFontMetrics fmBold(boldFont);
  const QFontMetrics fmReg(rowLabel->font());
  const int textW =
      fmBold.horizontalAdvance(label + QStringLiteral(": ")) + fmReg.horizontalAdvance(value);
  const bool fullRow = wrap || (halfW > 0 && textW > halfW);

  if (fullRow) {
    if (m_detailsCol != 0) {
      ++m_detailsRow;
      m_detailsCol = 0;
    }
    m_detailsLayout->addWidget(rowLabel, m_detailsRow, 0, 1, 2);
    ++m_detailsRow;
    m_detailsCol = 0;
    return;
  }
  m_detailsLayout->addWidget(rowLabel, m_detailsRow, m_detailsCol);
  if (m_detailsCol == 0) {
    m_detailsCol = 1;
  } else {
    m_detailsCol = 0;
    ++m_detailsRow;
  }
}

void DetailsPane::appendScrollingDescription(const QString &label, const QString &value,
                                             int maxLines) {
  if (!m_detailsLayout || value.trimmed().isEmpty()) {
    return;
  }
  // The description label mirrors the gallery section's "Media gallery"
  // header — same font weight, same padding, same color — so the two
  // section titles read as a matched pair.
  auto *labelWidget = new QLabel(label, m_detailsContainer);
  QFont labelFont = labelWidget->font();
  labelFont.setBold(true);
  labelWidget->setFont(labelFont);
  labelWidget->setStyleSheet("color: palette(windowtext); padding: 0px;");

  // The QScrollArea is the public widget — it owns the bubble background
  // (so the visual chrome matches the other "value" rows) and clips the
  // inner label as it animates. Disable user scrollbars; the auto-scroll
  // timer is the only thing that moves the viewport. The viewport is
  // forced transparent here so the bubble's background-color (applied
  // via the global stylesheet for sidebarRole="value") shows through.
  auto *scroll = new QScrollArea(m_detailsContainer);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidgetResizable(true);
  scroll->setProperty("sidebarRole", "value");
  scroll->viewport()->setAutoFillBackground(false);

  auto *valueWidget = new QLabel(value, scroll);
  valueWidget->setWordWrap(true);
  valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
  valueWidget->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  valueWidget->setStyleSheet("color: palette(windowtext); background: transparent;");
  scroll->setWidget(valueWidget);

  // Cap height to `maxLines` of wrapped text at the current font.
  // fontMetrics().lineSpacing() includes the leading; +4px slack covers
  // the inner padding the QScrollArea adds around its viewport.
  const int lineH = valueWidget->fontMetrics().lineSpacing();
  scroll->setFixedHeight(lineH * maxLines + 4);

  // The description block lives in m_detailsContainer's *outer* layout,
  // not in the metadata grid — that's what keeps it pinned in place
  // while the metadata card below marquees. Insert just before
  // m_metadataScroll so the visual order is preserved (description on
  // top, scrolling rows below).
  m_descriptionLabel = labelWidget;
  m_descriptionScroll = scroll;
  auto *containerLayout = qobject_cast<QVBoxLayout *>(m_detailsContainer->layout());
  if (containerLayout) {
    int insertIndex = containerLayout->indexOf(m_metadataScroll);
    if (insertIndex < 0) insertIndex = containerLayout->count();
    containerLayout->insertWidget(insertIndex, labelWidget);
    containerLayout->insertWidget(insertIndex + 1, scroll);
  }
  // Auto-scroll driver. Defer the start by one event-loop tick so the
  // QScrollArea has finished its first layout pass and `verticalScrollBar()
  // ->maximum()` reflects the wrapped text height (otherwise it's 0 and
  // the animator decides there's nothing to scroll).
  QTimer::singleShot(0, scroll, [scroll]() {
    auto *bar = scroll->verticalScrollBar();
    if (!bar || bar->maximum() <= 0) return; // content fits — no scroll needed
    auto *tick = new QTimer(scroll);
    // State for the marquee animation. One-way (top → bottom) scroll
    // at reading speed: every tick advances one pixel, the bottom
    // holds for a beat, then the position resets to 0 and the cycle
    // repeats. Reversing was confusing because the eye loses the line
    // it was reading mid-snap.
    struct State {
      int pauseTicks = 12; // initial pause at the top so the user has
                           // a beat to start reading before motion begins
    };
    auto *state = new State;
    QObject::connect(tick, &QTimer::destroyed, [state]() { delete state; });
    QObject::connect(tick, &QTimer::timeout, scroll, [bar, state]() {
      if (state->pauseTicks > 0) {
        --state->pauseTicks;
        return;
      }
      if (bar->value() >= bar->maximum()) {
        // Reached the bottom — hold long enough to read the last
        // line, then snap back to the top and hold again before the
        // next scroll cycle starts.
        bar->setValue(0);
        state->pauseTicks = 25; // ~3s hold after the reset
        return;
      }
      bar->setValue(bar->value() + 1);
    });
    // 120ms/tick = ~1px every 0.12s. With a ~5-line viewport on a 10-line
    // synopsis this works out to roughly one screen of text every 8s,
    // comfortable reading pace without feeling sluggish.
    tick->start(120);
  });
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
    // Unscraped fallback: surface the static file-info rows so the Item
    // tab still has something to show instead of an empty sidebar.
    setFileInfoRowsVisible(true);
    return;
  }
  // Scraped metadata wins the slot — hide the placeholder file-info rows.
  setFileInfoRowsVisible(false);

  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  clearDetailsSection();

  if (!metadata.title.isEmpty()) {
    ui->itemNameValue->setText(metadata.title);
  }

  appendScrollingDescription(tr("Description"), metadata.description, /*maxLines=*/5);
  appendDetailRow(tr("Genre"), metadata.genre);
  appendDetailRow(tr("Developer"), metadata.developer);
  appendDetailRow(tr("Publisher"), metadata.publisher);
  appendDetailRow(tr("Release date"), metadata.releaseDate);
  appendDetailRow(tr("Rating"), metadata.contentRating);
  appendDetailRow(tr("Players"), metadata.players);
  appendDetailRow(tr("Runtime"), formatRuntime(metadata.runtimeSeconds));
  appendDetailRow(tr("Tags"), formatTags(metadata.tags));

  // User-defined custom fields. Rendered after the structured
  // fields so they appear as a contiguous block at the bottom of Details.
  // parseCustomFields() returns rows in alphabetical key order for stable
  // display regardless of edit history. wrap is left at the default so
  // appendDetailRow's width heuristic decides per row whether to pair
  // with a neighbour or take the full sidebar width.
  const auto customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);
  for (const auto &pair : customFields) {
    appendDetailRow(pair.first, pair.second);
  }

  // re-apply the active sidebar-font override so the just-
  // appended detail rows pick up the same font as the static labels. The
  // override falls back to a no-op when no override is in effect.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);

  // Unscraped items have nothing to put in the metadata grid. The
  // metadataBackdrop QFrame has an Expanding size policy so without
  // content it stretches to fill all remaining sidebar height and
  // paints its bubble background across the whole region — visible
  // as an oversized empty card under the artwork tile. Hide the
  // scroll wrapper (and the description widgets, if appendScrollingDescription
  // never built them) when there's nothing to show, so the layout
  // collapses cleanly. Restored automatically on the next
  // setExtendedMetadata call with non-empty content.
  const bool haveDescription = m_descriptionScroll != nullptr;
  const bool haveMetadataRows = m_detailsLayout && m_detailsLayout->count() > 0;
  if (m_metadataScroll) m_metadataScroll->setVisible(haveMetadataRows);
  if (!haveDescription && !haveMetadataRows && m_manualPath.isEmpty()) {
    // Nothing scraped, no manual link — hide the whole container so
    // it doesn't reserve layout space below the gallery.
    m_detailsContainer->hide();
  } else {
    m_detailsContainer->show();
  }
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
  // Usage stats just added rows to the grid, so the metadata scroll
  // has content again — reveal it. setExtendedMetadata may have
  // hidden it on an unscraped item.
  if (m_metadataScroll && m_detailsLayout && m_detailsLayout->count() > 0) {
    m_metadataScroll->show();
  }
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
