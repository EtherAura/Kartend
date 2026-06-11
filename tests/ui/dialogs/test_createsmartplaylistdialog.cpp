// Kartend-0eeuk: CreateSmartPlaylistDialog rule construction. Covers the
// kind→Filter mapping (per-kind limit spins, extension parsing with
// dot-strip/lowercase/empty-drop, days window, collection uuid, trimmed
// title needle), the Ok gate (non-blank name AND a non-empty parameter for
// the three parameterised criteria — Kartend-dsvaq), the setInitialName /
// setInitialFilter edit-flow preload, and name() trimming. Driven
// headlessly — the dialog is never shown or exec()'d.

#include "createsmartplaylistdialog.h"

#include "smartfilter.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>

namespace {

QLineEdit *editWithPlaceholder(const QWidget &dlg, const QString &needle) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->placeholderText().contains(needle)) {
      return e;
    }
  }
  return nullptr;
}

QLineEdit *nameEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("Recently launched"));
}

QLineEdit *extensionsEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("mp4, mkv"));
}

QLineEdit *titleSearchEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("Episode 1"));
}

/// The criterion combo is the one whose items carry the stack-page indices;
/// identify it as the combo with the full criteria list (the only other
/// combo is the ByCollection picker, empty until setCollectionList).
QComboBox *kindCombo(const QWidget &dlg) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->count() >= 13) {
      return c;
    }
  }
  return nullptr;
}

QComboBox *collectionCombo(const QWidget &dlg) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->placeholderText().contains(QStringLiteral("Pick a collection"))) {
      return c;
    }
  }
  return nullptr;
}

QPushButton *okButton(const QWidget &dlg) {
  auto *box = dlg.findChild<QDialogButtonBox *>();
  return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}

/// Select a criterion by display label so the test doesn't bake in the
/// page-index constants.
void pickKind(QComboBox *combo, const QString &label) {
  const int idx = combo->findText(label);
  QVERIFY(idx >= 0);
  combo->setCurrentIndex(idx);
}

} // namespace

class TestCreateSmartPlaylistDialog : public QObject {
  Q_OBJECT

private slots:
  void defaultFilterIsRecentlyLaunchedWithDefaultLimit();
  void countedKindsRoundTripTheirOwnLimit();
  void extensionParsingNormalises();
  void okGatesOnNameAndCriterionParameter();
  void byCollectionMapsUuidBothWays();
  void titleSearchAndDateWindowMap();
  void nameIsTrimmed();
};

void TestCreateSmartPlaylistDialog::defaultFilterIsRecentlyLaunchedWithDefaultLimit() {
  CreateSmartPlaylistDialog dlg;
  const SmartFilter::Filter f = dlg.filter();
  QCOMPARE(f.kind, SmartFilter::Kind::RecentlyLaunched);
  QCOMPARE(f.limit, 50);
}

void TestCreateSmartPlaylistDialog::countedKindsRoundTripTheirOwnLimit() {
  // Each counted kind owns a separate spin — a limit set while editing a
  // TopPlayed playlist must not bleed into the other kinds' spins.
  CreateSmartPlaylistDialog dlg;
  SmartFilter::Filter top;
  top.kind = SmartFilter::Kind::TopPlayed;
  top.limit = 250;
  dlg.setInitialFilter(top);
  QCOMPARE(dlg.filter().kind, SmartFilter::Kind::TopPlayed);
  QCOMPARE(dlg.filter().limit, 250);

  pickKind(kindCombo(dlg), QStringLiteral("Never launched"));
  QCOMPARE(dlg.filter().kind, SmartFilter::Kind::NeverPlayed);
  QCOMPARE(dlg.filter().limit, 50); // untouched spin keeps its default

  pickKind(kindCombo(dlg), QStringLiteral("Most played"));
  QCOMPARE(dlg.filter().limit, 250); // the edited spin survives the toggle
}

