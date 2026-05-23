#include "artworkcandidates.h"

#include <algorithm>

#include <QFileInfo>

#include "fuzzymatch.h"

namespace ArtworkCandidates {

const QStringList &imageExtensions() {
  static const QStringList exts = {
      QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
      QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
  };
  return exts;
}

bool looksLikeImage(const QString &filePath) {
  const QString ext = QFileInfo(filePath).suffix().toLower();
  return imageExtensions().contains(ext);
}

QList<Candidate> rank(const QString &itemBaseName, const QStringList &candidateFiles,
                      int maxResults) {
  QList<Candidate> out;
  if (itemBaseName.isEmpty() || candidateFiles.isEmpty() || maxResults <= 0) {
    return out;
  }
  out.reserve(candidateFiles.size());
  for (const QString &path : candidateFiles) {
    if (!looksLikeImage(path)) {
      continue;
    }
    const QString base = QFileInfo(path).completeBaseName();
    const int s = FuzzyMatch::score(itemBaseName, base);
    if (s < 0) {
      continue;
    }
    out.append(Candidate{path, s});
  }
  // Sort high score first; secondary stable sort by path so identical
  // scores produce a deterministic order across runs (helps with tests
  // and predictable UI).
  std::stable_sort(out.begin(), out.end(),
                   [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
  if (out.size() > maxResults) {
    out = out.mid(0, maxResults);
  }
  return out;
}

} // namespace ArtworkCandidates
