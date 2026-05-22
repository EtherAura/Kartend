#ifndef API_ERRORPRESENTATION_H
#define API_ERRORPRESENTATION_H

#include "errorutils.h"

#include <functional>

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

[[nodiscard]] bool showCriticalError(QWidget *parent, const ErrorUtils::ErrorContext &context,
                                     bool allowContinue = true);

/// Test-only override hooks. When set, showError / showCriticalError invoke
/// the supplied function instead of opening an ErrorDialog modal — so
/// integration tests that exercise the error-signal slot path
/// (NavigationManager::mediaLibraryErrorRaised → MainWindow lambda → ...)
/// don't spin a nested QDialog::exec() event loop that the test then has to
/// race-dismiss. Pass an empty std::function to restore the default (open
/// the modal). Production code never touches these; integration tests run
/// single-threaded on the GUI thread, so no mutex is needed (Kartend-hlnl).
using ShowErrorFn = std::function<void(QWidget *, const ErrorUtils::ErrorContext &)>;
using ShowCriticalErrorFn =
    std::function<bool(QWidget *, const ErrorUtils::ErrorContext &, bool /*allowContinue*/)>;
void setShowErrorOverride(ShowErrorFn fn);
void setShowCriticalErrorOverride(ShowCriticalErrorFn fn);

} // namespace ErrorPresentation

#endif // API_ERRORPRESENTATION_H
