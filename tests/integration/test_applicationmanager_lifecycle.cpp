#include "test_applicationmanager_lifecycle.h"

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "detailspanemanager.h"

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
  ApplicationManager manager;
  ApplicationContext appCtx; manager.initialize(&appCtx);

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

void TestApplicationManagerLifecycle::testManagersAreParentedToApplicationManager() {
  ensureSandbox();
  ApplicationManager manager;
  ApplicationContext appCtx; manager.initialize(&appCtx);

  // Every manager constructed with ApplicationManager as Qt parent should
  // report it via QObject::parent(). CacheManager is intentionally
  // unparented (constructed without a parent argument) so it is excluded.
  // The parenting matters for shutdown ordering: managers without
  // ApplicationManager as parent rely solely on unique_ptr member-order
  // destruction, so a regression that drops the `this` arg would silently
  // change destruction semantics.
  QCOMPARE(manager.getSessionManager()->parent(), &manager);
  QCOMPARE(manager.getArtworkManager()->parent(), &manager);
  QCOMPARE(manager.getDatabaseManager()->parent(), &manager);
  QCOMPARE(manager.getPlaylistManager()->parent(), &manager);
  QCOMPARE(manager.getSettingsManager()->parent(), &manager);
  QCOMPARE(manager.getScrollManager()->parent(), &manager);
  QCOMPARE(manager.getDetailsPaneManager()->parent(), &manager);
  QCOMPARE(manager.getNavigationManager()->parent(), &manager);
  QCOMPARE(manager.getInteractionManager()->parent(), &manager);

  // CacheManager is intentionally not a QObject (plain class) and therefore
  // has no Qt-parent relationship. Its lifetime is bounded entirely by the
  // unique_ptr in ApplicationManager and the m_cacheInitFuture wait in the
  // dtor — see testDestructAfterInitializeWithoutShutdownIsSafe.
}

void TestApplicationManagerLifecycle::testShutdownAfterInitializeIsSafe() {
  ensureSandbox();
  ApplicationManager manager;
  ApplicationContext appCtx; manager.initialize(&appCtx);

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
  ApplicationManager manager;
  ApplicationContext appCtx; manager.initialize(&appCtx);
  // Intentionally no shutdown() call — block scope ends and dtor runs.
}
