#ifndef KARTEND_COLLECTION_HELPERS_H
#define KARTEND_COLLECTION_HELPERS_H

// Kartend-ysyn: the standalone CollectionUtils helpers (enum<->string
// converters, index validation, grid sizing, virtual-folder counting) lived
// inside collectionutils.h as inline bodies and forced ~113 TUs to re-parse
// 1.8K LOC of unrelated material on every settings touch. They're consumed
// independently of the CollectionConfig god-struct, so they peel out cleanly.
// collectionutils.h re-includes this header so existing call sites compile
// unchanged.

#include "collectiontypes.h"

#include <QList>
#include <QString>

#include <uiconstants/detailspaneconstants.h>
#include <uiconstants/grid.h>
#include <uiconstants/item.h>
#include <uiconstants/listview.h>

#include "collectionconfig.h"

namespace CollectionUtils {

// ─── Enum ↔ string converters ────────────────────────────────────────────────

[[nodiscard]] inline QString alignmentToString(HorizontalAlignment alignment) {
  switch (alignment) {
  case HorizontalAlignment::Left:
    return "left";
  case HorizontalAlignment::Center:
    return "center";
  case HorizontalAlignment::Right:
    return "right";
  default:
    return "center";
  }
}

[[nodiscard]] inline HorizontalAlignment stringToAlignment(const QString &str) {
  QString lower = str.toLower();
  if (lower == "left") return HorizontalAlignment::Left;
  if (lower == "right") return HorizontalAlignment::Right;
  return HorizontalAlignment::Center;
}

[[nodiscard]] inline QString viewTypeToString(ViewType viewType) {
  switch (viewType) {
  case ViewType::List:
    return "list";
  case ViewType::CoverFlow:
    return "coverflow";
  case ViewType::Horizontal:
    return "horizontal";
  case ViewType::Grid:
  default:
    return "grid";
  }
}

[[nodiscard]] inline ViewType stringToViewType(const QString &str) {
  QString lower = str.toLower();
  if (lower == "list") return ViewType::List;
  if (lower == "coverflow") return ViewType::CoverFlow;
  if (lower == "horizontal") return ViewType::Horizontal;
  return ViewType::Grid;
}

[[nodiscard]] inline QString headerLogoPositionToString(HeaderLogoPosition pos) {
  switch (pos) {
  case HeaderLogoPosition::TopLeft:
    return "topleft";
  case HeaderLogoPosition::TopRight:
    return "topright";
  case HeaderLogoPosition::TopCenter:
  default:
    return "topcenter";
  }
}

[[nodiscard]] inline HeaderLogoPosition stringToHeaderLogoPosition(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "topleft") return HeaderLogoPosition::TopLeft;
  if (lower == "topright") return HeaderLogoPosition::TopRight;
  return HeaderLogoPosition::TopCenter;
}

[[nodiscard]] inline QString detailsPanePositionToString(DetailsPanePosition pos) {
  switch (pos) {
  case DetailsPanePosition::Left:
    return "left";
  case DetailsPanePosition::Top:
    return "top";
  case DetailsPanePosition::Bottom:
    return "bottom";
  case DetailsPanePosition::Right:
  default:
    return "right";
  }
}

[[nodiscard]] inline DetailsPanePosition stringToDetailsPanePosition(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "left") return DetailsPanePosition::Left;
  if (lower == "top") return DetailsPanePosition::Top;
  if (lower == "bottom") return DetailsPanePosition::Bottom;
  return DetailsPanePosition::Right;
}

/// true for Top/Bottom dock — tells layout/drag code to treat the
/// pane's height (not width) as the configurable dimension and to span the full
/// viewport perpendicular to the dock edge.
[[nodiscard]] inline bool isDetailsPaneHorizontal(DetailsPanePosition pos) {
  return pos == DetailsPanePosition::Top || pos == DetailsPanePosition::Bottom;
}

[[nodiscard]] inline QString detailsPaneBackgroundTypeToString(DetailsPaneBackgroundType type) {
  switch (type) {
  case DetailsPaneBackgroundType::Image:
    return "image";
  case DetailsPaneBackgroundType::Pattern:
    return "pattern";
  case DetailsPaneBackgroundType::Color:
  default:
    return "color";
  }
}

