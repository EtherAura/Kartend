// Smart-playlist creation/edit dialog. Built programmatically rather than
// from a .ui file because the per-kind parameter pages are dynamic enough
// that Designer would offer little above just laying them out in code.
#include "createsmartplaylistdialog.h"

#include "uiconstants/color.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

namespace {

constexpr int LIMIT_MIN = 1;
constexpr int LIMIT_MAX = 1000;
constexpr int LIMIT_DEFAULT = 50;

// Stack-page indices kept in sync with the combo's userData. Compile-time
// constants so the onKindChanged switch + setInitialFilter share one
// source of truth — adding a kind requires touching exactly these
// integers + the combo population in buildUI.
constexpr int PAGE_RECENT = 0;
constexpr int PAGE_TOP = 1;
constexpr int PAGE_NEVER = 2;
constexpr int PAGE_BY_EXTENSION = 3;
constexpr int PAGE_HAS_ARTWORK = 4;
constexpr int PAGE_BY_DATE_ADDED = 5;
constexpr int PAGE_PINNED = 6;
constexpr int PAGE_HIDDEN = 7;
constexpr int PAGE_CONTINUE_LATER = 8;
constexpr int PAGE_BY_COLLECTION = 9;
constexpr int PAGE_BY_TITLE_SEARCH = 10;
constexpr int PAGE_MISSING_ARTWORK = 11;
constexpr int PAGE_FAVORITE = 12;

constexpr int DAYS_MIN = 1;
constexpr int DAYS_MAX = 3650;
constexpr int DAYS_DEFAULT = 30;

int kindToPage(SmartFilter::Kind k) {
  switch (k) {
  case SmartFilter::Kind::RecentlyLaunched:
    return PAGE_RECENT;
  case SmartFilter::Kind::TopPlayed:
    return PAGE_TOP;
  case SmartFilter::Kind::NeverPlayed:
    return PAGE_NEVER;
  case SmartFilter::Kind::ByExtension:
    return PAGE_BY_EXTENSION;
  case SmartFilter::Kind::HasArtwork:
    return PAGE_HAS_ARTWORK;
  case SmartFilter::Kind::ByDateAdded:
    return PAGE_BY_DATE_ADDED;
  case SmartFilter::Kind::Pinned:
    return PAGE_PINNED;
  case SmartFilter::Kind::Hidden:
    return PAGE_HIDDEN;
  case SmartFilter::Kind::ContinueLater:
    return PAGE_CONTINUE_LATER;
  case SmartFilter::Kind::ByCollection:
    return PAGE_BY_COLLECTION;
  case SmartFilter::Kind::ByTitleSearch:
    return PAGE_BY_TITLE_SEARCH;
  case SmartFilter::Kind::MissingArtwork:
    return PAGE_MISSING_ARTWORK;
  case SmartFilter::Kind::Favorite:
    return PAGE_FAVORITE;
  }
  return PAGE_RECENT;
}

SmartFilter::Kind pageToKind(int page) {
  switch (page) {
  case PAGE_TOP:
    return SmartFilter::Kind::TopPlayed;
  case PAGE_NEVER:
    return SmartFilter::Kind::NeverPlayed;
  case PAGE_BY_EXTENSION:
    return SmartFilter::Kind::ByExtension;
  case PAGE_HAS_ARTWORK:
    return SmartFilter::Kind::HasArtwork;
  case PAGE_BY_DATE_ADDED:
    return SmartFilter::Kind::ByDateAdded;
  case PAGE_PINNED:
    return SmartFilter::Kind::Pinned;
  case PAGE_HIDDEN:
    return SmartFilter::Kind::Hidden;
  case PAGE_CONTINUE_LATER:
    return SmartFilter::Kind::ContinueLater;
  case PAGE_BY_COLLECTION:
    return SmartFilter::Kind::ByCollection;
  case PAGE_BY_TITLE_SEARCH:
    return SmartFilter::Kind::ByTitleSearch;
  case PAGE_MISSING_ARTWORK:
    return SmartFilter::Kind::MissingArtwork;
  case PAGE_FAVORITE:
    return SmartFilter::Kind::Favorite;
  case PAGE_RECENT:
  default:
    return SmartFilter::Kind::RecentlyLaunched;
  }
}

QSpinBox *makeLimitSpin(QWidget *parent) {
  auto *s = new QSpinBox(parent);
  s->setRange(LIMIT_MIN, LIMIT_MAX);
  s->setValue(LIMIT_DEFAULT);
  s->setSuffix(QStringLiteral(" items"));
  return s;
}

} // namespace

CreateSmartPlaylistDialog::CreateSmartPlaylistDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Smart Playlist"));
  setModal(true);
  buildUI();
}

