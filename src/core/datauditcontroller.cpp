#include "datauditcontroller.h"

#include <Qt>
#include <QWidget>

#include "datauditdialog.h"

DatAuditController::DatAuditController(QObject *parent) : QObject(parent) {}

DatAuditController::~DatAuditController() = default;

void DatAuditController::setContext(const DatAuditControllerContext &context) {
  m_ctx = context;
}

DatAuditDialog *DatAuditController::ensureDialog() {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  if (!m_dialog) {
    m_dialog = new DatAuditDialog(parent);
    m_dialog->setWindowFlag(Qt::Window, true);
    m_dialog->setModal(false);
  }
  // Refresh the borrowed collection list on every open so the profile editor's
  // "Linked collection" picker reflects collections added/removed since.
  m_dialog->setCollections(m_ctx.getCollections ? m_ctx.getCollections() : nullptr);
  return m_dialog;
}

void DatAuditController::openDialog() {
  DatAuditDialog *dialog = ensureDialog();
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void DatAuditController::openForCollection(const QString &collectionUuid,
                                           const QString &collectionName, const QString &mediaDir,
                                           const QStringList &datPaths) {
  DatAuditDialog *dialog = ensureDialog();
  dialog->openForCollection(collectionUuid, collectionName, mediaDir, datPaths);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}
