#include "configurationpanel.h"

#include "extensionutils.h"
#include "metadataproviderregistry.h"
#include "screenscrapersystemcache.h"
#include "screenscrapersystems.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_configurationpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVariant>

ConfigurationPanel::ConfigurationPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::ConfigurationPanel) {
  ui->setupUi(this);

  // The scraper-provider list is fixed in-binary, so populate it once
  // here; load() only moves the selection. Index 0 (empty id) is the
  // "automatic — resolve by collection type" choice.
  ui->scraperProviderComboBox->addItem(tr("Automatic (match by type)"), QString());
  for (const auto &choice : MetadataProviderRegistry::scrapingProviders()) {
    ui->scraperProviderComboBox->addItem(choice.displayName, choice.id);
  }

  // Per-field changed() emission. Parent combo + linked-parents + recursive-
  // import are not wired here — the host dialog owns those connections.
  connect(ui->collectionTypeComboBox, &QComboBox::currentTextChanged, this,
          [this](const QString &) {
            autodetectScreenscraperSystem();
            emit changed();
          });
  connect(ui->scraperProviderComboBox, &QComboBox::currentIndexChanged, this,
          [this](int) { emit changed(); });
  connect(ui->mediaDirLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->fileExtensionsLineEdit, &QLineEdit::textChanged, this, [this](const QString &) {
    autodetectScreenscraperSystem();
    emit changed();
  });
  connect(ui->expandModeCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  connect(ui->showAllSubcollectionItemsCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->watchFilesystemCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  connect(ui->screenscraperSystemComboBox, &QComboBox::currentIndexChanged, this,
          [this](int) { emit changed(); });
  // A hand-pick freezes the type/extension → system autodetect so a
  // later edit to those fields can't override the deliberate choice.
  connect(ui->screenscraperSystemComboBox, &QComboBox::activated, this,
          [this](int) { m_systemManuallySet = true; });
  connect(ui->screenscraperHashArchiveCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  // Drag-reorder + row deletion both emit a change. The list-widget
  // model emits rowsMoved when the user drags; rowsInserted /
  // rowsRemoved fire from Add… and Remove.
  connect(ui->datFilesListWidget->model(), &QAbstractItemModel::rowsMoved, this,
          [this](const QModelIndex &, int, int, const QModelIndex &, int) { emit changed(); });
  connect(ui->datFilesListWidget->model(), &QAbstractItemModel::rowsInserted, this,
          [this](const QModelIndex &, int, int) { emit changed(); });
  connect(ui->datFilesListWidget->model(), &QAbstractItemModel::rowsRemoved, this,
          [this](const QModelIndex &, int, int) { emit changed(); });
  connect(ui->addDatFileButton, &QPushButton::clicked, this, [this]() {
    // Seed the dialog from the last entry's folder so re-picking from
    // the same DAT directory is one click. Multi-select so users can
    // add a folder of system-DATs in one shot.
    QString startDir;
    if (ui->datFilesListWidget->count() > 0) {
      const QString last =
          ui->datFilesListWidget->item(ui->datFilesListWidget->count() - 1)->text().trimmed();
      if (!last.isEmpty()) startDir = QFileInfo(last).absolutePath();
    }
    const QStringList picked = QFileDialog::getOpenFileNames(
        this, tr("Add DAT files"), startDir, tr("DAT files (*.dat *.xml);;All files (*)"));
    for (const QString &path : picked) {
      if (path.isEmpty()) continue;
      // De-dupe — adding the same DAT twice would waste scrape time
      // and clutter the list.
      bool duplicate = false;
      for (int i = 0; i < ui->datFilesListWidget->count(); ++i) {
        if (ui->datFilesListWidget->item(i)->text() == path) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) ui->datFilesListWidget->addItem(path);
    }
  });
  connect(ui->removeDatFileButton, &QPushButton::clicked, this, [this]() {
    // Walk selection in reverse so descending takeItem indexes stay
    // valid through the loop. Falls back to "remove the current row"
    // when nothing is explicitly selected — keeps a single-tap
    // remove flow available.
    QList<int> rows;
    const auto selected = ui->datFilesListWidget->selectedItems();
    for (QListWidgetItem *item : selected) {
      rows.append(ui->datFilesListWidget->row(item));
    }
    if (rows.isEmpty() && ui->datFilesListWidget->currentRow() >= 0) {
      rows.append(ui->datFilesListWidget->currentRow());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) delete ui->datFilesListWidget->takeItem(row);
  });
}

ConfigurationPanel::~ConfigurationPanel() {
  delete ui;
}

namespace {

// Populate the SS systemeid combo from the on-disk catalog cache. The
// cache is written by ScreenScraperProvider on its first successful
// systemesListe.php fetch, so cold-start (user hasn't scraped yet)
// just shows Auto-detect plus a hint label. We deliberately don't
// trigger a network fetch from the settings dialog — fetching needs
// dev credentials and can fail; the user-flow that surfaces those
// errors is the actual scrape, not this dropdown.
//
// `currentValue` is the current per-collection systemeid (-1 = unset
// = Auto-detect). When it's a positive id that the cache doesn't
// know about (e.g. hand-edited stale value, deleted cache file),
// we add an "(unknown id N)" entry so the user can still see / keep
// the value rather than having it silently swap to Auto-detect.
// Returns the loaded catalog so the caller can keep it for the
// type/extension-driven autodetect (the combo stores only id +
// display name, not the aliases autodetect scores against).
QList<ScreenScraperSystems::System> populateSystemsCombo(QComboBox *combo, QLabel *hintLabel,
                                                         int currentValue) {
  QSignalBlocker blocker(combo);
  combo->clear();
  combo->addItem(ConfigurationPanel::tr("Auto-detect"), -1);

  QList<ScreenScraperSystems::System> systems;
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  if (!cachePath.isEmpty()) {
    auto loaded = ScreenScraperSystemCache::loadCachedSystems(cachePath);
    if (loaded.isOk()) systems = loaded.value();
  }
  // Sort alphabetically by display name so the user can scan the
  // dropdown linearly. SS returns the catalog in their own order
  // (rough chronological-by-system-add), which is not useful for UI.
  std::sort(systems.begin(), systems.end(),
            [](const ScreenScraperSystems::System &a, const ScreenScraperSystems::System &b) {
              return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
            });
  for (const auto &sys : systems) {
    if (sys.id < 0 || sys.displayName.isEmpty()) continue;
    combo->addItem(sys.displayName, sys.id);
  }

  if (hintLabel) hintLabel->setVisible(systems.isEmpty());

  // Select the current value. If it's -1 (Auto-detect) it's always
  // index 0; otherwise look it up in the loaded catalog. When the
  // value is set but the catalog doesn't carry it, append a sticky
  // "unknown" entry so the user keeps the value rather than losing it.
  int idx = combo->findData(currentValue);
  if (idx < 0 && currentValue >= 0) {
    combo->addItem(ConfigurationPanel::tr("(unknown id %1)").arg(currentValue), currentValue);
    idx = combo->count() - 1;
  }
  if (idx < 0) idx = 0;
  combo->setCurrentIndex(idx);
  return systems;
}

} // namespace

void ConfigurationPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void ConfigurationPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // collectionTypeComboBox is populated separately via setKnownTypes; just
  // set the current text here.
  {
    QSignalBlocker blocker(ui->collectionTypeComboBox);
    ui->collectionTypeComboBox->setCurrentText(config.type);
  }
  {
    // findData on the empty id matches the Automatic entry; an
    // unrecognised stored id (hand-edit, or a provider dropped in a
    // later build) also falls back to Automatic.
    QSignalBlocker blocker(ui->scraperProviderComboBox);
    const int idx =
        ui->scraperProviderComboBox->findData(config.scraperOverrides.scraperProviderId);
    ui->scraperProviderComboBox->setCurrentIndex(idx >= 0 ? idx : 0);
  }
  SettingsFormBinding::loadInto(ui->mediaDirLineEdit, config.mediaDirectory);
  SettingsFormBinding::loadInto(ui->fileExtensionsLineEdit, config.extensions.join(", "));
  SettingsFormBinding::loadInto(ui->expandModeCheckBox, config.expandMode);
  SettingsFormBinding::loadInto(ui->showAllSubcollectionItemsCheckBox,
                                config.showAllSubcollectionItems);
  SettingsFormBinding::loadInto(ui->watchFilesystemCheckBox, config.watchFilesystem);
  m_screenscraperSystems =
      populateSystemsCombo(ui->screenscraperSystemComboBox, ui->screenscraperSystemHintLabel,
                           config.scraperOverrides.screenscraperSystemId);
  // A collection that already pins a concrete system counts as
  // manually set — don't let an autodetect on a later type/extension
  // edit override the user's explicit choice. Auto-detect (-1) stays
  // open to autodetect.
  m_systemManuallySet = config.scraperOverrides.screenscraperSystemId != -1;
  SettingsFormBinding::loadInto(ui->screenscraperHashArchiveCheckBox,
                                config.scraperOverrides.screenscraperHashArchive);
  ui->datFilesListWidget->clear();
  ui->datFilesListWidget->addItems(config.scraperOverrides.datFilePaths);
}

void ConfigurationPanel::autodetectScreenscraperSystem() {
  if (m_systemManuallySet) {
    return;
  }
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  // The collection name carries the platform tag autodetect scores
  // against; the editable type + extension fields refine it.
  const QString name = (*m_model->workingCollections)[*m_model->currentIndex].name;
  const QStringList extensions =
      ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text());
  const int detected = ScreenScraperSystems::autodetect(
      name, ui->collectionTypeComboBox->currentText(), extensions, m_screenscraperSystems);
  const int idx = detected > 0 ? ui->screenscraperSystemComboBox->findData(detected) : 0;
  ui->screenscraperSystemComboBox->setCurrentIndex(idx >= 0 ? idx : 0);
}

void ConfigurationPanel::clear() {
  {
    QSignalBlocker blocker(ui->collectionTypeComboBox);
    ui->collectionTypeComboBox->clear();
  }
  {
    // Items are fixed — just reset the selection to Automatic.
    QSignalBlocker blocker(ui->scraperProviderComboBox);
    ui->scraperProviderComboBox->setCurrentIndex(0);
  }
  ui->mediaDirLineEdit->clear();
  ui->fileExtensionsLineEdit->clear();
  ui->expandModeCheckBox->setChecked(false);
  ui->showAllSubcollectionItemsCheckBox->setChecked(false);
  ui->watchFilesystemCheckBox->setChecked(false);
  {
    QSignalBlocker blocker(ui->screenscraperSystemComboBox);
    ui->screenscraperSystemComboBox->clear();
  }
  ui->screenscraperHashArchiveCheckBox->setChecked(true);
  ui->datFilesListWidget->clear();
}

void ConfigurationPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // Read free-form type label from the editable combobox. currentText()
  // (rather than currentIndex()) lets a freshly typed value not yet
  // committed via Enter round-trip; trimming prevents accidental padding
  // from fragmenting the type set across collections.
  config.type = ui->collectionTypeComboBox->currentText().trimmed();
  // Scraper override: empty id (Automatic) means resolve by type at
  // scrape time; a concrete id pins that provider.
  config.scraperOverrides.scraperProviderId = ui->scraperProviderComboBox->currentData().toString();
  // Media dir preserves exact text (no trim) — matches legacy behavior.
  config.mediaDirectory = ui->mediaDirLineEdit->text();
  config.extensions = ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text());
  config.expandMode = ui->expandModeCheckBox->isChecked();
  config.showAllSubcollectionItems = ui->showAllSubcollectionItemsCheckBox->isChecked();
  config.watchFilesystem = ui->watchFilesystemCheckBox->isChecked();
  // Combo carries the systemeid as item data (Auto-detect → -1).
  // currentData().toInt() returns 0 for an empty combo, so guard with
  // the index check — clear() leaves the combo empty until load() runs.
  if (ui->screenscraperSystemComboBox->currentIndex() >= 0) {
    config.scraperOverrides.screenscraperSystemId =
        ui->screenscraperSystemComboBox->currentData().toInt();
  }
  config.scraperOverrides.screenscraperHashArchive =
      ui->screenscraperHashArchiveCheckBox->isChecked();
  config.scraperOverrides.datFilePaths.clear();
  config.scraperOverrides.datFilePaths.reserve(ui->datFilesListWidget->count());
  for (int i = 0; i < ui->datFilesListWidget->count(); ++i) {
    const QString path = ui->datFilesListWidget->item(i)->text();
    if (!path.isEmpty()) config.scraperOverrides.datFilePaths.append(path);
  }
}

