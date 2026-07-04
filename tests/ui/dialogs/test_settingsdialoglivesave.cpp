// SettingsDialog live-save debounce: the base-color / fonts / splash panels
// apply every edit to the running app immediately, but the GeneralSettings
// INI write is coalesced through a single-shot timer — a burst of edit
// signals (color-picker drag, spinbox scrub) must produce ONE disk write,
// not one per signal. Cancel must revert without letting a pending debounce
// fire afterwards, and Save/OK's full-save must supersede the pending flush
// (no duplicate write). Driven through a real SettingsDialog with a minimal
// QWidget+IMainWindow host and a counting ISettingsManager injected via
// ApplicationContext; the unsaved-changes prompts are answered by a
// zero-interval modal driver. The dialog itself is never shown.

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "imainwindow.h"
#include "settingsdialog.h"

#include "../../integration/mocks/mocksettingsmanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>

namespace {

/// Minimal IMainWindow host: generalSettings() is the live struct the
/// dialog's live-save handlers mirror into; everything else is inert.
class FakeMainWindow : public QWidget, public IMainWindow {
public:
  QList<CollectionConfig> collectionsList;
  GeneralSettings settings;

  [[nodiscard]] const QList<CollectionConfig> &collections() const override {
    return collectionsList;
  }
  void updateWindowTitleForCollection(int) override {}
  void openScraperDialog(int, const QString &) override {}
  void openEntityScraperDialog(int) override {}
  void openDatAuditForCollection(const CollectionConfig &) override {}
  [[nodiscard]] DatAuditStatus datAuditStatusForCollection(const QString &) override { return {}; }
  [[nodiscard]] GeneralSettings &generalSettings() override { return settings; }
  [[nodiscard]] const GeneralSettings &generalSettings() const override { return settings; }
  [[nodiscard]] ApplicationManager *applicationManager() const override { return nullptr; }
  void applyGlobalUiFontFromSettings() override {}
  void applyMarqueeSettings() override {}
  void applyToolbarCustomization() override {}
  void applyPixmapCacheBudget(int) override {}
};

/// Counts saveGeneralSettings disk writes and captures the last payload.
class CountingSettingsManager : public KartendTest::MockSettingsManager {
public:
  int saveCount = 0;
  GeneralSettings lastSaved;
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override {
    ++saveCount;
    lastSaved = settings;
    return ErrorUtils::Result<void>::success();
  }
};

/// Answers the next active modal (the unsaved-changes QMessageBox) with the
/// given standard button. Zero-interval repeating timer, same shape as
/// test_launcherchooserdialog.cpp.
class ModalDriver : public QObject {
public:
  explicit ModalDriver(QMessageBox::StandardButton button) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this, button] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!box) {
        return;
      }
      triggered = true;
      m_timer.stop();
      box->button(button)->click();
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  QTimer m_timer;
};

QList<CollectionConfig> oneCollection() {
  CollectionConfig c;
  c.name = QStringLiteral("First");
  return {c};
}

struct Harness {
  FakeMainWindow host;
  CountingSettingsManager sm;
  ApplicationContext ctx;
  SettingsDialog *dialog = nullptr; // parented to host

  Harness() {
    ctx.managers.settingsManager = &sm;
    dialog = new SettingsDialog(&host, oneCollection(), 0, &ctx);
  }
};

} // namespace

class TestSettingsDialogLiveSave : public QObject {
  Q_OBJECT

private slots:
  void editBurstAppliesInMemoryImmediatelyButWritesDiskOnce();
  void rejectRestoresBaselineAndCancelsPendingWrite();
  void acceptViaSaveSupersedesPendingDebounce();
};

void TestSettingsDialogLiveSave::editBurstAppliesInMemoryImmediatelyButWritesDiskOnce() {
  Harness h;
  auto *toggle = h.dialog->findChild<QCheckBox *>(QStringLiteral("bootSplashCheckBox"));
  QVERIFY(toggle);
  const bool original = toggle->isChecked();
  QCOMPARE(h.sm.saveCount, 0);

  // A burst of three live edits: each one mirrors into the host's live
  // struct immediately (instant-feedback UX preserved)...
  toggle->setChecked(!original);
  QCOMPARE(h.host.settings.splash.bootSplashEnabled, !original);
  QCOMPARE(h.sm.saveCount, 0); // ...but no disk write per edit
  toggle->setChecked(original);
  toggle->setChecked(!original);
  QCOMPARE(h.host.settings.splash.bootSplashEnabled, !original);
  QCOMPARE(h.sm.saveCount, 0);

  // The debounce settles into exactly ONE write carrying the final state.
  QTRY_COMPARE_WITH_TIMEOUT(h.sm.saveCount, 1, 3000);
  QCOMPARE(h.sm.lastSaved.splash.bootSplashEnabled, !original);

  // No trailing writes once settled.
  QTest::qWait(700);
  QCOMPARE(h.sm.saveCount, 1);
}

void TestSettingsDialogLiveSave::rejectRestoresBaselineAndCancelsPendingWrite() {
  Harness h;
  auto *toggle = h.dialog->findChild<QCheckBox *>(QStringLiteral("bootSplashCheckBox"));
  QVERIFY(toggle);
  const bool original = toggle->isChecked();

  toggle->setChecked(!original);
  QCOMPARE(h.sm.saveCount, 0); // debounce pending

  // Cancel: the live edit made the dialog dirty, so the unsaved-changes
  // prompt appears — Discard it. restoreLiveAppliedSettings then re-persists
  // the BASELINE exactly once; the pending debounced write of the live value
  // must never fire.
  ModalDriver driver(QMessageBox::Discard);
  h.dialog->reject();
  QVERIFY(driver.triggered);

  QCOMPARE(h.sm.saveCount, 1);
  QCOMPARE(h.sm.lastSaved.splash.bootSplashEnabled, original);
  QCOMPARE(h.host.settings.splash.bootSplashEnabled, original);

  // Wait out the debounce window: the cancelled timer stays cancelled.
  QTest::qWait(700);
  QCOMPARE(h.sm.saveCount, 1);
  QCOMPARE(h.sm.lastSaved.splash.bootSplashEnabled, original);
}

void TestSettingsDialogLiveSave::acceptViaSaveSupersedesPendingDebounce() {
  Harness h;
  auto *toggle = h.dialog->findChild<QCheckBox *>(QStringLiteral("bootSplashCheckBox"));
  QVERIFY(toggle);
  const bool original = toggle->isChecked();

  toggle->setChecked(!original);
  QCOMPARE(h.sm.saveCount, 0); // debounce pending

  // OK with unsaved changes → prompt → Save → the full-save path persists
  // the live value (and supersedes the pending flush).
  ModalDriver driver(QMessageBox::Save);
  h.dialog->accept();
  QVERIFY(driver.triggered);
  QCOMPARE(h.dialog->result(), static_cast<int>(QDialog::Accepted));

  QVERIFY(h.sm.saveCount >= 1);
  const int settled = h.sm.saveCount;
  QCOMPARE(h.sm.lastSaved.splash.bootSplashEnabled, !original);

  // The superseded debounce must not add a redundant write after the fact.
  QTest::qWait(700);
  QCOMPARE(h.sm.saveCount, settled);
}

QTEST_MAIN(TestSettingsDialogLiveSave)
#include "test_settingsdialoglivesave.moc"
