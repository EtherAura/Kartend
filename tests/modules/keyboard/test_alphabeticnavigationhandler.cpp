// Unit coverage for AlphabeticNavigationHandler — the PageUp/PageDown
// letter-jump. The handler reads only two ctx roles (IScrollDataSource for
// display names, ISelectionManager for the current index), so it is
// constructible without the widget graph: a file-local capturing data source
// provides per-index names and the shared StubSelectionManager provides the
// selection. Covers forward/backward group jumps, wraparound, the
// all-same-letter no-op, case folding, and the subcollection / virtual-folder
// / display-name-hash name sources.

#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include "../../integration/mocks/stubselectionmanager.h"
#include "alphabeticnavigationhandler.h"
#include "applicationcontext.h"
#include "iscrolldatasource.h"

namespace {

/// Minimal IScrollDataSource backed by three name lists: subcollection names,
/// virtual-folder paths, then media file paths (the same visual-index band
/// order the production store exposes). `fileNames` optionally maps a file
/// path to its display name, mirroring the scraped-title hash.
class FakeScrollData : public IScrollDataSource {
public:
  QStringList subNames;
  QStringList folderPaths;
  QStringList filePaths;
  QHash<QString, QString> fileNames;

  [[nodiscard]] int getTotalItems() const override {
    return static_cast<int>(subNames.size() + folderPaths.size() + filePaths.size());
  }
  [[nodiscard]] const QStringList &getFilePaths() const override { return filePaths; }
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const override { return fileNames; }
  [[nodiscard]] QString filePathForVisualIndex(int visualIndex) const override {
    const int fileIdx = visualIndex - static_cast<int>(subNames.size() + folderPaths.size());
    if (fileIdx < 0 || fileIdx >= filePaths.size()) {
      return {};
    }
    return filePaths[fileIdx];
  }
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const override {
    return m_widgets;
  }
  [[nodiscard]] int indexForWidget(ItemWidget *) const override { return -1; }
  [[nodiscard]] int getSubcollectionCount() const override {
    return static_cast<int>(subNames.size());
  }
  [[nodiscard]] QString getSubcollectionName(int subcollectionIndex) const override {
    return subNames.value(subcollectionIndex);
  }
  [[nodiscard]] int getVirtualFolderCount() const override {
    return static_cast<int>(folderPaths.size());
  }
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int visualIndex) const override {
    const int folderIdx = visualIndex - static_cast<int>(subNames.size());
    if (folderIdx < 0 || folderIdx >= folderPaths.size()) {
      return {};
    }
    return folderPaths[folderIdx];
  }
  [[nodiscard]] int subcollectionIndexFromActual(int) const override { return -1; }
  void updateContextForSubcollection(int) override {}
  void applySubcollectionFilter(int) override {}

private:
  QHash<int, ItemWidget *> m_widgets;
};

/// Handler + fake ctx roles wired for one test slot. `files` become media
/// paths "/media/<name>.mp4" so the display name falls back to the base name.
struct Harness {
  FakeScrollData data;
  KartendTest::StubSelectionManager selection;
  ApplicationContext ctx;
  AlphabeticNavigationHandler handler;

  explicit Harness(const QStringList &files = {}) {
    for (const QString &name : files) {
      data.filePaths << QStringLiteral("/media/%1.mp4").arg(name);
    }
    ctx.managers.scrollData = &data;
    ctx.managers.selectionManager = &selection;
    handler.setContext(&ctx);
  }
};

} // namespace

class TestAlphabeticNavigationHandler : public QObject {
  Q_OBJECT
private slots:
  void forwardJumpsToNextDifferentLetter();
  void forwardWrapsPastEndToBeginning();
  void backwardJumpsToStartOfPreviousGroup();
  void backwardWrapsToLastGroupStart();
  void allSameLetterNavigatesNowhere();
  void noSelectionStartsAtEdge();
  void emptyViewReturnsMinusOne();
  void groupingIsCaseInsensitive();
  void numbersFormTheirOwnGroups();
  void subcollectionNamesLeadTheBandOrder();
  void virtualFolderUsesFolderBaseName();
  void displayNameHashWinsOverFileBaseName();
  void firstCharUppercasesAndBoundsChecks();
};

void TestAlphabeticNavigationHandler::forwardJumpsToNextDifferentLetter() {
  Harness h({QStringLiteral("Alpha"), QStringLiteral("Anchor"), QStringLiteral("Beacon"),
             QStringLiteral("Breeze"), QStringLiteral("Canyon")});
  h.selection.index = 0;
  QSignalSpy spy(&h.handler, &AlphabeticNavigationHandler::requestSelection);

  QCOMPARE(h.handler.navigateToNextLetter(/*forward=*/true), 2); // first "B"
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
}

void TestAlphabeticNavigationHandler::forwardWrapsPastEndToBeginning() {
  Harness h({QStringLiteral("Beacon"), QStringLiteral("Breeze"), QStringLiteral("Canyon"),
             QStringLiteral("Cliff")});
  h.selection.index = 3;                             // inside the trailing "C" group
  QCOMPARE(h.handler.navigateToNextLetter(true), 0); // wraps to the "B" group
}

void TestAlphabeticNavigationHandler::backwardJumpsToStartOfPreviousGroup() {
  Harness h({QStringLiteral("Alpha"), QStringLiteral("Anchor"), QStringLiteral("Beacon"),
             QStringLiteral("Breeze"), QStringLiteral("Canyon")});
  h.selection.index = 4; // "Canyon"
  // Backward lands on the FIRST item of the previous group ("Beacon" at 2),
  // not the last, so repeated PageUp walks group starts.
  QCOMPARE(h.handler.navigateToNextLetter(/*forward=*/false), 2);
}

