#include "pathstatusglyph.h"

#include <QAction>
#include <QApplication>
#include <QStyle>

namespace PathStatusGlyph {

namespace {

constexpr auto kActionObjectName = "kartend.pathStatusGlyph";

QAction *findGlyphAction(QLineEdit *edit) {
  if (!edit) return nullptr;
  for (QAction *a : edit->actions()) {
    if (a->objectName() == QLatin1String(kActionObjectName)) {
      return a;
    }
  }
  return nullptr;
}

void apply(QLineEdit *edit, QAction *action, const PathChecker &checker) {
  if (!edit || !action || !checker) return;
  const PathUtils::PathStatus status = checker(edit->text());
  if (status == PathUtils::PathStatus::OK || status == PathUtils::PathStatus::Empty) {
    action->setVisible(false);
    action->setToolTip(QString());
  } else {
    action->setVisible(true);
    action->setToolTip(PathUtils::pathStatusDescription(status));
  }
}

} // namespace

void install(QLineEdit *edit, PathChecker checker) {
  if (!edit || !checker) return;

  QAction *action = findGlyphAction(edit);
  if (!action) {
    const QIcon warningIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
    action = edit->addAction(warningIcon, QLineEdit::TrailingPosition);
    action->setObjectName(QLatin1String(kActionObjectName));
    // Informational only — disable the trigger so accidental clicks don't
    // do anything.
    action->setCheckable(false);
  }

  // Re-bind the textChanged connection so a re-install with a new checker
  // doesn't leave the old closure firing on every edit.
  QObject::disconnect(edit, &QLineEdit::textChanged, action, nullptr);
  QObject::connect(edit, &QLineEdit::textChanged, action,
                   [edit, action, checker]() { apply(edit, action, checker); });

  apply(edit, action, checker);
}

} // namespace PathStatusGlyph
