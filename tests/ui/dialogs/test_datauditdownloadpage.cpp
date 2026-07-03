// DatAuditDownloadPage — the DAT Manager's "Download" stacked page. The
// network-bound flows (daily-form fetch, redump systems fetch, the download
// itself) are runtime-verified; covered here are the paths that must never
// touch the network: construction gating (No-Intro form visible, Redump form
// hidden, download/cancel disabled, progress hidden, DAT-type combo locked
// until a form loads) and the Load guard that rejects a non-numeric /
// non-URL system id with a status message INSTEAD of dispatching a fetch.
// The injected DatAuditDownloadService gets stub provenance accessors — a
// data seam, not DB mocking. The page is constructed headlessly, never
// shown.

#include "datauditdownloadpage.h"
#include "datauditdownloadservice.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTest>

namespace {

DatAuditDownloadService::ProvenanceAccess inertProvenance() {
  return {[] { return QList<DatLibraryState::Provenance>{}; },
          [](const DatLibraryState::Provenance &) {}};
}

QPushButton *buttonWithText(const QWidget &root, const QString &text) {
  const auto buttons = root.findChildren<QPushButton *>();
  for (auto *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

QGroupBox *groupWithTitle(const QWidget &root, const QString &title) {
  const auto boxes = root.findChildren<QGroupBox *>();
  for (auto *b : boxes) {
    if (b->title() == title) {
      return b;
    }
  }
  return nullptr;
}

bool hasLabelWithText(const QWidget &root, const QString &text) {
  const auto labels = root.findChildren<QLabel *>();
  for (const auto *l : labels) {
    if (l->text() == text) {
      return true;
    }
  }
  return false;
}

} // namespace

class TestDatAuditDownloadPage : public QObject {
  Q_OBJECT

private slots:
  void constructsWithNoIntroFormAndIdleGating();
  void loadRejectsUnparseableSystemIdWithoutGoingBusy();
};

void TestDatAuditDownloadPage::constructsWithNoIntroFormAndIdleGating() {
  DatAuditDownloadService service(inertProvenance());
  DatAuditDownloadPage page(&service);

  // Source defaults to No-Intro: its two boxes are shown, Redump's hidden.
  QGroupBox *noIntro = groupWithTitle(page, QStringLiteral("No-Intro (DAT-o-MATIC)"));
  QGroupBox *sets = groupWithTitle(page, QStringLiteral("Include sets"));
  QGroupBox *redump = groupWithTitle(page, QStringLiteral("Redump"));
  QVERIFY(noIntro && sets && redump);
  QVERIFY(!noIntro->isHidden());
  QVERIFY(!sets->isHidden());
  QVERIFY(redump->isHidden());

  // Nothing loaded yet: download gated off, cancel idle, progress hidden.
  QPushButton *download = buttonWithText(page, QStringLiteral("Download && import"));
  QPushButton *cancel = buttonWithText(page, QStringLiteral("Cancel"));
  QVERIFY(download && cancel);
  QVERIFY(!download->isEnabled());
  QVERIFY(!cancel->isEnabled());
  const auto bars = page.findChildren<QProgressBar *>();
  QCOMPARE(bars.size(), 1);
  QVERIFY(bars.first()->isHidden());

  // The DAT-type combo only unlocks once a daily form populates it.
  bool sawDisabledEmptyCombo = false;
  const auto combos = page.findChildren<QComboBox *>();
  for (const auto *c : combos) {
    if (c->count() == 0 && !c->isEnabled()) {
      sawDisabledEmptyCombo = true;
    }
  }
  QVERIFY(sawDisabledEmptyCombo);
}

void TestDatAuditDownloadPage::loadRejectsUnparseableSystemIdWithoutGoingBusy() {
  DatAuditDownloadService service(inertProvenance());
  DatAuditDownloadPage page(&service);

  auto *systemEdit = page.findChild<QLineEdit *>();
  QPushButton *load = buttonWithText(page, QStringLiteral("Load"));
  QPushButton *cancel = buttonWithText(page, QStringLiteral("Cancel"));
  QVERIFY(systemEdit && load && cancel);

  const QString rejection = QStringLiteral("Enter a DAT-o-MATIC system id or daily-download URL.");

  // Neither free text nor a URL without ?s=<id> parses to a system id — the
  // guard must speak up and stay idle (no busy flip, no fetch dispatched).
  for (const QString &junk :
       {QStringLiteral("not-a-number"), QStringLiteral("https://example.org/daily?x=3"),
        QStringLiteral("-7"), QString()}) {
    systemEdit->setText(junk);
    load->click();
    QVERIFY2(hasLabelWithText(page, rejection), qPrintable(junk));
    QVERIFY2(load->isEnabled(), "rejected input must not flip the page busy");
    QVERIFY(!cancel->isEnabled());
  }
}

QTEST_MAIN(TestDatAuditDownloadPage)
#include "test_datauditdownloadpage.moc"
