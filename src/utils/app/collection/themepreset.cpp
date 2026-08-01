#include "themepreset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "collectionconfig.h"
#include "pathutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ThemePresetIO {

namespace {

constexpr int kCurrentSchemaVersion = 1;

constexpr const char *KEY_NAME = "name";
constexpr const char *KEY_DESCRIPTION = "description";
constexpr const char *KEY_SCHEMA_VERSION = "schemaVersion";
constexpr const char *KEY_GRID = "grid";
constexpr const char *KEY_SIDEBAR = "sidebar";
constexpr const char *KEY_BACKGROUND = "background";
constexpr const char *KEY_LIST_VIEW = "listView";
constexpr const char *KEY_HORIZONTAL_ALIGNMENT = "horizontalAlignment";
constexpr const char *KEY_CUSTOM_FONT_FAMILY = "customFontFamily";

QJsonObject gridToJson(const GridLayoutPreferences &g) {
  QJsonObject o;
  o["gridWidth"] = g.gridWidth;
  o["horizontalGridHeight"] = g.horizontalGridHeight;
  o["gridWidthSidebarHidden"] = g.gridWidthSidebarHidden;
  o["horizontalGridHeightSidebarHidden"] = g.horizontalGridHeightSidebarHidden;
  o["gridHeightSidebarHidden"] = g.gridHeightSidebarHidden;
  o["horizontalSpacing"] = g.horizontalSpacing;
  o["verticalSpacing"] = g.verticalSpacing;
  o["hideHorizontalScrollbar"] = g.hideHorizontalScrollbar;
  o["hideVerticalScrollbar"] = g.hideVerticalScrollbar;
  o["itemWidth"] = g.itemWidth;
  o["itemHeight"] = g.itemHeight;
  o["fontSize"] = g.fontSize;
  o["cornerRadius"] = g.cornerRadius;
  return o;
}

GridLayoutPreferences gridFromJson(const QJsonObject &o) {
  GridLayoutPreferences g;
  if (o.contains("gridWidth")) g.gridWidth = o.value("gridWidth").toInt(g.gridWidth);
  if (o.contains("horizontalGridHeight"))
    g.horizontalGridHeight = o.value("horizontalGridHeight").toInt(g.horizontalGridHeight);
  if (o.contains("gridWidthSidebarHidden"))
    g.gridWidthSidebarHidden = o.value("gridWidthSidebarHidden").toInt(g.gridWidthSidebarHidden);
  if (o.contains("horizontalGridHeightSidebarHidden"))
    g.horizontalGridHeightSidebarHidden =
        o.value("horizontalGridHeightSidebarHidden").toInt(g.horizontalGridHeightSidebarHidden);
  if (o.contains("gridHeightSidebarHidden"))
    g.gridHeightSidebarHidden = o.value("gridHeightSidebarHidden").toInt(g.gridHeightSidebarHidden);
  if (o.contains("horizontalSpacing"))
    g.horizontalSpacing = o.value("horizontalSpacing").toInt(g.horizontalSpacing);
  if (o.contains("verticalSpacing"))
    g.verticalSpacing = o.value("verticalSpacing").toInt(g.verticalSpacing);
  if (o.contains("hideHorizontalScrollbar"))
    g.hideHorizontalScrollbar =
        o.value("hideHorizontalScrollbar").toBool(g.hideHorizontalScrollbar);
  if (o.contains("hideVerticalScrollbar"))
    g.hideVerticalScrollbar = o.value("hideVerticalScrollbar").toBool(g.hideVerticalScrollbar);
  if (o.contains("itemWidth")) g.itemWidth = o.value("itemWidth").toInt(g.itemWidth);
  if (o.contains("itemHeight")) g.itemHeight = o.value("itemHeight").toInt(g.itemHeight);
  if (o.contains("fontSize")) g.fontSize = o.value("fontSize").toInt(g.fontSize);
  if (o.contains("cornerRadius")) g.cornerRadius = o.value("cornerRadius").toInt(g.cornerRadius);
  return g;
}

QJsonObject sidebarToJson(const SidebarAppearance &s) {
  QJsonObject o;
  o["sidebarVisible"] = s.sidebarVisible;
  o["sidebarMode"] = int(s.sidebarMode);
  o["sidebarPosition"] = int(s.sidebarPosition);
  o["sidebarBackgroundType"] = int(s.sidebarBackgroundType);
  o["sidebarBackgroundColor"] = s.sidebarBackgroundColor;
  o["sidebarBackgroundImage"] = s.sidebarBackgroundImage;
  o["sidebarPattern"] = int(s.sidebarPattern);
  o["sidebarPatternIntensity"] = s.sidebarPatternIntensity;
  o["sidebarPatternColor"] = s.sidebarPatternColor;
  o["sidebarTextColor"] = s.sidebarTextColor;
  o["sidebarAccentColor"] = s.sidebarAccentColor;
  o["sidebarHeaderBgColor"] = s.sidebarHeaderBgColor;
  o["sidebarSectionBgColor"] = s.sidebarSectionBgColor;
  o["sidebarHeaderBgOpacity"] = s.sidebarHeaderBgOpacity;
  o["sidebarSectionBgOpacity"] = s.sidebarSectionBgOpacity;
  o["sidebarWidth"] = s.sidebarWidth;
  o["sidebarHeight"] = s.sidebarHeight;
  o["sidebarWidthLocked"] = s.sidebarWidthLocked;
  o["sidebarActiveTab"] = int(s.sidebarActiveTab);
  o["sidebarFontFamily"] = s.sidebarFontFamily;
  o["sidebarFontPointSize"] = s.sidebarFontPointSize;
  return o;
}

SidebarAppearance sidebarFromJson(const QJsonObject &o) {
  SidebarAppearance s;
  if (o.contains("sidebarVisible"))
    s.sidebarVisible = o.value("sidebarVisible").toBool(s.sidebarVisible);
  if (o.contains("sidebarMode"))
    s.sidebarMode = static_cast<DetailsPaneMode>(o.value("sidebarMode").toInt(int(s.sidebarMode)));
  if (o.contains("sidebarPosition"))
    s.sidebarPosition =
        static_cast<DetailsPanePosition>(o.value("sidebarPosition").toInt(int(s.sidebarPosition)));
  if (o.contains("sidebarBackgroundType"))
    s.sidebarBackgroundType = static_cast<DetailsPaneBackgroundType>(
        o.value("sidebarBackgroundType").toInt(int(s.sidebarBackgroundType)));
  if (o.contains("sidebarBackgroundColor"))
    s.sidebarBackgroundColor = o.value("sidebarBackgroundColor").toString();
  if (o.contains("sidebarBackgroundImage"))
    s.sidebarBackgroundImage = o.value("sidebarBackgroundImage").toString();
  if (o.contains("sidebarPattern"))
    s.sidebarPattern =
        static_cast<DetailsPanePattern>(o.value("sidebarPattern").toInt(int(s.sidebarPattern)));
  if (o.contains("sidebarPatternIntensity"))
    s.sidebarPatternIntensity = o.value("sidebarPatternIntensity").toInt(s.sidebarPatternIntensity);
  if (o.contains("sidebarPatternColor"))
    s.sidebarPatternColor = o.value("sidebarPatternColor").toString();
  if (o.contains("sidebarTextColor")) s.sidebarTextColor = o.value("sidebarTextColor").toString();
  if (o.contains("sidebarAccentColor"))
    s.sidebarAccentColor = o.value("sidebarAccentColor").toString();
  if (o.contains("sidebarHeaderBgColor"))
    s.sidebarHeaderBgColor = o.value("sidebarHeaderBgColor").toString();
  if (o.contains("sidebarSectionBgColor"))
    s.sidebarSectionBgColor = o.value("sidebarSectionBgColor").toString();
  if (o.contains("sidebarHeaderBgOpacity"))
    s.sidebarHeaderBgOpacity = o.value("sidebarHeaderBgOpacity").toInt(s.sidebarHeaderBgOpacity);
  if (o.contains("sidebarSectionBgOpacity"))
    s.sidebarSectionBgOpacity = o.value("sidebarSectionBgOpacity").toInt(s.sidebarSectionBgOpacity);
  if (o.contains("sidebarWidth")) s.sidebarWidth = o.value("sidebarWidth").toInt(s.sidebarWidth);
  if (o.contains("sidebarHeight"))
    s.sidebarHeight = o.value("sidebarHeight").toInt(s.sidebarHeight);
  if (o.contains("sidebarWidthLocked"))
    s.sidebarWidthLocked = o.value("sidebarWidthLocked").toBool(s.sidebarWidthLocked);
  if (o.contains("sidebarActiveTab"))
    s.sidebarActiveTab =
        static_cast<DetailsPaneTab>(o.value("sidebarActiveTab").toInt(int(s.sidebarActiveTab)));
  if (o.contains("sidebarFontFamily"))
    s.sidebarFontFamily = o.value("sidebarFontFamily").toString();
  if (o.contains("sidebarFontPointSize"))
    s.sidebarFontPointSize = o.value("sidebarFontPointSize").toInt(s.sidebarFontPointSize);
  return s;
}

QJsonObject backgroundToJson(const CollectionBackground &b) {
  QJsonObject o;
  o["backgroundType"] = int(b.backgroundType);
  o["backgroundColor"] = b.backgroundColor;
  o["backgroundImage"] = b.backgroundImage;
  o["backgroundVideo"] = b.backgroundVideo;
  o["primaryColor"] = b.primaryColor;
  o["tileColor"] = b.tileColor;
  o["selectionColor"] = b.selectionColor;
  o["headerLogoImage"] = b.headerLogoImage;
  o["headerLogoPosition"] = int(b.headerLogoPosition);
  o["vignetteEnabled"] = b.vignetteEnabled;
  o["vignetteIntensity"] = b.vignetteIntensity;
  o["wallpaperParallax"] = b.wallpaperParallax;
  o["parallaxStrength"] = b.parallaxStrength;
  o["toolbarBackdropBlur"] = b.toolbarBackdropBlur;
  o["backdropBlurRadius"] = b.backdropBlurRadius;
  return o;
}

CollectionBackground backgroundFromJson(const QJsonObject &o) {
  CollectionBackground b;
  if (o.contains("backgroundType"))
    b.backgroundType =
        static_cast<BackgroundType>(o.value("backgroundType").toInt(int(b.backgroundType)));
  if (o.contains("backgroundColor")) b.backgroundColor = o.value("backgroundColor").toString();
  if (o.contains("backgroundImage")) b.backgroundImage = o.value("backgroundImage").toString();
  if (o.contains("backgroundVideo")) b.backgroundVideo = o.value("backgroundVideo").toString();
  if (o.contains("primaryColor")) b.primaryColor = o.value("primaryColor").toString();
  if (o.contains("tileColor")) b.tileColor = o.value("tileColor").toString();
  if (o.contains("selectionColor")) b.selectionColor = o.value("selectionColor").toString();
  if (o.contains("headerLogoImage")) b.headerLogoImage = o.value("headerLogoImage").toString();
  if (o.contains("headerLogoPosition"))
    b.headerLogoPosition = static_cast<HeaderLogoPosition>(
        o.value("headerLogoPosition").toInt(int(b.headerLogoPosition)));
  if (o.contains("vignetteEnabled"))
    b.vignetteEnabled = o.value("vignetteEnabled").toBool(b.vignetteEnabled);
  if (o.contains("vignetteIntensity"))
    b.vignetteIntensity = o.value("vignetteIntensity").toInt(b.vignetteIntensity);
  if (o.contains("wallpaperParallax"))
    b.wallpaperParallax = o.value("wallpaperParallax").toBool(b.wallpaperParallax);
  if (o.contains("parallaxStrength"))
    b.parallaxStrength = o.value("parallaxStrength").toInt(b.parallaxStrength);
  if (o.contains("toolbarBackdropBlur"))
    b.toolbarBackdropBlur = o.value("toolbarBackdropBlur").toBool(b.toolbarBackdropBlur);
  if (o.contains("backdropBlurRadius"))
    b.backdropBlurRadius = o.value("backdropBlurRadius").toInt(b.backdropBlurRadius);
  return b;
}

QJsonObject listViewToJson(const ListViewOptions &l) {
  QJsonObject o;
  o["listFontSize"] = l.listFontSize;
  o["listRowHeight"] = l.listRowHeight;
  o["listRowColor"] = l.listRowColor;
  o["listAltRowColor"] = l.listAltRowColor;
  return o;
}

ListViewOptions listViewFromJson(const QJsonObject &o) {
  ListViewOptions l;
  if (o.contains("listFontSize")) l.listFontSize = o.value("listFontSize").toInt(l.listFontSize);
  if (o.contains("listRowHeight"))
    l.listRowHeight = o.value("listRowHeight").toInt(l.listRowHeight);
  if (o.contains("listRowColor")) l.listRowColor = o.value("listRowColor").toString();
  if (o.contains("listAltRowColor")) l.listAltRowColor = o.value("listAltRowColor").toString();
  return l;
}

} // namespace

