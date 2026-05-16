// Multi-field "new collection" dialog. Replaces the legacy name-only
// QInputDialog so a collection arrives with its content folder, artwork
// folder, launcher, media type, and scraper already set. UI is built
// programmatically (no .ui file) — it is a small fixed form.
#include "createcollectiondialog.h"

#include <algorithm>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "collectionutils.h"
#include "metadataproviderregistry.h"
#include "retroarchutils.h"
#include "screenscrapersystemcache.h"
#include "screenscrapersystems.h"

namespace {
// Minimum width keeps the folder line edits + Browse buttons from being
// cramped; the dialog height follows the form's content.
constexpr int DIALOG_MIN_WIDTH = 460;

// Fill the combo with "Auto-detect" plus whatever ScreenScraper systems are
// in the on-disk cache (written after the user's first ScreenScraper scrape).
// A brand-new collection has no system override, so Auto-detect (id -1) is
// the only selection that needs to exist when the cache is empty.
// Returns the catalog it loaded so the caller can keep it for
// name-driven autodetect (the combo only carries id + display name,
// not the aliases autodetect scores against).
QList<ScreenScraperSystems::System> populateScreenscraperSystems(QComboBox *combo) {
  combo->addItem(CreateCollectionDialog::tr("Auto-detect"), -1);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  if (cachePath.isEmpty()) {
    return {};
  }
  const auto loaded = ScreenScraperSystemCache::loadCachedSystems(cachePath);
  if (!loaded.isOk()) {
    return {};
  }
  QList<ScreenScraperSystems::System> systems = loaded.value();
  // Alphabetical so the dropdown scans linearly — the cache keeps SS's own
  // catalogue order, which isn't useful for a picker.
  std::sort(systems.begin(), systems.end(),
            [](const ScreenScraperSystems::System &a, const ScreenScraperSystems::System &b) {
              return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
            });
  for (const auto &sys : systems) {
    if (sys.id < 0 || sys.displayName.isEmpty()) {
      continue;
    }
    combo->addItem(sys.displayName, sys.id);
  }
  return systems;
}
} // namespace

CreateCollectionDialog::CreateCollectionDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Add Collection"));
  setModal(true);
  buildUi();
  setMinimumWidth(DIALOG_MIN_WIDTH);
}

CreateCollectionDialog::~CreateCollectionDialog() = default;

