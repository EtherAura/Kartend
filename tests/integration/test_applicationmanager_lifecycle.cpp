#include "test_applicationmanager_lifecycle.h"

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collection/collectionconfig.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"

#include <QList>
#include <QSet>
#include <QStandardPaths>
#include <QTest>
#include <QThreadPool>

namespace {

// ApplicationManager touches QStandardPaths-backed locations during
// initialize() (cache, session, settings INI). The integration binary already
// enables test mode globally in test_main.cpp, but each lifecycle test
// reasserts it because previous test cases may have toggled it during
// teardown.
void ensureSandbox() {
  QStandardPaths::setTestModeEnabled(true);
}

} // namespace

void TestApplicationManagerLifecycle::cleanup() {
  // Mirror of test_main.cpp's drainGlobalThreadPool() applied between test
  // methods within this suite. setExpiryTimeout(0) + waitForDone() forces
  // idle workers to exit so the next test starts with fresh threads;
  // restore the default 30s timeout afterwards so it doesn't leak into
  // other suites.
  auto *pool = QThreadPool::globalInstance();
  pool->setExpiryTimeout(0);
  pool->waitForDone();
  pool->setExpiryTimeout(30'000);
}

void TestApplicationManagerLifecycle::testBareConstructDestructIsSafe() {
  ensureSandbox();
  // Construct without initialize() and let the destructor run. The dtor
  // contract is that it must not crash even when initialize() was never
  // called — m_cacheInitFuture is default-constructed (not running) and
  // every unique_ptr member is nullptr.
  ApplicationManager manager;
  Q_UNUSED(manager);
}

void TestApplicationManagerLifecycle::testGettersReturnNullBeforeInitialize() {
  ensureSandbox();
  ApplicationManager manager;
  QVERIFY(manager.getCacheManager() == nullptr);
  QVERIFY(manager.getSessionManager() == nullptr);
  QVERIFY(manager.getArtworkManager() == nullptr);
  QVERIFY(manager.getDatabaseManager() == nullptr);
  QVERIFY(manager.getPlaylistManager() == nullptr);
  QVERIFY(manager.getSettingsManager() == nullptr);
  QVERIFY(manager.getScrollManager() == nullptr);
  QVERIFY(manager.getDetailsPaneManager() == nullptr);
  QVERIFY(manager.getNavigationManager() == nullptr);
  QVERIFY(manager.getInteractionManager() == nullptr);
}

void TestApplicationManagerLifecycle::testInitializeWiresAllManagers() {
  ensureSandbox();
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager manager;
  manager.initialize(&appCtx);

  // Each getter must return a distinct, non-null pointer. Aliased managers
  // would indicate a wiring bug where ApplicationManager hands out the same
  // pointer for two roles.
  QSet<void *> seen;
  const auto record = [&](void *p) {
    QVERIFY(p != nullptr);
    QVERIFY2(!seen.contains(p), "ApplicationManager handed out an aliased manager");
    seen.insert(p);
  };
  record(manager.getCacheManager());
  record(manager.getSessionManager());
  record(manager.getArtworkManager());
  record(manager.getDatabaseManager());
  record(manager.getPlaylistManager());
  record(manager.getSettingsManager());
  record(manager.getScrollManager());
  record(manager.getDetailsPaneManager());
  record(manager.getNavigationManager());
  record(manager.getInteractionManager());
}

void TestApplicationManagerLifecycle::testManagersHaveNullQObjectParent() {
  ensureSandbox();
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager manager;
  manager.initialize(&appCtx);

  // Kartend-d70s (re-attempted after Kartend-3v92 replaced
  // NavigationManager's parent()-based lifetime guards with the
  // appNotShuttingDown() helper): sub-managers are owned solely by their
  // std::unique_ptr members. QObject parent stays null so Qt's children
  // sweep can never double-delete a manager that's also held in a
  // unique_ptr, and so a future reparent doesn't change destruction
  // semantics. Asserting parent() == nullptr here catches a regression
  // that adds `this` back to any of these make_unique calls — that
  // would resurrect the double-ownership footgun.
  QCOMPARE(manager.getSessionManager()->parent(), nullptr);
  QCOMPARE(manager.getArtworkManager()->parent(), nullptr);
  QCOMPARE(manager.getDatabaseManager()->parent(), nullptr);
  QCOMPARE(manager.getPlaylistManager()->parent(), nullptr);
  QCOMPARE(manager.getSettingsManager()->parent(), nullptr);
  QCOMPARE(manager.getScrollManager()->parent(), nullptr);
  QCOMPARE(manager.getDetailsPaneManager()->parent(), nullptr);
  QCOMPARE(manager.getNavigationManager()->parent(), nullptr);
  QCOMPARE(manager.getInteractionManager()->parent(), nullptr);

  // CacheManager is intentionally not a QObject (plain class) and therefore
  // has no Qt-parent relationship. Its lifetime is bounded entirely by the
  // unique_ptr in ApplicationManager and the m_cacheInitFuture wait in the
  // dtor — see testDestructAfterInitializeWithoutShutdownIsSafe.
}

void TestApplicationManagerLifecycle::testShutdownAfterInitializeIsSafe() {
  ensureSandbox();
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager manager;
  manager.initialize(&appCtx);

  // shutdown() with an empty collection list exercises every conditional
  // branch (`if (m_artworkManager)`, snapshotting, settings save, cache
  // release, parallel persist) without depending on real on-disk
  // collections. The contract is "shutdown must complete without crashing
  // even when the user never opened a collection."
  const QList<CollectionConfig> empty;
  manager.shutdown(empty);

  // Managers must still be alive — shutdown() snapshots state but does not
  // destroy anything; destruction is the dtor's job.
  QVERIFY(manager.getCacheManager() != nullptr);
  QVERIFY(manager.getDatabaseManager() != nullptr);
}

void TestApplicationManagerLifecycle::testDestructAfterInitializeWithoutShutdownIsSafe() {
  ensureSandbox();
  // Cover the abnormal-teardown path the dtor's m_cacheInitFuture guard is
  // designed for: initialize() spawns the background cache init via
  // QtConcurrent::run, then we destruct before shutdown() ever runs. The
  // destructor must wait for the deferred task before letting CacheManager
  // be destroyed (otherwise the worker would dereference a freed cache).
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager manager;
  manager.initialize(&appCtx);
  // Intentionally no shutdown() call — block scope ends and dtor runs.
}

void TestApplicationManagerLifecycle::testDestructorNullsContextManagerSlots() {
  ensureSandbox();
  // Kartend-w06qp: ~ApplicationManager must null every ctx->managers.* slot
  // it (or MainWindow::initializeAppContext) registered, immediately before
  // the corresponding manager is destroyed. Otherwise the pervasive
  // `if (auto *m = ctx->x())` guards read stale non-null pointers during
  // the destruction window — a latent use-after-free class.
  ApplicationContext appCtx;
  {
    ApplicationManager manager;
    manager.initialize(&appCtx);
    // initialize() registers the slots it owns; sanity-check a few are live.
    QVERIFY(appCtx.cacheManager() != nullptr);
    QVERIFY(appCtx.sessionManager() != nullptr);
    QVERIFY(appCtx.interactionManager() != nullptr);
    // Simulate the slots MainWindow::initializeAppContext registers on top
    // (playlist manager + a sub-manager-owned slot) so the dtor's clearing
    // of those is exercised too.
    appCtx.managers.playlistManager = manager.getPlaylistManager();
    QVERIFY(appCtx.playlistManager() != nullptr);
  }
  // After the dtor, every manager slot must be null — a stale non-null here
  // means a ctx accessor would hand out a dangling pointer post-teardown.
  QVERIFY(appCtx.scrollManager() == nullptr);
  QVERIFY(appCtx.artworkManager() == nullptr);
  QVERIFY(appCtx.settingsManager() == nullptr);
  QVERIFY(appCtx.sessionManager() == nullptr);
  QVERIFY(appCtx.detailsPaneManager() == nullptr);
  QVERIFY(appCtx.detailPageManager() == nullptr);
  QVERIFY(appCtx.databaseManager() == nullptr);
  QVERIFY(appCtx.navigationManager() == nullptr);
  QVERIFY(appCtx.interactionManager() == nullptr);
  QVERIFY(appCtx.cacheManager() == nullptr);
  QVERIFY(appCtx.playlistManager() == nullptr);
  QVERIFY(appCtx.filterManager() == nullptr);
  QVERIFY(appCtx.animationManager() == nullptr);
  QVERIFY(appCtx.selectionManager() == nullptr);
  QVERIFY(appCtx.viewportManager() == nullptr);
  QVERIFY(appCtx.mouseManager() == nullptr);
  QVERIFY(appCtx.keyboardManager() == nullptr);
  QVERIFY(appCtx.eventManager() == nullptr);
  QVERIFY(appCtx.searchManager() == nullptr);
  QVERIFY(appCtx.launchManager() == nullptr);
  QVERIFY(appCtx.interactionState() == nullptr);
}
