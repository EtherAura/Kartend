#include "datauditdownloadpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "nointrodownloader.h" // NoIntroDownload::Options / Progress

namespace {
// Accept a bare DAT-o-MATIC system id or a daily-download URL with ?s=<id>.
int parseSystemId(const QString &input) {
  const QString t = input.trimmed();
  bool ok = false;
  const int direct = t.toInt(&ok);
  if (ok && direct > 0) {
    return direct;
  }
  const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("[?&]s=(\\d+)")).match(t);
  return m.hasMatch() ? m.captured(1).toInt() : 0;
}
} // namespace

DatAuditDownloadPage::DatAuditDownloadPage(DatAuditDownloadService *service, QWidget *parent)
    : QWidget(parent), m_service(service) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  // Source selector (Kartend-m6qsb.23): No-Intro vs Redump drive different
  // widget sets below — onDownloadSourceChanged toggles them.
  auto *sourceRow = new QHBoxLayout();
  sourceRow->addWidget(new QLabel(tr("Source:"), this));
  m_dlSourceCombo = new QComboBox(this);
  m_dlSourceCombo->addItem(tr("No-Intro (DAT-o-MATIC)"), QStringLiteral("nointro"));
  m_dlSourceCombo->addItem(tr("Redump"), QStringLiteral("redump"));
  sourceRow->addWidget(m_dlSourceCombo);
  sourceRow->addStretch();
  root->addLayout(sourceRow);

  // The two mutually-exclusive source sub-forms (Kartend-139sr): No-Intro's box
  // + its sets box, then Redump's box. onDownloadSourceChanged toggles them.
  root->addWidget(buildNoIntroSource());
  root->addWidget(m_setsBox);
  root->addWidget(buildRedumpSource());

  auto *dlRow = new QHBoxLayout();
  m_dlDownloadButton = new QPushButton(tr("Download && import"), this);
  m_dlDownloadButton->setEnabled(false);
  m_dlCancelButton = new QPushButton(tr("Cancel"), this);
  m_dlCancelButton->setEnabled(false);
  m_dlProgress = new QProgressBar(this);
  m_dlProgress->setVisible(false);
  dlRow->addWidget(m_dlDownloadButton);
  dlRow->addWidget(m_dlCancelButton);
  dlRow->addWidget(m_dlProgress, 1);
  root->addLayout(dlRow);

  m_dlStatus = new QLabel(this);
  m_dlStatus->setWordWrap(true);
  root->addWidget(m_dlStatus);
  root->addStretch(1);

  // Download page wiring (Kartend-m6qsb.16). The update-check watcher lives in
  // the dialog (driven by the library page's button).
  connect(m_dlSourceCombo, &QComboBox::currentIndexChanged, this,
          &DatAuditDownloadPage::onDownloadSourceChanged);
  connect(m_dlLoadButton, &QPushButton::clicked, this, &DatAuditDownloadPage::onLoadDailyForm);
  connect(m_redumpRefreshButton, &QPushButton::clicked, this, [this] {
    m_redumpSystemCombo->clear();
    onDownloadSourceChanged(); // re-triggers the fetch
  });
  connect(m_redumpSystemCombo, &QComboBox::currentIndexChanged, this,
          [this] { refreshDownloadButtonEnabled(); });
  connect(&m_redumpSystemsWatcher,
          &QFutureWatcher<DatAuditDownloadService::RedumpSystemsOutcome>::finished, this,
          &DatAuditDownloadPage::onRedumpSystemsFetched);
  connect(m_dlDownloadButton, &QPushButton::clicked, this, &DatAuditDownloadPage::onStartDownload);
  connect(m_dlCancelButton, &QPushButton::clicked, this, &DatAuditDownloadPage::onCancelDownload);
  connect(&m_dlFormWatcher, &QFutureWatcher<DatAuditDownloadService::DailyFormOutcome>::finished,
          this, &DatAuditDownloadPage::onLoadDailyFormFinished);
  connect(&m_dlWatcher, &QFutureWatcher<DatAuditDownloadService::Outcome>::finished, this,
          &DatAuditDownloadPage::onDownloadFinished);
}

