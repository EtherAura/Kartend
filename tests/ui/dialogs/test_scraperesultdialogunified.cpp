// ScrapeResultDialogUnified runtime behaviour, driven through the public
// ScrapeResultDialog surface against a real ScraperService wired with
// scripted stub providers (no DB, no network):
//   • Scrape-click queue hand-off — pre-checked selection → one
//     ScraperService::CollectionJob per owning collection with the uuid /
//     artwork dir re-resolved from the live CollectionConfig, plus the
//     media-filter / write-metadata derivation from the checkbox panel.
//   • Service-driven interactive picking — pickerNeeded populates the
//     candidate combo (subtitle / match-score label format) and the live
//     metadata panel via the SingleItemScrapeView detail hop.
//   • The Apply hop — applyResult persistence callback (collection index +
//     item path + delivered item), textual-field stripping when the
//     "_metadata" checkbox is off, queue advance through to the
//     unifiedScrapeFinished signal and the Setup-view restore.
//   • The error-details dialog and its re-scrape-failed re-queue: owner
//     grouping across collections in first-seen order, out-of-range owners
//     dropped, not-found items never re-queued. The modal exec() is answered
//     by a zero-interval driver (same pattern as test_launcherchooserdialog);
//     the rebuilt queue is asserted through the pending-scrape.json snapshot
//     ScraperService::startScrape writes synchronously.
//   • The quota-exhausted finish message on the current-status label.
//
// Not driven here: Auto mode (needs a DatabaseManager-backed
// BatchScrapeRunner — covered by the runner's own suites), the recent-media
// thumbnail strip (async image decode of real files), and the
// itemBegan/itemCompleted label refreshes (both bail while the dialog is
// hidden, and dialog suites follow the never-shown offscreen convention —
// the live panel is instead covered via the visibility-independent
// detailLoaded hop).

#include <functional>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>

#include "../../support/testsandbox.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scraperesultdialog.h"
#include "scraperservice.h"

using Scraper::ScraperService;

namespace {

/// Scripted lookup provider: parks, errors, or answers with canned
/// candidates per the active mode; fetchDetail answers synchronously from
/// detailById. Error mode reports 404/not-found for queries containing
/// "notfound" and the configured HTTP status otherwise, so one run can mix
/// re-queueable errors with not-found outcomes.
class ScriptedProvider : public MetadataLookupProvider {
public:
  enum class LookupMode { Park, Candidates, Error };
  LookupMode lookupMode = LookupMode::Park;
  int errorHttpStatus = 500;
  QList<Scraper::ScrapeCandidate> candidates;
  QHash<QString, Scraper::ScrapedItem> detailById;
  mutable LookupCallback lastLookupCallback;

  QString id() const override { return QStringLiteral("scripted"); }
  QString displayName() const override { return QStringLiteral("Scripted"); }
  QStringList categories() const override { return {}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  void lookup(const QString &query, LookupCallback cb) override {
    if (lookupMode == LookupMode::Park) {
      lastLookupCallback = std::move(cb);
      return;
    }
    if (lookupMode == LookupMode::Error) {
      if (query.contains(QLatin1String("notfound"))) {
        cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::RemoteResourceNotFound,
                                           QStringLiteral("no match"))
               .withHttpStatus(404));
      } else {
        cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           QStringLiteral("boom"))
               .withHttpStatus(errorHttpStatus));
      }
      return;
    }
    cb(candidates);
  }
  void fetchDetail(const Scraper::ScrapeCandidate &c, DetailCallback cb) override {
    cb(detailById.value(c.providerSpecificId));
  }
  void fetchMediaBytes(const QUrl &, MediaCallback) override {}

  // Entity-scrape seam (Kartend-ckepd.2): declare Platform support so an entity
  // job dispatches here, and drive fetchEntity independently of lookup(). Park
  // mode stashes the callback (re-queued run stays in flight for inspection);
  // Error mode errors with a transient non-notFound status.
  enum class EntityMode { Park, Error, Success };
  EntityMode entityMode = EntityMode::Error;
  Scraper::EntityScrapeTarget lastEntityTarget;
  mutable DetailCallback lastEntityCallback;
  QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Platform};
  }
  void fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback cb) override {
    lastEntityTarget = target;
    if (entityMode == EntityMode::Park) {
      lastEntityCallback = std::move(cb);
      return;
    }
    if (entityMode == EntityMode::Error) {
      cb(ErrorUtils::ErrorContext::error(
             ErrorUtils::ErrorCode::UnknownError,
             QStringLiteral("ScreenScraper systems catalog unavailable; cannot resolve system %1")
                 .arg(target.identity))
             .withHttpStatus(503));
      return;
    }
    cb(Scraper::ScrapedItem{});
  }
};