void TestCreateSmartPlaylistDialog::extensionParsingNormalises() {
  CreateSmartPlaylistDialog dlg;
  pickKind(kindCombo(dlg), QStringLiteral("By extension"));
  extensionsEdit(dlg)->setText(QStringLiteral(" .MP4, mkv ,, webm , . "));

  const SmartFilter::Filter f = dlg.filter();
  QCOMPARE(f.kind, SmartFilter::Kind::ByExtension);
  // Dot stripped, lowercased, blank/dot-only entries dropped.
  QCOMPARE(f.extensions,
           QStringList({QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm")}));
}

void TestCreateSmartPlaylistDialog::okGatesOnNameAndCriterionParameter() {
  CreateSmartPlaylistDialog dlg;
  QPushButton *ok = okButton(dlg);
  QVERIFY(ok);
  QVERIFY(!ok->isEnabled()); // blank name

  nameEdit(dlg)->setText(QStringLiteral("Watchlist"));
  QVERIFY(ok->isEnabled()); // RecentlyLaunched needs no parameter

  // The three parameterised criteria each hold the gate until filled —
  // otherwise an always-empty playlist could be created silently.
  pickKind(kindCombo(dlg), QStringLiteral("By extension"));
  QVERIFY(!ok->isEnabled());
  extensionsEdit(dlg)->setText(QStringLiteral("mkv"));
  QVERIFY(ok->isEnabled());
  extensionsEdit(dlg)->setText(QStringLiteral("   "));
  QVERIFY(!ok->isEnabled()); // whitespace-only is still empty

  pickKind(kindCombo(dlg), QStringLiteral("Title contains…"));
  QVERIFY(!ok->isEnabled());
  titleSearchEdit(dlg)->setText(QStringLiteral("concert"));
  QVERIFY(ok->isEnabled());

  pickKind(kindCombo(dlg), QStringLiteral("By collection"));
  QVERIFY(!ok->isEnabled()); // no collection list supplied → nothing pickable
  dlg.setCollectionList({{QStringLiteral("Films"), QStringLiteral("uuid-f")}});
  collectionCombo(dlg)->setCurrentIndex(0);
  QVERIFY(ok->isEnabled());

  // Paramless kinds release the gate again.
  pickKind(kindCombo(dlg), QStringLiteral("Pinned"));
  QVERIFY(ok->isEnabled());
}

void TestCreateSmartPlaylistDialog::byCollectionMapsUuidBothWays() {
  CreateSmartPlaylistDialog dlg;
  dlg.setCollectionList({{QStringLiteral("Films"), QStringLiteral("uuid-f")},
                         {QStringLiteral("Concerts"), QStringLiteral("uuid-c")}});
  pickKind(kindCombo(dlg), QStringLiteral("By collection"));
  collectionCombo(dlg)->setCurrentIndex(1);
  QCOMPARE(dlg.filter().kind, SmartFilter::Kind::ByCollection);
  QCOMPARE(dlg.filter().collectionUuid, QStringLiteral("uuid-c"));

  // Edit flow: preloading an existing filter selects the matching entry.
  CreateSmartPlaylistDialog editDlg;
  editDlg.setCollectionList({{QStringLiteral("Films"), QStringLiteral("uuid-f")},
                             {QStringLiteral("Concerts"), QStringLiteral("uuid-c")}});
  SmartFilter::Filter existing;
  existing.kind = SmartFilter::Kind::ByCollection;
  existing.collectionUuid = QStringLiteral("uuid-f");
  editDlg.setInitialFilter(existing);
  QCOMPARE(editDlg.filter().collectionUuid, QStringLiteral("uuid-f"));
}

void TestCreateSmartPlaylistDialog::titleSearchAndDateWindowMap() {
  CreateSmartPlaylistDialog dlg;
  pickKind(kindCombo(dlg), QStringLiteral("Title contains…"));
  titleSearchEdit(dlg)->setText(QStringLiteral("  concert  "));
  QCOMPARE(dlg.filter().kind, SmartFilter::Kind::ByTitleSearch);
  QCOMPARE(dlg.filter().titleSearch, QStringLiteral("concert")); // trimmed

  SmartFilter::Filter dated;
  dated.kind = SmartFilter::Kind::ByDateAdded;
  dated.days = 90;
  dlg.setInitialFilter(dated);
  QCOMPARE(dlg.filter().kind, SmartFilter::Kind::ByDateAdded);
  QCOMPARE(dlg.filter().days, 90);
}

void TestCreateSmartPlaylistDialog::nameIsTrimmed() {
  CreateSmartPlaylistDialog dlg;
  dlg.setInitialName(QStringLiteral("  Weekend queue  "));
  QCOMPARE(dlg.name(), QStringLiteral("Weekend queue"));
}

QTEST_MAIN(TestCreateSmartPlaylistDialog)
#include "test_createsmartplaylistdialog.moc"
