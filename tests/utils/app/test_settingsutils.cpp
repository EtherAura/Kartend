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
#include <QTextStream>

class TestSettingsUtils : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void exportConfig_writesCompleteLoadableCopy();
  void exportConfig_replacesExistingDestination();
  void importConfig_installsSourceAsLiveConfig();
  void roundTrip_adversarialValuesSurviveWriteThenRead();
  void roundTrip_legacyPlainValueStillReadsUnescaped();
  void legacyPercentValue_readsVerbatimAndSurvivesResave();
  void legacyPercentValue_firstRewriteSnapshotsBackup();
  void newWriterPercentValue_stillDecodes();
  void legacyRoundTrippingLiteral_isKnownDecodeResidue();

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

// Kartend-n777n: the custom "conf" writer/reader gained percent-escaping so
// arbitrary user strings survive a write/read round-trip. Drive a real
// QSettings (so the registered format's writeIniFile/readIniFile run) over the
// adversarial set the issue calls out: trailing space, a fully bracket-wrapped
// value, leading whitespace, an "--opt=val"-style embedded '=', a ';'-prefixed
// value, and an embedded newline. Each must read back byte-identical.
void TestSettingsUtils::roundTrip_adversarialValuesSurviveWriteThenRead() {
  struct Case {
    const char *key;
    QString value;
  };
  const QList<Case> cases = {
      {"General/trailingSpace", QStringLiteral("value ")},
      {"General/bracketWrapped", QStringLiteral("[x]")},
      {"General/leadingWhitespace", QStringLiteral(" leading")},
      {"General/embeddedEquals", QStringLiteral("--opt=val")},
      {"General/commentPrefix", QStringLiteral(";x")},
      {"General/hashPrefix", QStringLiteral("#x")},
      {"General/embeddedNewline", QStringLiteral("line1\nline2")},
      // A plain neighbour written in the same file must be unaffected by the
      // adversarial entries around it (guards against cascade corruption).
      {"General/plainNeighbour", QStringLiteral("ordinary")},
  };

  const QString path = m_scratch.filePath(QStringLiteral("roundtrip.cfg"));
  QFile::remove(path);
  {
    QSettings w(path, SettingsUtils::getFormat());
    for (const Case &c : cases) {
      w.setValue(QString::fromLatin1(c.key), c.value);
    }
    w.sync();
    QCOMPARE(w.status(), QSettings::NoError);
  }

  QSettings r(path, SettingsUtils::getFormat());
  QCOMPARE(r.status(), QSettings::NoError);
  for (const Case &c : cases) {
    QCOMPARE(r.value(QString::fromLatin1(c.key)).toString(), c.value);
  }
}

// Back-compat: a config file written by an older build (no escaping at all,
// plain "key=value" lines) must keep reading exactly as it did before. The
// reader trims surrounding whitespace on plain values, matching the historical
// behavior — only escaped values bypass the trim.
void TestSettingsUtils::roundTrip_legacyPlainValueStillReadsUnescaped() {
  const QString path = m_scratch.filePath(QStringLiteral("legacy-plain.cfg"));
  QFile::remove(path);
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[General]\n";
    out << "exampleKey=exampleValue\n";
    out << "spacedValue=  padded  \n"; // legacy reader trimmed these
  }

  QSettings r(path, SettingsUtils::getFormat());
  QCOMPARE(r.status(), QSettings::NoError);
  QCOMPARE(r.value(QStringLiteral("General/exampleKey")).toString(),
           QStringLiteral("exampleValue"));
  // Legacy plain value: surrounding whitespace stripped, exactly as before.
  QCOMPARE(r.value(QStringLiteral("General/spacedValue")).toString(), QStringLiteral("padded"));
}

// Kartend-309nh.6: pre-v0.0.15 builds wrote values unescaped, so a '%' on
// disk can be legacy user data (URL-escaped launch params, credentials). The
// decoder must only percent-decode when re-encoding the decoded result
// reproduces the raw bytes; otherwise the value is legacy and must pass
// through verbatim — on first load AND after any rewrite re-encodes the file.
void TestSettingsUtils::legacyPercentValue_readsVerbatimAndSurvivesResave() {
  const QString path = m_scratch.filePath(QStringLiteral("legacy-percent.cfg"));
  QFile::remove(path);
  QFile::remove(path + QStringLiteral(".legacy.bak"));
  const QString legacyValue = QStringLiteral("--rom=file%20name.bin --url=http%3A%2F%2Fx");
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[General]\n";
    out << "launchParams=" << legacyValue << "\n";
  }

  // First load: the legacy %HH bytes must come through untouched (the old
  // behavior decoded them to "file name.bin ..." — silent corruption).
  {
    QSettings r(path, SettingsUtils::getFormat());
    QCOMPARE(r.status(), QSettings::NoError);
    QCOMPARE(r.value(QStringLiteral("General/launchParams")).toString(), legacyValue);
    // Rewrite the file (any settings sync rewrites the whole map).
    r.setValue(QStringLiteral("General/other"), QStringLiteral("x"));
    r.sync();
    QCOMPARE(r.status(), QSettings::NoError);
  }

  // Re-read through a genuine parse: copy to a fresh path so QSettings'
  // per-path file cache can't serve the in-memory values.
  const QString rereadPath = m_scratch.filePath(QStringLiteral("legacy-percent-reread.cfg"));
  QFile::remove(rereadPath);
  QVERIFY(QFile::copy(path, rereadPath));
  QSettings r2(rereadPath, SettingsUtils::getFormat());
  QCOMPARE(r2.status(), QSettings::NoError);
  QCOMPARE(r2.value(QStringLiteral("General/launchParams")).toString(), legacyValue);
  QCOMPARE(r2.value(QStringLiteral("General/other")).toString(), QStringLiteral("x"));
}

