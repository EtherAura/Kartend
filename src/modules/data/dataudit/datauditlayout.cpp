// Folder-structure sampling probe for the DAT auditor. See the header for
// the role; the implementation notes here cover the thresholds.
#include "datauditlayout.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>
#include <QSet>

#include "romhasher.h"

namespace DatAudit {

namespace {

// Sample cap: enough files for the fractions to be stable on real libraries,
// small enough that the probe stays interactive on a cold spinning disk. The
// walk is breadth-bounded by this cap, not by depth.
constexpr int kMaxSample = 512;

// Fraction thresholds. Archive-per-item needs a clear majority (0.7) because
// the consequence — switching to archive-member hashing — is the most
// invasive; between 0.3 and 0.7 the mix is reported as Mixed rather than
// guessed. Flat vs Nested splits at 0.8/0.2 top-level fraction: real flat
// sets routinely carry a stray subfolder of extras, and real nested sets a
// few loose files in the root.
constexpr double kArchiveMajority = 0.7;
constexpr double kArchiveMinority = 0.3;
constexpr double kTopLevelMajority = 0.8;
constexpr double kTopLevelMinority = 0.2;

// Subfolder-per-item refinement (Kartend-m6qsb.12) of the subfolder-dominant
// case. Distinguished from generic Nested by three signals: the root is nearly
// empty (kSubfolderTopMax — keeps a flat set with sidecar folders out, since
// its content still sits in the root), files sit one level deep rather than in
// a deep tree (kSubfolderShallowMajority), and each item folder holds a
// multi-file set rather than a single file (kMultiFilePerFolder). Need at least
// a couple of item folders before it's a "pattern".
constexpr double kSubfolderTopMax = 0.1;
constexpr double kSubfolderShallowMajority = 0.8;
constexpr double kMultiFilePerFolder = 1.5;
constexpr int kMinItemFolders = 2;

} // namespace

QString layoutToken(Layout l) {
  switch (l) {
  case Layout::Flat:
    return QStringLiteral("flat");
  case Layout::Nested:
    return QStringLiteral("nested");
  case Layout::ArchivePerItem:
    return QStringLiteral("archive_per_item");
  case Layout::SubfolderPerItem:
    return QStringLiteral("subfolder_per_item");
  case Layout::Mixed:
    return QStringLiteral("mixed");
  case Layout::Unknown:
    break;
  }
  return QString();
}

Layout layoutFromToken(const QString &token) {
  if (token == QLatin1String("flat")) {
    return Layout::Flat;
  }
  if (token == QLatin1String("nested")) {
    return Layout::Nested;
  }
  if (token == QLatin1String("archive_per_item")) {
    return Layout::ArchivePerItem;
  }
  if (token == QLatin1String("subfolder_per_item")) {
    return Layout::SubfolderPerItem;
  }
  if (token == QLatin1String("mixed")) {
    return Layout::Mixed;
  }
  return Layout::Unknown;
}

LayoutDetection detectLayout(const QString &root, const QStringList &relevantExtensions) {
  LayoutDetection out;
  const QString trimmedRoot = root.trimmed();
  if (trimmedRoot.isEmpty() || !QFileInfo(trimmedRoot).isDir()) {
    out.evidence = QObject::tr("scan root is not a readable folder");
    return out;
  }

  // Normalised allow-list (lowercase, no dots) for the relevance filter.
  QStringList relevant;
  relevant.reserve(relevantExtensions.size());
  for (const QString &e : relevantExtensions) {
    QString norm = e.trimmed().toLower();
    while (norm.startsWith(QLatin1Char('.'))) {
      norm.remove(0, 1);
    }
    if (!norm.isEmpty()) {
      relevant.append(norm);
    }
  }

  const QString canonicalRoot = QFileInfo(trimmedRoot).canonicalFilePath();
  int sampled = 0;
  int topLevel = 0;
  int archives = 0;
  int depth1Files = 0;       // file's parent is an immediate child of root
  int deeperFiles = 0;       // file nested two or more levels down
  QSet<QString> itemFolders; // distinct immediate subfolders that hold sampled files
  QDirIterator it(trimmedRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (it.hasNext() && sampled < kMaxSample) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    const bool isArchive = RomHasher::isArchivePath(fi.fileName());
    // Archives always count (they may not appear in the allow-list even in an
    // archive-per-item layout — the list usually names the INNER formats);
    // everything else passes the relevance filter when one was supplied.
    if (!isArchive && !relevant.isEmpty() && !relevant.contains(fi.suffix().toLower())) {
      continue;
    }
    ++sampled;
    if (isArchive) {
      ++archives;
    }
    const QString parentCanonical = fi.absoluteDir().canonicalPath();
    if (parentCanonical == canonicalRoot) {
      ++topLevel;
    } else {
      // In a subfolder: separate depth-1 (immediate child of root) from deeper
      // nesting, and remember the immediate folder so multi-file sets can be
      // told apart from a deep tree (Kartend-m6qsb.12).
      const QString rel = QDir(canonicalRoot).relativeFilePath(parentCanonical);
      itemFolders.insert(rel.section(QLatin1Char('/'), 0, 0));
      if (rel.contains(QLatin1Char('/'))) {
        ++deeperFiles;
      } else {
        ++depth1Files;
      }
    }
  }

  out.sampledFiles = sampled;
  if (sampled == 0) {
    out.evidence = QObject::tr("no relevant files found to sample");
    return out;
  }

  const double archiveFrac = double(archives) / double(sampled);
  const double topFrac = double(topLevel) / double(sampled);

  if (archiveFrac >= kArchiveMajority) {
    out.layout = Layout::ArchivePerItem;
    out.confidence = archiveFrac;
    out.evidence = QObject::tr("%1% of %2 sampled files are archives")
                       .arg(qRound(archiveFrac * 100))
                       .arg(sampled);
    return out;
  }
  if (archiveFrac >= kArchiveMinority) {
    out.layout = Layout::Mixed;
    out.confidence = 0.5;
    out.evidence = QObject::tr("archives and loose files are mixed (%1% archives of %2 sampled)")
                       .arg(qRound(archiveFrac * 100))
                       .arg(sampled);
    return out;
  }
  if (topFrac >= kTopLevelMajority) {
    out.layout = Layout::Flat;
    out.confidence = topFrac;
    out.evidence = QObject::tr("%1% of %2 sampled files sit directly in the root")
                       .arg(qRound(topFrac * 100))
                       .arg(sampled);
    return out;
  }
  if (topFrac <= kTopLevelMinority) {
    // Refine the subfolder-dominant case: a true subfolder-per-item layout
    // (root nearly empty, files one level deep, multi-file sets per folder) vs
    // a generic Nested tree (Kartend-m6qsb.12).
    const int subdirFiles = depth1Files + deeperFiles;
    const int itemFolderCount = itemFolders.size();
    const double shallowFrac = subdirFiles > 0 ? double(depth1Files) / double(subdirFiles) : 0.0;
    const double filesPerFolder =
        itemFolderCount > 0 ? double(depth1Files) / double(itemFolderCount) : 0.0;
    if (topFrac <= kSubfolderTopMax && itemFolderCount >= kMinItemFolders &&
        shallowFrac >= kSubfolderShallowMajority && filesPerFolder >= kMultiFilePerFolder) {
      out.layout = Layout::SubfolderPerItem;
      out.confidence = shallowFrac;
      out.evidence = QObject::tr("%1 item folders, ~%2 files each (of %3 sampled)")
                         .arg(itemFolderCount)
                         .arg(QString::number(filesPerFolder, 'f', 1))
                         .arg(sampled);
      return out;
    }
    out.layout = Layout::Nested;
    out.confidence = 1.0 - topFrac;
    out.evidence = QObject::tr("%1% of %2 sampled files sit in subfolders")
                       .arg(qRound((1.0 - topFrac) * 100))
                       .arg(sampled);
    return out;
  }
  out.layout = Layout::Mixed;
  out.confidence = 0.5;
  out.evidence = QObject::tr("files are split between the root and subfolders (%1% in root of %2 "
                             "sampled)")
                     .arg(qRound(topFrac * 100))
                     .arg(sampled);
  return out;
}

} // namespace DatAudit
