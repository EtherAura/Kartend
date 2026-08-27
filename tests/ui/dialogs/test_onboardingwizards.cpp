// First-run-path coverage: the two onboarding wizards every new install can
// hit. Driven headlessly (offscreen QPA, never exec()'d): page sequencing,
// the media-page Next gate (mandatory fields + directory-must-exist), and
// the Finish handlers that fold wizard fields into the Result the caller
// persists. The Browse buttons open native QFileDialogs and are not driven;
// fields are set through QWizard::setField exactly as the pages registered
// them.

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QWizard>

#include "collection/typehelpers.h"
#include "firstrunwizard.h"
#include "libraryonboardingwizard.h"

class TestOnboardingWizards : public QObject {
  Q_OBJECT
private slots:
  void firstRun_pagesSequence();
  void firstRun_mediaPageGatesNextOnRealDirectory();
  void firstRun_finishPopulatesTrimmedResult();
  void firstRun_cancelLeavesResultUnaccepted();
  void onboarding_mediaPageGatesNextOnRealDirectory();
  void onboarding_finishBuildsConfigFromFields();
  void onboarding_typeComboOffersCanonicalPresets();
  void onboarding_summaryEchoesEveryStep();
};

void TestOnboardingWizards::firstRun_pagesSequence() {
  FirstRunWizard wizard;
  wizard.restart();
  QCOMPARE(wizard.pageIds().size(), 3);
  QVERIFY(wizard.currentPage());
  QCOMPARE(wizard.currentId(), wizard.pageIds().first());
  // The welcome page carries no fields — it must never block Next.
  QVERIFY(wizard.currentPage()->isComplete());
  wizard.next();
  QCOMPARE(wizard.currentId(), wizard.pageIds().at(1));
}

void TestOnboardingWizards::firstRun_mediaPageGatesNextOnRealDirectory() {
  FirstRunWizard wizard;
  wizard.restart();
  wizard.next(); // welcome -> media page
  QWizardPage *media = wizard.currentPage();
  QVERIFY(media);

  // Empty fields: gated.
  QVERIFY(!media->isComplete());
  // Name alone: still gated.
  wizard.setField(QStringLiteral("collectionName"), QStringLiteral("Videos"));
  QVERIFY(!media->isComplete());
  // A path that is not a real directory must not enable Next — this is the
  // wizard's whole reason for subclassing the page (mandatory-field plumbing
  // only checks non-empty).
  wizard.setField(QStringLiteral("mediaDirectory"), QStringLiteral("/nonexistent/media"));
  QVERIFY(!media->isComplete());
  // A real directory satisfies the gate.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  wizard.setField(QStringLiteral("mediaDirectory"), dir.path());
  QVERIFY(media->isComplete());
}

void TestOnboardingWizards::firstRun_finishPopulatesTrimmedResult() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  FirstRunWizard wizard;
  wizard.restart();
  // Padded values: the accept handler owns the trim.
  wizard.setField(QStringLiteral("collectionName"), QStringLiteral("  Videos  "));
  wizard.setField(QStringLiteral("mediaDirectory"), QStringLiteral(" %1 ").arg(dir.path()));
  wizard.accept();

  const FirstRunWizard::Result result = wizard.result();
  QVERIFY(result.accepted);
  QCOMPARE(result.pickedConfig.name, QStringLiteral("Videos"));
  QCOMPARE(result.pickedConfig.mediaDirectory, dir.path());
  // Top-level collection defaults the caller relies on before persisting.
  QCOMPARE(result.pickedConfig.parentCollectionIndex, -1);
  QVERIFY(!result.pickedConfig.isSubcollection);
}

void TestOnboardingWizards::firstRun_cancelLeavesResultUnaccepted() {
  FirstRunWizard wizard;
  wizard.restart();
  wizard.reject();
  QVERIFY(!wizard.result().accepted);
  QVERIFY(wizard.result().pickedConfig.mediaDirectory.isEmpty());
}

void TestOnboardingWizards::onboarding_mediaPageGatesNextOnRealDirectory() {
  LibraryOnboardingWizard wizard;
  wizard.restart();
  QVERIFY(wizard.currentPage());
  QVERIFY(wizard.currentPage()->isComplete()); // welcome never blocks
  wizard.next();                               // -> name + media dir page
  QWizardPage *media = wizard.currentPage();
  QVERIFY(media);

  QVERIFY(!media->isComplete());
  wizard.setField(QStringLiteral("collectionName"), QStringLiteral("Audiobooks"));
  QVERIFY(!media->isComplete());
  wizard.setField(QStringLiteral("mediaDirectory"), QStringLiteral("/nonexistent/media"));
  QVERIFY(!media->isComplete());
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  wizard.setField(QStringLiteral("mediaDirectory"), dir.path());
  QVERIFY(media->isComplete());
}

