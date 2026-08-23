#include "collectiontreesettings_persistence.h"

#include <algorithm>

#include <QLoggingCategory>

#include "collection/enumstringhelpers.h"
#include "settingskeys.h"

namespace keys = kartend::settings::keys;

// Mirrors the "kartend.settingsmanager" category declared in
// modules/data/settings/settingsmanager.h — utils-layer can't include that
// header (layering); re-declaring with the same string routes through the
// same Qt logging filter (same reasoning as sidebarappearance_persistence).
namespace {
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")

QString positionToString(DetailsPanePosition position) {
  return position == DetailsPanePosition::Right ? QStringLiteral("right") : QStringLiteral("left");
}
} // namespace

namespace CollectionTreeSettingsPersistence {

void load(QSettings &settings, CollectionTreeSettings &tree, const QString &collectionName) {
  tree.treeVisible = settings.value(keys::kCollectionTreeVisible, true).toBool();
  const QString rawPosition =
      settings.value(keys::kCollectionTreePosition, QStringLiteral("left")).toString();
  if (rawPosition == QLatin1String("right")) {
    tree.treePosition = DetailsPanePosition::Right;
  } else {
    tree.treePosition = DetailsPanePosition::Left;
    if (rawPosition != QLatin1String("left")) {
      qCWarning(lcSettingsManager).nospace()
          << "Collection '" << collectionName << "': unknown " << keys::kCollectionTreePosition
          << " value '" << rawPosition
          << "' — the tree panel docks left or right only, falling back to 'left'. Fix the INI "
             "to silence.";
    }
  }
  // Width: clamp rather than warn — a slightly-off value (resized on a
  // different DPI, hand-tweaked INI) has an obvious best interpretation,
  // unlike the enum keys above where a typo means intent is unknown.
  const int rawWidth =
      settings.value(keys::kCollectionTreeWidth, CollectionTreeSettings{}.treeWidth).toInt();
  tree.treeWidth =
      std::clamp(rawWidth, CollectionTreeSettings::kMinWidth, CollectionTreeSettings::kMaxWidth);
  // Kartend-j1mtg. Prefer the tri-state key; fall back to the legacy bool so an
  // existing config keeps the mode its owner chose. true -> IconOnly. false
  // maps to IconAndText, NOT TextOnly: false used to mean "icon, and the name
  // silently dropped by the delegate", so IconAndText is what that user was
  // reaching for and it keeps their artwork. Nobody loses an icon on upgrade.
  if (settings.contains(keys::kCollectionTreeIconDisplay)) {
    tree.treeIconDisplay = CollectionUtils::stringToTreeIconDisplay(
        settings.value(keys::kCollectionTreeIconDisplay).toString());
  } else if (settings.contains(keys::kCollectionTreeIconsOnly)) {
    tree.treeIconDisplay = settings.value(keys::kCollectionTreeIconsOnly).toBool()
                               ? TreeIconDisplay::IconOnly
                               : TreeIconDisplay::IconAndText;
  } else {
    tree.treeIconDisplay = CollectionTreeSettings{}.treeIconDisplay;
  }
  tree.treeShowLines =
      settings.value(keys::kCollectionTreeShowLines, CollectionTreeSettings{}.treeShowLines)
          .toBool();
  tree.treeScrollClippedLabels = settings
                                     .value(keys::kCollectionTreeScrollClippedLabels,
                                            CollectionTreeSettings{}.treeScrollClippedLabels)
                                     .toBool();
  tree.treeScrollClippedLabelsOnHover =
      settings
          .value(keys::kCollectionTreeScrollClippedLabelsOnHover,
                 CollectionTreeSettings{}.treeScrollClippedLabelsOnHover)
          .toBool();
  tree.treeColorizeSelected = settings
                                  .value(keys::kCollectionTreeColorizeSelected,
                                         CollectionTreeSettings{}.treeColorizeSelected)
                                  .toBool();
  // String, with legacy "true"/"false" accepted — see the grid variant.
  tree.treeScrollbarMode = CollectionUtils::stringToScrollbarMode(
      settings.value(keys::kCollectionTreeHideScrollbar).toString());
  const int rawIconSize =
      settings.value(keys::kCollectionTreeIconSize, CollectionTreeSettings{}.treeIconSize).toInt();
  tree.treeIconSize = std::clamp(rawIconSize, CollectionTreeSettings::kMinIconSize,
                                 CollectionTreeSettings::kMaxIconSize);
  bool styleFallback = false;
  tree.treeIconStyle = CollectionUtils::stringToTreeIconStyle(
      settings.value(keys::kCollectionTreeIconStyle, QStringLiteral("normal")).toString(),
      &styleFallback);
  if (styleFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kCollectionTreeIconStyle
        << " value — falling back to 'normal'. Fix the INI to silence.";
  }
  // Free-form hex; consumers validate via QColor and fall back to the accent
  // on garbage, so no clamp here beyond trimming.
  tree.treeIconTintColor = settings.value(keys::kCollectionTreeIconTint).toString().trimmed();
  // Absent key defaults to FULL-HEIGHT (the struct default; user decision
  // 2026-08-17) — an UNKNOWN value still clamps to below-toolbar via the
  // shared string helper, whose fallback is deliberately the conservative
  // classic layout.
  // Same "fixed"/"overlay" vocabulary the details pane uses (see
  // sidebarappearance_persistence), so the INI reads consistently across the
  // two panes. Absent defaults to "fixed" — the docked behaviour every
  // existing collection already has.
  tree.treeMode = (settings.value(keys::kCollectionTreeMode, "fixed").toString() == "overlay")
                      ? DetailsPaneMode::Overlay
                      : DetailsPaneMode::Expand;
  bool justificationFallback = false;
  tree.treeJustification = CollectionUtils::stringToSidebarJustification(
      settings.value(keys::kCollectionTreeJustification, QStringLiteral("full-height")).toString(),
      &justificationFallback);
  if (justificationFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kCollectionTreeJustification
        << " value — falling back to 'below-toolbar'. Fix the INI to silence.";
  }
}

void save(QSettings &settings, const CollectionTreeSettings &tree) {
  settings.setValue(keys::kCollectionTreeVisible, tree.treeVisible);
  settings.setValue(keys::kCollectionTreePosition, positionToString(tree.treePosition));
  settings.setValue(keys::kCollectionTreeWidth, tree.treeWidth);
  // Kartend-j1mtg: the legacy bool is REMOVED, not merely left unwritten.
  // Not writing it is not enough — an existing kartend.cfg already contains
  // it, so it would sit beside the new key forever, read by nothing and
  // contradicting it for anyone inspecting the file (a config showing
  // collectionTreeIconsOnly=false next to collectionTreeIconDisplay=icon-only
  // is actively misleading). Migration happens on load; this is the other half.
  settings.remove(keys::kCollectionTreeIconsOnly);
  settings.setValue(keys::kCollectionTreeIconDisplay,
                    CollectionUtils::treeIconDisplayToString(tree.treeIconDisplay));
  settings.setValue(keys::kCollectionTreeShowLines, tree.treeShowLines);
  settings.setValue(keys::kCollectionTreeScrollClippedLabels, tree.treeScrollClippedLabels);
  settings.setValue(keys::kCollectionTreeScrollClippedLabelsOnHover,
                    tree.treeScrollClippedLabelsOnHover);
  settings.setValue(keys::kCollectionTreeColorizeSelected, tree.treeColorizeSelected);
  settings.setValue(keys::kCollectionTreeHideScrollbar,
                    CollectionUtils::scrollbarModeToString(tree.treeScrollbarMode));
  settings.setValue(keys::kCollectionTreeIconSize, tree.treeIconSize);
  settings.setValue(keys::kCollectionTreeIconStyle,
                    CollectionUtils::treeIconStyleToString(tree.treeIconStyle));
  settings.setValue(keys::kCollectionTreeIconTint, tree.treeIconTintColor);
  settings.setValue(keys::kCollectionTreeJustification,
                    CollectionUtils::sidebarJustificationToString(tree.treeJustification));
  settings.setValue(keys::kCollectionTreeMode,
                    (tree.treeMode == DetailsPaneMode::Overlay) ? "overlay" : "fixed");
}

} // namespace CollectionTreeSettingsPersistence