QJsonObject toJson(const ThemePreset &preset) {
  QJsonObject o;
  o[KEY_SCHEMA_VERSION] = preset.schemaVersion <= 0 ? kCurrentSchemaVersion : preset.schemaVersion;
  o[KEY_NAME] = preset.name;
  o[KEY_DESCRIPTION] = preset.description;
  o[KEY_GRID] = gridToJson(preset.gridLayout);
  o[KEY_SIDEBAR] = sidebarToJson(preset.sidebar);
  o[KEY_BACKGROUND] = backgroundToJson(preset.background);
  o[KEY_LIST_VIEW] = listViewToJson(preset.listView);
  o[KEY_HORIZONTAL_ALIGNMENT] = int(preset.horizontalAlignment);
  o[KEY_CUSTOM_FONT_FAMILY] = preset.customFontFamily;
  return o;
}

ErrorUtils::Result<ThemePreset> fromJson(const QJsonObject &obj) {
  if (!obj.contains(KEY_SCHEMA_VERSION)) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Theme preset is missing the schemaVersion field",
                               "ThemePresetIO::fromJson");
  }
  const int schema = obj.value(KEY_SCHEMA_VERSION).toInt(-1);
  if (schema <= 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Theme preset schemaVersion is invalid",
                               "ThemePresetIO::fromJson")
        .withDetails(QStringLiteral("Value: %1").arg(schema));
  }
  if (schema > kCurrentSchemaVersion) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Theme preset was written by a newer Kartend build",
                               "ThemePresetIO::fromJson")
        .withDetails(QStringLiteral("Preset schema=%1, supported up to %2")
                         .arg(schema)
                         .arg(kCurrentSchemaVersion));
  }

  ThemePreset p;
  p.schemaVersion = schema;
  p.name = obj.value(KEY_NAME).toString();
  p.description = obj.value(KEY_DESCRIPTION).toString();
  p.gridLayout = gridFromJson(obj.value(KEY_GRID).toObject());
  p.sidebar = sidebarFromJson(obj.value(KEY_SIDEBAR).toObject());
  p.background = backgroundFromJson(obj.value(KEY_BACKGROUND).toObject());
  p.listView = listViewFromJson(obj.value(KEY_LIST_VIEW).toObject());
  if (obj.contains(KEY_HORIZONTAL_ALIGNMENT)) {
    p.horizontalAlignment = static_cast<HorizontalAlignment>(
        obj.value(KEY_HORIZONTAL_ALIGNMENT).toInt(int(p.horizontalAlignment)));
  }
  p.customFontFamily = obj.value(KEY_CUSTOM_FONT_FAMILY).toString();
  return p;
}