/// Runs @p action on the next active modal dialog (repeating zero-interval
/// timer, so it survives the window between exec() entry and modal
/// activation). Same driver shape as test_launcherchooserdialog.
class ModalDriver : public QObject {
public:
  explicit ModalDriver(std::function<void(QDialog *)> action) : m_action(std::move(action)) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dlg) {
        return;
      }
      triggered = true;
      m_timer.stop();
      m_action(dlg);
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  std::function<void(QDialog *)> m_action;
  QTimer m_timer;
};

template <typename W> W *widgetWithText(const QWidget &root, const QString &text) {
  const auto widgets = root.findChildren<W *>();
  for (W *w : widgets) {
    if (w->text() == text) {
      return w;
    }
  }
  return nullptr;
}

QLabel *labelContaining(const QWidget &root, const QString &needle) {
  const auto labels = root.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->text().contains(needle)) {
      return l;
    }
  }
  return nullptr;
}

QLineEdit *lineEditWithText(const QWidget &root, const QString &text) {
  const auto edits = root.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->text() == text) {
      return e;
    }
  }
  return nullptr;
}

/// The synthetic "_metadata" gate checkbox ("Metadata (title, description,
/// …)") — matched by prefix so the trailing ellipsis glyph stays out of the
/// test source.
QCheckBox *metadataCheckbox(const QWidget &root) {
  const auto checks = root.findChildren<QCheckBox *>();
  for (QCheckBox *c : checks) {
    if (c->text().startsWith(QLatin1String("Metadata"))) {
      return c;
    }
  }
  return nullptr;
}

Scraper::ScrapeCandidate makeCandidate(const QString &name, const QString &subtitle,
                                       const QString &id, int score) {
  Scraper::ScrapeCandidate c;
  c.displayName = name;
  c.subtitle = subtitle;
  c.providerSpecificId = id;
  c.matchScore = score;
  return c;
}

/// Two collections ("Alpha", "Beta") with real (temp) media + artwork dirs
/// so the dialog's uuid / artwork-dir resolution has something to expand.
QList<CollectionConfig> makeCollections(const QString &base) {
  auto make = [&base](const QString &name) {
    CollectionConfig c;
    c.name = name;
    c.mediaDirectory = base + QLatin1Char('/') + name + QStringLiteral("/media");
    c.artworkDirectory = base + QLatin1Char('/') + name + QStringLiteral("/artwork");
    QDir().mkpath(c.mediaDirectory);
    QDir().mkpath(c.artworkDirectory);
    return c;
  };
  return {make(QStringLiteral("Alpha")), make(QStringLiteral("Beta"))};
}

ScraperService::CollectionJob makeJob(int index, const QString &name, const QStringList &items) {
  ScraperService::CollectionJob job;
  job.collectionIndex = index;
  job.collectionUuid = QStringLiteral("uuid-seed-%1").arg(name);
  job.collectionName = name;
  job.artworkDir = QStringLiteral("/art");
  job.items = items;
  return job;
}

/// A Platform entity job for the collection at @p index — no item list, so
/// isEntityJob() is true and the service dispatches it via fetchEntity().
ScraperService::CollectionJob makeEntityJob(int index, const QString &name,
                                            const QString &systemeid) {
  ScraperService::CollectionJob job;
  job.collectionIndex = index;
  job.collectionUuid = QStringLiteral("uuid-seed-%1").arg(name);
  job.collectionName = name;
  job.artworkDir = QStringLiteral("/art");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = systemeid;
  job.entity.collectionIndex = index;
  return job;
}

QString expectedUuid(const CollectionConfig &cfg) {
  const QString mediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  return CollectionUtils::computeCollectionUuid(cfg.name, mediaDir);
}

