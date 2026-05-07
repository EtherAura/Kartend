#ifndef DETAILSPANE_H
#define DETAILSPANE_H

#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "collectionutils.h"
#include "itemmetadata.h"
#include "ui_detailspane.h"
#include "usagestatsstore.h"

class ArtworkPreviewOverlay;
class VideoPreviewWidget;
QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QPushButton;
class QTabBar;
class QTimer;
QT_END_NAMESPACE

class DetailsPane : public QWidget {
  Q_OBJECT
public:
  explicit DetailsPane(QWidget *parent = nullptr);
  ~DetailsPane();
  void setMetadata(const QString &filePath, const QString &itemName,
                   const QString &artworkDirectory = QString(),
                   const QString &videoDirectory = QString());
  /// Renders extended metadata in a Details section. Hides the section
  /// entirely when `metadata.isEmpty()` so empty rows do not clutter the UI.
  void setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata);
  /// Renders per-item usage statistics (Kartend-7vi) appended to the Details
  /// section: play count, last played, total time played. Empty rows are
  /// skipped so an item that has never been launched shows nothing.
  void setUsageStats(const UsageStatsStore::ItemUsageStats &stats);
  /// Sets the resolved manual file path for the current selection. When
  /// non-empty, a "Manual" button appears in the Details section that opens
  /// the file via QDesktopServices::openUrl (xdg-open on Linux). Pass an
  /// empty path to hide the button. Independent from `setExtendedMetadata`
  /// so callers can show a manual button on items with no other metadata.
  void setManualFile(const QString &manualPath);
  /// Renders the media gallery for the current selection (Kartend-un3l +
  /// Kartend-ljey). Each entry carries a display label, absolute file path,
  /// and an `isVideo` flag; videos use an async-extracted frame as the
  /// thumbnail (with a placeholder play icon while extraction runs) and
  /// open the video preview overlay on click. Pass an empty list to hide
  /// the section.
  struct GalleryEntry {
    QString label;
    QString path;
    bool isVideo = false;
  };
  void setArtworkGallery(const QList<GalleryEntry> &entries);
  /// Controls whether the gallery section's edit affordance is shown
  /// (Kartend-53vk). Independent of `setArtworkGallery` so the "Edit links…"
  /// button stays available for items with no current artwork — that is the
  /// exact case where the user wants to add a manual link.
  void setArtworkEditEnabled(bool enabled);
  /// Snapshot of collection-level fields rendered when no item is selected
  /// (Kartend-3mn). Empty `name` clears the cached summary, restoring the
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
    [[nodiscard]] bool isValid() const { return !name.trimmed().isEmpty(); }
  };
  /// Caches the collection summary used when no item is selected. The
  /// summary is rendered immediately if the sidebar is currently in the
  /// no-selection state; otherwise it is held until the user deselects.
  void setCollectionSummary(const CollectionSummary &summary);
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);
  /// Kartend-63e bug #5: stops the sidebar's preview video and cancels any
  /// pending start so it doesn't keep playing while a fullscreen overlay
  /// (artwork preview / expand mode) shows its own video. Idempotent — safe
  /// to call regardless of the current playback state.
  void pausePreviewVideo();
  /// Kartend-cjry: toggle the sidebar preview video between playing and
  /// paused. Returns true if a toggle occurred (i.e. a video is loaded),
  /// false when there's nothing to toggle.
  bool togglePreviewVideoPause();

  /// Kartend-63e: applies per-collection sidebar appearance — background
  /// type / color / image / pattern, text color, accent color. Called by
  /// DetailsPaneManager whenever the active collection changes or settings are
  /// saved. Triggers a repaint.
  void applyAppearance(const CollectionConfig &collection);

  /// Kartend-63e: switches the active built-in sidebar tab. Item shows the
  /// per-item view; Collection forces the summary regardless of selection;
  /// File displays a placeholder until custom panes are wired up.
  void setActiveTab(DetailsPaneTab tab);
  [[nodiscard]] DetailsPaneTab activeTab() const { return m_activeTab; }

