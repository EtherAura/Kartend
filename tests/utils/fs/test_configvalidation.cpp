// Unit tests for ConfigValidation (src/utils/configvalidation.cpp).
//
// Covers ValidationResult bookkeeping, single-collection schema checks,
// and cross-collection diagnostics (parent index, name dupes, UUID
// collisions). Filesystem-touching cases use QTemporaryDir so tests
// stay hermetic.

#include <QDir>
#include <QObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "configvalidation.h"

class TestConfigValidation : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
  void cleanupTestCase() { QStandardPaths::setTestModeEnabled(false); }

  void validationResult_addError_marksInvalidAndCollects();
  void validationResult_addWarning_keepsValid();
  void validationResult_hasIssues_reflectsContent();

  void validateCollection_missingNameIsError();
  void validateCollection_emptyMediaDirOnLeafIsWarning();
  void validateCollection_emptyMediaDirOnContainerIsAccepted();
  void validateCollection_missingMediaDirIsError();
  void validateCollection_mediaPathIsFileNotDirIsError();
  void validateCollection_invalidParentIndexIsError();
  void validateCollection_smallItemDimensionsWarn();
  void validateCollection_zeroGridWidthWarns();
  void validateCollection_optionalArtworkDirMissingIsWarning();
  void validateCollection_collectionPlaceholderMediaDirIsExpanded();

  // Kartend-ufxpm: one warning per launcher state (the non-executable case
  // used to ALSO emit a contradictory "does not exist"), and cycles longer
  // than self-parenting are reported.
  void validateCollection_nonExecutableLauncherWarnsOnce();
  void validateCollection_missingPathLauncherWarnsOnce();
  void validateAllCollections_twoCycleIsError();

  void validateAllCollections_emptyListWarns();
  void validateAllCollections_parentIndexOutOfRangeIsError();
  void validateAllCollections_selfParentIsError();
  void validateAllCollections_duplicateNameAtSameLevelWarns();
  void validateAllCollections_uuidCollisionIsError();
  void validateAllCollections_aggregatesPerCollectionIssues();

  void isCommandInPath_findsCommonShellBuiltin();
  void isCommandInPath_returnsFalseForGarbage();
};

namespace {

CollectionConfig makeLeaf(const QString &name, const QString &mediaDir = {}) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  c.parentCollectionIndex = -1;
  c.gridLayout.gridWidth = 5;
  c.gridLayout.itemWidth = 200;
  c.gridLayout.itemHeight = 300;
  return c;
}

} // namespace

void TestConfigValidation::validationResult_addError_marksInvalidAndCollects() {
  ConfigValidation::ValidationResult r;
  QVERIFY(r.valid);
  QVERIFY(!r.hasIssues());
  r.addError(QStringLiteral("boom"));
  QVERIFY(!r.valid);
  QVERIFY(r.hasIssues());
  QCOMPARE(r.errors.size(), 1);
  QCOMPARE(r.errors.first(), QStringLiteral("boom"));
  QVERIFY(r.warnings.isEmpty());
}

void TestConfigValidation::validationResult_addWarning_keepsValid() {
  ConfigValidation::ValidationResult r;
  r.addWarning(QStringLiteral("careful"));
  QVERIFY(r.valid);
  QVERIFY(r.hasIssues());
  QCOMPARE(r.warnings.size(), 1);
}

void TestConfigValidation::validationResult_hasIssues_reflectsContent() {
  ConfigValidation::ValidationResult r;
  QVERIFY(!r.hasIssues());
  r.addWarning(QStringLiteral("x"));
  QVERIFY(r.hasIssues());
}

