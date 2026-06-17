// Tests for the DAT-audit fix engine: pure plan computation, and apply/undo
// over real temp files (rename in place, relocate-copy to a managed root,
// quarantine move), plus the dry-run and collision-guard safety rails.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>

#include "datauditfix.h"
#include "dataudittypes.h"

#ifdef KARTEND_HAS_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

using DatAudit::AuditRow;
using DatAudit::FixAction;
using DatAudit::FixActionKind;
using DatAudit::FixPlan;
using DatAudit::FixSettings;
using DatAudit::Status;

namespace {

AuditRow row(Status status, const QString &filePath, const QString &expectedName) {
  AuditRow r;
  r.status = status;
  r.filePath = filePath;
  r.actualName = QFileInfo(filePath).fileName();
  r.expectedName = expectedName;
  return r;
}

QString makeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes) {
  const QString path = dir.filePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write(bytes);
  f.close();
  return path;
}

#ifdef KARTEND_HAS_LIBARCHIVE
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

// Read a zip into {entryName -> content}.
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
#endif // KARTEND_HAS_LIBARCHIVE

} // namespace

class TestDatAuditFix : public QObject {
  Q_OBJECT

private slots:
  void planRenameOnlyByDefault();
  void planSkipsNoopRename();
  void planRelocateAndQuarantineWhenEnabled();
  void planQuarantinesUnknownAndWrongContent();
  void planRepacksMisnamedArchiveSet();
  void planRepacksMultiRomSingleGameInPlace();
  void planQuarantinesWholeBadContainer();
  void planSkipsMixedArchiveContainer();
  void planSkipsMultiGameArchiveContainer();
  void planRepacks7zContainerPreservingExtension();
  void planSkipsNonRepackableArchiveContainer();
  void planSkipsCollidingInnerRenames();
  void planSkipsRenameOntoExistingMember();
  void planRepackPreservesNestedMemberSubdir();
  void planRenamesMisnamedContainerWithCanonicalInner();
  void applyInPlaceRepackBackupSwapAndUndo();
  void planRelocatePerItemSubfolderUsesGameName();
  void applyRenameInPlaceAndUndo();
  void applyRelocateCopiesLeavesOriginalAndUndoDeletes();
  void applyQuarantineMovesAndUndoRestores();
  void applyDryRunMakesNoChange();
  void applyCollisionGuardSkips();
  void applyRepackRenamesInnerAndContainerAndUndo();
};

void TestDatAuditFix::planRenameOnlyByDefault() {
  const QList<AuditRow> rows{
      row(Status::WrongName, QStringLiteral("/roms/misnamed.bin"), QStringLiteral("Canonical.bin")),
      row(Status::Unknown, QStringLiteral("/roms/junk.bin"), QString()),
      row(Status::Have, QStringLiteral("/roms/ok.bin"), QStringLiteral("ok.bin"))};
  FixSettings s; // defaults: rename on, relocate/quarantine off
  const FixPlan plan = DatAudit::computeFixPlan(rows, s);
  QCOMPARE(plan.actions.size(), 1);
  QCOMPARE(plan.actions.at(0).kind, FixActionKind::Rename);
  QCOMPARE(plan.actions.at(0).fromPath, QStringLiteral("/roms/misnamed.bin"));
  QCOMPARE(plan.actions.at(0).toPath, QStringLiteral("/roms/Canonical.bin"));
}

void TestDatAuditFix::planSkipsNoopRename() {
  // A WrongName row whose on-disk path already ends in the canonical name
  // produces no action (to == from).
  const QList<AuditRow> rows{row(Status::WrongName, QStringLiteral("/roms/Canonical.bin"),
                                 QStringLiteral("Canonical.bin"))};
  const FixPlan plan = DatAudit::computeFixPlan(rows, FixSettings{});
  QVERIFY(plan.isEmpty());
}

