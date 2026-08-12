#ifndef DETAILSPANE_H
#define DETAILSPANE_H

// Kartend-dk2c.3 (audit-2026-05-22 — included-cull pass): pulled QFrame,
// QLabel, QPointer, QScrollArea, QVBoxLayout out of the public header. They
// were only needed by pointer-member declarations, which forward-declare
// fine. QFont stays because m_appearance.font is a value member that needs
// the complete type. Kept QString / QStringList / QList because they appear
// in by-value method parameters that downstream callers expect to "just
// work" off this header. The full pane-extraction (ArtworkPane /
// MetadataPane / GalleryPane) is queued under Kartend-dk2c.7.
#include <functional>
#include <QDateTime>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "collection/collectionconfig.h"
#include "idetailspane.h"
#include "itemmetadata.h"
#include "stateutils.h"
#include "ui_detailspane.h"
#include "usagestatsstore.h"

class ArtworkPreviewOverlay;
class DetailsPaneArtwork;
class DetailsPaneGalleryView;
class DetailsPaneMetadataView;
class DetailsPaneResizeGrip;
class OverlayZOrderRegistry;
template <typename T> class QFutureWatcher;
class VideoPreviewWidget;
QT_BEGIN_NAMESPACE
class QFrame;
class QHBoxLayout;
class QPushButton;
class QScrollArea;
class QTabBar;
QT_END_NAMESPACE

class DetailsPane : public QWidget, public IDetailsPane {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailsPane)
  // NB: DetailsPaneGalleryView is deliberately NOT a friend — it owns its
  // own widgets and receives the content column / preview-tile anchors and
  // the scroll-idle gate via narrow setters in setupWidgets.
  // Artwork + video-preview helper (Kartend-5nxz). Coupling rationale:
  // reads/writes m_videoPlayback / m_artworkSource / m_primaryArtworkPath /
  // m_artworkLoadGen and the .ui-owned artwork display widget. State stays
  // on the host so the other helpers and ~100 existing access sites are
  // unchanged.
  friend class DetailsPaneArtwork;
  // Metadata-rows helper (Kartend-cd2u). Same friend-of-host pattern:
  // reads/writes m_detailsContainer / m_detailsLayout / m_descriptionScroll /
  // m_metadataScroll / m_metadataAutoScrollTimer / m_manualButton /
  // m_manualPath / m_currentMetadataTitle. State stays on the host.
  friend class DetailsPaneMetadataView;

