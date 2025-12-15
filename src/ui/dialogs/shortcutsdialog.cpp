// Keyboard shortcuts help dialog
#include "shortcutsdialog.h"

#include "mainwindow.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
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

void ShortcutsDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  populateContent();
}

void ShortcutsDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(12);
  mainLayout->setContentsMargins(20, 20, 20, 20);

  // Scrollable content area
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setFrameShape(QFrame::NoFrame);

  m_contentWidget = new QWidget();
  m_contentLayout = new QVBoxLayout(m_contentWidget);
  m_contentLayout->setSpacing(SECTION_SPACING);
  m_contentLayout->setContentsMargins(0, 0, 10, 0);

  m_scrollArea->setWidget(m_contentWidget);
  mainLayout->addWidget(m_scrollArea, 1);

  // Close button
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  auto *closeButton = new QPushButton(tr("Close"), this);
  closeButton->setDefault(true);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton);
  mainLayout->addLayout(buttonLayout);

  populateContent();
}

void ShortcutsDialog::populateContent() {
  if (!m_contentLayout) {
    return;
  }

  clearLayout(m_contentLayout);

  const auto *mw = qobject_cast<MainWindow *>(parent());
  const auto settings = mw ? mw->m_generalSettings : GeneralSettings{};

  auto keyText = [](int key) -> QString {
    const QKeySequence seq(key);
    const QString txt = seq.toString(QKeySequence::NativeText);
    return txt.isEmpty() ? QStringLiteral("(None)") : txt;
  };

  auto buttonText = [](const QString &btn) -> QString {
    const QString t = btn.trimmed();
    return t.isEmpty() ? QStringLiteral("(Unbound)") : t;
  };

  // Navigation
  addSection(m_contentLayout, tr("Navigation"));
  addShortcut(m_contentLayout, keyText(settings.keyNavUp), tr("Move selection up"));
  addShortcut(m_contentLayout, keyText(settings.keyNavDown), tr("Move selection down"));
  addShortcut(m_contentLayout, keyText(settings.keyNavLeft), tr("Move selection left"));
  addShortcut(m_contentLayout, keyText(settings.keyNavRight), tr("Move selection right"));
  addShortcut(m_contentLayout, keyText(settings.keyConfirm),
              tr("Open selected item / Enter subcollection"));
  addShortcut(m_contentLayout, keyText(settings.keyBack),
              tr("Go back / Clear search / Exit search mode"));
  addShortcut(m_contentLayout, keyText(settings.keyJumpFirst), tr("Jump to first item"));
  addShortcut(m_contentLayout, keyText(settings.keyJumpLast), tr("Jump to last item"));
  addShortcut(m_contentLayout, keyText(settings.keyAlphabeticBack),
              tr("Jump to previous letter (alphabetic)"));
  addShortcut(m_contentLayout, keyText(settings.keyAlphabeticForward),
              tr("Jump to next letter (alphabetic)"));

  // Search
  addSection(m_contentLayout, tr("Search"));
  addShortcut(m_contentLayout, keyText(settings.keySearch),
              tr("Focus search bar / Toggle search mode"));
  addShortcut(m_contentLayout, tr("Type letters"),
              tr("Quick filter (when search not focused)"));
  addShortcut(m_contentLayout, keyText(settings.keyBack),
              tr("Clear search text / Exit search"));

  // Gamepad
  addSection(m_contentLayout, tr("Gamepad"));
  addShortcut(m_contentLayout, tr("D-pad"),
              settings.gamepadUseDpad ? tr("Move selection (enabled)")
                                       : tr("Move selection (disabled)"));
  addShortcut(m_contentLayout, tr("Left stick"),
              settings.gamepadUseLeftStick ? tr("Move selection (enabled)")
                                            : tr("Move selection (disabled)"));
  addShortcut(m_contentLayout, buttonText(settings.gamepadConfirmButton),
              tr("Confirm / Open selected item"));
  addShortcut(m_contentLayout, buttonText(settings.gamepadBackButton),
              tr("Back / Escape"));
  addShortcut(m_contentLayout, buttonText(settings.gamepadToggleSidebarButton),
              tr("Toggle metadata sidebar"));

  // Window
  addSection(m_contentLayout, tr("Window"));
  addShortcut(m_contentLayout, tr("F11"), tr("Toggle fullscreen"));
  addShortcut(m_contentLayout, tr("F1"), tr("Show this help dialog"));

  // View
  addSection(m_contentLayout, tr("View"));
  addShortcut(m_contentLayout, tr("Ctrl++"),
              tr("Increase grid columns (smaller items)"));
  addShortcut(m_contentLayout, tr("Ctrl+-"),
              tr("Decrease grid columns (larger items)"));

  // Add stretch to push content to top
  m_contentLayout->addStretch();
}

void ShortcutsDialog::clearLayout(QLayout *layout) {
  if (!layout) {
    return;
  }

  while (QLayoutItem *item = layout->takeAt(0)) {
    if (QWidget *w = item->widget()) {
      w->deleteLater();
    }
    if (QLayout *childLayout = item->layout()) {
      clearLayout(childLayout);
      childLayout->deleteLater();
    }
    delete item;
  }
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
