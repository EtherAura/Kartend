#include "sidebarslayoutpanel.h"
#include "ui_sidebarslayoutpanel.h"

#include <algorithm>

#include <QCompleter>

#include "collection/collectionconfig.h"
#include "retroarchicons.h"
#include "retroarchutils.h"
#include "settingsmodel.h"

namespace {

/// The pane-side combo lists positions in enum order (Right=0, Left=1,
/// Top=2, Bottom=3), so index<->enum casts stay honest — the same contract
/// the Details Pane page's own position combo uses.
constexpr int kTreeSideLeftIndex = 0;
constexpr int kTreeSideRightIndex = 1;

} // namespace

SidebarsLayoutPanel::SidebarsLayoutPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::SidebarsLayoutPanel) {
  ui->setupUi(this);
  for (QComboBox *combo :
       {ui->paneSideComboBox, ui->paneJustificationComboBox, ui->paneScrollbarModeComboBox,
        ui->treeSideComboBox, ui->treeJustificationComboBox, ui->treeScrollbarModeComboBox,
        ui->treeModeComboBox}) {
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit changed(); });
  }
  connect(ui->treeIconDisplayComboBox, &QComboBox::currentIndexChanged, this,
          [this](int) { emit changed(); });
  connect(ui->treeShowLinesCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  connect(ui->treeScrollClippedLabelsCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->treeScrollClippedLabelsOnHoverCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->treeColorizeSelectedCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->treeIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { emit changed(); });
  connect(ui->treeWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { emit changed(); });
  connect(ui->treeIconStyleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { emit changed(); });
  connect(ui->treeIconTintEdit, &QLineEdit::textEdited, this,
          [this](const QString &) { emit changed(); });

  // Kartend-1kkk2 — the system-glyph group.
  if (QCompleter *completer = ui->systemIconSystemComboBox->completer()) {
    // Match anywhere in the name: libretro leads every system with its
    // manufacturer ("Nintendo - Game Boy"), and a user looking for a Game Boy
    // types "game boy", not "nintendo".
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
  }
  ui->systemIconSystemComboBox->setInsertPolicy(QComboBox::NoInsert);
  connect(ui->systemIconEnabledCheckBox, &QCheckBox::toggled, this, [this](bool) {
    updateSystemIconState();
    emit changed();
  });
  // Both of these repopulate the system list, which reads the assets tree — so
  // both bail out while load() is setting the widgets. Without the guard, one
  // load() drives a directory walk per widget it touches (a dozen or more) to
  // reach a state its own final populate call would have produced anyway.
  connect(ui->systemIconSubjectComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            if (m_loadingSystemIcon) return;
            // Subject decides which sets are eligible, so the set list is
            // rebuilt first. A set that is no longer eligible drops out and
            // the combo lands on Automatic, which is also what resolvePack
            // would render — control and sidebar stay in agreement without a
            // second reset step here.
            populateSystemIconPacks();
            // Sets cover different systems, so the system list follows.
            populateSystemIconSystems();
            updateSystemIconState();
            emit changed();
          });
  connect(ui->systemIconPackComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            if (m_loadingSystemIcon) return;
            populateSystemIconSystems();
            updateSystemIconState();
            emit changed();
          });
  connect(ui->systemIconSystemComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            if (m_loadingSystemIcon) return;
            // A pick made HERE is the user's, so detection may no longer
            // revise it. currentIndexChanged fires for programmatic changes
            // too, but every one of those happens under m_loadingSystemIcon
            // or from the Detect handler, which re-asserts the flag itself.
            m_systemIconAutoDetected = false;
            updateSystemIconState();
            emit changed();
          });
  connect(ui->systemIconStyleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            if (m_loadingSystemIcon) return;
            emit changed();
          });
  connect(ui->systemIconPlacementComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) {
            if (m_loadingSystemIcon) return;
            emit changed();
          });
  connect(ui->systemIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { emit changed(); });
  // Retrofit for collections that predate this option, or were created before
  // RetroArch was installed (user decision 2026-08-22) — without it the
  // feature would only ever reach collections made from now on.
  connect(ui->systemIconDetectButton, &QPushButton::clicked, this, [this]() {
    const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
    if (!current) return;
    QStringList systems;
    systems.reserve(ui->systemIconSystemComboBox->count());
    for (int i = 0; i < ui->systemIconSystemComboBox->count(); ++i) {
      if (const QString system = ui->systemIconSystemComboBox->itemData(i).toString();
          !system.isEmpty()) {
        systems.append(system);
      }
    }
    const QString detected = RetroArchIcons::autodetectSystem(current->name, systems);
    if (detected.isEmpty()) {
      ui->systemIconStatusLabel->setText(
          tr("No system matched “%1” — pick one from the list.").arg(current->name));
      return;
    }
    if (const int idx = ui->systemIconSystemComboBox->findData(detected); idx >= 0) {
      ui->systemIconSystemComboBox->setCurrentIndex(idx);
      // Detection's own answer — a later re-run is free to revise it. Set
      // AFTER the index change, whose handler clears the flag.
      m_systemIconAutoDetected = true;
      // Detecting a system is the point of the button, so turn the option on
      // rather than leaving the user to notice a second unticked box.
      ui->systemIconEnabledCheckBox->setChecked(true);
    }
  });
}

