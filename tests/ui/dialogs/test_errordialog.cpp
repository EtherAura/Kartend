// Headless ErrorDialog behavior: severity → window-title/icon mode mapping,
// the per-error-code user-guidance suffix appended to the message, the
// details/copy button gating on whether the context actually carries
// details, the Show/Hide Details toggle, the clipboard payload, and the
// showCriticalError() static's Continue/Quit button rewiring + exec result
// mapping (answered by a zero-interval modal driver under the offscreen
// QPA). setError()-driven cases never show the dialog.

#include "errordialog.h"
#include "errorutils.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QTextEdit>
#include <QTimer>

namespace {

/// Runs @p action on the next active modal dialog (repeating zero-interval
/// timer, so it survives the window between exec() entry and modal
/// activation). Same driver shape as test_launcherchooserdialog.cpp.
class ModalDriver : public QObject {
public:
  explicit ModalDriver(std::function<void(QDialog *)> action) : m_action(std::move(action)) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dlg) {
        return;
      }
      triggered = true;
      m_timer.stop();
      m_action(dlg);
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  std::function<void(QDialog *)> m_action;
  QTimer m_timer;
};

QPushButton *buttonWithText(const QWidget &root, const QString &text) {
  const auto buttons = root.findChildren<QPushButton *>();
  for (auto *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

/// The dialog's message label — the only QLabel carrying text (the sibling
/// icon label holds a pixmap).
QLabel *messageLabel(const QWidget &root) {
  const auto labels = root.findChildren<QLabel *>();
  for (auto *l : labels) {
    if (!l->text().isEmpty()) {
      return l;
    }
  }
  return nullptr;
}

ErrorUtils::ErrorContext makeContext(ErrorUtils::Severity severity) {
  ErrorUtils::ErrorContext ctx;
  ctx.code = ErrorUtils::ErrorCode::ConfigLoadFailed;
  ctx.severity = severity;
  ctx.message = QStringLiteral("Something happened");
  return ctx;
}

} // namespace

class TestErrorDialog : public QObject {
  Q_OBJECT

private slots:
  void severityDrivesWindowTitle();
  void suggestionAppendedForKnownErrorCodes();
  void detailButtonsGateOnContextPayload();
  void toggleDetailsShowsPaneAndRelabelsButton();
  void technicalDetailsRenderCodeSourceAndDetails();
  void copyPutsFullContextOnClipboard();
  void criticalErrorContinueAcceptsQuitRejects();
};

void TestErrorDialog::severityDrivesWindowTitle() {
  ErrorDialog dlg;
  dlg.setError(makeContext(ErrorUtils::Severity::Info));
  QCOMPARE(dlg.windowTitle(), QStringLiteral("Information"));
  dlg.setError(makeContext(ErrorUtils::Severity::Warning));
  QCOMPARE(dlg.windowTitle(), QStringLiteral("Warning"));
  dlg.setError(makeContext(ErrorUtils::Severity::Error));
  QCOMPARE(dlg.windowTitle(), QStringLiteral("Error"));
  dlg.setError(makeContext(ErrorUtils::Severity::Critical));
  QCOMPARE(dlg.windowTitle(), QStringLiteral("Critical Error"));
}

void TestErrorDialog::suggestionAppendedForKnownErrorCodes() {
  ErrorDialog dlg;
  ErrorUtils::ErrorContext ctx;
  ctx.code = ErrorUtils::ErrorCode::MediaDirectoryNotFound;
  ctx.message = QStringLiteral("Folder is gone");
  dlg.setError(ctx);

  QLabel *label = messageLabel(dlg);
  QVERIFY(label);
  QVERIFY(label->text().startsWith(QStringLiteral("Folder is gone")));
  QVERIFY2(label->text().contains(QStringLiteral("verify the media directory")),
           "MediaDirectoryNotFound must append its settings-path hint");

  // A code without a curated suggestion shows the raw message only.
  ErrorUtils::ErrorContext plain;
  plain.code = ErrorUtils::ErrorCode::DatabaseQueryFailed;
  plain.message = QStringLiteral("Query failed");
  dlg.setError(plain);
  QCOMPARE(messageLabel(dlg)->text(), QStringLiteral("Query failed"));
}

void TestErrorDialog::detailButtonsGateOnContextPayload() {
  ErrorDialog dlg;
  QPushButton *details = buttonWithText(dlg, QStringLiteral("Show Details"));
  QPushButton *copy = buttonWithText(dlg, QStringLiteral("Copy"));
  QVERIFY(details && copy);

  // No details and no source → nothing to expand or copy; both hidden.
  ErrorUtils::ErrorContext bare;
  bare.code = ErrorUtils::ErrorCode::InvalidFilePath;
  bare.message = QStringLiteral("Bad path");
  dlg.setError(bare);
  QVERIFY(details->isHidden());
  QVERIFY(copy->isHidden());

  // A source alone is enough to warrant the technical view.
  bare.source = QStringLiteral("PathUtils::validate");
  dlg.setError(bare);
  QVERIFY(!details->isHidden());
  QVERIFY(!copy->isHidden());
}

void TestErrorDialog::toggleDetailsShowsPaneAndRelabelsButton() {
  ErrorDialog dlg;
  ErrorUtils::ErrorContext ctx = makeContext(ErrorUtils::Severity::Error);
  ctx.details = QStringLiteral("stack-ish details");
  dlg.setError(ctx);

  auto *pane = dlg.findChild<QTextEdit *>();
  QPushButton *details = buttonWithText(dlg, QStringLiteral("Show Details"));
  QVERIFY(pane && details);
  QVERIFY(pane->isHidden()); // collapsed by default

  details->click();
  QVERIFY(!pane->isHidden());
  QCOMPARE(details->text(), QStringLiteral("Hide Details"));

  details->click();
  QVERIFY(pane->isHidden());
  QCOMPARE(details->text(), QStringLiteral("Show Details"));
}

void TestErrorDialog::technicalDetailsRenderCodeSourceAndDetails() {
  ErrorDialog dlg;
  ErrorUtils::ErrorContext ctx;
  ctx.code = ErrorUtils::ErrorCode::ConfigSaveFailed;
  ctx.message = QStringLiteral("Could not save");
  ctx.source = QStringLiteral("SettingsManager::saveGeneralSettings");
  ctx.details = QStringLiteral("EROFS: read-only file system");
  dlg.setError(ctx);

  const QString text = dlg.findChild<QTextEdit *>()->toPlainText();
  QVERIFY(text.contains(QStringLiteral("Error Code: %1 (%2)")
                            .arg(static_cast<int>(ctx.code))
                            .arg(ErrorUtils::errorCodeToString(ctx.code))));
  QVERIFY(text.contains(QStringLiteral("Source: SettingsManager::saveGeneralSettings")));
  QVERIFY(text.contains(QStringLiteral("EROFS: read-only file system")));
}

void TestErrorDialog::copyPutsFullContextOnClipboard() {
  ErrorDialog dlg;
  ErrorUtils::ErrorContext ctx;
  ctx.code = ErrorUtils::ErrorCode::DatabaseConnectionFailed;
  ctx.message = QStringLiteral("DB unreachable");
  ctx.source = QStringLiteral("DatabaseManager::initDatabase");
  ctx.details = QStringLiteral("unable to open database file");
  dlg.setError(ctx);

  QPushButton *copy = buttonWithText(dlg, QStringLiteral("Copy"));
  QVERIFY(copy);
  copy->click();

  const QString clip = QApplication::clipboard()->text();
  QVERIFY(clip.contains(QStringLiteral("DB unreachable")));
  QVERIFY(clip.contains(QStringLiteral("Source: DatabaseManager::initDatabase")));
  QVERIFY(clip.contains(QStringLiteral("Details: unable to open database file")));
  QVERIFY(clip.contains(QStringLiteral("Error Code: %1").arg(static_cast<int>(ctx.code))));
}

void TestErrorDialog::criticalErrorContinueAcceptsQuitRejects() {
  const ErrorUtils::ErrorContext ctx = makeContext(ErrorUtils::Severity::Critical);

  {
    // allowContinue: OK is swapped out for visible Continue (default) + Quit.
    ModalDriver driver([](QDialog *dlg) {
      QPushButton *ok = buttonWithText(*dlg, QStringLiteral("OK"));
      QPushButton *cont = buttonWithText(*dlg, QStringLiteral("Continue"));
      QPushButton *quit = buttonWithText(*dlg, QStringLiteral("Quit"));
      QVERIFY(ok && cont && quit);
      QVERIFY(ok->isHidden());
      QVERIFY(!cont->isHidden());
      QVERIFY(!quit->isHidden());
      QVERIFY(cont->isDefault());
      cont->click();
    });
    QVERIFY(ErrorDialog::showCriticalError(nullptr, ctx, /*allowContinue=*/true));
    QVERIFY(driver.triggered);
  }

  {
    // Quit rejects → false. Without allowContinue the Continue button stays
    // hidden and Quit becomes the default.
    ModalDriver driver([](QDialog *dlg) {
      QPushButton *cont = buttonWithText(*dlg, QStringLiteral("Continue"));
      QPushButton *quit = buttonWithText(*dlg, QStringLiteral("Quit"));
      QVERIFY(cont && quit);
      QVERIFY(cont->isHidden());
      QVERIFY(quit->isDefault());
      quit->click();
    });
    QVERIFY(!ErrorDialog::showCriticalError(nullptr, ctx, /*allowContinue=*/false));
    QVERIFY(driver.triggered);
  }
}

QTEST_MAIN(TestErrorDialog)
#include "test_errordialog.moc"
