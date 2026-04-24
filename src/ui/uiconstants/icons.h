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
/// Folder icon for collections
inline constexpr const char *FOLDER = "folder";
/// Open folder icon for subcollections
inline constexpr const char *SUBCOLLECTION = "folder-open";
/// Card file box icon for virtual folders (subfolder navigation)
inline constexpr const char *VIRTUAL_FOLDER = "folder-documents";
/// Magnifying glass for search
inline constexpr const char *SEARCH = "edit-find";
/// Search current collection only
inline constexpr const char *SEARCH_LOCAL = "edit-find";
/// Search current + subcollections
inline constexpr const char *SEARCH_SUBCOLLECTIONS = "folder-saved-search";
/// Search all collections (global)
inline constexpr const char *SEARCH_GLOBAL = "system-search";
/// Globe for global search mode
inline constexpr const char *GLOBE = "internet-services";
/// Image/picture icon for artwork preview
inline constexpr const char *IMAGE = "view-preview";

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
