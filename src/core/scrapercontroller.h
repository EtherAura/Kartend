#ifndef SCRAPERCONTROLLER_H
#define SCRAPERCONTROLLER_H

// Controller extracted from MainWindow (Kartend-hzef step 3). Owns the
// two scraper-flow entry points previously hosted by mainwindow_scraper.cpp:
//   - openScraperDialog() — long-lived ScrapeResultDialog construction +
//     ScraperService binding + post-completion housekeeping.
//   - promptResumePendingScrapeIfAny() — startup resume flow.
//
// The dialog (m_scraperDialog) and the long-lived service (m_scraperService)
// move into the controller, so the dialog cache survives across openScraper
// calls without crossing back through MainWindow. pickLookupProvider() moves
// into the controller's .cpp anonymous namespace too — it was a file-local
// helper for these two methods.
//
// MainWindow's IMainWindow::openScraperDialog override stays as a thin
// shim that forwards to this controller, so callers reaching MainWindow
// through the interface (e.g. interaction context menu) don't need to know
// about the controller.

#include <functional>
#include <memory>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class ArtworkManager;
class DetailsPaneManager;
class IDatabaseManager;
class InteractionManager;
class MetadataLookupProvider;
class NavigationManager;
class ScrapeResultDialog;
class ScrollManager;
#include "applicationcontext_fwd.h"
struct CollectionConfig;
struct GeneralSettings;

template <typename T> class QList;

namespace Scraper {
class ScraperService;
}

namespace ScraperControllerInternal {
/// The shared per-collection provider-builder closure openScraperDialog /
/// promptResumePendingScrapeIfAny wire into the dialog + service contexts.
/// Each call resolves the collection's scraper through
/// MetadataProviderRegistry::claimLookupProvider (override id first, else
/// first category match). Declared here — rather than staying file-local in
/// scrapercontroller.cpp — as a test seam: the integration suite exercises
/// the per-index claim path directly, which is otherwise only observable
/// through a live scrape. Production callers stay inside the controller.
[[nodiscard]] std::function<std::shared_ptr<MetadataLookupProvider>(int)>
makeProviderBuilder(QList<CollectionConfig> *collections, GeneralSettings *generalSettings);
} // namespace ScraperControllerInternal

struct ScraperControllerContext {
  /// Parent widget for the dialog + message boxes — the live MainWindow.
  /// Kept as a getter so the controller's signal lambdas don't snapshot a
  /// raw pointer that could outlive the window if a future refactor
  /// changes lifetime ownership.
  std::function<QWidget *()> getParentWindow;

  /// References to MainWindow's mutable state. Lists of collections and
  /// settings are kept on MainWindow because they're also read by many
  /// other code paths; the controller borrows them through these getters.
  std::function<QList<CollectionConfig> *()> getCollections;
  std::function<GeneralSettings *()> getGeneralSettings;
  std::function<int()> getCurrentCollectionIndex;
  std::function<const ApplicationContext *()> getApplicationContext;

  /// Manager accessors used by the dialog's post-completion housekeeping
  /// path (refresh sidebar selection, reload current collection, clear
  /// artwork directory cache) and by the provider-build closure.
  std::function<IDatabaseManager *()> getDatabaseManager;
  std::function<NavigationManager *()> getNavigationManager;
  std::function<DetailsPaneManager *()> getDetailsPaneManager;
  std::function<InteractionManager *()> getInteractionManager;
  std::function<ScrollManager *()> getScrollManager;
};

class ScraperController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperController)
public:
  explicit ScraperController(QObject *parent = nullptr);
  ~ScraperController() override;

signals:
  /// Re-emitted from the owned service's scrapeFinished — the service itself
  /// is private (Kartend-hzef ownership), so listeners that only care that "a
  /// run just ended and collection art/config may have changed" (the
  /// collection tree's icon refresh, Kartend-ob1c9.1) subscribe here instead
  /// of reaching into the controller for the service pointer.
  void scrapeRunFinished();

public:
  void setContext(const ScraperControllerContext &context);