void CreateSmartPlaylistDialog::buildUI() {
  auto *root = new QVBoxLayout(this);

  auto *form = new QFormLayout();
  m_nameEdit = new QLineEdit(this);
  m_nameEdit->setPlaceholderText(tr("e.g. Recently launched"));
  form->addRow(tr("Name:"), m_nameEdit);

  m_kindCombo = new QComboBox(this);
  // userData carries the page index so onKindChanged can route directly
  // without an extra map. Order here MUST match the addWidget() order
  // below — the page index ARE the row positions.
  m_kindCombo->addItem(tr("Recently launched"), PAGE_RECENT);
  m_kindCombo->addItem(tr("Most played"), PAGE_TOP);
  m_kindCombo->addItem(tr("Never launched"), PAGE_NEVER);
  m_kindCombo->addItem(tr("By extension"), PAGE_BY_EXTENSION);
  m_kindCombo->addItem(tr("Has artwork"), PAGE_HAS_ARTWORK);
  m_kindCombo->addItem(tr("Recently added"), PAGE_BY_DATE_ADDED);
  m_kindCombo->addItem(tr("Pinned"), PAGE_PINNED);
  m_kindCombo->addItem(tr("Hidden"), PAGE_HIDDEN);
  m_kindCombo->addItem(tr("Continue later"), PAGE_CONTINUE_LATER);
  m_kindCombo->addItem(tr("By collection"), PAGE_BY_COLLECTION);
  m_kindCombo->addItem(tr("Title contains…"), PAGE_BY_TITLE_SEARCH);
  m_kindCombo->addItem(tr("Missing artwork"), PAGE_MISSING_ARTWORK);
  m_kindCombo->addItem(tr("Favorites"), PAGE_FAVORITE);
  form->addRow(tr("Criterion:"), m_kindCombo);

  root->addLayout(form);

  // Parameter pages — one per kind, swapped in by onKindChanged.
  m_paramsStack = new QStackedWidget(this);

  // Recently launched
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_recentLimitSpin = makeLimitSpin(page);
    l->addRow(tr("Show top:"), m_recentLimitSpin);
    m_paramsStack->insertWidget(PAGE_RECENT, page);
  }
  // Top played
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_topLimitSpin = makeLimitSpin(page);
    l->addRow(tr("Show top:"), m_topLimitSpin);
    m_paramsStack->insertWidget(PAGE_TOP, page);
  }
  // Never played
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_neverLimitSpin = makeLimitSpin(page);
    l->addRow(tr("Show first:"), m_neverLimitSpin);
    m_paramsStack->insertWidget(PAGE_NEVER, page);
  }
  // By extension
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_extensionsEdit = new QLineEdit(page);
    m_extensionsEdit->setPlaceholderText(tr("e.g. mp4, mkv, webm"));
    l->addRow(tr("Extensions:"), m_extensionsEdit);
    auto *hint = new QLabel(tr("Comma-separated. The leading dot is optional. Matches all "
                               "items across all collections."),
                            page);
    hint->setWordWrap(true);
    hint->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addRow(hint);
    m_paramsStack->insertWidget(PAGE_BY_EXTENSION, page);
  }
  // Has artwork (no params)
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QVBoxLayout(page);
    m_hasArtworkNote = new QLabel(tr("Includes every item across every collection that has a "
                                     "real cover image (not the procedural placeholder)."),
                                  page);
    m_hasArtworkNote->setWordWrap(true);
    m_hasArtworkNote->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addWidget(m_hasArtworkNote);
    l->addStretch();
    m_paramsStack->insertWidget(PAGE_HAS_ARTWORK, page);
  }
  // Recently added (date_added recency window)
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_dateAddedDaysSpin = new QSpinBox(page);
    m_dateAddedDaysSpin->setRange(DAYS_MIN, DAYS_MAX);
    m_dateAddedDaysSpin->setValue(DAYS_DEFAULT);
    m_dateAddedDaysSpin->setSuffix(QStringLiteral(" days"));
    l->addRow(tr("Window:"), m_dateAddedDaysSpin);
    auto *hint = new QLabel(tr("Items added to your library within the last N days. The "
                               "stamp is set the first time the scanner sees an item; "
                               "re-scanning a known item does NOT reset it."),
                            page);
    hint->setWordWrap(true);
    hint->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addRow(hint);
    m_paramsStack->insertWidget(PAGE_BY_DATE_ADDED, page);
  }
  // State-flag pages (Pinned / Hidden / Continue later). Each takes no
  // parameters — the filter is "all items where the matching boolean is 1".
  // Hint text doubles as the page content so the dialog isn't blank.
  const auto buildStateFlagPage = [this](int pageIndex, const QString &description) {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QVBoxLayout(page);
    auto *note = new QLabel(description, page);
    note->setWordWrap(true);
    note->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addWidget(note);
    l->addStretch();
    m_paramsStack->insertWidget(pageIndex, page);
  };
  buildStateFlagPage(PAGE_PINNED, tr("Includes every item you've pinned via the right-click "
                                     "menu. Pinned items aggregate across all collections."));
  buildStateFlagPage(PAGE_HIDDEN, tr("Includes every item you've marked as hidden. Useful for "
                                     "reviewing or restoring items removed from the default "
                                     "browse view."));
  buildStateFlagPage(PAGE_CONTINUE_LATER,
                     tr("Includes every item you've flagged as 'continue later'. Useful for "
                        "tracking items you've started but haven't finished."));
  // ByCollection — pick a specific source collection by uuid via combobox.
  // The combobox is populated by setCollectionList() before exec(); we
  // leave it empty here so the dialog still constructs cleanly when the
  // caller forgets to populate (the evaluator returns no matches in that
  // case, so the failure is obvious instead of silent).
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_collectionCombo = new QComboBox(page);
    m_collectionCombo->setPlaceholderText(tr("Pick a collection…"));
    l->addRow(tr("Collection:"), m_collectionCombo);
    auto *hint = new QLabel(tr("Includes every item in the selected source collection. "
                               "Useful for aggregating one collection's items inside a "
                               "playlist alongside others."),
                            page);
    hint->setWordWrap(true);
    hint->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addRow(hint);
    m_paramsStack->insertWidget(PAGE_BY_COLLECTION, page);
  }
  // ByTitleSearch — simple substring search against items.name. SQL LIKE
  // is ASCII-case-insensitive by default; multibyte titles round-trip
  // case-sensitively (acceptable for an MVP — see follow-up if relevant).
  {
    auto *page = new QWidget(m_paramsStack);
    auto *l = new QFormLayout(page);
    m_titleSearchEdit = new QLineEdit(page);
    m_titleSearchEdit->setPlaceholderText(tr("e.g. 'concert' or 'Episode 1'"));
    l->addRow(tr("Title contains:"), m_titleSearchEdit);
    auto *hint =
        new QLabel(tr("Matches items whose title contains the text above (case-insensitive "
                      "for ASCII characters). Empty text matches nothing."),
                   page);
    hint->setWordWrap(true);
    hint->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addRow(hint);
    m_paramsStack->insertWidget(PAGE_BY_TITLE_SEARCH, page);
  }
  buildStateFlagPage(PAGE_MISSING_ARTWORK,
                     tr("Includes every item without a real cover image — useful as a "
                        "to-scrape worklist."));
  buildStateFlagPage(PAGE_FAVORITE,
                     tr("Includes every item present in the Favorites playlist. Combine "
                        "with another search to see your favorited subset by type or tag."));

  root->addWidget(m_paramsStack);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // Disable Ok until the form is complete: a non-blank name AND, for criteria
  // that need one, a non-empty parameter. Re-evaluated when the name, the
  // criterion, or a criterion parameter changes so a "By extension" / "Title
  // contains" / "By collection" with an empty parameter can't be accepted into
  // an always-empty playlist (Kartend-dsvaq).
  m_okButton = buttons->button(QDialogButtonBox::Ok);
  m_okButton->setEnabled(false);
  connect(m_nameEdit, &QLineEdit::textChanged, this,
          &CreateSmartPlaylistDialog::updateOkButtonState);
  connect(m_extensionsEdit, &QLineEdit::textChanged, this,
          &CreateSmartPlaylistDialog::updateOkButtonState);
  connect(m_titleSearchEdit, &QLineEdit::textChanged, this,
          &CreateSmartPlaylistDialog::updateOkButtonState);
  connect(m_collectionCombo, &QComboBox::currentIndexChanged, this,
          &CreateSmartPlaylistDialog::updateOkButtonState);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  connect(m_kindCombo, &QComboBox::currentIndexChanged, this,
          &CreateSmartPlaylistDialog::onKindChanged);
  m_kindCombo->setCurrentIndex(PAGE_RECENT);
  m_paramsStack->setCurrentIndex(PAGE_RECENT);
}