void TestDatAuditFix::planRelocateAndQuarantineWhenEnabled() {
  const QList<AuditRow> rows{
      row(Status::Have, QStringLiteral("/roms/a.bin"), QStringLiteral("a.bin")),
      row(Status::WrongName, QStringLiteral("/roms/bad.bin"), QStringLiteral("b.bin")),
      row(Status::Unknown, QStringLiteral("/roms/x.bin"), QString())};
  FixSettings s;
  s.rename = false;
  s.relocateToManagedOutput = true;
  s.managedOutputRoot = QStringLiteral("/out");
  s.quarantineUnknown = true;
  s.quarantineDir = QStringLiteral("/quar");
  const FixPlan plan = DatAudit::computeFixPlan(rows, s);

  int relocate = 0;
  int quarantine = 0;
  for (const auto &a : plan.actions) {
    if (a.kind == FixActionKind::Relocate) {
      ++relocate;
      QVERIFY(a.toPath.startsWith(QStringLiteral("/out/")));
    }
    if (a.kind == FixActionKind::Quarantine) {
      ++quarantine;
      QCOMPARE(a.toPath, QStringLiteral("/quar/x.bin"));
    }
  }
  QCOMPARE(relocate, 2);   // Have + WrongName are both "present"
  QCOMPARE(quarantine, 1); // the Unknown
}

void TestDatAuditFix::planQuarantinesUnknownAndWrongContent() {
  // The quarantine option moves BOTH unknown files (no DAT match) and
  // wrong-content / WrongHash files (right name, bytes no entry matches). A
  // WrongName row is still a rename, never a quarantine.
  const QList<AuditRow> rows{
      row(Status::Unknown, QStringLiteral("/roms/junk.bin"), QString()),
      row(Status::WrongHash, QStringLiteral("/roms/baddump.bin"), QStringLiteral("baddump.bin")),
      row(Status::WrongName, QStringLiteral("/roms/misnamed.bin"),
          QStringLiteral("Canonical.bin"))};
  FixSettings s;
  s.rename = false; // isolate the quarantine behaviour
  s.quarantineUnknown = true;
  s.quarantineDir = QStringLiteral("/quar");
  const FixPlan plan = DatAudit::computeFixPlan(rows, s);

  QStringList quarantined;
  Status wrongHashOrigin = Status::Have;
  for (const auto &a : plan.actions) {
    QCOMPARE(a.kind, FixActionKind::Quarantine); // rename is off, nothing else fires
    quarantined << a.toPath;
    if (a.toPath == QStringLiteral("/quar/baddump.bin")) {
      wrongHashOrigin = a.sourceStatus;
    }
  }
  quarantined.sort();
  const QStringList expected{QStringLiteral("/quar/baddump.bin"), QStringLiteral("/quar/junk.bin")};
  QCOMPARE(quarantined, expected);
  QCOMPARE(wrongHashOrigin, Status::WrongHash); // the action records its origin status
}

void TestDatAuditFix::planRepacksMisnamedArchiveSet() {
  // A single-ROM zip whose inner ROM (and container) carry a non-canonical name
  // yields ONE Repack action: rename the inner entry to the DAT name and the
  // container to <game>.zip.
  AuditRow r =
      row(Status::WrongName, QStringLiteral("/roms/Game (1995-07-07).zip/Game (1995-07-07).md"),
          QStringLiteral("Game (1995).md"));
  r.gameName = QStringLiteral("Game (1995)");
  const FixPlan plan = DatAudit::computeFixPlan({r}, FixSettings{}); // rename on by default
  QCOMPARE(plan.actions.size(), 1);
  const FixAction &a = plan.actions.first();
  QCOMPARE(a.kind, FixActionKind::Repack);
  QCOMPARE(a.fromPath, QStringLiteral("/roms/Game (1995-07-07).zip"));
  QCOMPARE(a.toPath, QStringLiteral("/roms/Game (1995).zip")); // container -> game.zip
  QCOMPARE(a.innerRenames.size(), 1);
  QCOMPARE(a.innerRenames.first().first, QStringLiteral("Game (1995-07-07).md")); // old inner
  QCOMPARE(a.innerRenames.first().second, QStringLiteral("Game (1995).md"));      // new inner
}

void TestDatAuditFix::planRepacksMultiRomSingleGameInPlace() {
  // A zip holding several ROMs of ONE game repacks all inner entries to canonical;
  // the container is already correctly named, so toPath == fromPath (in-place).
  auto member = [](const QString &path, const QString &expected) {
    AuditRow r = row(Status::WrongName, path, expected);
    r.gameName = QStringLiteral("G");
    return r;
  };
  const QList<AuditRow> rows{
      member(QStringLiteral("/roms/G.zip/d1 (old).md"), QStringLiteral("disc1.md")),
      member(QStringLiteral("/roms/G.zip/d2 (old).md"), QStringLiteral("disc2.md"))};
  const FixPlan plan = DatAudit::computeFixPlan(rows, FixSettings{});
  QCOMPARE(plan.actions.size(), 1);
  QCOMPARE(plan.actions.first().kind, FixActionKind::Repack);
  QCOMPARE(plan.actions.first().fromPath, QStringLiteral("/roms/G.zip"));
  QCOMPARE(plan.actions.first().toPath, QStringLiteral("/roms/G.zip")); // already canonical
  QCOMPARE(plan.actions.first().innerRenames.size(), 2);
}