void TestConfigValidation::validateCollection_missingNameIsError() {
  CollectionConfig c = makeLeaf(QString());
  const auto r = ConfigValidation::validateCollection(c, 0);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("missing name"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_emptyMediaDirOnLeafIsWarning() {
  CollectionConfig c = makeLeaf(QStringLiteral("Leaf"));
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/false);
  QVERIFY(r.valid); // warning only, not error
  bool found = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("no media directory"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_emptyMediaDirOnContainerIsAccepted() {
  CollectionConfig c = makeLeaf(QStringLiteral("Shell"));
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/true);
  QVERIFY(r.valid);
  for (const QString &w : r.warnings) {
    QVERIFY2(!w.contains(QStringLiteral("no media directory")),
             qUtf8Printable(QStringLiteral("Container shouldn't warn about media dir: ") + w));
  }
}

void TestConfigValidation::validateCollection_missingMediaDirIsError() {
  CollectionConfig c =
      makeLeaf(QStringLiteral("Test"), QStringLiteral("/this/path/does/not/exist/kartend-test"));
  const auto r = ConfigValidation::validateCollection(c, 0);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("does not exist"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_collectionPlaceholderMediaDirIsExpanded() {
  // Kartend-k375: a %collection%-templated media dir must validate against its
  // real expanded location (PathUtils::expandPath), not the literal
  // "%collection%" path which would always report "does not exist".
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString name = QStringLiteral("MyCollection");
  QVERIFY(QDir(tmp.path()).mkdir(name));

  CollectionConfig c = makeLeaf(name, tmp.path() + QStringLiteral("/%collection%"));
  const auto r = ConfigValidation::validateCollection(c, 0);

  // The expanded path (<tmp>/MyCollection) exists, so no "does not exist" media
  // error and the collection validates cleanly.
  for (const QString &e : r.errors) {
    QVERIFY2(!e.contains(QStringLiteral("does not exist")), qPrintable(e));
  }
  QVERIFY(r.valid);
}

void TestConfigValidation::validateCollection_mediaPathIsFileNotDirIsError() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString filePath = tmp.filePath(QStringLiteral("not-a-dir.txt"));
  QFile f(filePath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();

  CollectionConfig c = makeLeaf(QStringLiteral("Test"), filePath);
  const auto r = ConfigValidation::validateCollection(c, 0);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("not a directory"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_invalidParentIndexIsError() {
  CollectionConfig c = makeLeaf(QStringLiteral("Test"));
  c.parentCollectionIndex = -42;
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/true);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("invalid parentCollectionIndex"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_smallItemDimensionsWarn() {
  CollectionConfig c = makeLeaf(QStringLiteral("Test"));
  c.gridLayout.itemWidth = 30;
  c.gridLayout.itemHeight = 30;
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/true);
  bool found = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("very small item dimensions"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_zeroGridWidthWarns() {
  CollectionConfig c = makeLeaf(QStringLiteral("Test"));
  c.gridLayout.gridWidth = 0;
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/true);
  bool found = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("gridWidth less than 1"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_optionalArtworkDirMissingIsWarning() {
  CollectionConfig c = makeLeaf(QStringLiteral("Test"));
  c.artworkDirectory = QStringLiteral("/nonexistent/kartend-test/artwork");
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/true);
  // Missing artwork dir is a warning, not an error
  QVERIFY(r.valid);
  bool found = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("artwork directory does not exist"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateCollection_nonExecutableLauncherWarnsOnce() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString launcher = dir.filePath(QStringLiteral("launcher.sh"));
  {
    QFile f(launcher);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\n");
  }
  QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner); // present, not executable

  CollectionConfig c = makeLeaf(QStringLiteral("Test"), dir.path());
  c.launcher.launcherPath = launcher;
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/false);

  int launcherWarnings = 0;
  bool sawNotExecutable = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("launcher"))) {
      ++launcherWarnings;
      sawNotExecutable = sawNotExecutable || w.contains(QStringLiteral("not executable"));
    }
  }
  // Exactly ONE launcher warning, and it names the real state — previously a
  // second "does not exist" fired for the same launcher.
  QCOMPARE(launcherWarnings, 1);
  QVERIFY(sawNotExecutable);
}

void TestConfigValidation::validateCollection_missingPathLauncherWarnsOnce() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  CollectionConfig c = makeLeaf(QStringLiteral("Test"), dir.path());
  c.launcher.launcherPath = dir.filePath(QStringLiteral("gone/launcher"));
  const auto r = ConfigValidation::validateCollection(c, 0, /*isContainer=*/false);

  int launcherWarnings = 0;
  bool sawDoesNotExist = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("launcher"))) {
      ++launcherWarnings;
      sawDoesNotExist = sawDoesNotExist || w.contains(QStringLiteral("does not exist"));
    }
  }
  QCOMPARE(launcherWarnings, 1);
  QVERIFY(sawDoesNotExist);
}