void TestOnboardingWizards::onboarding_finishBuildsConfigFromFields() {
  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid() && artwork.isValid());

  LibraryOnboardingWizard wizard;
  wizard.restart();
  wizard.setField(QStringLiteral("collectionName"), QStringLiteral("Reference"));
  wizard.setField(QStringLiteral("mediaDirectory"), media.path());
  wizard.setField(QStringLiteral("artworkDirectory"), artwork.path());
  wizard.accept();

  const LibraryOnboardingWizard::Result result = wizard.result();
  QVERIFY(result.accepted);
  QCOMPARE(result.pickedConfig.name, QStringLiteral("Reference"));
  QCOMPARE(result.pickedConfig.mediaDirectory, media.path());
  QCOMPARE(result.pickedConfig.artworkDirectory, artwork.path());
  // The type combo always carries a current entry, so the config's type must
  // come back non-empty even when the user never visited the type page.
  QVERIFY(!result.pickedConfig.type.isEmpty());
}

// Kartend-0ydo7: the wizard's type presets must BE canon, not a copy of it.
// The hardcoded list they replaced had drifted in four of its five entries —
// "Reference"/"Image"/"Other" where canon says "Documents"/"Images", and no
// "Games" at all — so the one guided path into a new collection could not name
// the type most of the app's feature surface is built around.
void TestOnboardingWizards::onboarding_typeComboOffersCanonicalPresets() {
  LibraryOnboardingWizard wizard;
  wizard.restart();

  // Located by scan, not by page index, so reordering the pages cannot
  // silently point this at the wrong widget.
  QComboBox *typeCombo = nullptr;
  const QList<int> ids = wizard.pageIds();
  for (int id : ids) {
    if (auto *found = wizard.page(id)->findChild<QComboBox *>()) {
      typeCombo = found;
      break;
    }
  }
  QVERIFY(typeCombo);

  QStringList items;
  items.reserve(typeCombo->count());
  for (int i = 0; i < typeCombo->count(); ++i) {
    items << typeCombo->itemText(i);
  }
  QCOMPARE(items, CollectionUtils::standardCollectionTypes());
  QVERIFY(items.contains(QStringLiteral("Games")));

  // Editable, so a custom type is still reachable...
  QVERIFY(typeCombo->isEditable());
  // ...but the current text must never start out blank: collectionTypeChoices'
  // leading "untagged" row is for EDITING an existing collection, and the
  // Finish handler here takes the type straight off this combo for a user who
  // never opens the page (asserted by onboarding_finishBuildsConfigFromFields).
  QVERIFY(!typeCombo->currentText().isEmpty());
}

// Kartend-8cxjy: the Confirm page must echo every step, naming the ones left
// unset. It used to print name/folder/type only — dropping the artwork row
// whenever it was blank and omitting the launcher entirely, so the screen whose
// whole job is "review before it is created" under-reported a step's worth of
// what it had collected.
void TestOnboardingWizards::onboarding_summaryEchoesEveryStep() {
  QTemporaryDir media;
  QVERIFY(media.isValid());

  LibraryOnboardingWizard wizard;
  wizard.restart();
  wizard.setField(QStringLiteral("collectionName"), QStringLiteral("Shorts"));
  wizard.setField(QStringLiteral("mediaDirectory"), media.path());
  // Artwork deliberately left unset — that is the row the page used to drop.
  wizard.next(); // welcome -> media
  wizard.next(); // media  -> type
  wizard.next(); // type   -> launcher

  // Check the first selectable launcher when the host has one: LauncherProbe
  // reads the real PATH, so a runner with no known launcher gets the disabled
  // placeholder row instead and only the "(none selected)" half is asserted.
  QString expectedLauncher;
  if (auto *list = wizard.currentPage()->findChild<QListWidget *>()) {
    if (list->count() > 0 && (list->item(0)->flags() & Qt::ItemIsUserCheckable) != 0) {
      list->item(0)->setCheckState(Qt::Checked);
      expectedLauncher = list->item(0)->data(Qt::UserRole).toString();
    }
  }

  wizard.next(); // launcher -> summary
  auto *summary = wizard.currentPage()->findChild<QLabel *>();
  QVERIFY(summary);
  const QString text = summary->text();

  QVERIFY(text.contains(QStringLiteral("Shorts")));
  QVERIFY(text.contains(media.path()));
  // Both previously-missing rows are present as LABELS regardless of value.
  QVERIFY(text.contains(QStringLiteral("Artwork folder:")));
  QVERIFY(text.contains(QStringLiteral("Launcher:")));
  // Artwork was never set, so the row must SAY so rather than vanish.
  QVERIFY(text.contains(QStringLiteral("(none selected)")));
  if (!expectedLauncher.isEmpty()) {
    QVERIFY(text.contains(expectedLauncher));
  }
}

QTEST_MAIN(TestOnboardingWizards)
#include "test_onboardingwizards.moc"