signals:
  /// Fired when the user activates the gallery's "Edit links…" button
  /// (Kartend-53vk). The sidebar widget itself has no item context, so the
  /// owning manager handles persistence and refresh.
  void editArtworkRequested();

  /// Kartend-63e bug #7: forwards the gallery overlay's visibility so
  /// DetailsPaneManager can lower the sidebar while the overlay is on top. Only
  /// fires for the sidebar's own gallery overlay; the expand-mode overlay
  /// owned by SelectionDisplayManager has its own wiring.
  void galleryOverlayVisibilityChanged(bool visible);

  /// Kartend-63e: fired during a width drag with the new candidate width
  /// in pixels (already clamped to MIN/MAX). DetailsPaneManager applies it
  /// live; the matching widthCommitted() at drag-release is what triggers
  /// the actual settings save.
  void widthDragged(int width);
  /// Kartend-63e: fired when the width drag is released. The integer value
  /// is the final width; DetailsPaneManager persists it via SettingsManager.
  void widthCommitted(int width);
  /// Kartend-u2gx: height equivalents for Top/Bottom dock. Same lifecycle as
  /// the width pair — heightDragged() during the drag, heightCommitted() at
  /// release for persistence.
  void heightDragged(int height);
  void heightCommitted(int height);
  /// Kartend-63e: emitted when the user clicks a sidebar tab. DetailsPaneManager
  /// persists the new active tab to the current collection.
  void activeTabChanged(DetailsPaneTab tab);

