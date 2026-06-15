// Tests for the DAT-audit fix engine: pure plan computation, and apply/undo
// over real temp files (rename in place, relocate-copy to a managed root,
// quarantine move), plus the dry-run and collision-guard safety rails.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "datauditfix.h"
#include "dataudittypes.h"

using DatAudit::AuditRow;
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

} // namespace

class TestDatAuditFix : public QObject {
  Q_OBJECT

private slots:
  void planRenameOnlyByDefault();
  void planSkipsNoopRename();
  void planRelocateAndQuarantineWhenEnabled();
  void planRelocatePerItemSubfolderUsesGameName();
  void applyRenameInPlaceAndUndo();
  void applyRelocateCopiesLeavesOriginalAndUndoDeletes();
  void applyQuarantineMovesAndUndoRestores();
  void applyDryRunMakesNoChange();
  void applyCollisionGuardSkips();
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
  plan.actions.append(DatAudit::FixAction{FixActionKind::Rename, from, to, Status::WrongName});
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
  plan.actions.append(DatAudit::FixAction{FixActionKind::Relocate, from, out, Status::Have});
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
  plan.actions.append(DatAudit::FixAction{FixActionKind::Quarantine, from, quar, Status::Unknown});
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
  plan.actions.append(DatAudit::FixAction{FixActionKind::Rename, from, to, Status::WrongName});
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
  plan.actions.append(DatAudit::FixAction{FixActionKind::Rename, from, to, Status::WrongName});
  auto res = DatAudit::applyFixPlan(plan, false);
  QCOMPARE(res.skipped, 1);
  QCOMPARE(res.applied, 0);
  // Neither file disturbed.
  QVERIFY(QFileInfo::exists(from));
  QCOMPARE(QFile(to).size(), qint64(3)); // "two" still there, not overwritten
}

QTEST_MAIN(TestDatAuditFix)
#include "test_datauditfix.moc"
