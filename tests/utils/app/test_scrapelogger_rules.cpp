// Tests for Kartend-37t5r: toggling ScrapeLogger must not discard the startup
// diagnostic rules (KARTEND_LOG_RULES and the KARTEND_PERF_TRACE /
// KARTEND_SEARCH_DIAG / KARTEND_SCAN_DIAG bridges) that main() applies through
// LoggingRules::applyStartupRules(). Uses the real production categories and
// the real startup + toggle rule path. Qt reads QT_LOGGING_RULES before main()
// runs, so the CTest registration pins it (tests/cmake/05-dataudit-kart-and-db.cmake)
// to check its precedence too.

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTest>

#include "../../support/testsandbox.h"
#include "loggingcategories.h"
#include "loggingrules.h"
#include "scrapelogger.h"

// A scrape-prefixed category (driven by the toggle) and two categories pinned
// off by the QT_LOGGING_RULES the CTest registration sets.
Q_LOGGING_CATEGORY(lcScrapeRulesTest, "kartend.scrape.rulestest", QtWarningMsg)
Q_LOGGING_CATEGORY(lcScrapeRulesPinned, "kartend.scrape.rulestest.pinned")
Q_LOGGING_CATEGORY(lcRulesPinned, "kartend.rulestest.pinned")

namespace {

constexpr char kPinnedEnvRules[] = "*rulestest.pinned.debug=false";

const char *const kStartupEnvVars[] = {"KARTEND_LOG_RULES", "KARTEND_PERF_TRACE",
                                       "KARTEND_SEARCH_DIAG", "KARTEND_SCAN_DIAG"};

const QLoggingCategory &productionCategory(const QByteArray &name) {
  if (name == "kartend.perftrace") return lcPerfTrace();
  if (name == "kartend.searchdiag") return lcSearchDiag();
  return lcScanFlow();
}

void clearStartupEnv() {
  for (const char *var : kStartupEnvVars) {
    qunsetenv(var);
  }
}

} // namespace

class TestScrapeLoggerRules : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();
  void defaultsStayOffAcrossToggle();
  void startupRuleSurvivesScrapeToggle_data();
  void startupRuleSurvivesScrapeToggle();
  void scrapeToggleKeepsFinalSayOnScrapeCategories();
  void qtLoggingRulesStillOverride();

private:
  QList<QPair<QByteArray, QByteArray>> m_savedEnv;
};

void TestScrapeLoggerRules::initTestCase() {
  // Test-scoped config dir so we never touch the real scrape.log.
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scrapelogger-rules"));
  QVERIFY2(qgetenv("QT_LOGGING_RULES") == kPinnedEnvRules,
           "run through ctest: QT_LOGGING_RULES must be pinned by the test registration");
  // Isolate from the caller's environment; restored in cleanupTestCase().
  for (const char *var : kStartupEnvVars) {
    if (qEnvironmentVariableIsSet(var)) m_savedEnv.append({QByteArray(var), qgetenv(var)});
  }
  clearStartupEnv();
  LoggingRules::applyStartupRules();
}

void TestScrapeLoggerRules::cleanupTestCase() {
  clearStartupEnv();
  for (const auto &entry : m_savedEnv) {
    qputenv(entry.first.constData(), entry.second);
  }
}

void TestScrapeLoggerRules::cleanup() {
  // Back to: logging off, no startup env, rules re-applied from clean env.
  ScrapeLogger::setEnabled(false);
  clearStartupEnv();
  LoggingRules::applyStartupRules();
  QFile::remove(ScrapeLogger::logFilePath());
  QFile::remove(ScrapeLogger::logFilePath() + QStringLiteral(".old"));
}

void TestScrapeLoggerRules::defaultsStayOffAcrossToggle() {
  // Without any startup env the toggle must not broaden default logging.
  LoggingRules::applyStartupRules();
  const bool toggles[] = {true, false, true};
  for (bool on : toggles) {
    ScrapeLogger::setEnabled(on);
    QCOMPARE(ScrapeLogger::isEnabled(), on);
    QVERIFY(!lcPerfTrace().isDebugEnabled());
    QVERIFY(!lcSearchDiag().isDebugEnabled());
    // kartend.scanflow is compiled in at QtDebugMsg (loggingcategories.cpp),
    // so its default is on and must stay so.
    QVERIFY(lcScanFlow().isDebugEnabled());
    QVERIFY(!lcRulesPinned().isDebugEnabled());
    QCOMPARE(lcScrapeRulesTest().isDebugEnabled(), on);
    QCOMPARE(lcScrapeRulesTest().isInfoEnabled(), on);
    QVERIFY(lcScrapeRulesTest().isWarningEnabled());
  }
}

