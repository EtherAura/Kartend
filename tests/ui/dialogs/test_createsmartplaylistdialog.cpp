// Kartend-0eeuk: CreateSmartPlaylistDialog rule construction. Covers the
// kind→Filter mapping (per-kind limit spins, extension parsing with
// dot-strip/lowercase/empty-drop, days window, collection uuid, trimmed
// title needle), the Ok gate (non-blank name AND a non-empty parameter for
// the three parameterised criteria — Kartend-dsvaq), the setInitialName /
// setInitialFilterSet edit-flow preload, and name() trimming. Driven
// headlessly — the dialog is never shown or exec()'d.
//
// Kartend-8pn2w added the rule LIST on top: add/remove rows, the Match
// all/any selector, and the Ok gate spanning every rule rather than the
// first. The single-rule cases below read rule[0] through firstRule().

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

/// Single-rule shorthand: most cases here still exercise one rule, and a
/// set of one is exactly the playlist they were written against.
SmartFilter::Filter firstRule(const CreateSmartPlaylistDialog &dlg) {
  const SmartFilter::FilterSet set = dlg.filterSet();
  return set.rules.isEmpty() ? SmartFilter::Filter{} : set.rules.first();
}

/// Wraps a lone filter as the one-rule set setInitialFilterSet expects.
SmartFilter::FilterSet asSet(const SmartFilter::Filter &f) {
  return SmartFilter::FilterSet{SmartFilter::MatchMode::All, {f}};
}

/// The Match all/any selector — the only two-item combo in the dialog.
QComboBox *matchCombo(const QWidget &dlg) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->count() == 2) {
      return c;
    }
  }
  return nullptr;
}

/// The rule row a criterion combo belongs to — its SmartRuleEditor parent.
/// Every per-row widget lookup must be scoped through this: each row owns a
/// FULL set of parameter widgets (all criteria have a page on every row), so
/// an unscoped findChildren for e.g. the extensions edit always returns row
/// 0's, whichever row is actually the ByExtension one.
QWidget *ruleRow(const QWidget &dlg, int index);

/// Criterion combos, one per rule row, in display order.
QList<QComboBox *> kindCombos(const QWidget &dlg) {
  QList<QComboBox *> out;
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->count() >= 13) {
      out.append(c);
    }
  }
  return out;
}

QWidget *ruleRow(const QWidget &dlg, int index) {
  const auto combos = kindCombos(dlg);
  return (index >= 0 && index < combos.size()) ? combos.at(index)->parentWidget() : nullptr;
}

/// Row-scoped parameter-widget lookups.
QLineEdit *rowExtensionsEdit(const QWidget &dlg, int index) {
  QWidget *row = ruleRow(dlg, index);
  return row ? editWithPlaceholder(*row, QStringLiteral("mp4, mkv")) : nullptr;
}

QLineEdit *rowTitleSearchEdit(const QWidget &dlg, int index) {
  QWidget *row = ruleRow(dlg, index);
  return row ? editWithPlaceholder(*row, QStringLiteral("Episode 1")) : nullptr;
}

QPushButton *buttonWithText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

QList<QPushButton *> removeButtons(const QWidget &dlg) {
  QList<QPushButton *> out;
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == QStringLiteral("Remove")) {
      out.append(b);
    }
  }
  return out;
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
  void addRuleAppendsRowsAndBuildsAComposedSet();
  void lastRuleCannotBeRemovedAndMatchHidesAtOneRule();
  void okGatesOnEveryRuleNotJustTheFirst();
  void initialFilterSetLoadsEveryRuleAndTheMatchMode();
};

void TestCreateSmartPlaylistDialog::defaultFilterIsRecentlyLaunchedWithDefaultLimit() {
  CreateSmartPlaylistDialog dlg;
  const SmartFilter::Filter f = firstRule(dlg);
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
  dlg.setInitialFilterSet(asSet(top));
  QCOMPARE(firstRule(dlg).kind, SmartFilter::Kind::TopPlayed);
  QCOMPARE(firstRule(dlg).limit, 250);

  pickKind(kindCombos(dlg).first(), QStringLiteral("Never launched"));
  QCOMPARE(firstRule(dlg).kind, SmartFilter::Kind::NeverPlayed);
  QCOMPARE(firstRule(dlg).limit, 50); // untouched spin keeps its default

  pickKind(kindCombos(dlg).first(), QStringLiteral("Most played"));
  QCOMPARE(firstRule(dlg).limit, 250); // the edited spin survives the toggle
}

