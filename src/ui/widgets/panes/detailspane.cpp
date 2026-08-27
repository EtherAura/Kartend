// Displays file metadata, artwork preview, and item details in the sidebar
// panel.
#include "detailsformat.h"

#include <algorithm>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLoggingCategory>

#include "collection/enumstringhelpers.h"
#include "itemartwork.h"
#include "loggingcategories.h"
#include "stringutils.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QStyle>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "artworkpreviewoverlay.h"
#include "detailspane.h"

#include "detailspaneartwork.h"
#include "detailspanegalleryview.h"
#include "detailspanemetadataview.h"
#include "detailspaneresizegrip.h"
#include "extensionutils.h"
#include "itemwidget.h"
#include "overlayscrollbars.h"
#include "pathutils.h"
#include "uiconstants/detailspaneconstants.h"
#include "uiconstants/icons.h"
#include "uiconstants/metadata.h"
#include "uiconstants/timing.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"
#include <QLoggingCategory>
#include <QScrollBar>

#include <QPointer>
#include <QPolygon>

namespace {
// RAII per-phase timer for setMetadata's KARTEND_PERF_TRACE breakdown.
// When enabled, starts on construction and writes the elapsed ms into
// `out` on scope exit; a no-op when tracing is off. Collapses the
// repeated `QElapsedTimer t; if (trace) t.start(); …; if (trace) out =
// t.elapsed();` scaffolding each phase carried.
class PhaseTimer {
public:
  PhaseTimer(bool enabled, qint64 &out) : m_out(enabled ? &out : nullptr) {
    if (m_out) m_timer.start();
  }
  ~PhaseTimer() {
    if (m_out) *m_out = m_timer.elapsed();
  }
  PhaseTimer(const PhaseTimer &) = delete;
  PhaseTimer &operator=(const PhaseTimer &) = delete;

private:
  QElapsedTimer m_timer;
  qint64 *m_out;
};

// Kartend-4wxmp: the per-phase accumulators + the KARTEND_PERF_TRACE breakdown
// emit that setMetadata used to declare inline (one bool, five qint64s, and a
// ten-line reporting block). Folding it into one RAII object lets setMetadata
// read as orchestration: `SetMetadataPhaseTrace trace{filePath};` up top, each
// PhaseTimer writes into a `trace.*` field, and the breakdown prints from the
// destructor on scope exit.
struct SetMetadataPhaseTrace {
  explicit SetMetadataPhaseTrace(QString tracePath)
      : path(std::move(tracePath)), enabled(qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {}
  ~SetMetadataPhaseTrace() {
    if (!enabled) return;
    const qint64 sum = fileInfo + previewSize1 + loadArtwork + video + tabVis;
    if (sum > 5) {
      qCDebug(lcPerfTrace).nospace()
          << "DetailsPane::setMetadata phases: sum=" << sum << " (fileInfo=" << fileInfo
          << " previewSize1=" << previewSize1 << " loadArtwork=" << loadArtwork
          << " video=" << video << " tabVis=" << tabVis << ") path=" << path;
    }
  }
  SetMetadataPhaseTrace(const SetMetadataPhaseTrace &) = delete;
  SetMetadataPhaseTrace &operator=(const SetMetadataPhaseTrace &) = delete;

  QString path;
  bool enabled;
  qint64 fileInfo = 0, previewSize1 = 0, loadArtwork = 0, video = 0, tabVis = 0;
};
} // namespace

Q_LOGGING_CATEGORY(lcDetailsPane, "kartend.detailspane")

// Creates metadata sidebar with scrollable layout for displaying item
// information and artwork
DetailsPane::DetailsPane(QWidget *parent) : QWidget(parent), ui(new Ui::DetailsPane) {
  ui->setupUi(this);
  // Trailing zero-stretch expanding spacer (Kartend-6i10t user report "File
  // page can be tightened up"): with the details container hidden (File tab,
  // item view) nothing in the content column claimed the leftover height, so
  // Qt spread it BETWEEN the file-info rows — path/size/modified drifted
  // apart across the whole pane. Zero stretch keeps it inert whenever the
  // details container (stretch 1) is visible; when that hides, this spacer
  // is the only expanding item and the rows compact at the top.
  if (auto *contentCol = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout())) {
    contentCol->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
  }
  setAutoFillBackground(true);
  // bug #6: stop mouse events on the sidebar from propagating up
  // to ancestors. The previous Overlay-mode implementation relied on Qt's
  // default hit-testing alone, but the sidebar sits as a sibling of the grid
  // — a moveEvent that crossed from the grid into the sidebar could still
  // race the grid's hover-select tracking. WA_NoMousePropagation makes the
  // sidebar a solid stop for unhandled mouse events.
  setAttribute(Qt::WA_NoMousePropagation);