DatAuditDownloadPage::~DatAuditDownloadPage() {
  // Stop any in-flight transfer/fetch so a worker never outlives the page it
  // reports into (mirrors the dialog's former teardown).
  if (m_dlCancel) {
    m_dlCancel->store(true);
  }
  if (m_dlWatcher.isRunning()) {
    m_dlWatcher.waitForFinished();
  }
  if (m_dlFormWatcher.isRunning()) {
    m_dlFormWatcher.waitForFinished();
  }
  if (m_redumpSystemsWatcher.isRunning()) {
    m_redumpSystemsWatcher.waitForFinished();
  }
}

void DatAuditDownloadPage::setLibraryPathAccessors(std::function<QString()> getter,
                                                   std::function<void(const QString &)> setter) {
  m_getLibraryPath = std::move(getter);
  m_saveLibraryPath = std::move(setter);
}

QWidget *DatAuditDownloadPage::buildNoIntroSource() {
  // --- No-Intro source ---
  m_noIntroBox = new QGroupBox(tr("No-Intro (DAT-o-MATIC)"), this);
  auto *form = new QFormLayout(m_noIntroBox);
  form->setHorizontalSpacing(16);
  form->setVerticalSpacing(6);

  auto *sysRow = new QHBoxLayout();
  m_dlSystemEdit = new QLineEdit(m_noIntroBox);
  m_dlSystemEdit->setPlaceholderText(tr("DAT-o-MATIC system id or daily-download URL"));
  m_dlLoadButton = new QPushButton(tr("Load"), m_noIntroBox);
  sysRow->addWidget(m_dlSystemEdit, 1);
  sysRow->addWidget(m_dlLoadButton);
  form->addRow(tr("System:"), sysRow);

  m_dlDatTypeCombo = new QComboBox(m_noIntroBox);
  m_dlDatTypeCombo->setEnabled(false);
  form->addRow(tr("DAT type:"), m_dlDatTypeCombo);

  m_dlPackLabel = new QLabel(tr("Load a system to see the available pack."), m_noIntroBox);
  m_dlPackLabel->setForegroundRole(QPalette::PlaceholderText);
  form->addRow(tr("Pack:"), m_dlPackLabel);

  // The "Include sets" group belongs to the No-Intro source; it is created here
  // but added to the page root separately by the ctor so the widget add-order
  // stays exactly as before.
  m_setsBox = new QGroupBox(tr("Include sets"), this);
  auto *setsOuter = new QVBoxLayout(m_setsBox);
  m_dlSetsContainer = new QWidget(m_setsBox);
  m_dlSetsLayout = new QVBoxLayout(m_dlSetsContainer);
  m_dlSetsLayout->setContentsMargins(0, 0, 0, 0);
  setsOuter->addWidget(m_dlSetsContainer);
  return m_noIntroBox;
}

QWidget *DatAuditDownloadPage::buildRedumpSource() {
  // --- Redump source (one GET per system; pick by name) ---
  m_redumpBox = new QGroupBox(tr("Redump"), this);
  auto *redumpRow = new QHBoxLayout(m_redumpBox);
  redumpRow->addWidget(new QLabel(tr("System:"), m_redumpBox));
  m_redumpSystemCombo = new QComboBox(m_redumpBox);
  m_redumpSystemCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_redumpRefreshButton = new QPushButton(tr("Refresh list"), m_redumpBox);
  redumpRow->addWidget(m_redumpSystemCombo, 1);
  redumpRow->addWidget(m_redumpRefreshButton);
  m_redumpBox->setVisible(false);
  return m_redumpBox;
}

void DatAuditDownloadPage::rebuildSetCheckboxes() {
  QLayoutItem *item = nullptr;
  while ((item = m_dlSetsLayout->takeAt(0)) != nullptr) {
    if (item->widget() != nullptr) {
      item->widget()->deleteLater();
    }
    delete item;
  }
  m_dlSetChecks.clear();
  for (const NoIntroParse::DailySet &s : m_dailyForm.sets) {
    auto *cb = new QCheckBox(s.label.isEmpty() ? s.field : s.label, m_dlSetsContainer);
    cb->setChecked(s.checkedByDefault);
    m_dlSetsLayout->addWidget(cb);
    m_dlSetChecks.append(cb);
  }
  m_dlDatTypeCombo->clear();
  for (const QString &dt : m_dailyForm.datTypes) {
    m_dlDatTypeCombo->addItem(dt);
  }
  const int idx = m_dlDatTypeCombo->findText(m_dailyForm.defaultDatType);
  m_dlDatTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  m_dlDatTypeCombo->setEnabled(m_dlDatTypeCombo->count() > 0);
}

