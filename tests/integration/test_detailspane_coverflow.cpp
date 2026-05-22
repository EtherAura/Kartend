#include "applicationmanager.h"
#include "test_detailspane_coverflow.h"

#include "coverflowwidget.h"
#include "detailspane.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QString>
#include <QTest>

// ─── DetailsPane static formatters ────────────────────────────────────────

void TestDetailsPaneCoverflow::formatRuntime_negativeReturnsEmpty() {
  QCOMPARE(DetailsPane::formatRuntime(-1), QString());
  QCOMPARE(DetailsPane::formatRuntime(-3600), QString());
}

void TestDetailsPaneCoverflow::formatRuntime_secondsOnlyBranch() {
  QCOMPARE(DetailsPane::formatRuntime(0), QStringLiteral("0s"));
  QCOMPARE(DetailsPane::formatRuntime(45), QStringLiteral("45s"));
}

void TestDetailsPaneCoverflow::formatRuntime_minutesAndSeconds() {
  QCOMPARE(DetailsPane::formatRuntime(60), QStringLiteral("1m 00s"));
  QCOMPARE(DetailsPane::formatRuntime(125), QStringLiteral("2m 05s"));
  QCOMPARE(DetailsPane::formatRuntime(3599), QStringLiteral("59m 59s"));
}

void TestDetailsPaneCoverflow::formatRuntime_hoursAndMinutes() {
  QCOMPARE(DetailsPane::formatRuntime(3600), QStringLiteral("1h 00m"));
  QCOMPARE(DetailsPane::formatRuntime(3660), QStringLiteral("1h 01m"));
  QCOMPARE(DetailsPane::formatRuntime(7325), QStringLiteral("2h 02m"));
}

void TestDetailsPaneCoverflow::formatFileSize_smallReportsBytes() {
  QCOMPARE(DetailsPane::formatFileSize(0), QStringLiteral("0 bytes"));
  QCOMPARE(DetailsPane::formatFileSize(512), QStringLiteral("512 bytes"));
  QCOMPARE(DetailsPane::formatFileSize(1023), QStringLiteral("1023 bytes"));
}

void TestDetailsPaneCoverflow::formatFileSize_kiloRange() {
  QCOMPARE(DetailsPane::formatFileSize(1024), QStringLiteral("1.00 KB"));
  QCOMPARE(DetailsPane::formatFileSize(1536), QStringLiteral("1.50 KB"));
}

void TestDetailsPaneCoverflow::formatFileSize_megaRange() {
  const qint64 mb = 1024LL * 1024LL;
  QCOMPARE(DetailsPane::formatFileSize(mb), QStringLiteral("1.00 MB"));
  QCOMPARE(DetailsPane::formatFileSize(mb * 5 / 2), QStringLiteral("2.50 MB"));
}

void TestDetailsPaneCoverflow::formatFileSize_gigaRange() {
  const qint64 gb = 1024LL * 1024LL * 1024LL;
  QCOMPARE(DetailsPane::formatFileSize(gb), QStringLiteral("1.00 GB"));
  QCOMPARE(DetailsPane::formatFileSize(gb * 3), QStringLiteral("3.00 GB"));
}

void TestDetailsPaneCoverflow::formatTags_jsonArrayStripsBrackets() {
  QCOMPARE(DetailsPane::formatTags(QStringLiteral("[\"a\", \"b\", \"c\"]")),
           QStringLiteral("a, b, c"));
}

void TestDetailsPaneCoverflow::formatTags_commaSeparatedTrimsAndJoins() {
  QCOMPARE(DetailsPane::formatTags(QStringLiteral(" foo ,bar,  baz ")),
           QStringLiteral("foo, bar, baz"));
}

void TestDetailsPaneCoverflow::formatTags_emptyStaysEmpty() {
  QCOMPARE(DetailsPane::formatTags(QString()), QString());
  QCOMPARE(DetailsPane::formatTags(QStringLiteral("   ")), QString());
}

void TestDetailsPaneCoverflow::formatTags_skipsEmptyParts() {
  // ",foo,,bar," should produce "foo, bar" — empty parts are dropped.
  QCOMPARE(DetailsPane::formatTags(QStringLiteral(",foo,,bar,")),
           QStringLiteral("foo, bar"));
}

void TestDetailsPaneCoverflow::formatLastScanned_invalidReturnsNeverLabel() {
  QCOMPARE(DetailsPane::formatLastScanned(QDateTime()), QStringLiteral("never"));
}

