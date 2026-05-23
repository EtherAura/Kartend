// Sibling translation unit for SettingsDialog: the left-rail navigation —
// category list population, page-stack switching, context header, settings
// search, and dialog-state persistence. These remain SettingsDialog members;
// this is a translation-unit split.
#include <initializer_list>
#include <QAbstractButton>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStringList>

#include "settingsdialog.h"
#include "settingskeys.h"
#include "ui_settingsdialog.h"
#include "uiconstants/icons.h"

namespace keys = kartend::settings::keys;

namespace {
// Page index lives in Qt::UserRole; -1 marks a non-selectable group header.
constexpr int kRolePage = Qt::UserRole;
// Newline-joined searchable keywords for the row's page.
constexpr int kRoleKeywords = Qt::UserRole + 1;
// True when the row's page edits the selected collection (vs. app-wide).
constexpr int kRolePerCollection = Qt::UserRole + 2;
constexpr int kHeaderPage = -1;
} // namespace

void SettingsDialog::setupNavigation() {
  // Rail stays compact; every extra pixel of width goes to the content pane.
  ui->mainSplitter->setStretchFactor(0, 0);
  ui->mainSplitter->setStretchFactor(1, 1);
  ui->mainSplitter->setSizes({260, 880});

  // Inside the rail, a draggable handle splits the collection tree from the
  // category list. Extra rail height grows the tree (so more collections stay
  // visible); the category list keeps its fixed row set.
  ui->navSplitter->setStretchFactor(0, 1);
  ui->navSplitter->setStretchFactor(1, 0);
  ui->navSplitter->setSizes({260, 460});
  // Render both splitter handles as plain transparent spacing instead of the
  // theme's painted accent bar — a colored handle clashes with the rounded
  // outlines in the rail. The handles stay draggable; only the fill is
  // dropped (the resize cursor still appears on hover).
  const QString transparentHandle =
      QStringLiteral("QSplitter::handle { background: transparent; }");
  ui->mainSplitter->setStyleSheet(transparentHandle);
  ui->navSplitter->setStyleSheet(transparentHandle);

  // Context-header title reads as a heading — bold and scaled up from the
  // base UI font so it tracks the user's font-size preference (no hard-coded
  // point size). The subtitle uses the theme's secondary text color.
  QFont titleFont = ui->contextTitleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
  ui->contextTitleLabel->setFont(titleFont);
  ui->contextSubtitleLabel->setForegroundRole(QPalette::PlaceholderText);

  QListWidget *list = ui->categoryList;

  auto addHeader = [list](const QString &text) {
    auto *item = new QListWidgetItem(text.toUpper(), list);
    item->setFlags(Qt::NoItemFlags);
    item->setData(kRolePage, kHeaderPage);
    QFont headerFont = item->font();
    headerFont.setBold(true);
    item->setFont(headerFont);
    item->setSizeHint(QSize(0, 26));
  };

  // Page indices map 1:1 to the QStackedWidget child order in settingsdialog.ui.
  auto addEntry = [list](const QString &label, std::initializer_list<const char *> icons, int page,
                         bool perCollection) {
    auto *item = new QListWidgetItem(UIConstants::Icons::fromTheme(icons), label, list);
    item->setData(kRolePage, page);
    item->setData(kRolePerCollection, perCollection);
  };

  addHeader(tr("Collection settings"));
  addEntry(tr("Configuration"), {"configure", "document-properties", "document-edit"}, 0, true);
  addEntry(tr("Artwork"), {"view-preview", "image-x-generic", "insert-image"}, 1, true);
  addEntry(tr("Appearance"), {"preferences-desktop-color", "applications-graphics", "fill-color"},
           2, true);
  addEntry(tr("Launcher"), {"system-run", "application-x-executable"}, 3, true);
  addEntry(tr("Subfolders"), {"folder", "folder-open"}, 4, true);
  addEntry(tr("Details Pane"),
           {"view-split-left-right", "format-justify-right", "sidebar-expand-right"}, 5, true);

  addHeader(tr("Application"));
  addEntry(tr("General"), {"preferences-other", "configure", "settings-configure"}, 6, false);
  addEntry(tr("Fonts"), {"preferences-desktop-font", "font-x-generic", "format-text-bold"}, 7,
           false);
  addEntry(tr("Splash"), {"preferences-desktop-display", "view-presentation"}, 8, false);
  addEntry(tr("Attract Mode"), {"preferences-desktop-screensaver", "media-playback-start"}, 9,
           false);
  addEntry(tr("Marquee"), {"video-display", "preferences-desktop-display"}, 10, false);
  addEntry(tr("Toolbar"), {"configure-toolbars", "show-menu"}, 11, false);
  addEntry(tr("Controls"), {"input-gaming", "preferences-desktop-keyboard"}, 12, false);
  addEntry(tr("Launchers"), {"system-run", "applications-utilities"}, 13, false);
  addEntry(tr("Scrapers"), {"folder-download", "edit-download", "cloud-download"}, 14, false);

  buildSettingsSearchIndex();

  connect(list, &QListWidget::currentItemChanged, this, &SettingsDialog::onNavigationRowChanged);
  connect(ui->settingsSearchEdit, &QLineEdit::textChanged, this,
          &SettingsDialog::onSettingsSearchTextChanged);

  // Open on the first real category (row 0 is the "Collection settings"
  // header) — this drives the initial page + context header via the
  // currentItemChanged connection above.
  list->setCurrentRow(1);

  // Section grids: each section is a QGridLayout sized to its own field
  // count — one, two, or three label/field pairs per row. Stretch every
  // field column (the odd ones) equally so the pairs span the section's
  // full width; uic ignores QGridLayout columnStretch from the .ui, so it
  // is applied here. Iterating to columnCount() keeps a two-column section
  // from reserving stretch for a phantom, unused third column.
  for (QGridLayout *grid : findChildren<QGridLayout *>()) {
    for (int col = 1; col < grid->columnCount(); col += 2) {
      grid->setColumnStretch(col, 1);
    }
  }

  applyUniformPageWidgetSizing();

  // Geometry / splitter restore comes after setSizes() so a saved layout wins.
  restoreDialogState();
}

