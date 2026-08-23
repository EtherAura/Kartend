// Multi-field "new collection" dialog. Replaces the legacy name-only
// QInputDialog so a collection arrives with its content folder, artwork
// folder, launcher, media type, and scraper already set. UI is built
// programmatically (no .ui file) — it is a small fixed form.
#include "createcollectiondialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "collection/typehelpers.h"
#include "metadataproviderregistry.h"
#include "pathutils.h"
#include "retroarchicons.h"
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

  // Optional parent picker — hidden until a caller supplies options via
  // setParentCollectionOptions (Kartend-m6qsb.19). The Settings add-collection
  // flow leaves it hidden and derives the parent from the tree selection.
  m_parentCombo = new QComboBox(this);
  m_parentCombo->setObjectName(QStringLiteral("parentCollectionCombo"));
  m_parentCombo->setToolTip(tr("Nest this collection under an existing one. "
                               "Optional — leave as (none) for a top-level collection."));
  m_form->addRow(tr("Parent Collection:"), m_parentCombo);
  m_form->setRowVisible(m_parentCombo, false);

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

  // Navigation-sidebar glyph (Kartend-1kkk2) — a small console / controller /
  // cartridge mark beside this collection's name, read out of a local
  // RetroArch install. Game-only, like the ScreenScraper row above it, and
  // hidden by the same trigger: it is keyed on libretro's system names, which
  // is a vocabulary only a game collection has.
  m_systemIconSubjectCombo = new QComboBox(this);
  m_systemIconSubjectCombo->setToolTip(tr("What the sidebar glyph shows for this collection."));
  // Order must match SystemIconSubject — the value is cast from the index.
  m_systemIconSubjectCombo->addItem(tr("Controller"));
  m_systemIconSubjectCombo->addItem(tr("Console"));
  m_systemIconSubjectCombo->addItem(tr("Cartridge / disc"));
  m_systemIconCombo = new QComboBox(this);
  // Editable so a 300-entry list is typed into rather than scrolled through;
  // the completer matches anywhere in the name, since a collection is far more
  // often named for the system ("Mega Drive") than for its manufacturer, and
  // libretro leads every name with the manufacturer.
  m_systemIconCombo->setEditable(true);
  m_systemIconCombo->setInsertPolicy(QComboBox::NoInsert);
  if (QCompleter *completer = m_systemIconCombo->completer()) {
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
  }
  m_systemIconCombo->setToolTip(tr("System whose icon appears beside this collection "
                                   "in the navigation sidebar."));
  m_systemIconRow = new QHBoxLayout;
  m_systemIconRow->addWidget(m_systemIconSubjectCombo);
  m_systemIconRow->addWidget(m_systemIconCombo, /*stretch=*/1);
  m_form->addRow(tr("Sidebar Icon:"), m_systemIconRow);
  // Resolved from the (as yet unset) override, which means "probe the standard
  // locations" — so a caller that never calls setRetroarchConfigOverride still
  // finds a stock install, exactly as populateCoreCombo above already does.
  m_assetsDirectory = RetroArchUtils::resolveAssetsDirectory(m_retroarchOverride);
  populateSystemIconCombo();

  // Kartend-445su: opt-in entity scrape at creation. Placed as the form's
  // last row so it reads as "…and then fetch info about it". The default is
  // seeded from the persisted setting via setFetchCollectionInfoDefault.
  m_fetchInfoCheck =
      new QCheckBox(tr("Fetch collection info && artwork online after creating"), this);
  m_fetchInfoCheck->setToolTip(
      tr("Runs the scraper's collection/platform pass for the new collection: "
         "description, manufacturer and dates from ScreenScraper or "
         "Wikipedia/Wikidata, plus logo and background art."));
  m_form->addRow(QString(), m_fetchInfoCheck);

  root->addLayout(m_form);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_okButton = buttons->button(QDialogButtonBox::Ok);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  // A collection with no name is meaningless, and a name containing path
  // separators / `..` would silently fail at launch time because
  // `%collection%` expansion would inject a traversal segment into the
  // resolved launcher arguments — gate OK on both checks rather than
  // rejecting at launch.
  m_okButton->setEnabled(false);
  connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
    const QString trimmed = text.trimmed();
    auto validation = PathUtils::validateCollectionNameForSubstitution(trimmed);
    if (validation.isError()) {
      m_okButton->setEnabled(false);
      m_nameEdit->setToolTip(
          tr("Display name for the collection. Cannot contain '/', '\\\\', or '..' — "
             "these would be substituted into launcher paths as a traversal segment."));
    } else {
      m_okButton->setEnabled(true);
      m_nameEdit->setToolTip(tr("Display name for the collection. Required."));
    }
    // The collection name carries the platform tag autodetect scores
    // against — re-resolve the ScreenScraper system as the user types.
    syncScreenscraperSystemToName();
    syncSystemIconToName();
  });

  // Type drives the suggested scraper; a hand-pick on the scraper combo
  // freezes that association (activated fires only on user interaction,
  // never on the programmatic setCurrentIndex inside syncScraperToType).
  connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
    syncScraperToType();
    updateConditionalRows();
    syncScreenscraperSystemToName();
    syncSystemIconToName();
  });
  connect(m_scraperCombo, &QComboBox::activated, this,
          [this](int) { m_scraperManuallySet = true; });
  // A hand-pick on the system combo freezes the name→system association,
  // exactly like the scraper combo above.
  connect(m_screenscraperSystemCombo, &QComboBox::activated, this,
          [this](int) { m_screenscraperSystemManuallySet = true; });
  // Changing the subject can change the PACK, and packs cover different sets
  // of systems — so the list is rebuilt, then the current pick re-resolved
  // against it rather than silently dropped.
  connect(m_systemIconSubjectCombo, &QComboBox::activated, this, [this](int) {
    populateSystemIconCombo();
    syncSystemIconToName();
  });
  connect(m_systemIconCombo, &QComboBox::activated, this,
          [this](int) { m_systemIconManuallySet = true; });
  // Typing into the editable combo is a hand-pick too — without this, the
  // next keystroke in the NAME field would overwrite what was just typed.
  if (QLineEdit *systemEdit = m_systemIconCombo->lineEdit()) {
    connect(systemEdit, &QLineEdit::textEdited, this,
            [this](const QString &) { m_systemIconManuallySet = true; });
  }
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
  // The same override locates the assets tree, so the sidebar glyph's system
  // list follows the install the user pointed at rather than probing for a
  // second one (Kartend-1kkk2).
  m_assetsDirectory = RetroArchUtils::resolveAssetsDirectory(m_retroarchOverride);
  populateSystemIconCombo();
  syncSystemIconToName();
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