QString expectedArtworkDir(const CollectionConfig &cfg) {
  return PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
}

} // namespace

class TestScrapeResultDialogUnified : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void scrapeClickTranslatesSelectionIntoServiceQueue();
  void pickerNeededRendersCandidatesAndLiveMetadata();
  void interactiveApplyPersistsAdvancesAndStripsMetadata();
  void errorDetailsRescrapeRebuildsQueueGroupedByOwner();
  void errorDetailsRescrapeRebuildsEntityFailureAsEntityJob();
  void errorDetailsWithoutFailuresOmitsRescrapeButton();
  void quotaExhaustedFinishShowsResumeHint();

private:
  // Mirrors ScraperService's private pending-state paths (same approach as
  // test_scraperservice_resume) — the queue snapshot is the observation
  // point for what a scrape run was handed.
  static QString pendingFilePath();
  static QString pendingLockFilePath();
  static QJsonObject readPendingRoot();
};

QString TestScrapeResultDialogUnified::pendingFilePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(dir).filePath(QStringLiteral("pending-scrape.json"));
}

QString TestScrapeResultDialogUnified::pendingLockFilePath() {
  return pendingFilePath() + QStringLiteral(".lock");
}

QJsonObject TestScrapeResultDialogUnified::readPendingRoot() {
  QFile f(pendingFilePath());
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(f.readAll()).object();
}

void TestScrapeResultDialogUnified::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scraperesultdialogunified"));
}

void TestScrapeResultDialogUnified::init() {
  QFile::remove(pendingFilePath());
  QFile::remove(pendingLockFilePath());
}

void TestScrapeResultDialogUnified::cleanup() {
  QFile::remove(pendingFilePath());
  QFile::remove(pendingLockFilePath());
}

void TestScrapeResultDialogUnified::scrapeClickTranslatesSelectionIntoServiceQueue() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>(); // Park: the run stays live for inspection

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  // Right-click entry: pre-check collection 1 scoped to a single item.
  dlg.startUnifiedScrape(1, QStringLiteral("/m/beta-item.mp4"));

  // Interactive mode so no DB-backed BatchScrapeRunner is constructed.
  auto *interactive =
      widgetWithText<QRadioButton>(dlg, QStringLiteral("Interactive (pick candidate per item)"));
  QVERIFY(interactive);
  interactive->setChecked(true);
  // Tweak the media panel off its defaults: metadata off, screenshot on.
  QCheckBox *metadata = metadataCheckbox(dlg);
  QVERIFY(metadata);
  metadata->setChecked(false);
  auto *screenshot = widgetWithText<QCheckBox>(dlg, QStringLiteral("Screenshot"));
  QVERIFY(screenshot);
  screenshot->setChecked(true);

  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QVERIFY(tree->isVisibleTo(&dlg)); // setup view active before the click

  auto *scrapeButton = widgetWithText<QPushButton>(dlg, QStringLiteral("Scrape"));
  QVERIFY(scrapeButton);
  scrapeButton->click();

  // The service took the queue and parked on the item's lookup.
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(service.totalItems(), 1);
  QCOMPARE(service.currentCollectionName(), collections[1].name);
  QCOMPARE(service.currentItemPath(), QStringLiteral("/m/beta-item.mp4"));
  QCOMPARE(service.mediaFilter(),
           (QSet<QString>{QStringLiteral("front"), QStringLiteral("screenshot")}));
  QVERIFY(!service.writeMetadata());

  // startScrape's synchronous snapshot carries the translated job: uuid and
  // artwork dir re-resolved from the live CollectionConfig, item preserved.
  const QJsonObject root = readPendingRoot();
  QCOMPARE(root.value(QStringLiteral("mode")).toString(), QStringLiteral("interactive"));
  const QJsonArray queue = root.value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 1);
  const QJsonObject job = queue.first().toObject();
  QCOMPARE(job.value(QStringLiteral("collection_index")).toInt(), 1);
  QCOMPARE(job.value(QStringLiteral("collection_name")).toString(), QStringLiteral("Beta"));
  QCOMPARE(job.value(QStringLiteral("collection_uuid")).toString(), expectedUuid(collections[1]));
  QCOMPARE(job.value(QStringLiteral("artwork_dir")).toString(), expectedArtworkDir(collections[1]));
  const QJsonArray remaining = job.value(QStringLiteral("remaining")).toArray();
  QCOMPARE(remaining.size(), 1);
  QCOMPARE(remaining.first().toString(), QStringLiteral("/m/beta-item.mp4"));

  // Setup surface swapped for the live view: tree hidden, progress labels
  // showing service-sourced counters, Close button revealed.
  QVERIFY(!tree->isVisibleTo(&dlg));
  QLabel *timing = labelContaining(dlg, QStringLiteral("Items 0/1"));
  QVERIFY(timing);
  QVERIFY(timing->isVisibleTo(&dlg));
  QLabel *counts = labelContaining(dlg, QStringLiteral("Scraped 0 items"));
  QVERIFY(counts);
  QVERIFY(counts->isVisibleTo(&dlg));
  auto *closeButton = widgetWithText<QPushButton>(dlg, QStringLiteral("Close"));
  QVERIFY(closeButton);
  QVERIFY(closeButton->isVisibleTo(&dlg));

  service.cancel(); // unpark cleanly before teardown
}