void TestCreateSmartPlaylistDialog::extensionParsingNormalises() {
  CreateSmartPlaylistDialog dlg;
  pickKind(kindCombos(dlg).first(), QStringLiteral("By extension"));
  extensionsEdit(dlg)->setText(QStringLiteral(" .MP4, mkv ,, webm , . "));

  const SmartFilter::Filter f = firstRule(dlg);
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
  pickKind(kindCombos(dlg).first(), QStringLiteral("By extension"));
  QVERIFY(!ok->isEnabled());
  extensionsEdit(dlg)->setText(QStringLiteral("mkv"));
  QVERIFY(ok->isEnabled());
  extensionsEdit(dlg)->setText(QStringLiteral("   "));
  QVERIFY(!ok->isEnabled()); // whitespace-only is still empty

  pickKind(kindCombos(dlg).first(), QStringLiteral("Title contains…"));
  QVERIFY(!ok->isEnabled());
  titleSearchEdit(dlg)->setText(QStringLiteral("concert"));
  QVERIFY(ok->isEnabled());

  pickKind(kindCombos(dlg).first(), QStringLiteral("By collection"));
  QVERIFY(!ok->isEnabled()); // no collection list supplied → nothing pickable
  dlg.setCollectionList({{QStringLiteral("Films"), QStringLiteral("uuid-f")}});
  collectionCombo(dlg)->setCurrentIndex(0);
  QVERIFY(ok->isEnabled());

  // Paramless kinds release the gate again.
  pickKind(kindCombos(dlg).first(), QStringLiteral("Pinned"));
  QVERIFY(ok->isEnabled());
}

void TestCreateSmartPlaylistDialog::byCollectionMapsUuidBothWays() {
  CreateSmartPlaylistDialog dlg;
  dlg.setCollectionList({{QStringLiteral("Films"), QStringLiteral("uuid-f")},
                         {QStringLiteral("Concerts"), QStringLiteral("uuid-c")}});
  pickKind(kindCombos(dlg).first(), QStringLiteral("By collection"));
  collectionCombo(dlg)->setCurrentIndex(1);
  QCOMPARE(firstRule(dlg).kind, SmartFilter::Kind::ByCollection);
  QCOMPARE(firstRule(dlg).collectionUuid, QStringLiteral("uuid-c"));

  // Edit flow: preloading an existing filter selects the matching entry.
  CreateSmartPlaylistDialog editDlg;
  editDlg.setCollectionList({{QStringLiteral("Films"), QStringLiteral("uuid-f")},
                             {QStringLiteral("Concerts"), QStringLiteral("uuid-c")}});
  SmartFilter::Filter existing;
  existing.kind = SmartFilter::Kind::ByCollection;
  existing.collectionUuid = QStringLiteral("uuid-f");
  editDlg.setInitialFilterSet(asSet(existing));
  QCOMPARE(firstRule(editDlg).collectionUuid, QStringLiteral("uuid-f"));
}

void TestCreateSmartPlaylistDialog::titleSearchAndDateWindowMap() {
  CreateSmartPlaylistDialog dlg;
  pickKind(kindCombos(dlg).first(), QStringLiteral("Title contains…"));
  titleSearchEdit(dlg)->setText(QStringLiteral("  concert  "));
  QCOMPARE(firstRule(dlg).kind, SmartFilter::Kind::ByTitleSearch);
  QCOMPARE(firstRule(dlg).titleSearch, QStringLiteral("concert")); // trimmed

  SmartFilter::Filter dated;
  dated.kind = SmartFilter::Kind::ByDateAdded;
  dated.days = 90;
  dlg.setInitialFilterSet(asSet(dated));
  QCOMPARE(firstRule(dlg).kind, SmartFilter::Kind::ByDateAdded);
  QCOMPARE(firstRule(dlg).days, 90);
}

void TestCreateSmartPlaylistDialog::nameIsTrimmed() {
  CreateSmartPlaylistDialog dlg;
  dlg.setInitialName(QStringLiteral("  Weekend queue  "));
  QCOMPARE(dlg.name(), QStringLiteral("Weekend queue"));
}

void TestCreateSmartPlaylistDialog::addRuleAppendsRowsAndBuildsAComposedSet() {
  CreateSmartPlaylistDialog dlg;
  QCOMPARE(kindCombos(dlg).size(), 1); // a dialog always opens with one rule

  QPushButton *add = buttonWithText(dlg, QStringLiteral("Add rule"));
  QVERIFY(add);
  add->click();
  QCOMPARE(kindCombos(dlg).size(), 2);

  // Give the two rows different criteria and read the set back in order.
  pickKind(kindCombos(dlg).at(0), QStringLiteral("Never launched"));
  pickKind(kindCombos(dlg).at(1), QStringLiteral("By extension"));
  rowExtensionsEdit(dlg, 1)->setText(QStringLiteral("pdf"));

  const SmartFilter::FilterSet set = dlg.filterSet();
  QCOMPARE(set.rules.size(), 2);
  QCOMPARE(set.rules[0].kind, SmartFilter::Kind::NeverPlayed);
  QCOMPARE(set.rules[1].kind, SmartFilter::Kind::ByExtension);
  QCOMPARE(set.rules[1].extensions, QStringList{QStringLiteral("pdf")});

  // Match defaults to All and follows the selector.
  QCOMPARE(set.match, SmartFilter::MatchMode::All);
  QComboBox *match = matchCombo(dlg);
  QVERIFY(match);
  match->setCurrentIndex(match->findData(static_cast<int>(SmartFilter::MatchMode::Any)));
  QCOMPARE(dlg.filterSet().match, SmartFilter::MatchMode::Any);
}

