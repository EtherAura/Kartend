#include "cmdexequoting.h"

#include <QLatin1Char>

namespace CmdExeQuoting {

QString quoteForCmdExe(const QString &arg) {
  // Layer 1 — CommandLineToArgvW quoting, always force-quoted so the structure
  // is predictable for layer 2. Per the documented MSVCRT parsing rules a run
  // of N backslashes is doubled to 2N+1 when it precedes a literal `"`, doubled
  // to 2N when it precedes the closing quote, and left as N otherwise.
  QString quoted;
  quoted.reserve(arg.size() + 2);
  quoted += QLatin1Char('"');
  int backslashes = 0;
  for (const QChar ch : arg) {
    if (ch == QLatin1Char('\\')) {
      ++backslashes;
      continue;
    }
    if (ch == QLatin1Char('"')) {
      quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
      quoted += QLatin1Char('"');
    } else {
      quoted += QString(backslashes, QLatin1Char('\\'));
      quoted += ch;
    }
    backslashes = 0;
  }
  // Trailing backslashes sit right before the closing quote — double them so
  // they stay literal instead of escaping the quote.
  quoted += QString(backslashes * 2, QLatin1Char('\\'));
  quoted += QLatin1Char('"');

  // Layer 2 — caret-escape every cmd.exe metacharacter (including the quotes
  // emitted by layer 1) so cmd strips the carets and forwards the literal
  // characters to the CommandLineToArgvW pass rather than acting on them.
  static const QString cmdMeta = QStringLiteral("()%!^\"<>&|");
  QString escaped;
  escaped.reserve(quoted.size() * 2);
  for (const QChar ch : quoted) {
    if (cmdMeta.contains(ch)) {
      escaped += QLatin1Char('^');
    }
    escaped += ch;
  }
  return escaped;
}

} // namespace CmdExeQuoting