void TestAlphabeticNavigationHandler::backwardWrapsToLastGroupStart() {
  Harness h({QStringLiteral("Alpha"), QStringLiteral("Anchor"), QStringLiteral("Beacon"),
             QStringLiteral("Breeze")});
  h.selection.index = 0;
  // Nothing before index 0 → wrap: the previous group is the trailing "B"
  // run and the jump lands on its first member (index 2).
  QCOMPARE(h.handler.navigateToNextLetter(false), 2);
}

void TestAlphabeticNavigationHandler::allSameLetterNavigatesNowhere() {
  Harness h({QStringLiteral("Alpha"), QStringLiteral("Anchor"), QStringLiteral("Amber")});
  h.selection.index = 1;
  QSignalSpy spy(&h.handler, &AlphabeticNavigationHandler::requestSelection);

  QCOMPARE(h.handler.navigateToNextLetter(true), -1);
  QCOMPARE(h.handler.navigateToNextLetter(false), -1);
  QCOMPARE(spy.count(), 0);
}

void TestAlphabeticNavigationHandler::noSelectionStartsAtEdge() {
  Harness h({QStringLiteral("Alpha"), QStringLiteral("Beacon"), QStringLiteral("Canyon")});
  h.selection.index = -1;
  QSignalSpy spy(&h.handler, &AlphabeticNavigationHandler::requestSelection);

  // With no valid selection the jump anchors at the matching edge instead of
  // scanning: forward → first item, backward → last item.
  QCOMPARE(h.handler.navigateToNextLetter(true), 0);
  QCOMPARE(h.handler.navigateToNextLetter(false), 2);
  QCOMPARE(spy.count(), 2);
}

void TestAlphabeticNavigationHandler::emptyViewReturnsMinusOne() {
  Harness h;
  h.selection.index = 0;
  QSignalSpy spy(&h.handler, &AlphabeticNavigationHandler::requestSelection);
  QCOMPARE(h.handler.navigateToNextLetter(true), -1);
  QCOMPARE(spy.count(), 0);
}

void TestAlphabeticNavigationHandler::groupingIsCaseInsensitive() {
  Harness h({QStringLiteral("apple"), QStringLiteral("Apricot"), QStringLiteral("banana"),
             QStringLiteral("Berry")});
  h.selection.index = 0;
  // "apple" and "Apricot" fold to the same 'A' group; the jump crosses to
  // lowercase "banana", not to "Apricot".
  QCOMPARE(h.handler.navigateToNextLetter(true), 2);
}

void TestAlphabeticNavigationHandler::numbersFormTheirOwnGroups() {
  Harness h({QStringLiteral("1942"), QStringLiteral("1943"), QStringLiteral("2020"),
             QStringLiteral("Alpha")});
  h.selection.index = 0;
  // The scan keys on "first character differs", so digit prefixes group and
  // jump exactly like letters.
  QCOMPARE(h.handler.navigateToNextLetter(true), 2);
  h.selection.index = 2;
  QCOMPARE(h.handler.navigateToNextLetter(true), 3);
}

void TestAlphabeticNavigationHandler::subcollectionNamesLeadTheBandOrder() {
  Harness h({QStringLiteral("Beacon")});
  h.data.subNames << QStringLiteral("Archive"); // visual index 0
  h.selection.index = 0;
  // Visual index 0 is the subcollection "Archive" ('A'); the file band
  // follows, so the jump lands on "Beacon" at visual index 1.
  QCOMPARE(h.handler.navigateToNextLetter(true), 1);
}

void TestAlphabeticNavigationHandler::virtualFolderUsesFolderBaseName() {
  Harness h({QStringLiteral("Alpha")});
  h.data.folderPaths << QStringLiteral("/library/Drama"); // visual index 0
  h.selection.index = 0;
  // The folder's display name is its base name ("Drama", 'D'), so from the
  // folder tile the next group is the 'A' file after it.
  QCOMPARE(h.handler.getFirstCharForIndex(0), QChar('D'));
  QCOMPARE(h.handler.navigateToNextLetter(true), 1);
}

void TestAlphabeticNavigationHandler::displayNameHashWinsOverFileBaseName() {
  Harness h({QStringLiteral("zzz_dump"), QStringLiteral("Beacon")});
  // The scraped-title hash maps the first file to "Aurora" — grouping must
  // use the display name ('A'), not the on-disk base name ('Z').
  h.data.fileNames.insert(QStringLiteral("/media/zzz_dump.mp4"), QStringLiteral("Aurora"));
  QCOMPARE(h.handler.getFirstCharForIndex(0), QChar('A'));
  h.selection.index = 0;
  QCOMPARE(h.handler.navigateToNextLetter(true), 1); // "Beacon"
}

void TestAlphabeticNavigationHandler::firstCharUppercasesAndBoundsChecks() {
  Harness h({QStringLiteral("apple")});
  QCOMPARE(h.handler.getFirstCharForIndex(0), QChar('A'));
  QVERIFY(h.handler.getFirstCharForIndex(-1).isNull());
  QVERIFY(h.handler.getFirstCharForIndex(1).isNull());
}

QTEST_GUILESS_MAIN(TestAlphabeticNavigationHandler)
#include "test_alphabeticnavigationhandler.moc"