void CreateCollectionDialog::buildUi() {
  auto *root = new QVBoxLayout(this);

  m_introLabel = new QLabel(this);
  m_introLabel->setWordWrap(true);
  m_introLabel->setVisible(false);
  root->addWidget(m_introLabel);

  m_form = new QFormLayout;

  m_nameEdit = new QLineEdit(this);
  m_nameEdit->setToolTip(tr("Display name for the collection. Required."));
  m_form->addRow(tr("Name:"), m_nameEdit);

  // Builds a "<line edit> [Browse]" row. Browse opens a folder picker when
  // pickDirectory is set, otherwise a file picker; either way it is seeded
  // from the field's current value.
  auto makeBrowseRow = [this](QLineEdit *&edit, const QString &tip, const QString &caption,
                              bool pickDirectory) -> QHBoxLayout * {
    edit = new QLineEdit(this);
    edit->setToolTip(tip);
    auto *browse = new QPushButton(tr("Browse"), this);
    auto *row = new QHBoxLayout;
    row->addWidget(edit);
    row->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, caption, pickDirectory]() {
      const QString current = edit->text().trimmed();
      QString picked;
      if (pickDirectory) {
        const QString startDir = QFileInfo::exists(current) ? current : QString();
        picked = QFileDialog::getExistingDirectory(this, caption, startDir);
      } else {
        picked = QFileDialog::getOpenFileName(this, caption, current);
      }
      if (!picked.isEmpty()) {
        edit->setText(picked);
      }
    });
    return row;
  };

  m_form->addRow(tr("Content Folder:"),
                 makeBrowseRow(m_contentEdit,
                               tr("Folder holding this collection's content files. "
                                  "Optional — can be set later."),
                               tr("Select Content Folder"), /*pickDirectory=*/true));
  m_form->addRow(tr("Artwork Folder:"),
                 makeBrowseRow(m_artworkEdit,
                               tr("Folder for this collection's artwork. Scraping "
                                  "auto-creates per-type subfolders under it. "
                                  "Optional — can be set later."),
                               tr("Select Artwork Folder"), /*pickDirectory=*/true));

  m_form->addRow(tr("Launcher:"),
                 makeBrowseRow(m_launcherEdit,
                               tr("Executable that opens an item from this collection. "
                                  "Optional — can be set later."),
                               tr("Select Launcher Executable"), /*pickDirectory=*/false));

  // Libretro core — only meaningful for a RetroArch launcher, so the row is
  // revealed only then (toggled in updateConditionalRows()). The combo
  // lists cores discovered in the RetroArch install; picking one fills
  // the path field. The field + Browse stay for cores outside that
  // directory (custom builds, a non-standard install).
  m_coreEdit = new QLineEdit(this);
  m_coreEdit->setToolTip(tr("Libretro core (.so / .dll / .dylib) RetroArch loads "
                            "for this collection."));
  m_coreCombo = new QComboBox(this);
  m_coreCombo->setToolTip(tr("Cores detected in your RetroArch install — pick one "
                             "to fill the path."));
  auto *coreBrowse = new QPushButton(tr("Browse"), this);
  m_coreRow = new QHBoxLayout;
  m_coreRow->addWidget(m_coreCombo);
  m_coreRow->addWidget(m_coreEdit);
  m_coreRow->addWidget(coreBrowse);
  m_form->addRow(tr("Core:"), m_coreRow);
  connect(coreBrowse, &QPushButton::clicked, this, [this]() {
    const QString picked =
        QFileDialog::getOpenFileName(this, tr("Select Core File"), m_coreEdit->text().trimmed());
    if (!picked.isEmpty()) {
      m_coreEdit->setText(picked);
    }
  });
  // Picking a detected core fills the path field — the field stays the
  // single source of truth that corePath() reads back.
  connect(m_coreCombo, &QComboBox::activated, this, [this](int index) {
    const QString path = m_coreCombo->itemData(index).toString();
    if (!path.isEmpty()) {
      m_coreEdit->setText(path);
    }
  });
  populateCoreCombo();

  m_typeCombo = new QComboBox(this);
  m_typeCombo->setEditable(true);
  m_typeCombo->setToolTip(tr("Media category. Pick a preset or type a custom value. "
                             "Drives the type filter and the suggested scraper."));
  m_typeCombo->addItems(QStringList{QString()} + CollectionUtils::standardCollectionTypes());
  m_typeCombo->setCurrentIndex(0);
  m_form->addRow(tr("Media Type:"), m_typeCombo);

  m_scraperCombo = new QComboBox(this);
  m_scraperCombo->setToolTip(tr("Metadata scraper used for this collection. "
                                "Defaults to the match for the media type; "
                                "set it manually for a custom type."));
  // Index 0 = automatic: an empty id leaves the scrape to resolve a
  // provider by category at run time.
  m_scraperCombo->addItem(tr("Automatic (match by type)"), QString());
  for (const auto &choice : MetadataProviderRegistry::scrapingProviders()) {
    m_scraperCombo->addItem(choice.displayName, choice.id);
  }
  m_form->addRow(tr("Scraper:"), m_scraperCombo);

  // ScreenScraper.fr system override — only relevant when the collection is
  // scraped by ScreenScraper, so the row is revealed only for game-type
  // media (toggled in the media-type handler below).
  m_screenscraperSystemCombo = new QComboBox(this);
  m_screenscraperSystemCombo->setToolTip(
      tr("ScreenScraper.fr system for this collection. Leave on Auto-detect "
         "unless detection picks the wrong system."));
  m_screenscraperSystems = populateScreenscraperSystems(m_screenscraperSystemCombo);
  m_form->addRow(tr("ScreenScraper System:"), m_screenscraperSystemCombo);

  root->addLayout(m_form);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_okButton = buttons->button(QDialogButtonBox::Ok);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  // A collection with no name is meaningless — gate OK on it rather than
  // rejecting after the fact.
  m_okButton->setEnabled(false);
  connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
    m_okButton->setEnabled(!text.trimmed().isEmpty());
    // The collection name carries the platform tag autodetect scores
    // against — re-resolve the ScreenScraper system as the user types.
    syncScreenscraperSystemToName();
  });

  // Type drives the suggested scraper; a hand-pick on the scraper combo
  // freezes that association (activated fires only on user interaction,
  // never on the programmatic setCurrentIndex inside syncScraperToType).
  connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
    syncScraperToType();
    updateConditionalRows();
    syncScreenscraperSystemToName();
  });
  connect(m_scraperCombo, &QComboBox::activated, this,
          [this](int) { m_scraperManuallySet = true; });
  // A hand-pick on the system combo freezes the name→system association,
  // exactly like the scraper combo above.
  connect(m_screenscraperSystemCombo, &QComboBox::activated, this,
          [this](int) { m_screenscraperSystemManuallySet = true; });
  // The core row appears the moment the launcher path becomes a RetroArch
  // executable.
  connect(m_launcherEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { updateConditionalRows(); });

  // Type starts blank and the launcher empty, so both conditional rows
  // start hidden.
  updateConditionalRows();
  m_nameEdit->setFocus();
}

