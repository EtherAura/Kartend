#ifndef APPEARANCELISTPANEL_H
#define APPEARANCELISTPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceListPanel;
}
QT_END_NAMESPACE

/// Standalone panel widget for the per-collection "List Mode" appearance
/// sub-sub-tab. Owns the list-view font size and row height spin boxes.
class AppearanceListPanel : public QWidget {
  Q_OBJECT
public:
  explicit AppearanceListPanel(QWidget *parent = nullptr);
  ~AppearanceListPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

signals:
  void changed();

private:
  Ui::AppearanceListPanel *ui;
};

#endif // APPEARANCELISTPANEL_H
