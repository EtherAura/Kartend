#ifndef TRANSFERRATEFORMAT_H
#define TRANSFERRATEFORMAT_H

#include <algorithm>
#include <QCoreApplication>
#include <QString>

// Compact transfer-rate formatting shared by the single-item progress readout
// (ScrapeResultDialog::updateSingleItemProgress) and the unified dialog's
// timing label (ScrapeResultDialogUnified::updateUnifiedProgressLabel), which
// previously carried byte-identical local copies. Only the units math +
// formatting live here — the sampling policy (cumulative bytes over the whole
// download vs a sliding-window delta) stays with each caller.
namespace TransferRateFormat {

// Formats @p bytes transferred over @p elapsedMs as "N.N MiB/s" when the rate
// reaches 1 MiB/s, otherwise "N KiB/s" (whole KiB). @p elapsedMs is clamped to
// 1 so a zero-duration sample can't divide by zero.
[[nodiscard]] inline QString formatTransferRate(qint64 bytes, qint64 elapsedMs) {
  elapsedMs = std::max<qint64>(1, elapsedMs);
  const double mibPerSec = (bytes / (1024.0 * 1024.0)) / (elapsedMs / 1000.0);
  if (mibPerSec >= 1.0) {
    return QCoreApplication::translate("TransferRateFormat", "%1 MiB/s").arg(mibPerSec, 0, 'f', 1);
  }
  return QCoreApplication::translate("TransferRateFormat", "%1 KiB/s")
      .arg(mibPerSec * 1024.0, 0, 'f', 0);
}

} // namespace TransferRateFormat

#endif // TRANSFERRATEFORMAT_H
