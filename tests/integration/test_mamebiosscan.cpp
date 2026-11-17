// MAME BIOS/device-set scan compatibility (Kartend-tnt0l).
//
// The issue's acceptance says: "With the toggle off (the default), or with no
// MAME listxml attached, the item list is unchanged." This pins that half on
// the real scanner: an arcade folder holding BIOS, device, non-runnable,
// ordinary and unknown sets keeps every one of them as an item whenever the
// filter has nothing to act on — toggle off (untouched or explicit), or toggle
// on with no DAT, a non-MAME DAT, or a missing / unreadable DAT.
//
// The enabled case (toggle on with a MAME listxml attached) is deliberately
// NOT asserted here; it belongs to the round that implements the filter.
//
// Driven against a REAL on-disk SQLite database seeded with the production
// schema (the project forbids mocking the DB) — the same standalone
// ScanService wiring tests/modules/query/test_multidisccollapse.cpp uses.
#include <memory>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "databaseschema.h"
#include "itemmetadata.h"
#include "preparedstatementcache.h"
#include "scanservice.h"

namespace {

const QString kConnection = QStringLiteral("test_mamebiosscan");

// Real on-disk SQLite + the production schema wired to a standalone
// ScanService; per-instance temp dir so parallel test processes never share
// state. Mirrors CollapseFixture in test_multidisccollapse.cpp. Everything is
// created and destroyed on the test thread, handles before removeDatabase.
class MameScanFixture {
public:
  MameScanFixture() {
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnection);
    m_db.setDatabaseName(QDir(m_tmp.path()).filePath(QStringLiteral("scan.db")));
    m_opened = m_tmp.isValid() && m_db.open();
    if (m_opened) {
      DatabaseSchema::applyConnectionPragmas(m_db);
      DatabaseSchema::createTables(m_db);
      DatabaseSchema::createIndexes(m_db);
    }
    m_cache = std::make_unique<PreparedStatementCache>();
    m_cache->setDatabase(m_db);
    m_svc = std::make_unique<ScanService>(m_db, *m_cache, m_txnDepth);
  }
  ~MameScanFixture() {
    m_svc.reset();
    m_cache.reset();
    if (m_opened) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(kConnection);
  }
  MameScanFixture(const MameScanFixture &) = delete;
  MameScanFixture &operator=(const MameScanFixture &) = delete;

  ScanService *service() { return m_svc.get(); }
  [[nodiscard]] bool opened() const { return m_opened; }
  QSqlDatabase &db() { return m_db; }

private:
  QTemporaryDir m_tmp;
  QSqlDatabase m_db;
  bool m_opened = false;
  std::unique_ptr<PreparedStatementCache> m_cache;
  int m_txnDepth = 0;
  std::unique_ptr<ScanService> m_svc;
};

bool writeFile(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  const bool ok = f.write(bytes) == bytes.size();
  f.close();
  return ok;
}

QByteArray readFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

// The listxml marks one set of each kind the issue names: a BIOS, a device and
// a plain non-runnable machine, beside an ordinary game. "unknownset" is in the
// ROM folder but absent from the DAT.
const QByteArray kMameListXml =
    QByteArrayLiteral("<?xml version=\"1.0\"?>\n"
                      "<mame build=\"0.250\">\n"
                      "  <machine name=\"neogeo\" isbios=\"yes\" runnable=\"no\">"
                      "<description>Neo-Geo</description></machine>\n"
                      "  <machine name=\"z80\" isdevice=\"yes\" runnable=\"no\">"
                      "<description>Zilog Z80</description></machine>\n"
                      "  <machine name=\"norun\" runnable=\"no\">"
                      "<description>Not Runnable</description></machine>\n"
                      "  <machine name=\"puckman\"><description>PuckMan</description></machine>\n"
                      "</mame>\n");

const QByteArray kLogiqxDat =
    QByteArrayLiteral("<?xml version=\"1.0\"?>\n"
                      "<datafile>\n"
                      "  <header><name>Plain</name><description>Plain</description></header>\n"
                      "  <game name=\"neogeo\"><description>Neo-Geo</description></game>\n"
                      "  <game name=\"puckman\"><description>PuckMan</description></game>\n"
                      "</datafile>\n");

// Minimal archive bodies: an empty zip's end-of-central-directory record, and a
// 7z signature header. The scanner lists them; nothing extracts them.
const QByteArray kEmptyZip = QByteArray("PK\x05\x06", 4) + QByteArray(18, '\0');
const QByteArray k7zHeader = QByteArray("7z\xBC\xAF\x27\x1C", 6) + QByteArray(26, '\0');

// Every fixture set, as the item names a scan produces for them.
const QStringList kAllSets{QStringLiteral("neogeo"), QStringLiteral("norun"),
                           QStringLiteral("puckman"), QStringLiteral("unknownset"),
                           QStringLiteral("z80")};

enum class Dat { None, Mame, NonMame, Missing, Unreadable };

// Sorted item names of ONE collection. Fails the test (rather than returning
// an empty list) when the query itself cannot run.
void collectionItemNames(QSqlDatabase &db, const QString &uuid, QStringList *out) {
  QSqlQuery q(db);
  QVERIFY2(
      q.prepare(QStringLiteral("SELECT name FROM items WHERE collection_uuid = ? ORDER BY name")),
      qPrintable(q.lastError().text()));
  q.addBindValue(uuid);
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
  while (q.next()) out->append(q.value(0).toString());
}

