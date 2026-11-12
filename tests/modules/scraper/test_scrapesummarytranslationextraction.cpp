// Extraction test for the scrape completion summary (Kartend-ydalk). Runs the
// configured Qt lupdate on the production scrapesummaryformat.cpp and checks
// that every completion-box source text comes out as an active message in the
// ScraperController context. lupdate only sees literal tr()/translate()/
// QT_TRANSLATE_NOOP calls, so strings handed to a local wrapper are silently
// dropped from the catalogue; the build.yml drift check cannot catch that
// because its fresh extract misses the same strings as the committed seed.
#include <QtTest/QtTest>

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#ifndef KARTEND_LUPDATE_EXECUTABLE
#error "KARTEND_LUPDATE_EXECUTABLE must be defined by the test's CMake registration"
#endif
#ifndef KARTEND_SCRAPE_SUMMARY_SOURCE
#error "KARTEND_SCRAPE_SUMMARY_SOURCE must be defined by the test's CMake registration"
#endif

namespace {
struct ExtractedMessage {
  QString context;
  QString source;
  QString translationType;
};

// Every literal completionText() can show, verbatim.
const QStringList kExpectedSources = {
    QStringLiteral("Scrape complete."),
    QStringLiteral("Scraped: %1\nSkipped: %2\nNot found: %3\nErrors: %4\nMedia written: %5"),
    QStringLiteral("Media failures: %1 fetch, %2 write"),
    QStringLiteral("All %1 artwork file(s) were already up to date."),
    QStringLiteral("The provider offered no media matching the selected artwork types."),
    QStringLiteral("Metadata sidecar write failures: %1"),
    QStringLiteral("First failures:\n%1"),
};

QList<ExtractedMessage> parseTs(const QString &path, QString *error) {
  QList<ExtractedMessage> messages;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
    return messages;
  }
  QXmlStreamReader xml(&file);
  QString context;
  ExtractedMessage current;
  bool inMessage = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      const auto name = xml.name();
      if (name == QLatin1String("context")) {
        context.clear();
      } else if (name == QLatin1String("name") && !inMessage) {
        context = xml.readElementText();
      } else if (name == QLatin1String("message")) {
        inMessage = true;
        current = ExtractedMessage{context, QString(), QString()};
      } else if (name == QLatin1String("source") && inMessage) {
        current.source = xml.readElementText();
      } else if (name == QLatin1String("translation") && inMessage) {
        current.translationType = xml.attributes().value(QLatin1String("type")).toString();
      }
    } else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
      messages.append(current);
      inMessage = false;
    }
  }
  if (xml.hasError()) *error = QStringLiteral("XML parse error: %1").arg(xml.errorString());
  return messages;
}
} // namespace

class TestScrapeSummaryTranslationExtraction : public QObject {
  Q_OBJECT
private slots:
  void lupdateExtractsEveryCompletionSummaryString();
};

void TestScrapeSummaryTranslationExtraction::lupdateExtractsEveryCompletionSummaryString() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Kartend-vbryg: Ubuntu 24.04's libtsan aborts in its fork hook
  // (`tsan_rtl.cpp:253 "((!thr->slot)) != (0)"`, via ForkAfter) when
  // QProcess forks lupdate from a QtTest binary, the same CHECK bug as the
  // other QProcess tests (see test_romhasher.cpp). This test has no threads
  // of its own for TSan to watch, so it only runs in non-TSan builds.
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
  const QString lupdate = QStringLiteral(KARTEND_LUPDATE_EXECUTABLE);
  const QString source = QStringLiteral(KARTEND_SCRAPE_SUMMARY_SOURCE);
  QVERIFY2(QFileInfo(lupdate).isExecutable(), qPrintable(lupdate));
  QVERIFY2(QFileInfo::exists(source), qPrintable(source));

  // The extract goes to a scratch dir, never next to the tracked catalogue.
  QTemporaryDir scratch;
  QVERIFY(scratch.isValid());
  const QString tsPath = scratch.filePath(QStringLiteral("scrapesummary_extract.ts"));

  QProcess proc;
  proc.setProcessChannelMode(QProcess::MergedChannels);
  proc.start(lupdate, {QStringLiteral("-no-obsolete"), QStringLiteral("-locations"),
                       QStringLiteral("none"), source, QStringLiteral("-ts"), tsPath});
  QVERIFY2(proc.waitForStarted(), qPrintable(proc.errorString()));
  QVERIFY2(proc.waitForFinished(120000), qPrintable(proc.errorString()));
  const QString output = QString::fromLocal8Bit(proc.readAll());
  QVERIFY2(proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0, qPrintable(output));

  QString error;
  const QList<ExtractedMessage> messages = parseTs(tsPath, &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  for (const QString &expected : kExpectedSources) {
    QStringList contexts;
    for (const ExtractedMessage &m : messages) {
      if (m.source != expected) continue;
      contexts << m.context;
      QVERIFY2(m.translationType != QLatin1String("vanished") &&
                   m.translationType != QLatin1String("obsolete"),
               qPrintable(QStringLiteral("inactive extract for: %1").arg(expected)));
    }
    QVERIFY2(
        contexts == QStringList{QStringLiteral("ScraperController")},
        qPrintable(QStringLiteral("source text %1 extracted in contexts [%2], expected exactly "
                                  "[ScraperController]; lupdate said:\n%3")
                       .arg(expected, contexts.join(QStringLiteral(", ")), output)));
  }
}

QTEST_GUILESS_MAIN(TestScrapeSummaryTranslationExtraction)
#include "test_scrapesummarytranslationextraction.moc"