ErrorUtils::Result<bool> exportToFile(const ThemePreset &preset, const QString &filePath) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Cannot export theme preset to an empty path",
                               "ThemePresetIO::exportToFile");
  }
  // PathUtils::atomicWriteFile mirrors the playlists / session writer
  // pattern: write to a sibling temp, atomic-rename on commit, then fsync
  // the parent so the rename survives crash / power loss. It logs the
  // failing stage itself.
  const QByteArray bytes = QJsonDocument(toJson(preset)).toJson(QJsonDocument::Indented);
  if (!PathUtils::atomicWriteFile(filePath, bytes)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to write theme preset",
                               "ThemePresetIO::exportToFile")
        .withDetails(filePath);
  }
  return true;
}

ErrorUtils::Result<ThemePreset> importFromFile(const QString &filePath) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Cannot import theme preset from an empty path",
                               "ThemePresetIO::importFromFile");
  }
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError, "Failed to open theme preset for reading",
                               "ThemePresetIO::importFromFile")
        .withDetails(f.errorString());
  }
  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Theme preset is not valid JSON",
                               "ThemePresetIO::importFromFile")
        .withDetails(err.errorString());
  }
  return fromJson(doc.object());
}

void applyTo(const ThemePreset &preset, CollectionConfig &target) {
  target.gridLayout = preset.gridLayout;
  target.sidebar = preset.sidebar;
  target.background = preset.background;
  target.listView = preset.listView;
  target.horizontalAlignment = preset.horizontalAlignment;
  target.customFontFamily = preset.customFontFamily;
  // Re-floor anything an externally-edited preset might have under-shot.
  target.clampValues();
}

