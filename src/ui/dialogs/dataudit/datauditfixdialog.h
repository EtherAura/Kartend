#ifndef DATAUDITFIXDIALOG_H
#define DATAUDITFIXDIALOG_H

#include <QDialog>
#include <QList>

#include "datauditfix.h"
#include "dataudittypes.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/// Preview-and-apply window for the fix engine, embodying Scan -> Plan -> Apply:
/// it shows the exact rename / relocate / quarantine actions computed from the
/// audit rows *before* anything touches disk, then applies them with the undo
/// log retained so the user can revert. Destructive categories default off;
/// rename-in-place is the only default-on fix.
class DatAuditFixDialog : public QDialog {
  Q_OBJECT
public:
  explicit DatAuditFixDialog(QList<DatAudit::AuditRow> rows, QWidget *parent = nullptr);
  ~DatAuditFixDialog() override;

  /// True once a fix actually mutated the filesystem — the caller re-runs the
  /// audit to refresh the table.
  [[nodiscard]] bool didApply() const { return m_applied; }

private slots:
  void recomputePlan();
  void onBrowseQuarantine();
  void onBrowseManaged();
  void onApply();
  void onUndo();

private:
  [[nodiscard]] DatAudit::FixSettings settings() const;

  QList<DatAudit::AuditRow> m_rows;
  DatAudit::FixPlan m_plan;
  QList<DatAudit::UndoEntry> m_undo;
  bool m_applied = false;

  QCheckBox *m_rename = nullptr;
  QCheckBox *m_quarantine = nullptr;
  QLineEdit *m_quarantineDir = nullptr;
  QPushButton *m_browseQuarantine = nullptr;
  QCheckBox *m_relocate = nullptr;
  QLineEdit *m_managedDir = nullptr;
  QPushButton *m_browseManaged = nullptr;
  QListWidget *m_planList = nullptr;
  QLabel *m_status = nullptr;
  QPushButton *m_applyButton = nullptr;
  QPushButton *m_undoButton = nullptr;
};

#endif // DATAUDITFIXDIALOG_H