  // NB: we deliberately do NOT pin QPalette::Window on the DetailsPane itself.
  // paintEvent fills the whole rect (m_bgColor, or palette(Window) as the
  // default-background fallback) and applyAppearance sets autoFillBackground
  // false, so an explicit Window pin here would only freeze that fallback
  // against a later system/theme palette change — leave it inheriting.

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

  // Kartend-nk4v7: the constructor stays the orchestrator; the embedded
  // widgets, signal wiring, and the video-debounce timer are built by three
  // helpers in the same order the inlined blocks ran. setupWidgets() builds
  // the video preview tile (which setupConnections / setupVideo and the final
  // clearMetadata() depend on), so it must run first.
  setupWidgets();
  setupConnections();
  setupVideo();

  clearMetadata();
}

void DetailsPane::setupWidgets() {
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
  // Viewport resizes drive constrainContentToViewport (see eventFilter).
  if (ui->scrollArea && ui->scrollArea->viewport()) {
    ui->scrollArea->viewport()->installEventFilter(this);
    constrainContentToViewport();
  }
  m_videoPlayback.videoPreview->installEventFilter(this);

  setupTabBar();

  // The grip controller owns every piece of state the previous in-line
  // implementation kept on DetailsPane (drag flags + start positions).
  // Lock state and dock position are kept in sync by applyAppearance.
  m_resizeGrip = new DetailsPaneResizeGrip(this, this);

  // Vertical-dock media gallery view. Lazy-builds its widgets on the first
  // setEntries() call. Forwards its public events (Edit click + overlay
  // visibility transitions) as DetailsPane signals.
  m_galleryView = new DetailsPaneGalleryView(this);
  m_galleryView->setHost(this);
  // Inject the content column + preview-tile anchors the gallery section
  // builds against. Narrow-setter wiring instead of friend access — the
  // helper only needs layout placement, not host state.
  m_galleryView->setHostAnchors(ui->contentWidget, ui->artworkDisplay,
                                m_videoPlayback.videoPreview);

  // Inline edit-metadata button (Kartend-oewu) — mirrors the gallery's
  // editRequested wiring. Icon resolves from the active theme; the .ui
  // form leaves it blank because Qt Designer can't express runtime
  // theme lookups. (The click→editMetadataRequested wiring lives in
  // setupConnections.)
  if (ui->editMetadataButton) {
    ui->editMetadataButton->setIcon(UIConstants::Icons::fromTheme(
        {UIConstants::Icons::EDIT, "edit-entry", "accessories-text-editor"}));
    ui->editMetadataButton->setText(QString());
  }

  // Artwork + video-preview helper (Kartend-5nxz). State lives on the
  // host (m_videoPlayback etc.); the helper owns the methods so the .cpp
  // shrinks without disturbing the ~100 existing access sites.
  m_artworkController = new DetailsPaneArtwork(this);
  m_artworkController->setHost(this);
  // Scroll-idle gate for the gallery's deferred video-thumb extraction —
  // forwards the artwork controller's isScrollIdle() so the gallery view
  // doesn't need friend access to reach it. "Idle when no controller"
  // mirrors the old direct-reach default.
  m_galleryView->setScrollIdlePredicate(
      [this]() { return !m_artworkController || m_artworkController->isScrollIdle(); });

  // Details-section helper (Kartend-cd2u). Same shape: state stays on
  // the host (m_detailsContainer, m_detailsLayout, etc.); the helper
  // owns the lazy-build + per-selection rebuild methods.
  m_metadataView = new DetailsPaneMetadataView(this);
  m_metadataView->setHost(this);

  // itemNameValue now lives in the compact title row (right-aligned next
  // to the section heading). The original block centered it under the
  // artwork tile and wrapped long names; in the new layout we want a
  // single line that elides on overflow rather than expanding the row.
  ui->itemNameValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  ui->itemNameValue->setWordWrap(false);
  applyContentAlignment();
  applyPreviewSize();

  // Pre-warm the gallery section's lazy widget construction so the cost
  // (~2.5s on a slow filesystem — Kartend-jxp5) lands in startup instead
  // of the user's first-click critical path. Section is hidden until
  // setEntries populates it; prewarming has no UI consequence beyond the
  // up-front allocation. Safe to call after setHostAnchors above —
  // ensureSection's insertion-index calculation reads the injected video
  // tile as the anchor for placing the gallery container below it.
  if (m_galleryView) {
    m_galleryView->prewarmSection();
  }
}