void TestScrapeResultDialogUnified::pickerNeededRendersCandidatesAndLiveMetadata() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>();
  provider->lookupMode = ScriptedProvider::LookupMode::Candidates;
  provider->candidates = {
      makeCandidate(QStringLiteral("Alpha Game"), QStringLiteral("1997"), QStringLiteral("id-a"),
                    95),
      makeCandidate(QStringLiteral("Beta Game"), QString(), QStringLiteral("id-b"), -1)};
  Scraper::ScrapedItem detail;
  detail.title = QStringLiteral("Alpha Detail");
  detail.publisher = QStringLiteral("PubCo");
  detail.genre = QStringLiteral("RPG");
  detail.description = QStringLiteral("A long synthetic description.");
  detail.sourceProviderId = QStringLiteral("scripted");
  detail.customFields.insert(QStringLiteral("myextra"), QStringLiteral("42x"));
  provider->detailById.insert(QStringLiteral("id-a"), detail);

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  dlg.startUnifiedScrape();

  // The synchronous stub answers the lookup during startScrape, so the whole
  // pickerNeeded → candidate combo → detail fetch → live-panel hop settles
  // before it returns.
  service.startScrape({makeJob(0, collections[0].name, {QStringLiteral("/m/one.bin")})},
                      ScraperService::Mode::Interactive, {}, /*writeMetadata=*/true);

  auto *combo = dlg.findChild<QComboBox *>();
  QVERIFY(combo);
  QCOMPARE(combo->count(), 2);
  // Label format: displayName, then " — subtitle" when present, then the
  // match score in parentheses when non-negative.
  QCOMPARE(combo->itemText(0), QStringLiteral("Alpha Game — 1997  (95)"));
  QCOMPARE(combo->itemText(1), QStringLiteral("Beta Game"));
  QCOMPARE(combo->currentIndex(), 0);
  QVERIFY(combo->parentWidget());
  QVERIFY(combo->parentWidget()->isVisibleTo(&dlg)); // candidate row revealed

  // Apply shown + enabled once row 0's detail landed; Scrape hidden mid-run.
  auto *apply = widgetWithText<QPushButton>(dlg, QStringLiteral("Apply"));
  QVERIFY(apply);
  QVERIFY(apply->isVisibleTo(&dlg));
  QVERIFY(apply->isEnabled());
  auto *scrapeButton = widgetWithText<QPushButton>(dlg, QStringLiteral("Scrape"));
  QVERIFY(scrapeButton);
  QVERIFY(!scrapeButton->isVisibleTo(&dlg));

  // Live metadata panel rendered the fetched detail (visibility-independent
  // detailLoaded hop): typed fields, description, and a custom-field chip
  // for the previously-unseen key.
  QVERIFY(lineEditWithText(dlg, QStringLiteral("Alpha Detail")));
  QVERIFY(lineEditWithText(dlg, QStringLiteral("PubCo")));
  QVERIFY(lineEditWithText(dlg, QStringLiteral("RPG")));
  QVERIFY(lineEditWithText(dlg, QStringLiteral("scripted")));
  bool descriptionShown = false;
  const auto browsers = dlg.findChildren<QTextBrowser *>();
  for (QTextBrowser *b : browsers) {
    if (b->toPlainText() == QStringLiteral("A long synthetic description.")) {
      descriptionShown = true;
    }
  }
  QVERIFY(descriptionShown);
  QLineEdit *extra = lineEditWithText(dlg, QStringLiteral("42x"));
  QVERIFY(extra);
  QCOMPARE(extra->toolTip(), QStringLiteral("42x"));
  QLabel *extraLabel = widgetWithText<QLabel>(dlg, QStringLiteral("myextra:"));
  QVERIFY(extraLabel);
  QCOMPARE(extraLabel->toolTip(), QStringLiteral("myextra"));

  service.cancel();
}

