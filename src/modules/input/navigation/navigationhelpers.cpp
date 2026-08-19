#include "navigationhelpers.h"

#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include <algorithm>

namespace NavigationHelpers {

auto computeCollectionDepth(int collectionIndex, const QList<CollectionConfig> &collections)
    -> int {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return 0;
  }
  // Bounded iteration: in the worst case (linear chain) we visit every
  // collection once. Anything beyond that is a cycle and we bail out.
  const int maxIters = collections.size();
  int depth = 0;
  int idx = collectionIndex;
  for (int i = 0; i < maxIters && idx >= 0 && idx < collections.size(); ++i) {
    ++depth;
    int parent = collections[idx].parentCollectionIndex;
    if (parent < 0) {
      return depth;
    }
    idx = parent;
  }
  return depth;
}

auto isValidCollectionIndex(int collectionIndex, const QList<CollectionConfig> &collections)
    -> bool {
  return collectionIndex >= 0 && collectionIndex < collections.size();
}

auto lookupRememberedSelectionIndex(int collectionIndex, const QList<CollectionConfig> &collections,
                                    int totalItems, const SessionLookup &lookup) -> int {
  if (!isValidCollectionIndex(collectionIndex, collections) || totalItems <= 0 || !lookup) {
    return -1;
  }

  const CollectionConfig &cfg = collections[collectionIndex];
  const bool subfolderActive = !cfg.folderBrowsing.currentSubfolder.trimmed().isEmpty();

  int selIdx = -1;
  if (subfolderActive) {
    selIdx = lookup(CollectionUtils::selectionSessionKeyFor(cfg, collections));
  } else {
    selIdx = lookup(CollectionUtils::hierarchicalNameFor(cfg, collections));
    if (selIdx < 0) {
      selIdx = lookup(cfg.name);
    }
  }

  if (selIdx < 0) {
    return -1;
  }
  if (selIdx >= totalItems) {
    selIdx = totalItems - 1;
  }
  return std::max(selIdx, 0);
}

auto calculateSelectionIndex(int collectionIndex, const QList<CollectionConfig> &collections,
                             int totalItems, bool searchActive, bool rememberSelectionEnabled,
                             const SessionLookup &lookup) -> int {
  if (searchActive || !rememberSelectionEnabled || totalItems <= 0) {
    return -1;
  }
  const int selIdx =
      lookupRememberedSelectionIndex(collectionIndex, collections, totalItems, lookup);
  if (selIdx < 0) {
    return 0;
  }
  return selIdx;
}

auto findSubcollectionVisualIndex(int targetCollectionIndex, int previousIndex,
                                  const QList<CollectionConfig> &collections) -> int {
  if (targetCollectionIndex < 0 || targetCollectionIndex >= collections.size()) {
    return -1;
  }
  int visual = -1;
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == targetCollectionIndex) {
      ++visual;
      if (i == previousIndex) {
        return visual;
      }
    }
  }
  return -1;
}

auto chooseFallbackCollectionIndex(int currentIndex, const QList<CollectionConfig> &collections)
    -> int {
  if (collections.isEmpty()) {
    return -1;
  }
  if (currentIndex >= 0 && currentIndex < collections.size()) {
    return currentIndex;
  }
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == -1) {
      return i;
    }
  }
  return 0;
}

auto parseBreadcrumbLink(const QString &link) -> BreadcrumbLink {
  BreadcrumbLink result;
  if (link == QStringLiteral("root:")) {
    result.kind = BreadcrumbLink::Kind::Root;
    return result;
  }
  static constexpr QLatin1String kCollectionPrefix("collection:");
  static constexpr QLatin1String kSubfolderPrefix("subfolder:");
  if (link.startsWith(kCollectionPrefix)) {
    bool ok = false;
    const int idx = link.mid(kCollectionPrefix.size()).toInt(&ok);
    if (ok && idx >= 0) {
      result.kind = BreadcrumbLink::Kind::Collection;
      result.collectionIndex = idx;
    }
    return result;
  }
  if (link.startsWith(kSubfolderPrefix)) {
    result.kind = BreadcrumbLink::Kind::Subfolder;
    result.subfolderPath = link.mid(kSubfolderPrefix.size());
    return result;
  }
  return result;
}

auto parentSubfolderPath(const QString &currentSubfolder) -> QString {
  if (currentSubfolder.isEmpty()) {
    return {};
  }
  // Trim a single trailing slash (treat "Action/" same as "Action").
  QString trimmed = currentSubfolder;
  while (trimmed.endsWith(QLatin1Char('/'))) {
    trimmed.chop(1);
  }
  if (trimmed.isEmpty()) {
    return {};
  }
  const int lastSlash = trimmed.lastIndexOf(QLatin1Char('/'));
  if (lastSlash <= 0) {
    return {};
  }
  return trimmed.left(lastSlash);
}