public:
  explicit DetailsPane(QWidget *parent = nullptr);
  ~DetailsPane();

  // Pure utility formatters used by both the panel and downstream callers
  // (and exercised directly in tests). State-free — thin delegations to
  // DetailsFormat (utils/view/detailsformat.h), where the bodies live.
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  [[nodiscard]] static QString formatRuntime(int seconds);
  [[nodiscard]] static QString formatTags(const QString &raw);
  /// Renders a personal rating (0-10 internal, half-star precision) as a
  /// glyph string with a parenthetical fraction, e.g. "★★★★☆ (3.5 / 5)".
  /// Returns an empty string for the "unrated" sentinel (-1) so the
  /// Details section can skip the row entirely.
  [[nodiscard]] static QString formatPersonalRating(int rating);
  [[nodiscard]] static QString formatLastScanned(const QDateTime &lastScanned);

  /// Install a predicate the video-preview start timer consults before
  /// firing playVideo (Kartend-9q8d round 6). When the predicate returns
  /// true, the timer restarts itself with a short re-check interval
  /// instead of running playVideo's GUI-thread-blocking
  /// stop+setSource+play sequence (which can lock the GUI for ~100ms
  /// while QMediaPlayer/GStreamer initializes the pipeline). Without
  /// this, a wheel scroll that lands on a video item triggers playVideo
  /// mid-glide — the QMediaPlayer block visibly stutters the still-
  /// running scroll animation. DetailsPane has no manager/ctx access;
  /// the predicate is the cleanest way to forward the scroll-state
  /// check (DetailsPaneManager → InteractionStateHolder) without
  /// dragging the whole context tree into the widget. Default predicate
  /// returns false (preserves existing behaviour when not wired).
  void setScrollIdlePredicate(std::function<bool()> predicate);

  /// Forward the central overlay z-order coordinator to the gallery view so
  /// its lazily-created fullscreen preview overlay registers at
  /// Layer::ArtworkPreview instead of stacking by raw raise(). DetailsPane
  /// has no manager/ctx access (see setScrollIdlePredicate), so
  /// DetailsPaneManager pushes the registry down during setupReferences().
  void setOverlayLayerManager(OverlayZOrderRegistry *manager);

  void setMetadata(const QString &filePath, const QString &itemName,
                   const QString &artworkDirectory = QString(),
                   const QString &videoDirectory = QString());
  /// Renders extended metadata in a Details section. Hides the section
  /// entirely when `metadata.isEmpty()` so empty rows do not clutter the UI.
  void setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata);
  /// Renders per-item usage statistics appended to the Details
  /// section: play count, last played, total time played. Empty rows are
  /// skipped so an item that has never been launched shows nothing.
  void setUsageStats(const UsageStatsStore::ItemUsageStats &stats);
  /// Sets the resolved manual file path for the current selection. When
  /// non-empty, a "Manual" button appears in the Details section that opens
  /// the file via QDesktopServices::openUrl (xdg-open on Linux). Pass an
  /// empty path to hide the button. Independent from `setExtendedMetadata`
  /// so callers can show a manual button on items with no other metadata.
  void setManualFile(const QString &manualPath);
  /// Renders the media gallery for the current selection.
  /// Each entry carries a display label, absolute file path,
  /// and an `isVideo` flag; videos use an async-extracted frame as the
  /// thumbnail (with a placeholder play icon while extraction runs) and
  /// open the video preview overlay on click. Pass an empty list to hide
  /// the section.
  /// Per-item gallery entry alias — the canonical shape lives in
  /// `src/api/idetailspane.h` so module consumers can mirror the pane's
  /// gallery without pulling in this concrete widget header. Kept as a
  /// member alias so the swarm of existing `DetailsPane::GalleryEntry`
  /// references (DetailsPaneGalleryView, the horizontal-dock helper, etc.)
  /// keep compiling.
  using GalleryEntry = DetailsPaneGalleryEntry;
  void setArtworkGallery(const QList<GalleryEntry> &entries);
  /// Swap the main artwork preview tile to a specific gallery entry.
  /// Used by the gallery-thumb click handler so the user can flip the
  /// big preview between cover / screenshot / fanart / etc. without
  /// opening the full-screen overlay. For video entries this routes
  /// through the same debounced schedule path setMetadata uses; for
  /// images it loads the pixmap into `m_artworkSource` and re-renders
  /// at the current preview size. Passing an entry whose path no
  /// longer exists is a no-op (the existing tile stays in place).
  void showMainPreviewForEntry(const GalleryEntry &entry);
  /// Cycle the main preview to the next/previous entry in the gallery.
  /// `direction` is +1 (next) or -1 (previous); wraps at both ends.
  /// No-op when fewer than 2 gallery entries exist. Bound to Left/Right
  /// arrow keys via keyPressEvent so the user can flip artwork without
  /// reaching for the mouse.
  void cycleMainPreview(int direction);
  /// Snapshot of the most recently pushed gallery entries (the same list
  /// the sidebar's thumb strip is rendering). Used by the expand-mode
  /// ArtworkPreviewOverlay to mirror the sidebar gallery in its own strip
  /// without re-running the DB → resolve cycle. Empty list when no item
  /// is selected or when the item has no resolvable artwork.
  [[nodiscard]] QList<DetailsPaneGalleryEntry> currentGalleryEntries() const override;
  /// Controls whether the gallery section's edit affordance is shown
  /// Independent of `setArtworkGallery` so the "Edit links…"
  /// button stays available for items with no current artwork — that is the
  /// exact case where the user wants to add a manual link.
  void setArtworkEditEnabled(bool enabled);
  /// Snapshot of collection-level fields rendered when no item is selected
  /// Empty `name` clears the cached summary, restoring the
  /// legacy "No item selected" placeholder layout.
  struct CollectionSummary {
    QString name;
    QString type;
    qint64 itemCount = -1; // -1 = unknown / not yet computed
    QDateTime lastScanned;
    QString mediaDirectory;
    QString artworkDirectory;
    QString videoDirectory;
    QString manualDirectory;
    QStringList extensions;
    QString parentName;
    /// Kartend-ecky: persistent warning lines surfacing launcher paths
    /// that don't resolve on this host (typical for kart bundles imported
    /// from a different machine). Each entry is a human-readable
    /// "<field>: <path> <reason>" line ready to display verbatim; empty
    /// list = no issues, omit the section. Recomputed on every summary
    /// refresh via PathUtils::checkLauncherPath so the section clears the
    /// moment the user reinstalls the missing binary and the sidebar
    /// re-renders.
    QStringList launcherPathIssues;
    [[nodiscard]] bool isValid() const { return !name.trimmed().isEmpty(); }
  };
  /// Caches the collection summary used when no item is selected. The
  /// summary is rendered immediately if the sidebar is currently in the
  /// no-selection state; otherwise it is held until the user deselects.
  void setCollectionSummary(const CollectionSummary &summary);
  /// Test-only readback of the cached summary, so suites can assert what
  /// DetailsPaneManager pushed (e.g. the launcherPathIssues verdict) without
  /// scraping rendered rows.
  [[nodiscard]] const CollectionSummary &collectionSummaryForTesting() const {
    return m_collectionSummary;
  }
  void clearMetadata() override;
  [[nodiscard]] QWidget *asWidget() override { return this; }
  [[nodiscard]] const QWidget *asWidget() const override { return this; }
  /// bug #5: silences the sidebar's preview video and cancels any
  /// pending start so it doesn't keep playing while a fullscreen overlay
  /// (artwork preview / expand mode) shows its own video. Soft-pause via
  /// QWidget::hide() — the loaded source is preserved so a follow-up
  /// resumePreviewVideo() picks up at the same playback position. Idempotent
  /// — safe to call regardless of the current playback state.
  void pausePreviewVideo();
  /// reverse of pausePreviewVideo(): re-shows the video widget so its
  /// showEvent restarts playback from the paused position. When the prior
  /// pause caught the debounce mid-flight (path scheduled but not yet
  /// loaded), re-arms the start timer so the original flow runs again.
  /// No-op when nothing was scheduled or playing in the first place.
  void resumePreviewVideo();
  /// toggle the sidebar preview video between playing and
  /// paused. Returns true if a toggle occurred (i.e. a video is loaded),
  /// false when there's nothing to toggle.
  bool togglePreviewVideoPause();

  /// applies per-collection sidebar appearance — background
  /// type / color / image / pattern, text color, accent color. Called by
  /// DetailsPaneManager whenever the active collection changes or settings are
  /// saved. Triggers a repaint.
  /// reloadBackground=false (Kartend-gzptz) keeps the already-decoded sidebar
  /// background pixmap and only re-applies colours/pattern/accent — used for a
  /// colour-only re-theme so the wallpaper isn't re-decoded (and doesn't flash
  /// blank) on every system-accent change.
  void applyAppearance(const CollectionConfig &collection, bool reloadBackground = true);

  /// switches the active built-in sidebar tab. Item shows the
  /// per-item view; Collection forces the summary regardless of selection;
  /// File displays a placeholder until custom panes are wired up.
  void setActiveTab(DetailsPaneTab tab);
  [[nodiscard]] DetailsPaneTab activeTab() const { return m_activeTab; }

