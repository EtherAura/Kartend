// Kartend-0eeuk: GamepadCaptureController button-capture state machine. The
// controller is driven against a REAL GamepadManager (no SDL hardware
// needed: the binding-capture handshake is plain signal/slot, so the test
// emits bindingCaptureButtonPressed directly the way the backend poll would)
// resolved through a stub IInteractionManager on a stack ApplicationContext.
// Pins the arm/disarm UI sync (button labels, sibling lock-out, placeholder
// text), the captured-button -> target line-edit routing, the same-target
// toggle-off rule, retargeting mid-capture, the null-widget no-op contract,
// and the missing-backend QMessageBox path (answered by a zero-interval
// timer). The host SettingsDialog is null throughout — the controller only
// uses it as a QObject/message-box parent. Driven headlessly.

#include "gamepadcapturecontroller.h"

#include "applicationcontext.h"
#include "gamepadmanager.h"
#include "iinteractionmanager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QWidget>

namespace {

/// Minimal IInteractionManager stub: only gamepadManager() matters here.
class StubInteractionManager : public IInteractionManager {
public:
  explicit StubInteractionManager(GamepadManager *gamepad) : m_gamepad(gamepad) {}

  [[nodiscard]] int currentSelectedIndex() const override { return -1; }
  void clearSelectionAndFocus() override {}
  void initializeSearchModeForCurrentCollection() override {}
  void beginSelectionRestore(int /*targetIndex*/) override {}
  void cancelPendingSelectionRestore() override {}
  void resetSelectionRestoreState() override {}
  void stopRepeat(bool /*suppressRecentering*/) override {}
  void stopScrollAnimations() override {}
  [[nodiscard]] bool isWheelScrolling() const override { return false; }
  void setNavigationInProgress(bool /*inProgress*/) override {}
  [[nodiscard]] QString selectedFilePath() const override { return {}; }
  void saveCurrentSelection() override {}
  [[nodiscard]] AttractManager *attractManager() const override { return nullptr; }
  [[nodiscard]] GamepadManager *gamepadManager() const override { return m_gamepad; }

private:
  GamepadManager *m_gamepad = nullptr;
};

/// Owns the widget bag the controller drives, parented to one host widget.
struct WidgetBag {
  QWidget host;
  GamepadCaptureController::Bindings bindings;

  WidgetBag() {
    bindings.confirmEdit = new QLineEdit(&host);
    bindings.backEdit = new QLineEdit(&host);
    bindings.toggleSidebarEdit = new QLineEdit(&host);
    bindings.detectConfirmButton = new QPushButton(&host);
    bindings.detectBackButton = new QPushButton(&host);
    bindings.detectToggleSidebarButton = new QPushButton(&host);
    bindings.useDpadCheckBox = new QCheckBox(&host);
    bindings.useLeftStickCheckBox = new QCheckBox(&host);
  }
};

/// Clicks Ok on the next modal QMessageBox (the backend-unavailable info
/// box), capturing whether it appeared.
class InfoBoxAnswerer : public QObject {
public:
  InfoBoxAnswerer() {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!box) {
        return;
      }
      triggered = true;
      m_timer.stop();
      if (QAbstractButton *b = box->button(QMessageBox::Ok)) {
        b->click();
      }
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  QTimer m_timer;
};

} // namespace

class TestGamepadCaptureController : public QObject {
  Q_OBJECT

private slots:
  void idleStateAfterSetWidgets();
  void startArmsTargetAndLocksSiblings();
  void capturedButtonLandsInTargetEditAndDisarms();
  void sameTargetStartTogglesOff();
  void startingAnotherTargetRetargets();
  void nullWidgetsAreTolerated();
  void missingBackendShowsInfoBoxAndStaysIdle();
};

void TestGamepadCaptureController::idleStateAfterSetWidgets() {
  GamepadManager gamepad;
  StubInteractionManager interaction(&gamepad);
  ApplicationContext ctx;
  ctx.managers.interactionManager = &interaction;

  GamepadCaptureController controller(nullptr, &ctx);
  WidgetBag bag;
  controller.setWidgets(bag.bindings);

  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
  QCOMPARE(bag.bindings.detectConfirmButton->text(), QStringLiteral("Detect..."));
  QCOMPARE(bag.bindings.detectBackButton->text(), QStringLiteral("Detect..."));
  QCOMPARE(bag.bindings.detectToggleSidebarButton->text(), QStringLiteral("Detect..."));
  QVERIFY(bag.bindings.detectConfirmButton->isEnabled());
  QVERIFY(bag.bindings.detectBackButton->isEnabled());
  QVERIFY(bag.bindings.useDpadCheckBox->isEnabled());
  QVERIFY(bag.bindings.useLeftStickCheckBox->isEnabled());
  QVERIFY(bag.bindings.confirmEdit->placeholderText().isEmpty());
}

void TestGamepadCaptureController::startArmsTargetAndLocksSiblings() {
  GamepadManager gamepad;
  StubInteractionManager interaction(&gamepad);
  ApplicationContext ctx;
  ctx.managers.interactionManager = &interaction;
  GamepadCaptureController controller(nullptr, &ctx);
  WidgetBag bag;
  controller.setWidgets(bag.bindings);

  controller.start(GamepadCaptureController::Target::Back);
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::Back);
  QCOMPARE(bag.bindings.detectBackButton->text(), QStringLiteral("Press button..."));
  // The non-active detect buttons lock so a second capture can't be armed,
  // and the D-Pad / left-stick checkboxes freeze mid-capture.
  QVERIFY(!bag.bindings.detectConfirmButton->isEnabled());
  QVERIFY(!bag.bindings.detectToggleSidebarButton->isEnabled());
  QVERIFY(bag.bindings.detectBackButton->isEnabled());
  QVERIFY(!bag.bindings.useDpadCheckBox->isEnabled());
  QVERIFY(!bag.bindings.useLeftStickCheckBox->isEnabled());
  QCOMPARE(bag.bindings.backEdit->placeholderText(), QStringLiteral("Press any button"));
  QVERIFY(bag.bindings.confirmEdit->placeholderText().isEmpty());
}