auto buildTitleBreadcrumbHtml(int collectionIndex, const QList<CollectionConfig> &collections,
                              const QString &linkColorHex) -> QString {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }
  const CollectionConfig &config = collections[collectionIndex];
  const bool inSubfolder = !config.folderBrowsing.currentSubfolder.isEmpty();

  const QString rootLinkTemplate = QStringLiteral("<a href=\"root:\" style=\"color:%1; "
                                                  "text-decoration:none;\">%2</a>");

  if (config.isSubcollection) {
    const QList<int> ancestors = CollectionUtils::ancestorIndexChain(config, collections);
    if (ancestors.isEmpty()) {
      return config.name.toHtmlEscaped();
    }
    // Clickable breadcrumb from root-most ancestor down to direct parent.
    // Each ancestor segment navigates back to that collection via the
    // `collection:<index>` link scheme decoded by parseBreadcrumbLink.
    const QString linkTemplate = QStringLiteral("<a href=\"collection:%1\" style=\"color:%2; "
                                                "text-decoration:none;\">%3</a>");
    QStringList segments;
    segments.reserve(ancestors.size() + 1);
    for (int idx : ancestors) {
      const QString name = collections[idx].name.toHtmlEscaped();
      segments << linkTemplate.arg(QString::number(idx), linkColorHex, name);
    }
    // The current collection is ALWAYS clickable (field report
    // 2026-08-18: "clicking collection name in toolbar doesnt navigate").
    // In a subfolder it returns to the collection root; otherwise it
    // re-enters the collection itself, which is what re-selects it after
    // browsing elsewhere.
    segments << (inSubfolder ? rootLinkTemplate.arg(linkColorHex, config.name.toHtmlEscaped())
                             : QStringLiteral("<a href=\"collection:%1\" style=\"color:%2; "
                                              "text-decoration:none;\">%3</a>")
                                   .arg(QString::number(collectionIndex), linkColorHex,
                                        config.name.toHtmlEscaped()));
    return segments.join(QStringLiteral(" › "));
  }

  // Root collection — clickable either way (see above).
  if (inSubfolder) {
    return rootLinkTemplate.arg(linkColorHex, config.name.toHtmlEscaped());
  }
  return QStringLiteral("<a href=\"collection:%1\" style=\"color:%2; "
                        "text-decoration:none;\">%3</a>")
      .arg(QString::number(collectionIndex), linkColorHex, config.name.toHtmlEscaped());
}

auto buildSubfolderBreadcrumbHtml(const QString &subfolder, const QString &linkColorHex)
    -> QString {
  const QStringList pathParts = subfolder.split('/', Qt::SkipEmptyParts);
  QString html;
  QString accumulatedPath;
  for (int i = 0; i < pathParts.size(); ++i) {
    if (!accumulatedPath.isEmpty()) {
      accumulatedPath += '/';
    }
    accumulatedPath += pathParts[i];

    if (i > 0) {
      html += QStringLiteral(" › ");
    }

    if (i < pathParts.size() - 1) {
      // Intermediate folder — clickable to navigate to that level.
      html += QStringLiteral("<a href=\"subfolder:%1\" style=\"color:%2; "
                             "text-decoration:none;\">%3</a>")
                  .arg(accumulatedPath.toHtmlEscaped(), linkColorHex, pathParts[i].toHtmlEscaped());
    } else {
      // Current folder — not clickable, just styled.
      html += pathParts[i].toHtmlEscaped();
    }
  }
  return html;
}

auto filterSubcollectionsByName(const QList<int> &subcollections,
                                const QList<CollectionConfig> &collections,
                                const QString &searchText) -> QList<int> {
  if (searchText.isEmpty()) {
    return subcollections;
  }
  QList<int> filtered;
  filtered.reserve(subcollections.size());
  for (int subIdx : subcollections) {
    if (subIdx < 0 || subIdx >= collections.size()) {
      continue;
    }
    if (collections[subIdx].name.contains(searchText, Qt::CaseInsensitive)) {
      filtered.append(subIdx);
    }
  }
  return filtered;
}

auto shouldSkipRebuildAfterBackgroundRefresh(int currentViewItems, int newCount) -> bool {
  return currentViewItems > 0 && newCount <= currentViewItems;
}

} // namespace NavigationHelpers
