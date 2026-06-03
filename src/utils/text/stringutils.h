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

} // namespace StringUtils

#endif // STRINGUTILS_H
