// Tests for PresentationProfileIO — JSON round-trip, registry CRUD,
// applyTo / fromGeneralSettings symmetry. No DB or event loop.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "collection/generalsettings.h"
#include "collection/presentationprofile.h"

class TestPresentationProfile : public QObject {
  Q_OBJECT
private slots:
  void roundTripsThroughJson();
  void rejectsMissingSchemaVersion();
  void rejectsNewerSchemaVersion();
  void applyToOverwritesPresentationFields();
  void applyToLeavesNonPresentationAlone();
  void fromGeneralSettingsRoundTripsViaApply();
  void registry_emptyFile_returnsEmptyList();
  void registry_saveAndLoad_roundTrips();
  void registry_addOrReplace_dedupesCaseInsensitively();
  void registry_removeByName_caseInsensitive();
};

void TestPresentationProfile::roundTripsThroughJson() {
  PresentationProfile in;
  in.name = "Couch attract";
  in.description = "Idle 10 minutes, slow scroll.";
  in.attractModeEnabled = true;
  in.attractModeIdleTimeoutSec = 600;
  in.attractModeScrollSpeed = 0.5;
  in.attractModeAdvanceSelectionRandom = true;
  in.marqueeEnabled = true;
  in.marqueeScreenName = "HDMI-A-2";
  in.marqueeMode = 1;
  in.bootSplashTitle = "Hello";

  const auto json = PresentationProfileIO::toJson(in);
  auto parsed = PresentationProfileIO::fromJson(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value(), in);
}

void TestPresentationProfile::rejectsMissingSchemaVersion() {
  QJsonObject obj;
  obj["name"] = "x";
  auto r = PresentationProfileIO::fromJson(obj);
  QVERIFY(r.isError());
}

void TestPresentationProfile::rejectsNewerSchemaVersion() {
  QJsonObject obj;
  obj["schemaVersion"] = 99;
  obj["name"] = "future";
  auto r = PresentationProfileIO::fromJson(obj);
  QVERIFY(r.isError());
  QVERIFY(r.error().details.contains("schema"));
}

void TestPresentationProfile::applyToOverwritesPresentationFields() {
  GeneralSettings target;
  target.attractModeEnabled = false;
  target.marqueeMode = 0;

  PresentationProfile p;
  p.attractModeEnabled = true;
  p.attractModeIdleTimeoutSec = 30;
  p.marqueeEnabled = true;
  p.marqueeMode = 1;

  PresentationProfileIO::applyTo(p, target);
  QVERIFY(target.attractModeEnabled);
  QCOMPARE(target.attractModeIdleTimeoutSec, 30);
  QVERIFY(target.marqueeEnabled);
  QCOMPARE(target.marqueeMode, 1);
}

void TestPresentationProfile::applyToLeavesNonPresentationAlone() {
  // Sanity check: the profile must not stomp keybindings / scraper /
  // unrelated settings. We poke a few representative non-presentation
  // fields, apply a profile, and assert they survive.
  GeneralSettings target;
  target.keyNavLeft = Qt::Key_A;
  target.retroarchConfigPath = "/etc/retroarch.cfg";

  PresentationProfile p;
  p.attractModeEnabled = true;

  PresentationProfileIO::applyTo(p, target);
  QCOMPARE(target.keyNavLeft, int(Qt::Key_A));
  QCOMPARE(target.retroarchConfigPath, QStringLiteral("/etc/retroarch.cfg"));
}

void TestPresentationProfile::fromGeneralSettingsRoundTripsViaApply() {
  // fromGeneralSettings → applyTo on a fresh struct should reconstitute
  // the original values. Confirms the field mapping is symmetrical.
  GeneralSettings original;
  original.attractModeEnabled = true;
  original.attractModeIdleTimeoutSec = 60;
  original.attractModeScrollSpeed = 2.0;
  original.marqueeEnabled = true;
  original.marqueeMode = 1;
  original.bootSplashSubtitle = "Welcome back";

  const auto profile = PresentationProfileIO::fromGeneralSettings(original);
  GeneralSettings out; // default
  PresentationProfileIO::applyTo(profile, out);
  QCOMPARE(out.attractModeEnabled, original.attractModeEnabled);
  QCOMPARE(out.attractModeIdleTimeoutSec, original.attractModeIdleTimeoutSec);
  QCOMPARE(out.attractModeScrollSpeed, original.attractModeScrollSpeed);
  QCOMPARE(out.marqueeEnabled, original.marqueeEnabled);
  QCOMPARE(out.marqueeMode, original.marqueeMode);
  QCOMPARE(out.bootSplashSubtitle, original.bootSplashSubtitle);
}

void TestPresentationProfile::registry_emptyFile_returnsEmptyList() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto r = PresentationProfileIO::loadRegistry(dir.filePath("missing.json"));
  QVERIFY(r.isOk());
  QVERIFY(r.value().isEmpty());
}

void TestPresentationProfile::registry_saveAndLoad_roundTrips() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("registry.json");

  PresentationProfile a;
  a.name = "Couch";
  a.attractModeEnabled = true;
  PresentationProfile b;
  b.name = "Demo Mode";
  b.attractModeAdvanceSelectionRandom = true;

  const auto saved = PresentationProfileIO::saveRegistry({a, b}, path);
  QVERIFY(saved.isOk());

  const auto loaded = PresentationProfileIO::loadRegistry(path);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().size(), 2);
  QCOMPARE(loaded.value().at(0).name, QStringLiteral("Couch"));
  QVERIFY(loaded.value().at(1).attractModeAdvanceSelectionRandom);
}

void TestPresentationProfile::registry_addOrReplace_dedupesCaseInsensitively() {
  PresentationProfile a;
  a.name = "Couch";
  a.attractModeIdleTimeoutSec = 60;
  PresentationProfile b;
  b.name = "COUCH"; // same name, different case
  b.attractModeIdleTimeoutSec = 300;
  const auto after = PresentationProfileIO::addOrReplace({a}, b);
  QCOMPARE(after.size(), 1);
  QCOMPARE(after.first().attractModeIdleTimeoutSec, 300);
}

void TestPresentationProfile::registry_removeByName_caseInsensitive() {
  PresentationProfile a;
  a.name = "Couch";
  PresentationProfile b;
  b.name = "Demo";
  const auto after = PresentationProfileIO::removeByName({a, b}, "COUCH");
  QCOMPARE(after.size(), 1);
  QCOMPARE(after.first().name, QStringLiteral("Demo"));
}

QTEST_MAIN(TestPresentationProfile)
#include "test_presentationprofile.moc"
