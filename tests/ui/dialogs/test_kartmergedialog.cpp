// Kartend-0eeuk: KartMergeDialog decision matrix. The dialog hands
// KartManager's conflict resolver a (choice, policy, applyToAll) triple;
// these tests cover the decision branches: Skip is the safe default, Skip /
// Overwrite accept immediately with their choice, the first Merge... click
// only reveals the per-field pickers (no accept), the second commits Merge
// with every checked "use incoming" box mapped onto its own MergePolicy
// flag (full 14-row bijection), and applyToAll mirrors its checkbox.
// Driven headlessly — the dialog is never shown or exec()'d.

#include "kartmergedialog.h"

#include "itemmetadata.h"
#include "kartmerge.h"

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

QPushButton *buttonWithText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

QPushButton *skipButton(const QWidget &dlg) {
  return buttonWithText(dlg, QStringLiteral("Skip"));
}

QPushButton *overwriteButton(const QWidget &dlg) {
  return buttonWithText(dlg, QStringLiteral("Overwrite"));
}

QPushButton *mergeButton(const QWidget &dlg) {
  return buttonWithText(dlg, QStringLiteral("Merge..."));
}

/// The 14 per-field "use incoming" checkboxes in form-row order (the
/// apply-to-all checkbox carries different text so it is filtered out).
QList<QCheckBox *> fieldChecks(const QWidget &dlg) {
  QList<QCheckBox *> out;
  const auto checks = dlg.findChildren<QCheckBox *>();
  for (QCheckBox *c : checks) {
    if (c->text() == QStringLiteral("use incoming")) {
      out.append(c);
    }
  }
  return out;
}

QCheckBox *applyToAllCheck(const QWidget &dlg) {
  const auto checks = dlg.findChildren<QCheckBox *>();
  for (QCheckBox *c : checks) {
    if (c->text() != QStringLiteral("use incoming")) {
      return c;
    }
  }
  return nullptr;
}

/// MergePolicy flags in the dialog's form-row order. Keep in sync with the
/// addRow sequence in kartmergedialog.cpp.
QList<bool> policyFlags(const kart::MergePolicy &p) {
  return {
      p.preferIncomingTitle,         p.preferIncomingDescription,  p.preferIncomingGenre,
      p.preferIncomingDeveloper,     p.preferIncomingPublisher,    p.preferIncomingReleaseDate,
      p.preferIncomingContentRating, p.preferIncomingPlayers,      p.preferIncomingRuntimeSeconds,
      p.preferIncomingTags,          p.preferIncomingCustomFields, p.preferIncomingManualPath,
      p.preferIncomingLauncherIndex, p.preferIncomingSource,       p.preferIncomingNotes,
      p.preferIncomingSourceUrl,     p.preferIncomingRating,       p.preferIncomingIsPinned,
      p.preferIncomingIsHidden,      p.preferIncomingContinueLater};
}

/// One entry per dialog form row, same order as policyFlags(). Kartend-fh3ab
/// grew the grid from 14 rows to 20.
constexpr int kFieldRowCount = 20;

ItemMetadataStore::ItemMetadata makeMeta(const QString &title) {
  ItemMetadataStore::ItemMetadata m;
  m.title = title;
  m.genre = QStringLiteral("documentary");
  return m;
}

} // namespace

class TestKartMergeDialog : public QObject {
  Q_OBJECT

private slots:
  void defaultChoiceIsSkipAndSkipAccepts();
  void overwriteSetsChoiceAndAccepts();
  void firstMergeClickOnlyRevealsFieldPickers();
  void secondMergeClickMapsCheckedRowsOntoPolicy();
  void everyFieldRowMapsToExactlyItsOwnPolicyFlag();
  void applyToAllMirrorsCheckbox();
};

void TestKartMergeDialog::defaultChoiceIsSkipAndSkipAccepts() {
  KartMergeDialog dlg(QStringLiteral("/m/a.mkv"), makeMeta(QStringLiteral("Old")),
                      makeMeta(QStringLiteral("New")));
  // Skip is the safe default — a caller reading choice() before any click
  // must never see Overwrite/Merge.
  QCOMPARE(dlg.choice(), kart::MergeChoice::Skip);
  QVERIFY(!dlg.applyToAll());

  QSignalSpy accepted(&dlg, &QDialog::accepted);
  QPushButton *skip = skipButton(dlg);
  QVERIFY(skip);
  skip->click();
  QCOMPARE(dlg.choice(), kart::MergeChoice::Skip);
  QCOMPARE(accepted.count(), 1);
}

