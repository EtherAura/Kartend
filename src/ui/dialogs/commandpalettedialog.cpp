#include "commandpalettedialog.h"

#include <algorithm>

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include "fuzzymatch.h"
#include "uiconstants/dialog.h"

CommandPaletteDialog::CommandPaletteDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void CommandPaletteDialog::setupUi() {
  setWindowTitle(tr("Command palette"));
  // Borderless-ish chrome works best for a Spotlight-style picker but Qt's
  // platform handling of frameless windows varies; stick with the default
  // QDialog frame for now and keep the dialog small / centered.
  resize(UIConstants::CommandPaletteDialog::DEFAULT_WIDTH,
         UIConstants::CommandPaletteDialog::DEFAULT_HEIGHT);

  auto *outer = new QVBoxLayout(this);
  m_searchEdit = new QLineEdit(this);
  m_searchEdit->setPlaceholderText(tr("Type to search commands…"));
  m_searchEdit->setClearButtonEnabled(true);
  outer->addWidget(m_searchEdit);

  m_list = new QListWidget(this);
  m_list->setUniformItemSizes(true);
  m_list->setAlternatingRowColors(true);
  outer->addWidget(m_list, /*stretch=*/1);

  // Search field always has keyboard focus; arrow keys reach the list
  // via the event filter so the user never has to Tab to navigate.
  m_searchEdit->installEventFilter(this);
  m_searchEdit->setFocus();

  connect(m_searchEdit, &QLineEdit::textChanged, this, &CommandPaletteDialog::onQueryChanged);
  connect(m_list, &QListWidget::itemActivated, this,
          [this](QListWidgetItem * /*item*/) { onAccepted(); });
}

void CommandPaletteDialog::setCommands(QList<Command> commands) {
  m_commands = std::move(commands);
  refreshList();
}

void CommandPaletteDialog::onQueryChanged(const QString & /*text*/) {
  refreshList();
}

void CommandPaletteDialog::refreshList() {
  if (!m_list) return;
  const QString query = m_searchEdit ? m_searchEdit->text() : QString();
  m_list->clear();

  // Score every command, drop the no-matches, sort the survivors by
  // score descending. For an empty query the matcher returns 0 for all
  // candidates; we then list them in original (caller-supplied) order
  // which lets the caller put the most useful entries up top.
  struct Scored {
    int score;
    int originalIndex;
    const Command *cmd;
  };
  QList<Scored> scored;
  scored.reserve(m_commands.size());
  for (int i = 0; i < m_commands.size(); ++i) {
    const Command &cmd = m_commands.at(i);
    // Search against "Category | Label" so users can type either side.
    const QString combined =
        cmd.category.isEmpty() ? cmd.label : cmd.category + QStringLiteral(": ") + cmd.label;
    const int s = FuzzyMatch::score(query, combined);
    if (s < 0) continue;
    scored.append({s, i, &cmd});
  }
  std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
    if (a.score != b.score) return a.score > b.score;
    return a.originalIndex < b.originalIndex;
  });

  for (const Scored &row : scored) {
    QString text = row.cmd->category.isEmpty()
                       ? row.cmd->label
                       : QStringLiteral("[%1] %2").arg(row.cmd->category, row.cmd->label);
    auto *item = new QListWidgetItem(text, m_list);
    // Stash the original index so onAccepted can find the right command
    // even after sorting reshuffles the visible rows.
    item->setData(Qt::UserRole, row.originalIndex);
  }
  if (m_list->count() > 0) {
    m_list->setCurrentRow(0);
  }
}

bool CommandPaletteDialog::eventFilter(QObject *obj, QEvent *event) {
  if (obj == m_searchEdit && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    // Forward Up/Down/Home/End/PageUp/PageDown to the list so the user
    // can navigate without leaving the search field.
    if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up || key->key() == Qt::Key_PageDown ||
        key->key() == Qt::Key_PageUp || key->key() == Qt::Key_Home || key->key() == Qt::Key_End) {
      if (m_list) {
        QApplication::sendEvent(m_list, event);
      }
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      onAccepted();
      return true;
    }
  }
  return QDialog::eventFilter(obj, event);
}

void CommandPaletteDialog::keyPressEvent(QKeyEvent *event) {
  // Escape closes — QDialog handles it by default but we override the
  // method for clarity. Enter on the list goes through onAccepted via
  // itemActivated.
  if (event->key() == Qt::Key_Escape) {
    reject();
    return;
  }
  QDialog::keyPressEvent(event);
}

void CommandPaletteDialog::onAccepted() {
  if (!m_list || m_list->count() == 0) {
    return;
  }
  QListWidgetItem *current = m_list->currentItem();
  if (!current) {
    return;
  }
  const int originalIndex = current->data(Qt::UserRole).toInt();
  if (originalIndex < 0 || originalIndex >= m_commands.size()) {
    return;
  }
  const Command cmd = m_commands.at(originalIndex);
  // Close the dialog before invoking so the action can show its own
  // modal (e.g. settings dialog) without nesting under the palette.
  accept();
  if (cmd.run) {
    cmd.run();
  }
}
