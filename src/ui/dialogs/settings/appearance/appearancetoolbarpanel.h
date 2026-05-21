#ifndef APPEARANCETOOLBARPANEL_H
#define APPEARANCETOOLBARPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceToolbarPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Per-collection "Header Logo" appearance sub-sub-tab. Owns the header
/// logo path + position combo + browse picker.
class AppearanceToolbarPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceToolbarPanel)
public:
  explicit AppearanceToolbarPanel(QWidget *parent = nullptr);
  ~AppearanceToolbarPanel() override;

  void setModel(SettingsModel *model);
  void load();
  void clear();
  void save() const;
  [[nodiscard]] bool hasChanges() const;

signals:
  void changed();

private slots:
  void onBrowse();

private:
  Ui::AppearanceToolbarPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCETOOLBARPANEL_H