void TestScrapeResultDialogUnified::interactiveApplyPersistsAdvancesAndStripsMetadata() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>();
  provider->lookupMode = ScriptedProvider::LookupMode::Candidates;
  provider->candidates = {
      makeCandidate(QStringLiteral("Only"), QString(), QStringLiteral("id-a"), -1)};
  Scraper::ScrapedItem detail;
  detail.title = QStringLiteral("Alpha Detail");
  detail.genre = QStringLiteral("RPG");
  detail.sourceProviderId = QStringLiteral("scripted");
  detail.customFields.insert(QStringLiteral("myextra"), QStringLiteral("42x"));
  provider->detailById.insert(QStringLiteral("id-a"), detail);

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  struct Applied {
    int index = -1;
    QString path;
    ScrapeResultDialog::Result result;
  };
  QList<Applied> applied;

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dctx.applyResult = [&applied](int index, const QString &path,
                                const ScrapeResultDialog::Result &result) {
    applied.append({index, path, result});
  };
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  dlg.startUnifiedScrape();
  QSignalSpy finishedSpy(&dlg, &ScrapeResultDialog::unifiedScrapeFinished);

  service.startScrape(
      {makeJob(1, collections[1].name, {QStringLiteral("/m/a1.bin"), QStringLiteral("/m/a2.bin")})},
      ScraperService::Mode::Interactive, {}, /*writeMetadata=*/true);

  // Item 1: detail landed, Apply enabled. The detail carries no media and
  // the setup filter matches nothing on it, so Apply skips the download
  // dispatcher and hops straight into finishCurrentApply.
  auto *apply = widgetWithText<QPushButton>(dlg, QStringLiteral("Apply"));
  QVERIFY(apply);
  QVERIFY(apply->isEnabled());
  apply->click();

  QCOMPARE(applied.size(), 1);
  QCOMPARE(applied[0].index, 1); // service-reported owning collection index
  QCOMPARE(applied[0].path, QStringLiteral("/m/a1.bin"));
  QCOMPARE(applied[0].result.item.title, QStringLiteral("Alpha Detail"));
  QCOMPARE(applied[0].result.item.customFields.value(QStringLiteral("myextra")),
           QStringLiteral("42x"));
  QCOMPARE(service.summary().scraped, 1);

  // The service advanced to item 2 and re-armed the picker synchronously.
  QVERIFY(apply->isVisibleTo(&dlg));
  QVERIFY(apply->isEnabled());

  // Unchecking the "_metadata" gate strips every textual field from the
  // delivered item so apply preserves the DB's existing text.
  QCheckBox *metadata = metadataCheckbox(dlg);
  QVERIFY(metadata);
  metadata->setChecked(false);
  apply->click();

  QCOMPARE(applied.size(), 2);
  QCOMPARE(applied[1].path, QStringLiteral("/m/a2.bin"));
  QVERIFY(applied[1].result.item.title.isEmpty());
  QVERIFY(applied[1].result.item.genre.isEmpty());
  QVERIFY(applied[1].result.item.sourceProviderId.isEmpty());
  QVERIFY(applied[1].result.item.customFields.isEmpty());

  // Queue exhausted: run finished, dialog back on the setup surface.
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QCOMPARE(service.summary().scraped, 2);
  QCOMPARE(finishedSpy.count(), 1);
  QCOMPARE(finishedSpy.first().at(0).toInt(), 2); // totalScraped
  QCOMPARE(finishedSpy.first().at(2).toInt(), 0); // totalErrors
  auto *scrapeButton = widgetWithText<QPushButton>(dlg, QStringLiteral("Scrape"));
  QVERIFY(scrapeButton);
  QVERIFY(scrapeButton->isVisibleTo(&dlg));
  QVERIFY(!apply->isVisibleTo(&dlg));
}

