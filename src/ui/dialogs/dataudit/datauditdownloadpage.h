#ifndef DATAUDITDOWNLOADPAGE_H
#define DATAUDITDOWNLOADPAGE_H

#include <atomic>
#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QList>
#include <QWidget>

#include "datauditdownloadservice.h"
#include "nointroparse.h" // NoIntroParse::DailyForm

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

/// The "Download" stacked page of the DAT Manager (Kartend-oa0lu): the No-Intro
/// / Redump source forms and the download-and-import flow. It owns its widgets
/// and the three QFutureWatchers, drives the off-thread work through the
/// injected DatAuditDownloadService, and emits intent for the cross-page bits
/// (the library-path label lives on DatAuditLibraryPage; the post-download
/// review is opened by the dialog). Mirrors the DatAuditBrowserPage precedent.
/// The "check for updates" flow stays in the dialog (it is driven by the library
/// page's button), sharing the same service instance.
class DatAuditDownloadPage : public QWidget {
  Q_OBJECT

public:
  explicit DatAuditDownloadPage(DatAuditDownloadService *service, QWidget *parent = nullptr);
  ~DatAuditDownloadPage() override;

  /// Library destination accessors — the download writes DATs there; an empty
  /// getter result prompts the user, persists via the setter, and reports back
  /// through libraryPathChanged.
  void setLibraryPathAccessors(std::function<QString()> getter,
                               std::function<void(const QString &)> setter);

signals:
  /// A download chose a new library folder — the dialog updates the library
  /// page's label (persistence already happened via the injected setter).
  void libraryPathChanged(const QString &path);
  /// A download + import finished successfully — the dialog opens the review.
  void downloadCompleted();

private:
  enum class DownloadSource { NoIntro, Redump };
  [[nodiscard]] DownloadSource currentDownloadSource() const;

  QWidget *buildNoIntroSource();
  QWidget *buildRedumpSource();
  void refreshDownloadButtonEnabled();
  void setDownloadBusy(bool busy);
  void rebuildSetCheckboxes(); // from m_dailyForm
  void onDownloadSourceChanged();
  void onRedumpSystemsFetched();
  void onLoadDailyForm();
  void onLoadDailyFormFinished();
  void onStartDownload();
  void onDownloadFinished();
  void onCancelDownload();

  DatAuditDownloadService *m_service = nullptr;
  std::function<QString()> m_getLibraryPath;
  std::function<void(const QString &)> m_saveLibraryPath;

  QComboBox *m_dlSourceCombo = nullptr; ///< No-Intro | Redump
  QGroupBox *m_noIntroBox = nullptr;
  QGroupBox *m_setsBox = nullptr;
  QGroupBox *m_redumpBox = nullptr;
  QLineEdit *m_dlSystemEdit = nullptr;
  QPushButton *m_dlLoadButton = nullptr;
  QWidget *m_dlSetsContainer = nullptr;
  QVBoxLayout *m_dlSetsLayout = nullptr;
  QList<QCheckBox *> m_dlSetChecks;
  QComboBox *m_dlDatTypeCombo = nullptr;
  QLabel *m_dlPackLabel = nullptr;
  QComboBox *m_redumpSystemCombo = nullptr;
  QPushButton *m_redumpRefreshButton = nullptr;
  QPushButton *m_dlDownloadButton = nullptr;
  QPushButton *m_dlCancelButton = nullptr;
  QProgressBar *m_dlProgress = nullptr;
  QLabel *m_dlStatus = nullptr;
  NoIntroParse::DailyForm m_dailyForm;
  bool m_downloading = false;
  std::shared_ptr<std::atomic<bool>> m_dlCancel;
  QFutureWatcher<DatAuditDownloadService::DailyFormOutcome> m_dlFormWatcher;
  QFutureWatcher<DatAuditDownloadService::RedumpSystemsOutcome> m_redumpSystemsWatcher;
  QFutureWatcher<DatAuditDownloadService::Outcome> m_dlWatcher;
};

#endif // DATAUDITDOWNLOADPAGE_H