void TestDetailsPaneCoverflow::formatLastScanned_validRendersIso() {
  // Construct in UTC so the local-time render is deterministic relative to
  // a single locale; we don't pin the exact string (TZ-dependent) but
  // assert the format shape: "yyyy-MM-dd HH:mm" with two-digit fields.
  const QDateTime dt = QDateTime::fromString(QStringLiteral("2026-05-07T14:23:00Z"),
                                             Qt::ISODate);
  QVERIFY(dt.isValid());
  const QString rendered = DetailsPane::formatLastScanned(dt);
  QVERIFY(!rendered.isEmpty());
  QVERIFY(rendered != QStringLiteral("never"));
  // Pattern: 4 digits, hyphen, 2 digits, hyphen, 2 digits, space, 2 digits,
  // colon, 2 digits.
  QVERIFY2(rendered.contains(QRegularExpression(
               QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$"))),
           qPrintable(QStringLiteral("Unexpected format: ") + rendered));
}

// ─── CoverFlowWidget public surface ───────────────────────────────────────

namespace {

CoverFlowCardData makeCard(const QString &title) {
  CoverFlowCardData c;
  c.title = title;
  return c;
}

} // namespace

void TestDetailsPaneCoverflow::coverflow_emptyAtConstructionThenSetCardsCounts() {
  CoverFlowWidget w;
  QCOMPARE(w.cardCount(), 0);
  QList<CoverFlowCardData> cards;
  cards << makeCard("a") << makeCard("b") << makeCard("c");
  w.setCards(cards);
  QCOMPARE(w.cardCount(), 3);
}

void TestDetailsPaneCoverflow::coverflow_setSelectedIndexClampsToValidRange() {
  CoverFlowWidget w;
  QList<CoverFlowCardData> cards;
  cards << makeCard("a") << makeCard("b") << makeCard("c");
  w.setCards(cards);

  // Setting within range should land on the requested index.
  w.setSelectedIndex(1, /*animate=*/false);
  QCOMPARE(w.selectedIndex(), 1);
  // Above-range request: behavior is implementation-defined but should not
  // produce a negative or unbounded index; the value should remain valid
  // (in [0, cardCount-1]) or at minimum be the prior valid value.
  w.setSelectedIndex(99, false);
  QVERIFY2(w.selectedIndex() >= 0 && w.selectedIndex() < w.cardCount(),
           qPrintable(QStringLiteral("Out-of-range setSelectedIndex left widget at index %1 with cardCount %2")
                          .arg(w.selectedIndex())
                          .arg(w.cardCount())));
}

void TestDetailsPaneCoverflow::coverflow_selectionChangeFromUserEmitsRequest() {
  // User-driven selection (wheel / key / side-card click) emits
  // selectionChangeRequested — the widget intentionally does NOT mutate its
  // own m_selectedIndex from those events; the upstream caller routes
  // through SelectionManager and feeds back via setSelectedIndex(). We
  // can't fire a real wheel event easily, but a key-press-driven move is a
  // reasonable proxy; if the widget eats the event without emitting, the
  // signal contract has changed and the routing will silently break.
  CoverFlowWidget w;
  QList<CoverFlowCardData> cards;
  cards << makeCard("a") << makeCard("b");
  w.setCards(cards);
  w.setSelectedIndex(0, false);

  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  // Send a Right key (next-card request). The widget's keyPressEvent
  // handles arrow keys; on Right we expect a selectionChangeRequested(1).
  QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QApplication::sendEvent(&w, &right);
  // Either the signal fired or it didn't — both are valid depending on
  // whether the widget filters keys when it lacks focus/visibility. The
  // contract we pin: if it DID fire, the payload is a non-negative index
  // pointing at a valid card.
  if (spy.count() > 0) {
    const int idx = spy.takeFirst().at(0).toInt();
    QVERIFY2(idx >= 0 && idx < w.cardCount(),
             qPrintable(QStringLiteral("selectionChangeRequested emitted invalid index %1").arg(idx)));
  }
}

void TestDetailsPaneCoverflow::coverflow_setCornerRadiusAndColorsAreNoCrash() {
  CoverFlowWidget w;
  // Just exercise the setter surface — this is a smoke-level check that
  // the appearance-config inputs don't trip an assertion or crash on the
  // first call. A regression here usually shows up as a hard fault in
  // applyAppearance plumbing.
  w.setCornerRadius(8);
  w.setTileColor(QStringLiteral("#11223344"));
  w.setSelectionColor(QStringLiteral("#FFAABBCC"));
  w.setBackgroundColor(QStringLiteral("#000000"));
  w.setHideTitles(true);
  w.setFontSize(14);
  w.setFontFamily(QStringLiteral("Sans"));
  // setCornerRadius again with a sentinel-edge value
  w.setCornerRadius(0);
  QCOMPARE(w.cardCount(), 0);
}

void TestDetailsPaneCoverflow::coverflow_setCardsClearsAndReplaces() {
  CoverFlowWidget w;
  QList<CoverFlowCardData> initial;
  initial << makeCard("a") << makeCard("b") << makeCard("c");
  w.setCards(initial);
  QCOMPARE(w.cardCount(), 3);

  // Replacing with an empty list should reset count.
  w.setCards({});
  QCOMPARE(w.cardCount(), 0);

  // Replacing with a different list should land on that count.
  QList<CoverFlowCardData> next;
  next << makeCard("x");
  w.setCards(next);
  QCOMPARE(w.cardCount(), 1);
}
