/**
 * @file test_transferrateformat.cpp
 * @brief Unit tests for TransferRateFormat::formatTransferRate — the shared
 * bytes-over-elapsed → "N KiB/s" / "N.N MiB/s" readout used by both scrape
 * result dialogs. Pure math + formatting, no widgets.
 */

#include "dialogs/scraper/result/transferrateformat.h"
#include <QTest>

class TestTransferRateFormat : public QObject {
  Q_OBJECT

private slots:
  void mibRateUsesOneDecimal();
  void exactlyOneMibPerSecondIsMib();
  void subMibRateFallsBackToWholeKib();
  void zeroBytesIsZeroKib();
  void zeroElapsedClampsToOneMillisecond();
};

void TestTransferRateFormat::mibRateUsesOneDecimal() {
  // 5 MiB over 2 s = 2.5 MiB/s.
  QCOMPARE(TransferRateFormat::formatTransferRate(5LL * 1024 * 1024, 2000),
           QStringLiteral("2.5 MiB/s"));
}

void TestTransferRateFormat::exactlyOneMibPerSecondIsMib() {
  // The >= 1.0 boundary formats as MiB, not 1024 KiB.
  QCOMPARE(TransferRateFormat::formatTransferRate(1024 * 1024, 1000), QStringLiteral("1.0 MiB/s"));
}

void TestTransferRateFormat::subMibRateFallsBackToWholeKib() {
  // 512 KiB over 1 s stays below the MiB threshold; KiB prints with no decimals.
  QCOMPARE(TransferRateFormat::formatTransferRate(512 * 1024, 1000), QStringLiteral("512 KiB/s"));
}

void TestTransferRateFormat::zeroBytesIsZeroKib() {
  QCOMPARE(TransferRateFormat::formatTransferRate(0, 5000), QStringLiteral("0 KiB/s"));
}

void TestTransferRateFormat::zeroElapsedClampsToOneMillisecond() {
  // elapsedMs <= 0 clamps to 1 ms instead of dividing by zero:
  // 1 MiB over (clamped) 1 ms = 1000.0 MiB/s.
  QCOMPARE(TransferRateFormat::formatTransferRate(1024 * 1024, 0), QStringLiteral("1000.0 MiB/s"));
}

QTEST_APPLESS_MAIN(TestTransferRateFormat)
#include "test_transferrateformat.moc"