void TestDatAuditFix::planQuarantinesWholeBadContainer() {
  // A zip whose only member is unknown/wrong-content is quarantined as a whole
  // container (the real .zip file), not by the virtual inner path.
  AuditRow r = row(Status::Unknown, QStringLiteral("/roms/Mystery.zip/mystery.md"), QString());
  FixSettings s;
  s.rename = false;
  s.quarantineUnknown = true;
  s.quarantineDir = QStringLiteral("/quar");
  const FixPlan plan = DatAudit::computeFixPlan({r}, s);
  QCOMPARE(plan.actions.size(), 1);
  QCOMPARE(plan.actions.first().kind, FixActionKind::Quarantine);
  QCOMPARE(plan.actions.first().fromPath, QStringLiteral("/roms/Mystery.zip")); // container
  QCOMPARE(plan.actions.first().toPath, QStringLiteral("/quar/Mystery.zip"));
}

void TestDatAuditFix::planSkipsMixedArchiveContainer() {
  // A zip holding a good ROM AND a bad/unknown member is ambiguous: repacking
  // can't drop the bad entry and quarantining loses the good — so it's skipped.
  const QList<AuditRow> rows{
      row(Status::WrongName, QStringLiteral("/roms/multi.zip/a (bad name).md"),
          QStringLiteral("a.md")),
      row(Status::Unknown, QStringLiteral("/roms/multi.zip/b.md"), QString())};
  FixSettings s;
  s.quarantineUnknown = true;
  s.quarantineDir = QStringLiteral("/quar");
  const FixPlan plan = DatAudit::computeFixPlan(rows, s);
  QVERIFY(plan.isEmpty());
}

void TestDatAuditFix::planSkipsMultiGameArchiveContainer() {
  // A zip whose members map to different games has no single canonical container
  // name, so it is skipped rather than guessed.
  AuditRow a = row(Status::WrongName, QStringLiteral("/roms/two.zip/x.md"), QStringLiteral("X.md"));
  a.gameName = QStringLiteral("Game X");
  AuditRow b = row(Status::WrongName, QStringLiteral("/roms/two.zip/y.md"), QStringLiteral("Y.md"));
  b.gameName = QStringLiteral("Game Y");
  const FixPlan plan = DatAudit::computeFixPlan({a, b}, FixSettings{});
  QVERIFY(plan.isEmpty());
}

void TestDatAuditFix::planRepacks7zContainerPreservingExtension() {
  // A misnamed .7z set repacks too, keeping the .7z container extension.
  AuditRow r = row(Status::WrongName, QStringLiteral("/roms/Game (old).7z/Game (old).md"),
                   QStringLiteral("Game.md"));
  r.gameName = QStringLiteral("Game");
  const FixPlan plan = DatAudit::computeFixPlan({r}, FixSettings{});
  QCOMPARE(plan.actions.size(), 1);
  QCOMPARE(plan.actions.first().kind, FixActionKind::Repack);
  QCOMPARE(plan.actions.first().fromPath, QStringLiteral("/roms/Game (old).7z"));
  QCOMPARE(plan.actions.first().toPath, QStringLiteral("/roms/Game.7z")); // .7z preserved
}

void TestDatAuditFix::planSkipsNonRepackableArchiveContainer() {
  // libarchive can only write zip + 7z — a misnamed .rar set is left untouched
  // (no repack action) rather than rewritten into the wrong format.
  AuditRow r = row(Status::WrongName, QStringLiteral("/roms/Game (old).rar/Game (old).md"),
                   QStringLiteral("Game.md"));
  r.gameName = QStringLiteral("Game");
  const FixPlan plan = DatAudit::computeFixPlan({r}, FixSettings{});
  QVERIFY(plan.isEmpty());
}

