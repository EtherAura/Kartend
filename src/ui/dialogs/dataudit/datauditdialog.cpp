#include "datauditdialog.h"

#include <QApplication>
#include <QAtomicInteger>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QtConcurrent/QtConcurrentRun>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "datauditbrowsermodels.h" // DatAudit::auditRowsFromResults
#include "datauditbrowserpage.h"
#include "datauditdownloadpage.h"
#include "datauditexport.h"
#include "datauditfixdialog.h"
#include "datauditlibrarypage.h"
#include "datauditmodel.h"
#include "datauditprofile.h"
#include "datauditprofiledialog.h"
#include "datauditresultdelegate.h"
#include "datcache.h"
#include "datlibrarystate.h"
#include "datlookup.h"
#include "errorutils.h"
#include "formbuilders.h"
#include "nointrodownloader.h"
#include "pathutils.h"
#include "redumpdownload.h"
#include "uiconstants/icons.h"

DatAuditDialog::DatAuditDialog(QWidget *parent) : QDialog(parent) {
  // Kartend-ahf3d: the download/provenance/update-check orchestration lives in
  // DatAuditDownloadService (stage 1); its provenance accessor now reaches
  // SQLite through DatAuditProfileStore (stage 2), so profile + provenance
  // persistence no longer opens a QSqlDatabase in this dialog. (The audit-run
  // hash-cache connection in onRun is the remaining direct DB use — a stage-3
  // DatAuditRunner concern.)
  m_downloadService =
      std::make_unique<DatAuditDownloadService>(DatAuditDownloadService::ProvenanceAccess{
          [this] { return m_profileStore.loadAllProvenance(); },
          [this](const DatLibraryState::Provenance &pr) { m_profileStore.recordProvenance(pr); }});
  setWindowTitle(tr("DAT Manager"));
  setWindowFlag(Qt::Window, true);
  // Default opening size, and a generous minimum floor: without an explicit
  // minimum the window can be dragged down to the layout's tiny hint, cramping
  // the nav rail + the Browser's tree/DAT-info/game/ROM panes (the ROM table
  // alone has 9 columns) and the multi-column Audit results. Keep it usable.
  resize(960, 640);
  setMinimumSize(880, 600);

  // Shell: nav rail + (context header + stacked pages), mirroring the Settings
  // dialog. Native Breeze palette; the only stylesheet is the transparent
  // splitter handle the settings dialog also uses.
  auto *root = new QHBoxLayout(this);
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  m_splitter = splitter;
  splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  splitter->setChildrenCollapsible(false);

  m_nav = new QListWidget(splitter);
  m_nav->setObjectName(QStringLiteral("datNav"));

  auto *rightWrap = new QWidget(splitter);
  auto *rightCol = new QVBoxLayout(rightWrap);
  rightCol->setContentsMargins(0, 0, 0, 0);
  auto *headerRow = new QHBoxLayout();
  m_contextIcon = new QLabel(rightWrap);
  m_contextTitle = new QLabel(rightWrap);
  QFont titleFont = m_contextTitle->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
  m_contextTitle->setFont(titleFont);
  headerRow->addWidget(m_contextIcon);
  headerRow->addWidget(m_contextTitle, 1);
  rightCol->addLayout(headerRow);
  m_pages = new QStackedWidget(rightWrap);
  rightCol->addWidget(m_pages, 1);

  m_auditPage = new DatAuditAuditPage(m_profileStore, this);
  m_pages->addWidget(m_auditPage); // index 0
  m_libraryPage = new DatAuditLibraryPage(this);
  m_pages->addWidget(m_libraryPage); // index 1
  m_downloadPage = new DatAuditDownloadPage(m_downloadService.get(), this);
  m_pages->addWidget(m_downloadPage); // index 2
  // The download page (Kartend-oa0lu) writes the library-path label (which lives
  // on the library page) and opens the review after a successful import.
  connect(m_downloadPage, &DatAuditDownloadPage::libraryPathChanged, this,
          [this](const QString &p) { m_libraryPage->setLibraryPathText(p); });
  connect(m_downloadPage, &DatAuditDownloadPage::downloadCompleted, this, [this] {
    if (m_libraryOpener) {
      m_libraryOpener();
    }
  });
  m_browserPage = new DatAuditBrowserPage(this);
  m_pages->addWidget(m_browserPage); // index 3
  // Library-page actions (Kartend-oa0lu): the page emits intent; the dialog
  // keeps the opener / update-check / import handlers.
  connect(m_libraryPage, &DatAuditLibraryPage::reviewRequested, this, [this] {
    if (m_libraryOpener) {
      m_libraryOpener();
    }
  });
  connect(m_libraryPage, &DatAuditLibraryPage::checkUpdatesRequested, this,
          &DatAuditDialog::onCheckUpdates);
  connect(m_libraryPage, &DatAuditLibraryPage::importZipRequested, this,
          &DatAuditDialog::onImportZip);
  connect(m_libraryPage, &DatAuditLibraryPage::importFolderRequested, this,
          &DatAuditDialog::onImportFolder);
  // Browser write-actions (Kartend-7iqhl.2): the browser asks; the audit page
  // (which owns the runner + Fix dialog, Kartend-oa0lu) does the work and reports
  // back through the dialog, which refreshes the browser tree + mirrors progress.
  connect(m_browserPage, &DatAuditBrowserPage::reauditProfileRequested, m_auditPage,
          &DatAuditAuditPage::reauditProfile);
  connect(m_browserPage, &DatAuditBrowserPage::fixProfileRequested, m_auditPage,
          &DatAuditAuditPage::fixProfile);
  connect(m_auditPage, &DatAuditAuditPage::browserNodeRefreshRequested, this,
          [this](qint64 profileId) {
            if (m_browserPage != nullptr) {
              m_browserPage->refresh();
              m_browserPage->selectProfileNode(profileId);
            }
          });
  connect(m_auditPage, &DatAuditAuditPage::browserAuditProgress, this, [this](int done, int total) {
    if (m_browserPage != nullptr) {
      m_browserPage->setAuditProgress(done, total);
    }
  });
  connect(m_auditPage, &DatAuditAuditPage::browserAuditRunningChanged, this, [this](bool running) {
    if (m_browserPage != nullptr) {
      m_browserPage->setAuditRunning(running);
    }
  });

  splitter->addWidget(m_nav);
  splitter->addWidget(rightWrap);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({220, 740});
  root->addWidget(splitter);

  addNavEntry(tr("Audit"), {QStringLiteral("document-edit"), QStringLiteral("configure")}, 0);
  addNavEntry(tr("DAT Library"), {QStringLiteral("folder-sync"), QStringLiteral("folder")}, 1);
  addNavEntry(tr("Download"), {QStringLiteral("download"), QStringLiteral("emblem-downloads")}, 2);
  addNavEntry(tr("Browser"), {QStringLiteral("view-list-tree"), QStringLiteral("folder-table")}, 3);

  connect(m_nav, &QListWidget::currentRowChanged, this, &DatAuditDialog::onNavRowChanged);
  wireDownloadActions();

  applyUniformSizing();
  m_nav->setCurrentRow(0);
  restoreGeometry_();
}

