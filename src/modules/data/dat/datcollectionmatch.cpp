// DAT-to-collection match scoring. Pure token/set arithmetic — the
// only filesystem touch is the canonical-path probe behind the
// already-attached check (same fallback shape as DatCache::peek).
// The formula and its constants are documented in the header.
#include "datcollectionmatch.h"

#include <algorithm>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace DatCollectionMatch {

namespace {

/// Canonical path with raw-string fallback for missing files —
/// mirrors DatCache::peek so the library and the cache agree on file
/// identity. Missing files can't be canonicalised (realpath fails),
/// so they degrade to plain string comparison.
QString canonicalOrRaw(const QString &path) {
  if (path.isEmpty()) return path;
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return canonical.isEmpty() ? path : canonical;
}

/// Drop "(…)" and "[…]" qualifier groups. Catalogue names carry
/// revision/source suffixes ("Concert Recordings (2026 Refresh)")
/// that say nothing about which collection the DAT belongs to.
/// Non-nested groups only — nesting is unheard of in catalogue names
/// and a leftover token merely lowers Jaccard slightly.
QString stripQualifiers(const QString &name) {
  static const QRegularExpression qualifier(QStringLiteral(R"((\([^()]*\)|\[[^\[\]]*\]))"));
  QString out = name;
  out.replace(qualifier, QStringLiteral(" "));
  return out;
}

/// Lowercase tokens split on any non-alphanumeric run, deduped via
/// set semantics. Unicode-aware (\p{L}/\p{Nd}) so non-ASCII
/// collection names tokenize instead of collapsing to nothing.
QSet<QString> tokenize(const QString &name) {
  static const QRegularExpression separators(QStringLiteral(R"([^\p{L}\p{Nd}]+)"),
                                             QRegularExpression::UseUnicodePropertiesOption);
  const QStringList parts = name.toLower().split(separators, Qt::SkipEmptyParts);
  return QSet<QString>(parts.cbegin(), parts.cend());
}

/// Normalize an extension list into a comparable set. Both sides are
/// documented as lowercase/no-dot/deduped already; the cheap
/// re-normalisation here keeps a hand-built caller (or a stale
/// settings file) from silently skewing scores.
QSet<QString> extensionSet(const QStringList &extensions) {
  QSet<QString> out;
  for (const QString &raw : extensions) {
    QString ext = raw.trimmed().toLower();
    while (ext.startsWith(QLatin1Char('.'))) ext.remove(0, 1);
    if (!ext.isEmpty()) out.insert(ext);
  }
  return out;
}

/// The DAT-side name to score: header name, else header description,
/// else the file's base name — most-identifying source first. Logiqx
/// headers are optional, so every fallback step is reachable.
QString datDisplayName(const DatInfo &dat) {
  if (!dat.headerName.trimmed().isEmpty()) return dat.headerName;
  if (!dat.headerDescription.trimmed().isEmpty()) return dat.headerDescription;
  return QFileInfo(dat.path).completeBaseName();
}

/// Jaccard over token sets with the containment floor described on
/// kContainmentNameScore. Either side empty → 0 (no name signal).
double nameSimilarity(const QSet<QString> &datTokens, const QSet<QString> &collectionTokens) {
  if (datTokens.isEmpty() || collectionTokens.isEmpty()) return 0.0;
  const int shared = (datTokens & collectionTokens).size();
  const int unionSize = (datTokens | collectionTokens).size();
  double score = static_cast<double>(shared) / static_cast<double>(unionSize);
  if (shared == collectionTokens.size()) score = std::max(score, kContainmentNameScore);
  return score;
}

} // namespace

MatchResult rankCollections(const DatInfo &dat, const QList<CollectionInfo> &collections) {
  MatchResult result;

  const QSet<QString> datTokens = tokenize(stripQualifiers(datDisplayName(dat)));
  const QSet<QString> datExts = extensionSet(dat.romExtensions);
  const QString datCanonical = canonicalOrRaw(dat.path);

  // Carry the collection name through scoring so the equal-score
  // tiebreak can sort on it without a second pass over `collections`.
  struct Scored {
    Candidate candidate;
    QString collectionName;
  };
  QList<Scored> scored;

  for (const CollectionInfo &collection : collections) {
    // Already attached → fact, not proposal. Canonical-path comparison
    // so a relative / symlinked variant of an attached path still
    // counts as the same DAT. Guarded on a non-empty DAT path so an
    // empty DatInfo can't "match" an empty attached entry.
    bool attached = false;
    if (!datCanonical.isEmpty()) {
      for (const QString &attachedPath : collection.attachedDatPaths) {
        if (canonicalOrRaw(attachedPath) == datCanonical) {
          attached = true;
          break;
        }
      }
    }
    if (attached) {
      if (result.alreadyAttachedTo.isEmpty()) result.alreadyAttachedTo = collection.uuid;
      continue;
    }

    const double nameScore = nameSimilarity(datTokens, tokenize(collection.name));
    double score = kNameWeight * nameScore;
    QString reason = QStringLiteral("name match");

    const QSet<QString> collectionExts = extensionSet(collection.extensions);
    if (!datExts.isEmpty() && !collectionExts.isEmpty()) {
      const int shared = (datExts & collectionExts).size();
      if (shared > 0) {
        // Containment ratio (|∩| / |datExts|), not Jaccard: allow-lists
        // are deliberately broad, and a collection shouldn't score
        // worse for accepting more formats than this one catalogue
        // happens to use.
        score += kExtWeight * (static_cast<double>(shared) / static_cast<double>(datExts.size()));
        reason += QStringLiteral(" + extension overlap");
      } else {
        score -= kZeroExtOverlapPenalty;
        reason += QStringLiteral(", no extension overlap");
      }
    }

    score = std::clamp(score, 0.0, 1.0);
    if (score < kScoreThreshold) continue;
    scored.append(Scored{Candidate{collection.uuid, score, reason}, collection.name});
  }

  // Deterministic ranking: score descending, then collection name
  // ascending (case-insensitive first so "alpha" and "Alpha" sort
  // together, case-sensitive next, uuid last) — identical inputs
  // always yield the identical proposal order.
  std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
    if (a.candidate.score != b.candidate.score) return a.candidate.score > b.candidate.score;
    const int insensitive =
        QString::compare(a.collectionName, b.collectionName, Qt::CaseInsensitive);
    if (insensitive != 0) return insensitive < 0;
    const int sensitive = QString::compare(a.collectionName, b.collectionName);
    if (sensitive != 0) return sensitive < 0;
    return a.candidate.collectionUuid < b.candidate.collectionUuid;
  });

  result.candidates.reserve(scored.size());
  for (const Scored &s : scored) {
    result.candidates.append(s.candidate);
  }
  return result;
}

} // namespace DatCollectionMatch
