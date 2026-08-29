#include "test_scrapercontroller.h"

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "dialogcontroller.h"
#include "metadatalookupprovider.h"
#include "mocks/mockdatabasemanager.h"
#include "scrapependingstate.h"
#include "scrapercontroller.h"
#include "scraperesultdialog.h"

#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMessageBox>
#include <QString>
#include <QTest>
#include <QTimer>
#include <QWidget>

// These cases read modal-dialog / window-visibility state synchronously right
// after show()/close()/open(), which relies on the Linux offscreen QPA plugin
// delivering show/hide/modal events inline. macOS defers that delivery (and
// returns empty QMessageBox window titles), so the assertions race the event
// loop there. Skip them on macOS; the same behaviours stay covered on Linux
// (offscreen) and Windows. Mirrors SKIP_LOCAL_TLS_SERVER_ON_MACOS in
// test_httpclient.cpp.
#if defined(Q_OS_MACOS)
#define SKIP_MODAL_WIDGET_ON_MACOS()                                                               \
  QSKIP("modal-dialog/window show-hide events aren't delivered synchronously under the macOS "     \
        "offscreen QPA plugin; covered on Linux + Windows")
#else
#define SKIP_MODAL_WIDGET_ON_MACOS() ((void)0)
#endif

namespace {

/// One controller + the borrowed state its context closures serve. The
/// closure shapes mirror MainWindow's wiring (mainwindow_wiring.cpp) minus
/// the managers the tested paths never touch (navigation / details pane /
/// interaction / scroll are only read by the scrape-finished housekeeping
/// lambda, which no test drives).
struct ControllerHarness {
  QWidget host;
  QList<CollectionConfig> collections;
  GeneralSettings settings;
  KartendTest::MockDatabaseManager db{nullptr};
  ScraperController controller;

  explicit ControllerHarness(bool withDatabase = true) {
    ScraperControllerContext ctx;
    ctx.getParentWindow = [this]() -> QWidget * { return &host; };
    ctx.getCollections = [this]() { return &collections; };
    ctx.getGeneralSettings = [this]() { return &settings; };
    ctx.getCurrentCollectionIndex = []() { return -1; };
    if (withDatabase) {
      ctx.getDatabaseManager = [this]() -> IDatabaseManager * { return &db; };
    }
    controller.setContext(ctx);
  }
};

/// A minimal valid pending-scrape snapshot. The collection UUID deliberately
/// resolves to no live collection, so a Resume re-resolves the job, drops
/// it, and finishes immediately — deterministic, no provider construction,
/// no network (mirrors ScraperService's resume-drop semantics pinned by
/// tests/modules/scraper/test_scraperservice_resume.cpp).
QByteArray pendingSnapshotJson() {
  return R"json({
    "version": 1,
    "started_at_unix_ms": 1715000000000,
    "mode": "auto",
    "write_metadata": true,
    "summary_so_far": { "scraped": 2, "skipped": 1, "errors": 0, "not_found": 3 },
    "queue": [
      {
        "collection_index": 0,
        "collection_uuid": "uuid-gone",
        "collection_name": "Films",
        "artwork_dir": "/art",
        "remaining": ["/media/a.mkv"]
      }
    ]
  })json";
}

void writePendingSnapshot() {
  const QString path = Scraper::ScrapePendingState::filePath();
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(pendingSnapshotJson());
  f.close();
}

/// The controller's resume prompt is a QMessageBox opened non-modally
/// (box->open()) and parented to the context's parent window.
QMessageBox *findResumePrompt(QWidget &host) {
  const auto boxes = host.findChildren<QMessageBox *>();
  for (QMessageBox *box : boxes) {
    if (box->windowTitle() == QStringLiteral("Resume scrape?")) return box;
  }
  return nullptr;
}

QAbstractButton *buttonWithText(QMessageBox *box, const QString &text) {
  const auto buttons = box->buttons();
  for (QAbstractButton *b : buttons) {
    if (b->text() == text) return b;
  }
  return nullptr;
}

} // namespace

void TestScraperController::init() {
  // Each test starts without a pending scrape on disk — the integration
  // binary's sandbox HOME persists across this suite's methods.
  QFile::remove(Scraper::ScrapePendingState::filePath());
  QFile::remove(Scraper::ScrapePendingState::lockFilePath());
}

void TestScraperController::cleanup() {
  QFile::remove(Scraper::ScrapePendingState::filePath());
  QFile::remove(Scraper::ScrapePendingState::lockFilePath());
}

