#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QList>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <utility>

namespace KartendTest {

/// Answers a queue of modal QMessageBoxes in order, capturing each box's text.
/// A zero-interval timer fires only inside a box's own nested exec() loop, so
/// when fewer boxes appear than expected the leftover queue simply reports how
/// far the flow got (assert on `texts`/counts — never a wedged event loop).
///
/// Hardened for host-less dialog suites (hoisted from the SettingsDialog browse
/// suite so any suite can inherit the safe behavior):
///   * A dynamic property marks answered boxes instead of comparing pointers —
///     consecutive boxes routinely reuse the same heap address, so a pointer
///     guard mistakes box #2 for box #1 and never answers it, spinning the exec
///     loop forever (this wedged a full ctest run).
///   * An unexpected box (queue empty) or a box missing the requested button is
///     dismissed via its escape/default button and tallied, never left to hang.
///   * A non-QMessageBox modal — e.g. the ErrorDialog a standalone dialog
///     (constructed with no settings host) raises from a general-settings save
///     inside handleSaveCollection — is rejected WITHOUT consuming the button
///     queue and recorded in `otherDialogTitles`, so slots can assert how many
///     appeared. Left unanswered its exec() loop never exits and this timer
///     would drive it at 100% CPU (the suite-wedging spin).
class ModalAnswerQueue : public QObject {
public:
  explicit ModalAnswerQueue(QList<QMessageBox::StandardButton> buttons)
      : m_buttons(std::move(buttons)) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      QWidget *modal = QApplication::activeModalWidget();
      if (!modal || modal->property("modalQueueAnswered").toBool()) {
        return;
      }
      auto *box = qobject_cast<QMessageBox *>(modal);
      if (!box) {
        auto *dialog = qobject_cast<QDialog *>(modal);
        if (!dialog) {
          return;
        }
        dialog->setProperty("modalQueueAnswered", true);
        otherDialogTitles.append(dialog->windowTitle());
        dialog->reject();
        return;
      }
      box->setProperty("modalQueueAnswered", true);
      texts.append(box->text());
      QAbstractButton *toClick = nullptr;
      if (!m_buttons.isEmpty()) {
        toClick = box->button(m_buttons.takeFirst());
      } else {
        ++unexpectedBoxes;
      }
      if (!toClick) toClick = box->escapeButton();
      if (!toClick) toClick = box->defaultButton();
      if (!toClick && !box->buttons().isEmpty()) toClick = box->buttons().first();
      if (toClick) toClick->click();
    });
    m_timer.start();
  }

  QStringList texts;
  QStringList otherDialogTitles;
  int unexpectedBoxes = 0;

private:
  QList<QMessageBox::StandardButton> m_buttons;
  QTimer m_timer;
};

} // namespace KartendTest