protected:
  /// Kartend-63e bug #4: re-elide the file path when the sidebar is resized
  /// so the path always fits the available width without losing the start /
  /// end portions to a fixed-length right-truncation.
  void resizeEvent(QResizeEvent *event) override;
  /// Kartend-63e: render the per-collection background (color, image, or
  /// procedurally-drawn pattern) before children paint.
  void paintEvent(QPaintEvent *event) override;
  /// Kartend-63e width drag handle. Mouse press / move / release on the
  /// inner edge (left edge in Right position, right edge in Left position)
  /// resize the sidebar live while the lock is off. The grip strip is
  /// `WIDTH_GRIP_PX` wide and not rendered as a separate widget — the
  /// cursor change + hit-test on the parent handles it cleanly.
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;
  /// Kartend-u2gx: inner widgets (scrollArea, contentWidget, gallery thumbs)
  /// can swallow mouse events that fall in the grip zone before they reach
  /// the panel itself. We install ourselves as their event filter so we can
  /// intercept press/move/release in the grip area and run the resize logic
  /// no matter which descendant happened to be under the cursor.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setupUI();
  void updateFileInfo(const QString &filePath);
  void updateFilePathDisplay();
  /// Kartend-63e: build + install the bubble-bg stylesheet on the content
  /// widget. Empty hex disables the corresponding bubble. Stylesheet
  /// selectors target the existing label objectNames in detailspane.ui.
  void applyBubbleStyles(const QString &headerHex, const QString &sectionHex);
  /// Kartend-ekaa: capture every label's designer-set font once so the per-
  /// collection sidebar-font override can layer on top without losing the
  /// baseline weight/pointSize hierarchy. Refilled lazily on first use.
  void captureLabelFontBaselines();
  /// Kartend-ekaa: apply the per-collection sidebar font override.
  /// Empty @p family means "keep each label's baseline family" (i.e. fall
  /// back to the application font); @p pointSize == 0 means "keep each
  /// label's baseline size." Calling this with both blanks restores the
  /// original .ui fonts, which is what makes the override revertible.
  void applySidebarFont(const QString &family, int pointSize);
  /// Kartend-xcfr: re-asserts AlignHCenter on the artwork/name region so the
  /// stylesheet polish in applyAppearance() can't drop those items back to
  /// flush-left. The file-info rows, gallery card, and Details rows stay
  /// flush-left intentionally — only the artwork preview, video preview,
  /// "Artwork" header, "Name:" label, and the item name value sit on the
  /// center axis.
  void applyContentAlignment();
  /// Kartend-xcfr: scale the artwork + video preview boxes to the sidebar's
  /// current content width so they grow/shrink with the user's drag-handle
  /// resize. Re-renders the cached artwork pixmap at the new size to avoid
  /// scaling artifacts. Capped at the .ui's designer max so a very wide
  /// sidebar doesn't show a giant preview that pushes the file info off-
  /// screen.
  void applyPreviewSize();
  [[nodiscard]] int previewBoxSize() const;
  /// Kartend-u2gx: flip the contentLayout direction (TopToBottom / LeftToRight),
  /// scrollbar policies, and inter-section separator orientation based on the
  /// active dock edge. Called from applyAppearance after m_position is set.
  void applyDockOrientation();
  /// Kartend-u2gx: target gallery-thumb size in the current dock orientation.
  /// In horizontal dock the thumbs scale to match the artwork preview so
  /// they read as proportional siblings; in vertical dock they fall back to
  /// the .ui's compact GALLERY_THUMB_SIZE constant.
  [[nodiscard]] int currentGalleryThumbSize() const;
  /// Kartend-u2gx: walk existing gallery thumb buttons and resize them to
  /// match `currentGalleryThumbSize()`. Cheap to call after a dock change /
  /// resize event; setIconSize handles the pixmap rescale internally.
  void applyGalleryThumbSize();
  /// Kartend-u2gx: build the dedicated horizontal-dock view. Lazy-created on
  /// first switch into Top/Bottom dock so users who never dock horizontally
  /// don't pay for the extra widgets.
  void setupHorizontalView();
  /// Kartend-u2gx: push current selection state (item name, file info,
  /// artwork pixmap, gallery entries) into m_horizontalView's widgets.
  /// Called on selection change, on dock-orientation change, and on resize.
  void updateHorizontalView();
  /// Kartend-u2gx: target square size for the horizontal view's preview tile
  /// (artwork + video). Scales with panel viewport height minus the chrome
  /// the horizontal view's own QHBoxLayout contributes (margins + spacing).
  [[nodiscard]] int horizontalPreviewSize() const;
  /// Kartend-u2gx: rebuild the horizontal view's gallery thumb strip from
  /// `m_galleryEntries`, filtering out video entries (the live preview tile
  /// already plays the video). Runs on every gallery / orientation change.
  void rebuildHorizontalGallery();
  void loadArtwork(const QString &baseName, const QString &artworkDirectory);
  void schedulePreviewVideo(const QString &videoPath);
  void showArtworkOnly();
  void ensureDetailsSection();
  void clearDetailsSection();
  void appendDetailRow(const QString &label, const QString &value, bool wrap = false);
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  [[nodiscard]] static QString formatRuntime(int seconds);
  [[nodiscard]] static QString formatTags(const QString &raw);
  Ui::DetailsPane *ui;
  VideoPreviewWidget *m_videoPreview = nullptr;
  QTimer *m_videoStartTimer = nullptr;
  QString m_pendingVideoPath;
  /// Kartend-xcfr: original-resolution artwork pixmap kept so applyPreviewSize
  /// can re-render the centered/scaled artworkDisplay pixmap when the sidebar
  /// is resized, instead of re-reading the file from disk on every resize.
  QPixmap m_artworkSource;
  /// Kartend-63e bug #4: full file path of the current selection, kept so
  /// resizeEvent can re-elide it width-aware without re-querying the model.
  QString m_currentFilePath;
  /// Kartend-63e: cached appearance state copied from CollectionConfig so
  /// paintEvent doesn't need to reach back into the manager / model layer.
  DetailsPaneBackgroundType m_bgType = DetailsPaneBackgroundType::Color;
  QColor m_bgColor;
  QPixmap m_bgImage;
  DetailsPanePattern m_bgPattern = DetailsPanePattern::Crosshatch;
  int m_patternIntensity = 50; // 0–100 % alpha multiplier for pattern lines
  QColor m_patternColor;
  /// Kartend-63e width drag state. WidthLocked false enables a draggable
  /// grip on the inner edge; while m_widthDragging is true, mouseMove
  /// emits widthDragged() and mouseRelease emits widthCommitted().
  /// Kartend-u2gx: m_heightDragging is the Top/Bottom equivalent — same lock
  /// flag governs both, and only one of the two is ever active at once
  /// because the active edge is determined by m_position.
  bool m_widthLocked = true;
  DetailsPanePosition m_position = DetailsPanePosition::Right;
  bool m_widthDragging = false;
  int m_dragStartWidth = 0;
  int m_dragStartX = 0;
  bool m_heightDragging = false;
  int m_dragStartHeight = 0;
  int m_dragStartY = 0;
  [[nodiscard]] bool isOnGrip(const QPoint &posInWidget) const;

  /// Kartend-63e tabs. The tab bar is created programmatically and inserted
  /// at the top of mainLayout so the .ui file stays unchanged.
  QTabBar *m_tabBar = nullptr;
  QLabel *m_filePlaceholder = nullptr;
  DetailsPaneTab m_activeTab = DetailsPaneTab::Item;
  void setupTabBar();
  void applyTabVisibility();

  // Dynamically-built "Details" section appended to the content layout.
  // Built lazily on first use so existing layouts (and tests that don't show
  // the sidebar) are unaffected.
  QWidget *m_detailsContainer = nullptr;
  QVBoxLayout *m_detailsLayout = nullptr;
  QLabel *m_detailsTitle = nullptr;

  // Manual button (Kartend-9jdv). Persisted across selection changes so the
  // button doesn't appear in the middle of the Details list — it's always
  // pinned at the top of the section when a manual is available.
  QPushButton *m_manualButton = nullptr;
  QString m_manualPath;
  void ensureManualButton();
  void openCurrentManual();

  // Artwork gallery (Kartend-un3l). Lazy-built on first non-empty
  // setArtworkGallery() call. Sits between the artwork preview pane and the
  // file information section so all visual artwork stays clustered at the
  // top of the sidebar.
  QWidget *m_galleryContainer = nullptr;
  QHBoxLayout *m_galleryLayout = nullptr;
  /// Wraps the thumbnail row so the section title + edit button can stay
  /// visible while the thumbs are hidden (Kartend-53vk).
  QWidget *m_galleryThumbsHost = nullptr;
  QPushButton *m_galleryEditButton = nullptr;
  bool m_galleryEditEnabled = false;
  /// Kartend-u2gx: cached entries from the most recent setArtworkGallery
  /// call. Used to rebuild the horizontal-view gallery whenever the user
  /// switches dock orientation, without callers having to re-push the list.
  QList<GalleryEntry> m_galleryEntries;
  /// Kartend-u2gx: cached item name from the most recent setMetadata call.
  /// The .ui's itemNameValue holds the same string but the horizontal view
  /// reads from this so it doesn't matter whether the vertical labels have
  /// been mutated by tab logic ("Name:" vs "Collection:").
  QString m_currentItemName;

  // ─── Dedicated horizontal-dock view (Kartend-u2gx) ───────────────────────
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
  /// Kartend-u2gx: Edit button lives at the far right of the panel, not
  /// inside the gallery — keeps its position consistent regardless of how
  /// many thumbs render. Toggled by setArtworkEditEnabled.
  QPushButton *m_hEditButton = nullptr;
  // Owned-on-demand preview overlay reparented to the top-level window so it
  // can cover the full UI rather than just the narrow sidebar. nullptr until
  // the first thumbnail is clicked.
  ArtworkPreviewOverlay *m_galleryOverlay = nullptr;
  void ensureGallerySection();
  void clearGallerySection();
  void openGalleryPreview(const GalleryEntry &entry);

  // Collection-summary state (Kartend-3mn). The summary is cached on the
  // widget so every existing clearMetadata() call site can fall through to
  // a meaningful no-selection display without each caller re-pushing it.
  // m_hasItemDisplayed tracks whether the sidebar is currently rendering
  // an item — refreshes that arrive mid-selection update the cache silently
  // and apply on next deselect.
  CollectionSummary m_collectionSummary;
  bool m_hasItemDisplayed = false;
  /// Kartend-ekaa: designer-set font baseline per label, captured once. Stored
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

  void renderCollectionSummary();
  void applyItemViewVisibility(bool visible);
  [[nodiscard]] static QString formatLastScanned(const QDateTime &lastScanned);
  /// Builds the small placeholder shown for video tiles while their frame
  /// is being extracted (and as a permanent fallback when extraction
  /// fails). Renders a play triangle on a muted-tile background sized to
  /// `iconSize`.
  [[nodiscard]] QPixmap makeVideoPlaceholder(int iconSize) const;
};

#endif