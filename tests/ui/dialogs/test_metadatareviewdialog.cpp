// Kartend-0eeuk: MetadataReviewDialog queue-walking decisions. The dialog
// owns the cursor over a MetadataQueue::Entry list; these tests cover the
// decision branches: Skip advances, a cancelled edit keeps the cursor put
// (and never reloads), a saved edit that left fields missing keeps the
// entry with its refreshed missing-field list, a fully-populated reload
// drops the entry so the next one slides into the same cursor slot, and
// queue exhaustion disables the action buttons. The Edit/Reload bridges
// are plain closures, so no DB or child dialog is involved. Driven
// headlessly — the dialog is never shown or exec()'d.

#include "metadatareviewdialog.h"

#include "itemmetadata.h"
#include "metadataqueue.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTest>

namespace {

MetadataQueue::Entry makeEntry(const QString &path, const QString &name,
                               bool hasArtworkOnDisk = true) {
  MetadataQueue::Entry e;
  e.filePath = path;
  e.collectionUuid = QStringLiteral("uuid-1");
  e.itemName = name;
  e.hasArtworkOnDisk = hasArtworkOnDisk;
  e.missingFields = {QStringLiteral("Title"), QStringLiteral("Description"),
                     QStringLiteral("Genre")};
  return e;
}

QPushButton *buttonWithText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

QPushButton *editButton(const QWidget &dlg) {
  return buttonWithText(dlg, QStringLiteral("Edit metadata…"));
}

QPushButton *skipButton(const QWidget &dlg) {
  return buttonWithText(dlg, QStringLiteral("Skip"));
}

QLabel *labelContaining(const QWidget &dlg, const QString &needle) {
  const auto labels = dlg.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->text().contains(needle)) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestMetadataReviewDialog : public QObject {
  Q_OBJECT

private slots:
  void rendersFirstEntryWithProgressAndMissingFields();
  void skipAdvancesAndExhaustionDisablesActions();
  void cancelledEditKeepsCursorAndSkipsReload();
  void savedEditStillMissingKeepsEntryWithRefreshedFields();
  void savedEditFullyPopulatedDropsEntry();
  void editButtonDisabledWithoutHandler();
};

void TestMetadataReviewDialog::rendersFirstEntryWithProgressAndMissingFields() {
  MetadataReviewDialog dlg;
  dlg.setQueue({makeEntry(QStringLiteral("/m/a.mkv"), QStringLiteral("Alpha")),
                makeEntry(QStringLiteral("/m/b.mkv"), QStringLiteral("Beta"))},
               nullptr, nullptr);

  QVERIFY(labelContaining(dlg, QStringLiteral("Reviewing item 1 of 2")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Alpha")));
  QVERIFY(labelContaining(dlg, QStringLiteral("/m/a.mkv")));
  QLabel *missing = labelContaining(dlg, QStringLiteral("Missing:"));
  QVERIFY(missing);
  QVERIFY(missing->text().contains(QStringLiteral("Title")));
  QVERIFY(missing->text().contains(QStringLiteral("Genre")));
}

void TestMetadataReviewDialog::skipAdvancesAndExhaustionDisablesActions() {
  MetadataReviewDialog dlg;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/a.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/b.mkv"), QStringLiteral("Beta"))},
      [](const QString &, const QString &) { return true; }, nullptr);

  QPushButton *skip = skipButton(dlg);
  QVERIFY(skip);
  skip->click();
  QVERIFY(labelContaining(dlg, QStringLiteral("Reviewing item 2 of 2")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Beta")));

  skip->click();
  QVERIFY(labelContaining(dlg, QStringLiteral("No more items to review.")));
  QVERIFY(!editButton(dlg)->isEnabled());
  QVERIFY(!skip->isEnabled());
}

void TestMetadataReviewDialog::cancelledEditKeepsCursorAndSkipsReload() {
  MetadataReviewDialog dlg;
  int reloadCalls = 0;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/a.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/b.mkv"), QStringLiteral("Beta"))},
      [](const QString &, const QString &) { return false; }, // user cancelled
      [&reloadCalls](const QString &, const QString &) {
        ++reloadCalls;
        return ItemMetadataStore::ItemMetadata{};
      });

  editButton(dlg)->click();
  QCOMPARE(reloadCalls, 0); // nothing was saved, so nothing to re-evaluate
  QVERIFY(labelContaining(dlg, QStringLiteral("Reviewing item 1 of 2")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Alpha")));
}

void TestMetadataReviewDialog::savedEditStillMissingKeepsEntryWithRefreshedFields() {
  MetadataReviewDialog dlg;
  // Reload reports the title now filled in, but description/genre still
  // blank — the entry must stay at the cursor with an updated missing list.
  ItemMetadataStore::ItemMetadata partial;
  partial.title = QStringLiteral("Alpha");
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/a.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/b.mkv"), QStringLiteral("Beta"))},
      [](const QString &, const QString &) { return true; },
      [partial](const QString &, const QString &) { return partial; });

  editButton(dlg)->click();
  QVERIFY(labelContaining(dlg, QStringLiteral("Reviewing item 1 of 2"))); // still item 1
  QLabel *missing = labelContaining(dlg, QStringLiteral("Missing:"));
  QVERIFY(missing);
  QVERIFY(!missing->text().contains(QStringLiteral("Title"))); // resolved
  QVERIFY(missing->text().contains(QStringLiteral("Description")));
  QVERIFY(missing->text().contains(QStringLiteral("Genre")));
}

void TestMetadataReviewDialog::savedEditFullyPopulatedDropsEntry() {
  MetadataReviewDialog dlg;
  ItemMetadataStore::ItemMetadata complete;
  complete.title = QStringLiteral("Alpha");
  complete.description = QStringLiteral("desc");
  complete.genre = QStringLiteral("documentary");
  QStringList editedPaths;
  dlg.setQueue(
      {makeEntry(QStringLiteral("/m/a.mkv"), QStringLiteral("Alpha")),
       makeEntry(QStringLiteral("/m/b.mkv"), QStringLiteral("Beta"))},
      [&editedPaths](const QString &filePath, const QString &) {
        editedPaths << filePath;
        return true;
      },
      [complete](const QString &, const QString &) { return complete; });

  editButton(dlg)->click();
  QCOMPARE(editedPaths, QStringList{QStringLiteral("/m/a.mkv")});
  // Entry dropped — the queue is now one item long and Beta occupies the
  // same cursor slot.
  QVERIFY(labelContaining(dlg, QStringLiteral("Reviewing item 1 of 1")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Beta")));
}

void TestMetadataReviewDialog::editButtonDisabledWithoutHandler() {
  MetadataReviewDialog dlg;
  dlg.setQueue({makeEntry(QStringLiteral("/m/a.mkv"), QStringLiteral("Alpha"))}, nullptr, nullptr);
  QVERIFY(!editButton(dlg)->isEnabled());
  QVERIFY(skipButton(dlg)->isEnabled());
}

QTEST_MAIN(TestMetadataReviewDialog)
#include "test_metadatareviewdialog.moc"
