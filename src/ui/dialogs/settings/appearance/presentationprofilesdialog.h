#ifndef PRESENTATIONPROFILESDIALOG_H
#define PRESENTATIONPROFILESDIALOG_H

#include <QDialog>
#include <QList>

#include "collection/presentationprofile.h"

struct GeneralSettings;

QT_BEGIN_NAMESPACE
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

/// Modal dialog for managing the user's presentation-profile registry
/// (attract mode + marquee + splash settings). Mirrors
/// LayoutProfilesDialog's "list-with-buttons" shape: the dialog mutates
/// the supplied `profiles` list in place and the caller persists it on
/// close.
class PresentationProfilesDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(PresentationProfilesDialog)
public:
  using ApplyHandler = std::function<void(const PresentationProfile &)>;

  explicit PresentationProfilesDialog(QWidget *parent = nullptr);

  /// Caller passes the live registry list, the current GeneralSettings
  /// (used to seed Save-current), and the closure that writes the
  /// chosen profile onto the application's live settings.
  void setRegistry(QList<PresentationProfile> *profiles, const GeneralSettings *currentSettings,
                   ApplyHandler onApply);

private slots:
  void onSaveCurrent();
  void onApplySelected();
  void onDeleteSelected();
  void onSelectionChanged();

private:
  void setupUi();
  void refreshList(const QString &selectName = {});
  [[nodiscard]] int selectedRow() const;

  QListWidget *m_list = nullptr;
  QPushButton *m_saveButton = nullptr;
  QPushButton *m_applyButton = nullptr;
  QPushButton *m_deleteButton = nullptr;

  QList<PresentationProfile> *m_profiles = nullptr;
  const GeneralSettings *m_currentSettings = nullptr;
  ApplyHandler m_onApply;
};

#endif // PRESENTATIONPROFILESDIALOG_H
