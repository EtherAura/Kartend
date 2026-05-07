#ifndef UICONSTANTS_ICONS_H
#define UICONSTANTS_ICONS_H

#include <initializer_list>
#include <QIcon>
#include <QString>

namespace UIConstants {

// =============================================================================
// Theme Icons
// Breeze/Plasma icon names for visual indicators.
// Use with QIcon::fromTheme() or UIConstants::Icons::fromTheme().
// =============================================================================
namespace Icons {
/// Folder icon for collections (Breeze: "folder")
inline constexpr const char *FOLDER = "folder";
/// Open folder icon for subcollections (Breeze: "folder-open")
inline constexpr const char *SUBCOLLECTION = "folder-open";
/// Card file box icon for virtual folders (Breeze: "folder-documents")
inline constexpr const char *VIRTUAL_FOLDER = "folder-documents";
/// Magnifying glass for search — used as the single in-field search-mode
/// action icon regardless of mode (Breeze: "search").
inline constexpr const char *SEARCH = "search";
/// Globe for global search mode (Breeze: "internet-services")
inline constexpr const char *GLOBE = "internet-services";
/// Image/picture icon for artwork preview (Breeze: "view-preview")
inline constexpr const char *IMAGE = "view-preview";
/// Hamburger / application menu (Breeze: "application-menu")
inline constexpr const char *MENU = "application-menu";
/// Filter glyph for collection-categorization toolbar buttons
/// (Breeze: "filter-symbolic" with "view-filter" fallback).
inline constexpr const char *FILTER = "filter-symbolic";
/// Layout / view-mode picker (Breeze: "view-choose")
inline constexpr const char *VIEW_PICKER = "view-choose";

/// Get a themed icon with fallback support
/// @param names List of icon names to try in order
/// @param fallback Optional fallback text if no icon found
/// @return The first available icon, or empty icon if none found
inline QIcon fromTheme(std::initializer_list<const char *> names) {
  for (const char *name : names) {
    QIcon icon = QIcon::fromTheme(QString::fromUtf8(name));
    if (!icon.isNull()) {
      return icon;
    }
  }
  return {};
}

/// Get a themed icon by single name
inline QIcon fromTheme(const char *name) {
  return QIcon::fromTheme(QString::fromUtf8(name));
}
} // namespace Icons
} // namespace UIConstants

#endif
