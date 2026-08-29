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
#include <QCoreApplication>
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

// Kartend-0ylim: the window title agreed with nothing at n == 1 — "1
// subcollections", and "(1 Items)" for a collection holding a single file.
//
// Kartend-rp0hk moved this from in-code pluralisation to tr()/%n. That was
// previously unsafe and is now correct for one reason: translations/
// kartend_en.ts is a FILLED English catalogue that is compiled and shipped
// (see the lrelease block in CMakeLists.txt). Qt performs no English plural
// selection of its own for an untranslated string — it substitutes the count
// into the source verbatim — so without that .qm loaded a "%n subcollection(s)"
// source renders literally as "1 subcollection(s)", which is worse than the bug
// it fixes. With it loaded, the numerusform entries decide.
//
// If the English .qm is ever dropped again, revert these to in-code
// pluralisation in the same commit.

/// "3 subfolders" / "1 subcollection". Small counts, so no digit grouping —
/// matching what this suffix has always emitted.
QString subfolderPart(int n) {
  return QCoreApplication::translate("TitleCountsHelpers", "%n subfolder(s)", nullptr, n);
}

/// "subfolders" and "subcollections" are deliberately different nouns: the
/// first counts filesystem folders browsed as virtual folders, the second
/// counts real child collections. Not a vocabulary inconsistency.
QString subcollectionPart(int n) {
  return QCoreApplication::translate("TitleCountsHelpers", "%n subcollection(s)", nullptr, n);
}

/// "(1 Item)" / "(54 Items)". @p display carries the already locale-formatted
/// digits so grouping survives, and may be a joined ancestor chain ("6/6/7") —
/// so it is substituted as %1 rather than being the plural's own count.
/// @p governing is the count the noun agrees with; pass -1 when several numbers
/// are shown at once, since a compound like "19/54" reads as plural regardless.
///
/// governing is collapsed to 1-or-2 rather than cast: it is a qint64, and a
/// count above INT_MAX narrowed to int could land on 1 and silently pick the
/// singular. Only "is it exactly one" matters to English, and every other
/// language's rule is applied by its own catalogue, not here.
QString itemsCount(const QString &display, qint64 governing) {
  return QCoreApplication::translate("TitleCountsHelpers", "(%1 Item(s))", nullptr,
                                     governing == 1 ? 1 : 2)
      .arg(display);
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
      childParts << subfolderPart(directSubfolderCount);
    }
    if (directSubcollectionCount > 0) {
      childParts << subcollectionPart(directSubcollectionCount);
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
      // Two numbers shown ("19/54") — governing count -1 keeps the noun plural,
      // which is what a compound like that reads as in English either way.
      counts = itemsCount(QString("%1/%2").arg(StringUtils::formatCountNumber(subfolderItemCount),
                                               StringUtils::formatCountNumber(collectionCount)),
                          -1);
    } else {
      counts = itemsCount(StringUtils::formatCountNumber(subfolderItemCount), subfolderItemCount);
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
  // The count for the collection being viewed (chain[0]) — the one the noun
  // agrees with when it is the only number on show. -1 while unknown.
  qint64 currentCount = -1;
  for (int i = 0; i < chain.size(); ++i) {
    int idx = chain[i];
    qint64 countVal = -1;
    if (i == 0 && showAllItems && viewTotalItems >= 0) {
      countVal = viewTotalItems;
    } else {
      countVal = cachedRecursiveCountForIndex(idx);
    }
    if (i == 0) {
      currentCount = countVal;
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
    // A single part is this collection's own count, so it governs the noun —
    // this is the "(1 Items)" case from the report. An ancestor chain
    // ("6/6/7") shows several numbers at once and stays plural.
    counts = parts.size() == 1 ? itemsCount(parts.first(), currentCount)
                               : itemsCount(parts.join('/'), -1);
  }

  QString title = counts.isEmpty() ? base : QString("%1 %2").arg(base, counts);
  appendChildPartsSuffix(title);
  titleHost->setWindowTitle(title);
}

} // namespace TitleCountsHelpers
