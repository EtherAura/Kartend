/**
 * @file test_datauditlayout.cpp
 * @brief Unit tests for the folder-structure sampling probe (Kartend-m6qsb.6).
 *
 * Synthesizes temp trees for each layout class — flat, nested,
 * archive-per-item, mixed — and verifies the classification, the relevance
 * filter (collection extension allow-list), the token round-trip used by
 * persistence, and the defensive paths (missing/empty root).
 */

#include "datauditlayout.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using DatAudit::Layout;
using DatAudit::LayoutDetection;

namespace {

// One byte is enough — the probe never reads contents, only shape.
void touch(const QString &path) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
}

} // namespace

class TestDatAuditLayout : public QObject {
  Q_OBJECT

private slots:
  void flatFolderDetectsFlat();
  void nestedTreeDetectsNested();
  void subfolderPerItemDetectsMultiFileSets();
  void oneFilePerSubfolderStaysNested();
  void deepTreeStaysNested();
  void archiveFolderDetectsArchivePerItem();
  void mixedArchivesDetectMixed();
  void rootSplitBetweenTopAndSubfoldersDetectsMixed();
  void relevanceFilterIgnoresSidecars();
  void emptyAndMissingRootsDetectUnknown();
  void tokenRoundTrip();

private:
  QTemporaryDir m_dir;
  int m_caseId = 0;

  // A fresh subdirectory per test case so trees never bleed into each other.
  QString freshRoot() {
    const QString root = m_dir.filePath(QStringLiteral("case-%1").arg(m_caseId++));
    QDir().mkpath(root);
    return root;
  }
};

void TestDatAuditLayout::flatFolderDetectsFlat() {
  const QString root = freshRoot();
  for (int i = 0; i < 10; ++i) {
    touch(QStringLiteral("%1/clip-%2.mkv").arg(root).arg(i));
  }
  // A stray subfolder with one extra must not flip the verdict — real flat
  // sets routinely carry an "extras" folder.
  QDir().mkpath(root + QStringLiteral("/extras"));
  touch(root + QStringLiteral("/extras/notes.mkv"));

  const LayoutDetection d = DatAudit::detectLayout(root);
  QCOMPARE(d.layout, Layout::Flat);
  QVERIFY(d.confidence >= 0.8);
  QCOMPARE(d.sampledFiles, 11);
  QVERIFY(!d.evidence.isEmpty());
}

void TestDatAuditLayout::nestedTreeDetectsNested() {
  const QString root = freshRoot();
  for (int i = 0; i < 10; ++i) {
    const QString sub = QStringLiteral("%1/album-%2").arg(root).arg(i);
    QDir().mkpath(sub);
    touch(sub + QStringLiteral("/track.flac"));
  }
  touch(root + QStringLiteral("/readme.flac")); // one loose file doesn't flip it

  const LayoutDetection d = DatAudit::detectLayout(root);
  QCOMPARE(d.layout, Layout::Nested);
  QVERIFY(d.confidence >= 0.8);
}

void TestDatAuditLayout::subfolderPerItemDetectsMultiFileSets() {
  // One folder per item, each holding a multi-file set (disc pairs) — the
  // subfolder-per-item shape (Kartend-m6qsb.12). Root stays empty.
  const QString root = freshRoot();
  for (int i = 0; i < 6; ++i) {
    const QString item = QStringLiteral("%1/Game %2").arg(root).arg(i);
    QDir().mkpath(item);
    touch(item + QStringLiteral("/disc1.bin"));
    touch(item + QStringLiteral("/disc2.bin"));
    touch(item + QStringLiteral("/disc3.bin"));
  }
  const LayoutDetection d = DatAudit::detectLayout(root);
  QCOMPARE(d.layout, Layout::SubfolderPerItem);
  QVERIFY(d.confidence >= 0.8);
}

void TestDatAuditLayout::oneFilePerSubfolderStaysNested() {
  // One file per folder is NOT a multi-file set — it stays Nested even though
  // the root is empty and files sit one level deep.
  const QString root = freshRoot();
  for (int i = 0; i < 6; ++i) {
    const QString item = QStringLiteral("%1/Item %2").arg(root).arg(i);
    QDir().mkpath(item);
    touch(item + QStringLiteral("/only.bin"));
  }
  QCOMPARE(DatAudit::detectLayout(root).layout, Layout::Nested);
}

void TestDatAuditLayout::deepTreeStaysNested() {
  // Multi-file but buried two levels deep — a deep tree, not subfolder-per-item.
  const QString root = freshRoot();
  for (int i = 0; i < 4; ++i) {
    const QString deep = QStringLiteral("%1/group-%2/inner").arg(root).arg(i);
    QDir().mkpath(deep);
    touch(deep + QStringLiteral("/a.bin"));
    touch(deep + QStringLiteral("/b.bin"));
  }
  QCOMPARE(DatAudit::detectLayout(root).layout, Layout::Nested);
}

