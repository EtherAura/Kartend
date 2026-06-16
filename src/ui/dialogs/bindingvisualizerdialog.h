#ifndef BINDINGVISUALIZERDIALOG_H
#define BINDINGVISUALIZERDIALOG_H

#include <QDialog>

class GamepadManager;
struct GeneralSettings;

QT_BEGIN_NAMESPACE
class QLabel;
class QTreeWidget;
class QKeyEvent;
QT_END_NAMESPACE

/// Read-only viewer for the current keyboard / gamepad bindings.
/// Two-section table grouped by action area (Navigation, Selection,
/// Search, etc.) with the live key + gamepad bindings inline. A status
/// strip at the top tells the user to press a key or button to
/// identify the mapped action; the matching row highlights in response.
///
/// While open, the dialog calls `gamepadManager->beginBindingCapture()`
/// so gamepad input arrives via `bindingCaptureButtonPressed` instead
/// of triggering the underlying action. Capture is released on close
/// regardless of how the user dismisses the dialog.
class BindingVisualizerDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(BindingVisualizerDialog)
public:
  explicit BindingVisualizerDialog(const GeneralSettings *settings, GamepadManager *gamepadManager,
                                   QWidget *parent = nullptr);
  ~BindingVisualizerDialog() override;

protected:
  void keyPressEvent(QKeyEvent *event) override;
  // Re-tint the currently-highlighted row (Highlight/HighlightedText are baked
  // as concrete brushes into the item) when the system palette changes.
  void changeEvent(QEvent *event) override;

private:
  void setupUi();
  void buildBindingRows();
  void highlightKey(int key);
  void highlightGamepadButton(const QString &buttonName);
  void clearHighlight();
  // Re-apply the current Highlight/HighlightedText brushes to whichever row is
  // currently highlighted (no stored key — detected by a non-empty background).
  void retintHighlightedRow();

  const GeneralSettings *m_settings = nullptr;
  GamepadManager *m_gamepadManager = nullptr;
  QTreeWidget *m_tree = nullptr;
  QLabel *m_statusLabel = nullptr;
};

#endif // BINDINGVISUALIZERDIALOG_H