void CreateCollectionDialog::populateSystemIconCombo() {
  const QString previous = m_systemIconCombo->currentText().trimmed();
  m_systemIconCombo->clear();
  const auto subject = static_cast<SystemIconSubject>(m_systemIconSubjectCombo->currentIndex());
  // Through the shared rule even though this dialog has no set picker (so the
  // override is always empty) — one resolution path, no second place to drift.
  const QString pack = RetroArchIcons::resolvePack(
      subject, QString(), RetroArchIcons::discoverPacks(m_assetsDirectory));
  const QStringList systems = RetroArchIcons::discoverSystems(m_assetsDirectory, pack);
  if (systems.isEmpty()) {
    // No RetroArch, or no pack that covers this subject. Say which, rather
    // than offering an empty dropdown the user cannot act on.
    m_systemIconCombo->addItem(m_assetsDirectory.isEmpty() ? tr("RetroArch not detected")
                                                           : tr("No icons for this style"));
    m_systemIconCombo->setEnabled(false);
    return;
  }
  m_systemIconCombo->setEnabled(true);
  // Index 0 = no glyph, and the default: a collection gets one because
  // detection found its system or the user picked one, never by falling into
  // whatever sorts first.
  m_systemIconCombo->addItem(tr("None"), QString());
  for (const QString &system : systems) {
    m_systemIconCombo->addItem(system, system);
  }
  if (!previous.isEmpty()) {
    if (const int idx = m_systemIconCombo->findData(previous); idx >= 0) {
      m_systemIconCombo->setCurrentIndex(idx);
    }
  }
}

