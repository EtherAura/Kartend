#ifndef DATAUDITCONTROLLER_H
#define DATAUDITCONTROLLER_H

#include <functional>

#include <QObject>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class DatAuditDialog;

/// Owns the DAT Audit sub-window, mirroring ScraperController's role for the
/// scraper: the dialog is constructed lazily on first open and reused across
/// opens (hidden, not destroyed, on close), so it survives the user closing
/// the window. Lives on MainWindow as a unique_ptr(nullptr-parent) member.
///
/// v1 is intentionally thin — the audit dialog is self-contained (it picks its
/// own DAT files + scan folders and runs the engine off-thread), so the only
/// context it needs is the parent window. Profile-driven launches and a
/// collection link arrive with the profile-management UI follow-up.
struct DatAuditControllerContext {
  std::function<QWidget *()> getParentWindow;
};

class DatAuditController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DatAuditController)
public:
  explicit DatAuditController(QObject *parent = nullptr);
  ~DatAuditController() override;

  void setContext(const DatAuditControllerContext &context);

public slots:
  /// Show (or raise) the DAT Audit window, constructing it on first call.
  void openDialog();

private:
  DatAuditControllerContext m_ctx;
  DatAuditDialog *m_dialog = nullptr;
};

#endif // DATAUDITCONTROLLER_H
