#include "multidisc.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include "pathutils.h"

namespace MultiDisc {

namespace {

// The tag group itself — parenthesised or bracketed. Matched globally so a
// name can carry several tags ("(USA) (Disc 2)") and only the disc one is
// consumed; the rest stay in the base title, where they still distinguish
// releases that differ by region or revision.
const QRegularExpression &tagGroupRe() {
  static const QRegularExpression re(QStringLiteral("[\\(\\[]([^\\)\\]]*)[\\)\\]]"));
  return re;
}

// Contents of a tag group that names a disc. The separator is optional so
// "(CD1)" parses the same as "(CD 1)"; the value is either a run of digits or
// a single letter, which is what keeps "(Disc Two)" and "(Special Edition)"
// from being read as discs — a marker we cannot ORDER is worse than none,
// since it would place discs arbitrarily in the playlist.
const QRegularExpression &discTagRe() {
  static const QRegularExpression re(QStringLiteral("^(disc|disk|cd|side)\\s*([0-9]+|[a-z])$"),
                                     QRegularExpression::CaseInsensitiveOption);
  return re;
}

} // namespace

Marker parse(const QString &fileName) {
  Marker out;
  const QString completeBase = QFileInfo(fileName).completeBaseName();

  auto it = tagGroupRe().globalMatch(completeBase);
  while (it.hasNext()) {
    const QRegularExpressionMatch group = it.next();
    const QString inside = group.captured(1).trimmed();
    const QRegularExpressionMatch disc = discTagRe().match(inside);
    if (!disc.hasMatch()) {
      continue;
    }

    QString type = disc.captured(1).toLower();
    if (type == QLatin1String("disk")) {
      type = QStringLiteral("disc"); // fold so both spellings group together
    }
    const QString value = disc.captured(2).toLower();

    out.qualifier = type + QLatin1Char(' ') + value;
    // Numbered discs sort by VALUE, so "disc 10" lands after "disc 9" instead
    // of next to "disc 1" — lexicographic order would silently shuffle the
    // playlist of any release with ten or more parts. Lettered discs sort
    // after every numbered one, far enough out that the two never interleave.
    bool numeric = false;
    const int asNumber = value.toInt(&numeric);
    out.order = numeric ? asNumber : (1000 + value.at(0).unicode());

    // Remove only this tag group, then tidy the double spacing its removal
    // leaves behind. Other tag groups survive untouched.
    QString base = completeBase;
    base.remove(group.capturedStart(0), group.capturedLength(0));
    out.base = base.simplified();
    return out;
  }
  return out;
}

QList<Group> group(const QStringList &absolutePaths) {
  // Key on directory + base title: two releases that share a name under
  // different folders are different releases, and merging them would build a
  // playlist that jumps between directories.
  struct Entry {
    QString path;
    int order;
    QString qualifier;
  };
  QHash<QString, QList<Entry>> buckets;
  QStringList bucketOrder; // keep output stable in first-seen input order

  for (const QString &path : absolutePaths) {
    const QFileInfo info(path);
    const Marker marker = parse(info.fileName());
    if (!marker.isValid() || marker.base.isEmpty()) {
      continue;
    }
    const QString key = info.absolutePath() + QChar(u'\0') + marker.base.toLower();
    if (!buckets.contains(key)) {
      bucketOrder.append(key);
    }
    buckets[key].append({path, marker.order, marker.qualifier});
  }

  QList<Group> out;
  for (const QString &key : bucketOrder) {
    QList<Entry> entries = buckets.value(key);
    // A lone "(Disc 1)" is a file that happens to be labelled, not a release
    // split across discs. Collapsing it would strip the label from the title
    // for no gain and leave a one-line playlist behind.
    if (entries.size() < 2) {
      continue;
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
      if (a.order != b.order) {
        return a.order < b.order;
      }
      return a.qualifier < b.qualifier;
    });

    Group g;
    // Recover the base with its original casing from the first member rather
    // than using the lowercased key.
    g.base = parse(QFileInfo(entries.first().path).fileName()).base;
    for (const Entry &e : entries) {
      g.files.append(e.path);
    }
    out.append(g);
  }
  return out;
}

