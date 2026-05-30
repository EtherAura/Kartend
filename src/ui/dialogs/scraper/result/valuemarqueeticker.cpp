#include "valuemarqueeticker.h"

#include "scraperesultdialog.h"

#include <QFontMetrics>
#include <QGroupBox>
#include <QLineEdit>

ValueMarqueeTicker::ValueMarqueeTicker(ScrapeResultDialog *dlg) : QObject(dlg), m_dlg(dlg) {}

void ValueMarqueeTicker::start() {
  if (!m_inited) {
    m_timer.setInterval(150);
    connect(&m_timer, &QTimer::timeout, this, &ValueMarqueeTicker::tick);
    m_inited = true;
  }
  m_pauseTicks.clear();
  m_timer.start();
}

void ValueMarqueeTicker::stop() {
  m_timer.stop();
  m_pauseTicks.clear();
}

void ValueMarqueeTicker::pause() {
  m_timer.stop();
}

void ValueMarqueeTicker::resume() {
  if (m_inited && !m_timer.isActive()) {
    m_timer.start();
  }
}

void ValueMarqueeTicker::resetCells() {
  m_pauseTicks.clear();
}

void ValueMarqueeTicker::tick() {
  if (!m_dlg->m_liveMetadataGroup) return;
  // Defensive check — the dialog's hideEvent calls pause() which stops
  // m_timer, but if a late-fired tick lands before that the visibility
  // gate keeps us from doing the findChildren-tree-walk on an invisible
  // dialog.
  if (!m_dlg->isVisible()) return;
  const auto edits = m_dlg->m_liveMetadataGroup->findChildren<QLineEdit *>();
  for (auto *edit : edits) {
    if (!edit) continue;
    const QString text = edit->text();
    if (text.isEmpty()) continue;
    QFontMetrics fm(edit->font());
    const int textW = fm.horizontalAdvance(text);
    // -12 px accounts for the QLineEdit frame + horizontal padding;
    // close enough that "remainder fits" is true once the actual
    // tail of the string is visible.
    const int viewW = edit->width() - 12;
    if (textW <= viewW) {
      // No overflow; ensure we're at the start (in case the cell
      // previously held a longer value mid-marquee).
      if (edit->cursorPosition() != 0) edit->setCursorPosition(0);
      m_pauseTicks.remove(edit);
      continue;
    }
    // Pause-mode: counting down at the rightmost-visible position.
    // When the counter hits zero, snap back to position 0 (no
    // reverse animation) so the marquee restarts L→R.
    if (m_pauseTicks.contains(edit)) {
      int &pause = m_pauseTicks[edit];
      if (--pause <= 0) {
        edit->setCursorPosition(0);
        m_pauseTicks.remove(edit);
      }
      continue;
    }
    // Advancing phase: increment cursor (which scrolls the visible
    // region one character to the right). When the remainder of the
    // text fits in the visible viewport, hold for ~1.5 s before the
    // L→R wrap.
    const int curPos = edit->cursorPosition();
    const int remW = fm.horizontalAdvance(text.mid(curPos));
    if (remW <= viewW) {
      m_pauseTicks.insert(edit, 10); // ~10 × 150 ms = 1.5 s hold
    } else {
      edit->setCursorPosition(qMin(curPos + 1, text.length()));
    }
  }
}