void TestScrapeResultDialogUnified::errorDetailsRescrapeRebuildsQueueGroupedByOwner() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>();
  provider->lookupMode = ScriptedProvider::LookupMode::Error; // plain HTTP 500 per lookup

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  dlg.startUnifiedScrape();

  // Seed a run whose failures interleave two owners, plus one owner index
  // outside the live collection list and one not-found item. The erroring
  // stub answers synchronously, so the run completes inside startScrape.
  service.startScrape({makeJob(0, collections[0].name, {QStringLiteral("/m/a1.bin")}),
                       makeJob(1, collections[1].name, {QStringLiteral("/m/b1.bin")}),
                       makeJob(0, collections[0].name, {QStringLiteral("/m/a2.bin")}),
                       makeJob(7, QStringLiteral("Ghost"), {QStringLiteral("/m/z1.bin")}),
                       makeJob(0, collections[0].name, {QStringLiteral("/m/nf-notfound.bin")})},
                      ScraperService::Mode::Interactive, {}, /*writeMetadata=*/true);
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QCOMPARE(service.summary().errors, 4);
  QCOMPARE(service.summary().notFound, 1);
  QCOMPARE(service.summary().failedItems.size(), 4);

  // Post-run reachability: returning to the Setup view hides the live
  // progress widgets, but a run that recorded errors must keep the counts
  // label visible — its "Errors N" link is the only entry point to the
  // failure list and the re-queue affordance.
  QLabel *countsAfterRun = labelContaining(dlg, QStringLiteral("Errors"));
  QVERIFY(countsAfterRun);
  QVERIFY2(countsAfterRun->isVisibleTo(&dlg),
           "the error-link counts label must stay reachable after the run ends");

  // Park the re-queued run so its queue can be inspected mid-flight.
  provider->lookupMode = ScriptedProvider::LookupMode::Park;
  auto *interactive =
      widgetWithText<QRadioButton>(dlg, QStringLiteral("Interactive (pick candidate per item)"));
  QVERIFY(interactive);
  interactive->setChecked(true);

  bool sawRescrapeButton = false;
  int errorRowCount = -1;
  QString firstErrorRow;
  ModalDriver driver([&](QDialog *errDlg) {
    auto *list = errDlg->findChild<QListWidget *>();
    if (list) {
      errorRowCount = list->count();
      if (list->count() > 0) firstErrorRow = list->item(0)->text();
    }
    QPushButton *rescrape =
        widgetWithText<QPushButton>(*errDlg, QStringLiteral("Re-scrape failed items"));
    sawRescrapeButton = rescrape != nullptr;
    if (rescrape) {
      rescrape->click(); // accepts the modal and triggers the re-queue
    } else {
      errDlg->reject();
    }
  });
  QVERIFY(QMetaObject::invokeMethod(&dlg, "showScrapeErrorDetails"));
  QVERIFY(driver.triggered);
  QVERIFY(sawRescrapeButton);
  QCOMPARE(errorRowCount, 4); // errors listed; the not-found item is absent
  QVERIFY(firstErrorRow.contains(QStringLiteral("a1.bin")));

  // The re-queue: one job per owning collection in first-seen order, the
  // out-of-range owner dropped, the not-found item never re-queued.
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(service.totalItems(), 3);
  QCOMPARE(service.currentCollectionName(), collections[0].name);
  QCOMPARE(service.currentItemPath(), QStringLiteral("/m/a1.bin"));
  // Media/metadata selections reused from the (default) setup controls.
  QCOMPARE(service.mediaFilter(), (QSet<QString>{QStringLiteral("front")}));
  QVERIFY(service.writeMetadata());

  const QJsonArray queue = readPendingRoot().value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 2);
  const QJsonObject alphaJob = queue.at(0).toObject();
  QCOMPARE(alphaJob.value(QStringLiteral("collection_index")).toInt(), 0);
  QCOMPARE(alphaJob.value(QStringLiteral("collection_name")).toString(), collections[0].name);
  QCOMPARE(alphaJob.value(QStringLiteral("collection_uuid")).toString(),
           expectedUuid(collections[0]));
  QCOMPARE(alphaJob.value(QStringLiteral("artwork_dir")).toString(),
           expectedArtworkDir(collections[0]));
  const QJsonArray alphaRemaining = alphaJob.value(QStringLiteral("remaining")).toArray();
  QCOMPARE(alphaRemaining.size(), 2);
  QCOMPARE(alphaRemaining.at(0).toString(), QStringLiteral("/m/a1.bin"));
  QCOMPARE(alphaRemaining.at(1).toString(), QStringLiteral("/m/a2.bin"));
  const QJsonObject betaJob = queue.at(1).toObject();
  QCOMPARE(betaJob.value(QStringLiteral("collection_index")).toInt(), 1);
  QCOMPARE(betaJob.value(QStringLiteral("collection_uuid")).toString(),
           expectedUuid(collections[1]));
  const QJsonArray betaRemaining = betaJob.value(QStringLiteral("remaining")).toArray();
  QCOMPARE(betaRemaining.size(), 1);
  QCOMPARE(betaRemaining.first().toString(), QStringLiteral("/m/b1.bin"));

  service.cancel();
}

