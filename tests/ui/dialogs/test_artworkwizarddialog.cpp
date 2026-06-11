// Kartend-0eeuk: ArtworkWizardDialog queue-walking + candidate-list mapping.
// The fuzzy ranking lives in ArtworkCandidates::rank (covered under
// tests/utils/); the dialog's own logic is what these tests cover: the
// caller-supplied CandidatesProvider is invoked per entry and its results
// populate the list with the absolute path in Qt::UserRole (first row
// auto-selected, Pick enabled), the no-candidates empty state (label shown,
// Pick disabled, Browse/Skip still live), the Pick decision (a true return
// from the PickHandler advances the queue, false keeps the cursor put), Skip
// advancing, and the exhausted-queue state disabling the action row. The
// async thumbnail/preview decodes are exercised incidentally with paths that
// fail the decodable-image check, so no real image IO happens. Driven
// headlessly — the dialog is never shown or exec()'d; the Browse path is not
// driven because it opens a native QFileDialog.

#include "artworkwizarddialog.h"

#include "artworkcandidates.h"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTest>

namespace {

ArtworkWizardDialog::Entry makeEntry(const QString &path, const QString &name) {
  ArtworkWizardDialog::Entry e;
  e.filePath = path;
  e.collectionUuid = QStringLiteral("uuid-1");
  e.itemName = name;
  return e;
}

// Candidate paths intentionally fail ExtensionUtils::isDecodableImagePath so
// the QtConcurrent thumbnail jobs return immediately without touching disk.
QList<ArtworkCandidates::Candidate> twoCandidates() {
  return {{QStringLiteral("/art/alpha-cover.notimg"), 90},
          {QStringLiteral("/art/alpha-back.notimg"), 60}};
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

QLabel *labelContaining(const QWidget &dlg, const QString &needle) {
  const auto labels = dlg.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->text().contains(needle)) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestArtworkWizardDialog : public QObject {
  Q_OBJECT

private slots:
  void candidatesPopulateListWithPathPayloads();
  void noCandidatesShowsEmptyStateButKeepsBrowseAndSkip();
  void pickSuccessReportsChosenPathAndAdvances();
  void pickFailureKeepsCursorOnCurrentItem();
  void skipAdvancesAndExhaustionDisablesActions();
  void providerIsCalledWithEachEntryInTurn();
};

void TestArtworkWizardDialog::candidatesPopulateListWithPathPayloads() {
  ArtworkWizardDialog dlg;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/alpha.mkv"), QStringLiteral("Alpha"))},
      [](const ArtworkWizardDialog::Entry &) { return twoCandidates(); },
      [](const ArtworkWizardDialog::Entry &, const QString &) { return true; });

  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  QCOMPARE(list->count(), 2);
  QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), QStringLiteral("/art/alpha-cover.notimg"));
  QCOMPARE(list->item(0)->text(), QStringLiteral("alpha-cover.notimg")); // file name display
  QCOMPARE(list->currentRow(), 0);                                       // best match pre-selected

  QVERIFY(buttonWithText(dlg, QStringLiteral("Pick selected"))->isEnabled());
  QVERIFY(labelContaining(dlg, QStringLiteral("Assigning item 1 of 1")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Alpha")));
  QLabel *empty = labelContaining(dlg, QStringLiteral("No fuzzy-name matches"));
  QVERIFY(empty);
  QVERIFY(!empty->isVisibleTo(&dlg)); // hidden with candidates present
}

void TestArtworkWizardDialog::noCandidatesShowsEmptyStateButKeepsBrowseAndSkip() {
  ArtworkWizardDialog dlg;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/alpha.mkv"), QStringLiteral("Alpha"))},
      [](const ArtworkWizardDialog::Entry &) { return QList<ArtworkCandidates::Candidate>{}; },
      [](const ArtworkWizardDialog::Entry &, const QString &) { return true; });

  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  QCOMPARE(list->count(), 0);
  QLabel *empty = labelContaining(dlg, QStringLiteral("No fuzzy-name matches"));
  QVERIFY(empty);
  QVERIFY(empty->isVisibleTo(&dlg)); // would be visible once the dialog shows
  QVERIFY(!buttonWithText(dlg, QStringLiteral("Pick selected"))->isEnabled());
  QVERIFY(buttonWithText(dlg, QStringLiteral("Browse…"))->isEnabled());
  QVERIFY(buttonWithText(dlg, QStringLiteral("Skip"))->isEnabled());
}

