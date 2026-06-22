#ifndef KARTEND_UTILS_APP_COLLECTION_ARCHIVEOPTIONS_H
#define KARTEND_UTILS_APP_COLLECTION_ARCHIVEOPTIONS_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 2).
// Carrier for the per-collection archive-extraction toggles. Kept in its
// own translation-unit-input so callers that only branch on these flags
// don't drag in CollectionConfig + UIConstants. collectionutils.h
// re-includes this header so existing callers compile unchanged.

#include <QHash>
#include <QString>

/// Archive-extraction toggles. Originally flat fields on CollectionConfig;
/// bundled into a focused sub-struct so the cluster's intent (run native
/// binaries that don't grok zipped content) is obvious from one accessor.
/// INI keys + kart-manifest JSON keys (`extractArchives`,
/// `extractedExtension`) round-trip unchanged.
struct ArchiveOptions {
  /// Extract archives to a temp dir before launch; cleaned up post-exit.
  bool extractArchives = false;
  /// Extension to launch from the extracted archive when the archive
  /// itself isn't directly launchable.
  QString extractedExtension;

  bool operator==(const ArchiveOptions &other) const {
    return extractArchives == other.extractArchives &&
           extractedExtension == other.extractedExtension;
  }
  bool operator!=(const ArchiveOptions &other) const { return !(*this == other); }
};

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h).
inline size_t qHash(const ArchiveOptions &key, size_t seed = 0) {
  return qHashMulti(seed, key.extractArchives, key.extractedExtension);
}

#endif
