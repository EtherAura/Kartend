// Kartend-0eeuk: CreateCollectionDialog field validation + uuid/name rules.
// Covers the live OK gate on the name (path separators and `..` would
// become traversal segments via %collection% expansion), the type→scraper
// auto-association and its freeze on a manual pick, the conditional
// ScreenScraper-system / libretro-core rows (including the stale-value
// guards on screenscraperSystemId() / corePath()), accept()'s
// security-validation of the path fields (modal warning answered by a
// zero-interval timer), and getter trimming. QStandardPaths runs in test
// mode so the ScreenScraper system cache lookup stays inside the sandbox.
// Driven headlessly — the dialog is never shown or exec()'d.

#include "createcollectiondialog.h"

#include "metadataproviderregistry.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace {

QLineEdit *editWithToolTip(const QWidget &dlg, const QString &needle) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->toolTip().contains(needle)) {
      return e;
    }
  }
  return nullptr;
}

QComboBox *comboWithToolTip(const QWidget &dlg, const QString &needle) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->toolTip().contains(needle)) {
      return c;
    }
  }
  return nullptr;
}

QLineEdit *nameEdit(const QWidget &dlg) {
  return editWithToolTip(dlg, QStringLiteral("Display name"));
}

QLineEdit *contentEdit(const QWidget &dlg) {
  return editWithToolTip(dlg, QStringLiteral("content files"));
}

QLineEdit *launcherEdit(const QWidget &dlg) {
  return editWithToolTip(dlg, QStringLiteral("Executable that opens"));
}

QLineEdit *coreEdit(const QWidget &dlg) {
  return editWithToolTip(dlg, QStringLiteral("Libretro core"));
}

QComboBox *typeCombo(const QWidget &dlg) {
  return comboWithToolTip(dlg, QStringLiteral("Media category"));
}

QComboBox *scraperCombo(const QWidget &dlg) {
  return comboWithToolTip(dlg, QStringLiteral("Metadata scraper"));
}

QComboBox *systemCombo(const QWidget &dlg) {
  return comboWithToolTip(dlg, QStringLiteral("ScreenScraper.fr system"));
}

QPushButton *okButton(const QWidget &dlg) {
  auto *box = dlg.findChild<QDialogButtonBox *>();
  return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}

/// Simulate a user pick on a combo: setCurrentIndex never fires activated
/// (by design — that is what keeps the auto-association alive), so emit it
/// through the meta-object the way a real click would.
void userPick(QComboBox *combo, int index) {
  combo->setCurrentIndex(index);
  QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
}

/// Clicks Ok on the next modal QMessageBox (accept()'s invalid-path
/// warning), capturing its text first.
class WarningAnswerer : public QObject {
public:
  WarningAnswerer() {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!box) {
        return;
      }
      capturedText = box->text();
      triggered = true;
      m_timer.stop();
      if (QAbstractButton *b = box->button(QMessageBox::Ok)) {
        b->click();
      }
    });
    m_timer.start();
  }

  QString capturedText;
  bool triggered = false;

private:
  QTimer m_timer;
};

} // namespace

class TestCreateCollectionDialog : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void okGatesOnTraversalSafeName();
  void typeDrivesScraperUntilManualPickFreezesIt();
  void gamesTypeRevealsScreenscraperRowAndGatesSystemId();
  void corePathOnlyAppliesToLibretroLaunchers();
  void acceptRejectsShellMetacharacterPaths();
  void gettersTrimWhitespace();
};

void TestCreateCollectionDialog::initTestCase() {
  // Keep the ScreenScraper system-cache lookup (and anything else
  // QStandardPaths-routed) inside the per-test sandbox.
  QStandardPaths::setTestModeEnabled(true);
}

void TestCreateCollectionDialog::okGatesOnTraversalSafeName() {
  CreateCollectionDialog dlg;
  QPushButton *ok = okButton(dlg);
  QLineEdit *name = nameEdit(dlg);
  QVERIFY(ok && name);

  QVERIFY(!ok->isEnabled()); // empty name can never be accepted

  name->setText(QStringLiteral("My Videos"));
  QVERIFY(ok->isEnabled());

  // Separators / `..` would be substituted into launcher paths as a
  // traversal segment — gated live rather than rejected at launch.
  name->setText(QStringLiteral("a/b"));
  QVERIFY(!ok->isEnabled());
  name->setText(QStringLiteral("a\\b"));
  QVERIFY(!ok->isEnabled());
  name->setText(QStringLiteral(".."));
  QVERIFY(!ok->isEnabled());
  name->setText(QStringLiteral("   "));
  QVERIFY(!ok->isEnabled()); // whitespace-only trims to empty

  name->setText(QStringLiteral("Videos"));
  QVERIFY(ok->isEnabled()); // gate releases once the name is safe again
}