DatAuditDownloadPage::DownloadSource DatAuditDownloadPage::currentDownloadSource() const {
  return m_dlSourceCombo->currentData().toString() == QLatin1String("redump")
             ? DownloadSource::Redump
             : DownloadSource::NoIntro;
}

void DatAuditDownloadPage::refreshDownloadButtonEnabled() {
  bool ok = false;
  if (!m_downloading) {
    if (currentDownloadSource() == DownloadSource::NoIntro) {
      ok = m_dailyForm.valid;
    } else {
      ok = m_redumpSystemCombo->count() > 0 &&
           !m_redumpSystemCombo->currentData().toString().isEmpty();
    }
  }
  m_dlDownloadButton->setEnabled(ok);
}

void DatAuditDownloadPage::setDownloadBusy(bool busy) {
  m_downloading = busy;
  m_dlSourceCombo->setEnabled(!busy);
  m_dlLoadButton->setEnabled(!busy);
  m_dlSystemEdit->setEnabled(!busy);
  m_redumpSystemCombo->setEnabled(!busy);
  m_redumpRefreshButton->setEnabled(!busy);
  m_dlCancelButton->setEnabled(busy);
  m_dlProgress->setVisible(busy);
  if (busy) {
    m_dlProgress->setRange(0, 0); // indeterminate until the transfer reports a total
  }
  refreshDownloadButtonEnabled();
}

void DatAuditDownloadPage::onDownloadSourceChanged() {
  const bool redump = currentDownloadSource() == DownloadSource::Redump;
  m_noIntroBox->setVisible(!redump);
  m_setsBox->setVisible(!redump);
  m_redumpBox->setVisible(redump);
  // Lazily fetch the redump systems list the first time the user switches.
  if (redump && m_redumpSystemCombo->count() == 0 && !m_redumpSystemsWatcher.isRunning()) {
    m_redumpRefreshButton->setEnabled(false);
    m_dlStatus->setText(tr("Loading systems from redump.org…"));
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_redumpSystemsWatcher.setFuture(m_service->startRedumpSystemsFetch(cancel));
  }
  refreshDownloadButtonEnabled();
}

void DatAuditDownloadPage::onRedumpSystemsFetched() {
  const DatAuditDownloadService::RedumpSystemsOutcome o = m_redumpSystemsWatcher.result();
  m_redumpRefreshButton->setEnabled(true);
  if (!o.ok) {
    m_dlStatus->setText(o.error);
    return;
  }
  m_redumpSystemCombo->clear();
  for (const RedumpParse::System &s : o.systems) {
    m_redumpSystemCombo->addItem(s.name, s.slug);
  }
  m_dlStatus->setText(tr("%n system(s) available — pick one and Download & import.", nullptr,
                         static_cast<int>(o.systems.size())));
  refreshDownloadButtonEnabled();
}

void DatAuditDownloadPage::onLoadDailyForm() {
  const int sysId = parseSystemId(m_dlSystemEdit->text());
  if (sysId <= 0) {
    m_dlStatus->setText(tr("Enter a DAT-o-MATIC system id or daily-download URL."));
    return;
  }
  m_dlCancel = std::make_shared<std::atomic<bool>>(false);
  setDownloadBusy(true);
  m_dlStatus->setText(tr("Loading available pack…"));
  auto cancel = m_dlCancel;
  m_dlFormWatcher.setFuture(m_service->startDailyFormFetch(sysId, cancel));
}

