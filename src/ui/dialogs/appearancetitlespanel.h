#ifndef APPEARANCETITLESPANEL_H
#define APPEARANCETITLESPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceTitlesPanel;
}
QT_END_NAMESPACE

/// Per-collection "Titles" appearance sub-sub-tab. Owns title font size +
/// custom font + browse picker, and the hide-titles / hide-subcollection-
/// titles visibility checkboxes.
class AppearanceTitlesPanel : public QWidget {
  Q_OBJECT
public:
  explicit AppearanceTitlesPanel(QWidget *parent = nullptr);
  ~AppearanceTitlesPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

signals:
  void changed();

private slots:
  void onBrowseFont();

private:
  Ui::AppearanceTitlesPanel *ui;
};

#endif // APPEARANCETITLESPANEL_H
