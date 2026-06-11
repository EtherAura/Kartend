#ifndef NOWPLAYINGOVERLAY_H
#define NOWPLAYINGOVERLAY_H

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPropertyAnimation;
QT_END_NAMESPACE

class OverlayZOrderRegistry;

/**
 * @brief Full-window "Now Playing" overlay shown while a runtime-tracked
 * child process is running.
 *
 * Unlike SplashOverlay this does not auto-hide — it stays visible until
 * `hideOverlay()` is called (typically when the tracked child exits).
 * Mouse events are not consumed; the overlay is purely cosmetic so the
 * user can still alt-tab freely.
 */
class NowPlayingOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(NowPlayingOverlay)
public:
  explicit NowPlayingOverlay(QWidget *parent = nullptr);
  ~NowPlayingOverlay() override;

  /// Shows the overlay with `displayName` as the subtitle. `title` and
  /// `hint` override the "Now Playing" header / return hint when non-empty
  /// (used by the archive-extraction busy state, Kartend-mkcak); empty
  /// values restore the defaults.
  void showOverlay(const QString &displayName, const QString &title = QString(),
                   const QString &hint = QString());
  void hideOverlay();
  [[nodiscard]] bool isActive() const { return m_active; }

  /// See OverlayZOrderRegistry. Routes showOverlay()'s raise() through the
  /// central z-order coordinator when installed.
  void setLayerManager(OverlayZOrderRegistry *manager) { m_layerManager = manager; }

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setupUI();
  void updatePosition();

  QWidget *m_cardWidget = nullptr;
  QLabel *m_iconLabel = nullptr;
  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr;
  QLabel *m_hintLabel = nullptr;
  QPropertyAnimation *m_fadeAnimation = nullptr;
  bool m_active = false;
  OverlayZOrderRegistry *m_layerManager = nullptr;
};

#endif // NOWPLAYINGOVERLAY_H