SidebarsLayoutPanel::~SidebarsLayoutPanel() {
  delete ui;
}

void SidebarsLayoutPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void SidebarsLayoutPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  ui->paneSideComboBox->setCurrentIndex(static_cast<int>(current->sidebar.sidebarPosition));
  m_loadedPaneSideIndex = ui->paneSideComboBox->currentIndex();
  ui->paneJustificationComboBox->setCurrentIndex(
      static_cast<int>(current->sidebar.sidebarJustification));
  ui->paneScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(current->sidebar.sidebarScrollbarMode));
  ui->treeScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeScrollbarMode));
  ui->treeSideComboBox->setCurrentIndex(
      current->collectionTree.treePosition == DetailsPanePosition::Right ? kTreeSideRightIndex
                                                                         : kTreeSideLeftIndex);
  ui->treeJustificationComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeJustification));
  ui->treeModeComboBox->setCurrentIndex(static_cast<int>(current->collectionTree.treeMode));
  ui->treeIconDisplayComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeIconDisplay));
  ui->treeShowLinesCheckBox->setChecked(current->collectionTree.treeShowLines);
  ui->treeScrollClippedLabelsCheckBox->setChecked(current->collectionTree.treeScrollClippedLabels);
  ui->treeScrollClippedLabelsOnHoverCheckBox->setChecked(
      current->collectionTree.treeScrollClippedLabelsOnHover);
  ui->treeColorizeSelectedCheckBox->setChecked(current->collectionTree.treeColorizeSelected);
  ui->treeIconSizeSpinBox->setValue(current->collectionTree.treeIconSize);
  ui->treeWidthSpinBox->setValue(current->collectionTree.treeWidth);
  ui->treeIconStyleComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeIconStyle));
  ui->treeIconTintEdit->setText(current->collectionTree.treeIconTintColor);

  // Kartend-1kkk2. Re-resolved per load so a RetroArch install that appeared
  // since the dialog was last opened is found.
  m_assetsDirectory = RetroArchUtils::resolveAssetsDirectory(
      m_model && m_model->generalSettings ? m_model->generalSettings->launchers.retroarchConfigPath
                                          : QString());
  m_systemIconPacks = RetroArchIcons::discoverPacks(m_assetsDirectory);
  m_loadingSystemIcon = true;
  ui->systemIconEnabledCheckBox->setChecked(current->systemIcon.enabled);
  ui->systemIconSubjectComboBox->setCurrentIndex(static_cast<int>(current->systemIcon.subject));
  m_systemIconAutoDetected = current->systemIcon.systemAutoDetected;
  ui->systemIconStyleComboBox->setCurrentIndex(static_cast<int>(current->systemIcon.style));
  ui->systemIconPlacementComboBox->setCurrentIndex(static_cast<int>(current->systemIcon.placement));
  ui->systemIconSizeSpinBox->setValue(current->systemIcon.iconSize);
  populateSystemIconPacks();
  if (const int packIdx = ui->systemIconPackComboBox->findData(current->systemIcon.packOverride);
      packIdx >= 0) {
    ui->systemIconPackComboBox->setCurrentIndex(packIdx);
  }
  populateSystemIconSystems();
  const int systemIdx =
      current->systemIcon.useCollectionArtwork
          ? ui->systemIconSystemComboBox->findData(QStringLiteral("@artwork"))
          : ui->systemIconSystemComboBox->findData(current->systemIcon.systemName);
  if (systemIdx >= 0) {
    ui->systemIconSystemComboBox->setCurrentIndex(systemIdx);
  } else if (!current->systemIcon.systemName.isEmpty()) {
    // The saved system is not in this pack (or RetroArch is gone). Show it
    // anyway rather than silently resetting to None: the setting is still
    // what the config says, and dropping it here would quietly discard it on
    // the next save.
    ui->systemIconSystemComboBox->setEditText(current->systemIcon.systemName);
  }
  m_loadingSystemIcon = false;
  updateSystemIconState();
}

