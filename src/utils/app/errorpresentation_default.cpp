// Kartend-hx6l: low-layer implementation of the ErrorPresentation
// namespace declared in src/api/errorpresentation.h. Lives in
// kartend_utils so the data-layer call sites in
// settingsdialogcontroller.cpp (Kartend-ncot) can resolve the symbol
// without dragging a kartend_data → kartend_ui link edge into every
// test executable that links kartend_data.
//
// The default behaviour is a qCWarning() log line — no modal dialog,
// no UI dependency. kartend_ui's MainWindow startup is expected to
// call setShowErrorOverride() / setShowCriticalErrorOverride() with the
// real ErrorDialog-backed impl so production users see the same modal
// they always have. Tests that don't override see the qWarning trail
// only, which is exactly the right behaviour for headless integration
// runs (no QDialog::exec() spinning a nested event loop the test then
// has to race-dismiss).

#include "errorpresentation.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcErrorPresentation, "kartend.errorpresentation")
} // namespace

namespace ErrorPresentation {

namespace {
// Test-only overrides + the production setShowErrorOverride install
// point. setShowErrorOverride below replaces the default with the
// supplied function pointer; left empty in headless contexts.
ShowErrorFn g_showErrorOverride;
ShowCriticalErrorFn g_showCriticalErrorOverride;
} // namespace

void showError(QWidget *parent, const ErrorUtils::ErrorContext &context) {
  if (g_showErrorOverride) {
    g_showErrorOverride(parent, context);
    return;
  }
  // Headless fallback: emit a single log line carrying the same fields the
  // modal would have shown. kartend_ui's MainWindow startup installs a
  // QDialog-backed override that supplants this in production.
  qCWarning(lcErrorPresentation).nospace()
      << "showError (no UI override installed): " << context.message
      << " — source=" << context.source << " — details=" << context.details
      << " — code=" << static_cast<int>(context.code);
}

bool showCriticalError(QWidget *parent, const ErrorUtils::ErrorContext &context,
                       bool allowContinue) {
  if (g_showCriticalErrorOverride) {
    return g_showCriticalErrorOverride(parent, context, allowContinue);
  }
  // Headless fallback: log + continue (return true) so a missing override
  // doesn't terminate a test run. Production installs the real modal.
  qCCritical(lcErrorPresentation).nospace()
      << "showCriticalError (no UI override installed): " << context.message
      << " — source=" << context.source << " — details=" << context.details
      << " — code=" << static_cast<int>(context.code) << " — allowContinue=" << allowContinue;
  return true;
}

void setShowErrorOverride(ShowErrorFn fn) {
  g_showErrorOverride = std::move(fn);
}
void setShowCriticalErrorOverride(ShowCriticalErrorFn fn) {
  g_showCriticalErrorOverride = std::move(fn);
}

} // namespace ErrorPresentation
