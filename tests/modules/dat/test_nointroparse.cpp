/**
 * @file test_nointroparse.cpp
 * @brief Unit tests for the No-Intro DAT-o-MATIC page parsers (Kartend-m6qsb.16).
 *
 * Fixtures mirror the real markup captured from datomatic.no-intro.org on
 * 2026-06-13 (form field names, the daily_dl_<date> Request button, the 32-hex
 * Download!! token, Content-Disposition). The live flow is runtime-gated; these
 * pin the brittle parsing that turns its responses into actions.
 */

#include "nointroparse.h"

#include <QByteArray>
#include <QTest>

using NoIntroParse::DailyForm;

namespace {

// Trimmed but faithful daily-download form: dat_type radios, set checkboxes
// (some default-checked), and the dated Request submit.
const QByteArray kDailyPage = QByteArrayLiteral(R"HTML(
<html><body>
<form action="index.php" method="post"><input type="submit" name="login" value="Staff login" /></form>
<form action="index.php?page=download&op=daily&s=64" name="daily" method="post">
  <input type="radio" name="dat_type" value="dat" checked="checked" onChange="..."/>
  <input type="radio" name="dat_type" value="pc"  onChange="..."/>
  <input type="radio" name="dat_type" value="db"  onChange="..."/>
  <input type="checkbox" name="include_numbered" value="Ok" onChange="..."> Numbered
  <input type="checkbox" name="set1" value="Ok" checked="checked" onChange="..."> Main
  <input type="checkbox" name="set2" value="Ok" checked="checked" onChange="..."> Demos
  <input type="checkbox" name="set8" value="Ok"  onChange="...">
  <input type="submit" name="daily_dl_2026-05-30" value="Request" />
</form>
</body></html>
)HTML");

const QByteArray kConfirmPage = QByteArrayLiteral(R"HTML(
<html><body>
<form action="index.php" method="post"><input type="submit" name="login" value="Staff login" /></form>
<form action="" method="post">
  <input type="submit" name="1c763e800da745e5f19a6430acba7f66" value="Download!!">
</form>
</body></html>
)HTML");

} // namespace

class TestNoIntroParse : public QObject {
  Q_OBJECT

private slots:
  void parsesDailyForm();
  void invalidWhenNoDailyForm();
  void parsesConfirmToken();
  void confirmTokenAbsentReturnsEmpty();
  void contentDispositionFilenameVariants();
};

void TestNoIntroParse::parsesDailyForm() {
  const DailyForm f = NoIntroParse::parseDailyPage(kDailyPage);
  QVERIFY(f.valid);
  QCOMPARE(f.requestField, QStringLiteral("daily_dl_2026-05-30"));
  QCOMPARE(f.packDate, QStringLiteral("2026-05-30"));
  QCOMPARE(f.defaultDatType, QStringLiteral("dat"));
  QCOMPARE(f.datTypes,
           (QStringList{QStringLiteral("dat"), QStringLiteral("pc"), QStringLiteral("db")}));
  // include_numbered must NOT be treated as a set (it lacks the set prefix).
  QCOMPARE(f.sets.size(), 3);
  QCOMPARE(f.sets.at(0).field, QStringLiteral("set1"));
  QCOMPARE(f.sets.at(0).label, QStringLiteral("Main")); // human label, not the field name
  QCOMPARE(f.sets.at(1).label, QStringLiteral("Demos"));
  QCOMPARE(f.sets.at(2).label, QStringLiteral("set8")); // no label text → field-name fallback
  QVERIFY(f.sets.at(0).checkedByDefault);
  QVERIFY(f.sets.at(1).checkedByDefault);  // set2
  QVERIFY(!f.sets.at(2).checkedByDefault); // set8
}

void TestNoIntroParse::invalidWhenNoDailyForm() {
  // A page without the named form / Request button is a changed-site / anti-bot
  // signal — must report invalid rather than fabricate a request.
  QVERIFY(
      !NoIntroParse::parseDailyPage(QByteArrayLiteral("<html><body>blocked</body></html>")).valid);
  // Right form name but no Request submit → still invalid (nothing to POST).
  const QByteArray noButton =
      QByteArrayLiteral("<form name=\"daily\" method=\"post\"><input type=\"checkbox\" "
                        "name=\"set1\" value=\"Ok\"></form>");
  QVERIFY(!NoIntroParse::parseDailyPage(noButton).valid);
}

void TestNoIntroParse::parsesConfirmToken() {
  QCOMPARE(NoIntroParse::parseConfirmToken(kConfirmPage),
           QStringLiteral("1c763e800da745e5f19a6430acba7f66"));
}

void TestNoIntroParse::confirmTokenAbsentReturnsEmpty() {
  // A hex-named field that is NOT the Download button must be ignored.
  const QByteArray decoy = QByteArrayLiteral(
      "<input type=\"hidden\" name=\"deadbeefdeadbeefdeadbeefdeadbeef\" value=\"x\">");
  QVERIFY(NoIntroParse::parseConfirmToken(decoy).isEmpty());
  QVERIFY(NoIntroParse::parseConfirmToken(QByteArrayLiteral("<html/>")).isEmpty());
}

void TestNoIntroParse::contentDispositionFilenameVariants() {
  QCOMPARE(NoIntroParse::filenameFromContentDisposition(
               QStringLiteral("attachment; filename=\"No-Intro Love Pack (2026-06-13).zip\"")),
           QStringLiteral("No-Intro Love Pack (2026-06-13).zip"));
  QCOMPARE(NoIntroParse::filenameFromContentDisposition(
               QStringLiteral("attachment; filename*=UTF-8''pack%20a.zip")),
           QStringLiteral("pack a.zip"));
  QCOMPARE(NoIntroParse::filenameFromContentDisposition(
               QStringLiteral("attachment; filename=plain.zip")),
           QStringLiteral("plain.zip"));
  QVERIFY(NoIntroParse::filenameFromContentDisposition(QString()).isEmpty());
  QVERIFY(NoIntroParse::filenameFromContentDisposition(QStringLiteral("inline")).isEmpty());
}

QTEST_MAIN(TestNoIntroParse)
#include "test_nointroparse.moc"
