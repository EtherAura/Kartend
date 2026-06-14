// Kartend-4mqkof / Kartend-6wn0p: ConfigurationPanel gained a "DAT files" group
// with an "Open DAT Audit…" button and a "last audited N ago" label, both driven
// through SettingsModel hooks the host (MainWindow) wires. These tests drive the
// panel headlessly with stub hooks + a synthetic collection: they verify the
// panel computes the *canonical* collection UUID for the last-audited lookup (so
// the link resolves against profiles saved by the editor's picker), renders the
// right label state, and hands the launch hook the working collection with the
// live widgets' unsaved media-dir / DAT edits overlaid. The panel is never shown.

#include "configurationpanel.h"
#include "settingsmodel.h"

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "imainwindow.h"
#include "pathutils.h"

#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTest>

class TestConfigurationPanel : public QObject {
  Q_OBJECT

private slots:
  void lastAuditedLabel_neverWhenNoHook();
  void lastAuditedLabel_neverWhenHookReturnsZero();
  void lastAuditedLabel_usesCanonicalUuidAndShowsRelative();
  void openAuditButton_passesWorkingCopyWithUnsavedEdits();
};

namespace {

CollectionConfig makeCollection(const QString &name, const QString &mediaDir) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  return c;
}

QString canonicalUuid(const CollectionConfig &c) {
  return CollectionUtils::computeCollectionUuid(
      c.name, PathUtils::validateAndExpandPath(c.mediaDirectory, c.name));
}

QLabel *lastAuditedLabel(const ConfigurationPanel &panel) {
  return panel.findChild<QLabel *>(QStringLiteral("lastAuditedValueLabel"));
}

} // namespace

void TestConfigurationPanel::lastAuditedLabel_neverWhenNoHook() {
  ConfigurationPanel panel;
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Movies"), QStringLiteral("/m/mov"))};
  int idx = 0;
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  // No datAuditStatus hook (tests / standalone) → the label degrades to "never".
  panel.setModel(&model);
  panel.load();

  QLabel *label = lastAuditedLabel(panel);
  QVERIFY(label);
  QCOMPARE(label->text(), QStringLiteral("never"));
}

void TestConfigurationPanel::lastAuditedLabel_neverWhenHookReturnsZero() {
  ConfigurationPanel panel;
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Movies"), QStringLiteral("/m/mov"))};
  int idx = 0;
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  model.datAuditStatus = [](const QString &) {
    return IMainWindow::DatAuditStatus{}; // never audited
  };
  panel.setModel(&model);
  panel.load();

  QLabel *label = lastAuditedLabel(panel);
  QVERIFY(label);
  QCOMPARE(label->text(), QStringLiteral("never"));
}

void TestConfigurationPanel::lastAuditedLabel_usesCanonicalUuidAndShowsRelative() {
  ConfigurationPanel panel;
  const CollectionConfig collection =
      makeCollection(QStringLiteral("Music"), QStringLiteral("/m/music"));
  QList<CollectionConfig> cols{collection};
  int idx = 0;

  QString seenUuid;
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  model.datAuditStatus = [&seenUuid](const QString &uuid) {
    seenUuid = uuid;
    IMainWindow::DatAuditStatus s;
    s.lastScanMs = QDateTime::currentMSecsSinceEpoch() - 3LL * 60 * 60 * 1000; // ~3 hours ago
    s.hasResults = true;
    s.present = 12;
    s.missing = 3;
    return s;
  };
  panel.setModel(&model);
  panel.load();

  // The panel must look up the SAME UUID the rest of the app keys on, else a
  // saved link would never resolve.
  QCOMPARE(seenUuid, canonicalUuid(collection));

  QLabel *label = lastAuditedLabel(panel);
  QVERIFY(label);
  QVERIFY(label->text() != QStringLiteral("never")); // a real timestamp → a relative string
  QVERIFY(label->text().contains(QStringLiteral("ago")));
  // Persisted snapshot counts ride along (Kartend-m6qsb.8).
  QVERIFY(label->text().contains(QStringLiteral("12 present")));
  QVERIFY(label->text().contains(QStringLiteral("3 missing")));
}

void TestConfigurationPanel::openAuditButton_passesWorkingCopyWithUnsavedEdits() {
  // 6wn0p: launching from settings must reflect what's ON SCREEN (the live
  // widgets), not the last-Applied collection — so a just-added DAT / edited
  // media dir is audited without forcing an Apply first.
  ConfigurationPanel panel;
  CollectionConfig saved = makeCollection(QStringLiteral("Music"), QStringLiteral("/m/music"));
  saved.scraperOverrides.datFilePaths = {QStringLiteral("/d/a.dat")}; // last-Applied DAT list
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Other"), QStringLiteral("/o")),
                               saved};
  int idx = 1; // editing "Music"

  CollectionConfig launched;
  bool launchedSet = false;
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  model.openDatAudit = [&launched, &launchedSet](const CollectionConfig &c) {
    launched = c;
    launchedSet = true;
  };
  panel.setModel(&model);
  panel.load(); // populate widgets from the working collection

  // Unsaved on-screen edits: a changed media dir + an extra DAT not yet Applied.
  panel.mediaDirLineEdit()->setText(QStringLiteral("/m/music-edited"));
  auto *datList = panel.findChild<QListWidget *>(QStringLiteral("datFilesListWidget"));
  QVERIFY(datList);
  datList->addItem(QStringLiteral("/d/b.dat"));

  auto *button = panel.findChild<QPushButton *>(QStringLiteral("openDatAuditButton"));
  QVERIFY(button);
  button->click();

  QVERIFY(launchedSet);
  QCOMPARE(launched.name, QStringLiteral("Music")); // identity from the working collection
  QCOMPARE(launched.mediaDirectory, QStringLiteral("/m/music-edited")); // unsaved media-dir edit
  QCOMPARE(
      launched.scraperOverrides.datFilePaths,
      (QStringList{QStringLiteral("/d/a.dat"), QStringLiteral("/d/b.dat")})); // saved + unsaved
}

QTEST_MAIN(TestConfigurationPanel)
#include "test_configurationpanel.moc"
