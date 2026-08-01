// Implementation of ConfigValidation namespace functions.
// Bodies moved out of configvalidation.h to reduce per-TU compile time.
#include "configvalidation.h"

#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "collection/typehelpers.h"
#include "pathutils.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

// Per-module logging category. Defaults to warning level so config issues
// remain visible in release logs while letting users silence with
// QT_LOGGING_RULES="kartend.configvalidation=false".
Q_LOGGING_CATEGORY(lcConfigValidation, "kartend.configvalidation", QtWarningMsg)

namespace ConfigValidation {

bool isCommandInPath(const QString &command) {
  return !QStandardPaths::findExecutable(command).isEmpty();
}

ValidationResult validateCollection(const CollectionConfig &config, int index, bool isContainer) {
  ValidationResult result;
  QString prefix = QString("Collection '%1' (index %2): ")
                       .arg(config.name.isEmpty() ? "<unnamed>" : config.name)
                       .arg(index);

  // Required fields
  if (config.name.isEmpty()) {
    result.addError(prefix + "missing name");
  }

  // Media directory validation
  // Container/shell collections don't need media directories - they only hold
  // subcollections
  if (config.mediaDirectory.isEmpty()) {
    if (!isContainer) {
      result.addWarning(prefix + "no media directory specified");
    }
  } else {
    // Expand ~ and %collection% via the same resolver the runtime uses
    // (PathUtils::expandPath), so a %collection%-templated media dir is checked
    // against its real on-disk location instead of always reporting "does not
    // exist" (Kartend-k375).
    const QString expandedPath =
        PathUtils::expandPathWithoutExistenceCheck(config.mediaDirectory, config.name);
    QFileInfo mediaInfo(expandedPath);
    if (!mediaInfo.exists()) {
      result.addError(prefix + "media directory does not exist: " + config.mediaDirectory);
    } else if (!mediaInfo.isDir()) {
      result.addError(prefix + "media path is not a directory: " + config.mediaDirectory);
    } else if (!mediaInfo.isReadable()) {
      result.addError(prefix + "media directory is not readable: " + config.mediaDirectory);
    }
  }

  // Artwork directory validation (optional but validate if present)
  if (!config.artworkDirectory.isEmpty()) {
    const QString expandedPath =
        PathUtils::expandPathWithoutExistenceCheck(config.artworkDirectory, config.name);
    QFileInfo artworkInfo(expandedPath);
    if (!artworkInfo.exists()) {
      result.addWarning(prefix + "artwork directory does not exist: " + config.artworkDirectory);
    } else if (!artworkInfo.isDir()) {
      result.addWarning(prefix + "artwork path is not a directory: " + config.artworkDirectory);
    }
  }

  // No validation for videoDirectory / manualDirectory here: the load path
  // in SettingsManager forces both fields empty (the canonical home is now
  // `{artworkDirectory}/video/` and `{artworkDirectory}/manual/`), so by
  // the time validateAllCollections runs they're guaranteed empty and any
  // check would be dead code. Runtime consumers (Kart imports, coverflow,
  // marquee, details pane) still set and read the fields directly, so the
  // struct fields themselves stay in place.

  // Launcher validation (optional but validate if present). One warning per
  // launcher, keyed to its actual state — the previous shape let a
  // present-but-not-executable path fall through to a second "does not
  // exist" warning that contradicted the first (Kartend-ufxpm), and routed
  // "~launcher" (path branch) through the command-name message.
  if (!config.launcher.launcherPath.isEmpty()) {
    const QString launcherPath = config.launcher.launcherPath;

    if (launcherPath.contains('/') || launcherPath.startsWith('~')) {
      // Path-like: missing, present-but-not-executable, or fine.
      const QString expandedPath =
          PathUtils::expandPathWithoutExistenceCheck(launcherPath, config.name);
      const QFileInfo launcherInfo(expandedPath);
      if (!launcherInfo.exists()) {
        result.addWarning(prefix + "launcher does not exist: " + launcherPath);
      } else if (!launcherInfo.isExecutable()) {
        result.addWarning(prefix + "launcher is not executable: " + launcherPath);
      }
    } else if (!isCommandInPath(launcherPath)) {
      // Bare command name — the PATH lookup is the only existence check.
      result.addWarning(prefix + "launcher not found in PATH: " + launcherPath);
    }
  }

  // Numeric range validation (additional checks beyond clampValues)
  if (config.gridLayout.gridWidth < 1) {
    result.addWarning(prefix + "gridWidth less than 1, will be clamped");
  }
  if (config.gridLayout.itemWidth < 50 || config.gridLayout.itemHeight < 50) {
    result.addWarning(prefix + "very small item dimensions may cause display issues");
  }

  // Per-leaf-struct field validation. Each block is independent so adding a
  // new field to any leaf struct touches only the matching block here.
  // Convention: warning-level only — clampValues() is the runtime
  // backstop; these warnings give the user a diagnostic trail when a
  // hand-edited or migrated INI carries an out-of-range value.

  // GridLayoutPreferences — alternates + spacing + corner radius.
  const auto &gl = config.gridLayout;
  if (gl.horizontalGridHeight < 0) {
    result.addWarning(prefix + "horizontalGridHeight negative, will be clamped to 0 (inherit)");
  }
  if (gl.gridWidthSidebarHidden < 0) {
    result.addWarning(prefix + "gridWidthSidebarHidden negative, will be clamped to 0 (inherit)");
  }
  if (gl.horizontalGridHeightSidebarHidden < 0) {
    result.addWarning(prefix +
                      "horizontalGridHeightSidebarHidden negative, will be clamped to 0 (inherit)");
  }
  if (gl.gridHeightSidebarHidden < 0) {
    result.addWarning(prefix +
                      "gridHeightSidebarHidden negative, will be clamped to 0 (no override)");
  }
  // Spacing may be negative (overlap effects); clampValues() bounds it to
  // [-100, 200], so warn only when outside that range — not on every
  // negative value, which is legitimately allowed.
  if (gl.horizontalSpacing < -100 || gl.horizontalSpacing > 200) {
    result.addWarning(prefix + "horizontalSpacing " + QString::number(gl.horizontalSpacing) +
                      " outside -100..200, will be clamped");
  }
  if (gl.verticalSpacing < -100 || gl.verticalSpacing > 200) {
    result.addWarning(prefix + "verticalSpacing " + QString::number(gl.verticalSpacing) +
                      " outside -100..200, will be clamped");
  }
  if (gl.fontSize <= 0) {
    result.addWarning(prefix + "gridLayout.fontSize must be > 0, will be clamped to default");
  }
  if (gl.cornerRadius < 0) {
    result.addWarning(prefix + "cornerRadius negative, will be clamped to 0");
  }

  // ListViewOptions — font + row height.
  const auto &lv = config.listView;
  if (lv.listFontSize <= 0) {
    result.addWarning(prefix + "listFontSize must be > 0, will be clamped to default");
  }
  if (lv.listRowHeight <= 0) {
    result.addWarning(prefix + "listRowHeight must be > 0, will be clamped to default");
  }

  // CollectionBackground — vignette + parallax + blur ranges.
  const auto &bg = config.background;
  if (bg.vignetteIntensity < 0 || bg.vignetteIntensity > 100) {
    result.addWarning(prefix + "vignetteIntensity " + QString::number(bg.vignetteIntensity) +
                      " outside 0-100, will be clamped");
  }
  if (bg.parallaxStrength < 0 || bg.parallaxStrength > 100) {
    result.addWarning(prefix + "parallaxStrength " + QString::number(bg.parallaxStrength) +
                      " outside 0-100, will be clamped");
  }
  // clampValues() bounds the radius to [4, 32]; warn on any out-of-range
  // value (e.g. a hand-edited 1-3 silently bumped to 4), not just negatives.
  if (bg.backdropBlurRadius < 4 || bg.backdropBlurRadius > 32) {
    result.addWarning(prefix + "backdropBlurRadius " + QString::number(bg.backdropBlurRadius) +
                      " outside 4..32, will be clamped");
  }

  // SidebarAppearance — opacities + intensity + dimensions.
  const auto &sb = config.sidebar;
  if (sb.sidebarPatternIntensity < 0 || sb.sidebarPatternIntensity > 100) {
    result.addWarning(prefix + "sidebarPatternIntensity " +
                      QString::number(sb.sidebarPatternIntensity) +
                      " outside 0-100, will be clamped");
  }
  if (sb.sidebarHeaderBgOpacity < 0 || sb.sidebarHeaderBgOpacity > 255) {
    result.addWarning(prefix + "sidebarHeaderBgOpacity " +
                      QString::number(sb.sidebarHeaderBgOpacity) +
                      " outside 0-255, will be clamped");
  }
  if (sb.sidebarSectionBgOpacity < 0 || sb.sidebarSectionBgOpacity > 255) {
    result.addWarning(prefix + "sidebarSectionBgOpacity " +
                      QString::number(sb.sidebarSectionBgOpacity) +
                      " outside 0-255, will be clamped");
  }
  if (sb.sidebarWidth < 0) {
    result.addWarning(prefix + "sidebarWidth negative, will be clamped to MIN_WIDTH");
  }
  if (sb.sidebarHeight < 0) {
    result.addWarning(prefix + "sidebarHeight negative, will be clamped to MIN_HEIGHT");
  }
  if (sb.sidebarFontPointSize < 0) {
    result.addWarning(prefix + "sidebarFontPointSize negative, will be treated as 0 (inherit)");
  }

  // CollectionFilterPreferences — surface invalid regex patterns. The
  // runtime TitleFilter::compilePatterns silently drops bad patterns; this
  // warning gives the user a chance to fix the typo before they wonder why
  // their cleanup rule "isn't working".
  for (const QString &pat : config.filter.titleExclusionPatterns) {
    if (pat.isEmpty()) continue;
    QRegularExpression re(pat);
    if (!re.isValid()) {
      result.addWarning(prefix + "titleExclusionPatterns entry is not a valid regex: \"" + pat +
                        "\" — " + re.errorString());
    }
  }

  // Parent index validation
  if (config.parentCollectionIndex < -1) {
    result.addError(
        prefix + "invalid parentCollectionIndex: " + QString::number(config.parentCollectionIndex));
  }

  return result;
}

ValidationResult validateAllCollections(const QList<CollectionConfig> &collections) {
  ValidationResult result;

  if (collections.isEmpty()) {
    result.addWarning("No collections configured");
    return result;
  }

  // Build set of collections that have children (container/shell collections)
  QSet<int> containerIndices;
  for (int i = 0; i < collections.size(); ++i) {
    int parentIndex = collections[i].parentCollectionIndex;
    if (parentIndex >= 0 && parentIndex < collections.size()) {
      containerIndices.insert(parentIndex);
    }
  }

  // Validate each collection
  for (int i = 0; i < collections.size(); ++i) {
    bool isContainer = containerIndices.contains(i);
    ValidationResult collResult = validateCollection(collections[i], i, isContainer);
    result.warnings << collResult.warnings;
    result.errors << collResult.errors;
    if (!collResult.valid) {
      result.valid = false;
    }
  }

  // Cross-reference validation: check parent indices
  for (int i = 0; i < collections.size(); ++i) {
    int parentIndex = collections[i].parentCollectionIndex;
    if (parentIndex >= collections.size()) {
      result.addError(QString("Collection '%1' (index %2) has invalid parent index %3")
                          .arg(collections[i].name)
                          .arg(i)
                          .arg(parentIndex));
    }
    // Detect circular parent references. Self-parenting keeps its specific
    // message; deeper cycles reuse the visited-set ancestor walk that
    // wouldCreateCircularReference already implements — the old check only
    // caught parent == i, so a 2-cycle (or longer) passed unreported
    // (Kartend-ufxpm). The helper asks "may i have parentIndex as parent?"
    // — with parentIndex being i's CURRENT parent, a true answer means i's
    // ancestor chain reaches back to i or is itself cyclic.
    if (parentIndex == i) {
      result.addError(QString("Collection '%1' (index %2) has itself as parent")
                          .arg(collections[i].name)
                          .arg(i));
    } else if (parentIndex >= 0 && parentIndex < collections.size() &&
               CollectionUtils::wouldCreateCircularReference(i, parentIndex, collections)) {
      result.addError(QString("Collection '%1' (index %2) has a circular parent chain")
                          .arg(collections[i].name)
                          .arg(i));
    }
  }

  // Check for duplicate names at same hierarchy level
  QHash<QString, QList<int>> nameToIndices;
  for (int i = 0; i < collections.size(); ++i) {
    QString key =
        QString("%1:%2").arg(collections[i].parentCollectionIndex).arg(collections[i].name);
    nameToIndices[key].append(i);
  }
  for (auto it = nameToIndices.begin(); it != nameToIndices.end(); ++it) {
    if (it.value().size() > 1) {
      result.addWarning(QString("Duplicate collection name '%1' at same hierarchy level")
                            .arg(it.key().split(':').last()));
    }
  }

  // Check for UUID collisions (same name + mediaDirectory = same UUID)
  // This can cause data corruption as items would be stored under the same key
  QHash<QString, QList<int>> uuidToIndices;
  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &c = collections[i];
    // Skip collections without media directories (shell collections)
    if (c.mediaDirectory.isEmpty()) {
      continue;
    }
    QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    uuidToIndices[uuid].append(i);
  }
  for (auto it = uuidToIndices.begin(); it != uuidToIndices.end(); ++it) {
    if (it.value().size() > 1) {
      QStringList collisionNames;
      for (int idx : it.value()) {
        collisionNames << QString("'%1' (index %2)").arg(collections[idx].name).arg(idx);
      }
      const QString collisionMsg = QString("UUID collision detected: collections %1 have identical "
                                           "name+mediaDirectory combination. This will cause data "
                                           "corruption. Please rename one of the collections.")
                                       .arg(collisionNames.join(", "));
      result.addError(collisionMsg);
      result.collisions << collisionMsg; // data-corruption subset (cj462)
    }
  }

  return result;
}

void logValidationResult(const ValidationResult &result, const QString &context) {
  if (!result.hasIssues()) {
    return;
  }

  QString prefix = context.isEmpty() ? "Config validation" : context;

  for (const QString &error : result.errors) {
    qCWarning(lcConfigValidation) << prefix << "ERROR:" << error;
  }
  for (const QString &warning : result.warnings) {
    qCWarning(lcConfigValidation) << prefix << "WARNING:" << warning;
  }
}

} // namespace ConfigValidation
