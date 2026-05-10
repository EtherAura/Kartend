#ifndef CONFIGURATIONPANEL_H
#define CONFIGURATIONPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ConfigurationPanel;
}
class QComboBox;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

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
public:
  explicit ConfigurationPanel(QWidget *parent = nullptr);
  ~ConfigurationPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  /// Saves the simple data fields (type, mediaDirectory, extensions,
  /// expandMode, showAllSubcollectionItems). Parent-collection index is
  /// resolved separately by the host using the dropdown→collection mapping.
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

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
  Ui::ConfigurationPanel *ui;
};

#endif // CONFIGURATIONPANEL_H
