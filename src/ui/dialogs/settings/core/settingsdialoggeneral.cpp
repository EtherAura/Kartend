// Sibling translation unit for SettingsDialog: setupGeneralSettingsConnections.
// The configuration backup/restore controls (Export / Load .cfg profiles)
// were promoted into ConfigProfileController; this TU now just constructs it
// and hands across the three borrowed widgets.
#include "configprofilecontroller.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"

void SettingsDialog::setupGeneralSettingsConnections() {
  // Selection & Display / Startup / Input & Scroll Timing / Performance &
  // History live-save handlers all moved to GeneralSettingsPanel; the panel
  // emits changed() and SettingsDialog mirrors + persists in its constructor
  // wiring (see constructor's connect for ui->generalSettingsPanel).
  //
  // The previously live-applied showTitleInPlaceholder side-effect
  // (ItemWidget::setShowTitleInPlaceholder + repaint of visible widgets) is
  // now applied on Save only — consistent with the rest of the dialog's
  // deferred-save fields.

  // Browse font button for per-collection custom font lives on
  // AppearanceTitlesPanel.

  // Background type radios + value edit, palette pickers (base / primary /
  // tile / selection), list-row pickers, title-tint spin boxes, and the
  // baseColor live-save with ItemWidget side-effect — all live on
  // AppearanceColorsPanel now. Host wires baseColorChanged() / changed() in
  // the constructor.

  // customFontEdit textChanged routing lives on AppearanceTitlesPanel.

  // Global application-font controls (family + size + picker) live in
  // FontsPanel now; the panel emits changed() and SettingsDialog handles
  // the live-save mirror in its constructor.

  // Keyboard / Gamepad / Mouse connections (including detect-button →
  // GamepadCaptureController dispatch) live on ControlsPanel now.

  // Startup video / home-view / selection-display / input-timing /
  // performance-history connections all live in GeneralSettingsPanel now.

  // Toolbar-customization connections owned by ToolbarPanel.

  // configuration backup — Export / Load .cfg profile controls owned by
  // ConfigProfileController.
  if (!m_configProfileController) {
    m_configProfileController = new ConfigProfileController(this);
  }
  m_configProfileController->setupControls({.dialogParent = this,
                                            .importConfigComboBox = ui->importConfigComboBox,
                                            .importConfigButton = ui->importConfigButton,
                                            .exportConfigButton = ui->exportConfigButton});
}
