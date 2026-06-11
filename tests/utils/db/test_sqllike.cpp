// KartendDb::escapeLike / kLikeEscape contract tests (Kartend-joird), plus a
// real-SQLite proof that the escaped subfolder-prefix pattern stops
// underscore over-matching (`my_videos/%` previously also matched
// `myXvideos/...` because _ is a single-char LIKE wildcard even when bound).

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "sqllike.h"

class TestSqlLike : public QObject {
  Q_OBJECT
private slots:
  void testEscapeLike_data();
  void testEscapeLike();
  void testSubfolderPrefixNoLongerOverMatches();
};

void TestSqlLike::testEscapeLike_data() {
  QTest::addColumn<QString>("in");
  QTest::addColumn<QString>("out");

  QTest::newRow("plain") << "abc" << "abc";
  QTest::newRow("underscore") << "my_videos" << "my\\_videos";
  QTest::newRow("percent") << "100%" << "100\\%";
  QTest::newRow("backslash") << "a\\b" << "a\\\\b";
  QTest::newRow("mixed") << "a_b%c\\d" << "a\\_b\\%c\\\\d";
  QTest::newRow("empty") << "" << "";
}

void TestSqlLike::testEscapeLike() {
  QFETCH(QString, in);
  QFETCH(QString, out);
  QCOMPARE(KartendDb::escapeLike(in), out);
}

void TestSqlLike::testSubfolderPrefixNoLongerOverMatches() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString conn = QStringLiteral("test_sqllike");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(QDir(tmp.path()).filePath(QStringLiteral("t.db")));
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE t (p TEXT)")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES "
                                  "('my_videos/a.mkv'), ('myXvideos/b.mkv'), "
                                  "('100%/c.mkv'), ('100p/d.mkv')")));

    const auto countFor = [&](const QString &subfolder) {
      QSqlQuery sel(db);
      sel.prepare(QStringLiteral("SELECT COUNT(*) FROM t WHERE p LIKE ?") + KartendDb::kLikeEscape);
      sel.addBindValue(KartendDb::escapeLike(subfolder) + QStringLiteral("/%"));
      if (!sel.exec() || !sel.next()) {
        return -1;
      }
      return sel.value(0).toInt();
    };

    // The underscore must match only itself, not 'X'.
    QCOMPARE(countFor(QStringLiteral("my_videos")), 1);
    // A literal % in a folder name must not match everything.
    QCOMPARE(countFor(QStringLiteral("100%")), 1);

    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

QTEST_GUILESS_MAIN(TestSqlLike)

#include "test_sqllike.moc"
