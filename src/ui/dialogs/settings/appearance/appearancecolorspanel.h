#ifndef APPEARANCECOLORSPANEL_H
#define APPEARANCECOLORSPANEL_H

#include "collection/generalsettings.h"
#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AppearanceColorsPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Per-collection "Colors" appearance sub-sub-tab. Owns the background type
/// (Color / Image / Video radios + value edit + picker), item color palette
/// (primary / tile / selection), list-row colors, and per-collection vignette
/// enable/intensity. Also hosts three global title-tint fields
/// (titleBaseColor, titleTintSaturation, titleTintLightness) that live in
/// GeneralSettings — titleBaseColor uses live-save semantics (host applies
/// ItemWidget::setTitleBaseColor on baseColorChanged()); the two tint spin
/// boxes are deferred-save like the rest of the dialog's per-collection state.
class AppearanceColorsPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(AppearanceColorsPanel)
public:
  explicit AppearanceColorsPanel(QWidget *parent = nullptr);
  ~AppearanceColorsPanel() override;

  // Per-collection state (reads/writes the model's current working
  // collection) plus global title-tint state via the model's general
  // settings. The pointer must outlive the panel.
  void setModel(SettingsModel *model);
  void load() override;
  void clear() override;
  void save() override;
  [[nodiscard]] bool hasChanges() const;
  // Not part of ISettingsPanel: loads the three global title-tint fields. The
  // host calls this separately (alongside load()) — keep it as-is.
  void refresh();

signals:
  /// Emitted on any user-driven mutation. Host wires this to checkForChanges.
  void changed();
  /// Emitted when the global title base color is edited; host should persist
  /// + apply ItemWidget::setTitleBaseColor immediately (live-save).
  void baseColorChanged(const QString &color);

private:
  void writeBackGlobals();
  void onBrowseBackground();
  void onBrowseBaseColor();
  void onBrowsePrimaryColor();
  void onBrowseTileColor();
  void onBrowseSelectionColor();
  void onBrowseListRowColor();
  void onBrowseListAltRowColor();
  void onLoadColorScheme();
  void updateBackgroundButtonForType();

  Ui::AppearanceColorsPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // APPEARANCECOLORSPANEL_H
