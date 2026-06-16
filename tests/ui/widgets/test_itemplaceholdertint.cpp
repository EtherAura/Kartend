// Tests for the "follow the system accent" title-tint path and the
// placeholder-tile cache invalidation on accent change (Kartend-a0d6c).
//
// Both exercise the user's default appearance setting: titleBaseColor empty
// → ItemWidget resolves the accent from QApplication's QPalette::Highlight,
// which is the role KDE maps the (wallpaper-derived) system accent onto. The
// second test is the regression guard for the bug fixed here: the per-size
// tile cache keyed only on the palette Mid base + custom tile colour returned
// a stale, old-accent tile when only the system accent changed.
//
// QTEST_MAIN gives us a real QApplication so QApplication::setPalette() and
// QPixmap rendering work; no widgets are shown and no DB is touched.

#include "itemwidget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QTest>

namespace {
// Re-stamp the application palette's Highlight role (what KDE drives from the
// system accent) and return the previous full palette so callers can restore.
QPalette setAppHighlight(const QColor &highlight) {
  const QPalette previous = QApplication::palette();
  QPalette next = previous;
  next.setColor(QPalette::Highlight, highlight);
  QApplication::setPalette(next);
  return previous;
}
} // namespace

class TestItemPlaceholderTint : public QObject {
  Q_OBJECT
private slots:
  void init();
  void titleTintFollowsSystemHighlightHue();
  void placeholderTileRetintsWhenAccentChanges();
  void placeholderTileStableWhenAccentUnchanged();
};

void TestItemPlaceholderTint::init() {
  // Empty base colour => titleTint() (and thus the placeholder accent) follows
  // QPalette::Highlight. Pin the saturation/lightness so the test is immune to
  // any default drift.
  ItemWidget::setTitleBaseColor(QString());
  ItemWidget::setTitleTintSaturation(180);
  ItemWidget::setTitleTintLightness(60);
}

void TestItemPlaceholderTint::titleTintFollowsSystemHighlightHue() {
  const QPalette saved = setAppHighlight(QColor(200, 40, 40)); // red-ish accent
  const int redHue = ItemWidget::titleTint().hslHue();

  setAppHighlight(QColor(40, 80, 200)); // blue-ish accent
  const int blueHue = ItemWidget::titleTint().hslHue();

  QApplication::setPalette(saved);

  // The tint preserves the accent's hue, so a system-accent swap must move it.
  QVERIFY(redHue >= 0 && blueHue >= 0);
  QVERIFY(redHue != blueHue);
}

void TestItemPlaceholderTint::placeholderTileRetintsWhenAccentChanges() {
  // Same size + same palette Mid base; only the accent (Highlight) changes.
  // Before the cache-key fix the per-size cache returned the first tile
  // verbatim, so the accent shift was invisible. The images must now differ.
  const QPalette saved = setAppHighlight(QColor(220, 20, 20));
  const QImage first = ItemWidget::buildPlaceholderTile(64, 64).toImage();

  setAppHighlight(QColor(20, 20, 220));
  const QImage second = ItemWidget::buildPlaceholderTile(64, 64).toImage();

  QApplication::setPalette(saved);

  QVERIFY(!first.isNull());
  QVERIFY(!second.isNull());
  QVERIFY(first != second);
}

void TestItemPlaceholderTint::placeholderTileStableWhenAccentUnchanged() {
  // The cache must still hit when nothing relevant changes — re-rendering the
  // same size + same accent yields identical pixels (guards against the fix
  // accidentally disabling the cache).
  const QPalette saved = setAppHighlight(QColor(120, 160, 80));
  const QImage a = ItemWidget::buildPlaceholderTile(64, 64).toImage();
  const QImage b = ItemWidget::buildPlaceholderTile(64, 64).toImage();
  QApplication::setPalette(saved);

  QVERIFY(!a.isNull());
  QCOMPARE(a, b);
}

QTEST_MAIN(TestItemPlaceholderTint)
#include "test_itemplaceholdertint.moc"
