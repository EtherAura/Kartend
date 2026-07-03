// SettingsModel::currentWorkingCollection() — the one canonical guard every
// settings panel's load()/save() path leans on instead of repeating the
// null-parts + range checks inline. These tests pin the validity matrix:
// nullptr for each unwired model part and for out-of-range indexes, and the
// working-copy row (not a copy) when everything lines up.

#include "settingsmodel.h"

#include "collection/collectionconfig.h"

#include <QList>
#include <QTest>

class TestSettingsModel : public QObject {
  Q_OBJECT

private slots:
  void nullWorkingCollections_returnsNull();
  void nullCurrentIndex_returnsNull();
  void negativeIndex_returnsNull();
  void indexPastEnd_returnsNull();
  void emptyList_returnsNull();
  void validIndex_returnsThatRow();
};

namespace {

CollectionConfig makeCollection(const QString &name) {
  CollectionConfig c;
  c.name = name;
  return c;
}

} // namespace

void TestSettingsModel::nullWorkingCollections_returnsNull() {
  int idx = 0;
  SettingsModel model;
  model.currentIndex = &idx; // workingCollections stays unset
  QVERIFY(!model.currentWorkingCollection());
}

void TestSettingsModel::nullCurrentIndex_returnsNull() {
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Videos"))};
  SettingsModel model;
  model.workingCollections = &cols; // currentIndex stays unset
  QVERIFY(!model.currentWorkingCollection());
}

void TestSettingsModel::negativeIndex_returnsNull() {
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Videos"))};
  int idx = -1; // the dialog's "no row selected" sentinel
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  QVERIFY(!model.currentWorkingCollection());
}

void TestSettingsModel::indexPastEnd_returnsNull() {
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Videos")),
                               makeCollection(QStringLiteral("Audio"))};
  int idx = 2; // == size(): first invalid slot (stale index after a removal)
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  QVERIFY(!model.currentWorkingCollection());
  idx = 99;
  QVERIFY(!model.currentWorkingCollection());
}

void TestSettingsModel::emptyList_returnsNull() {
  QList<CollectionConfig> cols;
  int idx = 0;
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;
  QVERIFY(!model.currentWorkingCollection());
}

void TestSettingsModel::validIndex_returnsThatRow() {
  QList<CollectionConfig> cols{makeCollection(QStringLiteral("Videos")),
                               makeCollection(QStringLiteral("Audio"))};
  int idx = 1;
  SettingsModel model;
  model.workingCollections = &cols;
  model.currentIndex = &idx;

  CollectionConfig *current = model.currentWorkingCollection();
  QVERIFY(current);
  QCOMPARE(current->name, QStringLiteral("Audio"));
  // The accessor hands back the live working row — panel save() writes
  // through it must land in the list, not in a detached copy.
  current->name = QStringLiteral("Audiobooks");
  QCOMPARE(cols[1].name, QStringLiteral("Audiobooks"));

  // Index moves → the accessor tracks it (same pointers, new row).
  idx = 0;
  QCOMPARE(model.currentWorkingCollection()->name, QStringLiteral("Videos"));
}

QTEST_APPLESS_MAIN(TestSettingsModel)
#include "test_settingsmodel.moc"
