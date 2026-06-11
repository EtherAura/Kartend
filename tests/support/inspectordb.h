#ifndef KARTEND_TESTS_SUPPORT_INSPECTORDB_H
#define KARTEND_TESTS_SUPPORT_INSPECTORDB_H

// Kartend-9o2a9: RAII inspector connection onto a test's real SQLite file.
//
// The integration-style suites assert on persisted rows by opening a second,
// read-mostly connection onto the database the production code writes
// (docs/dev/testing.md: no DB mocking — assertions go through real SQLite).
// Each suite used to hand-roll the addDatabase/contains/removeDatabase dance
// with slightly different connection-name handling and no teardown; this
// helper centralises it and removes the named connection when the guard
// leaves scope, so reruns and sibling tests never trip Qt's "duplicate
// connection name" warning.
//
// NOTE: every QSqlQuery built on db() must be destroyed before this guard
// (normal reverse destruction order of locals already guarantees that),
// otherwise QSqlDatabase::removeDatabase warns about a connection in use.

#include <QSqlDatabase>
#include <QString>

namespace KartendTest {

class InspectorDb {
public:
  InspectorDb(const QString &dbFilePath, const QString &connectionName)
      : m_connectionName(connectionName) {
    if (QSqlDatabase::contains(connectionName)) {
      QSqlDatabase::removeDatabase(connectionName);
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    m_db.setDatabaseName(dbFilePath);
    if (!m_db.open()) {
      // Leave m_db invalid; callers assert via isOpen(). The named
      // connection is still cleaned up by the destructor.
      m_db = QSqlDatabase();
    }
  }

  ~InspectorDb() {
    if (m_db.isOpen()) {
      m_db.close();
    }
    // Drop our handle before removeDatabase so Qt doesn't warn about an
    // outstanding reference to the connection.
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
  }

  InspectorDb(const InspectorDb &) = delete;
  InspectorDb &operator=(const InspectorDb &) = delete;

  /// True when the connection opened successfully.
  [[nodiscard]] auto isOpen() const -> bool { return m_db.isValid() && m_db.isOpen(); }

  /// Handle for building QSqlQuery objects. Copying the handle is cheap;
  /// the underlying connection stays owned by this guard.
  [[nodiscard]] auto db() const -> QSqlDatabase { return m_db; }

private:
  QString m_connectionName;
  QSqlDatabase m_db;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_SUPPORT_INSPECTORDB_H
