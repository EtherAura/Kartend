#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QString>

namespace StringUtils {

/// Formats an integer with comma-separated thousands (e.g., 1234567 ->
/// "1,234,567")
[[nodiscard]] inline auto formatCountNumber(qint64 value) -> QString {
  QString digits = QString::number(value);
  int pos = digits.size() - 3;
  while (pos > 0) {
    digits.insert(pos, ',');
    pos -= 3;
  }
  return digits;
}

} // namespace StringUtils

#endif // STRINGUTILS_H
