// Tests for DatAuditModel: row/column shape, display data, custom roles, and
// the status filter (which backs both per-status filtering and the
// file-centric vs completeness framing toggle).

#include <QList>
#include <QModelIndex>
#include <QSet>
#include <QTest>

#include "datauditmodel.h"
#include "dataudittypes.h"

using DatAudit::AuditRow;
using DatAudit::DatAuditModel;
using DatAudit::Status;

namespace {

AuditRow mk(Status status, const QString &game, const QString &expected, const QString &file,
            qint64 size) {
  AuditRow r;
  r.status = status;
  r.gameName = game;
  r.expectedName = expected;
  r.filePath = file;
  r.actualName = file.section(QLatin1Char('/'), -1);
  r.size = size;
  return r;
}

QList<AuditRow> sampleRows() {
  return {mk(Status::Have, QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"),
             QStringLiteral("/r/Alpha.bin"), 10),
          mk(Status::WrongName, QStringLiteral("Beta"), QStringLiteral("Beta.bin"),
             QStringLiteral("/r/bad.bin"), 20),
          mk(Status::Unknown, QString(), QString(), QStringLiteral("/r/x.bin"), 30),
          mk(Status::Missing, QStringLiteral("Gamma"), QStringLiteral("Gamma.bin"), QString(), -1)};
}

} // namespace

class TestDatAuditModel : public QObject {
  Q_OBJECT

private slots:
  void shapeAndHeaders();
  void displayData();
  void customRoles();
  void statusFilterRestrictsRows();
  void clearingFilterShowsAll();
  void rowAtFollowsFilter();
};

void TestDatAuditModel::shapeAndHeaders() {
  DatAuditModel m;
  m.setRows(sampleRows());
  QCOMPARE(m.rowCount(), 4);
  QCOMPARE(m.columnCount(), int(DatAuditModel::ColumnCount));
  QCOMPARE(m.headerData(DatAuditModel::StatusColumn, Qt::Horizontal, Qt::DisplayRole).toString(),
           QStringLiteral("Status"));
}

void TestDatAuditModel::displayData() {
  DatAuditModel m;
  m.setRows(sampleRows());
  QCOMPARE(m.data(m.index(0, DatAuditModel::StatusColumn)).toString(), QStringLiteral("Have"));
  QCOMPARE(m.data(m.index(0, DatAuditModel::GameColumn)).toString(), QStringLiteral("Alpha"));
  QCOMPARE(m.data(m.index(1, DatAuditModel::FileColumn)).toString(), QStringLiteral("bad.bin"));
  QCOMPARE(m.data(m.index(0, DatAuditModel::SizeColumn)).toLongLong(), qint64(10));
}

void TestDatAuditModel::customRoles() {
  DatAuditModel m;
  m.setRows(sampleRows());
  QCOMPARE(m.data(m.index(0, 0), DatAuditModel::StatusRole).toInt(), int(Status::Have));
  QCOMPARE(m.data(m.index(2, 0), DatAuditModel::FilePathRole).toString(),
           QStringLiteral("/r/x.bin"));
}

void TestDatAuditModel::statusFilterRestrictsRows() {
  DatAuditModel m;
  m.setRows(sampleRows());
  m.setVisibleStatuses(QSet<Status>{Status::Missing, Status::Unknown});
  QCOMPARE(m.rowCount(), 2);
  // Order preserved (Unknown row precedes Missing row in the source).
  QCOMPARE(m.data(m.index(0, DatAuditModel::StatusColumn)).toString(), QStringLiteral("Unknown"));
  QCOMPARE(m.data(m.index(1, DatAuditModel::StatusColumn)).toString(), QStringLiteral("Missing"));
}

void TestDatAuditModel::clearingFilterShowsAll() {
  DatAuditModel m;
  m.setRows(sampleRows());
  m.setVisibleStatuses(QSet<Status>{Status::Have});
  QCOMPARE(m.rowCount(), 1);
  m.setVisibleStatuses(std::nullopt);
  QCOMPARE(m.rowCount(), 4);
}

void TestDatAuditModel::rowAtFollowsFilter() {
  DatAuditModel m;
  m.setRows(sampleRows());
  m.setVisibleStatuses(QSet<Status>{Status::WrongName});
  const AuditRow *r = m.rowAt(0);
  QVERIFY(r != nullptr);
  QCOMPARE(r->gameName, QStringLiteral("Beta"));
  QVERIFY(m.rowAt(1) == nullptr);
}

QTEST_MAIN(TestDatAuditModel)
#include "test_datauditmodel.moc"