public slots:
  /// Show (or raise) the unified scraper dialog, optionally pre-scoped to
  /// a single collection / item. The dialog is constructed lazily on
  /// first call and reused across subsequent opens — its closeEvent
  /// hides instead of destroying so a background scrape survives the
  /// user closing the window.
  void openScraperDialog(int preCollectionIndex = -1, const QString &preItemPath = QString());

  /// Kartend-ckepd.6: launch a one-shot Platform entity scrape for @p
  /// collectionIndex (right-click → "Scrape platform artwork"). Reuses the same
  /// dialog/service as openScraperDialog but skips the per-item grid.
  void openEntityScraperDialog(int collectionIndex);

  /// Kartend-ud6q2: fetch entity art (platform / collection logo +
  /// background) for @p collectionIndices with NO dialog on screen — the
  /// creation-time path, so a newly built collection's tree and home icons
  /// populate without the user asking. Silent by design: no progress window,
  /// no completion box; the summary lands in the scrape log and failures are
  /// routine (a collection whose platform cannot be resolved is a not-found,
  /// not an error).
  ///
  /// The service is strictly one-run-at-a-time, so indices arriving while a
  /// run is live are held and started when it ends rather than dropped —
  /// creating collections during a scrape is exactly when this fires.
  /// Collections that build no entity jobs (playlists, no configured scraper)
  /// drop out silently; if nothing survives, no run starts.
  void startBackgroundEntityScrape(const QList<int> &collectionIndices);

  /// Startup hook: if the on-disk pending-scrape state file is present
  /// (i.e. the previous run was interrupted mid-batch), prompt the user
  /// to Resume / Discard / Keep. Auto-resume short-circuits the prompt
  /// when the user has opted in via ScraperOptions.
  void promptResumePendingScrapeIfAny();

private:
  /// Shared open-time dialog setup used by both openScraperDialog and
  /// openEntityScraperDialog; returns the bound dialog or nullptr if the DB
  /// isn't ready. The caller picks the start method and shows it.
  ScrapeResultDialog *prepareScraperDialog();

  /// Push the live collections / settings / provider-builder closures into
  /// the long-lived service and hand the builder back. Split out of
  /// prepareScraperDialog so the background entity scrape can bind the
  /// service without constructing a dialog it would never show. Returns an
  /// empty std::function when the collection list isn't available.
  std::function<std::shared_ptr<MetadataLookupProvider>(int)> bindServiceContext();

  /// Start the held background entity queue if one is waiting and the service
  /// is idle. Called after a run ends and after a fresh request arrives.
  void drainPendingBackgroundEntityScrape();

  /// How long to wait before re-attempting a drain that was held off by an
  /// open modal dialog. Long enough not to spin behind a dialog the user is
  /// reading, short enough that the artwork appears while they are still
  /// looking at the collection they just made.
  static constexpr int kBackgroundDrainRetryMs = 750;

  ScraperControllerContext m_ctx;

  /// Collection indices waiting for the service to go idle so their entity
  /// art can be fetched. A QList (not a QSet) because queue order is the
  /// creation order the user sees the tree fill in. Deduped on insert.
  QList<int> m_pendingBackgroundEntityIndices;

  /// True while a run started by startBackgroundEntityScrape is in flight.
  /// Consumed by the dialog's unifiedScrapeFinished handler to skip the modal
  /// completion box: the dialog may have been constructed by an earlier
  /// interactive open and stays bound to the shared service, so it reports
  /// the end of a run the user never started. The post-run housekeeping
  /// (artwork cache, grid + sidebar refresh) still runs — art landed.
  bool m_backgroundEntityRunActive = false;

  /// A modal-blocked drain retry is already scheduled — keeps repeated
  /// requests behind one modal from stacking a timer each.
  bool m_backgroundDrainRetryArmed = false;

  /// Long-lived ScraperService — survives dialog close so a background
  /// scrape keeps progressing after the user dismisses the window.
  /// Constructed in the ctor; setContext() rebinds the service-context
  /// each open with live closures.
  std::unique_ptr<Scraper::ScraperService> m_scraperService;

  /// Cached dialog — constructed on first openScraperDialog, hidden (not
  /// destroyed) on close so the widget tree survives across re-opens.
  /// QPointer so a destruction elsewhere (parent teardown, a future
  /// WA_DeleteOnClose) nulls the cache and the `if (!m_scraperDialog)`
  /// check self-heals by recreating instead of reusing a dangling pointer.
  /// The unifiedScrapeFinished housekeeping handler is connected inside the
  /// construction block in openScraperDialog, so it is wired exactly once
  /// per dialog instance without an ad-hoc connected-yet flag.
  QPointer<ScrapeResultDialog> m_scraperDialog;
};

#endif // SCRAPERCONTROLLER_H
