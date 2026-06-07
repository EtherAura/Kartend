#ifndef DURATIONFORMAT_H
#define DURATIONFORMAT_H

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>

// Compact duration formatting shared by the scrape-result dialog and the
// batch-progress view (which previously carried byte-identical local copies).
namespace DurationFormat {

// Format a millisecond duration as H:MM:SS, M:SS, or "Ns" (whichever is the
// most compact non-zero form). Returns an em dash for non-positive input.
[[nodiscard]] inline QString formatDurationMs(qint64 ms) {
  if (ms <= 0) return QStringLiteral("—");
  const qint64 totalSec = ms / 1000;
  const qint64 h = totalSec / 3600;
  const qint64 m = (totalSec / 60) % 60;
  const qint64 s = totalSec % 60;
  if (h > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
  }
  if (m > 0) {
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
  }
  return QCoreApplication::translate("DurationFormat", "%1s").arg(s);
}

} // namespace DurationFormat

#endif // DURATIONFORMAT_H
