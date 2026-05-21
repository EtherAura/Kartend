#ifndef CONFIGPROFILECONTROLLER_H
#define CONFIGPROFILECONTROLLER_H

#include <QObject>

QT_BEGIN_NAMESPACE
class QComboBox;
class QPushButton;
class QWidget;
QT_END_NAMESPACE

/**
 * @brief Drives the SettingsDialog's configuration backup/restore UI surface.
 *
 * Extracted from SettingsDialog (settingsdialoggeneral.cpp seam). Owns the
 * three "configuration backup" controls — the Export button, the profile
 * combo box, and the Load button — and their wiring:
 *  - Export saves the live config to a named `<name>.cfg` profile inside the
 *    Kartend config directory.
 *  - The combo lists every `*.cfg` in that directory (active config tagged).
 *  - Load replaces `kartend.cfg` with the selected profile and quits the app
 *    so the new config takes effect on restart.
 *
 * Every widget is borrowed — the controller never owns them. The dialog
 * parent is borrowed too, used only as the parent for the modal Qt dialogs
 * (file/input/message boxes). Holds no back-pointer into SettingsDialog and
 * touches no dialog state, so the seam is one-directional.
 */
class ConfigProfileController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ConfigProfileController)
public:
  /// Setup struct for ConfigProfileController dependencies. Every field is
  /// borrowed — `dialogParent` is used purely as the parent widget for the
  /// modal dialogs this controller raises.
  struct Setup {
    QWidget *dialogParent = nullptr;
    QComboBox *importConfigComboBox = nullptr;
    QPushButton *importConfigButton = nullptr;
    QPushButton *exportConfigButton = nullptr;
  };

  explicit ConfigProfileController(QObject *parent = nullptr);
  ~ConfigProfileController() override = default;

  /// Bind the borrowed UI controls and wire their signals. Idempotent enough
  /// for one-shot setup-time use.
  void setupControls(const Setup &setup);

private:
  /// (Re)populate the profile combo with every `*.cfg` in the config dir,
  /// tagging and selecting the active config.
  void populateImportConfigComboBox();
  /// Prompt for a profile name, sanitize it, and export the live config.
  void exportConfig();
  /// Replace the live config with the selected profile, then quit the app.
  void importConfig();

  // Borrowed dependencies — never owned, never deleted through these.
  QWidget *m_dialogParent = nullptr;
  QComboBox *m_importConfigComboBox = nullptr;
  QPushButton *m_importConfigButton = nullptr;
  QPushButton *m_exportConfigButton = nullptr;
};

#endif // CONFIGPROFILECONTROLLER_H