signals:
  /// Fired when the user activates the gallery's "Edit links…" button
  /// The sidebar widget itself has no item context, so the
  /// owning manager handles persistence and refresh.
  void editArtworkRequested();

  /// Fired when the user clicks the inline edit-metadata button in the
  /// title row. Mirrors editArtworkRequested — the manager looks up the
  /// current selection and pops the EditMetadataDialog.
  void editMetadataRequested();

  /// bug #7: forwards the gallery overlay's visibility so
  /// DetailsPaneManager can lower the sidebar while the overlay is on top. Only
  /// fires for the sidebar's own gallery overlay; the expand-mode overlay
  /// owned by SelectionDisplayManager has its own wiring.
  void galleryOverlayVisibilityChanged(bool visible);

  /// fired during a width drag with the new candidate width
  /// in pixels (already clamped to MIN/MAX). DetailsPaneManager applies it
  /// live; the matching widthCommitted() at drag-release is what triggers
  /// the actual settings save.
  void widthDragged(int width);
  /// fired when the width drag is released. The integer value
  /// is the final width; DetailsPaneManager persists it via SettingsManager.
  void widthCommitted(int width);
  /// height equivalents for Top/Bottom dock. Same lifecycle as
  /// the width pair — heightDragged() during the drag, heightCommitted() at
  /// release for persistence.
  void heightDragged(int height);
  void heightCommitted(int height);
  /// emitted when the user clicks a sidebar tab. DetailsPaneManager
  /// persists the new active tab to the current collection.
  void activeTabChanged(DetailsPaneTab tab);

