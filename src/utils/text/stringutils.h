#ifndef STRINGUTILS_H
#define STRINGUTILS_H

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

} // namespace StringUtils

#endif // STRINGUTILS_H