void DatAuditDialog::wireDownloadActions() {
  // The download page owns its own wiring (Kartend-oa0lu); the dialog keeps only
  // the update-check result watcher, driven by the library page's button.
  connect(&m_updateWatcher, &QFutureWatcher<DatAuditDownloadService::UpdateResult>::finished, this,
          &DatAuditDialog::onCheckUpdatesFinished);
}

void DatAuditDialog::addNavEntry(const QString &label, const QStringList &iconNames,
                                 int pageIndex) {
  auto *item = new QListWidgetItem(label, m_nav);
  for (const QString &name : iconNames) {
    const QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) {
      item->setIcon(icon);
      break;
    }
  }
  item->setData(Qt::UserRole, pageIndex);
}

void DatAuditDialog::onNavRowChanged() {
  const QListWidgetItem *item = m_nav->currentItem();
  if (item == nullptr) {
    return;
  }
  const int page = item->data(Qt::UserRole).toInt();
  m_pages->setCurrentIndex(page);
  m_contextIcon->setPixmap(item->icon().pixmap(28, 28));
  m_contextTitle->setText(item->text());
  // The browser reads persisted results on demand — refresh it each time it is
  // shown so a just-completed audit is reflected without reopening the window.
  if (page == 3 && m_browserPage != nullptr) {
    // Resolve uuid → collection name so the browser can group profiles under
    // their linked collection when category grouping is on (Kartend-7iqhl.5).
    QHash<QString, QString> names;
    if (m_collections != nullptr) {
      for (const CollectionConfig &c : *m_collections) {
        const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
        names.insert(CollectionUtils::computeCollectionUuid(c.name, expanded), c.name);
      }
    }
    m_browserPage->setCollectionNames(names);
    m_browserPage->refresh();
  }
}