void TestScraperController::providerBuilder_resolvesProviderPerCollectionCategory() {
  // The builder must claim a DIFFERENT provider per collection index based
  // on each collection's type — including synonym normalisation ("music" →
  // audio, "books" → reference). This is the registry claim path the
  // controller wires into both the dialog and service contexts.
  QList<CollectionConfig> collections;
  GeneralSettings settings;

  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.type = QStringLiteral("video");
  CollectionConfig albums;
  albums.name = QStringLiteral("Albums");
  albums.type = QStringLiteral("music");
  CollectionConfig manuals;
  manuals.name = QStringLiteral("Manuals");
  manuals.type = QStringLiteral("books");
  collections << films << albums << manuals;

  const auto builder = ScraperControllerInternal::makeProviderBuilder(&collections, &settings);
  QVERIFY(builder);

  const auto video = builder(0);
  QVERIFY2(video, "video collection must resolve a lookup provider");
  QCOMPARE(video->id(), QStringLiteral("tmdb"));

  const auto audio = builder(1);
  QVERIFY2(audio, "audio-synonym collection must resolve a lookup provider");
  QCOMPARE(audio->id(), QStringLiteral("musicbrainz"));

  const auto reference = builder(2);
  QVERIFY2(reference, "reference-synonym collection must resolve a lookup provider");
  QCOMPARE(reference->id(), QStringLiteral("openlibrary"));

  // Fresh per call — the provider's collection accessor closes over the
  // index, so the builder must construct a new instance every time rather
  // than caching one across collections.
  QVERIFY(builder(0).get() != video.get());
}

void TestScraperController::providerBuilder_overrideIdWinsAndNonLookupOverrideFallsBack() {
  QList<CollectionConfig> collections;
  GeneralSettings settings;

  // An explicit lookup-capable override beats the category match…
  CollectionConfig pinned;
  pinned.name = QStringLiteral("Films");
  pinned.type = QStringLiteral("video");
  pinned.scraperOverrides.scraperProviderId = QStringLiteral("musicbrainz");
  // …while a URL-only override (no MetadataLookup capability) falls through
  // to the collection type's first lookup-capable provider.
  CollectionConfig urlOnly;
  urlOnly.name = QStringLiteral("Shows");
  urlOnly.type = QStringLiteral("video");
  urlOnly.scraperOverrides.scraperProviderId = QStringLiteral("imdb");
  collections << pinned << urlOnly;

  const auto builder = ScraperControllerInternal::makeProviderBuilder(&collections, &settings);

  const auto overridden = builder(0);
  QVERIFY(overridden);
  QCOMPARE(overridden->id(), QStringLiteral("musicbrainz"));

  const auto fallback = builder(1);
  QVERIFY(fallback);
  QCOMPARE(fallback->id(), QStringLiteral("tmdb"));
}

void TestScraperController::providerBuilder_invalidIndexOrNullListReturnsNull() {
  QList<CollectionConfig> collections;
  GeneralSettings settings;
  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.type = QStringLiteral("video");
  collections << films;

  const auto builder = ScraperControllerInternal::makeProviderBuilder(&collections, &settings);
  QVERIFY(!builder(-1));
  QVERIFY(!builder(collections.size()));

  // A null collection list must be inert, not a crash — the closures are
  // wired before MainWindow's state is guaranteed to exist.
  const auto nullListBuilder = ScraperControllerInternal::makeProviderBuilder(nullptr, &settings);
  QVERIFY(!nullListBuilder(0));
}

void TestScraperController::openScraperDialog_withoutDatabaseWarnsAndCreatesNoDialog() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h(/*withDatabase=*/false);

  // The guard branch pops a MODAL QMessageBox::warning (a nested exec loop),
  // so a zero-interval timer running inside that loop captures + closes it.
  bool sawWarning = false;
  QString warningTitle;
  auto *dismisser = new QTimer(&h.host);
  dismisser->setInterval(0);
  QObject::connect(dismisser, &QTimer::timeout, &h.host, [&]() {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (!box) return;
    sawWarning = true;
    warningTitle = box->windowTitle();
    box->close();
    dismisser->stop();
  });
  dismisser->start();

  h.controller.openScraperDialog();

  dismisser->stop();
  QVERIFY2(sawWarning, "missing database must surface the 'Database is not ready' warning");
  QCOMPARE(warningTitle, QStringLiteral("Scraper"));
  QVERIFY2(h.host.findChildren<ScrapeResultDialog *>().isEmpty(),
           "the scraper dialog must not be constructed without a database");
}