void TestScrapeResultDialogUnified::errorDetailsRescrapeRebuildsEntityFailureAsEntityJob() {
  // Fix A: an entity failure must re-queue AS an entity job (dispatched via
  // fetchEntity with the original target), NOT as a bogus game lookup of the
  // systemeid stuffed into a game item list. Regression: rescrapeFailedItems
  // used to rebuild every failedItem into CollectionJob.items, dropping the
  // entity target entirely.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>();
  provider->entityMode = ScriptedProvider::EntityMode::Error; // catalog-unavailable style 503

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  dlg.startUnifiedScrape();

  // A single Platform entity job for Alpha that errors synchronously.
  service.startScrape({makeEntityJob(0, collections[0].name, QStringLiteral("42"))},
                      ScraperService::Mode::Interactive, {}, /*writeMetadata=*/true);
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QCOMPARE(service.summary().errors, 1);
  QCOMPARE(service.summary().failedItems.size(), 1);
  QVERIFY(service.summary().failedItems.first().isEntity);

  // Park the re-queued run so the rebuilt entity queue can be inspected.
  provider->entityMode = ScriptedProvider::EntityMode::Park;
  auto *interactive =
      widgetWithText<QRadioButton>(dlg, QStringLiteral("Interactive (pick candidate per item)"));
  QVERIFY(interactive);
  interactive->setChecked(true);

  bool sawRescrapeButton = false;
  ModalDriver driver([&](QDialog *errDlg) {
    QPushButton *rescrape =
        widgetWithText<QPushButton>(*errDlg, QStringLiteral("Re-scrape failed items"));
    sawRescrapeButton = rescrape != nullptr;
    if (rescrape) {
      rescrape->click();
    } else {
      errDlg->reject();
    }
  });
  QVERIFY(QMetaObject::invokeMethod(&dlg, "showScrapeErrorDetails"));
  QVERIFY(driver.triggered);
  QVERIFY(sawRescrapeButton);

  // The rebuilt job is an ENTITY job: the provider's fetchEntity ran again with
  // the original systemeid, and the persisted queue entry carries an entity
  // descriptor with an empty remaining list — never a game lookup of "42".
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(provider->lastEntityTarget.type, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(provider->lastEntityTarget.identity, QStringLiteral("42"));

  const QJsonArray queue = readPendingRoot().value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 1);
  const QJsonObject entityJob = queue.at(0).toObject();
  QCOMPARE(entityJob.value(QStringLiteral("collection_index")).toInt(), 0);
  QVERIFY(entityJob.contains(QStringLiteral("entity")));
  const QJsonObject entityObj = entityJob.value(QStringLiteral("entity")).toObject();
  QCOMPARE(entityObj.value(QStringLiteral("identity")).toString(), QStringLiteral("42"));
  QCOMPARE(entityObj.value(QStringLiteral("type")).toInt(),
           static_cast<int>(Scraper::ScrapeEntityType::Platform));
  // Not stuffed into a game item list.
  QVERIFY(entityJob.value(QStringLiteral("remaining")).toArray().isEmpty());

  service.cancel();
  // Break the provider<->parked-callback cycle: the parked entity callback
  // captures the provider shared_ptr by value, and the stub parked it into the
  // provider, so the provider transitively owns itself and never frees (LSan
  // flags the closure's captures). Clear the parked member before the local
  // provider shared_ptr drops.
  provider->lastEntityCallback = {};
}