bool ConfigurationPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->collectionTypeComboBox->currentText().trimmed() != o.type) return true;
  if (ui->scraperProviderComboBox->currentData().toString() != o.scraperOverrides.scraperProviderId)
    return true;
  if (ui->mediaDirLineEdit->text() != o.mediaDirectory) return true;
  if (ui->expandModeCheckBox->isChecked() != o.expandMode) return true;
  if (ui->showAllSubcollectionItemsCheckBox->isChecked() != o.showAllSubcollectionItems)
    return true;
  if (ui->watchFilesystemCheckBox->isChecked() != o.watchFilesystem) return true;
  // Compare against the parsed form so cosmetic comma-spacing tweaks don't
  // register as dirty.
  const QStringList parsed =
      ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text());
  if (parsed != o.extensions) return true;
  if (ui->screenscraperSystemComboBox->currentIndex() >= 0 &&
      ui->screenscraperSystemComboBox->currentData().toInt() !=
          o.scraperOverrides.screenscraperSystemId) {
    return true;
  }
  if (ui->screenscraperHashArchiveCheckBox->isChecked() !=
      o.scraperOverrides.screenscraperHashArchive) {
    return true;
  }
  // Compare against the rebuilt list so duplicate-drop / empty-trim
  // logic in save() doesn't register as a spurious diff against the
  // original.
  QStringList current;
  current.reserve(ui->datFilesListWidget->count());
  for (int i = 0; i < ui->datFilesListWidget->count(); ++i) {
    const QString path = ui->datFilesListWidget->item(i)->text();
    if (!path.isEmpty()) current.append(path);
  }
  if (current != o.scraperOverrides.datFilePaths) {
    return true;
  }
  return false;
}

void ConfigurationPanel::setKnownTypes(const QStringList &knownTypes, const QString &currentText) {
  QSignalBlocker blocker(ui->collectionTypeComboBox);
  ui->collectionTypeComboBox->clear();
  ui->collectionTypeComboBox->addItems(knownTypes);
  ui->collectionTypeComboBox->setCurrentText(currentText);
}

QComboBox *ConfigurationPanel::parentCollectionComboBox() const {
  return ui->parentCollectionComboBox;
}
QPushButton *ConfigurationPanel::editLinkedParentsButton() const {
  return ui->editLinkedParentsButton;
}
QPushButton *ConfigurationPanel::recursiveImportContentButton() const {
  return ui->recursiveImportContentButton;
}
QPushButton *ConfigurationPanel::browseMediaDirButton() const {
  return ui->browseMediaDirButton;
}
QLineEdit *ConfigurationPanel::mediaDirLineEdit() const {
  return ui->mediaDirLineEdit;
}
QLineEdit *ConfigurationPanel::fileExtensionsLineEdit() const {
  return ui->fileExtensionsLineEdit;
}
QComboBox *ConfigurationPanel::collectionTypeComboBox() const {
  return ui->collectionTypeComboBox;
}
