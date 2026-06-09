#ifndef APPEARANCELISTPANEL_H
#define APPEARANCELISTPANEL_H

#include "collectiontypes.h"
#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceListPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the per-collection "List Mode" appearance
/// sub-sub-tab. Owns the list-view font size and row height spin boxes.
class AppearanceListPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceListPanel)
public:
  explicit AppearanceListPanel(QWidget *parent = nullptr);
  ~AppearanceListPanel() override;

  void setModel(SettingsModel *model);
  void load() override;
  void clear() override;
  void save() override;
  [[nodiscard]] bool hasChanges() const;

signals:
  void changed();

private:
  Ui::AppearanceListPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCELISTPANEL_H
