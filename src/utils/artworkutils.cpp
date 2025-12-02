// Utility functions for artwork file operations.
#include "artworkutils.h"
#include "extensionutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace ArtworkUtils {

namespace {

/**
 * @brief Internal helper to search for artwork with a given base name.
 * @return Path if found, empty string otherwise.
 */
QString searchWithName(const QDir &artworkDir, const QString &name,
                       const QStringList &extensions) {
  for (const QString &ext : extensions) {
    QString path = artworkDir.absoluteFilePath(name + "." + ext);
    if (QFile::exists(path)) {
      return path;
    }
    path = artworkDir.absoluteFilePath(name + "." + ext.toUpper());
    if (QFile::exists(path)) {
      return path;
    }
  }
  return {};
}

} // namespace

QString findArtworkForFile(const QString &fileName,
                           const QString &artworkDirectory) {
  if (fileName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }

  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return {};
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  const QString fullName = QFileInfo(fileName).fileName();
  const QStringList &bases = ExtensionUtils::imageBaseExtensions();

  // Try baseName first, then fullName
  QString result = searchWithName(artworkDir, baseName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  return searchWithName(artworkDir, fullName, bases);
}

ErrorUtils::Result<QString>
tryFindArtworkForFile(const QString &fileName, const QString &artworkDirectory) {
  using ErrorUtils::ErrorCode;
  using ErrorUtils::ErrorContext;

  if (fileName.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument, "Empty filename",
                                 "ArtworkUtils::tryFindArtworkForFile");
  }
  if (artworkDirectory.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Empty artwork directory",
                                 "ArtworkUtils::tryFindArtworkForFile");
  }

  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return ErrorContext::warning(ErrorCode::ArtworkDirectoryNotFound,
                                 "Artwork directory does not exist",
                                 "ArtworkUtils::tryFindArtworkForFile")
        .withDetails(artworkDirectory);
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  const QString fullName = QFileInfo(fileName).fileName();
  const QStringList &bases = ExtensionUtils::imageBaseExtensions();

  // Try baseName first, then fullName
  QString result = searchWithName(artworkDir, baseName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  result = searchWithName(artworkDir, fullName, bases);
  if (!result.isEmpty()) {
    return result;
  }

  return ErrorContext::info(ErrorCode::FileNotFound,
                            "No matching artwork found",
                            "ArtworkUtils::tryFindArtworkForFile")
      .withDetails(QString("Searched for: %1 in %2").arg(fileName, artworkDirectory));
}

} // namespace ArtworkUtils