void TestScraperController::openScraperDialog_createsThenReusesOneDialog() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h;

  h.controller.openScraperDialog();
  auto dialogs = h.host.findChildren<ScrapeResultDialog *>();
  QCOMPARE(dialogs.size(), 1);
  ScrapeResultDialog *dialog = dialogs.first();
  QVERIFY(dialog->isVisible());
  QVERIFY(ScrapeResultDialog::isAnyInstanceVisible());
  // DialogController's static wrapper reads the same visibility tracking.
  QVERIFY(DialogController::anyScrapeResultDialogVisible());

  // Second open rebinds + raises the SAME cached dialog — the closeEvent
  // hide-not-destroy contract depends on the instance surviving re-opens.
  h.controller.openScraperDialog();
  dialogs = h.host.findChildren<ScrapeResultDialog *>();
  QCOMPARE(dialogs.size(), 1);
  QCOMPARE(dialogs.first(), dialog);

  // Unified-mode close hides (background scrape survives) instead of
  // destroying; visibility tracking must follow.
  dialog->close();
  QVERIFY(!dialog->isVisible());
  QVERIFY(!DialogController::anyScrapeResultDialogVisible());
  QCOMPARE(h.host.findChildren<ScrapeResultDialog *>().size(), 1);
}

void TestScraperController::openScraperDialog_recreatesAfterExternalDestruction() {
  ControllerHarness h;

  h.controller.openScraperDialog();
  auto dialogs = h.host.findChildren<ScrapeResultDialog *>();
  QCOMPARE(dialogs.size(), 1);
  // Hide before destruction — destroying a still-visible dialog skips
  // hideEvent, which would unbalance the class-wide visible-instance
  // refcount for later tests.
  dialogs.first()->hide();

  // Premature destruction outside the controller (parent teardown order, a
  // future WA_DeleteOnClose): the QPointer cache must null itself so the
  // next open constructs a fresh dialog instead of reusing a dangling
  // pointer (use-after-free before the QPointer guard).
  delete dialogs.first();
  QVERIFY(h.host.findChildren<ScrapeResultDialog *>().isEmpty());

  h.controller.openScraperDialog();
  dialogs = h.host.findChildren<ScrapeResultDialog *>();
  QCOMPARE(dialogs.size(), 1);
  QVERIFY(dialogs.first()->isVisible());
  dialogs.first()->hide(); // keep the visible-instance refcount balanced
}

void TestScraperController::promptResume_noPendingStateShowsNothing() {
  ControllerHarness h;
  h.controller.promptResumePendingScrapeIfAny();
  QVERIFY(!findResumePrompt(h.host));
  QVERIFY(h.host.findChildren<ScrapeResultDialog *>().isEmpty());
}

void TestScraperController::promptResume_keepForLaterLeavesStateOnDisk() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h;
  writePendingSnapshot();

  h.controller.promptResumePendingScrapeIfAny();

  QMessageBox *box = findResumePrompt(h.host);
  QVERIFY2(box, "a valid pending snapshot must surface the resume prompt");
  QVERIFY(box->isVisible()); // box->open() — non-modal, no nested loop
  QVERIFY2(box->informativeText().contains(QStringLiteral("Remaining items: 1")),
           qPrintable(QStringLiteral("unexpected prompt body: %1").arg(box->informativeText())));

  QAbstractButton *keep = buttonWithText(box, QStringLiteral("Keep for later"));
  QVERIFY(keep);
  keep->click();

  // Keep = decline without discarding: the snapshot must survive for the
  // next launch.
  QVERIFY(QFile::exists(Scraper::ScrapePendingState::filePath()));
  QVERIFY(h.host.findChildren<ScrapeResultDialog *>().isEmpty());
}

void TestScraperController::promptResume_discardRemovesStateWithoutOpeningDialog() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h;
  writePendingSnapshot();

  h.controller.promptResumePendingScrapeIfAny();

  QMessageBox *box = findResumePrompt(h.host);
  QVERIFY(box);
  QAbstractButton *discard = buttonWithText(box, QStringLiteral("Discard"));
  QVERIFY(discard);
  discard->click();

  QVERIFY2(!QFile::exists(Scraper::ScrapePendingState::filePath()),
           "Discard must delete the pending snapshot");
  QVERIFY(h.host.findChildren<ScrapeResultDialog *>().isEmpty());
}