protected:
  /// bug #4: re-elide the file path when the sidebar is resized
  /// so the path always fits the available width without losing the start /
  /// end portions to a fixed-length right-truncation.
  void resizeEvent(QResizeEvent *event) override;
  /// render the per-collection background (color, image, or
  /// procedurally-drawn pattern) before children paint.
  void paintEvent(QPaintEvent *event) override;
  /// width drag handle. Mouse press / move / release on the
  /// inner edge (left edge in Right position, right edge in Left position)
  /// resize the sidebar live while the lock is off. The grip strip is
  /// `WIDTH_GRIP_PX` wide and not rendered as a separate widget — the
  /// cursor change + hit-test on the parent handles it cleanly.
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;
  /// inner widgets (scrollArea, contentWidget, gallery thumbs)
  /// can swallow mouse events that fall in the grip zone before they reach
  /// the panel itself. We install ourselves as their event filter so we can
  /// intercept press/move/release in the grip area and run the resize logic
  /// no matter which descendant happened to be under the cursor.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setupUI();
  // Constructor decomposition (Kartend-nk4v7). The constructor stays the
  // orchestrator (.ui setup + palette/attribute pins) and delegates the bulk
  // of its work to these three single-responsibility helpers, called in the
  // same order the inlined blocks ran.
  // setupWidgets: builds + parents the embedded widgets (video preview tile,
  // tab bar, resize grip, gallery / artwork / metadata helpers, edit button)
  // and applies the initial alignment + preview-size pass.
  void setupWidgets();
  // setupConnections: wires the resize-grip, gallery-view, and edit-metadata
  // button signals through to the matching DetailsPane signals/slots.
  void setupConnections();
  // setupVideo: arms the debounced video-preview start timer (the timeout
  // lambda that DetailsPane's preview pipeline drives).
  void setupVideo();
  void updateFileInfo(const QString &filePath);
  /// Apply the cached async file-stat result (m_fileStatDisplay) to the
  /// size/modified (and, when not-found, path/extension) labels. No-op until
  /// the worker has resolved. Called from the worker, when switching to the
  /// File tab (Kartend-kujy5), and whenever the rows are shown — the Item tab
  /// surfaces the same rows as its unscraped fallback (Kartend-e7xte).
  void applyFileStatDisplay();
  /// Whether the file-info rows are currently shown rather than hidden by
  /// setFileInfoRowsVisible(false). The stat result paints only when they are.
  [[nodiscard]] bool fileInfoRowsShowing() const;
  void updateFilePathDisplay();
  /// build + install the bubble-bg stylesheet on the content
  /// widget. Empty hex disables the corresponding bubble. Stylesheet
  /// selectors target the existing label objectNames in detailspane.ui.
  void applyBubbleStyles(const QString &headerHex, const QString &sectionHex);
  /// capture every label's designer-set font once so the per-
  /// collection sidebar-font override can layer on top without losing the
  /// baseline weight/pointSize hierarchy. Refilled lazily on first use.
  void captureLabelFontBaselines();
  /// apply the per-collection sidebar font override.
  /// Empty @p family means "keep each label's baseline family" (i.e. fall
  /// back to the application font); @p pointSize == 0 means "keep each
  /// label's baseline size." Calling this with both blanks restores the
  /// original .ui fonts, which is what makes the override revertible.
  void applySidebarFont(const QString &family, int pointSize);
  /// re-asserts AlignHCenter on the artwork/name region so the
  /// stylesheet polish in applyAppearance() can't drop those items back to
  /// flush-left. The file-info rows, gallery card, and Details rows stay
  /// flush-left intentionally — only the artwork preview, video preview,
  /// "Artwork" header, "Name:" label, and the item name value sit on the
  /// center axis.
  void applyContentAlignment();
  /// scale the artwork + video preview boxes to the sidebar's
  /// current content width so they grow/shrink with the user's drag-handle
  /// resize. Re-renders the cached artwork pixmap at the new size to avoid
  /// scaling artifacts. Capped at the .ui's designer max so a very wide
  /// sidebar doesn't show a giant preview that pushes the file info off-
  /// screen.
  void applyPreviewSize();
  /// flip the contentLayout direction (TopToBottom / LeftToRight),
  /// scrollbar policies, and inter-section separator orientation based on the
  /// active dock edge. Called from applyAppearance after m_position is set.
  void applyDockOrientation();
  /// target gallery-thumb size in the current dock orientation.
  /// In horizontal dock the thumbs scale to match the artwork preview so
  /// they read as proportional siblings; in vertical dock they fall back to
  /// the .ui's compact GALLERY_THUMB_SIZE constant.
  [[nodiscard]] int currentGalleryThumbSize() const;
  /// walk existing gallery thumb buttons and resize them to
  /// match `currentGalleryThumbSize()`. Cheap to call after a dock change /
  /// resize event; setIconSize handles the pixmap rescale internally.
  void applyGalleryThumbSize();
  /// build the dedicated horizontal-dock view. Lazy-created on
  /// first switch into Top/Bottom dock so users who never dock horizontally
  /// don't pay for the extra widgets.
  void setupHorizontalView();
  /// push current selection state (item name, file info,
  /// artwork pixmap, gallery entries) into m_horizontalView's widgets.
  /// Called on selection change, on dock-orientation change, and on resize.
  void updateHorizontalView();
  /// target square size for the horizontal view's preview tile
  /// (artwork + video). Scales with panel viewport height minus the chrome
  /// the horizontal view's own QHBoxLayout contributes (margins + spacing).
  [[nodiscard]] int horizontalPreviewSize() const;
  /// rebuild the horizontal view's gallery thumb strip from
  /// `m_galleryEntries`, filtering out video entries (the live preview tile
  /// already plays the video). Runs on every gallery / orientation change.
  void rebuildHorizontalGallery();
  void schedulePreviewVideo(const QString &videoPath);
  /// Resolve the preview video for @p filePath — collection `videoDirectory`
  /// first, then `{artworkDirectory}/video/` — and, when the resolved path
  /// changed for this item, hand it to the artwork/video controller. The
  /// unchanged-path guard avoids the hide -> pause -> re-show ping-pong that
  /// setMetadata's repeated firing for one selection would otherwise cause.
  /// Defined in detailspanevideo.cpp.
  void applyPreviewVideo(const QString &filePath, const QString &artworkDirectory,
                         const QString &videoDirectory);
  // Kartend-4wxmp: loadArtwork / showArtworkOnly / isScrollIdle /
  // ensureDetailsSection / clearDetailsSection / appendDetailRow /
  // appendScrollingDescription pass-through forwarders removed — callers use
  // m_artworkController / m_metadataView directly.
  /// Per-item media gallery row (vertical-dock) with section construction,
  /// thumb building, and the click-to-preview overlay. Owned, not borrowed.
  DetailsPaneGalleryView *m_galleryView = nullptr;
  /// Owns the artwork preview + video preview behaviour (Kartend-5nxz).
  /// Constructed in setupUI alongside m_galleryView. Parented to this so
  /// destruction order matches the rest of the helpers. Friend-of-host so
  /// it can reach m_videoPlayback / m_artworkSource etc. directly.
  DetailsPaneArtwork *m_artworkController = nullptr;
  /// Owns the dynamically-built "Details" section — description tile,
  /// metadata grid, usage-stats rows, Manual button (Kartend-cd2u).
  /// Same friend-of-host pattern as m_artworkController.
  DetailsPaneMetadataView *m_metadataView = nullptr;
  Ui::DetailsPane *ui;
  /// Grouped video preview state: the widget, debounce timer, queued
  /// path, and scroll-idle predicate. Migration step toward a future
  /// VideoPlaybackController class — passing this by ref into helpers
  /// keeps the call sites future-proof when extraction lands. See
  /// stateutils.h for member docs.
  VideoPlaybackState m_videoPlayback;
  /// original-resolution artwork pixmap kept so applyPreviewSize
  /// can re-render the centered/scaled artworkDisplay pixmap when the sidebar
  /// is resized, instead of re-reading the file from disk on every resize.
  QPixmap m_artworkSource;
  /// resolved on-disk path of m_artworkSource. setArtworkGallery synthesizes
  /// a thumb entry from this so the legacy "primary art at the top level of
  /// artworkDirectory" layout shows up in the gallery row alongside any
  /// typed-subdirectory thumbs and the video tile.
  QString m_primaryArtworkPath;
  /// Generation counter for async loadArtwork. Each loadArtwork call bumps
  /// the counter and captures its value in the worker callback; if a newer
  /// load has started by the time the worker finishes, the stale result is
  /// dropped instead of overwriting the currently-displayed pixmap. Without
  /// this guard a rapid wheel/arrow burst could deliver an older selection's
  /// image AFTER the new one (out-of-order completion via QThreadPool).
  quint64 m_artworkLoadGen = 0;
  /// Generation counter for async updateFileInfo. Same pattern as
  /// m_artworkLoadGen — drops stale stat() results when a newer selection
  /// has bumped the counter. Critical on slow filesystems (network/USB
  /// mounts) where a single QFileInfo::exists/size/lastModified call can
  /// take 50-260ms.
  quint64 m_fileInfoGen = 0;
  /// Cached result of the async file-stat worker for the current selection.
  /// The worker writes the size/modified labels only while the File tab is
  /// active; otherwise it just caches here, and applyTabVisibility() re-applies
  /// it when the File tab is shown — so a stat that resolves off-tab neither
  /// strands the '…' placeholder nor leaves a stale "File not found" to surface
  /// on a later tab switch (Kartend-kujy5). Reset on every new selection
  /// (updateFileInfo) and on deselect (clearForNoSelection).
  struct FileStatDisplay {
    bool resolved = false; ///< worker has delivered for the current selection
    bool exists = false;
    QString sizeText;
    QString modifiedText;
  };
  FileStatDisplay m_fileStatDisplay;
  /// bug #4: full file path of the current selection, kept so
  /// resizeEvent can re-elide it width-aware without re-querying the model.
  QString m_currentFilePath;
  /// cached appearance state copied from CollectionConfig so
  /// paintEvent doesn't need to reach back into the manager / model layer.
  DetailsPaneBackgroundType m_bgType = DetailsPaneBackgroundType::Color;
  QColor m_bgColor;
  QPixmap m_bgImage;
  // Supersedes in-flight async sidebar-background decodes so a rapid
  // collection switch's slow decode can't overwrite the current one.
  int m_bgImageGeneration = 0;
  DetailsPanePattern m_bgPattern = DetailsPanePattern::Crosshatch;
  int m_patternIntensity = 50; // 0–100 % alpha multiplier for pattern lines
  QColor m_patternColor;
  /// Cache for the diagonal-hatching pattern tile. Pre-fix the Pattern
  /// branch of paintEvent built a fresh sidebar-sized QPixmap on EVERY
  /// repaint via ItemWidget::buildPlaceholderTile (full QPainter pass over
  /// the sidebar dimensions). Sidebar repaints during scroll due to
  /// damage-event cascade from sibling widgets, so per-frame rebuild was
  /// the dominant scroll-judder source (Kartend-9q8d round 5 — the
  /// per-frame cost was invisible to all the per-call instrumentation
  /// because it's pure paint-time work, not measured by our timers).
  /// Keyed via the four scalar inputs that affect the tile so paint can
  /// rebuild only when one of them has changed (size, intensity, palette
  /// Mid). Stored as raw scalars (not a packed key) to avoid any
  /// truncation surprises from bit-shifting QRgb.
  mutable QPixmap m_cachedPatternTile;
  mutable int m_cachedPatternW = -1;
  mutable int m_cachedPatternH = -1;
  mutable int m_cachedPatternIntensity = -1;
  mutable QRgb m_cachedPatternMidRgba = 0;
  /// Cache for the cover-scaled sidebar background image — same idiom as the
  /// pattern-tile cache above, for the Image branch of paintEvent, which has
  /// the identical repaint frequency (sibling damage cascade during scroll)
  /// but re-ran a full SmoothTransformation scale of the wallpaper pixmap on
  /// every repaint. Keyed on the source pixmap's cacheKey() plus the target
  /// width/height: a resize misses on the size, and a background change
  /// misses on the cacheKey (applyAppearance replaces m_bgImage — including
  /// the cleared-then-async-decoded swap — and every new QPixmap gets a
  /// fresh cacheKey), so no extra invalidation hooks are needed.
  mutable QPixmap m_cachedBgScaled;
  mutable qint64 m_cachedBgScaledSrcKey = 0;
  mutable int m_cachedBgScaledW = -1;
  mutable int m_cachedBgScaledH = -1;
  /// Width-lock + dock position state. The lock flag toggles the resize
  /// grip on/off and the position drives both the grip's active edge and
  /// the paint code that draws the handle band. The grip controller below
  /// owns the live drag bookkeeping; isDragging() answers paint code that
  /// needs to skip the handle highlight mid-drag.
  bool m_widthLocked = true;
  DetailsPanePosition m_position = DetailsPanePosition::Right;
  /// Owns the resize-grip state machine: hit-test, drag bookkeeping, and
  /// cursor changes. Parented to this widget. Forwards widthDragged /
  /// widthCommitted / heightDragged / heightCommitted as DetailsPane
  /// signals via wired connections in setupUI().
  DetailsPaneResizeGrip *m_resizeGrip = nullptr;

  /// tabs. The tab bar is created programmatically and inserted
  /// at the top of mainLayout so the .ui file stays unchanged.
  /// Tab-bar construction + the per-tab visibility dispatch below are
  /// defined in detailspanetabs.cpp.
  QTabBar *m_tabBar = nullptr;
  DetailsPaneTab m_activeTab = DetailsPaneTab::Item;
  void setupTabBar();
  void applyTabVisibility();
  /// Per-section visibility toggles used by applyTabVisibility() to
  /// give each tab a distinct payload — the artwork header/preview/video
  /// is owned by the Item tab; the file-info rows belong to the File tab.
  /// Splitting the single legacy applyItemViewVisibility() into two helpers
  /// is what lets a tab show one without the other.
  void setArtworkSectionVisible(bool visible);
  void setFileInfoRowsVisible(bool visible);

  // Dynamically-built "Details" section appended to the content layout.
  // Built lazily on first use so existing layouts (and tests that don't show
  // the sidebar) are unaffected.
  QWidget *m_detailsContainer = nullptr;
  /// QFrame that wraps just the post-description metadata grid (so the
  /// description tile and the rows below it sit on visually distinct
  /// backdrops). applyBubbleStyles paints it a slightly darker shade of
  /// the section color so the rows read as a card under the section
  /// pills above. Nested inside m_metadataScroll so the rows scroll
  /// instead of the outer pane.
  class QFrame *m_metadataBackdrop = nullptr;
  /// Description widgets live outside m_metadataScroll so they stay
  /// pinned in place when the metadata block marquees. Each rebuild
  /// (clearDetailsSection → appendScrollingDescription) tears these
  /// down and rebuilds them with the new selection's text.
  QLabel *m_descriptionLabel = nullptr;
  class QScrollArea *m_descriptionScroll = nullptr;
  /// QScrollArea that holds the metadata card. Has an Expanding vertical
  /// size policy + stretch factor inside m_detailsContainer so it claims
  /// whatever sidebar height is left after the description (and the
  /// section pills above) finish laying out. Auto-scroll timer fires
  /// only when the inner content's height exceeds the viewport's, so
  /// short items just sit flush.
  class QScrollArea *m_metadataScroll = nullptr;
  /// Timer that drives the metadata block's top→bottom auto-scroll.
  /// Owned, restarted on every clearDetailsSection so it tracks the
  /// freshly-rebuilt content rather than the previous selection's
  /// scroll range.
  QTimer m_metadataAutoScrollTimer; // Kartend-a911.6: value member
  /// Ticks remaining before the next scroll step; used to pause briefly
  /// at the top + bottom of each cycle.
  int m_metadataAutoScrollPause = 0;
  /// Two-column grid for short metadata rows ("Genre: Action",
  /// "Players: 1-2", etc.). Short pairs share a row; long values
  /// (or wrap=true rows like the scrolling description) span both
  /// columns so they get the full sidebar width.
  class QGridLayout *m_detailsLayout = nullptr;
  /// Next-cell cursor for the grid layout above. (row, col=0 or 1).
  /// Reset to (0, 0) at the start of every clearDetailsSection().
  int m_detailsRow = 0;
  int m_detailsCol = 0;

  // Manual button. Persisted across selection changes so the
  // button doesn't appear in the middle of the Details list — it's always
  // pinned at the top of the section when a manual is available.
  QPushButton *m_manualButton = nullptr;
  QString m_manualPath;

  /// cached item name from the most recent setMetadata call.
  /// The .ui's itemNameValue holds the same string but the horizontal view
  /// reads from this so it doesn't matter whether the vertical labels have
  /// been mutated by tab logic ("Name:" vs "Collection:").
  QString m_currentItemName;
  /// canonical title from the most recent setExtendedMetadata call. The
  /// Item tab prefers this over m_currentItemName so the user sees
  /// "Donkey Kong Country" instead of "dkc.smc"; the File tab shows the
  /// raw m_currentItemName so the filesystem-oriented view stays honest.
  QString m_currentMetadataTitle;

  // ─── Dedicated horizontal-dock view ───────────────────────
  // m_horizontalView lives alongside the .ui's scrollArea in mainLayout.
  // applyDockOrientation toggles which one is visible. Each child below is
  // owned by m_horizontalView and built lazily by setupHorizontalView().
  QWidget *m_horizontalView = nullptr;
  QWidget *m_hPreviewArea = nullptr;
  QHBoxLayout *m_hPreviewLayout = nullptr;
  QLabel *m_hArtwork = nullptr;
  QLabel *m_hName = nullptr;
  QLabel *m_hPath = nullptr;
  QLabel *m_hSizeValue = nullptr;
  QLabel *m_hModifiedValue = nullptr;
  QLabel *m_hTypeValue = nullptr;
  QWidget *m_hMetaContainer = nullptr; // wraps the QFormLayout for show/hide
  QWidget *m_hGalleryHost = nullptr;
  QHBoxLayout *m_hGalleryLayout = nullptr;
  /// Edit button lives at the far right of the panel, not
  /// inside the gallery — keeps its position consistent regardless of how
  /// many thumbs render. Toggled by setArtworkEditEnabled.
  QPushButton *m_hEditButton = nullptr;
  /// Path-keyed pixmap cache for horizontal-gallery thumbnails. Avoids the
  /// synchronous QPixmap(path) decode on every selection change (the
  /// rebuildHorizontalGallery hot path). Bounded by m_galleryPixmapCacheCap
  /// — when full, the oldest entry by insertion order is evicted via the
  /// FIFO list. 32 covers the realistic max gallery size with headroom.
  QHash<QString, QPixmap> m_galleryPixmapCache;
  QStringList m_galleryPixmapCacheOrder;
  static constexpr int m_galleryPixmapCacheCap = 32;

  // Collection-summary state. The summary is cached on the
  // widget so every existing clearMetadata() call site can fall through to
  // a meaningful no-selection display without each caller re-pushing it.
  // m_hasItemDisplayed tracks whether the sidebar is currently rendering
  // an item — refreshes that arrive mid-selection update the cache silently
  // and apply on next deselect.
  CollectionSummary m_collectionSummary;
  bool m_hasItemDisplayed = false;
  /// designer-set font baseline per label, captured once. Stored
  /// alongside the QPointer so re-parented or deleted labels are skipped on
  /// re-apply without the manager needing to maintain its own bookkeeping.
  struct LabelFontBaseline {
    QPointer<QLabel> label;
    QFont font;
  };
  QList<LabelFontBaseline> m_labelFontBaselines;
  bool m_labelFontBaselinesCaptured = false;
  // Cached current override so dynamically-created labels (detail rows, etc.)
  // can pick it up at construction without re-walking applyAppearance().
  QString m_activeSidebarFontFamily;
  int m_activeSidebarFontPointSize = 0;

  /// Rebuilds the Collection tab's summary card from m_collectionSummary.
  /// Defined in detailspanesummary.cpp (with setCollectionSummary).
  void renderCollectionSummary();
};

#endif