// StatisticsDialog shell against a real on-disk SQLite database (no DB
// mocking — StatisticsService::gather opens its own read-only connection to
// the file the injected IDatabaseManager names, so the double only carries
// the path; a writer connection applies the migration chain and seeds rows
// exactly like test_statisticsservice.cpp). Covered: the async
// snapshot→trees rendering (per-item rows, collection-uuid→label mapping,
// navigate payload stash, by-collection aggregation, never-played summary,
// deleted-collection fallback label), the live history-enable toggle
// (in-memory flip + persisted write + hint visibility), and the Reset flow
// (confirm modal answered by a zero-interval driver, real SQL reset, tree
// refresh). The dialog is constructed but never exec()'d or shown.

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include "../../integration/mocks/mockdatabasemanager.h"
#include "../../integration/mocks/mocksettingsmanager.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "dbmigrations.h"
#include "historystore.h"
#include "statisticsdialog.h"
#include "usagestatsstore.h"

namespace {

/// IDatabaseManager double that names a real migrated db FILE. The dialog's
/// read path never touches this object beyond databaseFilePath();
/// reset/clear run real SQL on the writer connection so the follow-up
/// refresh re-gathers genuinely changed rows.
class SeededDbManager : public KartendTest::MockDatabaseManager {
public:
  QString path;
  QString writerConn;

  [[nodiscard]] QString databaseFilePath() const override { return path; }
  bool resetAllUsageStats() override {
    QSqlQuery q(QSqlDatabase::database(writerConn));
    // Same columns UsageStatsStore::resetAll clears.
    return q.exec(QStringLiteral(
        "UPDATE items SET play_count = 0, last_played = NULL, total_play_seconds = 0"));
  }
  bool clearHistory() override {
    QSqlQuery q(QSqlDatabase::database(writerConn));
    return q.exec(QStringLiteral("DELETE FROM launch_history"));
  }
};

/// Counting ISettingsManager double for the history toggle's persist path.
class CountingSettingsManager : public KartendTest::MockSettingsManager {
public:
  int saveCount = 0;
  GeneralSettings lastSaved;
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override {
    ++saveCount;
    lastSaved = settings;
    return ErrorUtils::Result<void>::success();
  }
};

/// Runs @p action on the next active modal dialog (zero-interval repeating
/// timer — same driver shape as test_launcherchooserdialog.cpp).
class ModalDriver : public QObject {
public:
  explicit ModalDriver(std::function<void(QDialog *)> action) : m_action(std::move(action)) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dlg) {
        return;
      }
      triggered = true;
      m_timer.stop();
      m_action(dlg);
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  std::function<void(QDialog *)> m_action;
  QTimer m_timer;
};

// Writer connection to a real on-disk file with the full migration chain —
// mirrors test_statisticsservice.cpp so gather()'s read-only connection sees
// the production schema.
QSqlDatabase openFileDb(const QString &connName, const QString &path) {
  auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
  db.setDatabaseName(path);
  if (!db.open()) {
    qWarning("Failed to open file db: %s", qPrintable(db.lastError().text()));
    return db;
  }
  QSqlQuery q(db);
  q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
  q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT NOT NULL)"));
  q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
                        "path TEXT NOT NULL, last_modified TEXT)"));
  DbMigrations::applySchemaMigrations(db, QStringLiteral("test"));
  return db;
}

void insertItem(QSqlDatabase &db, const QString &uuid, const QString &path, const QString &name) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT INTO items (collection_uuid, path, name, last_modified) "
                           "VALUES (?, ?, ?, '2026-01-01')"));
  q.addBindValue(uuid);
  q.addBindValue(path);
  q.addBindValue(name);
  QVERIFY(q.exec());
}

