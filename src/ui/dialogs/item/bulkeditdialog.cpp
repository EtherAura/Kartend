#include "bulkeditdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

BulkEditDialog::BulkEditDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void BulkEditDialog::setupUi() {
  setWindowTitle(tr("Bulk edit items"));
  resize(480, 240);

  auto *outer = new QVBoxLayout(this);
  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  outer->addWidget(m_headerLabel);

  auto *form = new QFormLayout();
  m_actionCombo = new QComboBox(this);
  // userData carries the BulkEdit::Action enum so the dispatch on
  // accept doesn't have to track the combo's row order.
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::AddTag),
                         int(BulkEdit::Action::AddTag));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::RemoveTag),
                         int(BulkEdit::Action::RemoveTag));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::MarkPinned),
                         int(BulkEdit::Action::MarkPinned));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::UnmarkPinned),
                         int(BulkEdit::Action::UnmarkPinned));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::MarkHidden),
                         int(BulkEdit::Action::MarkHidden));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::UnmarkHidden),
                         int(BulkEdit::Action::UnmarkHidden));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::MarkContinueLater),
                         int(BulkEdit::Action::MarkContinueLater));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::UnmarkContinueLater),
                         int(BulkEdit::Action::UnmarkContinueLater));
  m_actionCombo->addItem(BulkEdit::actionLabel(BulkEdit::Action::ClearRating),
                         int(BulkEdit::Action::ClearRating));
  form->addRow(tr("Action:"), m_actionCombo);

  m_parameterEdit = new QLineEdit(this);
  m_parameterEdit->setPlaceholderText(tr("Tag name, e.g. 'live'"));
  form->addRow(tr("Tag:"), m_parameterEdit);

  outer->addLayout(form);

  m_parameterHint = new QLabel(this);
  m_parameterHint->setWordWrap(true);
  m_parameterHint->setStyleSheet("color: palette(mid); font-style: italic;");
  outer->addWidget(m_parameterHint);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  // Custom Apply button so we can rename it dynamically and gate it on
  // the parameter field. QDialogButtonBox::Apply also fits but the
  // role-based mapping is ambiguous for what is really a destructive
  // confirm path.
  m_applyButton = new QPushButton(tr("Apply"), this);
  m_applyButton->setDefault(true);
  buttons->addButton(m_applyButton, QDialogButtonBox::AcceptRole);
  outer->addWidget(buttons);

  connect(m_actionCombo, &QComboBox::currentIndexChanged, this, &BulkEditDialog::onActionChanged);
  connect(m_parameterEdit, &QLineEdit::textChanged, this, [this] { refreshApplyState(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_applyButton, &QPushButton::clicked, this, &BulkEditDialog::onApply);

  onActionChanged(0);
}

void BulkEditDialog::setScope(const QString &collectionName, int itemCount) {
  const QString name = collectionName.isEmpty() ? tr("(unnamed)") : collectionName;
  m_headerLabel->setText(tr("This will apply to <b>%1</b> item(s) in <b>%2</b>.")
                             .arg(itemCount)
                             .arg(name.toHtmlEscaped()));
}

void BulkEditDialog::onActionChanged(int /*index*/) {
  const auto action = static_cast<BulkEdit::Action>(m_actionCombo->currentData().toInt());
  const bool needsParam = BulkEdit::actionRequiresParameter(action);
  m_parameterEdit->setEnabled(needsParam);
  if (!needsParam) {
    m_parameterEdit->clear();
    m_parameterHint->setText(tr("No additional input needed for this action."));
  } else if (action == BulkEdit::Action::AddTag) {
    m_parameterHint->setText(tr("The tag is added to items that don't already have it; rows "
                                "where the tag is already present are left untouched."));
  } else {
    m_parameterHint->setText(tr("The tag is removed from items that have it (case-insensitive)."));
  }
  refreshApplyState();
}

void BulkEditDialog::refreshApplyState() {
  const auto action = static_cast<BulkEdit::Action>(m_actionCombo->currentData().toInt());
  const bool needsParam = BulkEdit::actionRequiresParameter(action);
  // Apply is disabled when a tag action has no parameter — clicking it
  // would silently no-op against every row.
  m_applyButton->setEnabled(!needsParam || !m_parameterEdit->text().trimmed().isEmpty());
}

void BulkEditDialog::onApply() {
  m_result.action = static_cast<BulkEdit::Action>(m_actionCombo->currentData().toInt());
  m_result.parameter = m_parameterEdit->text().trimmed();
  accept();
}