void DatAuditDialog::applyUniformSizing() {
  // Mirror the settings dialog's uniform field widths so the pages read as one
  // family. Buttons get a floor (long labels still grow); line edits/combos a
  // sensible cap so they don't stretch the full pane.
  constexpr int kFieldWidth = 360;
  constexpr int kButtonMinWidth = 96;
  for (auto *edit : m_pages->findChildren<QLineEdit *>()) {
    edit->setMaximumWidth(kFieldWidth);
  }
  for (auto *button : m_pages->findChildren<QPushButton *>()) {
    button->setMinimumWidth(kButtonMinWidth);
  }
  m_nav->setMaximumWidth(220);
}

void DatAuditDialog::hideEvent(QHideEvent *event) {
  persistGeometry();
  QDialog::hideEvent(event);
}

void DatAuditDialog::persistGeometry() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  settings.setValue(QStringLiteral("geometry"), saveGeometry());
  if (m_splitter != nullptr) {
    settings.setValue(QStringLiteral("splitter"), m_splitter->saveState());
  }
  settings.endGroup();
  // Kartend-o46gy: the browser page persists its own splitter/column/filter
  // layout under the same group.
  if (m_browserPage != nullptr) {
    m_browserPage->persistState();
  }
}

void DatAuditDialog::restoreGeometry_() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  const QByteArray geom = settings.value(QStringLiteral("geometry")).toByteArray();
  if (!geom.isEmpty()) {
    restoreGeometry(geom);
  }
  const QByteArray sp = settings.value(QStringLiteral("splitter")).toByteArray();
  if (!sp.isEmpty() && m_splitter != nullptr) {
    m_splitter->restoreState(sp);
  }
  settings.endGroup();
  // Kartend-o46gy: restore the browser page's saved layout alongside the
  // dialog's own. Safe before first show — QSplitter/QHeaderView::restoreState
  // stash the sizes and apply them when the page becomes visible.
  if (m_browserPage != nullptr) {
    m_browserPage->restoreState_();
  }
}

void DatAuditDialog::setLibraryPathAccessors(std::function<QString()> getter,
                                             std::function<void(const QString &)> setter) {
  // The download page needs the destination accessors for its download flow.
  m_downloadPage->setLibraryPathAccessors(getter, std::move(setter));
  m_getLibraryPath = std::move(getter);
  if (m_getLibraryPath) {
    const QString path = m_getLibraryPath();
    m_libraryPage->setLibraryPathText(path);
  }
  // "Check for updates" only makes sense once we know where the library is.
  m_libraryPage->setCheckUpdatesVisible(static_cast<bool>(m_getLibraryPath));
  // The Download page needs a library destination; hide it when there is no
  // way to know where to import packs.
  if (m_nav != nullptr && m_nav->count() >= 3) {
    m_nav->item(2)->setHidden(!static_cast<bool>(m_getLibraryPath));
  }
}

void DatAuditDialog::setQuarantineDefaultProvider(std::function<QString()> provider) {
  m_auditPage->setQuarantineDefaultProvider(std::move(provider));
}

void DatAuditDialog::setImportHandler(std::function<void(const QString &)> handler) {
  m_importPack = std::move(handler);
  m_libraryPage->setImportButtonsVisible(static_cast<bool>(m_importPack));
}