/// Locate one of the dialog's QTreeWidgets by its exact header-label row —
/// the dialog exposes no getters.
QTreeWidget *treeWithHeaders(const QWidget &root, const QStringList &headers) {
  const auto trees = root.findChildren<QTreeWidget *>();
  for (auto *tree : trees) {
    if (tree->columnCount() != headers.size()) {
      continue;
    }
    bool match = true;
    for (int i = 0; i < headers.size(); ++i) {
      if (tree->headerItem()->text(i) != headers.at(i)) {
        match = false;
        break;
      }
    }
    if (match) {
      return tree;
    }
  }
  return nullptr;
}

const QStringList kMostPlayedHeaders{"Item", "Collection", "Plays", "Time", "Last played"};
const QStringList kNeverPlayedHeaders{"Item", "Collection"};
const QStringList kByCollectionHeaders{"Collection", "Items", "Launches", "Time"};
const QStringList kHistoryHeaders{"Launched at", "Item", "Collection", "Path"};

} // namespace

class TestStatisticsDialog : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void rendersSeededStatsAcrossTrees();
  void staleCollectionUuidRendersDeletedLabel();
  void historyToggleFlipsSettingPersistsAndShowsHint();
  void resetFlowClearsStatsAfterConfirm();

private:
  QTemporaryDir m_dir;
  QSqlDatabase m_db;
  QString m_conn;
  SeededDbManager m_dbManager;
  QList<CollectionConfig> m_collections;
  QString m_uuid;
};

void TestStatisticsDialog::init() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // StatisticsDialog's constructor kicks off an async snapshot via
  // QtConcurrent::run + QFutureWatcher. QThreadPool orders the task hand-off
  // through Qt's internal (futex-based) QMutex, which TSan can't see as a
  // happens-before edge, so it reports the task-object allocation on the main
  // thread as racing the worker's read of it — the same Qt-internal
  // false-positive class the tsan suppressions file documents. Every slot here
  // constructs the dialog, so skip the whole suite under TSan; the dialog's
  // behaviour is covered in the Debug and Release+clang configs.
  QSKIP("StatisticsDialog's ctor async snapshot trips a Qt-internal QtConcurrent "
        "happens-before TSan can't observe; covered in non-TSan builds");
#endif
  QVERIFY(m_dir.isValid());
  m_conn = QStringLiteral("statsdialog_writer_%1").arg(QTest::currentTestFunction());
  const QString path = m_dir.filePath(QStringLiteral("%1.db").arg(QTest::currentTestFunction()));
  m_db = openFileDb(m_conn, path);
  QVERIFY(m_db.isOpen());
  m_dbManager.path = path;
  m_dbManager.writerConn = m_conn;

  // One real collection whose uuid matches the seeded rows, so the dialog's
  // uuid→label mapping resolves. The media dir must exist for the canonical
  // uuid computation's path expansion.
  CollectionConfig games;
  games.name = QStringLiteral("Games");
  games.mediaDirectory = m_dir.path();
  m_collections = {games};
  m_uuid = CollectionUtils::computeCollectionUuid(games);
  QVERIFY(!m_uuid.isEmpty());
}

void TestStatisticsDialog::cleanup() {
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_conn);
}

