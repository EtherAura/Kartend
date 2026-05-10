#ifndef SPLASHPANEL_H
#define SPLASHPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SplashPanel;
}
QT_END_NAMESPACE

struct GeneralSettings;

/// Standalone panel widget for the global "Splash" sub-tab in SettingsDialog.
/// Owns the boot-splash + resume-focus-splash enable/title/subtitle fields,
/// observing a GeneralSettings* installed by the host dialog so changes write
/// straight through to the live settings struct. Live-save semantics — host
/// is expected to mirror to mainWindow + saveGeneralSettings on changed().
class SplashPanel : public QWidget {
  Q_OBJECT
public:
  explicit SplashPanel(QWidget *parent = nullptr);
  ~SplashPanel() override;

  void setSettings(GeneralSettings *settings);
  void refresh();

signals:
  void changed();

private:
  void writeBack();

  Ui::SplashPanel *ui;
  GeneralSettings *m_settings = nullptr;
};

#endif // SPLASHPANEL_H