void TestArtworkWizardDialog::pickSuccessReportsChosenPathAndAdvances() {
  ArtworkWizardDialog dlg;
  QStringList picks;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/alpha.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/beta.mkv"), QStringLiteral("Beta"))},
      [](const ArtworkWizardDialog::Entry &) { return twoCandidates(); },
      [&picks](const ArtworkWizardDialog::Entry &entry, const QString &chosen) {
        picks << entry.filePath + QStringLiteral("|") + chosen;
        return true;
      });

  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  list->setCurrentRow(1); // pick the second-ranked candidate deliberately
  buttonWithText(dlg, QStringLiteral("Pick selected"))->click();

  QCOMPARE(picks, QStringList{QStringLiteral("/m/alpha.mkv|/art/alpha-back.notimg")});
  QVERIFY(labelContaining(dlg, QStringLiteral("Assigning item 2 of 2")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Beta")));
}

void TestArtworkWizardDialog::pickFailureKeepsCursorOnCurrentItem() {
  ArtworkWizardDialog dlg;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/alpha.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/beta.mkv"), QStringLiteral("Beta"))},
      [](const ArtworkWizardDialog::Entry &) { return twoCandidates(); },
      [](const ArtworkWizardDialog::Entry &, const QString &) {
        return false; // persist failed — the wizard must not move on
      });

  buttonWithText(dlg, QStringLiteral("Pick selected"))->click();
  QVERIFY(labelContaining(dlg, QStringLiteral("Assigning item 1 of 2")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Alpha")));
}

void TestArtworkWizardDialog::skipAdvancesAndExhaustionDisablesActions() {
  ArtworkWizardDialog dlg;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/alpha.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/beta.mkv"), QStringLiteral("Beta"))},
      [](const ArtworkWizardDialog::Entry &) { return twoCandidates(); },
      [](const ArtworkWizardDialog::Entry &, const QString &) { return true; });

  QPushButton *skip = buttonWithText(dlg, QStringLiteral("Skip"));
  skip->click();
  QVERIFY(labelContaining(dlg, QStringLiteral("Assigning item 2 of 2")));

  skip->click();
  QVERIFY(labelContaining(dlg, QStringLiteral("Queue complete — no more items.")));
  QVERIFY(!buttonWithText(dlg, QStringLiteral("Pick selected"))->isEnabled());
  QVERIFY(!buttonWithText(dlg, QStringLiteral("Browse…"))->isEnabled());
  QVERIFY(!skip->isEnabled());
  auto *list = dlg.findChild<QListWidget *>();
  QCOMPARE(list->count(), 0); // stale candidates cleared with the queue
}

void TestArtworkWizardDialog::providerIsCalledWithEachEntryInTurn() {
  ArtworkWizardDialog dlg;
  QStringList probed;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/alpha.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/beta.mkv"), QStringLiteral("Beta"))},
      [&probed](const ArtworkWizardDialog::Entry &entry) {
        probed << entry.filePath;
        return QList<ArtworkCandidates::Candidate>{};
      },
      [](const ArtworkWizardDialog::Entry &, const QString &) { return true; });

  QCOMPARE(probed, QStringList{QStringLiteral("/m/alpha.mkv")});
  buttonWithText(dlg, QStringLiteral("Skip"))->click();
  QCOMPARE(probed, (QStringList{QStringLiteral("/m/alpha.mkv"), QStringLiteral("/m/beta.mkv")}));
}

QTEST_MAIN(TestArtworkWizardDialog)
#include "test_artworkwizarddialog.moc"
