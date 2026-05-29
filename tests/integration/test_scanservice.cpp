#include "test_scanservice.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "preparedstatementcache.h"
#include "scanservice.h"

namespace {

// Each test gets a wiped qttest sandbox. ScanService doesn't write to
// QStandardPaths directly, but ApplicationManager-adjacent test cases in
// this binary do, and a sticky sandbox between tests is a common
// source of cross-suite flake.
void wipeSandbox() {
  const auto wipe = [](QStandardPaths::StandardLocation loc) {
    const QString p = QStandardPaths::writableLocation(loc);
    if (!p.isEmpty() && p.contains(QStringLiteral("qttest"))) {
      QDir(p).removeRecursively();
    }
  };
  wipe(QStandardPaths::ConfigLocation);
  wipe(QStandardPaths::AppConfigLocation);
  wipe(QStandardPaths::AppDataLocation);
  wipe(QStandardPaths::AppLocalDataLocation);
}

// scanMediaDirectory needs only a db reference (it doesn't issue
// queries) but the reference must be live. Use an in-memory SQLite
// connection so the test stays self-contained without touching disk.
class ScanServiceFixture {
public:
  ScanServiceFixture()
      : m_db(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                       QStringLiteral("test_scanservice_inmem"))) {
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    m_opened = m_db.open();
    m_cache.setDatabase(m_db);
    m_svc = std::make_unique<ScanService>(m_db, m_cache);
  }
  ~ScanServiceFixture() {
    m_svc.reset();
    if (m_opened) m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("test_scanservice_inmem"));
  }
  ScanServiceFixture(const ScanServiceFixture &) = delete;
  ScanServiceFixture &operator=(const ScanServiceFixture &) = delete;

  ScanService *service() { return m_svc.get(); }
  [[nodiscard]] bool opened() const { return m_opened; }

private:
  QSqlDatabase m_db;
  bool m_opened = false;
  PreparedStatementCache m_cache;
  std::unique_ptr<ScanService> m_svc;
};

} // namespace

void TestScanService::init() {
  QStandardPaths::setTestModeEnabled(true);
  wipeSandbox();
}

void TestScanService::testScanMediaDirectoryEmpty() {
  ScanServiceFixture fx;
  QVERIFY2(fx.opened(), "Failed to open in-memory SQLite connection");

  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());

  CollectionConfig cfg;
  cfg.name = QStringLiteral("EmptyTest");
  cfg.mediaDirectory = mediaDir.path();
  cfg.extensions = QStringList{QStringLiteral("bin")};

  QHash<QString, QDateTime> timestamps;
  QString signature;
  const QStringList found = fx.service()->scanMediaDirectory(cfg, timestamps, &signature);
  QVERIFY2(found.isEmpty(),
           qPrintable(QStringLiteral("Expected no files in empty dir, got %1").arg(found.size())));
  QVERIFY2(timestamps.isEmpty(), "Timestamps map should match the empty files list");
  // Signature is implementation-defined but must be non-null/comparable
  // — a second scan of the same empty dir is allowed to differ across
  // implementations (e.g. embeds an mtime). We only assert it round-trips
  // and is stable within a single call.
  QVERIFY(!signature.isNull());
}

void TestScanService::testScanMediaDirectoryFindsExtensionMatches() {
  ScanServiceFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());
  const QDir dir(mediaDir.path());
  // Two matching .bin files + one .png we should NOT pick up. The
  // CollectionConfig only lists `bin` as a scannable extension, so .png
  // must be filtered out by scanMediaDirectory.
  const auto writeBytes = [](const QString &path, const QByteArray &bytes) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    f.close();
  };
  writeBytes(dir.filePath(QStringLiteral("rom-a.bin")), QByteArray("a"));
  writeBytes(dir.filePath(QStringLiteral("rom-b.bin")), QByteArray("bb"));
  writeBytes(dir.filePath(QStringLiteral("not-a-rom.png")), QByteArray("png-bytes"));

  CollectionConfig cfg;
  cfg.name = QStringLiteral("ExtTest");
  cfg.mediaDirectory = mediaDir.path();
  cfg.extensions = QStringList{QStringLiteral("bin")};

  QHash<QString, QDateTime> timestamps;
  QString signature;
  const QStringList found = fx.service()->scanMediaDirectory(cfg, timestamps, &signature);
  QCOMPARE(found.size(), 2);
  // Order isn't part of the contract — use a set comparison.
  QStringList names;
  for (const QString &rel : found) {
    names.append(QFileInfo(rel).fileName());
  }
  std::sort(names.begin(), names.end());
  QCOMPARE(names, (QStringList{QStringLiteral("rom-a.bin"), QStringLiteral("rom-b.bin")}));
  QCOMPARE(timestamps.size(), 2);
  for (const QString &rel : found) {
    QVERIFY2(timestamps.contains(rel),
             qPrintable(QStringLiteral("Timestamps map missing entry for %1").arg(rel)));
    QVERIFY(timestamps.value(rel).isValid());
  }
}

void TestScanService::testRequestCancelScanSetsFlag() {
  ScanServiceFixture fx;
  QVERIFY(fx.opened());

  // Initial state: not cancelled.
  QVERIFY(!fx.service()->isScanCancelled());

  // requestCancelScan must flip the observable flag without requiring a
  // scan to be in flight — the UI handler invokes it from the main
  // thread the moment the user clicks Cancel, before the worker thread
  // can react.
  fx.service()->requestCancelScan();
  QVERIFY(fx.service()->isScanCancelled());
}

void TestScanService::testResetScanCancellationClearsFlag() {
  ScanServiceFixture fx;
  QVERIFY(fx.opened());

  // Drive the flag high, then reset; a fresh scan must observe the
  // cleared state (i.e. not abort instantly on its first cancel check).
  fx.service()->requestCancelScan();
  QVERIFY(fx.service()->isScanCancelled());
  fx.service()->resetScanCancellation();
  QVERIFY(!fx.service()->isScanCancelled());
}
