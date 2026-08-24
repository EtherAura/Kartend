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
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QObject>
#include <QRegularExpression>
#include <QScrollArea>
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
  void scrapedCollectionFieldsRenderInBothSummarySurfaces();
  void fileTabShowsCollectionOnDiskStory();
  void tabRoundTripKeepsCollectionOverview();
  void summaryCardCapNeverClipsContent();
  void fileTabItemViewIsCompact();
  void collectionSurfacesShowScrapedImageGallery();

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
  // Collection tab follows it via the dedicated owner channel
  // (Kartend-6i10t; user decision 2026-08-23).
  DetailsPane::CollectionSummary owner;
  owner.name = QStringLiteral("Famicom");
  owner.type = QStringLiteral("Games");
  owner.parentName = QStringLiteral("Games");
  pane.setOwnerCollectionSummary(owner);
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Famicom"));

  // Clearing the selection returns the tab to the viewed collection.
  pane.clearOwnerCollectionSummary();
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));

  // A subcollection TILE, by contrast, does NOT surface on this tab — its
  // parent (the viewed collection) does; the tile's own info belongs to
  // the Item area (Kartend-6i10t user report: root-level tiles showed
  // their own info here).
  DetailsPane::CollectionSummary tile;
  tile.name = QStringLiteral("Sega");
  tile.parentName = QStringLiteral("Games");
  pane.setSelectionCollectionSummary(tile);
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));
  pane.setActiveTab(DetailsPaneTab::Item);
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Sega"));
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
  pane.setOwnerCollectionSummary(owner);
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

void TestDetailsPaneEmptyStates::scrapedCollectionFieldsRenderInBothSummarySurfaces() {
  DetailsPane pane;
  DetailsPane::CollectionSummary s = gamesSummary();
  s.scrapedDescription = QStringLiteral("A shelf of open-source classics.");
  s.scrapedManufacturer = QStringLiteral("Various");
  s.scrapedReleaseDate = QStringLiteral("1991");
  // Kartend-6i10t: the wider fact set renders alongside the original trio.
  s.scrapedCountry = QStringLiteral("Norway");
  s.scrapedDeveloper = QStringLiteral("Hobbyists United");
  s.scrapedPublisher = QStringLiteral("Freeware Press");
  s.scrapedGenre = QStringLiteral("Arcade");
  s.scrapedWebsite = QStringLiteral("https://classics.example");
  // Kartend-5b5r1: the spec sheet renders as plain rows after the facts.
  s.scrapedSpecs = {{QStringLiteral("CPU"), QStringLiteral("Motorola 68000")},
                    {QStringLiteral("Units sold"), QStringLiteral("30,750,000")}};
  pane.setCollectionSummary(s);

  const auto textsContain = [&pane](const QString &needle) {
    const auto labels = pane.findChildren<QLabel *>();
    return std::any_of(labels.cbegin(), labels.cend(),
                       [&needle](const QLabel *l) { return l->text().contains(needle); });
  };

  // Item-area overview (no selection).
  pane.setActiveTab(DetailsPaneTab::Item);
  QVERIFY(textsContain(QStringLiteral("open-source classics")));
  QVERIFY(textsContain(QStringLiteral("Various")));
  QVERIFY(textsContain(QStringLiteral("1991")));
  QVERIFY(textsContain(QStringLiteral("Norway")));
  QVERIFY(textsContain(QStringLiteral("Hobbyists United")));
  QVERIFY(textsContain(QStringLiteral("Freeware Press")));
  QVERIFY(textsContain(QStringLiteral("Arcade")));
  QVERIFY(textsContain(QStringLiteral("Motorola 68000")));
  QVERIFY(textsContain(QStringLiteral("30,750,000")));
  // The website renders as an anchor that opens externally — never as an
  // inert text run (Kartend-6i10t).
  {
    const auto labels = pane.findChildren<QLabel *>();
    const QLabel *websiteRow = nullptr;
    for (const QLabel *l : labels) {
      if (l->text().contains(QStringLiteral("classics.example"))) websiteRow = l;
    }
    QVERIFY(websiteRow);
    QVERIFY(websiteRow->text().contains(QStringLiteral("<a href=\"https://classics.example\"")));
    QVERIFY(websiteRow->openExternalLinks());
  }

  // Collection tab.
  pane.setActiveTab(DetailsPaneTab::Collection);
  QVERIFY(textsContain(QStringLiteral("open-source classics")));
  QVERIFY(textsContain(QStringLiteral("Various")));
  QVERIFY(textsContain(QStringLiteral("Norway")));
  QVERIFY(textsContain(QStringLiteral("https://classics.example")));
}

