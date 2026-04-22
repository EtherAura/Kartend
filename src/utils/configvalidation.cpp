// Implementation of ConfigValidation namespace functions.
// Bodies moved out of configvalidation.h to reduce per-TU compile time.
#include "configvalidation.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>

namespace ConfigValidation {

bool isCommandInPath(const QString &command) {
  return !QStandardPaths::findExecutable(command).isEmpty();
}

QString ValidationResult::summary() const {
  QStringList lines;
  if (!errors.isEmpty()) {
    lines << "Errors:";
    for (const QString &e : errors) {
      lines << "  • " + e;
    }
  }
  if (!warnings.isEmpty()) {
    lines << "Warnings:";
    for (const QString &w : warnings) {
      lines << "  • " + w;
    }
  }
  return lines.join("\n");
}

ValidationResult validateCollection(const CollectionConfig &config, int index,
                                    bool isContainer) {
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
    QString expandedPath = config.mediaDirectory;
    if (expandedPath.startsWith("~")) {
      expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    QFileInfo mediaInfo(expandedPath);
    if (!mediaInfo.exists()) {
      result.addError(
          prefix + "media directory does not exist: " + config.mediaDirectory);
    } else if (!mediaInfo.isDir()) {
      result.addError(
          prefix + "media path is not a directory: " + config.mediaDirectory);
    } else if (!mediaInfo.isReadable()) {
      result.addError(
          prefix + "media directory is not readable: " + config.mediaDirectory);
    }
  }

  // Artwork directory validation (optional but validate if present)
  if (!config.artworkDirectory.isEmpty()) {
    QString expandedPath = config.artworkDirectory;
    if (expandedPath.startsWith("~")) {
      expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    QFileInfo artworkInfo(expandedPath);
    if (!artworkInfo.exists()) {
      result.addWarning(prefix + "artwork directory does not exist: " +
                        config.artworkDirectory);
    } else if (!artworkInfo.isDir()) {
      result.addWarning(prefix + "artwork path is not a directory: " +
                        config.artworkDirectory);
    }
  }

  // Launcher validation (optional but validate if present)
  if (!config.launcherPath.isEmpty()) {
    QString launcherPath = config.launcherPath;
    bool launcherValid = false;

    // Check if it's an absolute or relative path
    if (launcherPath.contains('/') || launcherPath.startsWith("~")) {
      QString expandedPath = launcherPath;
      if (expandedPath.startsWith("~")) {
        expandedPath = QDir::homePath() + expandedPath.mid(1);
      }
      QFileInfo launcherInfo(expandedPath);
      if (launcherInfo.exists()) {
        if (launcherInfo.isExecutable()) {
          launcherValid = true;
        } else {
          result.addWarning(prefix +
                            "launcher is not executable: " + launcherPath);
        }
      }
    } else {
      // It's a command name - check if it's in PATH
      launcherValid = isCommandInPath(launcherPath);
    }

    if (!launcherValid && !launcherPath.contains('/')) {
      // Only warn if we haven't already added a warning above
      result.addWarning(prefix + "launcher not found in PATH: " + launcherPath);
    } else if (!launcherValid) {
      result.addWarning(prefix + "launcher does not exist: " + launcherPath);
    }
  }

  // Numeric range validation (additional checks beyond clampValues)
  if (config.gridWidth < 1) {
    result.addWarning(prefix + "gridWidth less than 1, will be clamped");
  }
  if (config.itemWidth < 50 || config.itemHeight < 50) {
    result.addWarning(prefix +
                      "very small item dimensions may cause display issues");
  }

  // Parent index validation
  if (config.parentCollectionIndex < -1) {
    result.addError(prefix + "invalid parentCollectionIndex: " +
                    QString::number(config.parentCollectionIndex));
  }

  return result;
}

ValidationResult
validateAllCollections(const QList<CollectionConfig> &collections) {
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
    ValidationResult collResult =
        validateCollection(collections[i], i, isContainer);
    result.warnings << collResult.warnings;
    result.errors << collResult.errors;
    if (!collResult.valid) {
      result.valid = false;
    }
  }

  // Cross-reference validation: check parent indices
  for (int i = 0; i < collections.size(); ++i) {
    int parentIndex = collections[i].parentCollectionIndex;
    if (parentIndex >= 0 && parentIndex >= collections.size()) {
      result.addError(
          QString("Collection '%1' (index %2) has invalid parent index %3")
              .arg(collections[i].name)
              .arg(i)
              .arg(parentIndex));
    }
    // Detect circular parent references
    if (parentIndex == i) {
      result.addError(QString("Collection '%1' (index %2) has itself as parent")
                          .arg(collections[i].name)
                          .arg(i));
    }
  }

  // Check for duplicate names at same hierarchy level
  QHash<QString, QList<int>> nameToIndices;
  for (int i = 0; i < collections.size(); ++i) {
    QString key = QString("%1:%2")
                      .arg(collections[i].parentCollectionIndex)
                      .arg(collections[i].name);
    nameToIndices[key].append(i);
  }
  for (auto it = nameToIndices.begin(); it != nameToIndices.end(); ++it) {
    if (it.value().size() > 1) {
      result.addWarning(
          QString("Duplicate collection name '%1' at same hierarchy level")
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
    QString uuid =
        CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    uuidToIndices[uuid].append(i);
  }
  for (auto it = uuidToIndices.begin(); it != uuidToIndices.end(); ++it) {
    if (it.value().size() > 1) {
      QStringList collisionNames;
      for (int idx : it.value()) {
        collisionNames
            << QString("'%1' (index %2)").arg(collections[idx].name).arg(idx);
      }
      result.addError(
          QString("UUID collision detected: collections %1 have identical "
                  "name+mediaDirectory combination. This will cause data "
                  "corruption. Please rename one of the collections.")
              .arg(collisionNames.join(", ")));
    }
  }

  return result;
}

void logValidationResult(const ValidationResult &result,
                         const QString &context) {
  if (!result.hasIssues()) {
    return;
  }

  QString prefix = context.isEmpty() ? "Config validation" : context;

  for (const QString &error : result.errors) {
    qWarning() << prefix << "ERROR:" << error;
  }
  for (const QString &warning : result.warnings) {
    qWarning() << prefix << "WARNING:" << warning;
  }
}

} // namespace ConfigValidation
