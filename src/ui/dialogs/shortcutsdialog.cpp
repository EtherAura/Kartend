// Keyboard shortcuts help dialog
#include "shortcutsdialog.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
constexpr int DIALOG_WIDTH = 450;
constexpr int DIALOG_HEIGHT = 500;
constexpr int SECTION_SPACING = 12;
constexpr int SHORTCUT_SPACING = 4;
}  // namespace

ShortcutsDialog::ShortcutsDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Keyboard Shortcuts"));
  setModal(true);
  setMinimumSize(DIALOG_WIDTH, DIALOG_HEIGHT);
  setupUI();
}

void ShortcutsDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(12);
  mainLayout->setContentsMargins(20, 20, 20, 20);

  // Scrollable content area
  auto *scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);

  auto *contentWidget = new QWidget();
  auto *contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setSpacing(SECTION_SPACING);
  contentLayout->setContentsMargins(0, 0, 10, 0);

  // Navigation
  addSection(contentLayout, tr("Navigation"));
  addShortcut(contentLayout, tr("Arrow Keys"), tr("Move selection"));
  addShortcut(contentLayout, tr("Enter"), tr("Open selected item / Enter subcollection"));
  addShortcut(contentLayout, tr("Escape"), tr("Go back / Clear search / Exit search mode"));
  addShortcut(contentLayout, tr("Home"), tr("Jump to first item"));
  addShortcut(contentLayout, tr("End"), tr("Jump to last item"));
  addShortcut(contentLayout, tr("Page Up"), tr("Jump to previous letter (alphabetic)"));
  addShortcut(contentLayout, tr("Page Down"), tr("Jump to next letter (alphabetic)"));

  // Search
  addSection(contentLayout, tr("Search"));
  addShortcut(contentLayout, tr("/"), tr("Focus search bar / Toggle search mode"));
  addShortcut(contentLayout, tr("Type letters"), tr("Quick filter (when search not focused)"));
  addShortcut(contentLayout, tr("Escape"), tr("Clear search text / Exit search"));

  // Window
  addSection(contentLayout, tr("Window"));
  addShortcut(contentLayout, tr("F11"), tr("Toggle fullscreen"));
  addShortcut(contentLayout, tr("F1"), tr("Show this help dialog"));

  // View
  addSection(contentLayout, tr("View"));
  addShortcut(contentLayout, tr("Ctrl++"), tr("Increase grid columns (smaller items)"));
  addShortcut(contentLayout, tr("Ctrl+-"), tr("Decrease grid columns (larger items)"));

  // Add stretch to push content to top
  contentLayout->addStretch();

  scrollArea->setWidget(contentWidget);
  mainLayout->addWidget(scrollArea, 1);

  // Close button
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  auto *closeButton = new QPushButton(tr("Close"), this);
  closeButton->setDefault(true);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton);
  mainLayout->addLayout(buttonLayout);
}

void ShortcutsDialog::addSection(QVBoxLayout *layout, const QString &title) {
  auto *sectionLabel = new QLabel(title, this);
  QFont font = sectionLabel->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 1);
  sectionLabel->setFont(font);
  sectionLabel->setStyleSheet("color: palette(highlight); margin-top: 8px;");
  layout->addWidget(sectionLabel);
}

void ShortcutsDialog::addShortcut(QVBoxLayout *layout, const QString &keys,
                                  const QString &description) {
  auto *row = new QHBoxLayout();
  row->setSpacing(16);

  auto *keysLabel = new QLabel(keys, this);
  keysLabel->setFixedWidth(120);
  keysLabel->setStyleSheet(
      "QLabel { "
      "background-color: palette(mid); "
      "border-radius: 3px; "
      "padding: 2px 6px; "
      "font-family: monospace; "
      "}");
  keysLabel->setAlignment(Qt::AlignCenter);

  auto *descLabel = new QLabel(description, this);
  descLabel->setWordWrap(true);

  row->addWidget(keysLabel);
  row->addWidget(descLabel, 1);

  auto *container = new QWidget(this);
  container->setLayout(row);
  layout->addWidget(container);
  layout->addSpacing(SHORTCUT_SPACING);
}