void TestStatisticsDialog::rendersSeededStatsAcrossTrees() {
  insertItem(m_db, m_uuid, QStringLiteral("/g/one"), QStringLiteral("Alpha"));
  insertItem(m_db, m_uuid, QStringLiteral("/g/two"), QStringLiteral("Beta"));
  insertItem(m_db, m_uuid, QStringLiteral("/g/three"), QStringLiteral("Gamma"));
  QVERIFY(UsageStatsStore::recordLaunch(m_db, m_uuid, QStringLiteral("/g/one")).isOk());
  QVERIFY(UsageStatsStore::recordLaunch(m_db, m_uuid, QStringLiteral("/g/one")).isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(m_db, m_uuid, QStringLiteral("/g/one"), 100).isOk());
  QVERIFY(UsageStatsStore::recordLaunch(m_db, m_uuid, QStringLiteral("/g/two")).isOk());
  QVERIFY(
      HistoryStore::recordLaunch(m_db, m_uuid, QStringLiteral("/g/one"), QStringLiteral("Alpha"))
          .isOk());
  QVERIFY(HistoryStore::recordLaunch(m_db, m_uuid, QStringLiteral("/g/two"), QStringLiteral("Beta"))
              .isOk());

  GeneralSettings settings;
  CountingSettingsManager sm;
  StatisticsDialog dlg(&m_dbManager, &m_collections, /*runtimeDetectionEnabled=*/true, &settings,
                       &sm);

  QTreeWidget *mostPlayed = treeWithHeaders(dlg, kMostPlayedHeaders);
  QTreeWidget *neverPlayed = treeWithHeaders(dlg, kNeverPlayedHeaders);
  QTreeWidget *byCollection = treeWithHeaders(dlg, kByCollectionHeaders);
  QTreeWidget *history = treeWithHeaders(dlg, kHistoryHeaders);
  QVERIFY(mostPlayed && neverPlayed && byCollection && history);

  // The stats load runs off-thread (QtConcurrent + watcher) — wait for the
  // snapshot to land.
  QTRY_COMPARE(mostPlayed->topLevelItemCount(), 2);

  // Sorted by plays, descending: Alpha (2) above Beta (1); the uuid resolves
  // to the collection's display name; the absolute path is stashed for the
  // navigate-on-double-click handler.
  QTreeWidgetItem *top = mostPlayed->topLevelItem(0);
  QCOMPARE(top->text(0), QStringLiteral("Alpha"));
  QCOMPARE(top->text(1), QStringLiteral("Games"));
  QCOMPARE(top->data(2, Qt::UserRole).toLongLong(), qint64(2));
  QCOMPARE(top->data(0, Qt::UserRole + 1).toString(), QStringLiteral("/g/one"));
  QCOMPARE(mostPlayed->topLevelItem(1)->text(0), QStringLiteral("Beta"));

  // Never played: exactly the unlaunched row, plus the exhaustive summary
  // wording (no "showing first N" clause when nothing was clipped).
  QCOMPARE(neverPlayed->topLevelItemCount(), 1);
  QCOMPARE(neverPlayed->topLevelItem(0)->text(0), QStringLiteral("Gamma"));
  bool sawSummary = false;
  const auto labels = dlg.findChildren<QLabel *>();
  for (const auto *l : labels) {
    if (l->text().contains(QStringLiteral("never been launched"))) {
      sawSummary = true;
      QVERIFY2(!l->text().contains(QStringLiteral("showing first")), qPrintable(l->text()));
    }
  }
  QVERIFY(sawSummary);

  // By-collection aggregation: one row carrying every item + launch.
  QCOMPARE(byCollection->topLevelItemCount(), 1);
  QCOMPARE(byCollection->topLevelItem(0)->text(0), QStringLiteral("Games"));
  QCOMPARE(byCollection->topLevelItem(0)->data(1, Qt::UserRole).toLongLong(), qint64(3));
  QCOMPARE(byCollection->topLevelItem(0)->data(2, Qt::UserRole).toLongLong(), qint64(3));
  QCOMPARE(byCollection->topLevelItem(0)->data(3, Qt::UserRole).toLongLong(), qint64(100));

  // History renders both entries with the navigate payload convention
  // (column 0 / UserRole+1 even though the visible path sits in column 3).
  QCOMPARE(history->topLevelItemCount(), 2);
  QCOMPARE(history->topLevelItem(0)->data(0, Qt::UserRole + 1).toString().left(3),
           QStringLiteral("/g/"));

  // Runtime detection is on → the caveat note stays hidden.
  bool sawVisibleRuntimeNote = false;
  for (const auto *l : labels) {
    if (l->text().contains(QStringLiteral("Runtime Detection")) && !l->isHidden()) {
      sawVisibleRuntimeNote = true;
    }
  }
  QVERIFY(!sawVisibleRuntimeNote);
}

