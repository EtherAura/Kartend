#ifndef ISETTINGSDIALOG_H
#define ISETTINGSDIALOG_H

#include "collectionutils.h"
#include <QList>

/**
 * @brief Neutral role interface to the modal Settings dialog.
 *
 * Lets the data-layer SettingsDialogController drive the settings dialog
 * (run it modally, read back the edited collection list) without #including
 * the concrete src/ui/dialogs/settings/settingsdialog.h — which would pull a
 * data->ui edge and break the layered DAG.
 *
 * Plain abstract class, not a QObject: SettingsDialog already derives QDialog
 * (its single QObject base) plus CollectionRemoverHost, so it picks this up
 * as a further non-QObject base. exec() is satisfied by QDialog::exec().
 *
 * The dialog cannot be *constructed* through this interface — construction
 * (and the collectionSaved / rescanRequired signal wiring, which needs the
 * concrete Qt signal symbols) is owned by MainWindow and handed to the data
 * layer as the SettingsDialogContext::createSettingsDialog factory. This
 * interface covers only what the controller invokes on an already-built,
 * already-wired dialog instance.
 */
class ISettingsDialog {
public:
  virtual ~ISettingsDialog() = default;

  /// Run the dialog modally. Returns a QDialog::DialogCode
  /// (QDialog::Accepted / QDialog::Rejected). Satisfied by QDialog::exec().
  virtual int exec() = 0;

  /// The collection list as edited in the dialog, valid after exec() returns.
  [[nodiscard]] virtual const QList<CollectionConfig> &getCollections() const = 0;
};

#endif // ISETTINGSDIALOG_H