ThemePreset fromCollection(const CollectionConfig &source, const QString &name) {
  ThemePreset p;
  p.schemaVersion = kCurrentSchemaVersion;
  p.name = name.isEmpty() ? source.name : name;
  p.gridLayout = source.gridLayout;
  p.sidebar = source.sidebar;
  p.background = source.background;
  p.listView = source.listView;
  p.horizontalAlignment = source.horizontalAlignment;
  p.customFontFamily = source.customFontFamily;
  return p;
}

ErrorUtils::Result<QList<ThemePreset>> loadRegistry(const QString &filePath) {
  QList<ThemePreset> out;
  if (filePath.isEmpty()) {
    return out;
  }
  QFile f(filePath);
  if (!f.exists()) {
    // No registry yet — empty list is the honest answer.
    return out;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError, "Failed to open layout profile registry",
                               "ThemePresetIO::loadRegistry")
        .withDetails(f.errorString());
  }
  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Layout profile registry is malformed JSON",
                               "ThemePresetIO::loadRegistry")
        .withDetails(err.errorString());
  }
  // The registry can either be the bare array (older format) or a wrapped
  // object with a `profiles` array (current format). Accept both so an
  // external editor that just dropped in an array still loads.
  QJsonArray arr;
  if (doc.isArray()) {
    arr = doc.array();
  } else if (doc.isObject() && doc.object().value("profiles").isArray()) {
    arr = doc.object().value("profiles").toArray();
  } else {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Layout profile registry root is not an array",
                               "ThemePresetIO::loadRegistry");
  }
  for (const auto &v : arr) {
    if (!v.isObject()) continue;
    auto parsed = fromJson(v.toObject());
    if (parsed.isOk()) {
      out.append(parsed.value());
    }
    // Silently skip individual malformed entries so one broken profile
    // doesn't render the whole list unloadable. The user can still see
    // the rest in the dialog and re-save.
  }
  return out;
}