void forceRescan(QSqlDatabase &db) {
  QSqlQuery q(db);
  QVERIFY2(q.prepare(QStringLiteral("UPDATE collections SET last_scanned = ?")),
           qPrintable(q.lastError().text()));
  q.addBindValue(QStringLiteral("2000-01-01T00:00:00"));
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

void seedHidden(QSqlDatabase &db, const QString &uuid, const QString &path) {
  ItemMetadataStore::ItemMetadata md;
  md.collectionUuid = uuid;
  md.path = path;
  md.isHidden = true;
  const auto saved = ItemMetadataStore::save(db, md);
  QVERIFY2(saved.isOk(), qPrintable(saved.error().message));
}

} // namespace

Q_DECLARE_METATYPE(Dat)

class TestMameBiosScan : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void filterInactive_keepsEveryFixtureSet_data();
  void filterInactive_keepsEveryFixtureSet();
};

void TestMameBiosScan::initTestCase() {
  // Keeps every AppDataLocation write inside the per-test sandbox.
  QStandardPaths::setTestModeEnabled(true);
}

void TestMameBiosScan::filterInactive_keepsEveryFixtureSet_data() {
  QTest::addColumn<Dat>("dat");
  QTest::addColumn<bool>("touchToggle");
  QTest::addColumn<bool>("hide");

  QTest::newRow("default-off, MAME listxml attached") << Dat::Mame << false << false;
  QTest::newRow("explicit-off, MAME listxml attached") << Dat::Mame << true << false;
  QTest::newRow("on, no DAT attached") << Dat::None << true << true;
  QTest::newRow("on, only a non-MAME DAT") << Dat::NonMame << true << true;
  QTest::newRow("on, attached DAT missing") << Dat::Missing << true << true;
  QTest::newRow("on, attached DAT unreadable") << Dat::Unreadable << true << true;
}

void TestMameBiosScan::filterInactive_keepsEveryFixtureSet() {
  QFETCH(Dat, dat);
  QFETCH(bool, touchToggle);
  QFETCH(bool, hide);

  MameScanFixture fx;
  QVERIFY2(fx.opened(), "failed to open + seed the test SQLite database");

  QTemporaryDir media;
  QTemporaryDir dats;
  QVERIFY(media.isValid());
  QVERIFY(dats.isValid());
  const QDir dir(media.path());
  QHash<QString, QByteArray> sources{
      {dir.filePath(QStringLiteral("neogeo.zip")), kEmptyZip},
      {dir.filePath(QStringLiteral("z80.zip")), kEmptyZip},
      {dir.filePath(QStringLiteral("norun.7z")), k7zHeader},
      {dir.filePath(QStringLiteral("puckman.zip")), kEmptyZip},
      {dir.filePath(QStringLiteral("unknownset.7z")), k7zHeader},
  };
  for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
    QVERIFY(writeFile(it.key(), it.value()));
  }

  CollectionConfig cfg;
  cfg.name = QStringLiteral("Arcade");
  cfg.mediaDirectory = media.path();
  cfg.extensions = QStringList{QStringLiteral("zip"), QStringLiteral("7z")};
  QVERIFY2(!cfg.scraperOverrides.hideMameBiosAndDeviceSets, "the toggle must default to off");
  if (touchToggle) cfg.scraperOverrides.hideMameBiosAndDeviceSets = hide;

  const QDir datDir(dats.path());
  switch (dat) {
  case Dat::None:
    break;
  case Dat::Mame: {
    const QString path = datDir.filePath(QStringLiteral("mame.xml"));
    QVERIFY(writeFile(path, kMameListXml));
    cfg.scraperOverrides.datFilePaths = QStringList{path};
    break;
  }
  case Dat::NonMame: {
    const QString path = datDir.filePath(QStringLiteral("plain.dat"));
    QVERIFY(writeFile(path, kLogiqxDat));
    cfg.scraperOverrides.datFilePaths = QStringList{path};
    break;
  }
  case Dat::Missing:
    cfg.scraperOverrides.datFilePaths =
        QStringList{datDir.filePath(QStringLiteral("does-not-exist.xml"))};
    break;
  case Dat::Unreadable: {
    const QString path = datDir.filePath(QStringLiteral("broken.xml"));
    QVERIFY(writeFile(path, QByteArrayLiteral("<mame><machine name=\"neogeo\" isbios=")));
    cfg.scraperOverrides.datFilePaths = QStringList{path};
    break;
  }
  }

  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
  const QString hiddenPath = dir.filePath(QStringLiteral("puckman.zip"));
  const QString visiblePath = dir.filePath(QStringLiteral("neogeo.zip"));
  seedHidden(fx.db(), uuid, hiddenPath);
  if (QTest::currentTestFailed()) return;

  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QStringList names;
  collectionItemNames(fx.db(), uuid, &names);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(names, kAllSets);

  // A forced rewalk lands on the same list: nothing is dropped later either.
  forceRescan(fx.db());
  if (QTest::currentTestFailed()) return;
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  names.clear();
  collectionItemNames(fx.db(), uuid, &names);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(names, kAllSets);

  // The scan reads the ROM folder; it never rewrites or removes a set.
  for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
    QVERIFY2(QFile::exists(it.key()), qPrintable(it.key()));
    QCOMPARE(readFile(it.key()), it.value());
  }

  // The user's own Hidden flag is untouched, and nothing else became hidden.
  const auto hidden = ItemMetadataStore::load(fx.db(), uuid, hiddenPath);
  QVERIFY2(hidden.isOk(), qPrintable(hidden.error().message));
  QVERIFY(hidden.value().isHidden);
  const auto visible = ItemMetadataStore::load(fx.db(), uuid, visiblePath);
  QVERIFY2(visible.isOk(), qPrintable(visible.error().message));
  QVERIFY(!visible.value().isHidden);
}

QTEST_MAIN(TestMameBiosScan)
#include "test_mamebiosscan.moc"
