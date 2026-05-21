#ifndef API_ERRORPRESENTATION_H
#define API_ERRORPRESENTATION_H

#include "errorutils.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Neutral entry points for showing an error to the user without #including
/// the concrete ErrorDialog header from src/ui/. Lower-layer modules (e.g.
/// modules/data/settings/settingsdialogcontroller.cpp) call these instead of
/// reaching upward into src/ui/dialogs/errordialog.h, keeping the documented
/// layering DAG intact. The implementation lives in src/ui/dialogs/ and
/// forwards to ErrorDialog::showError / showCriticalError (Kartend-ncot).
namespace ErrorPresentation {

void showError(QWidget *parent, const ErrorUtils::ErrorContext &context);

[[nodiscard]] bool showCriticalError(QWidget *parent,
                                     const ErrorUtils::ErrorContext &context,
                                     bool allowContinue = true);

} // namespace ErrorPresentation

#endif // API_ERRORPRESENTATION_H