void DetailsPane::setupConnections() {
  // Resize-grip drag/commit forwards. The grip controller owns the live
  // drag bookkeeping; these surface its width/height drag + commit events
  // as DetailsPane signals for DetailsPaneManager to persist.
  connect(m_resizeGrip, &DetailsPaneResizeGrip::widthDragged, this, &DetailsPane::widthDragged);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::widthCommitted, this, &DetailsPane::widthCommitted);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::heightDragged, this, &DetailsPane::heightDragged);
  connect(m_resizeGrip, &DetailsPaneResizeGrip::heightCommitted, this,
          &DetailsPane::heightCommitted);

  // Gallery-view events (Edit click + overlay visibility transitions)
  // forwarded as DetailsPane signals.
  connect(m_galleryView, &DetailsPaneGalleryView::editRequested, this,
          &DetailsPane::editArtworkRequested);
  connect(m_galleryView, &DetailsPaneGalleryView::overlayVisibilityChanged, this,
          &DetailsPane::galleryOverlayVisibilityChanged);

  // Inline edit-metadata button click (Kartend-oewu) — mirrors the gallery's
  // editRequested wiring.
  if (ui->editMetadataButton) {
    connect(ui->editMetadataButton, &QToolButton::clicked, this,
            &DetailsPane::editMetadataRequested);
  }
}

