// Smart-playlist creation/edit dialog. Built programmatically rather than
// from a .ui file because the rule rows are added and removed at runtime.
//
// Kartend-8pn2w turned this from a single-rule form into a rule LIST. The
// per-kind criterion combo and its parameter pages moved wholesale into
// SmartRuleEditor; what remains here is the name, the Match all/any
// selector, and the add/remove machinery around a column of those editors.
#include "createsmartplaylistdialog.h"

#include "smartruleeditor.h"
#include "uiconstants/color.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
/// Property carrying a row's Remove button, so updateRowChrome can reach it
/// from the editor without a parallel list to keep in sync.
constexpr const char *kRemoveButtonProperty = "kartendRemoveButton";
} // namespace

CreateSmartPlaylistDialog::CreateSmartPlaylistDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Smart Playlist"));
  setModal(true);
  buildUI();
}

void CreateSmartPlaylistDialog::buildUI() {
  auto *root = new QVBoxLayout(this);

  auto *form = new QFormLayout();
  m_nameEdit = new QLineEdit(this);
  m_nameEdit->setPlaceholderText(tr("e.g. Recently launched"));
  form->addRow(tr("Name:"), m_nameEdit);
  root->addLayout(form);

  // Match selector. Hidden while there is a single rule — "match all of: one
  // thing" is a question with no meaningful answer, and showing it invites
  // the user to wonder what they got wrong.
  m_matchRow = new QWidget(this);
  auto *matchLayout = new QHBoxLayout(m_matchRow);
  matchLayout->setContentsMargins(0, 0, 0, 0);
  auto *matchLabel = new QLabel(tr("Match:"), m_matchRow);
  m_matchCombo = new QComboBox(m_matchRow);
  m_matchCombo->addItem(tr("all of the rules"), static_cast<int>(SmartFilter::MatchMode::All));
  m_matchCombo->addItem(tr("any of the rules"), static_cast<int>(SmartFilter::MatchMode::Any));
  matchLayout->addWidget(matchLabel);
  matchLayout->addWidget(m_matchCombo);
  matchLayout->addStretch(1);
  root->addWidget(m_matchRow);

  // The rule column lives in a scroll area: a set of six rules is taller
  // than any sensible dialog, and letting the dialog grow without bound
  // pushes its buttons off the bottom of a small screen.
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *rulesContainer = new QWidget(scroll);
  m_rulesLayout = new QVBoxLayout(rulesContainer);
  m_rulesLayout->setContentsMargins(0, 0, 0, 0);
  m_rulesLayout->addStretch(1);
  scroll->setWidget(rulesContainer);
  root->addWidget(scroll, /*stretch=*/1);

  auto *addButton = new QPushButton(tr("Add rule"), this);
  connect(addButton, &QPushButton::clicked, this, [this]() {
    addRuleRow();
    updateRowChrome();
    updateOkButtonState();
  });
  auto *addRow = new QHBoxLayout();
  addRow->addWidget(addButton);
  addRow->addStretch(1);
  root->addLayout(addRow);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // Disable Ok until the form is complete: a non-blank name AND, for every
  // rule whose criterion needs one, a non-empty parameter (Kartend-dsvaq,
  // extended across the rule list by Kartend-8pn2w).
  m_okButton = buttons->button(QDialogButtonBox::Ok);
  m_okButton->setEnabled(false);
  connect(m_nameEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { updateOkButtonState(); });
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  // A dialog always shows at least one rule: the set format rejects an
  // empty rule array, so there is no state in which zero rows is valid.
  addRuleRow();
  updateRowChrome();
  updateOkButtonState();
}

SmartRuleEditor *CreateSmartPlaylistDialog::addRuleRow(const SmartFilter::Filter *filter) {
  auto *container = new QWidget();
  auto *layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *editor = new SmartRuleEditor(container);
  editor->setCollectionList(m_collectionEntries);
  if (filter) {
    editor->setFilter(*filter);
  }
  connect(editor, &SmartRuleEditor::changed, this, &CreateSmartPlaylistDialog::updateOkButtonState);
  layout->addWidget(editor, /*stretch=*/1);

  auto *removeButton = new QPushButton(tr("Remove"), container);
  connect(removeButton, &QPushButton::clicked, this, [this, editor]() { removeRuleRow(editor); });
  // Top-aligned: the editor is several rows tall and a vertically centred
  // button beside it reads as belonging to whichever parameter row it lands
  // next to rather than to the rule as a whole.
  layout->addWidget(removeButton, /*stretch=*/0, Qt::AlignTop);
  editor->setProperty(kRemoveButtonProperty, QVariant::fromValue(removeButton));

  // Insert before the trailing stretch so rows stack from the top.
  m_rulesLayout->insertWidget(m_rulesLayout->count() - 1, container);
  m_rules.append(editor);
  return editor;
}