void TestConfigValidation::validateAllCollections_twoCycleIsError() {
  // Corrupt input: 1 → 2 → 1 among subcollections. The old check only
  // caught parent == i, so this passed validation unreported.
  QList<CollectionConfig> collections;
  collections << makeLeaf(QStringLiteral("Root"));
  CollectionConfig b = makeLeaf(QStringLiteral("B"));
  b.parentCollectionIndex = 2;
  b.isSubcollection = true;
  collections << b;
  CollectionConfig c = makeLeaf(QStringLiteral("C"));
  c.parentCollectionIndex = 1;
  c.isSubcollection = true;
  collections << c;

  const auto r = ConfigValidation::validateAllCollections(collections);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("circular parent chain"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateAllCollections_emptyListWarns() {
  const auto r = ConfigValidation::validateAllCollections({});
  QVERIFY(r.valid);
  QVERIFY(!r.warnings.isEmpty());
  QVERIFY(r.warnings.first().contains(QStringLiteral("No collections")));
}

void TestConfigValidation::validateAllCollections_parentIndexOutOfRangeIsError() {
  QList<CollectionConfig> collections;
  CollectionConfig a = makeLeaf(QStringLiteral("A"));
  a.parentCollectionIndex = 99; // out of range
  collections << a;
  const auto r = ConfigValidation::validateAllCollections(collections);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("invalid parent index"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateAllCollections_selfParentIsError() {
  QList<CollectionConfig> collections;
  CollectionConfig a = makeLeaf(QStringLiteral("Self"));
  a.parentCollectionIndex = 0;
  collections << a;
  const auto r = ConfigValidation::validateAllCollections(collections);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("itself as parent"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateAllCollections_duplicateNameAtSameLevelWarns() {
  QList<CollectionConfig> collections;
  collections << makeLeaf(QStringLiteral("Dup"));
  collections << makeLeaf(QStringLiteral("Dup"));
  const auto r = ConfigValidation::validateAllCollections(collections);
  bool found = false;
  for (const QString &w : r.warnings) {
    if (w.contains(QStringLiteral("Duplicate collection name"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateAllCollections_uuidCollisionIsError() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString sharedPath = tmp.path();

  QList<CollectionConfig> collections;
  collections << makeLeaf(QStringLiteral("Same"), sharedPath);
  collections << makeLeaf(QStringLiteral("Same"), sharedPath);
  const auto r = ConfigValidation::validateAllCollections(collections);
  QVERIFY(!r.valid);
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("UUID collision"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::validateAllCollections_aggregatesPerCollectionIssues() {
  QList<CollectionConfig> collections;
  collections << makeLeaf(QString()); // missing name → error
  collections << makeLeaf(QStringLiteral("Ok"));
  const auto r = ConfigValidation::validateAllCollections(collections);
  QVERIFY(!r.valid);
  // The per-collection error from the first entry must surface in the
  // aggregate's errors list.
  bool found = false;
  for (const QString &e : r.errors) {
    if (e.contains(QStringLiteral("missing name"))) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestConfigValidation::isCommandInPath_findsCommonShellBuiltin() {
  // 'sh' exists in PATH on every realistic Linux/macOS test runner
  QVERIFY(ConfigValidation::isCommandInPath(QStringLiteral("sh")));
}

void TestConfigValidation::isCommandInPath_returnsFalseForGarbage() {
  QVERIFY(!ConfigValidation::isCommandInPath(QStringLiteral("kartend_no_such_executable_zzzzzzz")));
}

QTEST_MAIN(TestConfigValidation)
#include "test_configvalidation.moc"
