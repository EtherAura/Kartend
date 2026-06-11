#ifndef DIALOGRUNNERS_H
#define DIALOGRUNNERS_H

// Generic owner-supplied modal-dialog runners (Kartend-sqoq0).
//
// Extends the dialog-runner convention from Kartend-n8kh (owner supplies a
// closure that constructs the UI-layer dialog; the module invokes it and
// reads the result) to the *stock Qt* modals — QMessageBox / QInputDialog /
// QFileDialog — that modules-layer code previously constructed directly.
//
// Usage pattern: a DialogRunners value rides on the module's existing setup
// struct (InteractionManagerSetup, KartManagerSetup, SettingsDialogContext,
// PlaylistMenuControllerSetup). MainWindow populates the closures at wiring
// time (see MainWindow::makeDialogRunners); every closure parents its dialog
// on the main window. Each field may be left null — call sites fall back to
// constructing the stock Qt dialog directly, so production behavior without
// wiring is unchanged and headless tests get a stubbing seam: supply a
// closure returning a canned answer and no modal ever opens.
//
// This is a plain value struct of std::functions: copyable, no Qt Widgets
// includes, safe at the utils layer.

#include <functional>
#include <optional>
#include <QString>

struct DialogRunners {
  /// Yes/No confirmation, No as the default button. Returns true on Yes.
  std::function<bool(const QString &title, const QString &text)> confirm;

  /// Warning message box (single OK button).
  std::function<void(const QString &title, const QString &text)> warn;

  /// Informational message box (single OK button).
  std::function<void(const QString &title, const QString &text)> info;

  /// Single-line text prompt seeded with @p initialText. Returns nullopt on
  /// cancel; an accepted-but-empty entry returns an empty string.
  std::function<std::optional<QString>(const QString &title, const QString &label,
                                       const QString &initialText)>
      getText;

  /// File-open picker. Returns the selected path or empty on cancel.
  std::function<QString(const QString &caption, const QString &startPath, const QString &filter)>
      getOpenFileName;

  /// File-save picker. Returns the chosen path or empty on cancel.
  std::function<QString(const QString &caption, const QString &startPath, const QString &filter)>
      getSaveFileName;

  /// Directory picker (dirs only, symlinks unresolved — matches the
  /// QFileDialog flags every current call site uses). Empty on cancel.
  std::function<QString(const QString &caption, const QString &startDir)> getExistingDirectory;
};

#endif // DIALOGRUNNERS_H
