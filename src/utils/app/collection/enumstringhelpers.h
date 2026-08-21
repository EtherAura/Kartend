#ifndef KARTEND_COLLECTION_ENUMSTRINGHELPERS_H
#define KARTEND_COLLECTION_ENUMSTRINGHELPERS_H

// CollectionUtils enum ↔ string converters. Split out of the former
// collection/helpers.h grab-bag so the (many) INI-persistence call sites that
// only need enum<->string conversion don't drag in collectionconfig.h or the
// hierarchy/type declarations. Depends only on the enum definitions.

#include "collectiontypes.h"

#include <QString>

namespace CollectionUtils {

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

/// @p unknownFallback (optional) is set to true when @p str is non-empty but
/// doesn't match any recognized value — lets the caller warn about the silent
/// default. Mirrors stringToDetailsPanePosition (Kartend-hp2y / Kartend-1vie).
[[nodiscard]] inline HorizontalAlignment stringToAlignment(const QString &str,
                                                           bool *unknownFallback = nullptr) {
  QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "left") return HorizontalAlignment::Left;
  if (lower == "right") return HorizontalAlignment::Right;
  if (lower == "center") return HorizontalAlignment::Center;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
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

[[nodiscard]] inline ViewType stringToViewType(const QString &str,
                                               bool *unknownFallback = nullptr) {
  QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "list") return ViewType::List;
  if (lower == "coverflow") return ViewType::CoverFlow;
  if (lower == "horizontal") return ViewType::Horizontal;
  if (lower == "grid") return ViewType::Grid;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
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

[[nodiscard]] inline HeaderLogoPosition
stringToHeaderLogoPosition(const QString &str, bool *unknownFallback = nullptr) {
  const QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "topleft") return HeaderLogoPosition::TopLeft;
  if (lower == "topright") return HeaderLogoPosition::TopRight;
  if (lower == "topcenter") return HeaderLogoPosition::TopCenter;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
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

/// @p unknownFallback (optional) is set to true when @p str is non-empty but
/// doesn't match any recognized position, so the caller can warn about the
/// silent default — INI typos used to swap the user's sidebar with no
/// feedback (Kartend-hp2y). Pass nullptr to opt out.
[[nodiscard]] inline DetailsPanePosition
stringToDetailsPanePosition(const QString &str, bool *unknownFallback = nullptr) {
  const QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "left") return DetailsPanePosition::Left;
  if (lower == "top") return DetailsPanePosition::Top;
  if (lower == "bottom") return DetailsPanePosition::Bottom;
  if (lower == "right") return DetailsPanePosition::Right;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
  return DetailsPanePosition::Right;
}

/// true for Top/Bottom dock — tells layout/drag code to treat the
/// pane's height (not width) as the configurable dimension and to span the full
/// viewport perpendicular to the dock edge.
[[nodiscard]] inline bool isDetailsPaneHorizontal(DetailsPanePosition pos) {
  return pos == DetailsPanePosition::Top || pos == DetailsPanePosition::Bottom;
}

/// Kartend-auh7u: one vocabulary for the sidebar justification everywhere it
/// round-trips (INI blocks for both panels, the kart manifest JSON, and the
/// Sidebars settings page).
[[nodiscard]] inline QString sidebarJustificationToString(SidebarJustification justification) {
  return justification == SidebarJustification::FullHeight ? QStringLiteral("full-height")
                                                           : QStringLiteral("below-toolbar");
}

/// Mirrors stringToDetailsPanePosition's fallback contract: @p unknownFallback
/// (optional) reports a non-empty unrecognized value so callers can warn
/// instead of silently defaulting.
[[nodiscard]] inline SidebarJustification
stringToSidebarJustification(const QString &str, bool *unknownFallback = nullptr) {
  const QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "full-height") return SidebarJustification::FullHeight;
  if (lower == "below-toolbar") return SidebarJustification::BelowToolbar;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
  return SidebarJustification::BelowToolbar;
}

[[nodiscard]] [[nodiscard]] inline QString toolbarColorSourceToString(ToolbarColorSource source) {
  switch (source) {
  case ToolbarColorSource::Titlebar:
    return QStringLiteral("titlebar");
  case ToolbarColorSource::Accent:
    return QStringLiteral("accent");
  case ToolbarColorSource::Highlight:
    return QStringLiteral("highlight");
  case ToolbarColorSource::CollectionPrimary:
    break;
  }
  return QStringLiteral("collection");
}