void DetailsPane::setupVideo() {
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
      m_videoPlayback.videoStartTimer->start(UIConstants::Timing::UI_SETTLE_RETRY_MS);
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

  // Per-phase perf timers (Kartend-5ux9 follow-up) — the outer perfTrace
  // showed setMeta=200-240ms tail even after loadArtwork went async, so
  // the cost is in some other call inside this function. Each PhaseTimer
  // below records its phase into `trace`; the breakdown prints from
  // `trace`'s destructor when KARTEND_PERF_TRACE=1 (Kartend-4wxmp).
  SetMetadataPhaseTrace trace(filePath);

  m_hasItemDisplayed = true;
  // An item now carries the selection — any subcollection overview is
  // stale (Kartend-um69l), and so is the previous item's owner summary
  // (the manager re-pushes the new owner right after when it differs
  // from the viewed collection, Kartend-6i10t).
  m_selectionSummary = CollectionSummary{};
  m_ownerSummary = CollectionSummary{};
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
    PhaseTimer pt(trace.enabled, trace.fileInfo);
    updateFileInfo(filePath);
  }

  QFileInfo fileInfo(filePath);
  const QString baseName = fileInfo.completeBaseName();

  // Drop the previous selection's cached artwork so applyPreviewSize falls
  // back to the empty placeholder while the new image is resolved.
  m_artworkSource = QPixmap();
  m_primaryArtworkPath.clear();
  {
    PhaseTimer pt(trace.enabled, trace.previewSize1);
    applyPreviewSize();
  }

  // Try collection's artwork directory first if provided
  {
    PhaseTimer pt(trace.enabled, trace.loadArtwork);
    // Kartend-4wxmp: talk to the artwork controller directly (the pass-through
    // DetailsPane::loadArtwork forwarder was removed).
    if (m_artworkController) {
      if (!artworkDirectory.isEmpty()) {
        m_artworkController->loadArtwork(baseName, artworkDirectory);
      } else {
        // Fallback to sibling "artwork" directory
        const QDir fileDir = fileInfo.dir();
        const QString siblingArtworkDir = fileDir.absolutePath() + "/artwork";
        m_artworkController->loadArtwork(baseName, siblingArtworkDir);
      }
    }
  }

  // Resolve + apply the preview video (see detailspanevideo.cpp). A single
  // phase now; the per-lookup sub-timings were only needed to find the
  // original setMeta tail.
  {
    PhaseTimer pt(trace.enabled, trace.video);
    applyPreviewVideo(filePath, artworkDirectory, videoDirectory);
  }

  // Defer all section visibility to applyTabVisibility() so each tab
  // ends up with its own distinct widget set.
  {
    PhaseTimer pt(trace.enabled, trace.tabVis);
    applyTabVisibility();
  }
  // `trace`'s destructor emits the KARTEND_PERF_TRACE phase breakdown.
}