void CreateSmartPlaylistDialog::onKindChanged(int index) {
  const int page = m_kindCombo->itemData(index).toInt();
  m_paramsStack->setCurrentIndex(page);
  updateOkButtonState();
}

void CreateSmartPlaylistDialog::updateOkButtonState() {
  if (!m_okButton) {
    return;
  }
  const bool nameOk = m_nameEdit && !m_nameEdit->text().trimmed().isEmpty();
  bool paramOk = true;
  switch (m_kindCombo ? m_kindCombo->currentData().toInt() : -1) {
  case PAGE_BY_EXTENSION:
    paramOk = m_extensionsEdit && !m_extensionsEdit->text().trimmed().isEmpty();
    break;
  case PAGE_BY_TITLE_SEARCH:
    paramOk = m_titleSearchEdit && !m_titleSearchEdit->text().trimmed().isEmpty();
    break;
  case PAGE_BY_COLLECTION:
    paramOk = m_collectionCombo && m_collectionCombo->currentIndex() >= 0;
    break;
  default:
    break;
  }
  m_okButton->setEnabled(nameOk && paramOk);
}

void CreateSmartPlaylistDialog::setInitialName(const QString &name) {
  if (m_nameEdit) {
    m_nameEdit->setText(name);
  }
}

void CreateSmartPlaylistDialog::setInitialFilter(const SmartFilter::Filter &filter) {
  const int page = kindToPage(filter.kind);
  for (int i = 0; i < m_kindCombo->count(); ++i) {
    if (m_kindCombo->itemData(i).toInt() == page) {
      m_kindCombo->setCurrentIndex(i);
      break;
    }
  }
  m_paramsStack->setCurrentIndex(page);
  switch (filter.kind) {
  case SmartFilter::Kind::RecentlyLaunched:
    m_recentLimitSpin->setValue(filter.limit);
    break;
  case SmartFilter::Kind::TopPlayed:
    m_topLimitSpin->setValue(filter.limit);
    break;
  case SmartFilter::Kind::NeverPlayed:
    m_neverLimitSpin->setValue(filter.limit);
    break;
  case SmartFilter::Kind::ByExtension:
    m_extensionsEdit->setText(filter.extensions.join(QStringLiteral(", ")));
    break;
  case SmartFilter::Kind::HasArtwork:
  case SmartFilter::Kind::Pinned:
  case SmartFilter::Kind::Hidden:
  case SmartFilter::Kind::ContinueLater:
  case SmartFilter::Kind::MissingArtwork:
  case SmartFilter::Kind::Favorite:
    break;
  case SmartFilter::Kind::ByDateAdded:
    m_dateAddedDaysSpin->setValue(filter.days);
    break;
  case SmartFilter::Kind::ByCollection:
    if (m_collectionCombo) {
      const int idx = m_collectionCombo->findData(filter.collectionUuid);
      if (idx >= 0) {
        m_collectionCombo->setCurrentIndex(idx);
      }
    }
    break;
  case SmartFilter::Kind::ByTitleSearch:
    if (m_titleSearchEdit) {
      m_titleSearchEdit->setText(filter.titleSearch);
    }
    break;
  }
}

