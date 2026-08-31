#include "bindingvisualizerdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "collection/generalsettings.h"
#include "gamepadmanager.h"

namespace {

/// Per-row binding registry. Lookup is by action label so the dialog's
/// "matched" indicator can walk the tree and highlight the first row
/// whose key or gamepad button matches. The category column groups
/// the rows visually; the matcher ignores categories.
struct BindingRow {
  QString category;
  QString action;
  int key = 0;           // Qt::Key code; 0 = unbound
  QString gamepadButton; // empty = no gamepad mapping
};

/// Builds the registry from a live `GeneralSettings` snapshot. Pure
/// function so tests can substitute mocked settings if needed in the
/// future.
QList<BindingRow> buildBindings(const GeneralSettings &gs) {
  return {
      {QObject::tr("Navigation"), QObject::tr("Move left"), gs.keybindings.keyNavLeft, QString()},
      {QObject::tr("Navigation"), QObject::tr("Move right"), gs.keybindings.keyNavRight, QString()},
      {QObject::tr("Navigation"), QObject::tr("Move up"), gs.keybindings.keyNavUp, QString()},
      {QObject::tr("Navigation"), QObject::tr("Move down"), gs.keybindings.keyNavDown, QString()},
      {QObject::tr("Navigation"), QObject::tr("Jump to first"), gs.keybindings.keyJumpFirst,
       QString()},
      {QObject::tr("Navigation"), QObject::tr("Jump to last"), gs.keybindings.keyJumpLast,
       QString()},
      {QObject::tr("Navigation"), QObject::tr("Alphabetic back"), gs.keybindings.keyAlphabeticBack,
       QString()},
      {QObject::tr("Navigation"), QObject::tr("Alphabetic forward"),
       gs.keybindings.keyAlphabeticForward, QString()},
      {QObject::tr("Navigation"), QObject::tr("Go to Home view"), gs.keybindings.keyHomeView,
       QString()},
      {QObject::tr("Actions"), QObject::tr("Confirm / launch"), gs.keybindings.keyConfirm,
       gs.gamepad.gamepadConfirmButton},
      {QObject::tr("Actions"), QObject::tr("Back / cancel"), gs.keybindings.keyBack,
       gs.gamepad.gamepadBackButton},
      {QObject::tr("Actions"), QObject::tr("Open item details"), gs.keybindings.keyItemDetails,
       QString()},
      {QObject::tr("Actions"), QObject::tr("Toggle sidebar"), 0,
       gs.gamepad.gamepadToggleSidebarButton},
      {QObject::tr("Actions"), QObject::tr("Toggle collection tree"),
       gs.keybindings.keyToggleCollectionTree, gs.gamepad.gamepadToggleCollectionTreeButton},
      {QObject::tr("Search"), QObject::tr("Open search"), gs.keybindings.keySearch, QString()},
  };
}

/// "Move left" → key name like "Left" or "Ctrl+P". Returns "(unbound)"
/// for the 0 sentinel so the column reads honestly.
QString formatKey(int key) {
  if (key == 0) {
    return QObject::tr("(unbound)");
  }
  return QKeySequence(key).toString(QKeySequence::NativeText);
}

QString formatButton(const QString &button) {
  return button.isEmpty() ? QObject::tr("(none)") : button;
}

} // namespace

BindingVisualizerDialog::BindingVisualizerDialog(const GeneralSettings *settings,
                                                 GamepadManager *gamepadManager, QWidget *parent)
    : QDialog(parent), m_settings(settings), m_gamepadManager(gamepadManager) {
  setupUi();
  buildBindingRows();
  // Capture gamepad input while the dialog is alive so button presses
  // surface as identification events instead of doing their normal
  // action under the dialog.
  if (m_gamepadManager) {
    m_gamepadManager->beginBindingCapture();
    connect(m_gamepadManager, &GamepadManager::bindingCaptureButtonPressed, this,
            &BindingVisualizerDialog::highlightGamepadButton);
  }
}

BindingVisualizerDialog::~BindingVisualizerDialog() {
  if (m_gamepadManager) {
    m_gamepadManager->endBindingCapture();
  }
}

