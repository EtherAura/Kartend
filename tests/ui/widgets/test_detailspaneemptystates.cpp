// DetailsPane's empty-state rendering (Kartend-um69l): the Item area
// renders an item-styled COLLECTION overview when nothing is selected or a
// subcollection tile carries the selection, and an unscraped item keeps the
// full details skeleton (dimmed placeholder rows) instead of collapsing to
// a bare file-info list.
//
// Constructed headlessly and never shown, same pattern as
// test_detailspanefileinfo.cpp: labels are reached by objectName so the
// test needs no friendship with DetailsPane.
#include <QFrame>
#include <QLabel>
#include <QObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include "detailspane.h"

class TestDetailsPaneEmptyStates : public QObject {
  Q_OBJECT

private slots:
  void noSelectionRendersCollectionOverview();
  void subcollectionSummaryWinsAndClearsWithSelection();
  void collectionSwitchDropsStaleSubcollectionSummary();
  void unscrapedItemShowsPlaceholderSkeleton();
  void overviewShowsNoFilesystemPaths();
  void collectionTabFollowsSelectionOwner();
  void collectionTabCardKeepsUsableHeightAfterItemRender();

private:
  QTemporaryDir m_dir;

  QString stageFile(const QString &name) {
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
      return {};
    }
    f.write(QByteArrayLiteral("x"));
    f.close();
    return path;
  }

  static QLabel *label(DetailsPane &pane, const char *objectName) {
    return pane.findChild<QLabel *>(QLatin1String(objectName));
  }

  static DetailsPane::CollectionSummary gamesSummary() {
    DetailsPane::CollectionSummary s;
    s.name = QStringLiteral("Games");
    s.type = QStringLiteral("Games");
    s.itemCount = 21;
    s.mediaDirectory = QStringLiteral("/secret/media/root");
    s.artworkDirectory = QStringLiteral("/secret/artwork/root");
    return s;
  }
};

void TestDetailsPaneEmptyStates::noSelectionRendersCollectionOverview() {
  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setCollectionSummary(gamesSummary());

  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));
  QCOMPARE(label(pane, "titleLabel")->text(), QStringLiteral("Collection"));
}

void TestDetailsPaneEmptyStates::subcollectionSummaryWinsAndClearsWithSelection() {
  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setCollectionSummary(gamesSummary());

  DetailsPane::CollectionSummary child;
  child.name = QStringLiteral("Open Source Games");
  child.type = QStringLiteral("Games");
  child.itemCount = 21;
  child.parentName = QStringLiteral("Games");
  pane.setSelectionCollectionSummary(child);

  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Open Source Games"));
  QCOMPARE(label(pane, "titleLabel")->text(), QStringLiteral("Subcollection"));

  // A real item selection replaces the overview…
  const QString path = stageFile(QStringLiteral("game.kartlink"));
  QVERIFY(!path.isEmpty());
  pane.setMetadata(path, QStringLiteral("A Game"));
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("A Game"));
  QVERIFY(!pane.selectionSummaryForTesting().isValid());

  // …and deselection falls back to the CURRENT collection, not the child.
  pane.clearMetadata();
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));
  QCOMPARE(label(pane, "titleLabel")->text(), QStringLiteral("Collection"));
}

void TestDetailsPaneEmptyStates::collectionSwitchDropsStaleSubcollectionSummary() {
  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setCollectionSummary(gamesSummary());

  DetailsPane::CollectionSummary child;
  child.name = QStringLiteral("Open Source Games");
  pane.setSelectionCollectionSummary(child);
  QVERIFY(pane.selectionSummaryForTesting().isValid());

  DetailsPane::CollectionSummary films;
  films.name = QStringLiteral("Films");
  films.type = QStringLiteral("Videos");
  films.itemCount = 13;
  pane.setCollectionSummary(films);

  QVERIFY(!pane.selectionSummaryForTesting().isValid());
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Films"));
}