void TestScrapeResultDialogUnified::errorDetailsWithoutFailuresOmitsRescrapeButton() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>();
  provider->lookupMode = ScriptedProvider::LookupMode::Error; // 404s only — see item names

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  dlg.startUnifiedScrape();

  // Only not-found outcomes: they never enter failedItems, so the dialog
  // must not offer a doomed retry against the same provider.
  service.startScrape(
      {makeJob(0, collections[0].name,
               {QStringLiteral("/m/one-notfound.bin"), QStringLiteral("/m/two-notfound.bin")})},
      ScraperService::Mode::Interactive, {}, /*writeMetadata=*/true);
  QCOMPARE(service.summary().notFound, 2);
  QCOMPARE(service.summary().errors, 0);
  QVERIFY(service.summary().failedItems.isEmpty());

  bool sawRescrapeButton = false;
  bool sawNoDetailNote = false;
  ModalDriver driver([&](QDialog *errDlg) {
    sawRescrapeButton =
        widgetWithText<QPushButton>(*errDlg, QStringLiteral("Re-scrape failed items")) != nullptr;
    sawNoDetailNote =
        labelContaining(*errDlg, QStringLiteral("No further error detail")) != nullptr;
    errDlg->reject();
  });
  QVERIFY(QMetaObject::invokeMethod(&dlg, "showScrapeErrorDetails"));
  QVERIFY(driver.triggered);
  QVERIFY(!sawRescrapeButton);
  QVERIFY(sawNoDetailNote); // zero errors recorded → no failure list either
  QCOMPARE(service.state(), ScraperService::State::Idle);
}

void TestScrapeResultDialogUnified::quotaExhaustedFinishShowsResumeHint() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = makeCollections(tmp.path());
  auto provider = std::make_shared<ScriptedProvider>();
  provider->lookupMode = ScriptedProvider::LookupMode::Error;
  provider->errorHttpStatus = 430; // provider-classified quota exhaustion

  ScraperService service;
  ScraperService::Context sctx;
  sctx.collections = &collections;
  sctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(sctx);

  ScrapeResultDialog dlg(nullptr, {});
  ScrapeResultDialog::ScraperContext dctx;
  dctx.collections = &collections;
  dctx.providerBuilder = sctx.providerBuilder;
  dlg.setScraperContext(dctx);
  dlg.setScraperService(&service);
  dlg.startUnifiedScrape();
  QSignalSpy finishedSpy(&dlg, &ScrapeResultDialog::unifiedScrapeFinished);

  service.startScrape(
      {makeJob(0, collections[0].name, {QStringLiteral("/m/a1.bin"), QStringLiteral("/m/a2.bin")})},
      ScraperService::Mode::Interactive, {}, /*writeMetadata=*/true);

  // The quota error stopped the queue with a resume point; the dialog is
  // back on the setup surface but keeps the current-status label visible to
  // explain why the run ended early. No live quota update arrived, so the
  // generic (reset-time-free) wording is used.
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(finishedSpy.count(), 1);
  QLabel *message = labelContaining(dlg, QStringLiteral("request quota is exhausted"));
  QVERIFY(message);
  QVERIFY(message->isVisibleTo(&dlg));
  QVERIFY(message->text().contains(QStringLiteral("Resume after the quota resets.")));
  auto *scrapeButton = widgetWithText<QPushButton>(dlg, QStringLiteral("Scrape"));
  QVERIFY(scrapeButton);
  QVERIFY(scrapeButton->isVisibleTo(&dlg));
  QVERIFY(scrapeButton->isEnabled());
}

QTEST_MAIN(TestScrapeResultDialogUnified)
#include "test_scraperesultdialogunified.moc"
