#ifndef CONFIGURATIONPANEL_H
#define CONFIGURATIONPANEL_H

#include "collectionutils.h"
#include "screenscrapersystems.h"

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ConfigurationPanel;
}
class QComboBox;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the per-collection "Configuration" tab in
/// SettingsDialog. Owns the parent-collection / linked-parents / type
/// fields, the content-source row (media dir + browse + recursive import),
/// the file-extensions edit, and the expand-mode / show-all-subcollection-
/// items checkboxes.
///
/// The parent-collection combo, linked-parents button, and recursive-import
/// button still need cross-cutting access to dialog state (collection list,
/// circular-reference checks, m_workingAdditionalParentNames, the recursive-
/// import workflow), so the panel exposes accessors and the host dialog
/// continues to own those slots/connections.
class ConfigurationPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ConfigurationPanel)
public:
  explicit ConfigurationPanel(QWidget *parent = nullptr);
  ~ConfigurationPanel() override;

  void setModel(SettingsModel *model);
  void load();
  void clear();
  /// Saves the simple data fields (type, mediaDirectory, extensions,
  /// expandMode, showAllSubcollectionItems). Parent-collection index is
  /// resolved separately by the host using the dropdown→collection mapping.
  void save() const;
  [[nodiscard]] bool hasChanges() const;

  /// Populate the type combo with @p knownTypes and select / set
  /// @p currentText. Trims any leading/trailing whitespace on the current
  /// text; preserves editable behavior so the user can type new types.
  void setKnownTypes(const QStringList &knownTypes, const QString &currentText);

  // Cross-cutting widget accessors used by the host dialog:
  // - parentCollectionComboBox: populated via updateParentCollectionComboBox
  //   and read back via m_parentCollectionMapping in extractUIFieldValues
  //   and checkParentCollectionChanges.
  // - editLinkedParentsButton: clicked → SettingsDialog::onEditLinkedParents
  //   which mutates m_workingAdditionalParentNames.
  // - recursiveImportContentButton / mediaDirLineEdit: clicked →
  //   SettingsDialog::onRecursiveImportContent which reads the media dir.
  // - mediaDirLineEdit: also fed by SettingsDialog::browseMediaDir slot via
  //   the existing connection.
  [[nodiscard]] QComboBox *parentCollectionComboBox() const;
  [[nodiscard]] QPushButton *editLinkedParentsButton() const;
  [[nodiscard]] QPushButton *recursiveImportContentButton() const;
  [[nodiscard]] QPushButton *browseMediaDirButton() const;
  [[nodiscard]] QLineEdit *mediaDirLineEdit() const;
  [[nodiscard]] QLineEdit *fileExtensionsLineEdit() const;
  [[nodiscard]] QComboBox *collectionTypeComboBox() const;

signals:
  void changed();

private:
  /// Re-point the ScreenScraper system combo at the system autodetect
  /// resolves from the collection name + type + extensions. No-op once
  /// the user has picked a system by hand (or the collection already
  /// pinned a concrete one). Triggered by type / extension edits.
  void autodetectScreenscraperSystem();

  Ui::ConfigurationPanel *ui;
  SettingsModel *m_model = nullptr;
  /// Set when the loaded collection pins a concrete system, or once
  /// the user picks one by hand — freezes the autodetect.
  bool m_systemManuallySet = false;
  /// SS catalog kept for autodetect (the combo carries only id +
  /// display name, not the aliases autodetect scores against).
  QList<ScreenScraperSystems::System> m_screenscraperSystems;
};

#endif // CONFIGURATIONPANEL_H
