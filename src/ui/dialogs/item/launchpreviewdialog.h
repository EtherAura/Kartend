#ifndef LAUNCHPREVIEWDIALOG_H
#define LAUNCHPREVIEWDIALOG_H

#include <QDialog>

#include "launchmanager.h" // LaunchPreview

QT_BEGIN_NAMESPACE
class QLabel;
class QPlainTextEdit;
class QVBoxLayout;
QT_END_NAMESPACE

/// Read-only viewer for a `LaunchManager::previewLaunchCommand` result.
/// Shows the resolved program + arguments, archive extraction info, and
/// any warnings the dry-run validation surfaced, with a single Close
/// button. Strictly side-effect-free — no launching happens from here.
class LaunchPreviewDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LaunchPreviewDialog)
public:
  explicit LaunchPreviewDialog(QWidget *parent = nullptr);

  /// Replaces the dialog's contents with the given preview. Safe to call
  /// repeatedly so the same dialog instance can be reused for different
  /// items, though the current call sites construct fresh dialogs per
  /// invocation.
  void setPreview(const QString &itemTitle, const QString &launcherName, const QString &filePath,
                  const LaunchPreview &preview);

private:
  void setupUi();

  QLabel *m_headerLabel = nullptr;
  QLabel *m_launcherLabel = nullptr;
  QLabel *m_resolvedLabel = nullptr;
  QLabel *m_fileLabel = nullptr;
  QLabel *m_archiveLabel = nullptr;
  QPlainTextEdit *m_commandView = nullptr;
  QLabel *m_warningsLabel = nullptr;
};

#endif // LAUNCHPREVIEWDIALOG_H