void DatAuditDownloadPage::onLoadDailyFormFinished() {
  const DatAuditDownloadService::DailyFormOutcome o = m_dlFormWatcher.result();
  setDownloadBusy(false);
  if (!o.ok) {
    m_dailyForm = NoIntroParse::DailyForm{};
    m_dlDownloadButton->setEnabled(false);
    m_dlStatus->setText(o.error);
    return;
  }
  m_dailyForm = o.form;
  rebuildSetCheckboxes();
  m_dlPackLabel->setText(m_dailyForm.packDate.isEmpty()
                             ? tr("Available")
                             : tr("Available pack: %1").arg(m_dailyForm.packDate));
  refreshDownloadButtonEnabled();
  m_dlStatus->setText(tr("Ready. Choose sets, then Download & import."));
}

void DatAuditDownloadPage::onStartDownload() {
  if (!m_getLibraryPath) {
    m_dlStatus->setText(tr("Set a DAT library folder on the DAT Library page first."));
    return;
  }
  QString lib = m_getLibraryPath().trimmed();
  if (lib.isEmpty()) {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a DAT library folder"));
    if (dir.isEmpty()) {
      return;
    }
    lib = dir;
    if (m_saveLibraryPath) {
      m_saveLibraryPath(dir);
    }
    emit libraryPathChanged(dir);
  }
  const bool redump = currentDownloadSource() == DownloadSource::Redump;

  // Gather the source-specific request before flipping busy.
  NoIntroDownload::Options niOpts;
  QString redumpSlug;
  if (redump) {
    redumpSlug = m_redumpSystemCombo->currentData().toString();
    if (redumpSlug.isEmpty()) {
      m_dlStatus->setText(tr("Pick a system first."));
      return;
    }
  } else {
    const int sysId = parseSystemId(m_dlSystemEdit->text());
    if (sysId <= 0 || !m_dailyForm.valid) {
      m_dlStatus->setText(tr("Load a system first."));
      return;
    }
    niOpts.systemId = sysId;
    niOpts.datType = m_dlDatTypeCombo->currentText().isEmpty() ? m_dailyForm.defaultDatType
                                                               : m_dlDatTypeCombo->currentText();
    for (int i = 0; i < m_dlSetChecks.size() && i < m_dailyForm.sets.size(); ++i) {
      if (m_dlSetChecks.at(i)->isChecked()) {
        niOpts.selectedSets.append(m_dailyForm.sets.at(i).field);
      }
    }
  }

  m_dlCancel = std::make_shared<std::atomic<bool>>(false);
  setDownloadBusy(true);
  m_dlStatus->setText(tr("Downloading…"));
  auto cancel = m_dlCancel;
  // Marshal worker-thread progress onto the UI thread, throttle-free (the
  // transfer's own downloadProgress cadence is already coarse enough).
  auto progressFn = [this](const NoIntroDownload::Progress &p) {
    QMetaObject::invokeMethod(
        this,
        [this, p] {
          if (!m_downloading) {
            return;
          }
          if (p.total > 0) {
            m_dlProgress->setRange(0, 100);
            m_dlProgress->setValue(static_cast<int>(p.received * 100 / p.total));
          } else {
            m_dlProgress->setRange(0, 0);
          }
          if (!p.phase.isEmpty()) {
            m_dlStatus->setText(p.phase);
          }
        },
        Qt::QueuedConnection);
  };

  // No-Intro's revision is the daily pack date, known here on the UI thread;
  // Redump's is read from the downloaded DAT header inside the service.
  const QString packDate = redump ? QString() : m_dailyForm.packDate;
  DatAuditDownloadService::Request req;
  req.redump = redump;
  req.niOpts = niOpts;
  req.redumpSlug = redumpSlug;
  req.packDate = packDate;
  req.libraryDir = lib;
  m_dlWatcher.setFuture(m_service->startDownload(req, cancel, progressFn));
}

void DatAuditDownloadPage::onDownloadFinished() {
  const auto o = m_dlWatcher.result();
  setDownloadBusy(false);
  if (!o.ok) {
    m_dlStatus->setText(o.error);
    return;
  }
  m_dlStatus->setText(tr("Imported %n DAT file(s) into the library.", nullptr, o.datCount));
  m_service->recordProvenance(o);
  // Surface matches immediately via the library review.
  emit downloadCompleted();
}

void DatAuditDownloadPage::onCancelDownload() {
  if (m_dlCancel) {
    m_dlCancel->store(true);
  }
  m_dlCancelButton->setEnabled(false);
}
