// Kartend-0eeuk: MediaTypeCheckboxBuilder construction + state mapping. The
// curated table in mediatypecheckboxbuilder.cpp is the source of truth for
// which ScreenScraper media types surface in the unified scrape panel; these
// tests pin the key-set contract the BatchScrapeRunner filter relies on:
// every key is lowercase (the runner matches `type.toLower()` against the
// set), the synthetic `_metadata` gate is present, the default-on set is
// exactly {_metadata, front}, and the Select all / Select none header
// buttons bulk-toggle every checkbox. Driven headlessly — never shown.

#include "mediatypecheckboxbuilder.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHash>
#include <QPushButton>
#include <QScopedPointer>
#include <QStringList>
#include <QTest>

namespace {

QPushButton *buttonWithText(const QWidget &root, const QString &text) {
  const auto buttons = root.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

} // namespace

class TestMediaTypeCheckboxBuilder : public QObject {
  Q_OBJECT

private slots:
  void buildsThirtyTwoUniqueLowercaseKeys();
  void defaultOnSetIsMetadataAndFrontOnly();
  void selectAllChecksEveryBox();
  void selectNoneClearsEveryBox();
  void applyProviderDefaultsReticksMediaButNotMetadata();
  void applyProviderDefaultsEmptySetIsNoOp();
};

void TestMediaTypeCheckboxBuilder::buildsThirtyTwoUniqueLowercaseKeys() {
  QHash<QString, QCheckBox *> checks;
  // Null parent is documented as supported; own the result locally.
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  // 32 curated entries (matches the class comment, corrected in
  // Kartend-29vam).
  // The QHash matching the checkbox count proves the keys are unique — a
  // duplicate key would orphan one checkbox outside the filter map.
  QCOMPARE(checks.size(), 32);
  QCOMPARE(group->findChildren<QCheckBox *>().size(), checks.size());

  // The synthetic metadata gate plus a few collapsed-alias anchors the
  // runtime filter set depends on (box-2D → front, sstitle → title,
  // manuel → manual).
  QVERIFY(checks.contains(QStringLiteral("_metadata")));
  QVERIFY(checks.contains(QStringLiteral("front")));
  QVERIFY(checks.contains(QStringLiteral("title")));
  QVERIFY(checks.contains(QStringLiteral("manual")));
  QVERIFY(checks.contains(QStringLiteral("screenshot")));

  // The runner lowercases asset types before matching — an uppercase key
  // here would silently never match.
  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    QVERIFY2(it.key() == it.key().toLower(),
             qPrintable(QStringLiteral("key not lowercase: %1").arg(it.key())));
    QVERIFY(it.value() != nullptr);
  }
}

void TestMediaTypeCheckboxBuilder::defaultOnSetIsMetadataAndFrontOnly() {
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    const bool expectedOn =
        it.key() == QStringLiteral("_metadata") || it.key() == QStringLiteral("front");
    QVERIFY2(it.value()->isChecked() == expectedOn,
             qPrintable(QStringLiteral("default for %1 wrong").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::selectAllChecksEveryBox() {
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  QPushButton *all = buttonWithText(*group, QStringLiteral("Select all"));
  QVERIFY(all);
  all->click();
  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    QVERIFY2(it.value()->isChecked(),
             qPrintable(QStringLiteral("%1 not checked after Select all").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::selectNoneClearsEveryBox() {
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  QPushButton *none = buttonWithText(*group, QStringLiteral("Select none"));
  QVERIFY(none);
  none->click(); // clears the two default-on boxes too
  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    QVERIFY2(!it.value()->isChecked(),
             qPrintable(QStringLiteral("%1 still checked after Select none").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::applyProviderDefaultsReticksMediaButNotMetadata() {
  // Kartend-6e90v: a provider's curated set re-ticks the media grid — keys
  // in the set on, every other media key off — while the synthetic
  // `_metadata` gate keeps its current state (the curated sets describe
  // media palettes, not whether text fields are wanted).
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  const QStringList steamish = {QStringLiteral("front"), QStringLiteral("screenshot"),
                                QStringLiteral("background"), QStringLiteral("video")};
  MediaTypeCheckboxBuilder::applyProviderDefaults(checks, steamish);

  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    if (it.key() == QStringLiteral("_metadata")) {
      QVERIFY(it.value()->isChecked()); // untouched table default
      continue;
    }
    QVERIFY2(it.value()->isChecked() == steamish.contains(it.key()),
             qPrintable(QStringLiteral("tick for %1 wrong after provider defaults").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::applyProviderDefaultsEmptySetIsNoOp() {
  // Providers without a curated set must leave the table defaults alone —
  // an empty list re-ticking everything off would nuke the front default.
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  MediaTypeCheckboxBuilder::applyProviderDefaults(checks, {});

  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    const bool expectedOn =
        it.key() == QStringLiteral("_metadata") || it.key() == QStringLiteral("front");
    QCOMPARE(it.value()->isChecked(), expectedOn);
  }
}

QTEST_MAIN(TestMediaTypeCheckboxBuilder)
#include "test_mediatypecheckboxbuilder.moc"
