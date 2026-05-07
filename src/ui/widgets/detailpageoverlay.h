#ifndef DETAILPAGEOVERLAY_H
#define DETAILPAGEOVERLAY_H

#include <QList>
#include <QPair>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "itemmetadata.h"
#include "usagestatsstore.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QGridLayout;
class QVBoxLayout;
class QScrollArea;
QT_END_NAMESPACE

/**
 * @brief Full-window detail page for the currently selected item (Kartend-uve).
 *
 * Shows a hero artwork (cycle-able through every available artwork type with
 * Left/Right or the on-image arrows), the item title, full description, a
 * two-column metadata grid (genre / developer / publisher / release / rating
 * / players / runtime / tags / custom fields), file info, usage statistics,
 * and a manual button when one is configured.
 *
 * Pattern mirrors NowPlayingOverlay — the widget is created by MainWindow as a
 * child of the central widget, covers the full window when shown, and is
 * driven by DetailPageManager. Escape / the close button dismisses it.
 */
class DetailPageOverlay : public QWidget {
  Q_OBJECT
public:
  /// One artwork tile available for the hero pane. `label` is the human-
  /// readable type name; `path` is the resolved on-disk file. The list is
  /// rendered in order with Left/Right cycling between entries.
  struct ArtworkEntry {
    QString label;
    QString path;
  };

  /// Bundle of everything the overlay displays. Populated by
  /// DetailPageManager from DatabaseManager + filesystem before show().
  struct Payload {
    QString filePath;
    QString itemName; // Display title (scraped title overrides file stem)
    ItemMetadataStore::ItemMetadata metadata;
    UsageStatsStore::ItemUsageStats usage;
    QString manualPath; // Resolved manual file (empty hides the button)
    QList<ArtworkEntry> artwork;
    qint64 fileSize = -1;  // Bytes; negative means "unknown"
    QString fileModified;  // Pre-formatted modification timestamp
    QString fileExtension; // ".sfc" etc., already lower-cased
  };

  explicit DetailPageOverlay(QWidget *parent = nullptr);
  ~DetailPageOverlay() override;

  void showWith(const Payload &payload);
  void hideOverlay();
  [[nodiscard]] bool isActive() const { return m_active; }

signals:
  /// Fires when the overlay opens (true) or closes (false). DetailsPaneManager
  /// listens so the sidebar can be lowered while the overlay is up — same
  /// rationale as Kartend-63e bug #7 for the artwork preview overlay.
  void visibilityChanged(bool visible);

  /// Emitted when the user clicks the manual button. The manager opens the
  /// file via QDesktopServices so the overlay stays widget-only.
  void manualRequested(const QString &manualPath);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setupUI();
  void updatePosition();
  void renderHeroArtwork();
  void cycleArtwork(int direction);
  void clearMetadataGrid();
  void appendMetadataRow(const QString &label, const QString &value, bool wrap = false);
  void rebuildMetadataGrid(const Payload &payload);
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  [[nodiscard]] static QString formatRuntime(int seconds);
  [[nodiscard]] static QString formatTags(const QString &raw);

  QWidget *m_card = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_scrollContent = nullptr;

  // Hero artwork pane
  QLabel *m_heroLabel = nullptr;
  QLabel *m_heroTypeLabel = nullptr; // "Box (1/4)" style caption
  QPushButton *m_heroPrevButton = nullptr;
  QPushButton *m_heroNextButton = nullptr;

  // Header
  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr; // file stem when title overrides it

  // Body sections
  QLabel *m_descriptionLabel = nullptr;
  QWidget *m_metadataContainer = nullptr;
  QGridLayout *m_metadataGrid = nullptr;
  QPushButton *m_manualButton = nullptr;

  // Footer
  QPushButton *m_closeButton = nullptr;

  Payload m_payload;
  int m_currentArtworkIndex = 0;
  bool m_active = false;
};

#endif // DETAILPAGEOVERLAY_H