void TestScrapeLoggerRules::startupRuleSurvivesScrapeToggle_data() {
  QTest::addColumn<QByteArray>("envVar");
  QTest::addColumn<QByteArray>("envValue");
  QTest::addColumn<QByteArray>("category");
  QTest::newRow("KARTEND_LOG_RULES")
      << QByteArray("KARTEND_LOG_RULES") << QByteArray("kartend.perftrace.debug=true")
      << QByteArray("kartend.perftrace");
  QTest::newRow("KARTEND_PERF_TRACE")
      << QByteArray("KARTEND_PERF_TRACE") << QByteArray("1") << QByteArray("kartend.perftrace");
  QTest::newRow("KARTEND_SEARCH_DIAG")
      << QByteArray("KARTEND_SEARCH_DIAG") << QByteArray("1") << QByteArray("kartend.searchdiag");
  QTest::newRow("KARTEND_SCAN_DIAG")
      << QByteArray("KARTEND_SCAN_DIAG") << QByteArray("1") << QByteArray("kartend.scanflow");
}

void TestScrapeLoggerRules::startupRuleSurvivesScrapeToggle() {
  QFETCH(QByteArray, envVar);
  QFETCH(QByteArray, envValue);
  QFETCH(QByteArray, category);
  qputenv(envVar.constData(), envValue);
  LoggingRules::applyStartupRules();
  const QLoggingCategory &cat = productionCategory(category);
  QCOMPARE(QByteArray(cat.categoryName()), category);
  QVERIFY(cat.isDebugEnabled());

  // Enable, disable, enable again: the startup rule must hold throughout
  // while the toggle still drives its own categories.
  const bool toggles[] = {true, false, true};
  for (bool on : toggles) {
    ScrapeLogger::setEnabled(on);
    QCOMPARE(ScrapeLogger::isEnabled(), on);
    QVERIFY2(cat.isDebugEnabled(), category.constData());
    QCOMPARE(lcScrapeRulesTest().isDebugEnabled(), on);
    QCOMPARE(lcScrapeRulesTest().isInfoEnabled(), on);
  }
}

void TestScrapeLoggerRules::scrapeToggleKeepsFinalSayOnScrapeCategories() {
  // A broad startup rule also covers the scrape categories, but the toggle
  // is applied after it and so still switches them off, as before the fix.
  qputenv("KARTEND_LOG_RULES", "kartend.*.debug=true");
  LoggingRules::applyStartupRules();
  QVERIFY(lcPerfTrace().isDebugEnabled());

  ScrapeLogger::setEnabled(true);
  QVERIFY(lcScrapeRulesTest().isDebugEnabled());
  QVERIFY(lcPerfTrace().isDebugEnabled());

  ScrapeLogger::setEnabled(false);
  QVERIFY(!lcScrapeRulesTest().isDebugEnabled());
  QVERIFY(lcScrapeRulesTest().isWarningEnabled());
  QVERIFY(lcPerfTrace().isDebugEnabled());
}

void TestScrapeLoggerRules::qtLoggingRulesStillOverride() {
  // QT_LOGGING_RULES (pinned in main()) is evaluated after the API rules, so
  // it beats both a startup rule and the scrape toggle.
  qputenv("KARTEND_LOG_RULES", "kartend.rulestest.pinned.debug=true");
  LoggingRules::applyStartupRules();
  QVERIFY(!lcRulesPinned().isDebugEnabled());

  ScrapeLogger::setEnabled(true);
  QVERIFY(lcScrapeRulesTest().isDebugEnabled());
  QVERIFY(!lcScrapeRulesPinned().isDebugEnabled());
  QVERIFY(!lcRulesPinned().isDebugEnabled());
}

QTEST_GUILESS_MAIN(TestScrapeLoggerRules)

#include "test_scrapelogger_rules.moc"
