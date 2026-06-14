// Folder-structure sampling probe for the DAT auditor. See the header for
// the role; the implementation notes here cover the thresholds.
#include "datauditlayout.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>

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

} // namespace

QString layoutToken(Layout l) {
  switch (l) {
  case Layout::Flat:
    return QStringLiteral("flat");
  case Layout::Nested:
    return QStringLiteral("nested");
  case Layout::ArchivePerItem:
    return QStringLiteral("archive_per_item");
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
    if (fi.absoluteDir().canonicalPath() == canonicalRoot) {
      ++topLevel;
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