void TestGamepadCaptureController::capturedButtonLandsInTargetEditAndDisarms() {
  GamepadManager gamepad;
  StubInteractionManager interaction(&gamepad);
  ApplicationContext ctx;
  ctx.managers.interactionManager = &interaction;
  GamepadCaptureController controller(nullptr, &ctx);
  WidgetBag bag;
  controller.setWidgets(bag.bindings);

  controller.start(GamepadCaptureController::Target::Confirm);
  emit gamepad.bindingCaptureButtonPressed(QStringLiteral("button_a"));

  QCOMPARE(bag.bindings.confirmEdit->text(), QStringLiteral("button_a"));
  QVERIFY(bag.bindings.backEdit->text().isEmpty());
  // The capture is one-shot: the press disarms and the UI returns to idle.
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
  QCOMPARE(bag.bindings.detectConfirmButton->text(), QStringLiteral("Detect..."));
  QVERIFY(bag.bindings.detectBackButton->isEnabled());
  QVERIFY(bag.bindings.useDpadCheckBox->isEnabled());
  QVERIFY(bag.bindings.confirmEdit->placeholderText().isEmpty());

  // A stray press after the disarm must not re-route into any edit.
  emit gamepad.bindingCaptureButtonPressed(QStringLiteral("button_b"));
  QCOMPARE(bag.bindings.confirmEdit->text(), QStringLiteral("button_a"));
  QVERIFY(bag.bindings.backEdit->text().isEmpty());
}

void TestGamepadCaptureController::sameTargetStartTogglesOff() {
  GamepadManager gamepad;
  StubInteractionManager interaction(&gamepad);
  ApplicationContext ctx;
  ctx.managers.interactionManager = &interaction;
  GamepadCaptureController controller(nullptr, &ctx);
  WidgetBag bag;
  controller.setWidgets(bag.bindings);

  controller.start(GamepadCaptureController::Target::Confirm);
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::Confirm);
  // Hitting Detect again on the same target cancels instead of re-arming.
  controller.start(GamepadCaptureController::Target::Confirm);
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
  QCOMPARE(bag.bindings.detectConfirmButton->text(), QStringLiteral("Detect..."));
  QVERIFY(bag.bindings.useDpadCheckBox->isEnabled());

  // A press after the cancel goes nowhere.
  emit gamepad.bindingCaptureButtonPressed(QStringLiteral("button_x"));
  QVERIFY(bag.bindings.confirmEdit->text().isEmpty());
}

void TestGamepadCaptureController::startingAnotherTargetRetargets() {
  GamepadManager gamepad;
  StubInteractionManager interaction(&gamepad);
  ApplicationContext ctx;
  ctx.managers.interactionManager = &interaction;
  GamepadCaptureController controller(nullptr, &ctx);
  WidgetBag bag;
  controller.setWidgets(bag.bindings);

  controller.start(GamepadCaptureController::Target::Confirm);
  controller.start(GamepadCaptureController::Target::ToggleSidebar);
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::ToggleSidebar);

  emit gamepad.bindingCaptureButtonPressed(QStringLiteral("button_y"));
  // Only the new target receives the button — the abandoned one stays
  // untouched (a double-fill here would corrupt two bindings at once).
  QCOMPARE(bag.bindings.toggleSidebarEdit->text(), QStringLiteral("button_y"));
  QVERIFY(bag.bindings.confirmEdit->text().isEmpty());
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
}

void TestGamepadCaptureController::nullWidgetsAreTolerated() {
  GamepadManager gamepad;
  StubInteractionManager interaction(&gamepad);
  ApplicationContext ctx;
  ctx.managers.interactionManager = &interaction;
  GamepadCaptureController controller(nullptr, &ctx);
  // No setWidgets call — every Bindings member is null. The documented
  // contract is a no-op, not a crash.
  controller.start(GamepadCaptureController::Target::Confirm);
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::Confirm);
  emit gamepad.bindingCaptureButtonPressed(QStringLiteral("button_a"));
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
  controller.stop(); // idempotent
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
}

void TestGamepadCaptureController::missingBackendShowsInfoBoxAndStaysIdle() {
  // No interaction manager on the ctx → no gamepad → start() must refuse
  // with the info box rather than arming a capture that can never finish.
  ApplicationContext ctx;
  GamepadCaptureController controller(nullptr, &ctx);
  WidgetBag bag;
  controller.setWidgets(bag.bindings);

  InfoBoxAnswerer answer;
  controller.start(GamepadCaptureController::Target::Confirm);
  QVERIFY(answer.triggered);
  QCOMPARE(controller.activeTarget(), GamepadCaptureController::Target::None);
  QCOMPARE(bag.bindings.detectConfirmButton->text(), QStringLiteral("Detect..."));
  QVERIFY(bag.bindings.useDpadCheckBox->isEnabled());
}

QTEST_MAIN(TestGamepadCaptureController)
#include "test_gamepadcapturecontroller.moc"