// Clears per-item state and re-asserts visibility. Each tab now owns its
// own no-selection display (Item: "No item selected" placeholder, File:
// "-" placeholders, Collection: summary regardless of selection), so the
// dispatch happens in applyTabVisibility() rather than here.
void DetailsPane::clearMetadata() {
  m_hasItemDisplayed = false;
  // Deselection also ends any subcollection selection — fall back to the
  // current collection's overview (Kartend-um69l). showSubcollectionSummary
  // re-pushes a child summary right after this when a tile IS selected.
  // The owner summary dies with the item it described (Kartend-6i10t).
  m_selectionSummary = CollectionSummary{};
  m_ownerSummary = CollectionSummary{};
  m_currentItemName.clear();
  m_currentMetadataTitle.clear();

  // Tear down item-only chrome (artwork preview, video, gallery, details
  // rows, manual button) regardless of which mode we land in.
  schedulePreviewVideo(QString());
  // Kartend-4wxmp: removed pass-through forwarders — drive the sub-controllers
  // directly.
  if (m_artworkController) m_artworkController->showArtworkOnly();
  setManualFile(QString());
  if (m_detailsContainer) {
    if (m_metadataView) m_metadataView->clearDetailsSection();
    m_detailsContainer->hide();
  }
  setArtworkEditEnabled(false);
  setArtworkGallery({});

  // Reset textual placeholders the Item and File tabs use when no item
  // is selected. The Collection tab ignores these and re-renders its
  // summary inside applyTabVisibility().
  ui->itemNameValue->setText(tr("No item selected"));
  m_currentFilePath.clear();
  // Drop any in-flight stat worker (bump the generation) and clear the cached
  // result so a switch to the File tab after deselection can't re-apply the
  // previous item's size/modified (Kartend-kujy5).
  ++m_fileInfoGen;
  m_fileStatDisplay = FileStatDisplay{};
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

// setCollectionSummary / renderCollectionSummary live in
// detailspanesummary.cpp; setupTabBar / setActiveTab / applyTabVisibility
// and the setArtworkSectionVisible / setFileInfoRowsVisible toggles live
// in detailspanetabs.cpp — same-class sibling TUs, no state moved.

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

  // New selection: invalidate the cached stat result so a switch to the File
  // tab before the worker delivers can't re-apply the previous item's data
  // (Kartend-kujy5).
  m_fileStatDisplay = FileStatDisplay{};

  // Show placeholders for the stat-derived fields until the worker delivers.
  // On slow mounts (network/USB) a single QFileInfo::exists/size/lastModified
  // can take 50-260ms; pre-fix this dominated DetailsPane::setMetadata's
  // total cost (Kartend-5ux9 measurement run).
  ui->fileSizeValue->setText(QStringLiteral("…"));
  ui->lastModifiedValue->setText(QStringLiteral("…"));

  QString extension = QFileInfo(filePath).suffix().toUpper();
  if (extension.isEmpty()) {
    extension = tr("Unknown");
  }
  // tr the whole "%1 file" phrase rather than concatenating " file": the
  // suffix word order isn't fixed across languages.
  ui->fileExtensionValue->setText(tr("%1 file").arg(extension));

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
    // Cache the resolved values, then paint them only while the rows are
    // actually on screen. Off-screen the cache is enough — applyTabVisibility()
    // and setFileInfoRowsVisible() re-apply it when the rows come back, so the
    // result is never lost and a not-found verdict never leaks onto a hidden
    // tab to surface stale later (Kartend-kujy5).
    m_fileStatDisplay.resolved = true;
    m_fileStatDisplay.exists = res.exists;
    if (res.exists) {
      m_fileStatDisplay.sizeText = DetailsFormat::formatFileSize(res.size);
      m_fileStatDisplay.modifiedText = res.lastModified.toString("yyyy-MM-dd hh:mm:ss");
    } else {
      m_fileStatDisplay.sizeText = QStringLiteral("-");
      m_fileStatDisplay.modifiedText = QStringLiteral("-");
    }
    // Kartend-e7xte: gate on whether the rows are SHOWING, not on the File
    // tab. The Item tab surfaces the same rows as its unscraped fallback
    // (DetailsPaneMetadataView::…setFileInfoRowsVisible(true)), so a
    // tab-only gate left every unscraped item displaying the '…' placeholder
    // for good — which is most items in a fresh library.
    if (fileInfoRowsShowing()) {
      applyFileStatDisplay();
    }
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

void DetailsPane::applyFileStatDisplay() {
  if (!m_fileStatDisplay.resolved) {
    return; // worker hasn't delivered yet — leave the '…' placeholders
  }
  if (!m_fileStatDisplay.exists) {
    // Not-found is the only async case that also overrides the path/extension
    // the synchronous prelude set; the exists case leaves those untouched.
    ui->filePathValue->setText(tr("File not found"));
    ui->fileExtensionValue->setText(QStringLiteral("-"));
  }
  ui->fileSizeValue->setText(m_fileStatDisplay.sizeText);
  ui->lastModifiedValue->setText(m_fileStatDisplay.modifiedText);
}

// Kartend-5nxz: previewBoxSize / applyPreviewSize / pausePreviewVideo /
// resumePreviewVideo / togglePreviewVideoPause / loadArtwork /
// showArtworkOnly / schedulePreviewVideo / setScrollIdlePredicate /
// isScrollIdle moved to DetailsPaneArtwork. The public-API entry points
// stay here as thin delegations so external callers (mainwindow_*.cpp,
// DetailsPaneManager, integration tests) keep compiling without changes.

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
  if (m_artworkController) m_artworkController->applyPreviewSize();
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

void DetailsPane::updateFilePathDisplay() {
  if (m_currentFilePath.isEmpty()) {
    return;
  }
  // set the full path; QLabel wordWrap=true (set in
  // updateFileInfo) handles per-character wrapping so the entire path is
  // visible without truncation.
  ui->filePathValue->setText(m_currentFilePath);
}

void DetailsPane::constrainContentToViewport() {
  if (!ui || !ui->scrollArea || !ui->scrollArea->viewport() || !ui->contentWidget) {
    return;
  }
  // The pane scrolls VERTICALLY only (horizontalScrollBarPolicy=AlwaysOff in
  // the .ui), so content wider than the viewport cannot be reached — it is
  // simply lost. Worse, it silently moved everything: a long unbreakable
  // metadata value (rom_sha1, a full filename) sets a large minimum width on
  // the details grid, which pushed contentWidget to 410px inside a 314px
  // viewport. Children then centred themselves in 410 rather than in what the
  // user can see — measured 2026-08-20: artwork at x=58 instead of x=10, i.e.
  // a 48px band of empty pane between the grid and the artwork, and the
  // header pill running 76px past the right edge.
  //
  // Capping the widget at the viewport width makes every child lay out inside
  // the VISIBLE area. Over-long values are squeezed/elided by their own
  // labels instead of dragging the whole pane sideways.
  const int visible = ui->scrollArea->viewport()->width();
  if (visible > 0) {
    ui->contentWidget->setMaximumWidth(visible);
  }
}

void DetailsPane::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);

  // Kartend-z0dob. constrainContentToViewport() caps contentWidget at the
  // viewport width, and contentWidget is what every child lays out inside — so
  // one stale cap clips the artwork, the title bar and the metadata grid at
  // once, with no horizontal scrollbar to reach what was cut.
  //
  // At startup the pane is resized to the collection's sidebarWidth while it
  // is still HIDDEN (measured in the guest: the pane row reported
  // `detailsPaneWidget w=300 max=300 vis=0` at that moment). A hidden widget's
  // scroll area does not lay out, so the viewport still reported the old width
  // when resizeEvent's constrain read it — cap latched at the stale value —
  // and because no further pane resize followed, and the viewport's own
  // settling resize never reached the eventFilter arm either, the cap stayed
  // wrong for the rest of the session. Toggling the pane with F9 fixed it
  // permanently, which is precisely this seam being reached by hand.
  //
  // Re-cap on show, then again one turn later: the show-triggered layout pass
  // is what finally gives the viewport its real width, and that happens after
  // this handler returns.
  constrainContentToViewport();
  // Deferred one event-loop turn because the layout pass that show() schedules
  // has not run yet — reading the viewport here still returns the pre-show
  // width, which is the very staleness this exists to correct.
  QTimer::singleShot(0, this, [this]() { constrainContentToViewport(); });
}