void TestDatAuditFix::planSkipsCollidingInnerRenames() {
  // Two misnamed members that would both rename to the SAME canonical name are
  // ambiguous — repacking would write a duplicate entry and silently drop a ROM,
  // so the container is skipped (DATA-LOSS guard).
  auto member = [](const QString &path, const QString &expected) {
    AuditRow r = row(Status::WrongName, path, expected);
    r.gameName = QStringLiteral("G");
    return r;
  };
  const QList<AuditRow> rows{
      member(QStringLiteral("/roms/G.zip/a (old).md"), QStringLiteral("Same.md")),
      member(QStringLiteral("/roms/G.zip/b (old).md"), QStringLiteral("Same.md"))};
  const FixPlan plan = DatAudit::computeFixPlan(rows, FixSettings{});
  QVERIFY(plan.isEmpty());
}

void TestDatAuditFix::planSkipsRenameOntoExistingMember() {
  // A misnamed member whose canonical name equals a sibling Have member's
  // current name would collide on extraction — skip rather than drop a ROM.
  AuditRow wrong =
      row(Status::WrongName, QStringLiteral("/roms/G.zip/old.md"), QStringLiteral("keep.md"));
  wrong.gameName = QStringLiteral("G");
  AuditRow have =
      row(Status::Have, QStringLiteral("/roms/G.zip/keep.md"), QStringLiteral("keep.md"));
  have.gameName = QStringLiteral("G");
  const FixPlan plan = DatAudit::computeFixPlan({wrong, have}, FixSettings{});
  QVERIFY(plan.isEmpty());
}

void TestDatAuditFix::planRepackPreservesNestedMemberSubdir() {
  // A misnamed member inside an in-archive subdirectory keeps its subdir — only
  // the leaf is renamed, never flattened to the archive root.
  AuditRow r = row(Status::WrongName, QStringLiteral("/roms/G.zip/sub/old.md"),
                   QStringLiteral("Canonical.md"));
  r.gameName = QStringLiteral("G");
  const FixPlan plan = DatAudit::computeFixPlan({r}, FixSettings{});
  QCOMPARE(plan.actions.size(), 1);
  QCOMPARE(plan.actions.first().kind, FixActionKind::Repack);
  QCOMPARE(plan.actions.first().innerRenames.size(), 1);
  QCOMPARE(plan.actions.first().innerRenames.first().first, QStringLiteral("sub/old.md"));
  QCOMPARE(plan.actions.first().innerRenames.first().second, QStringLiteral("sub/Canonical.md"));
}

void TestDatAuditFix::planRenamesMisnamedContainerWithCanonicalInner() {
  // The inner ROM is already canonical (Have) but the .zip is misnamed -> a
  // PLAIN container rename to <game>.zip, no rewrite (works for any format).
  AuditRow r =
      row(Status::Have, QStringLiteral("/roms/WRONGNAME.zip/Game.md"), QStringLiteral("Game.md"));
  r.gameName = QStringLiteral("Game");
  const FixPlan plan = DatAudit::computeFixPlan({r}, FixSettings{});
  QCOMPARE(plan.actions.size(), 1);
  QCOMPARE(plan.actions.first().kind, FixActionKind::Rename); // plain rename, not repack
  QCOMPARE(plan.actions.first().fromPath, QStringLiteral("/roms/WRONGNAME.zip"));
  QCOMPARE(plan.actions.first().toPath, QStringLiteral("/roms/Game.zip"));
  QVERIFY(plan.actions.first().innerRenames.isEmpty());
}

void TestDatAuditFix::applyInPlaceRepackBackupSwapAndUndo() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("in-place repack apply needs libarchive to build + read the fixture zip");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Container name already canonical -> repack is IN-PLACE (toPath == fromPath).
  const QString container = dir.filePath(QStringLiteral("G.zip"));
  const QByteArray rom = QByteArrayLiteral("in-place rom \x07\x08");
  QVERIFY(makeZip(container, {{QStringLiteral("old.md"), rom}}));

  // The in-place swap stages the original to a dotted (hidden) sidecar.
  const QString backupPath = QFileInfo(container).path() + QStringLiteral("/.") +
                             QFileInfo(container).fileName() + QStringLiteral(".kartend-bak");

  FixPlan plan;
  FixAction a;
  a.kind = FixActionKind::Repack;
  a.fromPath = container;
  a.toPath = container; // in-place
  a.sourceStatus = Status::WrongName;
  a.innerRenames.append({QStringLiteral("old.md"), QStringLiteral("new.md")});
  plan.actions.append(a);

  auto res = DatAudit::applyFixPlan(plan, /*dryRun=*/false);
  QCOMPARE(res.applied, 1);
  QCOMPARE(res.failed, 0);
  QVERIFY(QFile::exists(container));   // container stays put
  QVERIFY(!QFile::exists(backupPath)); // backup cleaned up
  {
    const QMap<QString, QByteArray> out = readZip(container);
    QCOMPARE(out.value(QStringLiteral("new.md")), rom);
    QVERIFY(!out.contains(QStringLiteral("old.md")));
  }

  QCOMPARE(DatAudit::applyUndo(res.undo), 1);
  {
    const QMap<QString, QByteArray> out = readZip(container);
    QCOMPARE(out.value(QStringLiteral("old.md")), rom); // restored
    QVERIFY(!out.contains(QStringLiteral("new.md")));
  }
  QVERIFY(!QFile::exists(backupPath));