[[nodiscard]] inline DetailsPaneBackgroundType
stringToDetailsPaneBackgroundType(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "image") return DetailsPaneBackgroundType::Image;
  if (lower == "pattern") return DetailsPaneBackgroundType::Pattern;
  return DetailsPaneBackgroundType::Color;
}

[[nodiscard]] inline QString detailsPanePatternToString(DetailsPanePattern pattern) {
  switch (pattern) {
  case DetailsPanePattern::Crosshatch:
  default:
    return "crosshatch";
  }
}

[[nodiscard]] inline DetailsPanePattern stringToDetailsPanePattern(const QString &str) {
  // Single value for now; future patterns slot in here without persistence breakage.
  Q_UNUSED(str);
  return DetailsPanePattern::Crosshatch;
}

[[nodiscard]] inline QString detailsPaneTabToString(DetailsPaneTab tab) {
  switch (tab) {
  case DetailsPaneTab::Collection:
    return "collection";
  case DetailsPaneTab::File:
    return "file";
  case DetailsPaneTab::Item:
  default:
    return "item";
  }
}

[[nodiscard]] inline DetailsPaneTab stringToDetailsPaneTab(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "collection") return DetailsPaneTab::Collection;
  if (lower == "file") return DetailsPaneTab::File;
  return DetailsPaneTab::Item;
}

// ─── Index validation helpers ────────────────────────────────────────────────

/// Validates index against collection pointer (null-safe)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> *collections) {
  return collections && index >= 0 && index < collections->size();
}

/// Validates index pointer against collection pointer (null-safe for both)
[[nodiscard]] inline bool isValidIndex(const int *indexPtr,
                                       const QList<CollectionConfig> *collections) {
  return indexPtr && isValidIndex(*indexPtr, collections);
}

/// True when the index points at a real collection OR the synthetic
/// root / home view (index -1). The home view renders the root
/// collections as tiles but has no backing collection; input handlers
/// gate on this (instead of isValidIndex) so it still accepts keyboard
/// + mouse interaction. Callers that then read a per-collection
/// property must still guard that access with isValidIndex — only the
/// outer "is this an interactive view" gate is widened.
[[nodiscard]] inline bool isInteractiveViewIndex(const int *indexPtr,
                                                 const QList<CollectionConfig> *collections) {
  return isValidIndex(indexPtr, collections) || (indexPtr && *indexPtr == -1);
}

/// Validates index against collection reference (no null check needed)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> &collections) {
  return index >= 0 && index < collections.size();
}

// ─── Collection property accessors with validation ───────────────────────────

/// Get grid width with fallback to default - reduces duplication across
/// managers
[[nodiscard]] inline int getGridWidth(const int *indexPtr,
                                      const QList<CollectionConfig> *collections) {
  if (!isValidIndex(indexPtr, collections)) {
    return UIConstants::Grid::DEFAULT_WIDTH;
  }
  return (*collections)[*indexPtr].gridLayout.gridWidth;
}

// ─── Effective grid sizing helpers ───────────────────────────────────────────
//
// "Sidebar shrinking active" is the predicate captured by the caller: sidebar
// is currently hidden AND the collection's sidebarMode is Expand (i.e. the
// sidebar would push the grid when shown). Overlay mode never shrinks, so we
// always use the primary value there. The alt fields default to 0, which means
// "inherit the primary" — a fresh upgrade keeps existing layout behavior.

[[nodiscard]] inline int effectiveGridWidth(const CollectionConfig &config,
                                            bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridLayout.gridWidthSidebarHidden > 0) {
    return config.gridLayout.gridWidthSidebarHidden;
  }
  return config.gridLayout.gridWidth;
}

[[nodiscard]] inline int effectiveHorizontalGridHeight(const CollectionConfig &config,
                                                       bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridLayout.horizontalGridHeightSidebarHidden > 0) {
    return config.gridLayout.horizontalGridHeightSidebarHidden;
  }
  return config.gridLayout.horizontalGridHeight;
}

// ─── Virtual folder counting ─────────────────────────────────────────────────

/// Count virtual folders (subdirectories) for a collection config.
/// Returns 0 if includeContentSubfolders is disabled or showAllSubfolderItems
/// is enabled. Definition lives in helpers.cpp because it touches QDir +
/// PathUtils, which would balloon the include cost if dragged into this
/// header.
[[nodiscard]] int countVirtualFolders(const CollectionConfig &config);

} // namespace CollectionUtils

#endif // KARTEND_COLLECTION_HELPERS_H
