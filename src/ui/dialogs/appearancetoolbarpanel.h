#ifndef APPEARANCETOOLBARPANEL_H
#define APPEARANCETOOLBARPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceToolbarPanel;
}
QT_END_NAMESPACE

/// Per-collection "Header Logo" appearance sub-sub-tab. Owns the header
/// logo path + position combo + browse picker.
class AppearanceToolbarPanel : public QWidget {
  Q_OBJECT
public:
  explicit AppearanceToolbarPanel(QWidget *parent = nullptr);
  ~AppearanceToolbarPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

signals:
  void changed();

private slots:
  void onBrowse();

private:
  Ui::AppearanceToolbarPanel *ui;
};

#endif // APPEARANCETOOLBARPANEL_H
