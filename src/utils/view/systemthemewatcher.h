#ifndef SYSTEMTHEMEWATCHER_H
#define SYSTEMTHEMEWATCHER_H

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

/// Watches the desktop's colour configuration for *runtime* changes and emits
/// themeChanged() (debounced) when one is detected.
///
/// Why this exists: on KDE, a wallpaper-derived accent colour shifting at
/// runtime (e.g. switching Plasma activities, or a Picture-of-the-Day rollover)
/// updates QApplication::palette() via the platform theme — but Qt does **not**
/// reliably dispatch a QEvent::ApplicationPaletteChange / PaletteChange to the
/// running app's widgets for an accent-only change, so nothing repaints (the
/// classic "have to F5 / restart for colours to update" symptom). KDE persists
/// the active accent + colour scheme to `kdeglobals`, so we watch that file and
/// surface a single themeChanged() per change; the slot then re-broadcasts the
/// (already-fresh) application palette so every widget re-resolves and repaints.
///
/// Linux/KDE only — elsewhere the watcher constructs but never emits.
class SystemThemeWatcher : public QObject {
  Q_OBJECT
public:
  explicit SystemThemeWatcher(QObject *parent = nullptr);

signals:
  /// Emitted (debounced) when the desktop accent / colour scheme changes.
  void themeChanged();

private:
  void onConfigPathChanged(const QString &path);
  /// Re-add kdeglobals to the watch set if an atomic save (write-temp +
  /// rename) dropped it; returns true if it had to be re-armed (i.e. the file
  /// was just replaced, which is itself a change signal).
  bool rearmKdeglobalsWatch();

  QFileSystemWatcher *m_watcher = nullptr;
  QTimer *m_debounce = nullptr;
  QString m_kdeglobalsPath;
};

#endif // SYSTEMTHEMEWATCHER_H
