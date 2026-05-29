#ifndef APPEARANCEEFFECTSPANEL_H
#define APPEARANCEEFFECTSPANEL_H

#include "collectiontypes.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceEffectsPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Per-collection "Effects" appearance sub-sub-tab. Owns the wallpaper-
/// parallax + toolbar-backdrop-blur enable + strength/radius controls.
class AppearanceEffectsPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceEffectsPanel)
public:
  explicit AppearanceEffectsPanel(QWidget *parent = nullptr);
  ~AppearanceEffectsPanel() override;

  void setModel(SettingsModel *model);
  void load();
  void clear();
  void save() const;
  [[nodiscard]] bool hasChanges() const;

signals:
  void changed();

private:
  Ui::AppearanceEffectsPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCEEFFECTSPANEL_H
