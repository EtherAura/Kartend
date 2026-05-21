#ifndef APPEARANCELAYOUTPANEL_H
#define APPEARANCELAYOUTPANEL_H

#include "collectionutils.h"
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
/// Cross-cutting widgets exposed via accessors: gridWidth* spinboxes feed
/// updateGridWidthLimits() and the per-edit gridWidthChanged signal on the
/// host dialog; horizontal/vertical spacing values are shown in user units
/// but stored offset by SPACING_MIN, so spacingUiToInternal /
/// spacingInternalToUi conversions stay paired with the host's helpers.
class AppearanceLayoutPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceLayoutPanel)
public:
  explicit AppearanceLayoutPanel(QWidget *parent = nullptr);
  ~AppearanceLayoutPanel() override;

  void setModel(SettingsModel *model);
  void load();
  void clear();
  void save() const;
  [[nodiscard]] bool hasChanges() const;

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
  static int spacingInternalToUi(int spacing);
  static int spacingUiToInternal(int spacing);

  Ui::AppearanceLayoutPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCELAYOUTPANEL_H
