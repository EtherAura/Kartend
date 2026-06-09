// Tests for the DAT-audit exporters: CSV shape/escaping, fixdat round-trip
// through DatLookup, and the miss-list.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>

#include "datauditexport.h"
#include "dataudittypes.h"
#include "datlookup.h"

using DatAudit::AuditRow;
using DatAudit::Status;

namespace {

AuditRow have(const QString &game, const QString &name, const QString &file) {
  AuditRow r;
  r.status = Status::Have;
  r.gameName = game;
  r.expectedName = name;
  r.actualName = name;
  r.filePath = file;
  r.size = 42;
  r.crc = QStringLiteral("deadbeef");
  return r;
}

AuditRow missing(const QString &game, const QString &rom, const QString &crc, const QString &sha1) {
  AuditRow r;
  r.status = Status::Missing;
  r.gameName = game;
  r.expectedName = rom;
  r.size = 100;
  r.crc = crc;
  r.sha1 = sha1;
  return r;
}

} // namespace

class TestDatAuditExport : public QObject {
  Q_OBJECT

private slots:
  void csvHasHeaderAndRows();
  void csvEscapesCommasAndQuotes();
  void fixdatContainsOnlyMissingAndRoundTrips();
  void fixdatEmptyWhenNothingMissing();
  void missListSortedUniqueMissingOnly();
};

void TestDatAuditExport::csvHasHeaderAndRows() {
  const QList<AuditRow> rows{
      have(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"), QStringLiteral("/x/Alpha.bin")),
      missing(QStringLiteral("Beta"), QStringLiteral("Beta.bin"), QStringLiteral("cafebabe"),
              QString())};
  const QString csv = QString::fromUtf8(DatAudit::toCsv(rows));
  const QStringList lines = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 3); // header + 2 rows
  QVERIFY(lines.at(0).startsWith(QStringLiteral("status,game,expected_name")));
  QVERIFY(lines.at(1).startsWith(QStringLiteral("have,Alpha,Alpha.bin")));
  QVERIFY(lines.at(2).startsWith(QStringLiteral("missing,Beta,Beta.bin")));
}

void TestDatAuditExport::csvEscapesCommasAndQuotes() {
  AuditRow r = have(QStringLiteral("Game, The \"Special\""), QStringLiteral("g.bin"),
                    QStringLiteral("/x/g.bin"));
  const QString csv = QString::fromUtf8(DatAudit::toCsv({r}));
  // The comma+quote field must be wrapped and its quotes doubled.
  QVERIFY(csv.contains(QStringLiteral("\"Game, The \"\"Special\"\"\"")));
}

void TestDatAuditExport::fixdatContainsOnlyMissingAndRoundTrips() {
  const QList<AuditRow> rows{
      have(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"), QStringLiteral("/x/Alpha.bin")),
      missing(QStringLiteral("Beta"), QStringLiteral("Beta.bin"), QStringLiteral("cafebabe"),
              QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")),
      missing(QStringLiteral("Gamma"), QStringLiteral("Gamma.bin"), QStringLiteral("0badf00d"),
              QString())};
  const QByteArray fixdat = DatAudit::toFixdat(rows);

  // Round-trips through the real Logiqx parser.
  auto parsed = DatLookup::parseLogiqxDat(fixdat);
  QVERIFY2(parsed.isOk(), qPrintable(parsed.isError() ? parsed.error().message : QString()));
  const QList<DatLookup::DatRecord> recs = parsed.value();
  QCOMPARE(recs.size(), 2); // only the two Missing entries

  QStringList roms;
  for (const DatLookup::DatRecord &r : recs) {
    roms << r.romName;
  }
  QVERIFY(roms.contains(QStringLiteral("Beta.bin")));
  QVERIFY(roms.contains(QStringLiteral("Gamma.bin")));
  QVERIFY(!roms.contains(QStringLiteral("Alpha.bin"))); // Have excluded

  for (const DatLookup::DatRecord &r : recs) {
    if (r.romName == QStringLiteral("Beta.bin")) {
      QCOMPARE(r.crc, QStringLiteral("cafebabe"));
      QCOMPARE(r.sha1, QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    }
  }
}

void TestDatAuditExport::fixdatEmptyWhenNothingMissing() {
  const QByteArray fixdat = DatAudit::toFixdat(
      {have(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"), QStringLiteral("/x/Alpha.bin"))});
  auto parsed = DatLookup::parseLogiqxDat(fixdat);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().size(), 0); // valid datafile, no games
}

void TestDatAuditExport::missListSortedUniqueMissingOnly() {
  const QList<AuditRow> rows{
      missing(QStringLiteral("Zelda"), QStringLiteral("z.bin"), QString(), QString()),
      missing(QStringLiteral("Alpha"), QStringLiteral("a.bin"), QString(), QString()),
      missing(QStringLiteral("Alpha"), QStringLiteral("a.bin"), QString(), QString()), // dup
      have(QStringLiteral("Beta"), QStringLiteral("b.bin"), QStringLiteral("/x/b.bin"))};
  const QString list = DatAudit::toMissList(rows);
  const QStringList lines = list.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(lines.size(), 2); // Alpha, Zelda (deduped, Have excluded)
  QCOMPARE(lines.at(0), QStringLiteral("Alpha"));
  QCOMPARE(lines.at(1), QStringLiteral("Zelda"));
}

QTEST_MAIN(TestDatAuditExport)
#include "test_datauditexport.moc"
