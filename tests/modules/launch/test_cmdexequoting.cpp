/**
 * @file test_cmdexequoting.cpp
 * @brief Unit tests for CmdExeQuoting::quoteForCmdExe — the two-layer
 * (CommandLineToArgvW + cmd.exe caret) escaping LaunchManager uses to spawn
 * Windows .cmd/.bat launchers. Pure string transform, so it is exercised
 * directly on every platform; moved here from the PathUtils tests alongside
 * the function's relocation into the launch module.
 */

#include "cmdexequoting.h"

#include <QLatin1Char>
#include <QString>
#include <QTest>

namespace {
// Emulate cmd.exe's caret processing: a `^` escapes the next character (the
// caret is consumed, the next char is emitted literally and loses any
// metacharacter/quote meaning). This is the inverse of layer 2 of
// CmdExeQuoting::quoteForCmdExe — applying it must recover the layer-1
// CommandLineToArgvW-quoted token. (Variable expansion runs in an earlier cmd
// phase and is deliberately not modelled; the round-trip inputs avoid live
// `%VAR%` names.)
QString stripCmdCarets(const QString &s) {
  QString out;
  out.reserve(s.size());
  for (int i = 0; i < s.size(); ++i) {
    if (s[i] == QLatin1Char('^') && i + 1 < s.size()) {
      out += s[i + 1];
      ++i;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Emulate CommandLineToArgvW parsing of a single force-quoted token back into
// the original argument. Only the escaping forms our encoder emits are handled
// (structural quotes, backslash-escaped literal quotes, doubled trailing
// backslashes) — which is exactly what a round-trip of our own output needs.
QString parseSingleArgvToken(const QString &cmdline) {
  QString out;
  bool inQuotes = false;
  int i = 0;
  const int n = cmdline.size();
  while (i < n) {
    if (cmdline[i] == QLatin1Char('\\')) {
      int bs = 0;
      while (i < n && cmdline[i] == QLatin1Char('\\')) {
        ++bs;
        ++i;
      }
      if (i < n && cmdline[i] == QLatin1Char('"')) {
        out += QString(bs / 2, QLatin1Char('\\'));
        if (bs % 2 == 1) {
          out += QLatin1Char('"'); // escaped literal quote
        } else {
          inQuotes = !inQuotes;
        }
        ++i;
      } else {
        out += QString(bs, QLatin1Char('\\'));
      }
    } else if (cmdline[i] == QLatin1Char('"')) {
      inQuotes = !inQuotes;
      ++i;
    } else {
      out += cmdline[i];
      ++i;
    }
  }
  return out;
}
} // namespace

class TestCmdExeQuoting : public QObject {
  Q_OBJECT

private slots:
  void testQuoteForCmdExe_exact_data();
  void testQuoteForCmdExe_exact();
  void testQuoteForCmdExe_roundTrip_data();
  void testQuoteForCmdExe_roundTrip();
};

void TestCmdExeQuoting::testQuoteForCmdExe_exact_data() {
  QTest::addColumn<QString>("arg");
  QTest::addColumn<QString>("expected");

  // Force-quoted (layer 1) then every cmd metacharacter — including the quotes
  // layer 1 added — caret-escaped (layer 2).
  QTest::newRow("plain") << "plain" << R"(^"plain^")";
  QTest::newRow("ampersand+parens") << "Sonic & Knuckles (USA).rom"
                                    << R"(^"Sonic ^& Knuckles ^(USA^).rom^")";
  QTest::newRow("percent") << "100%done" << R"(^"100^%done^")";
  QTest::newRow("bang") << "hi!there" << R"(^"hi^!there^")";
  QTest::newRow("caret") << "a^b" << R"(^"a^^b^")";
  QTest::newRow("pipe+redirect") << "a|b<c>d" << R"(^"a^|b^<c^>d^")";
  QTest::newRow("empty") << QString() << R"(^"^")";
  QTest::newRow("embedded-quote") << R"(a"b)" << R"(^"a\^"b^")";
  QTest::newRow("trailing-backslash") << R"(C:\dir\)" << R"(^"C:\dir\\^")";
}

void TestCmdExeQuoting::testQuoteForCmdExe_exact() {
  QFETCH(QString, arg);
  QFETCH(QString, expected);
  QCOMPARE(CmdExeQuoting::quoteForCmdExe(arg), expected);
}

void TestCmdExeQuoting::testQuoteForCmdExe_roundTrip_data() {
  QTest::addColumn<QString>("arg");

  QTest::newRow("plain") << "plain.rom";
  QTest::newRow("space") << "My Game.rom";
  QTest::newRow("ampersand") << "Sonic & Knuckles.rom";
  QTest::newRow("parens") << "Game (USA) (Rev 1).iso";
  QTest::newRow("all-meta") << "a&b(c)d|e<f>g^h.rom";
  QTest::newRow("percent-not-a-var") << "save_%notavar%_slot.rom";
  QTest::newRow("embedded-quote") << R"(weird"name.rom)";
  QTest::newRow("backslashes") << R"(C:\Program Files (x86)\game.rom)";
  QTest::newRow("trailing-backslash") << R"(C:\dir\)";
  QTest::newRow("windows-path-with-meta") << R"(D:\ROMs\Sonic & Knuckles (USA).bin)";
  QTest::newRow("empty") << QString();
}

void TestCmdExeQuoting::testQuoteForCmdExe_roundTrip() {
  QFETCH(QString, arg);
  // Encode, then reverse the two layers in cmd's order (caret processing first,
  // then CommandLineToArgvW) and confirm the original argument survives intact.
  const QString encoded = CmdExeQuoting::quoteForCmdExe(arg);
  const QString afterCmd = stripCmdCarets(encoded);
  const QString recovered = parseSingleArgvToken(afterCmd);
  QCOMPARE(recovered, arg);
}

QTEST_APPLESS_MAIN(TestCmdExeQuoting)
#include "test_cmdexequoting.moc"
