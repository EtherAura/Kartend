// DetailsPane's file-info rows: the async stat result has to reach them
// whenever they are SHOWING, not only while the File tab is active
// (Kartend-e7xte).
//
// The rows appear on two paths, and only one of them was ever painted:
//   * the File tab, which is what the original gate tested for; and
//   * the Item tab's unscraped fallback, which surfaces the same rows so an
//     item with no scraped metadata does not leave an empty sidebar.
// Because most items in a fresh library are unscraped, the second path is the
// common one — and it displayed the '…' placeholder permanently.
//
// Constructed headlessly and never shown; the labels are reached by
// objectName so the test does not need friendship with DetailsPane.
#include <QLabel>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "detailspane.h"

class TestDetailsPaneFileInfo : public QObject {
  Q_OBJECT

private slots:
  void unscrapedItemOnItemTabResolvesSizeAndModified();
  void fileTabStillResolves();
  void missingFileReportsDashRatherThanPlaceholder();

private:
  QTemporaryDir m_dir;

  /// A real file on disk — the pane stats the actual path, so there is
  /// nothing to fake here.
  QString stageFile(const QString &name, int bytes) {
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
      return {};
    }
    f.write(QByteArray(bytes, 'x'));
    f.close();
    return path;
  }

  static QLabel *label(DetailsPane &pane, const char *objectName) {
    return pane.findChild<QLabel *>(QLatin1String(objectName));
  }
};

void TestDetailsPaneFileInfo::unscrapedItemOnItemTabResolvesSizeAndModified() {
  const QString path = stageFile(QStringLiteral("clip.mp4"), 2048);
  QVERIFY(!path.isEmpty());

  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setMetadata(path, QStringLiteral("Clip"));
  // Empty metadata is what drives the Item tab's unscraped fallback into
  // showing the file-info rows.
  pane.setExtendedMetadata({});

  QLabel *size = label(pane, "fileSizeValue");
  QLabel *modified = label(pane, "lastModifiedValue");
  QVERIFY(size);
  QVERIFY(modified);
  QVERIFY(!size->isHidden()); // the fallback really did surface the rows

  // The stat is async; what matters is that it lands at all rather than
  // leaving the placeholder up for good.
  QTRY_VERIFY_WITH_TIMEOUT(size->text() != QStringLiteral("…"), 5000);
  QCOMPARE(size->text(), DetailsPane::formatFileSize(2048));
  QVERIFY(modified->text() != QStringLiteral("…"));
  QVERIFY(!modified->text().isEmpty());
}

void TestDetailsPaneFileInfo::fileTabStillResolves() {
  // The path Kartend-kujy5 established must keep working — this fix widens
  // the gate, it does not move it.
  const QString path = stageFile(QStringLiteral("other.mp4"), 4096);
  QVERIFY(!path.isEmpty());

  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::File);
  pane.setMetadata(path, QStringLiteral("Other"));

  QLabel *size = label(pane, "fileSizeValue");
  QVERIFY(size);
  QTRY_VERIFY_WITH_TIMEOUT(size->text() != QStringLiteral("…"), 5000);
  QCOMPARE(size->text(), DetailsPane::formatFileSize(4096));
}

void TestDetailsPaneFileInfo::missingFileReportsDashRatherThanPlaceholder() {
  // A stub pointing at something deleted must settle on the not-found
  // rendering, not sit on '…' forever — the placeholder is indistinguishable
  // from "still loading".
  DetailsPane pane;
  pane.setActiveTab(DetailsPaneTab::Item);
  pane.setMetadata(m_dir.filePath(QStringLiteral("gone.mp4")), QStringLiteral("Gone"));
  pane.setExtendedMetadata({});

  QLabel *size = label(pane, "fileSizeValue");
  QVERIFY(size);
  QTRY_VERIFY_WITH_TIMEOUT(size->text() != QStringLiteral("…"), 5000);
  QCOMPARE(size->text(), QStringLiteral("-"));
}

QTEST_MAIN(TestDetailsPaneFileInfo)
#include "test_detailspanefileinfo.moc"
