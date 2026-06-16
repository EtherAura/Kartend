// Per-item editor for artwork override paths.
#include "itemartworklinksdialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include "itemartwork.h"

namespace {
constexpr int kColumnLabel = 0;
constexpr int kColumnPath = 1;
constexpr int kColumnBrowse = 2;
constexpr int kColumnClear = 3;
} // namespace

ItemArtworkLinksDialog::ItemArtworkLinksDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void ItemArtworkLinksDialog::setupUi() {
  setWindowTitle(tr("Artwork links"));
  resize(640, 360);

  auto *layout = new QVBoxLayout(this);

  auto *header = new QLabel(this);
  header->setObjectName("artworkLinksHeader");
  header->setWordWrap(true);
  header->setText(tr("Pick a file for any artwork type. Clearing the path falls back to "
                     "auto-discovery for standard types; custom types stay hidden until a file "
                     "is set."));
  layout->addWidget(header);

  m_table = new QTableWidget(0, 4, this);
  m_table->setHorizontalHeaderLabels({tr("Type"), tr("Override path"), QString(), QString()});
  m_table->horizontalHeader()->setSectionResizeMode(kColumnLabel, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(kColumnPath, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(kColumnBrowse, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(kColumnClear, QHeaderView::ResizeToContents);
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  // Row label and the browse/clear buttons are display-only / action cells;
  // the path column accepts direct edits so users can paste an existing
  // absolute path without needing to round-trip through a file picker.
  m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
                           QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
  layout->addWidget(m_table, /*stretch=*/1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ItemArtworkLinksDialog::setItemTitle(const QString &title) {
  if (auto *header = findChild<QLabel *>("artworkLinksHeader")) {
    if (title.isEmpty()) {
      header->setText(tr("Pick a file for any artwork type."));
    } else {
      header->setText(tr("Artwork links for: %1").arg(title));
    }
  }
}

void ItemArtworkLinksDialog::setTypeRows(const QStringList &standardTypes,
                                         const QStringList &customTypes) {
  m_table->setRowCount(0);
  m_rowTypeIds.clear();
  m_rowIsStandard.clear();
  for (const QString &type : standardTypes) {
    QString display = ItemArtworkStore::standardTypeDisplayName(type);
    if (display.isEmpty()) {
      display = type;
    }
    appendRow(type, display, /*isStandard=*/true);
  }
  for (const QString &type : customTypes) {
    appendRow(type, type, /*isStandard=*/false);
  }
}

void ItemArtworkLinksDialog::appendRow(const QString &typeId, const QString &displayLabel,
                                       bool isStandard) {
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  m_rowTypeIds.append(typeId);
  m_rowIsStandard.append(isStandard);

  auto *labelItem = new QTableWidgetItem(displayLabel);
  labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
  if (!isStandard) {
    labelItem->setToolTip(tr("Custom type — only resolves via this dialog (no auto-discovery)."));
  }
  m_table->setItem(row, kColumnLabel, labelItem);

  auto *pathItem = new QTableWidgetItem(QString());
  pathItem->setToolTip(
      isStandard ? tr("Leave empty to auto-discover from {artworkDirectory}/%1/").arg(typeId)
                 : tr("Leave empty to hide this custom type from the gallery."));
  m_table->setItem(row, kColumnPath, pathItem);

  auto *browseBtn = new QToolButton(m_table);
  browseBtn->setText(tr("Browse…"));
  browseBtn->setToolTip(tr("Pick an artwork file"));
  connect(browseBtn, &QToolButton::clicked, this, [this, row]() { onBrowseClicked(row); });
  m_table->setCellWidget(row, kColumnBrowse, browseBtn);

  auto *clearBtn = new QToolButton(m_table);
  clearBtn->setText(tr("Clear"));
  clearBtn->setToolTip(tr("Remove the override and restore default behaviour"));
  connect(clearBtn, &QToolButton::clicked, this, [this, row]() { onClearClicked(row); });
  m_table->setCellWidget(row, kColumnClear, clearBtn);
}

void ItemArtworkLinksDialog::setOverrides(const QHash<QString, QString> &overrides) {
  for (int row = 0; row < m_rowTypeIds.size(); ++row) {
    const QString &typeId = m_rowTypeIds.at(row);
    const QString path = overrides.value(typeId);
    setOverrideTextForRow(row, path);
  }
}

QHash<QString, QString> ItemArtworkLinksDialog::overrides() const {
  QHash<QString, QString> out;
  for (int row = 0; row < m_rowTypeIds.size(); ++row) {
    const QString &typeId = m_rowTypeIds.at(row);
    const QString text = currentOverrideForRow(row).trimmed();
    if (text.isEmpty()) {
      continue;
    }
    // A row whose value still matches the auto-discovered path is
    // treated as "no override" — promoting it to a DB override would
    // freeze the path and stop future auto-discovery from following
    // file renames inside `{artwork}/{type}/`.
    if (m_autoPathByType.value(typeId) == text) {
      continue;
    }
    out.insert(typeId, text);
  }
  return out;
}

void ItemArtworkLinksDialog::setAutoResolvedPaths(const QHash<QString, QString> &autoPaths) {
  m_autoPathByType = autoPaths;
  for (int row = 0; row < m_rowTypeIds.size(); ++row) {
    const QString &typeId = m_rowTypeIds.at(row);
    QTableWidgetItem *item = m_table->item(row, kColumnPath);
    if (!item) {
      continue;
    }
    // Only auto-fill rows that don't already carry a real override —
    // we don't want to overwrite a saved path with the auto hint.
    if (!item->text().trimmed().isEmpty()) {
      continue;
    }
    const QString autoPath = autoPaths.value(typeId);
    if (autoPath.isEmpty()) {
      continue;
    }
    item->setText(autoPath);
    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);
    // Use the disabled-text role so the auto hint adapts to the
    // active palette (dark / light) instead of a fixed gray.
    item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    item->setToolTip(tr("Auto-discovered from %1. Edit to override.").arg(autoPath));
  }
}

void ItemArtworkLinksDialog::changeEvent(QEvent *event) {
  QDialog::changeEvent(event);
  // The auto-hint rows bake palette(Disabled, Text) as a concrete brush; the
  // override (non-hint) rows use the default foreground. Auto-hint rows are
  // the italic ones, so re-stamp the brush on those when the palette changes.
  if (event->type() == QEvent::PaletteChange && m_table) {
    const QBrush hint = palette().brush(QPalette::Disabled, QPalette::Text);
    for (int row = 0; row < m_table->rowCount(); ++row) {
      QTableWidgetItem *item = m_table->item(row, kColumnPath);
      if (item && item->font().italic()) {
        item->setForeground(hint);
      }
    }
  }
}

QString ItemArtworkLinksDialog::currentOverrideForRow(int row) const {
  if (row < 0 || row >= m_table->rowCount()) {
    return {};
  }
  QTableWidgetItem *item = m_table->item(row, kColumnPath);
  return item ? item->text() : QString();
}

void ItemArtworkLinksDialog::setOverrideTextForRow(int row, const QString &path) {
  if (row < 0 || row >= m_table->rowCount()) {
    return;
  }
  if (auto *item = m_table->item(row, kColumnPath)) {
    item->setText(path);
  }
}

void ItemArtworkLinksDialog::onBrowseClicked(int row) {
  if (row < 0 || row >= m_table->rowCount()) {
    return;
  }
  QString startDir = currentOverrideForRow(row);
  if (!startDir.isEmpty()) {
    startDir = QFileInfo(startDir).absolutePath();
  }
  if (startDir.isEmpty()) {
    startDir = m_browseStartDir;
  }
  // Image-only filter mirrors ArtworkUtils' supported extensions plus a
  // permissive "Any file" fallback so the user can still pick something
  // unusual (e.g. a converted .tga) without us blocking it here. The
  // gallery renders whatever QPixmap can decode at runtime.
  const QString filters = tr("Image files (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;Any file (*)");
  const QString chosen =
      QFileDialog::getOpenFileName(this, tr("Select artwork file"), startDir, filters);
  if (chosen.isEmpty()) {
    return;
  }
  setOverrideTextForRow(row, chosen);
}

void ItemArtworkLinksDialog::onClearClicked(int row) {
  setOverrideTextForRow(row, QString());
}
