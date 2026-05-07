// Keyboard shortcuts help dialog
#include "shortcutsdialog.h"

#include "mainwindow.h"

#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
constexpr int DIALOG_WIDTH = 760;
constexpr int DIALOG_HEIGHT = 560;
constexpr int SECTION_SPACING = 10;
constexpr int SHORTCUT_SPACING = 2;
} // namespace

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
  mainLayout->setSpacing(10);
  mainLayout->setContentsMargins(12, 12, 12, 12);

  // Scrollable content area
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setFrameShape(QFrame::NoFrame);

  m_contentWidget = new QWidget();
  m_columnsLayout = new QHBoxLayout(m_contentWidget);
  m_columnsLayout->setSpacing(12);
  m_columnsLayout->setContentsMargins(0, 0, 10, 0);

  m_leftColumnLayout = new QVBoxLayout();
  m_leftColumnLayout->setSpacing(SECTION_SPACING);
  m_rightColumnLayout = new QVBoxLayout();
  m_rightColumnLayout->setSpacing(SECTION_SPACING);

  m_columnsLayout->addLayout(m_leftColumnLayout, 1);
  m_columnsLayout->addLayout(m_rightColumnLayout, 1);

  m_scrollArea->setWidget(m_contentWidget);
  mainLayout->addWidget(m_scrollArea, 1);

  // Close button
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  auto *closeButton = new QPushButton(tr("Close"), this);
  closeButton->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::WindowClose));
  closeButton->setDefault(true);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton);
  mainLayout->addLayout(buttonLayout);

  populateContent();
}

void ShortcutsDialog::populateContent() {
  if (!m_leftColumnLayout || !m_rightColumnLayout) {
    return;
  }

  clearLayout(m_leftColumnLayout);
  clearLayout(m_rightColumnLayout);

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

  // Left column: Navigation, Search
  auto *navSection = addSection(m_leftColumnLayout, tr("Navigation"));
  addShortcut(navSection, keyText(settings.keyNavUp), tr("Move selection up"));
  addShortcut(navSection, keyText(settings.keyNavDown), tr("Move selection down"));
  addShortcut(navSection, keyText(settings.keyNavLeft), tr("Move selection left"));
  addShortcut(navSection, keyText(settings.keyNavRight), tr("Move selection right"));
  addShortcut(navSection, keyText(settings.keyConfirm),
              tr("Open selected item / Enter subcollection"));
  addShortcut(navSection, keyText(settings.keyBack),
              tr("Go back / Clear search / Exit search mode"));
  addShortcut(navSection, keyText(settings.keyJumpFirst), tr("Jump to first item"));
  addShortcut(navSection, keyText(settings.keyJumpLast), tr("Jump to last item"));
  addShortcut(navSection, keyText(settings.keyAlphabeticBack),
              tr("Jump to previous letter (alphabetic)"));
  addShortcut(navSection, keyText(settings.keyAlphabeticForward),
              tr("Jump to next letter (alphabetic)"));
  addShortcut(navSection, keyText(settings.keyItemDetails), tr("Open item detail page"));

  auto *searchSection = addSection(m_leftColumnLayout, tr("Search"));
  addShortcut(searchSection, keyText(settings.keySearch),
              tr("Focus search bar / Toggle search mode"));
  addShortcut(searchSection, tr("Type letters"), tr("Quick filter (when search not focused)"));
  addShortcut(searchSection, keyText(settings.keyBack), tr("Clear search text / Exit search"));

  m_leftColumnLayout->addStretch();

  // Right column: Gamepad, Mouse, Window, View
  auto *gamepadSection = addSection(m_rightColumnLayout, tr("Gamepad"));
  addShortcut(gamepadSection, tr("D-pad"),
              settings.gamepadUseDpad ? tr("Move selection (enabled)")
                                      : tr("Move selection (disabled)"));
  addShortcut(gamepadSection, tr("Left stick"),
              settings.gamepadUseLeftStick ? tr("Move selection (enabled)")
                                           : tr("Move selection (disabled)"));
  addShortcut(gamepadSection, buttonText(settings.gamepadConfirmButton),
              tr("Confirm / Open selected item"));
  addShortcut(gamepadSection, buttonText(settings.gamepadBackButton), tr("Back / Escape"));
  addShortcut(gamepadSection, buttonText(settings.gamepadToggleSidebarButton),
              tr("Toggle details pane"));

  auto *mouseSection = addSection(m_rightColumnLayout, tr("Mouse"));
  addShortcut(mouseSection, tr("Middle-click"), tr("Open media preview overlay"));
  auto modifierLabel = [&]() -> QString {
    switch (settings.artworkCycleModifier) {
    case static_cast<int>(Qt::ControlModifier):
      return tr("Ctrl");
    case static_cast<int>(Qt::AltModifier):
      return tr("Alt");
    case static_cast<int>(Qt::MetaModifier):
      return tr("Meta");
    case static_cast<int>(Qt::ShiftModifier):
    default:
      return tr("Shift");
    }
  }();
  addShortcut(mouseSection, tr("%1+Middle-click").arg(modifierLabel),
              tr("Cycle item artwork to next available type"));

  auto *windowSection = addSection(m_rightColumnLayout, tr("Window"));
  addShortcut(windowSection, tr("F11"), tr("Toggle fullscreen"));
  addShortcut(windowSection, tr("F1"), tr("Show this help dialog"));

  auto *viewSection = addSection(m_rightColumnLayout, tr("View"));
  addShortcut(viewSection, tr("Ctrl++"), tr("Increase text zoom"));
  addShortcut(viewSection, tr("Ctrl+-"), tr("Decrease text zoom"));
  addShortcut(viewSection, tr("Ctrl+0"), tr("Reset text zoom"));
  addShortcut(viewSection, tr("Ctrl+Shift++"), tr("Increase grid columns (smaller items)"));
  addShortcut(viewSection, tr("Ctrl+Shift+-"), tr("Decrease grid columns (larger items)"));

  m_rightColumnLayout->addStretch();
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

QVBoxLayout *ShortcutsDialog::addSection(QVBoxLayout *parentLayout, const QString &title) {
  auto *box = new QGroupBox(title, this);
  auto *boxLayout = new QVBoxLayout(box);
  boxLayout->setSpacing(SHORTCUT_SPACING);
  boxLayout->setContentsMargins(10, 8, 10, 8);
  parentLayout->addWidget(box);
  return boxLayout;
}

void ShortcutsDialog::addShortcut(QVBoxLayout *layout, const QString &keys,
                                  const QString &description) {
  auto *row = new QHBoxLayout();
  row->setSpacing(12);

  auto *keysLabel = new QLabel(keys, this);
  keysLabel->setMinimumWidth(110);
  keysLabel->setStyleSheet("QLabel { "
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
}