QString buildM3uContents(const QStringList &absolutePaths, const QString &m3uDir) {
  QString out;
  // Both sides of the sibling compare go through QFileInfo: on Windows,
  // QFileInfo::absolutePath() drive-qualifies a drive-less absolute path
  // ("/m" -> "C:/m") while QDir::absolutePath() hands the clean input back
  // verbatim — the asymmetry made the compare below never match on the
  // first Windows run of this suite. Real callers pass drive-qualified
  // playlist dirs, where the two forms are identical.
  const QString dirAbs =
      m3uDir.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(m3uDir).absoluteFilePath());
  for (const QString &path : absolutePaths) {
    QString line = path;
    if (!dirAbs.isEmpty()) {
      const QFileInfo info(path);
      // Relative only for files sitting directly in the playlist's own
      // directory. Anything deeper or outside stays absolute: a "../.." chain
      // is more fragile than the absolute path it replaces.
      if (info.absolutePath() == dirAbs) {
        line = info.fileName();
      }
    }
    out += line;
    out += QLatin1Char('\n');
  }
  return out;
}

QString playlistDirFor(const QString &appDataDir, const QString &collectionUuid) {
  const QString uuid = collectionUuid.trimmed();
  if (appDataDir.isEmpty() || uuid.isEmpty()) {
    // Never fall back to the shared root: a caller cleaning up "this
    // collection's playlists" would then be handed every collection's.
    return {};
  }
  // The uuid is Kartend-generated, but this path is handed to directory
  // removal, so refuse anything that could climb out of the managed root
  // rather than trusting that it always will be.
  if (uuid.contains(QLatin1Char('/')) || uuid.contains(QLatin1Char('\\')) ||
      uuid.contains(QStringLiteral(".."))) {
    return {};
  }
  return QDir::cleanPath(appDataDir + QStringLiteral("/multi-disc/") + uuid);
}

DiscMetadata mergeDiscs(const QList<DiscMetadata> &discsInOrder) {
  DiscMetadata out;
  if (discsInOrder.isEmpty()) {
    return out;
  }

  // Case-insensitive membership sets, kept alongside the ordered output so the
  // result is stable across runs rather than hash-ordered.
  QSet<QString> seenTags;
  QSet<QString> seenFieldKeys;

  for (const DiscMetadata &disc : discsInOrder) {
    // Scalars: first non-empty wins, so disc 1 is authoritative and a later
    // disc only fills a gap. Never overwrite — that is the same rule the
    // importers and the scraper follow, and it is what makes collapsing safe
    // to run over a library the user has already curated.
    if (out.artworkPath.isEmpty()) {
      out.artworkPath = disc.artworkPath;
    }
    if (out.notes.isEmpty()) {
      out.notes = disc.notes;
    }
    if (out.sourceUrl.isEmpty()) {
      out.sourceUrl = disc.sourceUrl;
    }
    if (out.rating < 0) {
      out.rating = disc.rating;
    }

    for (const QString &tag : disc.tags) {
      const QString key = tag.trimmed().toLower();
      if (key.isEmpty() || seenTags.contains(key)) {
        continue;
      }
      seenTags.insert(key);
      out.tags.append(tag.trimmed());
    }

    for (const auto &[fieldKey, fieldValue] : disc.customFields) {
      const QString key = fieldKey.trimmed().toLower();
      if (key.isEmpty() || seenFieldKeys.contains(key)) {
        continue; // disc 1 wins a collision; later discs only add new keys
      }
      seenFieldKeys.insert(key);
      out.customFields.append({fieldKey, fieldValue});
    }

    // Flags OR: marking disc 2 "continue later" says something about the
    // release, and dropping it on collapse would discard a deliberate act.
    out.pinned = out.pinned || disc.pinned;
    out.hidden = out.hidden || disc.hidden;
    out.continueLater = out.continueLater || disc.continueLater;
  }
  return out;
}

QString m3uFileNameFor(const QString &base) {
  // Kartend-tb5nb: delegates to the shared rule set rather than carrying its
  // own. This used to do the character replacement ALONE — no trailing-dot
  // chop, no C0/NUL strip, no leading-dash strip, no length cap — which had
  // silently diverged from the launcher-import stub namer in a sibling module.
  //
  // Underscore stays the substitute so names that were already legal resolve to
  // the same .m3u file on disk as before. ':' is passed as an extra because a
  // playlist may be written to a Windows/SMB share, which refuses it — the stub
  // namer keeps colons on purpose, so it is a per-caller choice, not shared.
  return PathUtils::sanitizeFileBaseName(base, QStringLiteral("_"), QStringLiteral("playlist"),
                                         QStringLiteral(":")) +
         QStringLiteral(".m3u");
}

} // namespace MultiDisc