void CreateCollectionDialog::syncScraperToType() {
  if (m_scraperManuallySet) {
    return;
  }
  const QString id = MetadataProviderRegistry::defaultScraperForType(m_typeCombo->currentText());
  const int idx = id.isEmpty() ? 0 : m_scraperCombo->findData(id);
  m_scraperCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void CreateCollectionDialog::setRetroarchConfigOverride(const QString &path) {
  m_retroarchOverride = path;
  populateCoreCombo();
}

void CreateCollectionDialog::populateCoreCombo() {
  m_coreCombo->clear();
  const QList<RetroArchUtils::Core> cores =
      RetroArchUtils::discoverCores(RetroArchUtils::resolveCoreDirectory(m_retroarchOverride));
  if (cores.isEmpty()) {
    // Nothing to pick — the user falls back to the path field + Browse.
    m_coreCombo->addItem(tr("No cores detected"), QString());
    m_coreCombo->setEnabled(false);
    return;
  }
  m_coreCombo->setEnabled(true);
  m_coreCombo->addItem(tr("Pick a detected core…"), QString());
  for (const auto &core : cores) {
    m_coreCombo->addItem(core.displayName, core.path);
  }
}

void CreateCollectionDialog::syncScreenscraperSystemToName() {
  // A hand-pick freezes the association; the system row is only
  // meaningful for game collections anyway.
  if (m_screenscraperSystemManuallySet || !isGamesType()) {
    return;
  }
  // Autodetect from the collection name + type. The dialog has no
  // extensions field yet, so the name's platform tag does the work;
  // a no-match leaves the combo on "Auto-detect" (index 0).
  const int detected = ScreenScraperSystems::autodetect(
      m_nameEdit->text().trimmed(), m_typeCombo->currentText(), {}, m_screenscraperSystems);
  const int idx = detected > 0 ? m_screenscraperSystemCombo->findData(detected) : 0;
  m_screenscraperSystemCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

bool CreateCollectionDialog::isGamesType() const {
  // Reuse the scraper registry's synonym normalisation so game synonyms
  // ("rom", "emulator", …) count too, not just the literal "Games" preset.
  return MetadataProviderRegistry::normaliseCategory(m_typeCombo->currentText()) ==
         QStringLiteral("games");
}

void CreateCollectionDialog::updateConditionalRows() {
  // ScreenScraper system applies to game collections, the libretro core to
  // RetroArch launchers. Resize once, and only when a row actually flips, so
  // typing in the type / launcher fields doesn't churn the dialog geometry.
  bool flipped = false;
  const bool games = isGamesType();
  if (m_form->isRowVisible(m_screenscraperSystemCombo) != games) {
    m_form->setRowVisible(m_screenscraperSystemCombo, games);
    flipped = true;
  }
  const bool retro = LauncherUtils::usesLibretroCore(m_launcherEdit->text());
  if (m_form->isRowVisible(m_coreRow) != retro) {
    m_form->setRowVisible(m_coreRow, retro);
    flipped = true;
  }
  if (flipped) {
    adjustSize();
  }
}

void CreateCollectionDialog::setIntroText(const QString &text) {
  m_introLabel->setText(text);
  m_introLabel->setVisible(!text.isEmpty());
}

QString CreateCollectionDialog::collectionName() const {
  return m_nameEdit->text().trimmed();
}

QString CreateCollectionDialog::contentPath() const {
  return m_contentEdit->text().trimmed();
}

QString CreateCollectionDialog::artworkDirectory() const {
  return m_artworkEdit->text().trimmed();
}

QString CreateCollectionDialog::launcherPath() const {
  return m_launcherEdit->text().trimmed();
}

QString CreateCollectionDialog::corePath() const {
  // Core only applies to a RetroArch launcher; the row is hidden otherwise
  // and a stale path must not leak into a non-libretro collection.
  if (!LauncherUtils::usesLibretroCore(m_launcherEdit->text())) {
    return {};
  }
  return m_coreEdit->text().trimmed();
}

QString CreateCollectionDialog::collectionType() const {
  return m_typeCombo->currentText().trimmed();
}

QString CreateCollectionDialog::scraperProviderId() const {
  // While the scraper combo merely mirrors the media type (auto-
  // association), persist an empty id — that leaves the collection on
  // "Automatic" so the scraper re-resolves if the type changes later,
  // and keeps a redundant pin out of the config. A concrete id is
  // stored only once the user has chosen a provider by hand.
  if (!m_scraperManuallySet) {
    return {};
  }
  return m_scraperCombo->currentData().toString();
}

int CreateCollectionDialog::screenscraperSystemId() const {
  // Only meaningful for game collections; the row is hidden otherwise and a
  // stale id left over from a since-changed type must not leak into the new
  // config. Auto-detect carries id -1.
  if (!isGamesType()) {
    return -1;
  }
  return m_screenscraperSystemCombo->currentData().toInt();
}
