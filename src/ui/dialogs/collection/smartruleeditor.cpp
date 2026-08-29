// One rule of a smart playlist — criterion combo + its parameter page.
// Built programmatically rather than from a .ui file for the same reason the
// dialog around it is: the per-kind pages are dynamic enough that Designer
// would offer little over laying them out in code.
//
// Moved here from CreateSmartPlaylistDialog (Kartend-8pn2w). The page
// constants, kindToPage/pageToKind, and the per-kind widget set came across
// unchanged; what is new is that they are now scoped to a single rule, so a
// dialog can own several without owning several of everything.
#include "smartruleeditor.h"

#include "uiconstants/color.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

namespace {

constexpr int LIMIT_MIN = 1;
constexpr int LIMIT_MAX = 1000;
constexpr int LIMIT_DEFAULT = 50;

// Stack-page indices kept in sync with the combo's userData. Compile-time
// constants so the page switch + setFilter share one source of truth —
// adding a kind requires touching exactly these integers + the combo
// population in buildUI.
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

SmartRuleEditor::SmartRuleEditor(QWidget *parent) : QWidget(parent) {
  buildUI();
}

void SmartRuleEditor::buildUI() {
  auto *root = new QVBoxLayout(this);
  // No outer margin: the owning dialog lays these out in a column and adds
  // its own spacing, so a per-rule margin would double up.
  root->setContentsMargins(0, 0, 0, 0);

  auto *form = new QFormLayout();
  m_kindCombo = new QComboBox(this);
  // userData carries the page index so the change handler can route directly
  // without an extra map. Order here MUST match the insertWidget() order
  // below — the page indices ARE the row positions.
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

  // Parameter pages — one per kind, swapped in when the criterion changes.
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
    auto *note = new QLabel(tr("Includes every item across every collection that has a "
                               "real cover image (not the procedural placeholder)."),
                            page);
    note->setWordWrap(true);
    note->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    l->addWidget(note);
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
  // State-flag pages (Pinned / Hidden / Continue later / Missing artwork /
  // Favorites). Each takes no parameters — the filter is "all items where the
  // matching boolean is 1". Hint text doubles as the page content so the row
  // isn't blank.
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
  // ByCollection — pick a specific source collection by uuid. The combo is
  // populated by setCollectionList() before the dialog runs; left empty here
  // so the widget still constructs cleanly when the caller forgets (the
  // evaluator returns no matches then, so the failure is obvious).
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
  // ByTitleSearch — substring search against items.name. SQL LIKE is
  // ASCII-case-insensitive by default; multibyte titles round-trip
  // case-sensitively.
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

  // Every editable widget reports upward: the dialog re-validates the WHOLE
  // rule set on each change, because one incomplete rule invalidates the set.
  connect(m_kindCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    m_paramsStack->setCurrentIndex(m_kindCombo->itemData(index).toInt());
    emit changed();
  });
  connect(m_extensionsEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(m_titleSearchEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(m_collectionCombo, &QComboBox::currentIndexChanged, this,
          [this](int) { emit changed(); });
  for (QSpinBox *spin :
       {m_recentLimitSpin, m_topLimitSpin, m_neverLimitSpin, m_dateAddedDaysSpin}) {
    connect(spin, &QSpinBox::valueChanged, this, [this](int) { emit changed(); });
  }

  m_kindCombo->setCurrentIndex(PAGE_RECENT);
  m_paramsStack->setCurrentIndex(PAGE_RECENT);
}

void SmartRuleEditor::setCollectionList(const QList<CollectionEntry> &collections) {
  if (!m_collectionCombo) {
    return;
  }
  m_collectionCombo->clear();
  for (const auto &pair : collections) {
    m_collectionCombo->addItem(pair.first, pair.second);
  }
}

void SmartRuleEditor::setFilter(const SmartFilter::Filter &filter) {
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

SmartFilter::Filter SmartRuleEditor::filter() const {
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

bool SmartRuleEditor::isComplete() const {
  switch (m_kindCombo ? m_kindCombo->currentData().toInt() : -1) {
  case PAGE_BY_EXTENSION:
    return m_extensionsEdit && !m_extensionsEdit->text().trimmed().isEmpty();
  case PAGE_BY_TITLE_SEARCH:
    return m_titleSearchEdit && !m_titleSearchEdit->text().trimmed().isEmpty();
  case PAGE_BY_COLLECTION:
    return m_collectionCombo && m_collectionCombo->currentIndex() >= 0;
  default:
    // Every other criterion is parameterless or has a spin box that cannot
    // be empty, so there is nothing left to fill in.
    return true;
  }
}