void TestStatisticsDialog::staleCollectionUuidRendersDeletedLabel() {
  // Rows whose collection no longer exists must stay identifiable rather
  // than vanish — the label falls back to a truncated uuid.
  insertItem(m_db, QStringLiteral("gone-collection-uuid"), QStringLiteral("/x/one"),
             QStringLiteral("Orphan"));
  QVERIFY(UsageStatsStore::recordLaunch(m_db, QStringLiteral("gone-collection-uuid"),
                                        QStringLiteral("/x/one"))
              .isOk());

  GeneralSettings settings;
  CountingSettingsManager sm;
  StatisticsDialog dlg(&m_dbManager, &m_collections, true, &settings, &sm);
  QTreeWidget *byCollection = treeWithHeaders(dlg, kByCollectionHeaders);
  QVERIFY(byCollection);
  QTRY_COMPARE(byCollection->topLevelItemCount(), 1);
  QCOMPARE(byCollection->topLevelItem(0)->text(0), QStringLiteral("(deleted) gone-col"));
}

void TestStatisticsDialog::historyToggleFlipsSettingPersistsAndShowsHint() {
  GeneralSettings settings;
  settings.history.historyEnabled = true;
  CountingSettingsManager sm;
  StatisticsDialog dlg(&m_dbManager, &m_collections, true, &settings, &sm);

  QCheckBox *toggle = nullptr;
  const auto boxes = dlg.findChildren<QCheckBox *>();
  for (auto *b : boxes) {
    if (b->text() == QStringLiteral("Record launch history")) {
      toggle = b;
    }
  }
  QVERIFY(toggle);
  QVERIFY(toggle->isChecked()); // synced from the live setting during refresh
  QCOMPARE(sm.saveCount, 0);    // the sync itself must not write back

  QLabel *disabledNote = nullptr;
  const auto labels = dlg.findChildren<QLabel *>();
  for (auto *l : labels) {
    if (l->text().contains(QStringLiteral("History recording is currently off"))) {
      disabledNote = l;
    }
  }
  QVERIFY(disabledNote);
  QVERIFY(disabledNote->isHidden());

  // Unchecking flips the live setting, persists once, and reveals the hint.
  toggle->setChecked(false);
  QCOMPARE(settings.history.historyEnabled, false);
  QCOMPARE(sm.saveCount, 1);
  QCOMPARE(sm.lastSaved.history.historyEnabled, false);
  QVERIFY(!disabledNote->isHidden());

  // Re-enabling round-trips.
  toggle->setChecked(true);
  QCOMPARE(settings.history.historyEnabled, true);
  QCOMPARE(sm.saveCount, 2);
  QVERIFY(disabledNote->isHidden());
}

void TestStatisticsDialog::resetFlowClearsStatsAfterConfirm() {
  insertItem(m_db, m_uuid, QStringLiteral("/g/one"), QStringLiteral("Alpha"));
  QVERIFY(UsageStatsStore::recordLaunch(m_db, m_uuid, QStringLiteral("/g/one")).isOk());

  GeneralSettings settings;
  CountingSettingsManager sm;
  StatisticsDialog dlg(&m_dbManager, &m_collections, true, &settings, &sm);
  QTreeWidget *mostPlayed = treeWithHeaders(dlg, kMostPlayedHeaders);
  QVERIFY(mostPlayed);
  QTRY_COMPARE(mostPlayed->topLevelItemCount(), 1);

  QPushButton *reset = nullptr;
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (auto *b : buttons) {
    if (b->text().startsWith(QStringLiteral("Reset usage stats"))) {
      reset = b;
    }
  }
  QVERIFY(reset);

  // Confirm the destructive prompt; the double's resetAllUsageStats runs the
  // real UPDATE, and the follow-up refresh re-gathers from the file.
  ModalDriver driver([](QDialog *dlg) {
    auto *box = qobject_cast<QMessageBox *>(dlg);
    QVERIFY(box);
    box->button(QMessageBox::Yes)->click();
  });
  reset->click();
  QVERIFY(driver.triggered);
  QTRY_COMPARE(mostPlayed->topLevelItemCount(), 0);
}

QTEST_MAIN(TestStatisticsDialog)
#include "test_statisticsdialog.moc"