[[nodiscard]] inline ToolbarColorSource stringToToolbarColorSource(const QString &value,
                                                                   bool *fellBack = nullptr) {
  const QString lower = value.trimmed().toLower();
  if (fellBack) *fellBack = false;
  if (lower == QLatin1String("titlebar")) return ToolbarColorSource::Titlebar;
  if (lower == QLatin1String("accent")) return ToolbarColorSource::Accent;
  if (lower == QLatin1String("highlight")) return ToolbarColorSource::Highlight;
  if (lower == QLatin1String("collection")) return ToolbarColorSource::CollectionPrimary;
  if (fellBack) *fellBack = !lower.isEmpty();
  return ToolbarColorSource::Titlebar;
}

inline QString scrollbarModeToString(ScrollbarMode mode) {
  switch (mode) {
  case ScrollbarMode::Autohide:
    return QStringLiteral("autohide");
  case ScrollbarMode::Hide:
    return QStringLiteral("hide");
  case ScrollbarMode::Show:
    break;
  }
  return QStringLiteral("show");
}

/// Also accepts the legacy BOOL spelling. Every one of these keys was a
/// hide-yes/no bool before 2026-08-19, and QSettings wrote them as
/// "true"/"false", so a config from any earlier build lands here and has to
/// map onto the two states that existed then — true meant hidden.
/// Deliberately not flagged as an unknown-value fallback: a legacy bool is a
/// correct old config, not a typo, and must not log a warning.
[[nodiscard]] inline ScrollbarMode stringToScrollbarMode(const QString &str,
                                                         bool *unknownFallback = nullptr) {
  const QString lower = str.trimmed().toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == QLatin1String("autohide")) return ScrollbarMode::Autohide;
  if (lower == QLatin1String("hide") || lower == QLatin1String("true")) return ScrollbarMode::Hide;
  if (lower == QLatin1String("show") || lower == QLatin1String("false")) return ScrollbarMode::Show;
  if (unknownFallback && !lower.isEmpty()) *unknownFallback = true;
  return ScrollbarMode::Show;
}

inline QString treeIconStyleToString(TreeIconStyle style) {
  switch (style) {
  case TreeIconStyle::MonochromeDark:
    return QStringLiteral("monochrome-dark");
  case TreeIconStyle::MonochromeLight:
    return QStringLiteral("monochrome-light");
  case TreeIconStyle::Tinted:
    return QStringLiteral("tinted");
  case TreeIconStyle::Normal:
    break;
  }
  return QStringLiteral("normal");
}

[[nodiscard]] inline TreeIconStyle stringToTreeIconStyle(const QString &str,
                                                         bool *unknownFallback = nullptr) {
  const QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "monochrome-dark") return TreeIconStyle::MonochromeDark;
  if (lower == "monochrome-light") return TreeIconStyle::MonochromeLight;
  if (lower == "tinted") return TreeIconStyle::Tinted;
  if (lower == "normal") return TreeIconStyle::Normal;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
  return TreeIconStyle::Normal;
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
stringToDetailsPaneBackgroundType(const QString &str, bool *unknownFallback = nullptr) {
  const QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "image") return DetailsPaneBackgroundType::Image;
  if (lower == "pattern") return DetailsPaneBackgroundType::Pattern;
  if (lower == "color") return DetailsPaneBackgroundType::Color;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
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

[[nodiscard]] inline DetailsPaneTab stringToDetailsPaneTab(const QString &str,
                                                           bool *unknownFallback = nullptr) {
  const QString lower = str.toLower();
  if (unknownFallback) *unknownFallback = false;
  if (lower == "collection") return DetailsPaneTab::Collection;
  if (lower == "file") return DetailsPaneTab::File;
  if (lower == "item") return DetailsPaneTab::Item;
  if (unknownFallback && !str.isEmpty()) *unknownFallback = true;
  return DetailsPaneTab::Item;
}

} // namespace CollectionUtils

#endif // KARTEND_COLLECTION_ENUMSTRINGHELPERS_H