void SidebarsLayoutPanel::populateSystemIconPacks() {
  const QString previous = ui->systemIconPackComboBox->currentData().toString();
  const bool wasLoading = m_loadingSystemIcon;
  m_loadingSystemIcon = true;
  ui->systemIconPackComboBox->clear();
  // Index 0 = automatic, and the default: the curated pack for the chosen
  // subject, which is the setting most users should leave alone.
  ui->systemIconPackComboBox->addItem(tr("Automatic (match the style)"), QString());

  // FILTERED to the sets that actually hold art for the chosen subject (user
  // decision 2026-08-22: "filter to retroarch icon sets that do actually
  // contain console icons").
  //
  // A pack holds exactly one icon per system, so which of the two it is comes
  // down to the set: `monochrome` has no console art for a system that has a
  // controller, and asking for one there can only ever hand back the
  // controller. Listing it under Console offers a choice that cannot be
  // honoured — which is precisely what got reported, twice.
  //
  // A conflicting saved value is NOT retained in the list, deliberately.
  // Retaining it would leave the combo naming one set while resolvePack —
  // where the subject wins — rendered from another, so the page would
  // contradict the sidebar. Landing on Automatic instead means the control
  // shows what is actually being drawn.
  //
  // Known cost, accepted: for a handheld, a home computer or an arcade board
  // there is no separate controller, so every set draws the machine and this
  // filter hides sets that would have been fine. The sets it leaves are all
  // still correct for those systems, so the cost is a narrower choice of
  // style, not a wrong icon.
  const auto subject =
      static_cast<SystemIconSubject>(ui->systemIconSubjectComboBox->currentIndex());
  for (const RetroArchIcons::Pack &pack : m_systemIconPacks) {
    // Content is the `-content` sibling and every set ships one, so it has no
    // subject to filter on. An unclassified set is offered for every subject:
    // nothing here knows what it draws, so there is no basis to rule it out.
    if (subject != SystemIconSubject::Content && pack.subjectKnown && pack.subject != subject) {
      continue;
    }
    // The system count is the difference between a glyph and a blank — packs
    // range from a few dozen systems to a few hundred — so it belongs in the
    // label rather than being something to discover by trial.
    ui->systemIconPackComboBox->addItem(
        tr("%1 (%n system(s))", nullptr, pack.systemCount).arg(pack.displayName), pack.id);
  }
  if (const int idx = ui->systemIconPackComboBox->findData(previous); idx >= 0) {
    ui->systemIconPackComboBox->setCurrentIndex(idx);
  }
  m_loadingSystemIcon = wasLoading;
}

QString SidebarsLayoutPanel::resolvedSystemIconPack() const {
  const auto subject =
      static_cast<SystemIconSubject>(ui->systemIconSubjectComboBox->currentIndex());
  return RetroArchIcons::resolvePack(subject, ui->systemIconPackComboBox->currentData().toString(),
                                     m_systemIconPacks);
}

void SidebarsLayoutPanel::populateSystemIconSystems() {
  const QString previous = ui->systemIconSystemComboBox->currentData().toString();
  const bool wasLoading = m_loadingSystemIcon;
  m_loadingSystemIcon = true;
  ui->systemIconSystemComboBox->clear();
  // Index 0 = no icon at all, index 1 = the collection's own artwork. Two
  // distinct answers to "what goes here", where before there was one and the
  // artwork was an implicit fallback nobody could switch off (user 2026-08-23:
  // "want to be able to clear/override nav bar icons individually ... in some
  // cases manufacturer logo+text too is redundant").
  ui->systemIconSystemComboBox->addItem(tr("None (no icon)"), QString());
  ui->systemIconSystemComboBox->addItem(tr("This collection's own artwork"),
                                        QStringLiteral("@artwork"));
  for (const QString &system :
       RetroArchIcons::discoverSystems(m_assetsDirectory, resolvedSystemIconPack())) {
    ui->systemIconSystemComboBox->addItem(system, system);
  }
  if (const int idx = ui->systemIconSystemComboBox->findData(previous); idx >= 0) {
    ui->systemIconSystemComboBox->setCurrentIndex(idx);
  } else if (!previous.isEmpty()) {
    // Carried across a pack change that does not cover it — kept as text so
    // switching sets to compare them does not lose the system, and flagged by
    // updateSystemIconState.
    ui->systemIconSystemComboBox->setEditText(previous);
  }
  m_loadingSystemIcon = wasLoading;
}

