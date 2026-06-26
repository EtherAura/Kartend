#include "kartmanifest.h"
#include "collection/collectionconfig.h"
#include "collection/enumstringhelpers.h"
#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"
#include "extensionutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <uiconstants/detailspaneconstants.h>
#include <uiconstants/grid.h>
#include <uiconstants/item.h>
#include <uiconstants/listview.h>

namespace KartManifest {

namespace {

// Kartend-1vie: surface unknown enum strings in imported .kart manifests so a
// cross-machine import doesn't silently fall back to defaults. Reuse the
// settingsmanager category so users only have to enable one logging filter
// to debug both INI-side and manifest-side enum typos.
Q_LOGGING_CATEGORY(lcKartManifest, "kartend.settingsmanager")

QString sidebarModeToString(DetailsPaneMode mode) {
  return mode == DetailsPaneMode::Expand ? "expand" : "overlay";
}

DetailsPaneMode stringToSidebarMode(const QString &s) {
  return s.toLower() == "expand" ? DetailsPaneMode::Expand : DetailsPaneMode::Overlay;
}

QString backgroundTypeToString(BackgroundType type) {
  switch (type) {
  case BackgroundType::Image:
    return "image";
  case BackgroundType::Video:
    return "video";
  case BackgroundType::Color:
  default:
    return "color";
  }
}

BackgroundType stringToBackgroundType(const QString &s) {
  const QString l = s.toLower();
  if (l == "image") return BackgroundType::Image;
  if (l == "video") return BackgroundType::Video;
  return BackgroundType::Color;
}

QJsonArray stringListToJson(const QStringList &list) {
  QJsonArray arr;
  for (const QString &s : list) {
    arr.append(s);
  }
  return arr;
}

QStringList jsonToStringList(const QJsonValue &v) {
  QStringList out;
  if (!v.isArray()) {
    return out;
  }
  const QJsonArray arr = v.toArray();
  for (const auto &item : arr) {
    out.append(item.toString());
  }
  return out;
}

QJsonObject launcherConfigToJson(const LauncherConfig &lc) {
  QJsonObject o;
  o["name"] = lc.name;
  o["launcher_path"] = lc.launcherPath;
  o["core_path"] = lc.corePath;
  o["launch_parameters"] = lc.launchParameters;
  o["preset_id"] = lc.presetId;
  return o;
}

LauncherConfig jsonToLauncherConfig(const QJsonObject &o) {
  LauncherConfig lc;
  lc.name = o["name"].toString();
  lc.launcherPath = o["launcher_path"].toString();
  lc.corePath = o["core_path"].toString();
  lc.launchParameters = o["launch_parameters"].toString();
  lc.presetId = o["preset_id"].toString();
  return lc;
}

QJsonObject launcherPresetToJson(const LauncherPreset &p) {
  QJsonObject o;
  o["id"] = p.id;
  o["name"] = p.name;
  o["launcher_path"] = p.launcherPath;
  o["core_path"] = p.corePath;
  o["launch_parameters"] = p.launchParameters;
  return o;
}

LauncherPreset jsonToLauncherPreset(const QJsonObject &o) {
  LauncherPreset p;
  p.id = o["id"].toString();
  p.name = o["name"].toString();
  p.launcherPath = o["launcher_path"].toString();
  p.corePath = o["core_path"].toString();
  p.launchParameters = o["launch_parameters"].toString();
  return p;
}

QJsonObject collectionConfigToJson(const CollectionConfig &c) {
  QJsonObject o;
  o["name"] = c.name;
  o["type"] = c.type;
  o["launcher_path"] = c.launcher.launcherPath;
  o["core_path"] = c.launcher.corePath;
  o["launch_parameters"] = c.launcher.launchParameters;
  o["launcher_name"] = c.launcher.launcherName;

  QJsonArray addl;
  for (const LauncherConfig &lc : c.launcher.additionalLaunchers) {
    addl.append(launcherConfigToJson(lc));
  }
  o["additional_launchers"] = addl;
  o["default_launcher_index"] = c.launcher.defaultLauncherIndex;

  o["media_directory"] = c.mediaDirectory;
  o["artwork_directory"] = c.artworkDirectory;
  o["video_directory"] = c.videoDirectory;
  o["manual_directory"] = c.manualDirectory;
  o["placeholder_artwork"] = c.placeholderArtwork;
  o["collection_icon"] = c.collectionIcon;
  o["extensions"] = stringListToJson(c.extensions);
  o["custom_artwork_types"] = stringListToJson(c.customArtworkTypes);

  o["grid_width"] = c.gridLayout.gridWidth;
  o["horizontal_grid_height"] = c.gridLayout.horizontalGridHeight;
  o["grid_width_sidebar_hidden"] = c.gridLayout.gridWidthSidebarHidden;
  o["horizontal_grid_height_sidebar_hidden"] = c.gridLayout.horizontalGridHeightSidebarHidden;
  // alt items-per-column for Top/Bottom-pane-shrink case.
  o["grid_height_sidebar_hidden"] = c.gridLayout.gridHeightSidebarHidden;
  o["sidebar_visible"] = c.sidebar.sidebarVisible;
  o["parent_collection_index"] = c.parentCollectionIndex;
  o["is_subcollection"] = c.isSubcollection;
  o["show_all_subcollection_items"] = c.showAllSubcollectionItems;
  o["hide_titles"] = c.hideTitles;
  o["hide_subcollection_titles"] = c.hideSubcollectionTitles;
  o["title_exclusion_patterns"] = stringListToJson(c.filter.titleExclusionPatterns);
  o["title_exclusion_enabled"] = c.filter.titleExclusionEnabled;

  o["horizontal_alignment"] = CollectionUtils::alignmentToString(c.horizontalAlignment);
  o["sidebar_mode"] = sidebarModeToString(c.sidebar.sidebarMode);
  o["sidebar_position"] = CollectionUtils::detailsPanePositionToString(c.sidebar.sidebarPosition);
  o["sidebar_background_type"] =
      CollectionUtils::detailsPaneBackgroundTypeToString(c.sidebar.sidebarBackgroundType);
  o["sidebar_background_color"] = c.sidebar.sidebarBackgroundColor;
  o["sidebar_background_image"] = c.sidebar.sidebarBackgroundImage;
  o["sidebar_pattern"] = CollectionUtils::detailsPanePatternToString(c.sidebar.sidebarPattern);
  o["sidebar_pattern_intensity"] = c.sidebar.sidebarPatternIntensity;
  o["sidebar_pattern_color"] = c.sidebar.sidebarPatternColor;
  o["sidebar_text_color"] = c.sidebar.sidebarTextColor;
  o["sidebar_accent_color"] = c.sidebar.sidebarAccentColor;
  o["sidebar_header_bg_color"] = c.sidebar.sidebarHeaderBgColor;
  o["sidebar_section_bg_color"] = c.sidebar.sidebarSectionBgColor;
  o["sidebar_header_bg_opacity"] = c.sidebar.sidebarHeaderBgOpacity;
  o["sidebar_section_bg_opacity"] = c.sidebar.sidebarSectionBgOpacity;
  o["sidebar_width"] = c.sidebar.sidebarWidth;
  // pane height for Top/Bottom dock.
  o["sidebar_height"] = c.sidebar.sidebarHeight;
  o["sidebar_width_locked"] = c.sidebar.sidebarWidthLocked;
  o["sidebar_active_tab"] = CollectionUtils::detailsPaneTabToString(c.sidebar.sidebarActiveTab);
  o["sidebar_font_family"] = c.sidebar.sidebarFontFamily;
  o["sidebar_font_point_size"] = c.sidebar.sidebarFontPointSize;

  o["view_type"] = CollectionUtils::viewTypeToString(c.viewType);
  o["hide_missing_artwork"] = c.hideMissingArtwork;
  o["horizontal_spacing"] = c.gridLayout.horizontalSpacing;
  o["vertical_spacing"] = c.gridLayout.verticalSpacing;
  o["hide_horizontal_scrollbar"] = c.gridLayout.hideHorizontalScrollbar;
  o["hide_vertical_scrollbar"] = c.gridLayout.hideVerticalScrollbar;
  o["item_width"] = c.gridLayout.itemWidth;
  o["item_height"] = c.gridLayout.itemHeight;
  o["font_size"] = c.gridLayout.fontSize;
  o["corner_radius"] = c.gridLayout.cornerRadius;

  o["background_type"] = backgroundTypeToString(c.background.backgroundType);
  o["background_color"] = c.background.backgroundColor;
  o["background_image"] = c.background.backgroundImage;
  o["background_video"] = c.background.backgroundVideo;
  o["primary_color"] = c.background.primaryColor;
  o["tile_color"] = c.background.tileColor;
  o["selection_color"] = c.background.selectionColor;

  o["header_logo_image"] = c.background.headerLogoImage;
  o["header_logo_position"] =
      CollectionUtils::headerLogoPositionToString(c.background.headerLogoPosition);

  o["vignette_enabled"] = c.background.vignetteEnabled;
  o["vignette_intensity"] = c.background.vignetteIntensity;
  o["wallpaper_parallax"] = c.background.wallpaperParallax;
  o["parallax_strength"] = c.background.parallaxStrength;
  o["toolbar_backdrop_blur"] = c.background.toolbarBackdropBlur;
  o["backdrop_blur_radius"] = c.background.backdropBlurRadius;

  o["extract_archives"] = c.archive.extractArchives;
  o["extracted_extension"] = c.archive.extractedExtension;
  o["expand_mode"] = c.expandMode;

  o["include_content_subfolders"] = c.folderBrowsing.includeContentSubfolders;
  o["include_artwork_subfolders"] = c.folderBrowsing.includeArtworkSubfolders;
  o["show_all_subfolder_items"] = c.folderBrowsing.showAllSubfolderItems;
  o["hide_subfolder_titles"] = c.folderBrowsing.hideSubfolderTitles;
  o["show_hidden_folders"] = c.folderBrowsing.showHiddenFolders;

  o["list_font_size"] = c.listView.listFontSize;
  o["list_row_height"] = c.listView.listRowHeight;
  o["list_row_color"] = c.listView.listRowColor;
  o["list_alt_row_color"] = c.listView.listAltRowColor;
  o["custom_font_family"] = c.customFontFamily;

  o["additional_parent_names"] = stringListToJson(c.additionalParentNames);
  return o;
}

CollectionConfig jsonToCollectionConfig(const QJsonObject &o) {
  CollectionConfig c;
  c.name = o["name"].toString();
  c.type = o["type"].toString();
  c.launcher.launcherPath = o["launcher_path"].toString();
  c.launcher.corePath = o["core_path"].toString();
  c.launcher.launchParameters = o["launch_parameters"].toString();
  c.launcher.launcherName = o["launcher_name"].toString();

  const QJsonArray addlArr = o["additional_launchers"].toArray();
  for (const auto &v : addlArr) {
    c.launcher.additionalLaunchers.append(jsonToLauncherConfig(v.toObject()));
  }
  // Clamp the manifest-supplied index at the import boundary so an out-of-range
  // value from an untrusted .kart can't reach a consumer that forgets to clamp
  // (Kartend-aep2e). launcherCount() >= 1, so the upper bound is never negative.
  const int rawDefaultIndex = o["default_launcher_index"].toInt(0);
  c.launcher.defaultLauncherIndex = qBound(0, rawDefaultIndex, c.launcher.launcherCount() - 1);

  c.mediaDirectory = o["media_directory"].toString();
  c.artworkDirectory = o["artwork_directory"].toString();
  c.videoDirectory = o["video_directory"].toString();
  c.manualDirectory = o["manual_directory"].toString();
  c.placeholderArtwork = o["placeholder_artwork"].toString();
  c.collectionIcon = o["collection_icon"].toString();
  // Older .kart files carry the pre-Kartend-693zb glob form ("*.mp4");
  // normalize on import so the in-memory canonical stays bare.
  c.extensions = ExtensionUtils::normalizeStoredExtensions(jsonToStringList(o["extensions"]));
  c.customArtworkTypes = jsonToStringList(o["custom_artwork_types"]);

  c.gridLayout.gridWidth = o["grid_width"].toInt(4);
  c.gridLayout.horizontalGridHeight = o["horizontal_grid_height"].toInt(0);
  c.gridLayout.gridWidthSidebarHidden = o["grid_width_sidebar_hidden"].toInt(0);
  c.gridLayout.horizontalGridHeightSidebarHidden =
      o["horizontal_grid_height_sidebar_hidden"].toInt(0);
  c.gridLayout.gridHeightSidebarHidden = o["grid_height_sidebar_hidden"].toInt(0);
  c.sidebar.sidebarVisible = o["sidebar_visible"].toBool(false);
  c.parentCollectionIndex = o["parent_collection_index"].toInt(-1);
  c.isSubcollection = o["is_subcollection"].toBool(false);
  c.showAllSubcollectionItems = o["show_all_subcollection_items"].toBool(false);
  c.hideTitles = o["hide_titles"].toBool(false);
  c.hideSubcollectionTitles = o["hide_subcollection_titles"].toBool(false);
  c.filter.titleExclusionPatterns = jsonToStringList(o["title_exclusion_patterns"]);
  c.filter.titleExclusionEnabled = o["title_exclusion_enabled"].toBool(true);

  auto warnUnknown = [&](const QString &key, const QString &raw, const QString &dflt) {
    qCWarning(lcKartManifest).nospace()
        << "Imported .kart collection '" << c.name << "': unknown " << key << " value '" << raw
        << "' — falling back to '" << dflt << "'. Fix the manifest to silence.";
  };

  const QString rawAlignment = o["horizontal_alignment"].toString();
  bool alignmentFallback = false;
  c.horizontalAlignment = CollectionUtils::stringToAlignment(rawAlignment, &alignmentFallback);
  if (alignmentFallback) warnUnknown("horizontal_alignment", rawAlignment, "center");
  c.sidebar.sidebarMode = stringToSidebarMode(o["sidebar_mode"].toString());
  c.sidebar.sidebarPosition =
      CollectionUtils::stringToDetailsPanePosition(o["sidebar_position"].toString());
  const QString rawSidebarBgType = o["sidebar_background_type"].toString();
  bool sidebarBgTypeFallback = false;
  c.sidebar.sidebarBackgroundType =
      CollectionUtils::stringToDetailsPaneBackgroundType(rawSidebarBgType, &sidebarBgTypeFallback);
  if (sidebarBgTypeFallback) warnUnknown("sidebar_background_type", rawSidebarBgType, "color");
  c.sidebar.sidebarBackgroundColor = o["sidebar_background_color"].toString();
  c.sidebar.sidebarBackgroundImage = o["sidebar_background_image"].toString();
  c.sidebar.sidebarPattern =
      CollectionUtils::stringToDetailsPanePattern(o["sidebar_pattern"].toString());
  c.sidebar.sidebarPatternIntensity = o["sidebar_pattern_intensity"].toInt(50);
  c.sidebar.sidebarPatternColor = o["sidebar_pattern_color"].toString();
  c.sidebar.sidebarTextColor = o["sidebar_text_color"].toString();
  c.sidebar.sidebarAccentColor = o["sidebar_accent_color"].toString();
  c.sidebar.sidebarHeaderBgColor = o["sidebar_header_bg_color"].toString();
  c.sidebar.sidebarSectionBgColor = o["sidebar_section_bg_color"].toString();
  c.sidebar.sidebarHeaderBgOpacity = o["sidebar_header_bg_opacity"].toInt(200);
  c.sidebar.sidebarSectionBgOpacity = o["sidebar_section_bg_opacity"].toInt(170);
  c.sidebar.sidebarWidth = o["sidebar_width"].toInt(UIConstants::DetailsPane::FIXED_WIDTH);
  c.sidebar.sidebarHeight = o["sidebar_height"].toInt(UIConstants::DetailsPane::FIXED_HEIGHT);
  c.sidebar.sidebarWidthLocked = o["sidebar_width_locked"].toBool(true);
  const QString rawSidebarActiveTab = o["sidebar_active_tab"].toString();
  bool sidebarActiveTabFallback = false;
  c.sidebar.sidebarActiveTab =
      CollectionUtils::stringToDetailsPaneTab(rawSidebarActiveTab, &sidebarActiveTabFallback);
  if (sidebarActiveTabFallback) warnUnknown("sidebar_active_tab", rawSidebarActiveTab, "item");
  c.sidebar.sidebarFontFamily = o["sidebar_font_family"].toString();
  c.sidebar.sidebarFontPointSize = o["sidebar_font_point_size"].toInt(0);

  const QString rawViewType = o["view_type"].toString();
  bool viewTypeFallback = false;
  c.viewType = CollectionUtils::stringToViewType(rawViewType, &viewTypeFallback);
  if (viewTypeFallback) warnUnknown("view_type", rawViewType, "grid");
  c.hideMissingArtwork = o["hide_missing_artwork"].toBool(false);
  c.gridLayout.horizontalSpacing = o["horizontal_spacing"].toInt(UIConstants::Grid::SPACING);
  c.gridLayout.verticalSpacing = o["vertical_spacing"].toInt(20);
  c.gridLayout.hideHorizontalScrollbar = o["hide_horizontal_scrollbar"].toBool(false);
  c.gridLayout.hideVerticalScrollbar = o["hide_vertical_scrollbar"].toBool(false);
  c.gridLayout.itemWidth = o["item_width"].toInt(UIConstants::Item::DEFAULT_WIDTH);
  c.gridLayout.itemHeight = o["item_height"].toInt(UIConstants::Item::DEFAULT_HEIGHT);
  c.gridLayout.fontSize = o["font_size"].toInt(UIConstants::Item::DEFAULT_FONT_SIZE);
  c.gridLayout.cornerRadius = o["corner_radius"].toInt(UIConstants::Item::DEFAULT_CORNER_RADIUS);

  c.background.backgroundType = stringToBackgroundType(o["background_type"].toString());
  c.background.backgroundColor = o["background_color"].toString();
  c.background.backgroundImage = o["background_image"].toString();
  c.background.backgroundVideo = o["background_video"].toString();
  c.background.primaryColor = o["primary_color"].toString();
  c.background.tileColor = o["tile_color"].toString();
  c.background.selectionColor = o["selection_color"].toString();

  c.background.headerLogoImage = o["header_logo_image"].toString();
  const QString rawHeaderLogoPosition = o["header_logo_position"].toString();
  bool headerLogoPositionFallback = false;
  c.background.headerLogoPosition = CollectionUtils::stringToHeaderLogoPosition(
      rawHeaderLogoPosition, &headerLogoPositionFallback);
  if (headerLogoPositionFallback)
    warnUnknown("header_logo_position", rawHeaderLogoPosition, "topcenter");

  c.background.vignetteEnabled = o["vignette_enabled"].toBool(false);
  c.background.vignetteIntensity = o["vignette_intensity"].toInt(60);
  c.background.wallpaperParallax = o["wallpaper_parallax"].toBool(false);
  c.background.parallaxStrength = o["parallax_strength"].toInt(30);
  c.background.toolbarBackdropBlur = o["toolbar_backdrop_blur"].toBool(false);
  c.background.backdropBlurRadius = o["backdrop_blur_radius"].toInt(12);

  c.archive.extractArchives = o["extract_archives"].toBool(false);
  c.archive.extractedExtension = o["extracted_extension"].toString();
  c.expandMode = o["expand_mode"].toBool(false);

  c.folderBrowsing.includeContentSubfolders = o["include_content_subfolders"].toBool(false);
  c.folderBrowsing.includeArtworkSubfolders = o["include_artwork_subfolders"].toBool(false);
  c.folderBrowsing.showAllSubfolderItems = o["show_all_subfolder_items"].toBool(false);
  c.folderBrowsing.hideSubfolderTitles = o["hide_subfolder_titles"].toBool(false);
  c.folderBrowsing.showHiddenFolders = o["show_hidden_folders"].toBool(false);

  c.listView.listFontSize = o["list_font_size"].toInt(UIConstants::Item::DEFAULT_FONT_SIZE);
  c.listView.listRowHeight = o["list_row_height"].toInt(UIConstants::ListView::DEFAULT_ROW_HEIGHT);
  c.listView.listRowColor = o["list_row_color"].toString();
  c.listView.listAltRowColor = o["list_alt_row_color"].toString();
  c.customFontFamily = o["custom_font_family"].toString();

  c.additionalParentNames = jsonToStringList(o["additional_parent_names"]);

  // Clamp every numeric layout field to the UI-enforced ranges at the import
  // boundary — the same canonical clamp the settings loader applies — so an
  // untrusted or corrupt .kart can't inject a degenerate layout or a huge
  // allocation (Kartend audit E-03; complements the default_launcher_index
  // clamp above, which clampValues() also re-applies idempotently).
  c.clampValues();
  return c;
}

QJsonObject itemMetadataToJson(const ItemMetadataStore::ItemMetadata &m) {
  QJsonObject o;
  o["title"] = m.title;
  o["description"] = m.description;
  o["genre"] = m.genre;
  o["developer"] = m.developer;
  o["publisher"] = m.publisher;
  o["release_date"] = m.releaseDate;
  o["content_rating"] = m.contentRating;
  o["players"] = m.players;
  o["runtime_seconds"] = m.runtimeSeconds;
  o["tags"] = m.tags;
  o["custom_fields"] = m.customFields;
  o["manual_path"] = m.manualPath;
  o["launcher_index"] = m.launcherIndex;
  o["source"] = m.source;
  return o;
}

ItemMetadataStore::ItemMetadata jsonToItemMetadata(const QJsonObject &o) {
  ItemMetadataStore::ItemMetadata m;
  m.title = o["title"].toString();
  m.description = o["description"].toString();
  m.genre = o["genre"].toString();
  m.developer = o["developer"].toString();
  m.publisher = o["publisher"].toString();
  m.releaseDate = o["release_date"].toString();
  m.contentRating = o["content_rating"].toString();
  m.players = o["players"].toString();
  m.runtimeSeconds = o["runtime_seconds"].toInt(-1);
  m.tags = o["tags"].toString();
  m.customFields = o["custom_fields"].toString();
  m.manualPath = o["manual_path"].toString();
  m.launcherIndex = o["launcher_index"].toInt(-1);
  m.source = o["source"].toString();
  return m;
}

QJsonObject itemToJson(const Item &i) {
  QJsonObject o;
  o["media_path"] = i.mediaPath;
  o["artwork_path"] = i.artworkPath;
  o["video_path"] = i.videoPath;
  o["manual_path"] = i.manualPath;
  o["title"] = i.title;
  o["metadata"] = itemMetadataToJson(i.metadata);
  o["launcher_index"] = i.launcherIndex;
  return o;
}

Item jsonToItem(const QJsonObject &o) {
  Item i;
  i.mediaPath = o["media_path"].toString();
  i.artworkPath = o["artwork_path"].toString();
  i.videoPath = o["video_path"].toString();
  i.manualPath = o["manual_path"].toString();
  i.title = o["title"].toString();
  i.metadata = jsonToItemMetadata(o["metadata"].toObject());
  i.launcherIndex = o["launcher_index"].toInt(-1);
  return i;
}

QJsonObject playlistItemToJson(const PlaylistItemEntry &it) {
  QJsonObject o;
  o["media_path"] = it.mediaPath;
  o["position"] = it.position;
  return o;
}

PlaylistItemEntry jsonToPlaylistItem(const QJsonObject &o) {
  PlaylistItemEntry it;
  it.mediaPath = o["media_path"].toString();
  it.position = o["position"].toInt(-1);
  return it;
}

QJsonObject playlistToJson(const PlaylistEntry &p) {
  QJsonObject o;
  o["name"] = p.name;
  o["icon"] = p.icon;
  o["reserved_kind"] = p.reservedKind;
  o["is_smart"] = p.isSmart;
  o["smart_filter"] = p.smartFilterJson;
  QJsonArray itemsArr;
  for (const PlaylistItemEntry &it : p.items) {
    itemsArr.append(playlistItemToJson(it));
  }
  o["items"] = itemsArr;
  return o;
}

PlaylistEntry jsonToPlaylist(const QJsonObject &o) {
  PlaylistEntry p;
  p.name = o["name"].toString();
  p.icon = o["icon"].toString();
  p.reservedKind = o["reserved_kind"].toString();
  p.isSmart = o["is_smart"].toBool(false);
  p.smartFilterJson = o["smart_filter"].toString();
  const QJsonArray itemsArr = o["items"].toArray();
  for (const auto &v : itemsArr) {
    p.items.append(jsonToPlaylistItem(v.toObject()));
  }
  return p;
}

} // namespace

QByteArray serialize(const Manifest &manifest) {
  QJsonObject root;
  root["format_version"] = static_cast<int>(manifest.formatVersion);
  root["uuid"] = manifest.uuid;
  root["version"] = manifest.version;
  root["created_at"] = manifest.createdAt;
  root["name"] = manifest.name;
  root["author"] = manifest.author;
  root["description"] = manifest.description;
  root["license"] = manifest.license;
  root["collection_config"] = collectionConfigToJson(manifest.collectionConfig);

  QJsonArray launchers;
  for (const LauncherPreset &p : manifest.launchers) {
    launchers.append(launcherPresetToJson(p));
  }
  root["launchers"] = launchers;

  QJsonArray items;
  for (const Item &it : manifest.items) {
    items.append(itemToJson(it));
  }
  root["items"] = items;

  QJsonArray playlistsArr;
  for (const PlaylistEntry &p : manifest.playlists) {
    playlistsArr.append(playlistToJson(p));
  }
  root["playlists"] = playlistsArr;

  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ErrorUtils::Result<Manifest> parse(const QByteArray &json) {
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartManifestParseFailed,
                                           "Failed to parse Kart manifest JSON",
                                           "KartManifest::parse")
        .withDetails(err.errorString());
  }
  if (!doc.isObject()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartManifestInvalid,
                                           "Kart manifest root must be a JSON object",
                                           "KartManifest::parse");
  }
  const QJsonObject root = doc.object();

  Manifest m;
  m.formatVersion = static_cast<quint32>(root["format_version"].toInt(0));
  if (m.formatVersion == 0) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartManifestInvalid,
                                           "Kart manifest is missing format_version",
                                           "KartManifest::parse");
  }
  if (m.formatVersion > KartFormat::CURRENT_VERSION) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartVersionUnsupported,
                                           "Kart manifest format version is newer than supported",
                                           "KartManifest::parse")
        .withDetails(QString("manifest=%1, supported=%2")
                         .arg(m.formatVersion)
                         .arg(KartFormat::CURRENT_VERSION));
  }

  m.uuid = root["uuid"].toString();
  m.version = root["version"].toString();
  m.createdAt = root["created_at"].toString();
  m.name = root["name"].toString();
  m.author = root["author"].toString();
  m.description = root["description"].toString();
  m.license = root["license"].toString();

  if (m.uuid.isEmpty() || m.name.isEmpty()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartManifestInvalid,
                                           "Kart manifest is missing required field (uuid or name)",
                                           "KartManifest::parse");
  }

  m.collectionConfig = jsonToCollectionConfig(root["collection_config"].toObject());

  const QJsonArray launchersArr = root["launchers"].toArray();
  for (const auto &v : launchersArr) {
    m.launchers.append(jsonToLauncherPreset(v.toObject()));
  }
  const QJsonArray itemsArr = root["items"].toArray();
  for (const auto &v : itemsArr) {
    m.items.append(jsonToItem(v.toObject()));
  }
  // Playlists optional — v1 bundles omit the field entirely. toArray()
  // returns an empty array for a missing or wrong-type value, which is
  // exactly the fall-back the version comment in kartformat.h promises.
  const QJsonArray playlistsArr = root["playlists"].toArray();
  for (const auto &v : playlistsArr) {
    m.playlists.append(jsonToPlaylist(v.toObject()));
  }
  return m;
}

} // namespace KartManifest
