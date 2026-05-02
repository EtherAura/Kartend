#ifndef ARTWORKPREVIEWOVERLAY_H
#define ARTWORKPREVIEWOVERLAY_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

/**
 * @brief Modal overlay widget that displays artwork in a centered popup.
 *
 * Shows the artwork image for a selected item when the artwork button is
 * clicked in list view mode. The overlay covers the parent widget and can be
 * dismissed by clicking anywhere or pressing Escape.
 */
class ArtworkPreviewOverlay : public QWidget {
  Q_OBJECT

public:
  explicit ArtworkPreviewOverlay(QWidget *parent = nullptr);
  ~ArtworkPreviewOverlay() override = default;

  /// Show the overlay with artwork for the given file path
  void showArtworkForFile(const QString &filePath, const QString &artworkDirectory);

  /// Show the overlay with the artwork file at the given absolute path.
  /// Used by the sidebar artwork gallery (Kartend-un3l) when the path was
  /// resolved by ItemArtworkStore and there is no need to re-search a
  /// directory. `launchRequested` is still emitted on Enter / double-click;
  /// callers that only want preview behavior should leave that signal
  /// unconnected.
  void showArtworkAtPath(const QString &absoluteArtworkPath);

  /// Hide the overlay
  void hideOverlay();

signals:
  /// Emitted when the user activates (Enter / double-click) while the
  /// overlay is visible. Used by expand-mode to launch the previewed item
  /// on the second activation.
  void launchRequested();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  QLabel *m_artworkLabel = nullptr;
  QPushButton *m_closeButton = nullptr;
  QString m_currentFilePath;

  void setupUI();
  void centerArtwork();
  void displayPixmap(const QPixmap &pixmap);
};

#endif // ARTWORKPREVIEWOVERLAY_H