void TestScraperController::promptResume_resumeConsumesStateAndOpensDialog() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h;
  writePendingSnapshot();

  h.controller.promptResumePendingScrapeIfAny();

  QMessageBox *box = findResumePrompt(h.host);
  QVERIFY(box);
  QAbstractButton *resume = buttonWithText(box, QStringLiteral("Resume"));
  QVERIFY(resume);
  resume->click();

  // The snapshot's collection UUID resolves to no live collection, so the
  // resumed queue drops the job and finishes immediately — the state file
  // must be consumed either way, and the dialog must come up so the user
  // sees where the resume landed.
  QVERIFY2(!QFile::exists(Scraper::ScrapePendingState::filePath()),
           "Resume must consume the pending snapshot");
  const auto dialogs = h.host.findChildren<ScrapeResultDialog *>();
  QCOMPARE(dialogs.size(), 1);
  QVERIFY(dialogs.first()->isVisible());
  // Hide before teardown so the class-wide visible-instance refcount
  // (isAnyInstanceVisible) stays balanced for later tests — destruction
  // of a still-visible dialog skips hideEvent.
  dialogs.first()->hide();
}

void TestScraperController::promptResume_autoResumeSkipsPromptAndOpensDialog() {
  ControllerHarness h;
  h.settings.scraper.options.scrapeAutoResume = true;
  writePendingSnapshot();

  h.controller.promptResumePendingScrapeIfAny();

  // Opted-in unattended recovery: no modal gate, straight to the dialog
  // with the snapshot consumed.
  QVERIFY2(!findResumePrompt(h.host), "auto-resume must not surface the resume prompt");
  QVERIFY(!QFile::exists(Scraper::ScrapePendingState::filePath()));
  const auto dialogs = h.host.findChildren<ScrapeResultDialog *>();
  QCOMPARE(dialogs.size(), 1);
  QVERIFY(dialogs.first()->isVisible());
  dialogs.first()->hide(); // keep the visible-instance refcount balanced
}

void TestScraperController::
    backgroundEntityScrape_unscrapableRequestStartsNothingAndShowsNothing() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h;

  // A playlist (synthesized, no single platform to resolve) and a collection
  // whose type claims no lookup provider. Neither builds an entity job, so
  // there is nothing to run — and the creation-time path must say so by doing
  // nothing rather than by surfacing the "no scraper is configured" box the
  // right-click entity scrape shows.
  CollectionConfig favorites;
  favorites.name = QStringLiteral("Favorites");
  favorites.isPlaylist = true;
  CollectionConfig unscrapable;
  unscrapable.name = QStringLiteral("Home Videos");
  unscrapable.type = QStringLiteral("kartend-has-no-provider-for-this");
  h.collections << favorites << unscrapable;

  bool sawModal = false;
  auto *watcher = new QTimer(&h.host);
  watcher->setInterval(0);
  QObject::connect(watcher, &QTimer::timeout, &h.host, [&]() {
    if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      sawModal = true;
      box->close();
    }
  });
  watcher->start();

  h.controller.startBackgroundEntityScrape({0, 1});
  // An empty request is inert too — no run, no crash on the empty queue.
  h.controller.startBackgroundEntityScrape({});
  QCoreApplication::processEvents();
  watcher->stop();

  QVERIFY2(!sawModal, "the creation-time fetch must never interrupt with a message box");
  QVERIFY2(h.host.findChildren<ScrapeResultDialog *>().isEmpty(),
           "the creation-time fetch must not construct the scraper dialog");
}

void TestScraperController::backgroundEntityScrape_withoutDatabaseHoldsInsteadOfWarning() {
  SKIP_MODAL_WIDGET_ON_MACOS();
  ControllerHarness h(/*withDatabase=*/false);

  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.type = QStringLiteral("video");
  h.collections << films;

  // openScraperDialog pops "Database is not ready" here. The background path
  // has no window to warn through and warning would break its silence, so it
  // holds the request for a later drain instead.
  bool sawModal = false;
  auto *watcher = new QTimer(&h.host);
  watcher->setInterval(0);
  QObject::connect(watcher, &QTimer::timeout, &h.host, [&]() {
    if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      sawModal = true;
      box->close();
    }
  });
  watcher->start();

  h.controller.startBackgroundEntityScrape({0});
  QCoreApplication::processEvents();
  watcher->stop();

  QVERIFY2(!sawModal, "a missing database must not surface a warning on the silent path");
  QVERIFY2(h.host.findChildren<ScrapeResultDialog *>().isEmpty(),
           "a missing database must not construct the scraper dialog");
}