void TestCreateCollectionDialog::typeDrivesScraperUntilManualPickFreezesIt() {
  CreateCollectionDialog dlg;
  QComboBox *type = typeCombo(dlg);
  QComboBox *scraper = scraperCombo(dlg);
  QVERIFY(type && scraper);

  // Auto-association: the combo follows the type's default provider...
  type->setCurrentText(QStringLiteral("Video"));
  const QString videoDefault =
      MetadataProviderRegistry::defaultScraperForType(QStringLiteral("Video"));
  QVERIFY(!videoDefault.isEmpty());
  QCOMPARE(scraper->currentData().toString(), videoDefault);
  // ...but while merely tracking the type, the persisted id stays empty
  // ("Automatic") so a later type edit re-resolves the provider.
  QCOMPARE(dlg.scraperProviderId(), QString());

  // A deliberate pick freezes the association and is what gets persisted.
  const int ssIdx = scraper->findData(QStringLiteral("screenscraper"));
  QVERIFY(ssIdx >= 0);
  userPick(scraper, ssIdx);
  QCOMPARE(dlg.scraperProviderId(), QStringLiteral("screenscraper"));

  type->setCurrentText(QStringLiteral("Audio"));
  QCOMPARE(scraper->currentData().toString(), QStringLiteral("screenscraper"));
  QCOMPARE(dlg.scraperProviderId(), QStringLiteral("screenscraper"));
}

void TestCreateCollectionDialog::gamesTypeRevealsScreenscraperRowAndGatesSystemId() {
  CreateCollectionDialog dlg;
  QComboBox *type = typeCombo(dlg);
  QComboBox *system = systemCombo(dlg);
  QVERIFY(type && system);

  // Blank type → the system row stays hidden and the id is inert.
  QVERIFY(!system->isVisibleTo(&dlg));
  QCOMPARE(dlg.screenscraperSystemId(), -1);

  type->setCurrentText(QStringLiteral("Games"));
  QVERIFY(system->isVisibleTo(&dlg));
  QCOMPARE(dlg.screenscraperSystemId(), -1); // Auto-detect default

  // Synonym normalisation: "roms" counts as the games category too.
  type->setCurrentText(QStringLiteral("roms"));
  QVERIFY(system->isVisibleTo(&dlg));

  // Leaving the games category hides the row again AND must keep any stale
  // system id from leaking into the new collection's config.
  type->setCurrentText(QStringLiteral("Video"));
  QVERIFY(!system->isVisibleTo(&dlg));
  QCOMPARE(dlg.screenscraperSystemId(), -1);
}

void TestCreateCollectionDialog::corePathOnlyAppliesToLibretroLaunchers() {
  CreateCollectionDialog dlg;
  QLineEdit *launcher = launcherEdit(dlg);
  QLineEdit *core = coreEdit(dlg);
  QVERIFY(launcher && core);

  // Core row hidden + corePath() empty while the launcher isn't RetroArch,
  // even when the field holds a stale value.
  core->setText(QStringLiteral("/cores/foo_libretro.so"));
  QVERIFY(!core->isVisibleTo(&dlg));
  QCOMPARE(dlg.corePath(), QString());

  launcher->setText(QStringLiteral("/usr/bin/retroarch"));
  QVERIFY(core->isVisibleTo(&dlg));
  QCOMPARE(dlg.corePath(), QStringLiteral("/cores/foo_libretro.so"));

  // Only the executable basename qualifies — a directory merely named
  // "retroarch" must not reveal the row (Kartend-r7att).
  launcher->setText(QStringLiteral("/opt/retroarch-tools/player"));
  QVERIFY(!core->isVisibleTo(&dlg));
  QCOMPARE(dlg.corePath(), QString());
}

void TestCreateCollectionDialog::acceptRejectsShellMetacharacterPaths() {
  CreateCollectionDialog dlg;
  QLineEdit *name = nameEdit(dlg);
  QLineEdit *content = contentEdit(dlg);
  QVERIFY(name && content);
  name->setText(QStringLiteral("Videos"));

  QSignalSpy accepted(&dlg, &QDialog::accepted);
  content->setText(QStringLiteral("/media/library;rm -rf /"));
  {
    WarningAnswerer answer;
    dlg.accept();
    QVERIFY(answer.triggered); // warning shown, dialog stayed open
    QVERIFY(answer.capturedText.contains(QStringLiteral("Content Folder")));
  }
  QCOMPARE(accepted.count(), 0);

  // Fixing the offending field lets the same accept() through. Empty
  // optional fields pass validation untouched.
  content->setText(QStringLiteral("/media/library"));
  dlg.accept();
  QCOMPARE(accepted.count(), 1);
}

void TestCreateCollectionDialog::gettersTrimWhitespace() {
  CreateCollectionDialog dlg;
  nameEdit(dlg)->setText(QStringLiteral("  Concerts  "));
  contentEdit(dlg)->setText(QStringLiteral("  /media/concerts  "));
  typeCombo(dlg)->setCurrentText(QStringLiteral("  Video  "));

  QCOMPARE(dlg.collectionName(), QStringLiteral("Concerts"));
  QCOMPARE(dlg.contentPath(), QStringLiteral("/media/concerts"));
  QCOMPARE(dlg.collectionType(), QStringLiteral("Video"));
}

QTEST_MAIN(TestCreateCollectionDialog)
#include "test_createcollectiondialog.moc"
