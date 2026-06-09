#include "datauditcontroller.h"

#include <Qt>
#include <QWidget>

#include "datauditdialog.h"

DatAuditController::DatAuditController(QObject *parent) : QObject(parent) {}

DatAuditController::~DatAuditController() = default;

void DatAuditController::setContext(const DatAuditControllerContext &context) {
  m_ctx = context;
}

void DatAuditController::openDialog() {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  if (!m_dialog) {
    m_dialog = new DatAuditDialog(parent);
    m_dialog->setWindowFlag(Qt::Window, true);
    m_dialog->setModal(false);
  }
  m_dialog->show();
  m_dialog->raise();
  m_dialog->activateWindow();
}