#endif
}

void TestDatAuditFix::planRelocatePerItemSubfolderUsesGameName() {
  // Structured managed output (Kartend-m6qsb.14): present files go under a
  // per-item subfolder named for their game; rows without a game name fall back
  // to a flat destination, and the game name is filesystem-sanitised.
  auto present = [](const QString &path, const QString &expected, const QString &game) {
    AuditRow r = row(Status::Have, path, expected);
    r.gameName = game;
    return r;
  };
  const QList<AuditRow> rows{
      present(QStringLiteral("/roms/disc1.bin"), QStringLiteral("disc1.bin"),
              QStringLiteral("Game: Alpha (USA)")), // ':' is illegal -> sanitised
      present(QStringLiteral("/roms/disc2.bin"), QStringLiteral("disc2.bin"),
              QStringLiteral("Game: Alpha (USA)")),
      present(QStringLiteral("/roms/loose.bin"), QStringLiteral("loose.bin"), QString())};

  FixSettings s;
  s.rename = false;
  s.relocateToManagedOutput = true;
  s.managedOutputRoot = QStringLiteral("/out");
  s.managedOutputPerItemSubfolder = true;
  const FixPlan plan = DatAudit::computeFixPlan(rows, s);
  QCOMPARE(plan.actions.size(), 3);
  // The two discs group under one sanitised game folder (':' -> '_').
  QCOMPARE(plan.actions[0].toPath, QStringLiteral("/out/Game_ Alpha (USA)/disc1.bin"));
  QCOMPARE(plan.actions[1].toPath, QStringLiteral("/out/Game_ Alpha (USA)/disc2.bin"));
  // No game name -> flat under the root.
  QCOMPARE(plan.actions[2].toPath, QStringLiteral("/out/loose.bin"));

  // Flag off -> everything flat (unchanged behaviour).
  s.managedOutputPerItemSubfolder = false;
  const FixPlan flat = DatAudit::computeFixPlan(rows, s);
  QCOMPARE(flat.actions[0].toPath, QStringLiteral("/out/disc1.bin"));
}

void TestDatAuditFix::applyRenameInPlaceAndUndo() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString from = makeFile(dir, QStringLiteral("misnamed.bin"), QByteArrayLiteral("data"));
  QVERIFY(!from.isEmpty());
  const QString to = dir.filePath(QStringLiteral("Canonical.bin"));

  FixPlan plan;
  plan.actions.append(DatAudit::FixAction{FixActionKind::Rename, from, to, Status::WrongName, {}});
  auto res = DatAudit::applyFixPlan(plan, /*dryRun=*/false);
  QCOMPARE(res.applied, 1);
  QCOMPARE(res.failed, 0);
  QVERIFY(!QFileInfo::exists(from));
  QVERIFY(QFileInfo::exists(to));

  QCOMPARE(DatAudit::applyUndo(res.undo), 1);
  QVERIFY(QFileInfo::exists(from));
  QVERIFY(!QFileInfo::exists(to));
}

void TestDatAuditFix::applyRelocateCopiesLeavesOriginalAndUndoDeletes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString from = makeFile(dir, QStringLiteral("a.bin"), QByteArrayLiteral("payload"));
  const QString out = dir.filePath(QStringLiteral("managed/a.bin"));

  FixPlan plan;
  plan.actions.append(DatAudit::FixAction{FixActionKind::Relocate, from, out, Status::Have, {}});
  auto res = DatAudit::applyFixPlan(plan, false);
  QCOMPARE(res.applied, 1);
  QVERIFY(QFileInfo::exists(from)); // original untouched (copy)
  QVERIFY(QFileInfo::exists(out));  // managed copy created (parent dir auto-made)

  QCOMPARE(DatAudit::applyUndo(res.undo), 1);
  QVERIFY(QFileInfo::exists(from)); // still there
  QVERIFY(!QFileInfo::exists(out)); // copy removed
}