void DatAuditDialog::onImportZip() {
  if (!m_importPack) {
    return;
  }
  const QString zip =
      QFileDialog::getOpenFileName(this, tr("Import DAT zip"), QString(),
                                   tr("DAT archives (*.zip *.7z *.rar *.tar *.gz);;All files (*)"));
  if (!zip.isEmpty()) {
    m_importPack(zip);
  }
}

void DatAuditDialog::onImportFolder() {
  if (!m_importPack) {
    return;
  }
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Import DAT folder"));
  if (!dir.isEmpty()) {
    m_importPack(dir);
  }
}

DatAuditDialog::~DatAuditDialog() {
  // The audit page's DatAuditRunController and the download page's watchers each
  // cancel + wait on their own off-thread work in their destructors; the dialog
  // owns only the update-check watcher, torn down here.
  if (m_updateCancel) {
    m_updateCancel->store(true);
  }
  if (m_updateWatcher.isRunning()) {
    m_updateWatcher.waitForFinished();
  }
}

void DatAuditDialog::setCollections(QList<CollectionConfig> *collections) {
  m_collections = collections; // kept for the browser's collection-name grouping
  m_auditPage->setCollections(collections);
}

void DatAuditDialog::openForCollection(const QString &collectionUuid, const QString &collectionName,
                                       const QString &mediaDir, const QStringList &datPaths) {
  m_auditPage->openForCollection(collectionUuid, collectionName, mediaDir, datPaths);
}

void DatAuditDialog::setLibraryOpener(std::function<void()> opener) {
  m_libraryOpener = std::move(opener);
  // The page's reviewRequested signal is connected once in the ctor and invokes
  // m_libraryOpener when set; here we just reflect availability on the button.
  m_libraryPage->setReviewEnabled(static_cast<bool>(m_libraryOpener));
}

void DatAuditDialog::setScraperOpener(std::function<void(const QString &)> opener) {
  m_auditPage->setScraperOpener(std::move(opener));
}

void DatAuditDialog::onCheckUpdates() {
  if (m_updateWatcher.isRunning()) {
    return;
  }
  const QList<DatLibraryState::Provenance> all = m_downloadService->trackedProvenance();
  if (all.isEmpty()) {
    QMessageBox::information(
        this, tr("Check for updates"),
        tr("No downloaded catalogues to check yet. Only DATs fetched via the Download page "
           "(No-Intro / Redump) are tracked for updates."));
    return;
  }
  const auto answer = QMessageBox::question(
      this, tr("Check for updates"),
      tr("Check your %n downloaded catalogue(s) against their source and re-download any with a "
         "newer version? This may download data.",
         nullptr, static_cast<int>(all.size())));
  if (answer != QMessageBox::Yes) {
    return;
  }
  const QString lib = m_getLibraryPath ? m_getLibraryPath().trimmed() : QString();
  m_updateCancel = std::make_shared<std::atomic<bool>>(false);
  auto cancel = m_updateCancel;
  m_libraryPage->setCheckUpdatesBusy(true);
  m_updateWatcher.setFuture(m_downloadService->checkUpdates(all, lib, cancel));
}

void DatAuditDialog::onCheckUpdatesFinished() {
  m_libraryPage->setCheckUpdatesBusy(false);
  const auto r = m_updateWatcher.result();
  int updated = 0;
  for (const auto &d : r.downloads) {
    if (d.ok) {
      m_downloadService->recordProvenance(d); // refresh stored version to the new revision
      ++updated;
    }
  }
  if (r.downloads.isEmpty()) {
    QMessageBox::information(
        this, tr("Check for updates"),
        tr("All %n downloaded catalogue(s) are up to date.", nullptr, r.checked));
    return;
  }
  const int failed = static_cast<int>(r.downloads.size()) - updated;
  QString msg = tr("Updated %n catalogue(s) to the latest version.", nullptr, updated);
  if (failed > 0) {
    msg += QLatin1Char(' ') + tr("%n could not be re-downloaded.", nullptr, failed);
  }
  QMessageBox::information(this, tr("Check for updates"), msg);
  if (m_libraryOpener) {
    m_libraryOpener(); // surface the refreshed catalogues
  }
}
