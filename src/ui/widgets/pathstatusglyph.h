#ifndef KARTEND_UI_WIDGETS_PATHSTATUSGLYPH_H
#define KARTEND_UI_WIDGETS_PATHSTATUSGLYPH_H

// Kartend-liye: small trailing-action glyph on a QLineEdit that surfaces a
// PathUtils::PathStatus value as a warning icon + tooltip. Hidden when the
// path checks out (status == OK or Empty). Re-evaluates on every text
// change so a user pasting in a now-valid path sees the badge clear without
// closing the dialog.
//
// Purely informational — does not block save (matches the
// Kartend-qc1c contract: values are preserved as-is so the user can fix
// the underlying resource and reopen the dialog to confirm).

#include <functional>
#include <QLineEdit>
#include <QString>

#include "pathutils.h"

namespace PathStatusGlyph {

using PathChecker = std::function<PathUtils::PathStatus(const QString &)>;

/// Installs a trailing-action warning glyph on @p edit whose visibility and
/// tooltip track @p checker(edit->text()). The action is parented to
/// @p edit so its lifetime matches the line edit. Safe to call multiple
/// times — repeated installs reuse the existing action by objectName so the
/// line edit doesn't accumulate ghost actions, and the connected closure is
/// re-bound to the latest @p checker. Each loadConfig() in the dialog
/// re-evaluates state because install() runs again with the freshly-saved
/// text, so the badge clears when the user reinstalls a missing binary and
/// reopens the dialog.
void install(QLineEdit *edit, const PathChecker &checker);

} // namespace PathStatusGlyph

#endif // KARTEND_UI_WIDGETS_PATHSTATUSGLYPH_H