void TestKartMergeDialog::overwriteSetsChoiceAndAccepts() {
  KartMergeDialog dlg(QStringLiteral("/m/a.mkv"), makeMeta(QStringLiteral("Old")),
                      makeMeta(QStringLiteral("New")));
  QSignalSpy accepted(&dlg, &QDialog::accepted);
  QPushButton *overwrite = overwriteButton(dlg);
  QVERIFY(overwrite);
  overwrite->click();
  QCOMPARE(dlg.choice(), kart::MergeChoice::Overwrite);
  QCOMPARE(accepted.count(), 1);
}

void TestKartMergeDialog::firstMergeClickOnlyRevealsFieldPickers() {
  KartMergeDialog dlg(QStringLiteral("/m/a.mkv"), makeMeta(QStringLiteral("Old")),
                      makeMeta(QStringLiteral("New")));
  auto *fields = dlg.findChild<QGroupBox *>();
  QVERIFY(fields);
  QVERIFY(!fields->isEnabled()); // pickers start locked

  QSignalSpy accepted(&dlg, &QDialog::accepted);
  QPushButton *merge = mergeButton(dlg);
  QVERIFY(merge);
  merge->click();

  // First click is "show me the per-field choices", not a commit.
  QVERIFY(fields->isEnabled());
  QCOMPARE(accepted.count(), 0);
  QCOMPARE(dlg.choice(), kart::MergeChoice::Skip); // unchanged until commit
}

void TestKartMergeDialog::secondMergeClickMapsCheckedRowsOntoPolicy() {
  KartMergeDialog dlg(QStringLiteral("/m/a.mkv"), makeMeta(QStringLiteral("Old")),
                      makeMeta(QStringLiteral("New")));
  const QList<QCheckBox *> checks = fieldChecks(dlg);
  QCOMPARE(checks.size(), kFieldRowCount);

  // Title (0), Genre (2), Tags (9), Source (13), Rating (16), Pinned (17)
  // → incoming; rest existing. The last two cover the Kartend-fh3ab rows.
  checks[0]->setChecked(true);
  checks[2]->setChecked(true);
  checks[9]->setChecked(true);
  checks[13]->setChecked(true);
  checks[16]->setChecked(true);
  checks[17]->setChecked(true);

  QSignalSpy accepted(&dlg, &QDialog::accepted);
  QPushButton *merge = mergeButton(dlg);
  QVERIFY(merge);
  merge->click(); // reveal
  merge->click(); // commit
  QCOMPARE(accepted.count(), 1);
  QCOMPARE(dlg.choice(), kart::MergeChoice::Merge);

  const QList<bool> flags = policyFlags(dlg.policy());
  const QList<int> expectedOn{0, 2, 9, 13, 16, 17};
  for (int i = 0; i < flags.size(); ++i) {
    QCOMPARE(flags[i], expectedOn.contains(i));
  }
}

void TestKartMergeDialog::everyFieldRowMapsToExactlyItsOwnPolicyFlag() {
  // Full bijection sweep: checking row i must set flag i and nothing else.
  // A swapped pair in the addRow sequence (e.g. developer/publisher) would
  // silently merge the wrong field and pass any single-spot check.
  for (int i = 0; i < kFieldRowCount; ++i) {
    KartMergeDialog dlg(QStringLiteral("/m/a.mkv"), makeMeta(QStringLiteral("Old")),
                        makeMeta(QStringLiteral("New")));
    const QList<QCheckBox *> checks = fieldChecks(dlg);
    QCOMPARE(checks.size(), kFieldRowCount);
    checks[i]->setChecked(true);

    QPushButton *merge = mergeButton(dlg);
    QVERIFY(merge);
    merge->click();
    merge->click();

    const QList<bool> flags = policyFlags(dlg.policy());
    for (int j = 0; j < flags.size(); ++j) {
      QVERIFY2(flags[j] == (j == i),
               qPrintable(QStringLiteral("checked row %1, flag %2 wrong").arg(i).arg(j)));
    }
  }
}

void TestKartMergeDialog::applyToAllMirrorsCheckbox() {
  KartMergeDialog dlg(QStringLiteral("/m/a.mkv"), makeMeta(QStringLiteral("Old")),
                      makeMeta(QStringLiteral("New")));
  QCheckBox *all = applyToAllCheck(dlg);
  QVERIFY(all);
  QVERIFY(!dlg.applyToAll());
  all->setChecked(true);
  QVERIFY(dlg.applyToAll());

  // The flag must survive any choice path — batch-skip is a real flow.
  QPushButton *skip = skipButton(dlg);
  QVERIFY(skip);
  skip->click();
  QVERIFY(dlg.applyToAll());
  QCOMPARE(dlg.choice(), kart::MergeChoice::Skip);
}

QTEST_MAIN(TestKartMergeDialog)
#include "test_kartmergedialog.moc"