void TestCreateSmartPlaylistDialog::lastRuleCannotBeRemovedAndMatchHidesAtOneRule() {
  CreateSmartPlaylistDialog dlg;
  // One rule: nothing to combine, so the Match selector is not shown, and
  // the sole Remove is disabled — a set with no rules is rejected on parse,
  // so there is no valid state with zero rows.
  QCOMPARE(removeButtons(dlg).size(), 1);
  QVERIFY(!removeButtons(dlg).first()->isEnabled());
  QVERIFY(matchCombo(dlg));
  QVERIFY(!matchCombo(dlg)->isVisibleTo(&dlg));

  buttonWithText(dlg, QStringLiteral("Add rule"))->click();
  QCOMPARE(kindCombos(dlg).size(), 2);
  QVERIFY(matchCombo(dlg)->isVisibleTo(&dlg));
  for (QPushButton *b : removeButtons(dlg)) {
    QVERIFY(b->isEnabled());
  }

  // Removing takes the row back out and restores the single-rule chrome.
  removeButtons(dlg).at(1)->click();
  QCOMPARE(dlg.filterSet().rules.size(), 1);
  QVERIFY(!matchCombo(dlg)->isVisibleTo(&dlg));
  QVERIFY(!removeButtons(dlg).first()->isEnabled());
}

void TestCreateSmartPlaylistDialog::okGatesOnEveryRuleNotJustTheFirst() {
  // The single-rule gate existed already (Kartend-dsvaq); the trap when the
  // list arrived was gating on rule[0] alone, which lets a Match-all set
  // with one blank rule through — and that matches nothing at all.
  CreateSmartPlaylistDialog dlg;
  nameEdit(dlg)->setText(QStringLiteral("Unread PDFs"));
  QPushButton *ok = okButton(dlg);
  QVERIFY(ok->isEnabled()); // one paramless rule, name filled

  buttonWithText(dlg, QStringLiteral("Add rule"))->click();
  pickKind(kindCombos(dlg).at(1), QStringLiteral("By extension"));
  QVERIFY2(!ok->isEnabled(), "an incomplete SECOND rule must hold the gate");

  rowExtensionsEdit(dlg, 1)->setText(QStringLiteral("pdf"));
  QVERIFY(ok->isEnabled());

  // …and removing the offending row releases it again.
  rowExtensionsEdit(dlg, 1)->setText(QString());
  QVERIFY(!ok->isEnabled());
  removeButtons(dlg).at(1)->click();
  QVERIFY(ok->isEnabled());
}

void TestCreateSmartPlaylistDialog::initialFilterSetLoadsEveryRuleAndTheMatchMode() {
  CreateSmartPlaylistDialog dlg;
  SmartFilter::Filter never;
  never.kind = SmartFilter::Kind::NeverPlayed;
  never.limit = 8;
  SmartFilter::Filter pdfs;
  pdfs.kind = SmartFilter::Kind::ByExtension;
  pdfs.extensions = {QStringLiteral("pdf")};
  dlg.setInitialFilterSet({SmartFilter::MatchMode::Any, {never, pdfs}});

  QCOMPARE(kindCombos(dlg).size(), 2); // the default blank row is replaced
  const SmartFilter::FilterSet set = dlg.filterSet();
  QCOMPARE(set.match, SmartFilter::MatchMode::Any);
  QCOMPARE(set.rules.size(), 2);
  QCOMPARE(set.rules[0].kind, SmartFilter::Kind::NeverPlayed);
  QCOMPARE(set.rules[0].limit, 8);
  QCOMPARE(set.rules[1].extensions, QStringList{QStringLiteral("pdf")});

  // An empty set is ignored rather than emptying the form — zero rows is a
  // state the format rejects, so the dialog must never present it.
  CreateSmartPlaylistDialog untouched;
  untouched.setInitialFilterSet({});
  QCOMPARE(untouched.filterSet().rules.size(), 1);
}

QTEST_MAIN(TestCreateSmartPlaylistDialog)
#include "test_createsmartplaylistdialog.moc"
