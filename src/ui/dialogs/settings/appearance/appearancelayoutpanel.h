#ifndef APPEARANCELAYOUTPANEL_H
#define APPEARANCELAYOUTPANEL_H

#include "collectiontypes.h"
#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceLayoutPanel;
}
class QSpinBox;
class QComboBox;
QT_END_NAMESPACE

struct SettingsModel;

/// Per-collection "Layout" appearance sub-sub-tab. Owns the View / Grid
/// Sizing / Item Dimensions field groups (12 fields total).
///
/// Cross-cutting widgets exposed via accessors: the gridWidth* spinbox is read
/// by the host dialog's dirty-check; horizontal/vertical spacing values are
/// shown in user units
/// in the same units they are stored in: the spacing spin boxes are PIXELS
/// between tiles, negative included (Kartend-hxly2). They used to be shown on
/// an unlabelled 0-150 scale offset by SPACING_MIN, so a stored -80 read as
/// "20" and users reasonably took that for 20px.
class AppearanceLayoutPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceLayoutPanel)
public:
  explicit AppearanceLayoutPanel(QWidget *parent = nullptr);
  ~AppearanceLayoutPanel() override;

  void setModel(SettingsModel *model);
  void load() override;
  void clear() override;
  void save() override;

  // Cross-cutting accessors:
  [[nodiscard]] QSpinBox *gridWidthSpinBox() const;
  [[nodiscard]] QSpinBox *gridWidthSidebarHiddenSpinBox() const;
  [[nodiscard]] QSpinBox *horizontalGridHeightSpinBox() const;
  [[nodiscard]] QSpinBox *horizontalGridHeightSidebarHiddenSpinBox() const;
  [[nodiscard]] QSpinBox *horizontalSpacingSpinBox() const;
  [[nodiscard]] QSpinBox *verticalSpacingSpinBox() const;
  [[nodiscard]] QComboBox *viewTypeComboBox() const;

signals:
  void changed();

private:
  Ui::AppearanceLayoutPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCELAYOUTPANEL_H
