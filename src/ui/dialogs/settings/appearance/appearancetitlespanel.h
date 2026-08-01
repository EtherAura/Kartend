#ifndef APPEARANCETITLESPANEL_H
#define APPEARANCETITLESPANEL_H

#include "collectiontypes.h"
#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceTitlesPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Per-collection "Titles" appearance sub-sub-tab. Owns title font size +
/// custom font + browse picker, and the hide-titles / hide-subcollection-
/// titles visibility checkboxes.
class AppearanceTitlesPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceTitlesPanel)
public:
  explicit AppearanceTitlesPanel(QWidget *parent = nullptr);
  ~AppearanceTitlesPanel() override;

  void setModel(SettingsModel *model);
  void load() override;
  void clear() override;
  void save() override;

signals:
  void changed();

private slots:
  void onBrowseFont();

private:
  Ui::AppearanceTitlesPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCETITLESPANEL_H
