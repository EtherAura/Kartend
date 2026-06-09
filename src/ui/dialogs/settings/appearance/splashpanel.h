#ifndef SPLASHPANEL_H
#define SPLASHPANEL_H

#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SplashPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the global "Splash" sub-tab in SettingsDialog.
/// Owns the boot-splash + resume-focus-splash enable/title/subtitle fields,
/// observing a SettingsModel* installed by the host dialog so changes write
/// straight through to the live settings struct. Live-save semantics — host
/// is expected to mirror to mainWindow + saveGeneralSettings on changed().
class SplashPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SplashPanel)
public:
  explicit SplashPanel(QWidget *parent = nullptr);
  ~SplashPanel() override;

  void setModel(SettingsModel *model);
  // ISettingsPanel (Kartend-ny2ki). load() was refresh(); save() was the
  // private writeBack(); clear() is a no-op — this global panel is always
  // backed by the single live GeneralSettings model.
  void load() override;
  void save() override;
  void clear() override {}

signals:
  void changed();

private:
  Ui::SplashPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // SPLASHPANEL_H
