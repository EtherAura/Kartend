// Overlay scrollbars must sit BESIDE the content, never on top of it
// (field report 2026-08-19: "the grid scrollbar also overlaps the items").
//
// The first implementation parented each handle to the viewport and drew it
// at the viewport's right edge, i.e. deliberately over the content. The lane
// is now reserved permanently — permanently, so that a handle fading in or
// out never moves an item, which was the whole point of overlay bars.
#include "overlayscrollbars.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QWidget>

class TestOverlayScrollbarGutter : public QObject {
  Q_OBJECT
private slots:
  void viewportIsInsetSoContentNeverSitsUnderTheHandle();
  void detachingRestoresTheFullViewport();
};

void TestOverlayScrollbarGutter::viewportIsInsetSoContentNeverSitsUnderTheHandle() {
  QScrollArea area;
  area.resize(400, 300);
  auto *content = new QWidget;
  content->resize(380, 2000); // taller than the viewport → a bar is warranted
  area.setWidget(content);
  area.show();
  QVERIFY(QTest::qWaitForWindowExposed(&area));

  QCOMPARE(OverlayScrollbars::reservedGutter(&area), 0); // nothing attached yet

  OverlayScrollbars::apply(&area, true);
  QTest::qWait(50);

  const int gutter = OverlayScrollbars::reservedGutter(&area);
  QVERIFY2(gutter > 0, "no lane reserved — content would be laid out under the handle");
}

void TestOverlayScrollbarGutter::detachingRestoresTheFullViewport() {
  QScrollArea area;
  area.resize(400, 300);
  auto *content = new QWidget;
  content->resize(380, 2000);
  area.setWidget(content);
  area.show();
  QVERIFY(QTest::qWaitForWindowExposed(&area));

  OverlayScrollbars::apply(&area, true);
  QTest::qWait(50);
  QVERIFY(OverlayScrollbars::reservedGutter(&area) > 0);

  // Turning the option off must hand the space back, or disabling overlay
  // scrollbars would silently cost the user a strip of content width.
  OverlayScrollbars::apply(&area, false);
  QTest::qWait(50);
  QCOMPARE(OverlayScrollbars::reservedGutter(&area), 0);
}

QTEST_MAIN(TestOverlayScrollbarGutter)
#include "test_overlayscrollbargutter.moc"
