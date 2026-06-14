/**
 * @file test_datauditresultdelegate.cpp
 * @brief Unit tests for the audit-result status tint mapping (Kartend-m6qsb.20).
 *
 * The delegate's colour logic is a free function so it's testable without a
 * running view: each meaningful status tints away from the base toward its
 * semantic hue, statuses share/differ as designed, and Unknown/reserved leave
 * the row untinted.
 */

#include "datauditresultdelegate.h"

#include <QColor>
#include <QTest>

using DatAudit::Status;
using DatAudit::statusTint;

class TestDatAuditResultDelegate : public QObject {
  Q_OBJECT

private slots:
  void haveTintsGreenish();
  void badStatusesTintReddish();
  void unknownAndReservedLeaveBaseUnchanged();
  void distinctFamiliesDiffer();
  void worksOnDarkAndLightBases();
};

void TestDatAuditResultDelegate::haveTintsGreenish() {
  const QColor base(128, 128, 128); // neutral grey
  const QColor have = statusTint(Status::Have, base);
  QVERIFY(have != base);
  // Green channel pulled up, red/blue pulled down relative to neutral.
  QVERIFY(have.green() > base.green());
  QVERIFY(have.red() < base.green());
}

void TestDatAuditResultDelegate::badStatusesTintReddish() {
  const QColor base(128, 128, 128);
  for (Status s : {Status::WrongHash, Status::Corrupt, Status::Missing}) {
    const QColor t = statusTint(s, base);
    QVERIFY2(t != base, "bad status should tint");
    QVERIFY2(t.red() > t.blue(), "bad status should lean red");
  }
}

void TestDatAuditResultDelegate::unknownAndReservedLeaveBaseUnchanged() {
  const QColor base(128, 128, 128);
  QCOMPARE(statusTint(Status::Unknown, base), base);
  QCOMPARE(statusTint(Status::Unscanned, base), base);
  QCOMPARE(statusTint(Status::BadDump, base), base);
}

void TestDatAuditResultDelegate::distinctFamiliesDiffer() {
  const QColor base(128, 128, 128);
  // Good vs bad vs gap must be visually distinct.
  QVERIFY(statusTint(Status::Have, base) != statusTint(Status::WrongHash, base));
  QVERIFY(statusTint(Status::Have, base) != statusTint(Status::Missing, base));
  QVERIFY(statusTint(Status::WrongHash, base) != statusTint(Status::Missing, base));
}

void TestDatAuditResultDelegate::worksOnDarkAndLightBases() {
  // Blending stays in range and moves the colour on both extremes.
  const QColor dark(20, 20, 20);
  const QColor light(245, 245, 245);
  const QColor dh = statusTint(Status::Have, dark);
  const QColor lh = statusTint(Status::Have, light);
  QVERIFY(dh != dark);
  QVERIFY(lh != light);
  for (const QColor &c : {dh, lh}) {
    QVERIFY(c.red() >= 0 && c.red() <= 255);
    QVERIFY(c.green() >= 0 && c.green() <= 255);
    QVERIFY(c.blue() >= 0 && c.blue() <= 255);
  }
}

QTEST_MAIN(TestDatAuditResultDelegate)
#include "test_datauditresultdelegate.moc"
