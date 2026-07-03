#ifndef KARTENDTEST_MIGRATEDDB_H
#define KARTENDTEST_MIGRATEDDB_H

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QString>
#include <QTemporaryDir>

#include "databaseschema.h"

namespace KartendTest {

/// One-liner "open a migrated, sandboxed SQLite" fixture for tests.
///
/// The project's no-DB-mocking rule means suites exercise real SQLite — but
/// ~27 files used to hand-roll the same addDatabase + unique-connection-name
/// + schema-bootstrap dance, so a schema change fanned out across all of
/// them. This RAII type runs the exact production bootstrap
/// (DatabaseSchema::openConnection/applyConnectionPragmas/createTables —
/// which applies DbMigrations internally — plus createIndexes) against a
/// private QTemporaryDir, or against `:memory:` for suites that never reopen
/// the file:
///
///   KartendTest::MigratedDb db;                       // on-disk, migrated
///   QSqlQuery q(db.database());
///   ... or ...
///   KartendTest::MigratedDb db(KartendTest::MigratedDb::InMemory);
///
/// Each instance owns a unique connection name (pid + counter, safe under
/// ctest -j and multiple instances per slot) and removes the connection on
/// destruction. Requires linking kartend_data (DatabaseSchema lives in the
/// database module).
class MigratedDb {
public:
  enum Backing { OnDisk, InMemory };

  explicit MigratedDb(Backing backing = OnDisk) : m_backing(backing) {
    static QAtomicInteger<quint64> counter{0};
    m_connectionName = QStringLiteral("kartend_test_migrated_%1_%2")
                           .arg(QCoreApplication::applicationPid())
                           .arg(counter.fetchAndAddRelaxed(1));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    if (backing == InMemory) {
      db.setDatabaseName(QStringLiteral(":memory:"));
      m_ok = db.open();
      if (m_ok) {
        DatabaseSchema::applyConnectionPragmas(db);
        DatabaseSchema::createTables(db); // runs DbMigrations internally
        DatabaseSchema::createIndexes(db);
      }
      return;
    }
    m_ok = m_dir.isValid();
    if (!m_ok) return;
    // openConnection derives `<dir>/media.db` — the same path shape the
    // production DatabaseManager opens under AppDataLocation.
    m_ok = DatabaseSchema::openConnection(db, m_dir.path());
    if (!m_ok) return;
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db); // runs DbMigrations internally
    DatabaseSchema::createIndexes(db);
  }

  ~MigratedDb() {
    {
      QSqlDatabase db = QSqlDatabase::database(m_connectionName, /*open=*/false);
      if (db.isValid()) db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
  }

  MigratedDb(const MigratedDb &) = delete;
  MigratedDb &operator=(const MigratedDb &) = delete;

  /// False when the temp dir or the open failed — assert this first.
  [[nodiscard]] bool isOpen() const { return m_ok; }

  /// The live connection. Grab fresh per use; safe to copy (QSqlDatabase is
  /// a handle onto the named connection).
  [[nodiscard]] QSqlDatabase database() const { return QSqlDatabase::database(m_connectionName); }

  [[nodiscard]] QString connectionName() const { return m_connectionName; }

  /// On-disk variant only: the media.db path (for code under test that opens
  /// its own connection to a FILE, e.g. worker-thread services). Empty for
  /// InMemory.
  [[nodiscard]] QString databasePath() const {
    if (m_backing == InMemory || !m_dir.isValid()) return {};
    return m_dir.filePath(QStringLiteral("media.db"));
  }

private:
  Backing m_backing = OnDisk;
  QString m_connectionName;
  QTemporaryDir m_dir;
  bool m_ok = false;
};

} // namespace KartendTest

#endif // KARTENDTEST_MIGRATEDDB_H