void TestDetailsPaneEmptyStates::unscrapedItemShowsPlaceholderSkeleton() {
  const QString path = stageFile(QStringLiteral("clip.mp4"));
  QVERIFY(!path.isEmpty());

  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setMetadata(path, QStringLiteral("Clip"));
  pane.setExtendedMetadata({});

  // The skeleton keeps the details card in play (it used to be hidden for
  // unscraped items) with the dimmed stand-in rows in place.
  auto *backdrop = pane.findChild<QFrame *>(QStringLiteral("metadataBackdrop"));
  QVERIFY(backdrop);
  bool sawPlaceholderDescription = false;
  bool sawPlaceholderRow = false;
  const auto labels = pane.findChildren<QLabel *>();
  for (const QLabel *l : labels) {
    if (l->text().contains(QStringLiteral("No description available"))) {
      sawPlaceholderDescription = true;
    }
    // The dash rows render as rich text "<b>Genre:</b> —" etc.
    if (l->text().contains(QStringLiteral("Genre")) && l->text().contains(QStringLiteral("—"))) {
      sawPlaceholderRow = true;
    }
  }
  QVERIFY(sawPlaceholderDescription);
  QVERIFY(sawPlaceholderRow);
}

void TestDetailsPaneEmptyStates::overviewShowsNoFilesystemPaths() {
  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setCollectionSummary(gamesSummary());

  // The Collection tab shows directories; the Item area's overview must
  // not (published captures carry it, and paths are the Collection tab's
  // job). The staged summary uses "/secret/…" paths that would be easy to
  // spot in any rendered label.
  const auto labels = pane.findChildren<QLabel *>();
  for (const QLabel *l : labels) {
    QVERIFY2(!l->text().contains(QStringLiteral("/secret/")),
             qPrintable(QStringLiteral("path leaked into overview: %1").arg(l->text())));
  }
}

void TestDetailsPaneEmptyStates::collectionTabFollowsSelectionOwner() {
  DetailsPane pane;
  pane.setCollectionSummary(gamesSummary());
  pane.setActiveTab(DetailsPaneTab::Collection);
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));

  // An aggregated view pushes the selected item's OWNING collection — the
  // Collection tab follows it (user decision 2026-08-23).
  DetailsPane::CollectionSummary owner;
  owner.name = QStringLiteral("Famicom");
  owner.type = QStringLiteral("Games");
  owner.parentName = QStringLiteral("Games");
  pane.setSelectionCollectionSummary(owner);
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Famicom"));

  // Clearing the selection returns the tab to the viewed collection.
  pane.clearSelectionCollectionSummary();
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));
}

void TestDetailsPaneEmptyStates::collectionTabCardKeepsUsableHeightAfterItemRender() {
  // Reproduces the collapsed Collection-tab card seen in the guest
  // (Kartend-um69l follow-up): after an item render, pushing the owning
  // collection's summary while the Collection tab is active left the
  // metadata card capped to a sliver. The card must stay tall enough to
  // show its rows.
  DetailsPane pane;
  pane.resize(400, 1000);
  pane.show();
  QTest::qWait(50);

  pane.setCollectionSummary(gamesSummary());
  pane.setActiveTab(DetailsPaneTab::Collection);
  QTest::qWait(50);

  auto *scroll = pane.findChild<QScrollArea *>(QStringLiteral("metadataScroll"));
  auto *backdrop = pane.findChild<QFrame *>(QStringLiteral("metadataBackdrop"));
  QVERIFY(scroll);
  QVERIFY(backdrop);
  const int firstCap = scroll->maximumHeight();
  qInfo("first render: cap=%d scrollH=%d hintH=%d", firstCap, scroll->height(),
        backdrop->sizeHint().height());

  // Aggregated flow: item selected (owner differs), then the owner push.
  const QString path = stageFile(QStringLiteral("probe.kartlink"));
  QVERIFY(!path.isEmpty());
  pane.setMetadata(path, QStringLiteral("A Game"));
  pane.setExtendedMetadata({});
  QTest::qWait(50);
  DetailsPane::CollectionSummary owner;
  owner.name = QStringLiteral("Open Source Games");
  owner.type = QStringLiteral("Games");
  owner.itemCount = 21;
  owner.mediaDirectory = QStringLiteral("/m/osg");
  pane.setSelectionCollectionSummary(owner);
  QTest::qWait(100);
  qInfo("after owner push: cap=%d scrollH=%d hintH=%d", scroll->maximumHeight(), scroll->height(),
        backdrop->sizeHint().height());

  // The card needs at least a few row-heights of space; the guest showed
  // ~15px. Use the row font's line spacing as the yardstick.
  const int rowH = pane.fontMetrics().lineSpacing();
  QVERIFY2(scroll->maximumHeight() >= rowH * 3,
           qPrintable(QStringLiteral("card capped to %1px (row height %2)")
                          .arg(scroll->maximumHeight())
                          .arg(rowH)));
}

QTEST_MAIN(TestDetailsPaneEmptyStates)
#include "test_detailspaneemptystates.moc"
