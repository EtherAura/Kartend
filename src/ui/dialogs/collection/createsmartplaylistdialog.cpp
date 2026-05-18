// Smart-playlist creation/edit dialog. Built programmatically rather than
// from a .ui file because the per-kind parameter pages are dynamic enough
// that Designer would offer little above just laying them out in code.
#include "createsmartplaylistdialog.h"

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
    hint->setStyleSheet("color: palette(mid); font-style: italic;");
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
    m_hasArtworkNote->setStyleSheet("color: palette(mid); font-style: italic;");
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
    hint->setStyleSheet("color: palette(mid); font-style: italic;");
    l->addRow(hint);
    m_paramsStack->insertWidget(PAGE_BY_DATE_ADDED, page);
  }

  root->addWidget(m_paramsStack);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // Disable Ok until the name is non-blank — prevents accepting and then
  // hitting the createSmartPlaylist InvalidArgument branch.
  auto *okBtn = buttons->button(QDialogButtonBox::Ok);
  okBtn->setEnabled(false);
  connect(m_nameEdit, &QLineEdit::textChanged, this,
          [okBtn](const QString &t) { okBtn->setEnabled(!t.trimmed().isEmpty()); });
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
    break;
  case SmartFilter::Kind::ByDateAdded:
    m_dateAddedDaysSpin->setValue(filter.days);
    break;
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
    break;
  case SmartFilter::Kind::ByDateAdded:
    f.days = m_dateAddedDaysSpin->value();
    break;
  }
  return f;
}