void TestDatAuditLayout::archiveFolderDetectsArchivePerItem() {
  const QString root = freshRoot();
  for (int i = 0; i < 9; ++i) {
    touch(QStringLiteral("%1/item-%2.zip").arg(root).arg(i));
  }
  touch(root + QStringLiteral("/index.txt")); // 10% loose — still archive-per-item

  const LayoutDetection d = DatAudit::detectLayout(root);
  QCOMPARE(d.layout, Layout::ArchivePerItem);
  QVERIFY(d.confidence >= 0.7);
}

void TestDatAuditLayout::mixedArchivesDetectMixed() {
  const QString root = freshRoot();
  for (int i = 0; i < 5; ++i) {
    touch(QStringLiteral("%1/packed-%2.zip").arg(root).arg(i));
    touch(QStringLiteral("%1/loose-%2.mkv").arg(root).arg(i));
  }

  // 50% archives sits between the 0.3/0.7 thresholds: the probe must say
  // Mixed rather than guess the more invasive archive handling.
  const LayoutDetection d = DatAudit::detectLayout(root);
  QCOMPARE(d.layout, Layout::Mixed);
}

void TestDatAuditLayout::rootSplitBetweenTopAndSubfoldersDetectsMixed() {
  const QString root = freshRoot();
  for (int i = 0; i < 5; ++i) {
    touch(QStringLiteral("%1/top-%2.mkv").arg(root).arg(i));
    const QString sub = QStringLiteral("%1/series-%2").arg(root).arg(i);
    QDir().mkpath(sub);
    touch(sub + QStringLiteral("/episode.mkv"));
  }

  const LayoutDetection d = DatAudit::detectLayout(root);
  QCOMPARE(d.layout, Layout::Mixed);
}

void TestDatAuditLayout::relevanceFilterIgnoresSidecars() {
  const QString root = freshRoot();
  // Ten relevant files flat in the root, fifty sidecars nested under
  // per-item folders (topFrac 10/60 ≈ 0.17 → Nested) — without the
  // allow-list filter the sidecars out-vote the content and misreport.
  for (int i = 0; i < 10; ++i) {
    touch(QStringLiteral("%1/recording-%2.flac").arg(root).arg(i));
    const QString side = QStringLiteral("%1/artwork-%2").arg(root).arg(i);
    QDir().mkpath(side);
    touch(side + QStringLiteral("/cover.jpg"));
    touch(side + QStringLiteral("/back.jpg"));
    touch(side + QStringLiteral("/disc.jpg"));
    touch(side + QStringLiteral("/spine.jpg"));
    touch(side + QStringLiteral("/notes.txt"));
  }

  const LayoutDetection unfiltered = DatAudit::detectLayout(root);
  QCOMPARE(unfiltered.layout, Layout::Nested); // sidecars dominate unfiltered

  const LayoutDetection filtered = DatAudit::detectLayout(root, {QStringLiteral("flac")});
  QCOMPARE(filtered.layout, Layout::Flat);
  QCOMPARE(filtered.sampledFiles, 10);

  // The allow-list is normalised defensively: dots and case don't matter.
  const LayoutDetection sloppy = DatAudit::detectLayout(root, {QStringLiteral(".FLAC")});
  QCOMPARE(sloppy.layout, Layout::Flat);
}

void TestDatAuditLayout::emptyAndMissingRootsDetectUnknown() {
  QCOMPARE(DatAudit::detectLayout(freshRoot()).layout, Layout::Unknown); // empty dir
  QCOMPARE(DatAudit::detectLayout(m_dir.filePath(QStringLiteral("absent"))).layout,
           Layout::Unknown);
  QCOMPARE(DatAudit::detectLayout(QString()).layout, Layout::Unknown);
  QCOMPARE(DatAudit::detectLayout(QStringLiteral("   ")).layout, Layout::Unknown);
}

void TestDatAuditLayout::tokenRoundTrip() {
  const Layout all[] = {Layout::Unknown,        Layout::Flat,  Layout::Nested,
                        Layout::ArchivePerItem, Layout::Mixed, Layout::SubfolderPerItem};
  for (Layout l : all) {
    QCOMPARE(DatAudit::layoutFromToken(DatAudit::layoutToken(l)), l);
  }
  // Unknown persists as '' (the table's "no value" convention) and garbage
  // tokens parse back safely.
  QVERIFY(DatAudit::layoutToken(Layout::Unknown).isEmpty());
  QCOMPARE(DatAudit::layoutFromToken(QStringLiteral("garbage")), Layout::Unknown);
}

QTEST_MAIN(TestDatAuditLayout)
#include "test_datauditlayout.moc"
