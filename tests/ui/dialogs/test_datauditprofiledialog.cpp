// Kartend-x9mkif.3: DatAuditProfileDialog grew a "Linked collection" picker.
// These tests drive the picker's value semantics headlessly — the dialog is
// constructed with a seed Profile + a synthetic collection list and queried via
// profile() without ever being shown or exec()'d, so the read-back logic
// (preselect the matching link, default to none, preserve an orphaned link,
// honour an explicit pick/clear) is verified without a display or DB.
//
// The UUID a picked collection stores must equal the canonical
// CollectionUtils::computeCollectionUuid(name, expandedMediaDir) the rest of the
// app keys on; the test computes its expectation through the exact same helpers
// the dialog uses, so a match proves the link resolves elsewhere regardless of
// whether the (synthetic) media dir exists on disk.

#include "datauditprofiledialog.h"

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "datauditprofile.h"
#include "pathutils.h"

#include <QApplication>
#include <QComboBox>
#include <QList>
#include <QPushButton>
#include <QTest>

class TestDatAuditProfileDialog : public QObject {
  Q_OBJECT

private slots:
  void preselectsMatchingLink();
  void defaultsToNoneWhenSeedHasNoLink();
  void preservesOrphanLinkWithCollections();
  void preservesLinkWhenNoCollectionsSupplied();
  void selectingACollectionStoresItsUuid();
  void selectingNoneClearsLink();
  void linkingDerivesListsAndLocksEditors();
  void unlinkingUnlocksEditors();
  void orphanLinkKeepsSeedListsLocked();
};

namespace {

CollectionConfig makeCollection(const QString &name, const QString &mediaDir) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  return c;
}

// The UUID the dialog computes for a collection — same two helpers, same order.
QString expectedUuid(const CollectionConfig &c) {
  return CollectionUtils::computeCollectionUuid(
      c.name, PathUtils::validateAndExpandPath(c.mediaDirectory, c.name));
}

// The "Linked collection" combo is the one whose first entry is "(none)".
QComboBox *linkedCombo(const QWidget &dlg) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->count() > 0 && c->itemText(0) == QStringLiteral("(none)")) {
      return c;
    }
  }
  return nullptr;
}

// The Add-button of a path group, found by its label text.
QPushButton *buttonByText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

} // namespace

void TestDatAuditProfileDialog::preselectsMatchingLink() {
  const CollectionConfig c = makeCollection(QStringLiteral("Movies"), QStringLiteral("/m/movies"));
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed;
  seed.collectionUuid = expectedUuid(c);

  DatAuditProfileDialog dlg(seed, &cols);
  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  QCOMPARE(combo->currentText(), c.name); // the collection, not "(none)"
  QCOMPARE(combo->currentData().toString(), expectedUuid(c));
  QCOMPARE(dlg.profile().collectionUuid, expectedUuid(c));
}

void TestDatAuditProfileDialog::defaultsToNoneWhenSeedHasNoLink() {
  const CollectionConfig c = makeCollection(QStringLiteral("Movies"), QStringLiteral("/m/movies"));
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed; // collectionUuid empty

  DatAuditProfileDialog dlg(seed, &cols);
  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  QCOMPARE(combo->currentIndex(), 0);
  QVERIFY(combo->currentData().toString().isEmpty());
  QVERIFY(dlg.profile().collectionUuid.isEmpty());
}

void TestDatAuditProfileDialog::preservesOrphanLinkWithCollections() {
  // Seed links a UUID that no current collection produces (renamed/removed).
  // The picker keeps it under a trailing entry so saving doesn't drop it.
  const CollectionConfig c = makeCollection(QStringLiteral("Movies"), QStringLiteral("/m/movies"));
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed;
  seed.collectionUuid = QStringLiteral("orphaned-uuid-not-in-list");

  DatAuditProfileDialog dlg(seed, &cols);
  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  QCOMPARE(combo->currentData().toString(), QStringLiteral("orphaned-uuid-not-in-list"));
  QCOMPARE(dlg.profile().collectionUuid, QStringLiteral("orphaned-uuid-not-in-list"));
}

void TestDatAuditProfileDialog::preservesLinkWhenNoCollectionsSupplied() {
  // No collection list at all — an existing link must still round-trip.
  DatAuditProfile::Profile seed;
  seed.collectionUuid = QStringLiteral("some-stored-uuid");

  DatAuditProfileDialog dlg(seed, nullptr);
  QCOMPARE(dlg.profile().collectionUuid, QStringLiteral("some-stored-uuid"));
}

