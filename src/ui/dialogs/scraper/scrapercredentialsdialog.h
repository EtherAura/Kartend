#ifndef SCRAPERCREDENTIALSDIALOG_H
#define SCRAPERCREDENTIALSDIALOG_H

#include <QDialog>

#include "collection/generalsettings.h"
#include "settingsmodel.h"

class ISettingsManager;
class ScraperCredentialsPanel;

/// Modal wrapper around the shared ScraperCredentialsPanel in filterless
/// mode (every provider's fields at once). The panel owns the form
/// composition — a new authed provider added to the panel shows up here
/// automatically. Save flushes the panel into a working copy, commits it
/// to the caller's GeneralSettings, and persists via ISettingsManager;
/// Cancel discards the working copy.
class ScraperCredentialsDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperCredentialsDialog)
public:
  ScraperCredentialsDialog(GeneralSettings *generalSettings, ISettingsManager *settingsManager,
                           QWidget *parent = nullptr);
  ~ScraperCredentialsDialog() override;

private slots:
  void onSave();

private:
  void buildUi();

  GeneralSettings *m_generalSettings = nullptr;
  ISettingsManager *m_settingsManager = nullptr;

  /// Working copy the embedded panel edits. The panel write-throughs its
  /// model on every keystroke (deferred-save shape), so pointing it at the
  /// caller's live settings would leak edits on Cancel.
  GeneralSettings m_working;
  /// Non-owning model aggregate targeting m_working; only generalSettings
  /// is wired — the credentials panel reads nothing else.
  SettingsModel m_model;
  ScraperCredentialsPanel *m_panel = nullptr;
};

#endif // SCRAPERCREDENTIALSDIALOG_H
