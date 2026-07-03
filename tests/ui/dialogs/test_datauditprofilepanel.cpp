// Headless test for DatAuditProfilePanel — the saved-profile combo + CRUD row
// lifted off DatAuditAuditPage. The panel is constructed against a real
// sandboxed media.db (same harness as test_datauditprofilecontroller.cpp: the
// store derives its path from AppDataLocation, redirected to a per-test
// sandbox, and a setup connection builds the production schema — no DB
// mocking). Covered here: combo population, the selection flow's announced
// signals (profileChanged / unsavedSelected), the silent "(unsaved)" select
// used by openForCollection, and the shared persist helper. The CRUD button
// flows open modal dialogs, so they are exercised interactively, not here.
// QTEST_MAIN (real QApplication) is required — the panel is a QWidget.

#include <QComboBox>
#include <QFile>
#include <QPushButton>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTest>

#include "../../support/testsandbox.h"
#include "databaseschema.h"
#include "datauditprofile.h"
#include "datauditprofilecontroller.h"
#include "datauditprofilepanel.h"
#include "datauditprofilestore.h"

using DatAuditProfile::Profile;

namespace {
QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

qint64 insertProfile(DatAuditProfileStore &store, const QString &name) {
  Profile p;
  p.name = name;
  p.scanRoots = {QStringLiteral("/media/") + name};
  DatAuditProfileController controller(store);
  auto res = controller.persist(p);
  return res.isOk() ? p.id : -1;
}
} // namespace

class TestDatAuditProfilePanel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void constructsWithUnsavedRowOnly();
  void announcesSelectedProfile();
  void unsavedRowAnnouncesWithoutProfile();
  void selectUnsavedSilentlyStaysQuiet();
  void persistProfileInsertsAndAssignsId();
};

void TestDatAuditProfilePanel::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-datauditprofilepanel"));
}

void TestDatAuditProfilePanel::init() {
  // Build the production schema at the sandbox media.db through a throwaway
  // setup connection (the same DatabaseSchema::createTables path the app runs),
  // then drop it so the store opens its own per-call connections to the file.
  const QString conn = QStringLiteral("setup_datauditprofilepanel");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

void TestDatAuditProfilePanel::cleanup() {
  QFile::remove(appDataDir() + QStringLiteral("/media.db"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-wal"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-shm"));
}

void TestDatAuditProfilePanel::constructsWithUnsavedRowOnly() {
  DatAuditProfileStore store;
  DatAuditProfilePanel panel(store);

  // The ctor loads the combo itself: "(unsaved)" first, nothing else on an
  // empty store, and the five CRUD buttons exist.
  const auto combos = panel.findChildren<QComboBox *>();
  QCOMPARE(combos.size(), 1);
  QCOMPARE(combos.first()->count(), 1);
  QCOMPARE(combos.first()->itemData(0).toLongLong(), qint64(-1));
  QCOMPARE(panel.findChildren<QPushButton *>().size(), 5);
}

void TestDatAuditProfilePanel::announcesSelectedProfile() {
  DatAuditProfileStore store;
  const qint64 idA = insertProfile(store, QStringLiteral("Alpha"));
  const qint64 idB = insertProfile(store, QStringLiteral("Beta"));
  QVERIFY(idA > 0);
  QVERIFY(idB > 0);

  DatAuditProfilePanel panel(store);
  QCOMPARE(panel.findChildren<QComboBox *>().first()->count(), 3);

  // Profile is not a registered metatype, so observe via a captured lambda
  // instead of QSignalSpy.
  int changed = 0;
  int unsaved = 0;
  Profile announced;
  connect(&panel, &DatAuditProfilePanel::profileChanged, &panel, [&](const Profile &p) {
    announced = p;
    ++changed;
  });
  connect(&panel, &DatAuditProfilePanel::unsavedSelected, &panel, [&] { ++unsaved; });

  panel.selectProfileById(idB);
  QCOMPARE(changed, 1);
  QCOMPARE(unsaved, 0);
  QCOMPARE(announced.id, idB);
  QCOMPARE(announced.name, QStringLiteral("Beta"));
  QCOMPARE(announced.scanRoots, QStringList{QStringLiteral("/media/Beta")});

  // Selecting an id the combo doesn't know is a no-op — no announcement.
  panel.selectProfileById(9999);
  QCOMPARE(changed, 1);
}

void TestDatAuditProfilePanel::unsavedRowAnnouncesWithoutProfile() {
  DatAuditProfileStore store;
  const qint64 id = insertProfile(store, QStringLiteral("Gamma"));
  QVERIFY(id > 0);

  DatAuditProfilePanel panel(store);
  int changed = 0;
  int unsaved = 0;
  connect(&panel, &DatAuditProfilePanel::profileChanged, &panel,
          [&](const Profile &) { ++changed; });
  connect(&panel, &DatAuditProfilePanel::unsavedSelected, &panel, [&] { ++unsaved; });

  panel.selectProfileById(id);
  QCOMPARE(changed, 1);

  // Back to the "(unsaved)" row: announced as unsavedSelected only, so the
  // page keeps its ad-hoc lists instead of adopting a loaded profile.
  panel.selectProfileById(-1);
  QCOMPARE(changed, 1);
  QCOMPARE(unsaved, 1);
}

void TestDatAuditProfilePanel::selectUnsavedSilentlyStaysQuiet() {
  DatAuditProfileStore store;
  const qint64 id = insertProfile(store, QStringLiteral("Delta"));
  QVERIFY(id > 0);

  DatAuditProfilePanel panel(store);
  int changed = 0;
  int unsaved = 0;
  connect(&panel, &DatAuditProfilePanel::profileChanged, &panel,
          [&](const Profile &) { ++changed; });
  connect(&panel, &DatAuditProfilePanel::unsavedSelected, &panel, [&] { ++unsaved; });

  panel.selectProfileById(id);
  QCOMPARE(changed, 1);

  // openForCollection's seed path: land on "(unsaved)" without announcing, so
  // the page's just-seeded working profile is not cleared underneath it.
  panel.selectUnsavedSilently();
  QCOMPARE(changed, 1);
  QCOMPARE(unsaved, 0);
  QCOMPARE(panel.findChildren<QComboBox *>().first()->currentIndex(), 0);

  // The silent select must not wedge the flow: the next real selection still
  // announces (the row genuinely changes, so the combo signal fires again).
  panel.selectProfileById(id);
  QCOMPARE(changed, 2);
}

void TestDatAuditProfilePanel::persistProfileInsertsAndAssignsId() {
  DatAuditProfileStore store;
  DatAuditProfilePanel panel(store);

  Profile p;
  p.name = QStringLiteral("Persisted");
  QCOMPARE(p.id, qint64(-1)); // "(unsaved)" — persist should INSERT
  QVERIFY(panel.persistProfile(p));
  QVERIFY(p.id > 0); // id assigned back into the passed-in profile

  panel.reloadProfiles();
  QComboBox *combo = panel.findChildren<QComboBox *>().first();
  QCOMPARE(combo->count(), 2);
  QCOMPARE(combo->itemData(1).toLongLong(), p.id);
  QCOMPARE(combo->itemText(1), QStringLiteral("Persisted"));
}

QTEST_MAIN(TestDatAuditProfilePanel)
#include "test_datauditprofilepanel.moc"