// The first rewrite of a file that loaded with legacy-suspect '%' values must
// leave a one-time <name>.legacy.bak snapshot of the pre-rewrite bytes; later
// rewrites must not touch it.
void TestSettingsUtils::legacyPercentValue_firstRewriteSnapshotsBackup() {
  const QString path = m_scratch.filePath(QStringLiteral("legacy-bak.cfg"));
  const QString bakPath = path + QStringLiteral(".legacy.bak");
  QFile::remove(path);
  QFile::remove(bakPath);
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[General]\n";
    out << "cred=pass%20word\n";
  }

  QSettings s(path, SettingsUtils::getFormat());
  QCOMPARE(s.value(QStringLiteral("General/cred")).toString(), QStringLiteral("pass%20word"));
  QVERIFY(!QFile::exists(bakPath)); // load alone must not create the snapshot

  s.setValue(QStringLiteral("General/first"), QStringLiteral("1"));
  s.sync();
  QCOMPARE(s.status(), QSettings::NoError);
  QVERIFY(QFile::exists(bakPath));
  {
    QFile bak(bakPath);
    QVERIFY(bak.open(QIODevice::ReadOnly));
    const QByteArray snapshot = bak.readAll();
    QVERIFY(snapshot.contains("cred=pass%20word")); // original raw bytes
    QVERIFY(!snapshot.contains("first"));           // taken before the rewrite
  }

  // Second rewrite: the snapshot is one-time and must stay untouched.
  s.setValue(QStringLiteral("General/second"), QStringLiteral("2"));
  s.sync();
  QFile bak(bakPath);
  QVERIFY(bak.open(QIODevice::ReadOnly));
  QVERIFY(!bak.readAll().contains("second"));
}

// Values written by the current encoder must keep decoding: everything the
// encoder emits passes the round-trip check by construction, so the legacy
// heuristic must not affect it — and such files are not legacy-suspect, so
// no .legacy.bak appears.
void TestSettingsUtils::newWriterPercentValue_stillDecodes() {
  const QString path = m_scratch.filePath(QStringLiteral("new-percent.cfg"));
  QFile::remove(path);
  {
    QSettings w(path, SettingsUtils::getFormat());
    w.setValue(QStringLiteral("General/volume"), QStringLiteral("100%"));
    w.setValue(QStringLiteral("General/multiline"), QStringLiteral("a\nb"));
    w.sync();
    QCOMPARE(w.status(), QSettings::NoError);
  }

  // Genuine re-parse via a fresh path (see above).
  const QString rereadPath = m_scratch.filePath(QStringLiteral("new-percent-reread.cfg"));
  QFile::remove(rereadPath);
  QVERIFY(QFile::copy(path, rereadPath));
  QSettings r(rereadPath, SettingsUtils::getFormat());
  QCOMPARE(r.status(), QSettings::NoError);
  QCOMPARE(r.value(QStringLiteral("General/volume")).toString(), QStringLiteral("100%"));
  QCOMPARE(r.value(QStringLiteral("General/multiline")).toString(), QStringLiteral("a\nb"));

  // A round-tripping file is not legacy-suspect: rewriting it leaves no .bak.
  QSettings w2(rereadPath, SettingsUtils::getFormat());
  w2.setValue(QStringLiteral("General/extra"), QStringLiteral("x"));
  w2.sync();
  QVERIFY(!QFile::exists(rereadPath + QStringLiteral(".legacy.bak")));
}

// Documented residue of the round-trip heuristic (Kartend-309nh.6, user
// decision Q5=A): a legacy literal that happens to look exactly like encoder
// output — "100%25" — still percent-decodes to "100%". This assertion pins
// the accepted behavior so any future change to it is deliberate.
void TestSettingsUtils::legacyRoundTrippingLiteral_isKnownDecodeResidue() {
  const QString path = m_scratch.filePath(QStringLiteral("legacy-residue.cfg"));
  QFile::remove(path);
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[General]\n";
    out << "pct=100%25\n";
  }

  QSettings r(path, SettingsUtils::getFormat());
  QCOMPARE(r.status(), QSettings::NoError);
  QCOMPARE(r.value(QStringLiteral("General/pct")).toString(), QStringLiteral("100%"));

  // Round-tripping values don't mark the file legacy-suspect either.
  r.setValue(QStringLiteral("General/extra"), QStringLiteral("x"));
  r.sync();
  QVERIFY(!QFile::exists(path + QStringLiteral(".legacy.bak")));
}

QTEST_MAIN(TestSettingsUtils)
#include "test_settingsutils.moc"
