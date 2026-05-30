// Tests for SettingsUtils::exportConfig / importConfig — the atomic copy/replace
// of kartend.cfg. Kartend-g2ox switched both from a copy-to-.tmp / remove-dest /
// rename dance (which left no destination if interrupted, and never fsync'd) to
// QSaveFile + parent-directory fsync. getConfigPath() is redirected to a private
// temp dir via XDG_CONFIG_HOME so the tests never touch the real config and can't
// race other test binaries under parallel ctest (cf. Kartend-kkeur).
#include "settingsutils.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class TestSettingsUtils : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void exportConfig_writesCompleteLoadableCopy();
  void exportConfig_replacesExistingDestination();
  void importConfig_installsSourceAsLiveConfig();

private:
  void seedLiveConfig(const QString &key, const QString &value);
  QTemporaryDir m_configHome;
  QTemporaryDir m_scratch;
};

void TestSettingsUtils::initTestCase() {
#ifndef Q_OS_LINUX
  QSKIP("getConfigPath() is isolated here via XDG_CONFIG_HOME, which only "
        "redirects QStandardPaths::ConfigLocation on Linux.");
#endif
  QVERIFY(m_configHome.isValid());
  QVERIFY(m_scratch.isValid());
  qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
  // Guard: if the redirect didn't take effect we must not run — the tests
  // would otherwise read/write the developer's real kartend.cfg.
  QVERIFY2(SettingsUtils::getConfigPath().startsWith(m_configHome.path()),
           qPrintable(SettingsUtils::getConfigPath()));
}

void TestSettingsUtils::seedLiveConfig(const QString &key, const QString &value) {
  QSettings live(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  live.setValue(key, value);
  live.sync();
  QCOMPARE(live.status(), QSettings::NoError);
}

void TestSettingsUtils::exportConfig_writesCompleteLoadableCopy() {
  seedLiveConfig(QStringLiteral("General/exampleKey"), QStringLiteral("exampleValue"));

  const QString dest = m_scratch.filePath(QStringLiteral("export-out.cfg"));
  const auto result = SettingsUtils::exportConfig(dest);
  QVERIFY2(result.isOk(),
           result.isError() ? qPrintable(result.error().message) : "exportConfig failed");
  QVERIFY(QFileInfo::exists(dest));

  QSettings exported(dest, SettingsUtils::getFormat());
  QCOMPARE(exported.value(QStringLiteral("General/exampleKey")).toString(),
           QStringLiteral("exampleValue"));
}

void TestSettingsUtils::exportConfig_replacesExistingDestination() {
  seedLiveConfig(QStringLiteral("General/fresh"), QStringLiteral("new"));

  const QString dest = m_scratch.filePath(QStringLiteral("export-replace.cfg"));
  {
    QFile stale(dest);
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write("[General]\nstale=old\n");
  }

  const auto result = SettingsUtils::exportConfig(dest);
  QVERIFY2(result.isOk(),
           result.isError() ? qPrintable(result.error().message) : "exportConfig failed");

  QSettings exported(dest, SettingsUtils::getFormat());
  QCOMPARE(exported.value(QStringLiteral("General/fresh")).toString(), QStringLiteral("new"));
  // The destination is replaced wholesale (atomic rename), not merged into.
  QVERIFY(!exported.contains(QStringLiteral("General/stale")));
}

void TestSettingsUtils::importConfig_installsSourceAsLiveConfig() {
  const QString src = m_scratch.filePath(QStringLiteral("import-src.cfg"));
  {
    QSettings s(src, SettingsUtils::getFormat());
    s.setValue(QStringLiteral("General/imported"), QStringLiteral("yes"));
    s.sync();
    QCOMPARE(s.status(), QSettings::NoError);
  }

  const auto result = SettingsUtils::importConfig(src);
  QVERIFY2(result.isOk(),
           result.isError() ? qPrintable(result.error().message) : "importConfig failed");

  QSettings live(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  QCOMPARE(live.value(QStringLiteral("General/imported")).toString(), QStringLiteral("yes"));
}

QTEST_MAIN(TestSettingsUtils)
#include "test_settingsutils.moc"
