// Tests for ArchiveRepack::repack — rewriting a zip while renaming selected
// member entries (the inner-ROM rename that makes a zip-packed set DAT-canonical).
// The round-trip cases build + read real zips via libarchive, so they are gated
// on KARTEND_HAS_LIBARCHIVE; the argument-guard cases run unconditionally.

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include "archiverepack.h"

#ifdef KARTEND_HAS_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>

namespace {

// Write a zip with the given {entryName, content} regular-file entries.
bool makeZip(const QString &path, const QVector<QPair<QString, QByteArray>> &entries) {
  struct archive *a = archive_write_new();
  archive_write_set_format_zip(a);
  if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK) {
    archive_write_free(a);
    return false;
  }
  for (const auto &[name, content] : entries) {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, name.toUtf8().constData());
    archive_entry_set_size(e, content.size());
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    if (!content.isEmpty()) {
      archive_write_data(a, content.constData(), static_cast<size_t>(content.size()));
    }
    archive_entry_free(e);
  }
  const bool ok = archive_write_close(a) == ARCHIVE_OK;
  archive_write_free(a);
  return ok;
}

// Read a zip into an ordered {entryName -> content} map.
QMap<QString, QByteArray> readZip(const QString &path) {
  QMap<QString, QByteArray> out;
  struct archive *a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);
  if (archive_read_open_filename(a, QFile::encodeName(path).constData(), 65536) != ARCHIVE_OK) {
    archive_read_free(a);
    return out;
  }
  struct archive_entry *e = nullptr;
  while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
    const QString name = QString::fromUtf8(archive_entry_pathname(e));
    QByteArray data;
    const void *buff = nullptr;
    size_t len = 0;
    la_int64_t off = 0;
    while (archive_read_data_block(a, &buff, &len, &off) == ARCHIVE_OK) {
      data.append(static_cast<const char *>(buff), static_cast<qsizetype>(len));
    }
    out.insert(name, data);
  }
  archive_read_free(a);
  return out;
}

} // namespace
#endif // KARTEND_HAS_LIBARCHIVE

class TestArchiveRepack : public QObject {
  Q_OBJECT

private slots:
  void refusesSameSourceAndDestination();
  void missingSourceErrors();
  void renamesEntryAndPreservesContent();
  void nonMatchingRenameLeavesArchiveIntact();
};

void TestArchiveRepack::refusesSameSourceAndDestination() {
  // Guard runs before any archive work — a repack that wrote over its own source
  // could corrupt the original, so it must be refused regardless of backend.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString p = dir.filePath(QStringLiteral("a.zip"));
  QFile f(p);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("not really a zip");
  f.close();
  auto r = ArchiveRepack::repack(p, p, {});
  QVERIFY(r.isError());
}

void TestArchiveRepack::missingSourceErrors() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  auto r = ArchiveRepack::repack(dir.filePath(QStringLiteral("nope.zip")),
                                 dir.filePath(QStringLiteral("out.zip")), {});
  QVERIFY(r.isError());
}

void TestArchiveRepack::renamesEntryAndPreservesContent() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("round-trip needs libarchive to build + read the fixture zip");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("Game (1995-07-07).zip"));
  const QString dst = dir.filePath(QStringLiteral("Game.zip"));
  const QByteArray rom = QByteArrayLiteral("\x01\x02\x03 ROM BYTES \xff\xfe");
  QVERIFY(makeZip(src, {{QStringLiteral("Game (1995-07-07).md"), rom},
                        {QStringLiteral("notes.txt"), QByteArrayLiteral("keep me")}}));

  QHash<QString, QString> renames{
      {QStringLiteral("Game (1995-07-07).md"), QStringLiteral("Game.md")}};
  auto r = ArchiveRepack::repack(src, dst, renames);
  QVERIFY2(!r.isError(), qPrintable(r.isError() ? r.error().message : QString()));
  QCOMPARE(r.value(), 1); // exactly the one matched entry renamed

  const QMap<QString, QByteArray> out = readZip(dst);
  QVERIFY(!out.contains(QStringLiteral("Game (1995-07-07).md")));                 // old name gone
  QVERIFY(out.contains(QStringLiteral("Game.md")));                               // renamed
  QCOMPARE(out.value(QStringLiteral("Game.md")), rom);                            // bytes preserved
  QCOMPARE(out.value(QStringLiteral("notes.txt")), QByteArrayLiteral("keep me")); // untouched
  QVERIFY(QFile::exists(src)); // source is left intact (caller swaps)
#endif
}

void TestArchiveRepack::nonMatchingRenameLeavesArchiveIntact() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("round-trip needs libarchive to build + read the fixture zip");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("a.zip"));
  const QString dst = dir.filePath(QStringLiteral("b.zip"));
  QVERIFY(makeZip(src, {{QStringLiteral("only.md"), QByteArrayLiteral("data")}}));

  // A stale plan whose rename matches nothing returns 0 (the caller can detect it).
  auto r = ArchiveRepack::repack(src, dst, {{QStringLiteral("ghost.md"), QStringLiteral("x.md")}});
  QVERIFY(!r.isError());
  QCOMPARE(r.value(), 0);
  const QMap<QString, QByteArray> out = readZip(dst);
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.value(QStringLiteral("only.md")), QByteArrayLiteral("data"));
#endif
}

QTEST_GUILESS_MAIN(TestArchiveRepack)
#include "test_archiverepack.moc"