void SettingsDialog::applyUniformPageWidgetSizing() {
  // Uniform width constants applied to every page in the QStackedWidget.
  // Mirrors the per-.ui constants and the kSpinMaxWidth / kComboMaxWidth /
  // kLabelMinWidth values in the programmatic scraper panels — kept in
  // sync so the visual rhythm doesn't drift as panels migrate between
  // .ui and code.
  constexpr int kFieldWidth = 320;    // line edits, combos, key-sequence edits
  constexpr int kSpinWidth = 120;     // spin boxes (numbers don't need 320)
  constexpr int kButtonMinWidth = 96; // helper buttons get a uniform floor
  constexpr int kLabelMinWidth = 200; // form-row labels share a column

  // Only touch widgets that live under a page in the QStackedWidget —
  // skips the nav rail (search bar, category list) and the dialog footer
  // (Save / OK / Cancel) so their sizing stays bespoke.
  for (int p = 0; p < ui->pageStack->count(); ++p) {
    QWidget *page = ui->pageStack->widget(p);
    if (!page) continue;

    for (auto *edit : page->findChildren<QLineEdit *>()) {
      edit->setMinimumWidth(kFieldWidth);
      edit->setMaximumWidth(kFieldWidth);
    }
    for (auto *combo : page->findChildren<QComboBox *>()) {
      combo->setMinimumWidth(kFieldWidth);
      combo->setMaximumWidth(kFieldWidth);
    }
    for (auto *spin : page->findChildren<QSpinBox *>()) {
      spin->setMinimumWidth(kSpinWidth);
      spin->setMaximumWidth(kSpinWidth);
    }
    for (auto *spin : page->findChildren<QDoubleSpinBox *>()) {
      spin->setMinimumWidth(kSpinWidth);
      spin->setMaximumWidth(kSpinWidth);
    }
    for (auto *keyEdit : page->findChildren<QKeySequenceEdit *>()) {
      keyEdit->setMinimumWidth(kFieldWidth);
      keyEdit->setMaximumWidth(kFieldWidth);
    }
    // Buttons get a floor, not a cap — long-text action buttons (Recursive
    // Import…, Load color scheme…, Export placeholder PNGs…) still grow to
    // fit their label, while short helpers (Browse, Pick, Detect, Add,
    // Remove, Edit) snap to the uniform 96 px column.
    for (auto *button : page->findChildren<QPushButton *>()) {
      button->setMinimumWidth(kButtonMinWidth);
    }

    // Label column: every QFormLayout's LabelRole widget gets the same
    // minimum width so labels align across groupboxes even when their
    // text is short. The .ui files set this on the QLabel itself; this
    // catches labels QFormLayout auto-creates from string addRow().
    for (auto *form : page->findChildren<QFormLayout *>()) {
      for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
        if (!item) continue;
        if (auto *label = qobject_cast<QLabel *>(item->widget())) {
          label->setMinimumWidth(kLabelMinWidth);
        }
      }
    }
  }
}

void SettingsDialog::onNavigationRowChanged() {
  const QListWidgetItem *item = ui->categoryList->currentItem();
  if (!item) {
    return;
  }
  const int page = item->data(kRolePage).toInt();
  if (page == kHeaderPage) {
    return; // header rows are non-selectable; guard defensively anyway
  }
  ui->pageStack->setCurrentIndex(page);
  updateContextHeader();
}

