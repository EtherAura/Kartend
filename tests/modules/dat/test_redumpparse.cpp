/**
 * @file test_redumpparse.cpp
 * @brief Unit tests for the redump.org systems-index parser (Kartend-m6qsb.23).
 *
 * Fixtures mirror the /datfile/ index shape captured on 2026-06-13: a list of
 * <a href="…/discs/system/<slug>/">Name</a> links whose slug doubles as the
 * /datfile/<slug>/ download key.
 */

#include "redumpparse.h"

#include <QByteArray>
#include <QTest>

using RedumpParse::System;

class TestRedumpParse : public QObject {
  Q_OBJECT

private slots:
  void parsesSystemsSlugAndName();
  void dedupesAndIgnoresNonSystemLinks();
  void emptyOnUnrecognisedPage();
};

void TestRedumpParse::parsesSystemsSlugAndName() {
  const QByteArray html =
      QByteArrayLiteral("<html><body>"
                        "<a href=\"/discs/system/psx/\">Sony - PlayStation</a>"
                        "<a href=\"http://redump.org/discs/system/ss/\">Sega - Saturn</a>"
                        "<a href=\"/discs/system/dc\">Sega - Dreamcast</a>" // no trailing slash
                        "</body></html>");
  const QList<System> sys = RedumpParse::parseSystems(html);
  QCOMPARE(sys.size(), 3);
  QCOMPARE(sys.at(0).slug, QStringLiteral("psx"));
  QCOMPARE(sys.at(0).name, QStringLiteral("Sony - PlayStation"));
  QCOMPARE(sys.at(1).slug, QStringLiteral("ss"));
  QCOMPARE(sys.at(2).slug, QStringLiteral("dc"));
}

void TestRedumpParse::dedupesAndIgnoresNonSystemLinks() {
  const QByteArray html = QByteArrayLiteral(
      "<a href=\"/discs/system/psx/\">PlayStation</a>"
      "<a href=\"/discs/system/psx/serial/\">PlayStation (serials)</a>" // same slug → deduped
      "<a href=\"/cues/psx/\">cuesheets</a>"                            // not a system link
      "<a href=\"/about/\">About</a>");
  const QList<System> sys = RedumpParse::parseSystems(html);
  QCOMPARE(sys.size(), 1);
  QCOMPARE(sys.at(0).slug, QStringLiteral("psx"));
}

void TestRedumpParse::emptyOnUnrecognisedPage() {
  QVERIFY(RedumpParse::parseSystems(QByteArrayLiteral("<html>nope</html>")).isEmpty());
  QVERIFY(RedumpParse::parseSystems(QByteArray()).isEmpty());
}

QTEST_MAIN(TestRedumpParse)
#include "test_redumpparse.moc"
