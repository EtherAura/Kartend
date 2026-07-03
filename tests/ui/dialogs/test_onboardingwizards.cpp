// First-run-path coverage: the two onboarding wizards every new install can
// hit. Driven headlessly (offscreen QPA, never exec()'d): page sequencing,
// the media-page Next gate (mandatory fields + directory-must-exist), and
// the Finish handlers that fold wizard fields into the Result the caller
// persists. The Browse buttons open native QFileDialogs and are not driven;
// fields are set through QWizard::setField exactly as the pages registered
// them.

#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QWizard>

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

QTEST_MAIN(TestOnboardingWizards)
#include "test_onboardingwizards.moc"
