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

// Kartend-0ylim: the window title agreed with nothing at n == 1 — "1
// subcollections", and "(1 Items)" for a collection holding a single file.
//
// Pluralised in CODE rather than through tr()/%n on purpose. The committed
// translations/kartend_en.ts carries only `type="unfinished"` entries with
// EMPTY translation bodies — it is a seed for translators, not a filled English
// catalogue — so tr() falls back to the source text verbatim and a
// "%Ln subcollection(s)" source would render literally as "1 subcollection(s)",
// which is worse than the bug it fixes. Qt does no English plural selection of
// its own for an untranslated string. The rest of this title builder is
// likewise untranslated raw QString; making the whole title translatable is a
// separate job (Kartend-rp0hk), and doing it half-way here would regress the
// default-locale user everyone actually has.

/// "3 subfolders" / "1 subcollection". Small counts, so no digit grouping —
/// matching what this suffix has always emitted.
QString childPart(int n, const QString &singular, const QString &plural) {
  return QString("%1 %2").arg(QString::number(n), n == 1 ? singular : plural);
}

/// "(1 Item)" / "(54 Items)". @p display carries the already locale-formatted
/// digits so grouping survives, and may be a joined ancestor chain ("6/6/7").
/// @p governing is the count the noun agrees with; pass -1 when several numbers
/// are shown at once, since a compound like "19/54" reads as plural regardless.
QString itemsCount(const QString &display, qint64 governing) {
  return QString("(%1 %2)").arg(display,
                                governing == 1 ? QStringLiteral("Item") : QStringLiteral("Items"));
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
      // "subfolders" and "subcollections" are deliberately different nouns:
      // the first counts filesystem folders browsed as virtual folders, the
      // second counts real child collections. Not a vocabulary inconsistency.
      childParts << childPart(directSubfolderCount, QStringLiteral("subfolder"),
                              QStringLiteral("subfolders"));
    }
    if (directSubcollectionCount > 0) {
      childParts << childPart(directSubcollectionCount, QStringLiteral("subcollection"),
                              QStringLiteral("subcollections"));
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
