#ifndef COLLECTIONSTREESHELL_H
#define COLLECTIONSTREESHELL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class CollectionsTreeShell;
}
QT_END_NAMESPACE

class CollectionTreeWidget;
class QComboBox;
class QPushButton;

/// Container widget for the collection-tree management UI at the top of the
/// Collections tab. Hosts the add/remove/duplicate buttons, the settings-
/// scope dropdown, and the tree widget itself. The dialog-global save button
/// lives in SettingsDialog's tab-widget corner so it stays put across tabs.
/// The complex management slots (addCollection / removeCollection /
/// duplicateCollection / onTreeContextMenuRequested / onTreeRearranged /
/// propagateCollectionNameChange / populateLinkedAppearances) remain on
/// SettingsDialog because they mutate dialog-owned working state; the shell
/// is a passive container that exposes accessors for the host to wire up.
class CollectionsTreeShell : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionsTreeShell)
public:
  explicit CollectionsTreeShell(QWidget *parent = nullptr);
  ~CollectionsTreeShell() override;

  CollectionTreeWidget *collectionTreeWidget() const;
  QPushButton *addCollectionButton() const;
  QPushButton *removeCollectionButton() const;
  QPushButton *duplicateCollectionButton() const;
  QComboBox *settingsScopeComboBox() const;

private:
  Ui::CollectionsTreeShell *ui;
};

#endif // COLLECTIONSTREESHELL_H
