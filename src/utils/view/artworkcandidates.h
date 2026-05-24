#ifndef KARTEND_UTILS_VIEW_ARTWORKCANDIDATES_H
#define KARTEND_UTILS_VIEW_ARTWORKCANDIDATES_H

#include <QList>
#include <QString>
#include <QStringList>

// Fuzzy candidate-ranker for the artwork assignment wizard. Given an
// item's base name and the list of image files in the collection's
// artwork directory, produces a top-N ranked list of plausible matches.
// Pure logic over input strings — the caller is responsible for the
// filesystem read so this stays unit-testable.

namespace ArtworkCandidates {

struct Candidate {
  /// Absolute path to the candidate image file.
  QString path;
  /// FuzzyMatch score (higher = better). Negative scores are filtered
  /// out so the list only contains plausible matches.
  int score = 0;
};

/// Image extensions the wizard considers as candidate artwork. Caller
/// can filter their directory listing through this set before calling
/// `rank()` to keep arbitrary files out of the candidate pool.
[[nodiscard]] const QStringList &imageExtensions();

/// True iff `filePath` ends with one of `imageExtensions()` (case-
/// insensitive). Convenience for the caller's filesystem walk.
[[nodiscard]] bool looksLikeImage(const QString &filePath);

/// Scores each entry in `candidateFiles` against `itemBaseName` using
/// FuzzyMatch::score. Candidates whose base name doesn't subsequence-
/// match are dropped. Returns the top `maxResults` by descending score.
///
/// `candidateFiles` is a list of absolute paths the caller harvested
/// from the artwork directory; the function takes care of basename
/// extraction so callers can pass paths verbatim from QDir::entryList.
[[nodiscard]] QList<Candidate> rank(const QString &itemBaseName, const QStringList &candidateFiles,
                                    int maxResults = 12);

} // namespace ArtworkCandidates

#endif // KARTEND_UTILS_VIEW_ARTWORKCANDIDATES_H