void TestDetailsPaneEmptyStates::fileTabShowsCollectionOnDiskStory() {
  // Kartend-6i10t: with a collection selected (no item), the File tab tells
  // the collection's on-disk story instead of "No item selected".
  DetailsPane pane;
  DetailsPane::CollectionSummary s = gamesSummary();
  s.totalSizeBytes = 5LL * 1024 * 1024 * 1024; // 5 GiB
  pane.setCollectionSummary(s);
  pane.setActiveTab(DetailsPaneTab::File);

  const auto textsContain = [&pane](const QString &needle) {
    const auto labels = pane.findChildren<QLabel *>();
    return std::any_of(labels.cbegin(), labels.cend(),
                       [&needle](const QLabel *l) { return l->text().contains(needle); });
  };
  QVERIFY(!textsContain(QStringLiteral("No item selected")));
  QVERIFY(textsContain(s.mediaDirectory));
  QVERIFY(textsContain(QLocale().formattedDataSize(s.totalSizeBytes)));
  QVERIFY(textsContain(QStringLiteral("Last scanned")));

  // An unknown size (-1 — DB absent) hides the row rather than showing 0.
  DetailsPane pane2;
  DetailsPane::CollectionSummary s2 = gamesSummary();
  s2.totalSizeBytes = -1;
  pane2.setCollectionSummary(s2);
  pane2.setActiveTab(DetailsPaneTab::File);
  const auto labels2 = pane2.findChildren<QLabel *>();
  QVERIFY(std::none_of(labels2.cbegin(), labels2.cend(), [](const QLabel *l) {
    return l->text().contains(QStringLiteral("Size on disk"));
  }));
}

void TestDetailsPaneEmptyStates::tabRoundTripKeepsCollectionOverview() {
  // User report (Kartend-6i10t screenshots): with a subcollection selected,
  // Item tab -> Collection tab -> Item tab lost the overview card.
  DetailsPane pane;
  DetailsPane::CollectionSummary viewed = gamesSummary();
  pane.setCollectionSummary(viewed);
  DetailsPane::CollectionSummary sub = gamesSummary();
  sub.name = QStringLiteral("Sony");
  sub.scrapedDescription = QStringLiteral("A Japanese conglomerate.");
  pane.setSelectionCollectionSummary(sub);

  const auto textsContain = [&pane](const QString &needle) {
    const auto labels = pane.findChildren<QLabel *>();
    return std::any_of(labels.cbegin(), labels.cend(), [&needle, &pane](const QLabel *l) {
      return l->isVisibleTo(&pane) && l->text().contains(needle);
    });
  };

  pane.setActiveTab(DetailsPaneTab::Item);
  QVERIFY(textsContain(QStringLiteral("Japanese conglomerate")));
  // The Collection tab shows the tile's PARENT (the viewed collection),
  // not the tile itself (Kartend-6i10t). Cleared rows die via deleteLater —
  // flush deferred deletes before asserting their absence.
  pane.setActiveTab(DetailsPaneTab::Collection);
  QCOMPARE(label(pane, "itemNameValue")->text(), QStringLiteral("Games"));
  QTest::qWait(0);
  QVERIFY(!textsContain(QStringLiteral("Japanese conglomerate")));
  pane.setActiveTab(DetailsPaneTab::Item);
  QVERIFY2(textsContain(QStringLiteral("Japanese conglomerate")),
           "overview card lost after Item -> Collection -> Item round trip");
  QVERIFY(textsContain(QStringLiteral("Sony")));

  // Same round trip through the File tab.
  pane.setActiveTab(DetailsPaneTab::File);
  pane.setActiveTab(DetailsPaneTab::Item);
  QVERIFY2(textsContain(QStringLiteral("Japanese conglomerate")),
           "overview card lost after Item -> File -> Item round trip");
}