void TestDatAuditFix::applyQuarantineMovesAndUndoRestores() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString from = makeFile(dir, QStringLiteral("junk.bin"), QByteArrayLiteral("?"));
  const QString quar = dir.filePath(QStringLiteral("quarantine/junk.bin"));

  FixPlan plan;
  plan.actions.append(
      DatAudit::FixAction{FixActionKind::Quarantine, from, quar, Status::Unknown, {}});
  auto res = DatAudit::applyFixPlan(plan, false);
  QCOMPARE(res.applied, 1);
  QVERIFY(!QFileInfo::exists(from)); // moved out
  QVERIFY(QFileInfo::exists(quar));

  QCOMPARE(DatAudit::applyUndo(res.undo), 1);
  QVERIFY(QFileInfo::exists(from));
  QVERIFY(!QFileInfo::exists(quar));
}

void TestDatAuditFix::applyDryRunMakesNoChange() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString from = makeFile(dir, QStringLiteral("misnamed.bin"), QByteArrayLiteral("d"));
  const QString to = dir.filePath(QStringLiteral("Canonical.bin"));

  FixPlan plan;
  plan.actions.append(DatAudit::FixAction{FixActionKind::Rename, from, to, Status::WrongName, {}});
  auto res = DatAudit::applyFixPlan(plan, /*dryRun=*/true);
  QCOMPARE(res.applied, 1); // counted as "would apply"
  QVERIFY(res.undo.isEmpty());
  QVERIFY(QFileInfo::exists(from)); // nothing moved
  QVERIFY(!QFileInfo::exists(to));
}

void TestDatAuditFix::applyCollisionGuardSkips() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString from = makeFile(dir, QStringLiteral("src.bin"), QByteArrayLiteral("one"));
  const QString to = makeFile(dir, QStringLiteral("dst.bin"), QByteArrayLiteral("two"));

  FixPlan plan;
  plan.actions.append(DatAudit::FixAction{FixActionKind::Rename, from, to, Status::WrongName, {}});
  auto res = DatAudit::applyFixPlan(plan, false);
  QCOMPARE(res.skipped, 1);
  QCOMPARE(res.applied, 0);
  // Neither file disturbed.
  QVERIFY(QFileInfo::exists(from));
  QCOMPARE(QFile(to).size(), qint64(3)); // "two" still there, not overwritten
}

void TestDatAuditFix::applyRepackRenamesInnerAndContainerAndUndo() {
#ifndef KARTEND_HAS_LIBARCHIVE
  QSKIP("repack apply needs libarchive to build + read the fixture zip");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString container = dir.filePath(QStringLiteral("Game (1995-07-07).zip"));
  const QString newContainer = dir.filePath(QStringLiteral("Game.zip"));
  const QByteArray rom = QByteArrayLiteral("\x10\x20 inner rom \x30\x40");
  QVERIFY(makeZip(container, {{QStringLiteral("Game (1995-07-07).md"), rom}}));

  FixPlan plan;
  FixAction a;
  a.kind = FixActionKind::Repack;
  a.fromPath = container;
  a.toPath = newContainer;
  a.sourceStatus = Status::WrongName;
  a.innerRenames.append({QStringLiteral("Game (1995-07-07).md"), QStringLiteral("Game.md")});
  plan.actions.append(a);

  auto res = DatAudit::applyFixPlan(plan, /*dryRun=*/false);
  QCOMPARE(res.applied, 1);
  QCOMPARE(res.failed, 0);
  QVERIFY(!QFile::exists(container));   // old container removed
  QVERIFY(QFile::exists(newContainer)); // renamed container
  {
    const QMap<QString, QByteArray> out = readZip(newContainer);
    QVERIFY(!out.contains(QStringLiteral("Game (1995-07-07).md")));
    QCOMPARE(out.value(QStringLiteral("Game.md")), rom); // inner renamed, bytes intact
  }

  // Undo restores the original container name AND the inner entry name.
  QCOMPARE(DatAudit::applyUndo(res.undo), 1);
  QVERIFY(QFile::exists(container));
  QVERIFY(!QFile::exists(newContainer));
  {
    const QMap<QString, QByteArray> out = readZip(container);
    QCOMPARE(out.value(QStringLiteral("Game (1995-07-07).md")), rom);
    QVERIFY(!out.contains(QStringLiteral("Game.md")));
  }
#endif
}

QTEST_MAIN(TestDatAuditFix)
#include "test_datauditfix.moc"
