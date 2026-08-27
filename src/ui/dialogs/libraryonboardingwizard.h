#ifndef LIBRARYONBOARDINGWIZARD_H
#define LIBRARYONBOARDINGWIZARD_H

#include <QWizard>

#include "collection/collectionconfig.h"
QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
QT_END_NAMESPACE

/// Multi-step wizard for creating a fully-configured collection.
///
/// Pages:
///   1. Welcome — short pitch, sets expectations.
///   2. Name + media directory — validated (non-empty + exists) before Next.
///   3. Media type + artwork directory — type picker seeded from
///      CollectionUtils::standardCollectionTypes() so it cannot drift from
///      the settings dialog's Media Type combo (Kartend-0ydo7); the combo
///      stays editable, so custom types are still typeable. Optional
///      artwork dir override.
///   4. Launcher detection — multi-select list of installed launchers from
///      LauncherProbe; the first checked entry becomes the primary launcher.
///   5. Summary — confirm before Finish. Echoes every step, naming the ones
///      left unset rather than dropping their row (Kartend-8cxjy).
///
/// Returns a populated CollectionConfig via `result()` after `exec()`
/// returns Accepted; the caller persists the new config via
/// SettingsManager::saveCollections and switches to it. The wizard does
/// NOT touch the live collection list itself.
class LibraryOnboardingWizard : public QWizard {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LibraryOnboardingWizard)
public:
  struct Result {
    /// True when the user clicked Finish (vs Cancel / window close).
    bool accepted = false;
    /// Fully-populated config — name, mediaDirectory, type, artwork
    /// directory, and primary launcher (when one was picked) all set
    /// from the wizard fields.
    CollectionConfig pickedConfig;
  };

  explicit LibraryOnboardingWizard(QWidget *parent = nullptr);

  [[nodiscard]] Result result() const { return m_result; }

private:
  void buildWelcomePage();
  void buildMediaPage();
  void buildTypePage();
  void buildLauncherPage();
  void buildSummaryPage();

  /// Pulled from each page's field by the validator so Finish builds the
  /// full CollectionConfig without re-reading widgets.
  Result m_result;

  /// addPage() id of the LauncherPage, captured when it's added so the Finish
  /// handler finds it regardless of page order (its rows aren't field-registered,
  /// so it has to be located and queried directly). -1 until built.
  int m_launcherPageId = -1;
};

#endif // LIBRARYONBOARDINGWIZARD_H