void SettingsDialog::updateContextHeader() {
  const QListWidgetItem *item = ui->categoryList->currentItem();
  if (!item || item->data(kRolePage).toInt() == kHeaderPage) {
    return;
  }

  ui->contextTitleLabel->setText(item->text());

  const QIcon icon = item->icon();
  if (icon.isNull()) {
    ui->contextIconLabel->clear();
    ui->contextIconLabel->setVisible(false);
  } else {
    // contextIconLabel is a fixed 36×36 box (settingsdialog.ui); render the
    // themed icon at that size so it stays crisp rather than upscaled.
    ui->contextIconLabel->setPixmap(icon.pixmap(36, 36));
    ui->contextIconLabel->setVisible(true);
  }

  if (item->data(kRolePerCollection).toBool()) {
    if (CollectionUtils::isValidIndex(currentCollectionIndex, &collections)) {
      ui->contextSubtitleLabel->setText(
          tr("Editing collection: %1").arg(collections[currentCollectionIndex].name));
    } else {
      ui->contextSubtitleLabel->setText(tr("No collection selected"));
    }
  } else {
    ui->contextSubtitleLabel->setText(tr("Applies to the whole application"));
  }
}

void SettingsDialog::buildSettingsSearchIndex() {
  for (int row = 0; row < ui->categoryList->count(); ++row) {
    QListWidgetItem *item = ui->categoryList->item(row);
    const int page = item->data(kRolePage).toInt();
    if (page == kHeaderPage) {
      continue;
    }
    const QWidget *pageWidget = ui->pageStack->widget(page);
    if (!pageWidget) {
      continue;
    }
    // Index every visible piece of static text on the page so a search like
    // "vignette" or "deadzone" surfaces the category that owns it.
    QStringList keywords;
    keywords << item->text();
    for (const QLabel *label : pageWidget->findChildren<QLabel *>()) {
      keywords << label->text();
    }
    for (const QAbstractButton *button : pageWidget->findChildren<QAbstractButton *>()) {
      keywords << button->text();
    }
    for (const QGroupBox *group : pageWidget->findChildren<QGroupBox *>()) {
      keywords << group->title();
    }
    QString joined = keywords.join(QLatin1Char('\n'));
    joined.remove(QLatin1Char('&')); // strip mnemonic markers
    item->setData(kRoleKeywords, joined);
  }
}

void SettingsDialog::onSettingsSearchTextChanged(const QString &text) {
  const QString query = text.trimmed();
  QListWidget *list = ui->categoryList;

  // Pass 1 — show/hide category rows by keyword match.
  QListWidgetItem *soleMatch = nullptr;
  int visibleMatches = 0;
  for (int row = 0; row < list->count(); ++row) {
    QListWidgetItem *item = list->item(row);
    if (item->data(kRolePage).toInt() == kHeaderPage) {
      continue; // headers handled in pass 2
    }
    bool match = query.isEmpty();
    if (!match) {
      const QString haystack =
          item->text() + QLatin1Char('\n') + item->data(kRoleKeywords).toString();
      match = haystack.contains(query, Qt::CaseInsensitive);
    }
    item->setHidden(!match);
    if (match && !query.isEmpty()) {
      ++visibleMatches;
      soleMatch = item;
    }
  }

  // Pass 2 — a group header shows only when at least one of the rows it
  // introduces (up to the next header) is still visible.
  QListWidgetItem *header = nullptr;
  bool headerHasVisibleChild = false;
  auto flushHeader = [&]() {
    if (header) {
      header->setHidden(!headerHasVisibleChild);
    }
  };
  for (int row = 0; row < list->count(); ++row) {
    QListWidgetItem *item = list->item(row);
    if (item->data(kRolePage).toInt() == kHeaderPage) {
      flushHeader();
      header = item;
      headerHasVisibleChild = false;
    } else if (!item->isHidden()) {
      headerHasVisibleChild = true;
    }
  }
  flushHeader();

  // When the query narrows to exactly one category, jump straight to it.
  if (visibleMatches == 1 && soleMatch) {
    list->setCurrentItem(soleMatch);
  }
}

void SettingsDialog::restoreDialogState() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(keys::kGroupSettingsDialog);
  const QByteArray geometry = settings.value(keys::kGeometry).toByteArray();
  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
  }
  const QByteArray splitterState = settings.value(keys::kSplitterState).toByteArray();
  if (!splitterState.isEmpty()) {
    ui->mainSplitter->restoreState(splitterState);
  }
  const QByteArray navSplitterState = settings.value(keys::kNavSplitterState).toByteArray();
  if (!navSplitterState.isEmpty()) {
    ui->navSplitter->restoreState(navSplitterState);
  }
  settings.endGroup();
}

void SettingsDialog::saveDialogState() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(keys::kGroupSettingsDialog);
  settings.setValue(keys::kGeometry, saveGeometry());
  settings.setValue(keys::kSplitterState, ui->mainSplitter->saveState());
  settings.setValue(keys::kNavSplitterState, ui->navSplitter->saveState());
  settings.endGroup();
}
