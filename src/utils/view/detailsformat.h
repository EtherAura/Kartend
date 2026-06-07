#ifndef DETAILSFORMAT_H
#define DETAILSFORMAT_H

#include <algorithm>

#include <QChar>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include "stringutils.h"

// Pure, state-free metadata formatters for the Details section / detail page.
// Extracted off the DetailsPane widget so they have a real home and downstream
// callers (panel, overlay, tests) don't reach them through a ~900-LOC widget.
namespace DetailsFormat {

[[nodiscard]] inline QString formatFileSize(qint64 bytes) {
  return StringUtils::formatFileSize(bytes);
}

[[nodiscard]] inline QString formatRuntime(int seconds) {
  if (seconds < 0) {
    return {};
  }
  const int hours = seconds / 3600;
  const int minutes = (seconds % 3600) / 60;
  const int secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
  }
  if (minutes > 0) {
    return QStringLiteral("%1m %2s").arg(minutes).arg(secs, 2, 10, QChar('0'));
  }
  return QStringLiteral("%1s").arg(secs);
}

[[nodiscard]] inline QString formatPersonalRating(int rating) {
  if (rating < 0) {
    return {};
  }
  const int clamped = std::clamp(rating, 0, 10);
  const int fullStars = clamped / 2;
  const bool halfStar = (clamped % 2) != 0;
  const int emptyStars = 5 - fullStars - (halfStar ? 1 : 0);

  QString glyphs;
  glyphs.reserve(5);
  for (int i = 0; i < fullStars; ++i) {
    glyphs.append(QChar(0x2605)); // ★
  }
  if (halfStar) {
    glyphs.append(QChar(0x00BD)); // ½ — placed where the half star would be.
  }
  for (int i = 0; i < emptyStars; ++i) {
    glyphs.append(QChar(0x2606)); // ☆
  }

  const double stars = clamped / 2.0;
  const QString fraction =
      QString::number(stars, 'f', stars == int(stars) ? 0 : 1) + QStringLiteral(" / 5");
  return QStringLiteral("%1 (%2)").arg(glyphs, fraction);
}

[[nodiscard]] inline QString formatTags(const QString &raw) {
  // Accept either a JSON array string or a comma-separated list. We do not pull
  // in QJsonDocument here — the Details section renders whatever the source
  // provides with light cleanup.
  QString trimmed = raw.trimmed();
  if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
    trimmed.chop(1);
    trimmed.remove(0, 1);
    trimmed.replace('"', QString());
  }
  QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
  for (QString &p : parts) {
    p = p.trimmed();
  }
  parts.removeAll(QString());
  return parts.join(QStringLiteral(", "));
}

[[nodiscard]] inline QString formatLastScanned(const QDateTime &lastScanned) {
  if (!lastScanned.isValid()) {
    return QCoreApplication::translate("DetailsFormat", "never");
  }
  return lastScanned.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

} // namespace DetailsFormat

#endif // DETAILSFORMAT_H
