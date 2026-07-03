#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QCoreApplication>
#include <QLocale>
#include <QString>

namespace StringUtils {

/// Formats an integer for display using the active locale's digit grouping
/// (e.g. 1234567 -> "1,234,567" in en_US, "1.234.567" in de_DE), with the
/// sign placed correctly for negative values. Replaces a hand-rolled
/// comma-grouping loop that was locale-blind and inserted a stray separator
/// right after the minus sign for values like -123 (Kartend-ixrhn).
[[nodiscard]] inline auto formatCountNumber(qint64 value) -> QString {
  return QLocale().toString(value);
}

/// Normalizes a raw display name into a comparison/sort key: underscores and
/// dashes become spaces, whitespace runs collapse, and the result is
/// lowercased. Used by the query layer to sort media filenames the way a
/// human reads their titles. Pure text transform — moved here from PathUtils,
/// where it was off-mission among the path expansion/validation helpers.
[[nodiscard]] inline auto normalizeDisplayName(const QString &input) -> QString {
  QString out = input;
  out.replace('_', ' ').replace('-', ' ');
  out = out.simplified().toLower();
  return out;
}

/// Formats a byte count as human-readable size using binary (1024) steps:
/// "1.00 KB", "2.50 MB", "512 bytes". Units go through translate() so the label
/// and number/unit spacing stay localizable; the value uses QString::number
/// (C locale) to keep "1.00"-style output stable. Single source for what used to
/// be byte-identical helpers on DetailsPane and DetailPageOverlay (Kartend-kp7up).
[[nodiscard]] inline auto formatFileSize(qint64 bytes) -> QString {
  constexpr qint64 kKibi = 1024;
  constexpr qint64 kMebi = kKibi * 1024;
  constexpr qint64 kGibi = kMebi * 1024;
  if (bytes >= kGibi) {
    return QCoreApplication::translate("StringUtils", "%1 GB")
        .arg(QString::number(bytes / static_cast<double>(kGibi), 'f', 2));
  }
  if (bytes >= kMebi) {
    return QCoreApplication::translate("StringUtils", "%1 MB")
        .arg(QString::number(bytes / static_cast<double>(kMebi), 'f', 2));
  }
  if (bytes >= kKibi) {
    return QCoreApplication::translate("StringUtils", "%1 KB")
        .arg(QString::number(bytes / static_cast<double>(kKibi), 'f', 2));
  }
  return QCoreApplication::translate("StringUtils", "%1 bytes").arg(QString::number(bytes));
}

/// Formats a past timestamp (Unix ms) as a coarse "N ago" string relative to
/// @p nowMs — "just now", "5 minutes ago", "3 days ago", "2 years ago".
/// @p pastMs <= 0 yields "never"; a future stamp (clock skew) clamps to "just
/// now". @p nowMs is passed in rather than read from the clock so callers stay
/// deterministic + testable; production passes
/// QDateTime::currentMSecsSinceEpoch(). Singular/plural are spelled out
/// explicitly (not %n) so the untranslated English source reads cleanly — "1
/// minute ago", not "1 minute(s) ago" — while staying localizable.
[[nodiscard]] inline auto relativePastTime(qint64 pastMs, qint64 nowMs) -> QString {
  if (pastMs <= 0) {
    return QCoreApplication::translate("StringUtils", "never");
  }
  qint64 deltaMs = nowMs - pastMs;
  if (deltaMs < 0) {
    deltaMs = 0; // future stamp / clock skew — clamp to "just now"
  }
  const qint64 sec = deltaMs / 1000;
  if (sec < 60) {
    return QCoreApplication::translate("StringUtils", "just now");
  }
  const qint64 minutes = sec / 60;
  if (minutes < 60) {
    return minutes == 1 ? QCoreApplication::translate("StringUtils", "1 minute ago")
                        : QCoreApplication::translate("StringUtils", "%1 minutes ago").arg(minutes);
  }
  const qint64 hours = minutes / 60;
  if (hours < 24) {
    return hours == 1 ? QCoreApplication::translate("StringUtils", "1 hour ago")
                      : QCoreApplication::translate("StringUtils", "%1 hours ago").arg(hours);
  }
  const qint64 days = hours / 24;
  if (days < 7) {
    return days == 1 ? QCoreApplication::translate("StringUtils", "1 day ago")
                     : QCoreApplication::translate("StringUtils", "%1 days ago").arg(days);
  }
  if (days < 30) {
    const qint64 weeks = days / 7;
    return weeks == 1 ? QCoreApplication::translate("StringUtils", "1 week ago")
                      : QCoreApplication::translate("StringUtils", "%1 weeks ago").arg(weeks);
  }
  if (days < 365) {
    const qint64 months = days / 30;
    return months == 1 ? QCoreApplication::translate("StringUtils", "1 month ago")
                       : QCoreApplication::translate("StringUtils", "%1 months ago").arg(months);
  }
  const qint64 years = days / 365;
  return years == 1 ? QCoreApplication::translate("StringUtils", "1 year ago")
                    : QCoreApplication::translate("StringUtils", "%1 years ago").arg(years);
}

} // namespace StringUtils

#endif // STRINGUTILS_H