void DetailsPane::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  // Re-cap the content on the PANE's own resize, not only the viewport's. The
  // pane is built at FIXED_WIDTH and resized later to the collection's
  // sidebarWidth (DetailsPaneManager::applyDockSizing), and until the cap
  // caught up, contentWidget stayed pinned to the construction width while the
  // pane around it was wider — so a widened pane clipped its own artwork and
  // metadata against a stale bound, with no horizontal scrollbar to reach what
  // was cut. Cheap: setMaximumWidth is a no-op when the value is unchanged.
  constrainContentToViewport();

  // Permanent diagnostic for "the gap is in the details pane" reports. Every
  // x is relative to the PANE's own left edge, so a non-zero value is dead
  // space this widget is responsible for and a zero is not. Enable with:
  //   QT_LOGGING_RULES='kartend.detailspane.debug=true'
  if (lcDetailsPane().isDebugEnabled()) {
    const auto rel = [this](QWidget *w) { return w ? w->mapTo(this, QPoint(0, 0)).x() : -1; };
    QScrollBar *vbar = ui->scrollArea->verticalScrollBar();
    QWidget *win = window();
    qCDebug(lcDetailsPane).nospace()
        << "PANEGAP paneWinX=" << (win ? mapTo(win, QPoint(0, 0)).x() : -1) << " contentWinX="
        << ((win && ui->contentWidget) ? ui->contentWidget->mapTo(win, QPoint(0, 0)).x() : -1)
        << " | paneW=" << width() << " | scrollArea x=" << rel(ui->scrollArea)
        << " w=" << ui->scrollArea->width() << " | viewport x=" << rel(ui->scrollArea->viewport())
        << " w=" << ui->scrollArea->viewport()->width()
        << " | contentWidget x=" << rel(ui->contentWidget) << " w=" << ui->contentWidget->width()
        << " | titleBar x=" << rel(ui->titleBar) << " w=" << ui->titleBar->width()
        << " | artwork x=" << rel(ui->artworkDisplay) << " w=" << ui->artworkDisplay->width()
        << " | vbar visible=" << (vbar && vbar->isVisible()) << " w=" << (vbar ? vbar->width() : 0)
        << " | lane=" << OverlayScrollbars::reservedGutter(ui->scrollArea);

    // Dump the ROW the pane lives in. A gap between two adjacent widgets in a
    // zero-spacing layout means either a third item nobody remembers, or a
    // widget refusing to grow into the space the layout offers it.
    if (QWidget *parent = parentWidget()) {
      if (QLayout *row = parent->layout()) {
        QString dump;
        for (int i = 0; i < row->count(); ++i) {
          QLayoutItem *it = row->itemAt(i);
          if (!it) continue;
          if (QWidget *w = it->widget()) {
            dump +=
                QStringLiteral(" [%1 x=%2 w=%3 max=%4 vis=%5]")
                    .arg(w->objectName().isEmpty() ? w->metaObject()->className() : w->objectName())
                    .arg(w->x())
                    .arg(w->width())
                    .arg(w->maximumWidth())
                    .arg(w->isVisible() ? 1 : 0);
          } else if (it->spacerItem()) {
            dump += QStringLiteral(" [SPACER w=%1]").arg(it->geometry().width());
          }
        }
        qCDebug(lcDetailsPane).nospace()
            << "PANEROW parentW=" << parent->width() << " count=" << row->count() << dump;
      }
    }
  }

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