void CreateSmartPlaylistDialog::removeRuleRow(SmartRuleEditor *row) {
  // The last rule is not removable — a set has to keep at least one.
  if (!row || m_rules.size() <= 1) {
    return;
  }
  m_rules.removeAll(row);
  // The editor's parent is the row container built in addRuleRow, so
  // deleting it takes the Remove button with it.
  if (QWidget *container = row->parentWidget()) {
    container->deleteLater();
  }
  updateRowChrome();
  updateOkButtonState();
}

void CreateSmartPlaylistDialog::updateRowChrome() {
  const bool removable = m_rules.size() > 1;
  for (SmartRuleEditor *row : std::as_const(m_rules)) {
    if (auto *button = row->property(kRemoveButtonProperty).value<QPushButton *>()) {
      button->setEnabled(removable);
    }
  }
  if (m_matchRow) {
    m_matchRow->setVisible(removable);
  }
}

void CreateSmartPlaylistDialog::updateOkButtonState() {
  if (!m_okButton) {
    return;
  }
  const bool nameOk = m_nameEdit && !m_nameEdit->text().trimmed().isEmpty();
  bool rulesOk = !m_rules.isEmpty();
  for (const SmartRuleEditor *row : std::as_const(m_rules)) {
    if (!row->isComplete()) {
      rulesOk = false;
      break;
    }
  }
  m_okButton->setEnabled(nameOk && rulesOk);
}

void CreateSmartPlaylistDialog::setInitialName(const QString &name) {
  if (m_nameEdit) {
    m_nameEdit->setText(name);
  }
}

void CreateSmartPlaylistDialog::setInitialFilterSet(const SmartFilter::FilterSet &set) {
  if (set.rules.isEmpty()) {
    // Nothing to load, and clearing would leave zero rows — which the format
    // rejects. Keep the default single blank rule.
    return;
  }
  // Drop the default row and rebuild from the saved set. Deleted OUTRIGHT
  // rather than via deleteLater(): this runs before the dialog is shown, not
  // from one of these widgets' own signal handlers, so there is nothing
  // mid-emit to protect — and a deferred delete would leave the discarded row
  // in the widget tree until the event loop turns, which anything inspecting
  // the dialog before exec() would see as an extra rule.
  const QList<SmartRuleEditor *> existing = m_rules;
  m_rules.clear();
  for (SmartRuleEditor *row : existing) {
    delete row->parentWidget();
  }
  for (const SmartFilter::Filter &rule : set.rules) {
    addRuleRow(&rule);
  }
  if (m_matchCombo) {
    const int idx = m_matchCombo->findData(static_cast<int>(set.match));
    m_matchCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  }
  updateRowChrome();
  updateOkButtonState();
}

void CreateSmartPlaylistDialog::setCollectionList(const QList<CollectionEntry> &collections) {
  m_collectionEntries = collections;
  for (SmartRuleEditor *row : std::as_const(m_rules)) {
    row->setCollectionList(collections);
  }
}

QString CreateSmartPlaylistDialog::name() const {
  return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

SmartFilter::FilterSet CreateSmartPlaylistDialog::filterSet() const {
  SmartFilter::FilterSet set;
  set.match = m_matchCombo
                  ? static_cast<SmartFilter::MatchMode>(m_matchCombo->currentData().toInt())
                  : SmartFilter::MatchMode::All;
  for (const SmartRuleEditor *row : std::as_const(m_rules)) {
    set.rules.append(row->filter());
  }
  // Defensive: every path that empties m_rules refills it, but an empty set
  // is rejected on parse, so hand back a default rule rather than something
  // that cannot round-trip.
  if (set.rules.isEmpty()) {
    set.rules.append(SmartFilter::Filter{});
  }
  return set;
}
