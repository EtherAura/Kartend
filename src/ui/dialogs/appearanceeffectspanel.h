#ifndef APPEARANCEEFFECTSPANEL_H
#define APPEARANCEEFFECTSPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceEffectsPanel;
}
QT_END_NAMESPACE

/// Per-collection "Effects" appearance sub-sub-tab. Owns the wallpaper-
/// parallax + toolbar-backdrop-blur enable + strength/radius controls.
class AppearanceEffectsPanel : public QWidget {
  Q_OBJECT
public:
  explicit AppearanceEffectsPanel(QWidget *parent = nullptr);
  ~AppearanceEffectsPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

signals:
  void changed();

private:
  Ui::AppearanceEffectsPanel *ui;
};

#endif // APPEARANCEEFFECTSPANEL_H
