// DatAuditAuditPage — the DAT Manager's "Audit" stacked page. Runs against a
// real sandboxed media.db (same harness as test_datauditprofilepanel.cpp:
// AppDataLocation redirected per-suite, production schema built through a
// throwaway setup connection — no DB mocking). Covered headlessly:
// construction (inputs/filter/run-control widget inventory + idle gating),
// and both openForCollection flows — the unlinked seed (unsaved working
// profile aimed at the collection, empty DAT paths dropped, silent
// "(unsaved)" combo row) and the linked-profile flow (combo selects the
// stored profile, then the caller's WORKING media-dir/DAT list overrides the
// saved derivation, Kartend-6wn0p). The actual audit run (off-thread hashing)
// and the modal CRUD/fix/export flows are runtime-verified, not driven here.

#include <QComboBox>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTest>

#include "../../support/testsandbox.h"
#include "databaseschema.h"
#include "datauditauditpage.h"
#include "datauditprofile.h"
#include "datauditprofilecontroller.h"
#include "datauditprofilepanel.h"
#include "datauditprofilestore.h"

using DatAuditProfile::Profile;

namespace {

QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

/// The DAT-file / scan-folder lists live inside titled group boxes built by
/// FormBuilders::makePathListGroup — locate each list through its group.
QListWidget *listInGroup(const QWidget &root, const QString &groupTitle) {
  const auto boxes = root.findChildren<QGroupBox *>();
  for (auto *b : boxes) {
    if (b->title() == groupTitle) {
      return b->findChild<QListWidget *>();
    }
  }
  return nullptr;
}

QStringList itemsOf(const QListWidget *list) {
  QStringList out;
  for (int i = 0; i < list->count(); ++i) {
    out << list->item(i)->text();
  }
  return out;
}

QLabel *linkedHint(const QWidget &root) {
  const auto labels = root.findChildren<QLabel *>();
  for (auto *l : labels) {
    if (l->text().startsWith(QStringLiteral("The scan folder and DAT files are seeded"))) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestDatAuditAuditPage : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void constructsWithIdleAuditControls();
  void openForCollectionSeedsUnsavedWorkingProfile();
  void openForCollectionAdoptsLinkedProfileAndPrefersWorkingInputs();
};

void TestDatAuditAuditPage::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-datauditauditpage"));
}

void TestDatAuditAuditPage::init() {
  // Build the production schema at the sandbox media.db through a throwaway
  // setup connection, then drop it so the store opens its own per-call
  // connections to the file (same preamble as test_datauditprofilepanel.cpp).
  const QString conn = QStringLiteral("setup_datauditauditpage");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

void TestDatAuditAuditPage::cleanup() {
  QFile::remove(appDataDir() + QStringLiteral("/media.db"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-wal"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-shm"));
}

void TestDatAuditAuditPage::constructsWithIdleAuditControls() {
  DatAuditProfileStore store;
  DatAuditAuditPage page(store);

  // Input lists exist inside their titled groups and start empty.
  QListWidget *dats = listInGroup(page, QStringLiteral("DAT files"));
  QListWidget *roots = listInGroup(page, QStringLiteral("Scan folders"));
  QVERIFY(dats && roots);
  QCOMPARE(dats->count(), 0);
  QCOMPARE(roots->count(), 0);

  // The filter combo carries the full status-preset set (All + the two
  // framing presets + seven statuses).
  auto *panel = page.findChild<DatAuditProfilePanel *>();
  QVERIFY(panel);
  QComboBox *filter = nullptr;
  const auto combos = page.findChildren<QComboBox *>();
  for (auto *c : combos) {
    if (!panel->isAncestorOf(c)) {
      filter = c;
    }
  }
  QVERIFY(filter);
  QCOMPARE(filter->count(), 10);
  QCOMPARE(filter->itemText(0), QStringLiteral("All"));

  // Idle run controls: Run enabled, Cancel not, progress hidden, linked hint
  // hidden until a collection-linked profile arrives.
  const auto findButton = [&page](const QString &text) -> QPushButton * {
    const auto buttons = page.findChildren<QPushButton *>();
    for (auto *b : buttons) {
      if (b->text() == text) {
        return b;
      }
    }
    return nullptr;
  };
  QVERIFY(findButton(QStringLiteral("Run audit")));
  QPushButton *cancel = findButton(QStringLiteral("Cancel"));
  QVERIFY(cancel);
  QVERIFY(!cancel->isEnabled());
  QVERIFY(linkedHint(page));
  QVERIFY(linkedHint(page)->isHidden());
}

void TestDatAuditAuditPage::openForCollectionSeedsUnsavedWorkingProfile() {
  DatAuditProfileStore store;
  DatAuditAuditPage page(store);

  // No profile links to this uuid → the page seeds an UNSAVED working
  // profile aimed at the collection: media dir becomes the scan root, blank
  // DAT paths are dropped, and the profile combo lands on the "(unsaved)"
  // row silently (announcing would clear the seed underneath us).
  page.openForCollection(QStringLiteral("uuid-unlinked"), QStringLiteral("Concert Films"),
                         QStringLiteral("/media/concerts"),
                         {QString(), QStringLiteral("/dats/concerts.dat")});

  QCOMPARE(itemsOf(listInGroup(page, QStringLiteral("Scan folders"))),
           QStringList{QStringLiteral("/media/concerts")});
  QCOMPARE(itemsOf(listInGroup(page, QStringLiteral("DAT files"))),
           QStringList{QStringLiteral("/dats/concerts.dat")});

  auto *panel = page.findChild<DatAuditProfilePanel *>();
  QComboBox *profileCombo = panel->findChild<QComboBox *>();
  QVERIFY(profileCombo);
  QCOMPARE(profileCombo->currentIndex(), 0); // "(unsaved)"
  QCOMPARE(profileCombo->currentData().toLongLong(), qint64(-1));

  // The seed carries the collection link, so the seeded-lists hint shows.
  QVERIFY(!linkedHint(page)->isHidden());
}

void TestDatAuditAuditPage::openForCollectionAdoptsLinkedProfileAndPrefersWorkingInputs() {
  DatAuditProfileStore store;

  // A stored profile already linked to the collection, with stale inputs.
  Profile stored;
  stored.name = QStringLiteral("Concerts audit");
  stored.collectionUuid = QStringLiteral("uuid-linked");
  stored.scanRoots = {QStringLiteral("/old/media")};
  DatAuditProfile::DatRef oldDat;
  oldDat.path = QStringLiteral("/old/catalogue.dat");
  stored.dats = {oldDat};
  {
    DatAuditProfileController controller(store);
    QVERIFY(controller.persist(stored).isOk());
    QVERIFY(stored.id > 0);
  }

  DatAuditAuditPage page(store);
  // The settings panel hands over its WORKING copy — fresher than the saved
  // derivation, so the on-screen lists must show the caller's inputs, not
  // the stored profile's (Kartend-6wn0p).
  page.openForCollection(QStringLiteral("uuid-linked"), QStringLiteral("Concert Films"),
                         QStringLiteral("/fresh/media"), {QStringLiteral("/fresh/catalogue.dat")});

  auto *panel = page.findChild<DatAuditProfilePanel *>();
  QComboBox *profileCombo = panel->findChild<QComboBox *>();
  QVERIFY(profileCombo);
  QCOMPARE(profileCombo->currentData().toLongLong(), stored.id);

  QCOMPARE(itemsOf(listInGroup(page, QStringLiteral("Scan folders"))),
           QStringList{QStringLiteral("/fresh/media")});
  QCOMPARE(itemsOf(listInGroup(page, QStringLiteral("DAT files"))),
           QStringList{QStringLiteral("/fresh/catalogue.dat")});
  QVERIFY(!linkedHint(page)->isHidden());
}

QTEST_MAIN(TestDatAuditAuditPage)
#include "test_datauditauditpage.moc"
