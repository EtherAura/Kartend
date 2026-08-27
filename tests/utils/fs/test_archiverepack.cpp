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

// Write an archive (zip by default, 7z when sevenZip) with the given
// {entryName, content} regular-file entries.
bool makeZip(const QString &path, const QVector<QPair<QString, QByteArray>> &entries,
             bool sevenZip = false) {
  struct archive *a = archive_write_new();
  if (sevenZip) {
    archive_write_set_format_7zip(a);
  } else {
    archive_write_set_format_zip(a);
  }
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

// Write an archive containing one entry of a caller-chosen filetype: a
// symlink (@p linkTarget non-empty, AE_IFLNK) or a hardlink (@p hardlink
// true). These are the entry kinds ArchiveSafety refuses at extraction time
// and that repack used to copy through verbatim (Kartend-11lzl).
bool makeArchiveWithLinkEntry(const QString &path, const QString &entryName,
                              const QString &linkTarget, bool hardlink = false) {
  struct archive *a = archive_write_new();
  // pax restricted: the zip writer drops link entries, so the fixture has to
  // use a format that actually records them — which is also what makes the
  // repack path reachable with one in hand.
  archive_write_set_format_pax_restricted(a);
  if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK) {
    archive_write_free(a);
    return false;
  }
  struct archive_entry *e = archive_entry_new();
  archive_entry_set_pathname(e, entryName.toUtf8().constData());
  archive_entry_set_size(e, 0);
  archive_entry_set_perm(e, 0644);
  if (hardlink) {
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_hardlink(e, linkTarget.toUtf8().constData());
  } else {
    archive_entry_set_filetype(e, AE_IFLNK);
    archive_entry_set_symlink(e, linkTarget.toUtf8().constData());
  }
  const bool headerOk = archive_write_header(a, e) == ARCHIVE_OK;
  archive_entry_free(e);
  const bool ok = archive_write_close(a) == ARCHIVE_OK;
  archive_write_free(a);
  return headerOk && ok;
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
  void renames7zEntryPreservingFormat();
  void refusesSymlinkEntry();
  void refusesHardlinkEntry();
  void refusesEscapingRenameTarget();
  void refusesEscapingSourceEntryName();
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

void TestArchiveRepack::renames7zEntryPreservingFormat() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("round-trip needs libarchive to build + read the fixture archive");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("Game (old).7z"));
  const QString dst = dir.filePath(QStringLiteral("Game.7z"));
  const QByteArray rom = QByteArrayLiteral("7z inner rom bytes \x01\x02\x03");
  QVERIFY(makeZip(src, {{QStringLiteral("Game (old).md"), rom}}, /*sevenZip=*/true));

  auto r = ArchiveRepack::repack(src, dst,
                                 {{QStringLiteral("Game (old).md"), QStringLiteral("Game.md")}});
  QVERIFY2(!r.isError(), qPrintable(r.isError() ? r.error().message : QString()));
  QCOMPARE(r.value(), 1);

  // Inner entry renamed + bytes intact...
  const QMap<QString, QByteArray> out = readZip(dst);
  QCOMPARE(out.value(QStringLiteral("Game.md")), rom);
  QVERIFY(!out.contains(QStringLiteral("Game (old).md")));
  // ...and the output is really 7z on disk (magic "7z…"), not a zip wearing a
  // .7z name (zip would start with "PK").
  QFile f(dst);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray magic = f.read(2);
  f.close();
  QCOMPARE(magic, QByteArrayLiteral("7z"));

  QVERIFY(ArchiveRepack::isRepackableFormat(dst));
  QVERIFY(ArchiveRepack::isRepackableFormat(QStringLiteral("a.zip")));
  QVERIFY(!ArchiveRepack::isRepackableFormat(QStringLiteral("a.rar")));
#endif
}

// Kartend-11lzl. repack() replaces the user's original file with its output,
// so an entry kind ArchiveSafety refuses at extraction time must not survive
// the trip — otherwise the DAT-fix engine launders a hostile archive into a
// fresh one and flags nothing. The destination must also be gone: a partial
// dst that outlived the refusal could still be swapped over the original.
void TestArchiveRepack::refusesSymlinkEntry() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("needs libarchive to build the fixture archive");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("evil.tar"));
  const QString dst = dir.filePath(QStringLiteral("out.tar"));
  QVERIFY(makeArchiveWithLinkEntry(src, QStringLiteral("rom.md"), QStringLiteral("/etc/passwd")));

  auto r = ArchiveRepack::repack(src, dst, {});
  QVERIFY2(r.isError(), "a symlink entry must not be copied through");
  QCOMPARE(r.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  QVERIFY2(!QFile::exists(dst), "the half-written destination must be removed on refusal");
#endif
}

void TestArchiveRepack::refusesHardlinkEntry() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("needs libarchive to build the fixture archive");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("evil.tar"));
  const QString dst = dir.filePath(QStringLiteral("out.tar"));
  QVERIFY(makeArchiveWithLinkEntry(src, QStringLiteral("rom.md"), QStringLiteral("other.md"),
                                   /*hardlink=*/true));

  auto r = ArchiveRepack::repack(src, dst, {});
  QVERIFY2(r.isError(), "a hardlink entry must not be copied through");
  QCOMPARE(r.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  QVERIFY(!QFile::exists(dst));
#endif
}

// The rename target comes from downloaded DAT data, so it is untrusted in
// exactly the way the source entry name is — and it used to reach
// archive_entry_set_pathname with no validation at all.
void TestArchiveRepack::refusesEscapingRenameTarget() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("round-trip needs libarchive to build the fixture zip");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("Game.zip"));
  const QString dst = dir.filePath(QStringLiteral("Fixed.zip"));
  QVERIFY(makeZip(src, {{QStringLiteral("Game (old).md"), QByteArrayLiteral("rom")}}));

  auto r = ArchiveRepack::repack(
      src, dst, {{QStringLiteral("Game (old).md"), QStringLiteral("../../autoexec.bat")}});
  QVERIFY2(r.isError(), "a traversing DAT rename target must be refused");
  QCOMPARE(r.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  QVERIFY(!QFile::exists(dst));

  // An absolute target is the same class and must fail the same way.
  auto abs = ArchiveRepack::repack(
      src, dst, {{QStringLiteral("Game (old).md"), QStringLiteral("/etc/cron.d/x")}});
  QVERIFY(abs.isError());
  QCOMPARE(abs.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
#endif
}

// A traversing name already present in the SOURCE is refused even when no
// rename touches it — repack is not a laundering service.
void TestArchiveRepack::refusesEscapingSourceEntryName() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("round-trip needs libarchive to build the fixture archive");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString src = dir.filePath(QStringLiteral("evil.zip"));
  const QString dst = dir.filePath(QStringLiteral("out.zip"));
  QVERIFY(makeZip(src, {{QStringLiteral("../../autoexec.bat"), QByteArrayLiteral("x")}}));

  auto r = ArchiveRepack::repack(src, dst, {});
  QVERIFY2(r.isError(), "a traversing source entry name must not survive a repack");
  QCOMPARE(r.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  QVERIFY(!QFile::exists(dst));
#endif
}

QTEST_GUILESS_MAIN(TestArchiveRepack)
#include "test_archiverepack.moc"
