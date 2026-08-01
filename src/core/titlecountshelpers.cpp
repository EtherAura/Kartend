// Extracted from src/core/mainwindow.cpp (Kartend-dk2c.5). See
// titlecountshelpers.h for the rationale.
#include "titlecountshelpers.h"

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "iartworkmanager.h" // unused but pulled along by ApplicationContext header chain
#include "idatabasemanager.h"
#include "iscrolldatasource.h"
#include "isessionmanager.h"
#include "loadingoverlay.h"
#include "stringutils.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace {

// refreshTitleCounts fires on every navigation entry, items-loaded pass,
// route change, and cachedCountsUpdated event (the last is wired straight to
// the DB scan pipeline in mainwindow_wiring.cpp), and each call used to
// re-list the current directory on the GUI thread just to render the
// "N subfolders" suffix. Memoize the count per (scan dir, hidden-folders
// flag), invalidated by the directory's mtime: a directory's modification
// time changes exactly when direct entries are added, removed, or renamed —
// the only mutations that can change the count — so one stat replaces a full
// readdir per refresh, and DB-side scan bursts (which never touch the
// directory itself) hit the memo every time. No cross-event generation
// counter is cleanly reachable from this free function (the callers span
// NavigationManager and DbEventsController), hence the path+mtime key.
// GUI-thread only, like the caller.
struct VirtualFolderCountMemoEntry {
  QDateTime mtime;
  int count = 0;
};

int memoizedVirtualFolderCount(const CollectionConfig &config) {
  if (!config.folderBrowsing.includeContentSubfolders ||
      config.folderBrowsing.showAllSubfolderItems) {
    return 0;
  }
  const QFileInfo info(CollectionUtils::virtualFolderScanDir(config));
  if (!info.exists() || !info.isDir()) {
    return 0;
  }
  const QString key =
      info.absoluteFilePath() + QLatin1Char('\n') +
      (config.folderBrowsing.showHiddenFolders ? QLatin1Char('1') : QLatin1Char('0'));
  const QDateTime mtime = info.lastModified();

  static QHash<QString, VirtualFolderCountMemoEntry> memo;
  const auto it = memo.constFind(key);
  if (it != memo.cend() && it->mtime == mtime) {
    return it->count;
  }
  const int count = CollectionUtils::countVirtualFolders(config);
  // Bound the memo so a long session browsing many subfolders can't grow it
  // without limit; 64 live (dir, flag) keys is far beyond one window's needs.
  if (memo.size() >= 64) {
    memo.clear();
  }
  memo.insert(key, {mtime, count});
  return count;
}

} // namespace

namespace TitleCountsHelpers {

void refreshTitleCounts(QWidget *titleHost, const ApplicationContext &ctx,
                        const QList<CollectionConfig> &collections, int currentCollectionIndex,
                        const LoadingOverlay *loadingOverlay) {
  if (!titleHost) {
    return;
  }
  if (!ctx.databaseManager()) {
    return;
  }

  // Don't update title bar with counts while a scan is in progress
  // (the scan progress handler sets the title instead)
  if (loadingOverlay && loadingOverlay->isActive()) {
    return;
  }

  const int cur = currentCollectionIndex;
  if (cur < 0 || cur >= collections.size()) {
    titleHost->setWindowTitle(qApp->applicationName());
    return;
  }

  auto *session = ctx.sessionManager();
  auto cachedRecursiveCountForIndex = [&collections, session](int collectionIndex) -> qint64 {
    if (!session) {
      return -1;
    }
    if (collectionIndex < 0 || collectionIndex >= collections.size()) {
      return -1;
    }
    qint64 direct = -1;
    qint64 recursive = -1;
    if (!session->getCollectionCounts(collections[collectionIndex], collections, direct,
                                      recursive)) {
      return -1;
    }
    return recursive;
  };

  // Appends " — N subfolders, M subcollections" to @p title when @p cur has
  // any direct children. Used by both the subfolder and the collection
  // branches below.
  auto appendChildPartsSuffix = [&collections, cur](QString &title) {
    const int directSubfolderCount = memoizedVirtualFolderCount(collections[cur]);
    const int directSubcollectionCount = CollectionUtils::directChildrenOf(cur, collections).size();
    QStringList childParts;
    if (directSubfolderCount > 0) {
      childParts << QString("%1 subfolders").arg(directSubfolderCount);
    }
    if (directSubcollectionCount > 0) {
      childParts << QString("%1 subcollections").arg(directSubcollectionCount);
    }
    if (!childParts.isEmpty()) {
      title += QString(" — %1").arg(childParts.join(", "));
    }
  };

  auto *scroll = ctx.scrollData();

  // Check if we're in a subfolder
  const QString &subfolder = collections[cur].folderBrowsing.currentSubfolder;
  if (!subfolder.isEmpty() && scroll) {
    // In a subfolder: show "SubfolderName (subfolderCount/collectionCount
    // Items)"
    QString subfolderName = subfolder;
    int lastSlash = subfolder.lastIndexOf('/');
    if (lastSlash >= 0) {
      subfolderName = subfolder.mid(lastSlash + 1);
    }

    const int subfolderItemCount = scroll->getTotalItems();
    const qint64 collectionCount = cachedRecursiveCountForIndex(cur);
    QString counts;
    if (collectionCount >= 0) {
      counts = QString("(%1/%2 Items)")
                   .arg(StringUtils::formatCountNumber(subfolderItemCount))
                   .arg(StringUtils::formatCountNumber(collectionCount));
    } else {
      counts = QString("(%1 Items)").arg(StringUtils::formatCountNumber(subfolderItemCount));
    }

    QString title = QString("%1 %2").arg(subfolderName, counts);
    appendChildPartsSuffix(title);
    titleHost->setWindowTitle(title);
    return;
  }

  // Not in subfolder: show collection hierarchy counts
  QVector<int> chain;
  int walk = cur;
  while (walk >= 0 && walk < collections.size()) {
    chain.append(walk);
    int parentIndex = collections[walk].parentCollectionIndex;
    if (parentIndex < 0) {
      break;
    }
    walk = parentIndex;
  }

  // When showAllSubcollectionItems is enabled, the displayed items include all
  // descendant items. Use the actual view count for the current collection
  // rather than the cached recursive count (which may not include flattened
  // items).
  const bool showAllItems = collections[cur].showAllSubcollectionItems;
  const int viewTotalItems = scroll ? scroll->getTotalItems() : -1;

  QStringList parts;
  bool anyKnown = false;
  for (int i = 0; i < chain.size(); ++i) {
    int idx = chain[i];
    qint64 countVal = -1;
    if (i == 0 && showAllItems && viewTotalItems >= 0) {
      countVal = viewTotalItems;
    } else {
      countVal = cachedRecursiveCountForIndex(idx);
    }
    if (countVal >= 0) {
      anyKnown = true;
      parts << StringUtils::formatCountNumber(countVal);
    } else {
      parts << QStringLiteral("…");
    }
  }

  const QString base = collections[cur].name;
  QString counts;
  if (anyKnown) {
    counts = QString("(%1 Items)").arg(parts.size() == 1 ? parts.first() : parts.join('/'));
  }

  QString title = counts.isEmpty() ? base : QString("%1 %2").arg(base, counts);
  appendChildPartsSuffix(title);
  titleHost->setWindowTitle(title);
}

} // namespace TitleCountsHelpers
