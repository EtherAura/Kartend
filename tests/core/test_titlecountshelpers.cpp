// Tests for TitleCountsHelpers::refreshTitleCounts — the window-title renderer
// extracted from MainWindow (Kartend-tu2hq, second standalone src/core test).
// Covers the early-exit guards that don't require a fully-wired manager graph:
// null host, missing DatabaseManager, and out-of-range collection index.

#include <QApplication>
#include <QLabel>
#include <QTest>
#include <QWidget>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "isessionmanager.h"
#include "mockdatabasemanager.h"
#include "titlecountshelpers.h"

namespace {

/// Minimal ISessionManager that reports one fixed recursive count for every
/// collection. refreshTitleCounts reads counts ONLY through
/// getCollectionCounts, so the rest of the interface can stay inert — without a
/// session manager at all the helper falls back to "…" and renders no count,
/// which is the branch that cannot exercise the Items noun.
class FixedCountSession : public ISessionManager {
public:
  explicit FixedCountSession(qint64 recursive) : m_recursive(recursive) {}

  bool getCollectionCounts(const CollectionConfig &, const QList<CollectionConfig> &,
                           qint64 &itemCount, qint64 &recursiveCount) const override {
    itemCount = m_recursive;
    recursiveCount = m_recursive;
    return true;
  }

  void initialize() override {}
  void saveToDisk() override {}
  void saveToDiskForShutdown() override {}
  [[nodiscard]] QByteArray snapshotSessionJsonBytesForShutdown() const override { return {}; }
  void setLastSelected(const QString &, int, const QString &) override {}
  [[nodiscard]] int getLastSelectedIndex(const QString &) const override { return -1; }
  [[nodiscard]] qint64 getGlobalItemCount() const override { return m_recursive; }
  void setGlobalItemCount(qint64) override {}
  void setCollectionCounts(const CollectionConfig &, const QList<CollectionConfig> &, qint64,
                           qint64) override {}
  void setCachedViewport(const QString &, int, int, const QStringList &,
                         const QHash<QString, QString> &,
                         const QHash<QString, QString> &) override {}
  [[nodiscard]] CachedViewport getCachedViewport(const QString &) const override { return {}; }
  void clearStaleCollections(const QList<CollectionConfig> &) override {}
  void setCollectionTreeCollapsedKeys(const QStringList &) override {}
  [[nodiscard]] QStringList collectionTreeCollapsedKeys() const override { return {}; }

private:
  qint64 m_recursive;
};

} // namespace

class TestTitleCountsHelpers : public QObject {
  Q_OBJECT
private slots:
  void nullHostIsNoOp();
  void noDatabaseManagerIsNoOp();
  void outOfRangeIndexResetsToApplicationName();
  void subcollectionSuffixAgreesInNumber();
  void itemCountAgreesInNumber_data();
  void itemCountAgreesInNumber();
};

void TestTitleCountsHelpers::nullHostIsNoOp() {
  // No title host → must return without dereferencing anything.
  ApplicationContext ctx;
  QList<CollectionConfig> collections;
  TitleCountsHelpers::refreshTitleCounts(nullptr, ctx, collections, 0, nullptr);
  QVERIFY(true); // reaching here (no crash) is the assertion
}

void TestTitleCountsHelpers::noDatabaseManagerIsNoOp() {
  // Without a DatabaseManager the helper bails before touching the title, so a
  // host title set by the caller is left untouched.
  QWidget host;
  host.setWindowTitle(QStringLiteral("untouched"));
  ApplicationContext ctx; // ctx.databaseManager() == nullptr
  QList<CollectionConfig> collections;
  collections.append(CollectionConfig{});
  TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
  QCOMPARE(host.windowTitle(), QStringLiteral("untouched"));
}

void TestTitleCountsHelpers::outOfRangeIndexResetsToApplicationName() {
  // DatabaseManager present (so the helper proceeds past the early guard) but
  // the current index is out of range → the title resets to the application
  // name rather than indexing past the end of the collection list.
  QWidget host;
  host.setWindowTitle(QStringLiteral("stale"));
  KartendTest::MockDatabaseManager db;
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;
  QList<CollectionConfig> collections; // empty → index 0 is out of range
  TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
  QCOMPARE(host.windowTitle(), qApp->applicationName());
}

// Kartend-0ylim: "Films (19/54 Items) - 1 subcollections". The child-part
// suffix is reachable without a session manager — directChildrenOf reads the
// collection list alone — so this pins the noun at n == 1 and n > 1.
void TestTitleCountsHelpers::subcollectionSuffixAgreesInNumber() {
  KartendTest::MockDatabaseManager db;
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;

  CollectionConfig parent;
  parent.name = QStringLiteral("Films");
  CollectionConfig childA;
  childA.name = QStringLiteral("Shorts");
  childA.parentCollectionIndex = 0;
  childA.isSubcollection = true;

  QWidget host;

  // Exactly one child — the reported case.
  {
    QList<CollectionConfig> collections{parent, childA};
    TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
    QVERIFY2(host.windowTitle().contains(QStringLiteral("1 subcollection")),
             qPrintable(host.windowTitle()));
    QVERIFY2(
        !host.windowTitle().contains(QStringLiteral("1 subcollections")),
        qPrintable(
            QStringLiteral("n==1 must not read '1 subcollections': %1").arg(host.windowTitle())));
  }

  // Two children — plural must survive the fix.
  {
    CollectionConfig childB;
    childB.name = QStringLiteral("Features");
    childB.parentCollectionIndex = 0;
    childB.isSubcollection = true;
    QList<CollectionConfig> collections{parent, childA, childB};
    TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
    QVERIFY2(host.windowTitle().contains(QStringLiteral("2 subcollections")),
             qPrintable(host.windowTitle()));
  }
}

// The other half of the same report, confirmed by the reporter: a freshly
// created collection holding one file titled "240p Test Suite (1 Items)".
void TestTitleCountsHelpers::itemCountAgreesInNumber_data() {
  QTest::addColumn<qint64>("count");
  QTest::addColumn<QString>("expected");

  QTest::newRow("one") << qint64(1) << QStringLiteral("(1 Item)");
  QTest::newRow("zero") << qint64(0) << QStringLiteral("(0 Items)");
  QTest::newRow("many") << qint64(54) << QStringLiteral("(54 Items)");
}

void TestTitleCountsHelpers::itemCountAgreesInNumber() {
  QFETCH(qint64, count);
  QFETCH(QString, expected);

  KartendTest::MockDatabaseManager db;
  FixedCountSession session(count);
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;
  ctx.managers.sessionManager = &session;

  // A single root collection with no children: one number on show, so it
  // governs the noun, and no child-part suffix muddies the assertion.
  CollectionConfig solo;
  solo.name = QStringLiteral("240p Test Suite");
  QList<CollectionConfig> collections{solo};

  QWidget host;
  TitleCountsHelpers::refreshTitleCounts(&host, ctx, collections, 0, nullptr);
  QCOMPARE(host.windowTitle(), QStringLiteral("240p Test Suite %1").arg(expected));
}

QTEST_MAIN(TestTitleCountsHelpers)
#include "test_titlecountshelpers.moc"