void CreateSmartPlaylistDialog::setCollectionList(const QList<CollectionEntry> &collections) {
  if (!m_collectionCombo) {
    return;
  }
  m_collectionCombo->clear();
  for (const auto &pair : collections) {
    m_collectionCombo->addItem(pair.first, pair.second);
  }
}

QString CreateSmartPlaylistDialog::name() const {
  return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

SmartFilter::Filter CreateSmartPlaylistDialog::filter() const {
  SmartFilter::Filter f;
  const int page = m_kindCombo->currentData().toInt();
  f.kind = pageToKind(page);
  switch (f.kind) {
  case SmartFilter::Kind::RecentlyLaunched:
    f.limit = m_recentLimitSpin->value();
    break;
  case SmartFilter::Kind::TopPlayed:
    f.limit = m_topLimitSpin->value();
    break;
  case SmartFilter::Kind::NeverPlayed:
    f.limit = m_neverLimitSpin->value();
    break;
  case SmartFilter::Kind::ByExtension: {
    const QStringList raw = m_extensionsEdit->text().split(',', Qt::SkipEmptyParts);
    for (const QString &ext : raw) {
      QString clean = ext.trimmed().toLower();
      if (clean.startsWith('.')) {
        clean.remove(0, 1);
      }
      if (!clean.isEmpty()) {
        f.extensions.append(clean);
      }
    }
    break;
  }
  case SmartFilter::Kind::HasArtwork:
  case SmartFilter::Kind::Pinned:
  case SmartFilter::Kind::Hidden:
  case SmartFilter::Kind::ContinueLater:
  case SmartFilter::Kind::MissingArtwork:
  case SmartFilter::Kind::Favorite:
    break;
  case SmartFilter::Kind::ByDateAdded:
    f.days = m_dateAddedDaysSpin->value();
    break;
  case SmartFilter::Kind::ByCollection:
    if (m_collectionCombo) {
      f.collectionUuid = m_collectionCombo->currentData().toString();
    }
    break;
  case SmartFilter::Kind::ByTitleSearch:
    if (m_titleSearchEdit) {
      f.titleSearch = m_titleSearchEdit->text().trimmed();
    }
    break;
  }
  return f;
}
