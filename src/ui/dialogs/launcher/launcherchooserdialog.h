#ifndef LAUNCHERCHOOSERDIALOG_H
#define LAUNCHERCHOOSERDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QListWidget;
QT_END_NAMESPACE

/// Modal launcher picker shown when a collection has more than one launcher
/// configured. Lists each launcher's display name; Enter or
/// double-click on the selected row launches with that launcher; Escape
/// cancels.
///
/// Use the static `choose()` helper instead of constructing directly:
///
/// \code
/// int idx = LauncherChooserDialog::choose(parent, collectionName,
///                                         launcherNames, defaultIndex);
/// if (idx < 0) { /* user cancelled */ }
/// \endcode
class LauncherChooserDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LauncherChooserDialog)
public:
  explicit LauncherChooserDialog(QWidget *parent, const QString &collectionName,
                                 const QStringList &launcherNames, int defaultIndex);

  /// Returns the selected launcher index, or -1 if the user cancelled.
  [[nodiscard]] int chosenIndex() const;

  /// Convenience wrapper that creates, executes, and reads the chosen index
  /// in one call. Returns -1 on cancel.
  [[nodiscard]] static int choose(QWidget *parent, const QString &collectionName,
                                  const QStringList &launcherNames, int defaultIndex);

private:
  QListWidget *m_list = nullptr;
};

#endif // LAUNCHERCHOOSERDIALOG_H
