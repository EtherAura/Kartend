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
#include <QFrame>
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
#include <QTabBar>
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
#include "pathutils.h"
#include "uiconstants/detailspaneconstants.h"
#include "uiconstants/icons.h"
#include "uiconstants/metadata.h"
#include "uiconstants/timing.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"

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

  // Inline edit-metadata button (Kartend-oewu) — mirrors the gallery's
  // editRequested wiring. Icon resolves from the active theme; the .ui
  // form leaves it blank because Qt Designer can't express runtime
  // theme lookups.
  if (ui->editMetadataButton) {
    ui->editMetadataButton->setIcon(UIConstants::Icons::fromTheme(
        {UIConstants::Icons::EDIT, "edit-entry", "accessories-text-editor"}));
    ui->editMetadataButton->setText(QString());
    connect(ui->editMetadataButton, &QToolButton::clicked, this,
            &DetailsPane::editMetadataRequested);
  }

  // Artwork + video-preview helper (Kartend-5nxz). State lives on the
  // host (m_videoPlayback etc.); the helper owns the methods so the .cpp
  // shrinks without disturbing the ~100 existing access sites.
  m_artworkController = new DetailsPaneArtwork(this);
  m_artworkController->setHost(this);

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

  // Per-phase perf timers (Kartend-5ux9 follow-up) — the outer perfTrace
  // showed setMeta=200-240ms tail even after loadArtwork went async, so
  // the cost is in some other call inside this function. Each PhaseTimer
  // below records its phase into `trace`; the breakdown prints from
  // `trace`'s destructor when KARTEND_PERF_TRACE=1 (Kartend-4wxmp).
  SetMetadataPhaseTrace trace(filePath);

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

  // Kartend-4wxmp: drive the metadata view directly (the pass-through
  // ensureDetailsSection/clearDetailsSection/appendDetailRow forwarders were
  // removed). ensureDetailsSection() is what creates m_detailsContainer, so a
  // non-null container past the guard implies m_metadataView is non-null.
  if (m_metadataView) m_metadataView->ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  DetailsPaneMetadataView *mv = m_metadataView;
  mv->clearDetailsSection();

  if (!m_collectionSummary.type.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Type"), m_collectionSummary.type);
  }
  if (m_collectionSummary.itemCount >= 0) {
    mv->appendDetailRow(tr("Items"), QString::number(m_collectionSummary.itemCount));
  }
  mv->appendDetailRow(tr("Last scanned"), formatLastScanned(m_collectionSummary.lastScanned));
  if (!m_collectionSummary.parentName.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Parent"), m_collectionSummary.parentName);
  }
  mv->appendDetailRow(tr("Media"), m_collectionSummary.mediaDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Artwork"), m_collectionSummary.artworkDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Video"), m_collectionSummary.videoDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Manuals"), m_collectionSummary.manualDirectory, /*wrap=*/true);
  if (!m_collectionSummary.extensions.isEmpty()) {
    mv->appendDetailRow(tr("Extensions"), m_collectionSummary.extensions.join(QStringLiteral(", ")),
                        /*wrap=*/true);
  }
  // Kartend-ecky: persistent warning surface for launcher paths that
  // don't resolve on this host. One row per offending launcher so a
  // multi-launcher collection makes it clear which entry needs fixing.
  for (const QString &issue : m_collectionSummary.launcherPathIssues) {
    mv->appendDetailRow(tr("⚠ Launcher path"), issue, /*wrap=*/true);
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
  return DetailsFormat::formatLastScanned(lastScanned);
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
    // Cache the resolved values, then paint them only while the File tab is
    // showing. Off-tab the cache is enough — applyTabVisibility() re-applies it
    // when the user switches to the File tab, so the result is never lost and a
    // not-found verdict never leaks onto a hidden tab to surface stale later
    // (Kartend-kujy5).
    m_fileStatDisplay.resolved = true;
    m_fileStatDisplay.exists = res.exists;
    if (res.exists) {
      m_fileStatDisplay.sizeText = formatFileSize(res.size);
      m_fileStatDisplay.modifiedText = res.lastModified.toString("yyyy-MM-dd hh:mm:ss");
    } else {
      m_fileStatDisplay.sizeText = QStringLiteral("-");
      m_fileStatDisplay.modifiedText = QStringLiteral("-");
    }
    if (m_activeTab == DetailsPaneTab::File) {
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

// Human-readable file size (KB/MB/GB). Delegates to the shared StringUtils
// helper so the logic isn't duplicated with DetailPageOverlay (Kartend-kp7up).
auto DetailsPane::formatFileSize(qint64 bytes) -> QString {
  return DetailsFormat::formatFileSize(bytes);
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

void DetailsPane::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  if (m_metadataView) m_metadataView->setExtendedMetadata(metadata);
}

void DetailsPane::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
  if (m_metadataView) m_metadataView->setUsageStats(stats);
}

QString DetailsPane::formatRuntime(int seconds) {
  return DetailsFormat::formatRuntime(seconds);
}

// Kartend-cd2u: ensureManualButton / setManualFile / openCurrentManual
// moved to DetailsPaneMetadataView. setManualFile stays here as a thin
// delegation; the two private helpers no longer exist on DetailsPane
// (their declarations also dropped from detailspane.h).
void DetailsPane::setManualFile(const QString &manualPath) {
  if (m_metadataView) m_metadataView->setManualFile(manualPath);
}

QString DetailsPane::formatPersonalRating(int rating) {
  return DetailsFormat::formatPersonalRating(rating);
}

QString DetailsPane::formatTags(const QString &raw) {
  return DetailsFormat::formatTags(raw);
}