ErrorUtils::Result<bool> saveRegistry(const QList<ThemePreset> &profiles, const QString &filePath) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Cannot save layout profile registry to an empty path",
                               "ThemePresetIO::saveRegistry");
  }
  QJsonArray arr;
  for (const ThemePreset &p : profiles) {
    arr.append(toJson(p));
  }
  QJsonObject root;
  root["schemaVersion"] = kCurrentSchemaVersion;
  root["profiles"] = arr;

  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (!PathUtils::atomicWriteFile(filePath, bytes)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to write layout profile registry",
                               "ThemePresetIO::saveRegistry")
        .withDetails(filePath);
  }
  return true;
}

QList<ThemePreset> addOrReplace(const QList<ThemePreset> &registry, const ThemePreset &preset) {
  const QString trimmedName = preset.name.trimmed();
  if (trimmedName.isEmpty()) {
    // An empty name would create an unselectable row in the picker.
    // Reject silently — the dialog gates the Save button on a non-blank
    // name, so this is purely a defence-in-depth check.
    return registry;
  }
  QList<ThemePreset> out;
  out.reserve(registry.size() + 1);
  bool replaced = false;
  for (const ThemePreset &existing : registry) {
    if (existing.name.trimmed().compare(trimmedName, Qt::CaseInsensitive) == 0) {
      out.append(preset);
      replaced = true;
    } else {
      out.append(existing);
    }
  }
  if (!replaced) {
    out.append(preset);
  }
  return out;
}

QList<ThemePreset> removeByName(const QList<ThemePreset> &registry, const QString &name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return registry;
  }
  QList<ThemePreset> out;
  out.reserve(registry.size());
  for (const ThemePreset &existing : registry) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) != 0) {
      out.append(existing);
    }
  }
  return out;
}

QStringList describeChanges(const ThemePreset &preset, const CollectionConfig &target) {
  QStringList changes;
  if (preset.gridLayout != target.gridLayout) {
    changes << QStringLiteral("Grid layout (items-per-row, item size, spacing, scrollbars)");
  }
  if (preset.sidebar != target.sidebar) {
    changes << QStringLiteral("Sidebar appearance (colors, dock, pattern, font)");
  }
  if (preset.background != target.background) {
    changes << QStringLiteral("View background (color/image/video, vignette, parallax)");
  }
  if (preset.listView != target.listView) {
    changes << QStringLiteral("List-view (font, row height, row colors)");
  }
  if (preset.horizontalAlignment != target.horizontalAlignment) {
    changes << QStringLiteral("Horizontal alignment");
  }
  if (preset.customFontFamily != target.customFontFamily) {
    changes << QStringLiteral("Custom font family");
  }
  return changes;
}

} // namespace ThemePresetIO