// Kartend-4wxmp: removed 7 pass-through forwarders that had no callers or only
// internal DetailsPane callers — loadArtwork / showArtworkOnly / isScrollIdle
// (→ m_artworkController), ensureDetailsSection / clearDetailsSection /
// appendDetailRow / appendScrollingDescription (→ m_metadataView). Their few
// internal callers now talk to the sub-controller directly. setScrollIdlePredicate,
// setExtendedMetadata, setUsageStats and setManualFile stay as delegations
// because DetailsPaneManager (not a friend) still calls them.
void DetailsPane::setScrollIdlePredicate(std::function<bool()> predicate) {
  if (m_artworkController) m_artworkController->setScrollIdlePredicate(std::move(predicate));
}

void DetailsPane::setOverlayLayerManager(OverlayZOrderRegistry *manager) {
  if (m_galleryView) m_galleryView->setLayerManager(manager);
}

void DetailsPane::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  if (m_metadataView) m_metadataView->setExtendedMetadata(metadata);
}

void DetailsPane::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
  if (m_metadataView) m_metadataView->setUsageStats(stats);
}

// Kartend-cd2u: ensureManualButton / setManualFile / openCurrentManual
// moved to DetailsPaneMetadataView. setManualFile stays here as a thin
// delegation; the two private helpers no longer exist on DetailsPane
// (their declarations also dropped from detailspane.h).
void DetailsPane::setManualFile(const QString &manualPath) {
  if (m_metadataView) m_metadataView->setManualFile(manualPath);
}

// ─── Pure formatter wrappers ──────────────────────────────────────────────
// The bodies live in DetailsFormat (utils/view/detailsformat.h) so the
// logic isn't duplicated with DetailPageOverlay / UsageStatsStore. These
// statics stay because downstream callers and the integration tests reach
// the formatters through DetailsPane; internal call sites (and the family
// helper TUs) use DetailsFormat directly.
QString DetailsPane::formatFileSize(qint64 bytes) {
  return DetailsFormat::formatFileSize(bytes);
}

QString DetailsPane::formatRuntime(int seconds) {
  return DetailsFormat::formatRuntime(seconds);
}

QString DetailsPane::formatTags(const QString &raw) {
  return DetailsFormat::formatTags(raw);
}

QString DetailsPane::formatPersonalRating(int rating) {
  return DetailsFormat::formatPersonalRating(rating);
}

QString DetailsPane::formatLastScanned(const QDateTime &lastScanned) {
  return DetailsFormat::formatLastScanned(lastScanned);
}
