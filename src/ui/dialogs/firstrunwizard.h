#ifndef FIRSTRUNWIZARD_H
#define FIRSTRUNWIZARD_H

#include <QString>
#include <QWizard>

#include "collectionutils.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

/// Three-step welcome wizard for new installs.
///
/// Pages:
///   1. Welcome      — short pitch + Get Started / Skip
///   2. Media folder — name + media-folder picker, validated before Next
///   3. Done         — summary, pointer to Settings → Launchers
///
/// On accept the caller reads `result()`: when `accepted` is true and
/// `pickedConfig.mediaDirectory` is non-empty, the caller appends the
/// CollectionConfig and persists. Either way the caller flips
/// GeneralSettings::firstRunComplete to true so the auto-launch never fires
/// again — re-running via Help → Setup Wizard… stays available.
class FirstRunWizard : public QWizard {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(FirstRunWizard)
public:
  struct Result {
    /// True when the user clicked Finish (vs Cancel/close).
    bool accepted = false;
    /// Populated when the user picked a folder on the media-folder page.
    /// Empty mediaDirectory means the user finished without configuring a
    /// collection (Skip path); the caller should still flip
    /// firstRunComplete so the wizard doesn't reappear.
    CollectionConfig pickedConfig;
  };

  explicit FirstRunWizard(QWidget *parent = nullptr);

  [[nodiscard]] Result result() const { return m_result; }

private:
  void buildWelcomePage();
  void buildMediaFolderPage();
  void buildDonePage();

  Result m_result;
};

#endif // FIRSTRUNWIZARD_H