void CreateCollectionDialog::syncSystemIconToName() {
  if (m_systemIconManuallySet || !isGamesType() || !m_systemIconCombo->isEnabled()) {
    return;
  }
  // Hand the icon matcher the aliases ScreenScraper already holds for
  // whichever system it detected. Those carry the recalbox / retropie /
  // launchbox shorthand ("snes", "megadrive", "psx") that no amount of word
  // matching recovers from a libretro name — and they cost nothing here,
  // since the catalog is already loaded for the row above.
  QStringList aliases;
  if (const int ssId = m_screenscraperSystemCombo->currentData().toInt(); ssId > 0) {
    if (const ScreenScraperSystems::System *system =
            ScreenScraperSystems::find(m_screenscraperSystems, ssId)) {
      aliases = system->aliases;
      aliases.append(system->displayName);
    }
  }
  QStringList systems;
  systems.reserve(m_systemIconCombo->count());
  for (int i = 0; i < m_systemIconCombo->count(); ++i) {
    if (const QString system = m_systemIconCombo->itemData(i).toString(); !system.isEmpty()) {
      systems.append(system);
    }
  }
  const QString detected =
      RetroArchIcons::autodetectSystem(m_nameEdit->text().trimmed(), systems, aliases);
  const int idx = detected.isEmpty() ? 0 : m_systemIconCombo->findData(detected);
  m_systemIconCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

SystemIconSettings CreateCollectionDialog::systemIcon() const {
  SystemIconSettings icon;
  // Non-game types never showed the row, so a value left in it from before a
  // type change must not leak into the config — the same rule corePath()
  // applies to a hidden core row.
  if (!isGamesType() || !m_systemIconCombo->isEnabled()) {
    return icon;
  }
  icon.subject = static_cast<SystemIconSubject>(m_systemIconSubjectCombo->currentIndex());
  // currentData is empty for the "None" entry AND for free text typed into the
  // editable combo, so fall back to the text and let resolution decide: an
  // exact system name typed by hand should work, and a typo becomes "no
  // glyph" rather than an error at creation time.
  const QString byData = m_systemIconCombo->currentData().toString();
  icon.systemName = byData.isEmpty() && m_systemIconCombo->currentIndex() != 0
                        ? m_systemIconCombo->currentText().trimmed()
                        : byData;
  // Anything the dialog resolved is DETECTION's, so a later re-run may revise
  // it; a system the user picked from the list is theirs and is left alone.
  icon.systemAutoDetected = !m_systemIconManuallySet;
  // Turning it ON is what a resolved system MEANS. Left off when nothing
  // resolved, so a collection never carries an enabled option that draws
  // nothing.
  icon.enabled = !icon.systemName.isEmpty();
  return icon;
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
  // The sidebar glyph is keyed on libretro's system names, so it shares the
  // ScreenScraper row's game-only trigger.
  if (m_form->isRowVisible(m_systemIconRow) != games) {
    m_form->setRowVisible(m_systemIconRow, games);
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

void CreateCollectionDialog::setParentCollectionOptions(
    const QList<QPair<QString, QString>> &options, const QString &preselectUuid) {
  m_parentCombo->clear();
  m_parentCombo->addItem(tr("(none)"), QString());
  for (const auto &opt : options) {
    m_parentCombo->addItem(opt.first, opt.second);
  }
  if (!preselectUuid.isEmpty()) {
    const int idx = m_parentCombo->findData(preselectUuid);
    if (idx >= 0) {
      m_parentCombo->setCurrentIndex(idx);
    }
  }
  m_parentPickerEnabled = true;
  m_form->setRowVisible(m_parentCombo, true);
  adjustSize();
}

QString CreateCollectionDialog::parentCollectionUuid() const {
  return m_parentPickerEnabled ? m_parentCombo->currentData().toString() : QString();
}

void CreateCollectionDialog::accept() {
  // Mirror SettingsDialog::saveCollectionFromUI: reject a path with shell
  // metacharacters / backslashes / control characters at creation instead of
  // silently accepting it and only catching it on a later re-save
  // (Kartend-nkzx). Empty fields are optional and pass. corePath() already
  // returns empty for a non-libretro launcher, so it is naturally skipped.
  auto validatePath = [this](const QString &path, const QString &fieldName) -> bool {
    if (path.isEmpty()) {
      return true;
    }
    auto result = PathUtils::validatePathSecurity(path);
    if (result.isError()) {
      QMessageBox::warning(this, tr("Invalid Path"),
                           tr("The %1 contains invalid characters:\n\n%2\n\n"
                              "Please remove shell metacharacters, backslashes, "
                              "or other special characters.")
                               .arg(fieldName, result.error().message));
      return false;
    }
    return true;
  };

  if (!validatePath(contentPath(), tr("Content Folder")) ||
      !validatePath(artworkDirectory(), tr("Artwork Folder")) ||
      !validatePath(launcherPath(), tr("Launcher")) || !validatePath(corePath(), tr("Core"))) {
    return; // Keep the dialog open so the user can fix the offending field.
  }

  QDialog::accept();
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

bool CreateCollectionDialog::fetchCollectionInfo() const {
  return m_fetchInfoCheck && m_fetchInfoCheck->isChecked();
}

void CreateCollectionDialog::setFetchCollectionInfoDefault(bool checked) {
  if (m_fetchInfoCheck) m_fetchInfoCheck->setChecked(checked);
}
