#ifndef BULKEDITDIALOG_H
#define BULKEDITDIALOG_H

#include <QDialog>

#include "bulkedit.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

/// Picker dialog for choosing which bulk operation to apply to the
/// current collection's items. Returns the user's choice via the
/// `result()` accessor when exec() returns Accepted. The caller fetches
/// the affected item list, applies via BulkEdit::applyAction, and
/// persists.
class BulkEditDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(BulkEditDialog)
public:
  struct Choice {
    BulkEdit::Action action = BulkEdit::Action::AddTag;
    QString parameter;
  };

  explicit BulkEditDialog(QWidget *parent = nullptr);

  /// Header text — caller passes the active collection name + the item
  /// count so the dialog can render "Apply to 142 items in 'Concerts'".
  void setScope(const QString &collectionName, int itemCount);

  [[nodiscard]] Choice result() const noexcept { return m_result; }

private slots:
  void onActionChanged(int index);
  void onApply();

private:
  void setupUi();
  void refreshApplyState();

  QLabel *m_headerLabel = nullptr;
  QComboBox *m_actionCombo = nullptr;
  QLineEdit *m_parameterEdit = nullptr;
  QLabel *m_parameterHint = nullptr;
  QPushButton *m_applyButton = nullptr;

  Choice m_result;
};

#endif // BULKEDITDIALOG_H