void TestDetailsPaneEmptyStates::summaryCardCapNeverClipsContent() {
  // User decision (Kartend-6i10t, 2026-08-23): summary cards FILL the
  // sidebar's height like item details do — no hug-to-content cap. The
  // rows stay top-aligned inside the full-height card (the backdrop's
  // trailing stretch), so nothing can clip and nothing floats.
  DetailsPane pane;
  pane.resize(340, 900);
  pane.show();
  DetailsPane::CollectionSummary s = gamesSummary();
  s.scrapedDescription =
      QStringLiteral("A very long description that wraps across many narrow sidebar lines: "
                     "games are activities defined by challenges and rules, played for "
                     "enjoyment, competition, or development, in formats from board and "
                     "card games to video games and sports, debated by scholars at length.");
  s.scrapedCountry = QStringLiteral("Japan");
  s.scrapedWebsite = QStringLiteral("https://example.org/a/rather/long/path");
  pane.setCollectionSummary(s);
  pane.setActiveTab(DetailsPaneTab::Collection);
  QTest::qWait(50);

  auto *scroll = pane.findChild<QScrollArea *>(QStringLiteral("metadataScroll"));
  auto *backdrop = pane.findChild<QFrame *>(QStringLiteral("metadataBackdrop"));
  QVERIFY(scroll && backdrop);
  QCOMPARE(scroll->maximumHeight(), QWIDGETSIZE_MAX); // no height clamp
  // The card genuinely fills leftover pane height rather than hugging the
  // rows: with a 900px pane the scroll must be far taller than the rows.
  QVERIFY2(scroll->height() > backdrop->sizeHint().height(),
           qPrintable(QStringLiteral("card %1 does not fill pane beyond rows %2")
                          .arg(scroll->height())
                          .arg(backdrop->sizeHint().height())));

  // Same contract on the File tab's collection surface.
  pane.setActiveTab(DetailsPaneTab::File);
  QTest::qWait(50);
  QCOMPARE(scroll->maximumHeight(), QWIDGETSIZE_MAX);
}

void TestDetailsPaneEmptyStates::fileTabItemViewIsCompact() {
  // Kartend-6i10t user report ("the File page can be tightened up"): with
  // the details container hidden, leftover height used to spread the
  // path/size/modified rows across the whole pane, and the section header
  // duplicated the tab title.
  const QString path = stageFile(QStringLiteral("compact.mp4"));
  QVERIFY(!path.isEmpty());
  DetailsPane pane;
  pane.resize(340, 900);
  pane.show();
  pane.setMetadata(path, QStringLiteral("Compact"));
  pane.setActiveTab(DetailsPaneTab::File);
  QTest::qWait(50);

  QLabel *title = label(pane, "fileInfoTitle");
  QVERIFY(title);
  QVERIFY(!title->isVisibleTo(&pane)); // tab title already says it

  QLabel *lastRow = label(pane, "fileExtensionValue");
  QVERIFY(lastRow);
  const int bottom = lastRow->mapTo(&pane, lastRow->rect().bottomLeft()).y();
  QVERIFY2(bottom < pane.height() / 2,
           qPrintable(QStringLiteral("file rows spread to y=%1 in a %2px pane")
                          .arg(bottom)
                          .arg(pane.height())));
}

void TestDetailsPaneEmptyStates::collectionSurfacesShowScrapedImageGallery() {
  // Kartend-5b5r1 (user decision): the scraped system images feed the same
  // gallery strip items get, on the Item-area overview AND the Collection
  // tab; no entries hides it.
  // Real decodable PNGs: the gallery skips entries whose bytes aren't an
  // image (the libqpdf-abort guard), so 1-byte stand-ins would render an
  // empty strip.
  const QString a = m_dir.filePath(QStringLiteral("platform_42.png"));
  const QString b = m_dir.filePath(QStringLiteral("collection_u1.png"));
  {
    QImage img(4, 4, QImage::Format_RGB32);
    img.fill(Qt::darkRed);
    QVERIFY(img.save(a));
    QVERIFY(img.save(b));
  }
  DetailsPane pane;
  pane.resize(340, 900);
  pane.show();
  DetailsPane::CollectionSummary s = gamesSummary();
  s.galleryEntries = {{QStringLiteral("wheel"), a, false}, {QStringLiteral("photo"), b, false}};
  pane.setCollectionSummary(s);
  pane.setActiveTab(DetailsPaneTab::Item);
  QTest::qWait(50);
  auto *gallery = pane.findChild<QWidget *>(QStringLiteral("galleryBackdrop"));
  QVERIFY(gallery);
  QVERIFY(gallery->isVisibleTo(&pane));

  pane.setActiveTab(DetailsPaneTab::Collection);
  QTest::qWait(50);
  QVERIFY(gallery->isVisibleTo(&pane));

  // No scraped images → no gallery strip.
  DetailsPane::CollectionSummary bare = gamesSummary();
  pane.setCollectionSummary(bare);
  QTest::qWait(50);
  QVERIFY(!gallery->isVisibleTo(&pane));
}

QTEST_MAIN(TestDetailsPaneEmptyStates)
#include "test_detailspaneemptystates.moc"
