#ifndef KARTEND_MODULES_DATA_SETTINGS_COLLECTIONDIFFFINGERPRINT_H
#define KARTEND_MODULES_DATA_SETTINGS_COLLECTIONDIFFFINGERPRINT_H

// Kartend-lc58a: cheap snapshot for the per-collection hot-reload diff that
// SettingsManager::saveCollections runs after every write.
//
// Previously the diff baseline (m_lastSavedCollections) retained a full
// by-value QList<CollectionConfig>. CollectionConfig is a god-struct embedding
// nine leaf clusters (each with its own QHash/QList/QStringList members), so on
// a populous library every save effectively deep-copied all of those containers
// — and because the baseline shared QList copy-on-write storage with the live
// list, the live list's next mutation (a sidebar drag, a tab switch, a filter
// toggle) forced the detach/deep-copy then. saveCollections fires on many such
// ordinary UI events, so the cost was paid constantly.
//
// emitPerCollectionDiffs never needs the old VALUES — it emits the NEW
// sub-cluster value and only needs to know WHICH of the nine diffed clusters
// changed since the last save. So the baseline only has to retain a fingerprint
// of each cluster, not a copy. This struct is that fingerprint: one hash per
// diffed cluster, keyed by collection UUID in the baseline map.
//
// Faithfulness: each cluster hash comes from the qHash overload co-located with
// that cluster's operator== (gridlayoutpreferences.h, sidebarappearance.h, …),
// which hashes exactly the operator== fields. Equal clusters therefore always
// fingerprint equal, so a real change is never missed (no false-clean diff). A
// hash collision could in principle make two DIFFERENT clusters fingerprint
// equal and skip one fine-grained signal, but at 64-bit width that is ~2^-64
// per change, and the coarse collectionsModified() still fires on every save —
// so the global refresh path is unaffected regardless.

#include <cstddef>

#include "collection/collectionconfig.h"

struct CollectionDiffFingerprint {
  std::size_t gridLayout = 0;
  std::size_t sidebar = 0;
  std::size_t background = 0;
  std::size_t listView = 0;
  std::size_t archive = 0;
  std::size_t folderBrowsing = 0;
  std::size_t filter = 0;
  std::size_t scraperOverrides = 0;
  std::size_t launcher = 0;
};

// Hashes the nine leaf clusters emitPerCollectionDiffs compares. Seed 0 keeps
// the result deterministic within a process run, which is all the diff needs:
// the baseline and the post-save recompute always use the same seed, so equal
// clusters compare equal. (No cross-process or cross-Qt-version stability is
// required — the fingerprints never leave memory.)
inline CollectionDiffFingerprint collectionDiffFingerprint(const CollectionConfig &c) {
  return CollectionDiffFingerprint{
      qHash(c.gridLayout), qHash(c.sidebar),          qHash(c.background),
      qHash(c.listView),   qHash(c.archive),          qHash(c.folderBrowsing),
      qHash(c.filter),     qHash(c.scraperOverrides), qHash(c.launcher)};
}

#endif
