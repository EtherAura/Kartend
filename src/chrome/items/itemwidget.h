#ifndef ITEMWIDGET_H
#define ITEMWIDGET_H

#include <QColor>
#include <QElapsedTimer>
#include <QFrame>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QSize>
#include <QTimer>

#include "ui_itemwidget.h"
#include <QString>

class QLabel;
class QMouseEvent;
class QEnterEvent;
class QPaintEvent;
class QShowEvent;
class QResizeEvent;
class QPushButton;

class ItemWidget : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ItemWidget)
  Q_PROPERTY(qreal pulseOpacity READ pulseOpacity WRITE setPulseOpacity)
  bool isSelectedState = false;
  QPropertyAnimation *pulseAnimation = nullptr;
  qreal m_pulseOpacity = 1.0;
  QPixmap storedPixmap;
  // Kartend-63wg: true when storedPixmap is a worker-composed final card (set
  // via setComposedArtwork) rather than raw artwork — onArtworkChanged then
  // sets it straight through instead of re-scaling + re-compositing.
  bool m_storedIsComposed = false;

public:
  /// Kartend-63wg: the inputs onArtworkChanged uses to compose a tile's card,
  /// snapshotted so the artwork worker can do that compositing off the GUI
  /// thread. An empty labelSize means the artwork label isn't laid out yet.
  struct ArtworkRenderSpec {
    QSize labelSize;
    int cornerRadius = 0;
    QColor background;
  };
  [[nodiscard]] ArtworkRenderSpec artworkRenderSpec() const;
  explicit ItemWidget(QWidget *parent = nullptr);
  ~ItemWidget() override;

  void setItemName(const QString &name);
  void setFilePath(const QString &path);
  void setArtworkPixmap(const QPixmap &pixmap);
  /// Kartend-63wg: set an already-composed final card (corner-masked, sized to
  /// the artwork label) produced by the artwork worker. onArtworkChanged sets
  /// it straight onto the label with no further scale/composite.
  void setComposedArtwork(const QPixmap &card);
  void setPlaceholderArtworkPixmap(const QPixmap &pixmap);
  virtual void setSelected(bool selected);
  void resetForReuse();

  [[nodiscard]] const QString &getItemName() const { return itemName; }
  [[nodiscard]] const QString &getFilePath() const { return filePath; }
  [[nodiscard]] QRect selectionBorderRectInParent() const;
  [[nodiscard]] bool isSelected() const { return isSelectedState; }

  void setPulseOpacity(qreal opacity);
  [[nodiscard]] qreal pulseOpacity() const { return m_pulseOpacity; }

  void setItemDimensions(int width, int height);
  void setFontSize(int fontSize);
  void setHideTitles(bool hide);
  void setHideSubcollectionTitles(bool hide);
  void setCornerRadius(int radius);
  void setListMode(bool listMode);
  void setRowIndex(int index) { m_rowIndex = index; }
  void setCollectionName(const QString &name);
  void setHasArtwork(bool hasArtwork);
  void setArtworkDirectory(const QString &dir) { m_artworkDirectory = dir; }
  void setCollectionColumnWidth(int width);
  void setArtworkColumnWidth(int width);
  [[nodiscard]] bool isListMode() const { return m_isListMode; }
  [[nodiscard]] bool hasArtwork() const { return m_hasArtwork; }
  [[nodiscard]] int collectionColumnWidth() const { return m_collectionColumnWidth; }
  [[nodiscard]] int artworkColumnWidth() const { return m_artworkColumnWidth; }
  int m_itemWidth;
  int m_itemHeight;
  int m_artworkSize = 0;  // Computed artwork size for triangle indicator positioning
  int m_fontSize = 12;    // Default font size
  int m_cornerRadius = 0; // Corner radius for artwork clipping
  bool m_hideTitles = false;
  bool m_hideSubcollectionTitles = false;
  bool m_hideSubfolderTitle = false;
  QLabel *imageLabel = nullptr;
  QLabel *nameLabel = nullptr;
  QWidget *triangleIndicator;
  void setAsSubcollection(int index, const QString &name);
  void setAsVirtualFolder(const QString &folderPath, const QString &displayName,
                          bool hideTitle = false);
  [[nodiscard]] bool isSubcollection() const { return m_isSubcollection; }
  [[nodiscard]] bool isVirtualFolder() const { return m_isVirtualFolder; }
  [[nodiscard]] const QString &virtualFolderPath() const { return m_virtualFolderPath; }
  void applyTitleTint();
  static QColor titleTint();
  QColor m_titleTintColor; // Cached tint color for custom painting

  /// build a placeholder-style hatched tile at the given size
  /// using the app palette + s_tileColor + s_titleTintLightness. Static so
  /// the metadata sidebar can render the exact same hatch the per-item
  /// placeholder uses for "missing artwork" tiles. `cornerRadius` masks the
  /// corners (0 = no rounding); the sidebar passes 0. `applyGradient`
  /// controls the vertical fade overlay — items keep it (true) so the tile
  /// matches the legacy look; the sidebar disables it (false) so a single
  /// tall pattern doesn't get a stretched, washed-out gradient.
  /// `baseOverride` lets a caller substitute its own widget-instance Mid
  /// color (default = QApplication::palette() Mid). Pass an invalid color
  /// to use the default. `lineAlphaScale` (0.0–1.0) scales both
  /// primary/secondary line alphas — the sidebar uses ~0.5 because the
  /// lines run much longer at sidebar scale than on a small item card and
  /// were perceived as too bright at full PRIMARY_ALPHA.
  static QPixmap buildPlaceholderTile(int width, int height, int cornerRadius = 0,
                                      bool applyGradient = true,
                                      const QColor &baseOverride = QColor(),
                                      double lineAlphaScale = 1.0);

  // Static configuration for title appearance (set from GeneralSettings)
  static void setTitleTintSaturation(int saturation);
  static void setTitleTintLightness(int lightness);
  static void setTitleBaseColor(const QString &hexColor);
  static void setCustomFontFamily(const QString &fontFamily);
  static void setPrimaryColor(const QString &hexColor);
  static void setTileColor(const QString &hexColor);
  static void setSelectionColor(const QString &hexColor);
  static void setListRowColor(const QString &hexColor);
  static void setListAltRowColor(const QString &hexColor);
  // when true, onArtworkChanged draws itemName centered on the
  // placeholder pixmap. Toggle is read at render time, so a setting flip
  // followed by a viewport refresh updates every visible tile.
  static void setShowTitleInPlaceholder(bool enabled);

  // ─── Per-item state-flag registry (Kartend-elte) ──────────────────────
  // The grid renders pinned/hidden/continue-later glyph badges over each
  // tile's top-right corner. MainWindow populates the registry once per
  // collection switch with the result of
  // IDatabaseManager::loadItemStateFlagsForCollection; the paint path
  // consults it via the item's filePath. Keeping the lookup table static
  // avoids threading a context pointer through every widget pool slot.
  struct StateFlags {
    bool pinned = false;
    bool hidden = false;
    bool continueLater = false;
    [[nodiscard]] bool any() const { return pinned || hidden || continueLater; }
  };
  /// Replace the registry contents. Repaints every visible ItemWidget so
  /// the new flags appear without waiting for the next scroll event.
  static void setStateFlagsRegistry(const QHash<QString, StateFlags> &flags);
  /// Convenience for unit-tests / hot-update flows: clears the registry
  /// so the next paint shows no badges.
  static void clearStateFlagsRegistry();
  static int s_titleTintSaturation;
  static int s_titleTintLightness;
  static QString s_titleBaseColor;
  static QString s_customFontFamily;
  static QString s_primaryColor;
  static QString s_tileColor;
  static QString s_selectionColor;
  static QString s_listRowColor;
  static QString s_listAltRowColor;
  static bool s_showTitleInPlaceholder;

  void mousePressEvent(QMouseEvent *event) override;

  void onArtworkChanged();

  /// Paint the pinned/hidden/continue-later badges in @p painter's
  /// coordinate space. Called from paintEvent after the artwork + title
  /// are drawn so the glyphs sit on top. Public for unit-test access; the
  /// grid path uses paintEvent.
  void paintStateBadges(QPainter &painter);