void SidebarsLayoutPanel::updateSystemIconState() {
  const bool haveRetroArch = !m_assetsDirectory.isEmpty();
  const bool on = ui->systemIconEnabledCheckBox->isChecked();
  ui->systemIconEnabledCheckBox->setEnabled(haveRetroArch);
  for (QWidget *w : {static_cast<QWidget *>(ui->systemIconSubjectComboBox),
                     static_cast<QWidget *>(ui->systemIconSystemComboBox),
                     static_cast<QWidget *>(ui->systemIconPackComboBox),
                     static_cast<QWidget *>(ui->systemIconStyleComboBox),
                     static_cast<QWidget *>(ui->systemIconPlacementComboBox),
                     static_cast<QWidget *>(ui->systemIconSizeSpinBox),
                     static_cast<QWidget *>(ui->systemIconDetectButton)}) {
    w->setEnabled(haveRetroArch && on);
  }
  if (!haveRetroArch) {
    ui->systemIconStatusLabel->setText(
        tr("RetroArch was not found. Point Kartend at your install under "
           "Settings → Launchers to use its icons."));
    return;
  }
  // Index 0 is the "None" entry — tested by INDEX, not by comparing against
  // the translated label, which would stop matching in any other language and
  // would also swallow a system genuinely typed as "None".
  const QString byData = ui->systemIconSystemComboBox->currentData().toString();
  if (byData == QLatin1String("@artwork")) {
    // Nothing to check against RetroArch — this row draws the collection's own
    // artwork, and whether that exists is the scraper's business.
    ui->systemIconStatusLabel->clear();
    return;
  }
  const QString system = byData.isEmpty() && ui->systemIconSystemComboBox->currentIndex() != 0
                             ? ui->systemIconSystemComboBox->currentText().trimmed()
                             : byData;
  if (!on || system.isEmpty()) {
    ui->systemIconStatusLabel->clear();
    return;
  }
  const auto subject =
      static_cast<SystemIconSubject>(ui->systemIconSubjectComboBox->currentIndex());
  const QString pack = resolvedSystemIconPack();
  // Say so HERE rather than letting the sidebar render a blank row and leave
  // the user guessing which of the three settings is at fault.
  if (RetroArchIcons::iconPath(m_assetsDirectory, pack, system, subject).isEmpty()) {
    ui->systemIconStatusLabel->setText(
        tr("This icon set has no icon for “%1” — pick another set, or another system.")
            .arg(system));
  } else {
    ui->systemIconStatusLabel->clear();
  }
}

void SidebarsLayoutPanel::clear() {
  ui->paneSideComboBox->setCurrentIndex(static_cast<int>(DetailsPanePosition::Right));
  m_loadedPaneSideIndex = ui->paneSideComboBox->currentIndex();
  ui->paneJustificationComboBox->setCurrentIndex(
      static_cast<int>(SidebarJustification::BelowToolbar));
  ui->paneScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(SidebarAppearance{}.sidebarScrollbarMode));
  ui->treeScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeScrollbarMode));
  ui->treeSideComboBox->setCurrentIndex(kTreeSideLeftIndex);
  // Struct defaults — the tree's default justification is FULL-HEIGHT since
  // the 2026-08-17 defaults decision, unlike the pane's above.
  ui->treeJustificationComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeJustification));
  ui->treeModeComboBox->setCurrentIndex(static_cast<int>(CollectionTreeSettings{}.treeMode));
  ui->treeIconDisplayComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeIconDisplay));
  ui->treeShowLinesCheckBox->setChecked(CollectionTreeSettings{}.treeShowLines);
  ui->treeScrollClippedLabelsCheckBox->setChecked(CollectionTreeSettings{}.treeScrollClippedLabels);
  ui->treeScrollClippedLabelsOnHoverCheckBox->setChecked(
      CollectionTreeSettings{}.treeScrollClippedLabelsOnHover);
  ui->treeColorizeSelectedCheckBox->setChecked(CollectionTreeSettings{}.treeColorizeSelected);
  ui->treeIconSizeSpinBox->setValue(CollectionTreeSettings{}.treeIconSize);
  ui->treeWidthSpinBox->setValue(CollectionTreeSettings{}.treeWidth);
  ui->treeIconStyleComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeIconStyle));
  ui->treeIconTintEdit->clear();

  m_loadingSystemIcon = true;
  ui->systemIconEnabledCheckBox->setChecked(SystemIconSettings{}.enabled);
  ui->systemIconSubjectComboBox->setCurrentIndex(static_cast<int>(SystemIconSettings{}.subject));
  ui->systemIconStyleComboBox->setCurrentIndex(static_cast<int>(SystemIconSettings{}.style));
  ui->systemIconPlacementComboBox->setCurrentIndex(
      static_cast<int>(SystemIconSettings{}.placement));
  ui->systemIconSizeSpinBox->setValue(SystemIconSettings{}.iconSize);
  populateSystemIconPacks();
  populateSystemIconSystems();
  ui->systemIconSystemComboBox->setCurrentIndex(0);
  m_loadingSystemIcon = false;
  updateSystemIconState();
}

void SidebarsLayoutPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  // Aliased with the Details Pane page's Position: write only when edited
  // HERE, so an untouched copy never overwrites the other page's edit.
  if (ui->paneSideComboBox->currentIndex() != m_loadedPaneSideIndex) {
    current->sidebar.sidebarPosition =
        static_cast<DetailsPanePosition>(ui->paneSideComboBox->currentIndex());
  }
  current->sidebar.sidebarJustification =
      static_cast<SidebarJustification>(ui->paneJustificationComboBox->currentIndex());
  current->sidebar.sidebarScrollbarMode =
      static_cast<ScrollbarMode>(ui->paneScrollbarModeComboBox->currentIndex());
  current->collectionTree.treeScrollbarMode =
      static_cast<ScrollbarMode>(ui->treeScrollbarModeComboBox->currentIndex());
  current->collectionTree.treePosition = ui->treeSideComboBox->currentIndex() == kTreeSideRightIndex
                                             ? DetailsPanePosition::Right
                                             : DetailsPanePosition::Left;
  current->collectionTree.treeJustification =
      static_cast<SidebarJustification>(ui->treeJustificationComboBox->currentIndex());
  // Combo order matches DetailsPaneMode (Overlay = 0, Expand = 1).
  current->collectionTree.treeMode =
      static_cast<DetailsPaneMode>(ui->treeModeComboBox->currentIndex());
  current->collectionTree.treeIconDisplay =
      static_cast<TreeIconDisplay>(ui->treeIconDisplayComboBox->currentIndex());
  current->collectionTree.treeShowLines = ui->treeShowLinesCheckBox->isChecked();
  current->collectionTree.treeScrollClippedLabels =
      ui->treeScrollClippedLabelsCheckBox->isChecked();
  current->collectionTree.treeScrollClippedLabelsOnHover =
      ui->treeScrollClippedLabelsOnHoverCheckBox->isChecked();
  current->collectionTree.treeColorizeSelected = ui->treeColorizeSelectedCheckBox->isChecked();
  current->collectionTree.treeIconSize = ui->treeIconSizeSpinBox->value();
  current->collectionTree.treeWidth = ui->treeWidthSpinBox->value();
  current->collectionTree.treeIconStyle =
      static_cast<TreeIconStyle>(ui->treeIconStyleComboBox->currentIndex());
  current->collectionTree.treeIconTintColor = ui->treeIconTintEdit->text().trimmed();

  // Kartend-1kkk2.
  current->systemIcon.enabled = ui->systemIconEnabledCheckBox->isChecked();
  current->systemIcon.subject =
      static_cast<SystemIconSubject>(ui->systemIconSubjectComboBox->currentIndex());
  current->systemIcon.packOverride = ui->systemIconPackComboBox->currentData().toString();
  current->systemIcon.style =
      static_cast<TreeIconStyle>(ui->systemIconStyleComboBox->currentIndex());
  current->systemIcon.placement =
      static_cast<SystemIconPlacement>(ui->systemIconPlacementComboBox->currentIndex());
  current->systemIcon.iconSize = ui->systemIconSizeSpinBox->value();
  // currentData is empty both for the "None" entry and for text typed into the
  // editable combo, so fall back to the text — index 0 is the only one that
  // genuinely means "no system".
  const QString systemByData = ui->systemIconSystemComboBox->currentData().toString();
  // The artwork row is a UI-only sentinel — it never reaches the config, where
  // it is a bool. Keeping it out of systemName means that field stays purely a
  // libretro system name and can never be mistaken for a path component.
  current->systemIcon.systemAutoDetected = m_systemIconAutoDetected;
  current->systemIcon.useCollectionArtwork = systemByData == QLatin1String("@artwork");
  // Spelled out rather than nested: artwork carries no system, free text typed
  // into the editable combo comes back as text, and everything else is the
  // item's own data.
  if (current->systemIcon.useCollectionArtwork) {
    current->systemIcon.systemName.clear();
  } else if (systemByData.isEmpty() && ui->systemIconSystemComboBox->currentIndex() != 0) {
    current->systemIcon.systemName = ui->systemIconSystemComboBox->currentText().trimmed();
  } else {
    current->systemIcon.systemName = systemByData;
  }
}
