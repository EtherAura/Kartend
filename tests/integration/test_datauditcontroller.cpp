#include "test_datauditcontroller.h"

#include "collection/collectionconfig.h"
#include "datauditcontroller.h"
#include "datauditdialog.h"

#include <QApplication>
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QWidget>

namespace {

struct DatAuditHarness {
  QWidget host;
  QList<CollectionConfig> collections;
  QStringList statusMessages;
  DatAuditController controller;

  explicit DatAuditHarness(const QString &libraryPath = QString()) {
    DatAuditControllerContext ctx;
    ctx.getParentWindow = [this]() -> QWidget * { return &host; };
    ctx.getCollections = [this]() { return &collections; };
    if (!libraryPath.isNull()) {
      ctx.getDatLibraryPath = [libraryPath]() { return libraryPath; };
    }
    ctx.showStatusMessage = [this](const QString &message) { statusMessages.append(message); };
    controller.setContext(ctx);
  }
};

} // namespace

void TestDatAuditController::openDialog_createsThenReusesOneDialog() {
  DatAuditHarness h;

  h.controller.openDialog();
  auto dialogs = h.host.findChildren<DatAuditDialog *>();
  QCOMPARE(dialogs.size(), 1);
  DatAuditDialog *dialog = dialogs.first();
  QVERIFY(dialog->isVisible());

  // The dialog survives a close (hidden, not destroyed) and the next open
  // re-raises the SAME instance instead of constructing a second one.
  dialog->hide();
  h.controller.openDialog();
  dialogs = h.host.findChildren<DatAuditDialog *>();
  QCOMPARE(dialogs.size(), 1);
  QCOMPARE(dialogs.first(), dialog);
  QVERIFY(dialog->isVisible());
}

void TestDatAuditController::openDialog_recreatesAfterExternalDestruction() {
  DatAuditHarness h;

  h.controller.openDialog();
  auto dialogs = h.host.findChildren<DatAuditDialog *>();
  QCOMPARE(dialogs.size(), 1);
  dialogs.first()->hide();

  // Premature destruction outside the controller (parent teardown order, a
  // future WA_DeleteOnClose): the QPointer cache must null itself so the
  // next open constructs a fresh dialog instead of reusing a dangling
  // pointer (use-after-free before the QPointer guard).
  delete dialogs.first();
  QVERIFY(h.host.findChildren<DatAuditDialog *>().isEmpty());

  h.controller.openDialog();
  dialogs = h.host.findChildren<DatAuditDialog *>();
  QCOMPARE(dialogs.size(), 1);
  QVERIFY(dialogs.first()->isVisible());
}

void TestDatAuditController::openForCollection_showsTheSameCachedDialog() {
  DatAuditHarness h;

  // A collection-scoped open must reuse the cached dialog too — it is the
  // same window, just seeded with the collection's audit profile.
  h.controller.openDialog();
  const auto first = h.host.findChildren<DatAuditDialog *>();
  QCOMPARE(first.size(), 1);

  h.controller.openForCollection(QStringLiteral("uuid-1"), QStringLiteral("Videos"),
                                 QStringLiteral("/media/videos"), {});
  const auto second = h.host.findChildren<DatAuditDialog *>();
  QCOMPARE(second.size(), 1);
  QCOMPARE(second.first(), first.first());
  QVERIFY(second.first()->isVisible());
}

void TestDatAuditController::startupLibraryScan_withoutLibraryPathIsInert() {
  // No DAT-library folder configured: the startup scan must neither spin
  // up the off-thread scan (nothing to walk) nor surface a status hint —
  // and it must not construct the dialog as a side effect.
  DatAuditHarness h(QStringLiteral("")); // hook wired, but empty path
  h.controller.startupLibraryScan();
  QVERIFY(h.statusMessages.isEmpty());
  QVERIFY(h.host.findChildren<DatAuditDialog *>().isEmpty());

  // Same when the host never wired the library hook at all.
  DatAuditHarness unwired;
  unwired.controller.startupLibraryScan();
  QVERIFY(unwired.statusMessages.isEmpty());
}

void TestDatAuditController::openWhileModalActive_adoptsAndRestoresParent() {
  DatAuditHarness h;

  // Baseline open: parented to the host window.
  h.controller.openDialog();
  DatAuditDialog *dialog = h.host.findChildren<DatAuditDialog *>().first();
  QCOMPARE(dialog->parentWidget(), &h.host);

  {
    // An application-modal dialog is up (the Settings dialog in production —
    // setModal + show registers it as activeModalWidget without needing a
    // blocking exec() here).
    QDialog modal;
    modal.setModal(true);
    modal.show();
    QCOMPARE(QApplication::activeModalWidget(), &modal);

    // Opening the audit now must transient-parent it to the modal so Qt
    // exempts it from the modal input block — previously it stayed under
    // the host and froze until Settings closed (Kartend-j6a00).
    h.controller.openDialog();
    QCOMPARE(dialog->parentWidget(), static_cast<QWidget *>(&modal));
    QVERIFY(dialog->isVisible());

    // Closing the modal (finished) hands the dialog back to the host BEFORE
    // the modal is destroyed — a QWidget parent owns its top-level children,
    // and the cached dialog must outlive the settings session.
    modal.close();
    QCOMPARE(dialog->parentWidget(), &h.host);
    QVERIFY(dialog->isVisible());
  } // modal destroyed here; the destroyed()-path restore is a no-op now

  QCOMPARE(h.host.findChildren<DatAuditDialog *>().size(), 1);
  QCOMPARE(h.host.findChildren<DatAuditDialog *>().first(), dialog);
}