signals:
  // Only subcollectionDoubleClicked is used - EventManager intercepts all other
  // clicks
  void subcollectionDoubleClicked(int index);
  void virtualFolderDoubleClicked(const QString &folderPath);
  // Emitted when artwork preview button is clicked in list mode
  void artworkPreviewRequested(const QString &filePath, const QString &artworkDir);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  void enterEvent(QEnterEvent *event) override;
#else
  void enterEvent(QEvent *event) override;
#endif
  void leaveEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  QString itemName;
  QString filePath;
  QString m_collectionName;   // Parent collection name for list mode display
  QString m_artworkDirectory; // Artwork directory for this item's collection
  QPixmap m_placeholderArtworkPixmap;
  bool m_isSubcollection = false;
  int m_subcollectionIndex = -1;
  bool m_isVirtualFolder = false;
  bool m_isListMode = false;         // True when displaying in list view (no artwork)
  bool m_hasArtwork = false;         // True when artwork exists for this item
  int m_rowIndex = -1;               // Row index for alternating background colors in list mode
  int m_collectionColumnWidth = 150; // Collection column width for list mode (resizable)
  int m_artworkColumnWidth = 32;     // Artwork column width for list mode (resizable)
  QString m_virtualFolderPath;
  QLabel *m_collectionLabel = nullptr;    // Collection name label for list mode
  QLabel *m_folderIconLabel = nullptr;    // Folder icon for subcollection/virtual folder
  QPushButton *m_artworkButton = nullptr; // Artwork preview button for list mode
  void updateTriangleIndicator();
  void paintTriangleIndicator();
  [[nodiscard]] QPixmap buildPlaceholderPattern(int width, int height) const;
  /// paints `itemName` centered on @p pixmap with the title tint
  /// color and a dark backing for legibility. No-op when itemName is empty or
  /// the pixmap has zero area. @p dpr lets the caller scale the font when the
  /// pixmap is in physical pixels (placeholder pattern path uses dpr=1, the
  /// scaled-artwork path uses the screen DPR).
  void drawTitleOnPlaceholder(QPixmap &pixmap, qreal dpr = 1.0) const;
  void setupPulseAnimation();
  void startPulseAnimation();
  QTimer m_pulseDelayTimer; // Kartend-a911.6: value member
  QElapsedTimer m_lastClickTimer;
  QPoint m_lastClickPos;
  static constexpr int DOUBLE_CLICK_TIMEOUT_MS = 300;
  static constexpr int CLICK_POSITION_TOLERANCE = 5;

  [[nodiscard]] bool isGlideActive() const;
  [[nodiscard]] QRect computeSelectionBorderRect() const;
  void applySelectedUiEffects();
  void applyDeselectedUiEffects();
  void scheduleSelectionBorderUpdate();
  void applyDimensions();
};

#endif // ITEMWIDGET_H