void BindingVisualizerDialog::setupUi() {
  setWindowTitle(tr("Binding visualizer"));
  resize(640, 520);

  auto *outer = new QVBoxLayout(this);
  m_statusLabel = new QLabel(tr("Press any key or gamepad button to identify it."), this);
  m_statusLabel->setStyleSheet(UIConstants::Color::mutedLabelStyleSheet(/*italic=*/true));
  m_statusLabel->setWordWrap(true);
  outer->addWidget(m_statusLabel);

  m_tree = new QTreeWidget(this);
  m_tree->setHeaderLabels({tr("Action"), tr("Keyboard"), tr("Gamepad")});
  m_tree->setRootIsDecorated(false);
  m_tree->setUniformRowHeights(true);
  m_tree->setSelectionMode(QAbstractItemView::NoSelection);
  m_tree->setAlternatingRowColors(true);
  m_tree->setFocusPolicy(Qt::NoFocus); // dialog keeps the focus so keyPressEvent fires
  outer->addWidget(m_tree, /*stretch=*/1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  outer->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Dialog handles key presses directly; reject focus from children so
  // arrow keys don't move tree selection while the user is identifying.
  setFocusPolicy(Qt::StrongFocus);
  setFocus();
}

void BindingVisualizerDialog::buildBindingRows() {
  if (!m_tree || !m_settings) {
    return;
  }
  m_tree->clear();
  // Group rows by category — section headers are plain items with a
  // muted style so the grouping is visible without a tree expand/collapse
  // toggle.
  QString currentCategory;
  const auto rows = buildBindings(*m_settings);
  for (const BindingRow &row : rows) {
    if (row.category != currentCategory) {
      currentCategory = row.category;
      auto *header = new QTreeWidgetItem(m_tree);
      header->setText(0, currentCategory);
      QFont f = header->font(0);
      f.setBold(true);
      for (int col = 0; col < 3; ++col) {
        header->setFont(col, f);
      }
      header->setFlags(Qt::ItemIsEnabled);
    }
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, QStringLiteral("  ") + row.action);
    item->setText(1, formatKey(row.key));
    item->setText(2, formatButton(row.gamepadButton));
    item->setData(0, Qt::UserRole, row.key);
    item->setData(0, Qt::UserRole + 1, row.gamepadButton);
  }
  m_tree->header()->resizeSections(QHeaderView::ResizeToContents);
}

void BindingVisualizerDialog::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    reject();
    return;
  }
  highlightKey(event->key());
  event->accept();
}

void BindingVisualizerDialog::highlightKey(int key) {
  if (!m_tree || key == 0) {
    return;
  }
  clearHighlight();
  bool matched = false;
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_tree->topLevelItem(i);
    if (!(item->flags() & Qt::ItemIsSelectable)) {
      // Category header — skip.
      continue;
    }
    if (item->data(0, Qt::UserRole).toInt() == key) {
      const QString action = item->text(0).trimmed();
      const QColor highlight = palette().color(QPalette::Highlight);
      for (int col = 0; col < 3; ++col) {
        item->setBackground(col, highlight);
        item->setForeground(col, palette().color(QPalette::HighlightedText));
      }
      m_statusLabel->setText(tr("Key %1 → %2").arg(QKeySequence(key).toString(), action));
      matched = true;
      break;
    }
  }
  if (!matched) {
    m_statusLabel->setText(tr("Key %1 → no binding.").arg(QKeySequence(key).toString()));
  }
}

void BindingVisualizerDialog::highlightGamepadButton(const QString &buttonName) {
  if (!m_tree || buttonName.isEmpty()) {
    return;
  }
  clearHighlight();
  bool matched = false;
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_tree->topLevelItem(i);
    if (!(item->flags() & Qt::ItemIsSelectable)) {
      continue;
    }
    if (item->data(0, Qt::UserRole + 1).toString().compare(buttonName, Qt::CaseInsensitive) == 0) {
      const QString action = item->text(0).trimmed();
      const QColor highlight = palette().color(QPalette::Highlight);
      for (int col = 0; col < 3; ++col) {
        item->setBackground(col, highlight);
        item->setForeground(col, palette().color(QPalette::HighlightedText));
      }
      m_statusLabel->setText(tr("Button %1 → %2").arg(buttonName, action));
      matched = true;
      break;
    }
  }
  if (!matched) {
    m_statusLabel->setText(tr("Button %1 → no binding.").arg(buttonName));
  }
}

void BindingVisualizerDialog::clearHighlight() {
  if (!m_tree) return;
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_tree->topLevelItem(i);
    for (int col = 0; col < 3; ++col) {
      item->setBackground(col, QBrush());
      item->setForeground(col, QBrush());
    }
  }
}

void BindingVisualizerDialog::retintHighlightedRow() {
  if (!m_tree) return;
  // The highlight brushes are baked concrete colours; find the row that
  // currently carries one (non-empty background) and re-stamp it from the
  // fresh palette so a runtime accent change re-tints the active row.
  const QColor highlight = palette().color(QPalette::Highlight);
  const QColor highlightText = palette().color(QPalette::HighlightedText);
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_tree->topLevelItem(i);
    if (item->background(0).style() == Qt::NoBrush) {
      continue;
    }
    for (int col = 0; col < 3; ++col) {
      item->setBackground(col, highlight);
      item->setForeground(col, highlightText);
    }
  }
}

void BindingVisualizerDialog::changeEvent(QEvent *event) {
  QDialog::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    retintHighlightedRow();
  }
}
