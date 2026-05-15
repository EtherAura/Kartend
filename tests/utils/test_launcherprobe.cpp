// Tests for LauncherProbe — the curated list + the PATH-resolution probe.
// Real PATH inspection is intentional (no mocking): the probe is a thin
// wrapper around QStandardPaths::findExecutable, so any test fixture
// would just be reimplementing the helper. We pick `/bin/sh` as a
// stand-in via a one-off curated list, and assert structural invariants
// against the real curated list.
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include "launcherprobe.h"

class TestLauncherProbe : public QObject {
  Q_OBJECT
private slots:
  void knownLaunchers_isNonEmptyAndHasNoDuplicates();
  void knownLaunchers_leadsWithVideoCategory();
  void knownLaunchers_endsWithEmulatorCategory();
  void knownLaunchers_everyEntryHasNonEmptyFields();
  void knownLaunchers_retroarchUsesCoreParamTemplate();
  void knownLaunchers_includesCommonMediaPlayers();
  void categoryLabel_returnsNonEmptyForEveryCategory();
  void probeInstalled_subsetMatchesKnownList();
};

void TestLauncherProbe::knownLaunchers_isNonEmptyAndHasNoDuplicates() {
  const auto list = LauncherProbe::knownLaunchers();
  QVERIFY(!list.isEmpty());
  QSet<QString> seen;
  for (const auto &k : list) {
    QVERIFY2(!seen.contains(k.binary),
             qPrintable(QString("Duplicate binary in curated list: %1").arg(k.binary)));
    seen.insert(k.binary);
  }
}

void TestLauncherProbe::knownLaunchers_leadsWithVideoCategory() {
  // The doc convention is to surface media-player tools first; the
  // panel-side picker relies on the curated order to group entries
  // without an extra sort. Guard that ordering here.
  const auto list = LauncherProbe::knownLaunchers();
  QVERIFY(!list.isEmpty());
  QCOMPARE(static_cast<int>(list.first().category),
           static_cast<int>(LauncherProbe::Category::Video));
}

void TestLauncherProbe::knownLaunchers_endsWithEmulatorCategory() {
  // Emulators sit at the bottom — same rationale as the leads-with-video
  // check, just the other end. If a future edit adds a new category at
  // the tail, update both this test and the picker grouping logic.
  const auto list = LauncherProbe::knownLaunchers();
  QCOMPARE(static_cast<int>(list.last().category),
           static_cast<int>(LauncherProbe::Category::Emulator));
}

void TestLauncherProbe::knownLaunchers_everyEntryHasNonEmptyFields() {
  for (const auto &k : LauncherProbe::knownLaunchers()) {
    QVERIFY2(!k.binary.trimmed().isEmpty(),
             qPrintable(QString("Empty binary for displayName=%1").arg(k.displayName)));
    QVERIFY2(!k.displayName.trimmed().isEmpty(),
             qPrintable(QString("Empty displayName for binary=%1").arg(k.binary)));
    QVERIFY2(!k.launchParameters.trimmed().isEmpty(),
             qPrintable(QString("Empty params for binary=%1").arg(k.binary)));
    // Every parameter template must include %1 so the launched item's
    // path actually gets passed. Without this guard a typo in a future
    // entry would silently launch the binary with no file.
    QVERIFY2(k.launchParameters.contains(QStringLiteral("%1")),
             qPrintable(QString("Missing %1 in params for %2").arg(k.binary)));
  }
}

void TestLauncherProbe::knownLaunchers_retroarchUsesCoreParamTemplate() {
  // RetroArch is the one entry with a non-trivial params template — it
  // needs both %core (resolved at launch time from the LauncherPreset's
  // corePath field) and %1 (the item path). Other emulators get the
  // default "%1" because they don't carry a core concept.
  for (const auto &k : LauncherProbe::knownLaunchers()) {
    if (k.binary == QStringLiteral("retroarch")) {
      QVERIFY(k.launchParameters.contains(QStringLiteral("%core")));
      QVERIFY(k.launchParameters.contains(QStringLiteral("%1")));
      return;
    }
  }
  QFAIL("retroarch entry missing from curated list");
}

void TestLauncherProbe::knownLaunchers_includesCommonMediaPlayers() {
  // Sanity check: the absolute baseline of common FOSS tools
  // (mpv, vlc, ffplay) must always be present. If a future cleanup
  // accidentally drops one, this test surfaces it before the picker
  // appears empty for users with stock distros.
  QStringList binaries;
  for (const auto &k : LauncherProbe::knownLaunchers()) {
    binaries.append(k.binary);
  }
  QVERIFY(binaries.contains(QStringLiteral("mpv")));
  QVERIFY(binaries.contains(QStringLiteral("vlc")));
  QVERIFY(binaries.contains(QStringLiteral("ffplay")));
}

void TestLauncherProbe::categoryLabel_returnsNonEmptyForEveryCategory() {
  const LauncherProbe::Category all[] = {
      LauncherProbe::Category::Video, LauncherProbe::Category::Audio,
      LauncherProbe::Category::Reader, LauncherProbe::Category::Image,
      LauncherProbe::Category::Emulator,
  };
  for (auto c : all) {
    QVERIFY2(!LauncherProbe::categoryLabel(c).isEmpty(),
             qPrintable(QString("Empty label for category %1").arg(static_cast<int>(c))));
  }
}

void TestLauncherProbe::probeInstalled_subsetMatchesKnownList() {
  // The probe must only return entries that were already in the
  // curated list — it never invents binaries on the fly. Order must
  // match (probe walks knownLaunchers() in order; we want the picker
  // to see groups intact).
  const auto known = LauncherProbe::knownLaunchers();
  QSet<QString> knownSet;
  for (const auto &k : known) {
    knownSet.insert(k.binary);
  }
  const auto installed = LauncherProbe::probeInstalled();
  for (const auto &k : installed) {
    QVERIFY2(knownSet.contains(k.binary),
             qPrintable(QString("probeInstalled returned %1 not in curated list").arg(k.binary)));
  }
  // Order check: each installed entry's index in `known` should
  // monotonically increase.
  int lastKnownIdx = -1;
  for (const auto &k : installed) {
    int idx = -1;
    for (int i = 0; i < known.size(); ++i) {
      if (known[i].binary == k.binary) {
        idx = i;
        break;
      }
    }
    QVERIFY(idx > lastKnownIdx);
    lastKnownIdx = idx;
  }
}

QTEST_MAIN(TestLauncherProbe)
#include "test_launcherprobe.moc"