void TestDatAuditProfileDialog::selectingACollectionStoresItsUuid() {
  const CollectionConfig c = makeCollection(QStringLiteral("Music"), QStringLiteral("/m/music"));
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed; // starts unlinked

  DatAuditProfileDialog dlg(seed, &cols);
  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  const int idx = combo->findData(expectedUuid(c));
  QVERIFY(idx > 0); // present, and not the "(none)" row
  combo->setCurrentIndex(idx);
  QCOMPARE(dlg.profile().collectionUuid, expectedUuid(c));
}

void TestDatAuditProfileDialog::selectingNoneClearsLink() {
  const CollectionConfig c = makeCollection(QStringLiteral("Music"), QStringLiteral("/m/music"));
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed;
  seed.collectionUuid = expectedUuid(c); // starts linked

  DatAuditProfileDialog dlg(seed, &cols);
  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(0); // "(none)"
  QVERIFY(dlg.profile().collectionUuid.isEmpty());
}

void TestDatAuditProfileDialog::linkingDerivesListsAndLocksEditors() {
  // Kartend-m6qsb.2: linking a collection makes the collection's settings
  // canonical — the editor's lists repopulate from it and lock.
  CollectionConfig c = makeCollection(QStringLiteral("Concerts"), QStringLiteral("/m/concerts"));
  c.scraperOverrides.datFilePaths = {QStringLiteral("/dats/concerts.dat")};
  const QList<CollectionConfig> cols{c};

  DatAuditProfile::Profile seed; // unlinked, with hand-picked lists
  DatAuditProfile::DatRef handPicked;
  handPicked.path = QStringLiteral("/dats/manual.dat");
  seed.dats = {handPicked};
  seed.scanRoots = {QStringLiteral("/somewhere/else")};

  DatAuditProfileDialog dlg(seed, &cols);
  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(combo->findData(expectedUuid(c)));

  const DatAuditProfile::Profile out = dlg.profile();
  QCOMPARE(out.dats.size(), 1);
  QCOMPARE(out.dats.at(0).path, QStringLiteral("/dats/concerts.dat"));
  QCOMPARE(out.scanRoots,
           QStringList{PathUtils::expandPathWithoutExistenceCheck(c.mediaDirectory, c.name)});

  QPushButton *addDat = buttonByText(dlg, QStringLiteral("Add DAT…"));
  QPushButton *addRoot = buttonByText(dlg, QStringLiteral("Add folder…"));
  QVERIFY(addDat && addRoot);
  QVERIFY(!addDat->isEnabled());
  QVERIFY(!addRoot->isEnabled());
}

void TestDatAuditProfileDialog::unlinkingUnlocksEditors() {
  CollectionConfig c = makeCollection(QStringLiteral("Concerts"), QStringLiteral("/m/concerts"));
  c.scraperOverrides.datFilePaths = {QStringLiteral("/dats/concerts.dat")};
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed;
  seed.collectionUuid = expectedUuid(c); // starts linked → locked

  DatAuditProfileDialog dlg(seed, &cols);
  QPushButton *addDat = buttonByText(dlg, QStringLiteral("Add DAT…"));
  QVERIFY(addDat);
  QVERIFY(!addDat->isEnabled());

  QComboBox *combo = linkedCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(0); // "(none)" — local editing resumes
  QVERIFY(addDat->isEnabled());
  QVERIFY(dlg.profile().collectionUuid.isEmpty());
}

void TestDatAuditProfileDialog::orphanLinkKeepsSeedListsLocked() {
  // An unresolvable stored link must not clobber the cached lists — they are
  // the only record of what the profile audits — but editing stays locked
  // (the lists are still semantically derived, just from a missing source).
  const CollectionConfig c = makeCollection(QStringLiteral("Movies"), QStringLiteral("/m/movies"));
  const QList<CollectionConfig> cols{c};
  DatAuditProfile::Profile seed;
  seed.collectionUuid = QStringLiteral("orphaned-uuid-not-in-list");
  DatAuditProfile::DatRef cached;
  cached.path = QStringLiteral("/dats/cached.dat");
  seed.dats = {cached};
  seed.scanRoots = {QStringLiteral("/m/cached-root")};

  DatAuditProfileDialog dlg(seed, &cols);
  const DatAuditProfile::Profile out = dlg.profile();
  QCOMPARE(out.dats.size(), 1);
  QCOMPARE(out.dats.at(0).path, QStringLiteral("/dats/cached.dat"));
  QCOMPARE(out.scanRoots, QStringList{QStringLiteral("/m/cached-root")});

  QPushButton *addDat = buttonByText(dlg, QStringLiteral("Add DAT…"));
  QVERIFY(addDat);
  QVERIFY(!addDat->isEnabled());
}

QTEST_MAIN(TestDatAuditProfileDialog)
#include "test_datauditprofiledialog.moc"
