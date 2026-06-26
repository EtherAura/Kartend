#ifndef KARTEND_COLLECTION_TYPEHELPERS_H
#define KARTEND_COLLECTION_TYPEHELPERS_H

// CollectionUtils media-type/category helpers plus the deterministic UUID
// hash. Split out of the former collection/helpers.h grab-bag. These walk the
// parent chain or hash strings, so the definitions live in typehelpers.cpp
// (they touch QCryptographicHash + std::sort, which would balloon the include
// cost if dragged inline).

#include "collectionconfig.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace CollectionUtils {

/**
 * @brief Returns the effective category/type for a collection, walking up
 * the parent chain when the collection's own `type` field is empty.
 *
 * the per-collection type is optional. Subcollections can either
 * declare their own type or inherit from the nearest non-empty ancestor —
 * this matches the user's mental model of "this whole branch is Games" while
 * still letting an oddball subcollection be tagged differently. Returns an
 * empty string when nothing in the chain is tagged.
 *
 * Cycle-safe: bounds the walk by `collections.size()` so a malformed
 * parentCollectionIndex chain can't loop forever.
 */
[[nodiscard]] QString effectiveCollectionType(int collectionIndex,
                                              const QList<CollectionConfig> &collections);

/**
 * @brief Returns the set of distinct non-empty `type` labels across the full
 * collection list (roots and subcollections), case-insensitive deduped and
 * sorted alphabetically. Used to populate the toolbar filter dropdown and
 * the per-collection editor's combobox completion.
 */
[[nodiscard]] QStringList collectAllCollectionTypes(const QList<CollectionConfig> &collections);

/**
 * @brief Returns the curated built-in media-type labels offered as presets in
 * the collection-type dropdowns (creation dialog + per-collection editor).
 * The combobox stays editable, so these are suggestions rather than a closed
 * set — a user can still type a custom type. Order is display order.
 */
[[nodiscard]] QStringList standardCollectionTypes();

/**
 * @brief Builds the type-combobox item list: a leading blank entry (untagged),
 * then the standard presets, then any custom types already in use across
 * @p collections. Case-insensitive deduped; presets keep their display order
 * and custom extras are appended sorted.
 */
[[nodiscard]] QStringList collectionTypeChoices(const QList<CollectionConfig> &collections);

/**
 * @brief Computes a deterministic UUID from collection name and media
 * directory.
 * @param name Collection name.
 * @param mediaDir Media directory path.
 * @return SHA1 hash as hex string.
 */
[[nodiscard]] QString computeCollectionUuid(const QString &name, const QString &mediaDir);

/**
 * @brief Convenience overload: computes the canonical uuid for @p collection,
 * expanding its mediaDirectory through PathUtils::validateAndExpandPath — the
 * exact pairing every call site uses (Kartend audit D-07).
 */
[[nodiscard]] QString computeCollectionUuid(const CollectionConfig &collection);

/**
 * @brief Locates the collection whose canonical uuid equals @p uuid.
 * indexForUuid returns its index (or -1 when none match / @p uuid is empty);
 * findByUuid returns a pointer into @p collections (or nullptr). These replace
 * the hand-rolled "loop all, recompute each uuid, return the match" idiom
 * (Kartend audit D-07).
 */
[[nodiscard]] int indexForUuid(const QList<CollectionConfig> &collections, const QString &uuid);
[[nodiscard]] const CollectionConfig *findByUuid(const QList<CollectionConfig> &collections,
                                                 const QString &uuid);

} // namespace CollectionUtils

#endif // KARTEND_COLLECTION_TYPEHELPERS_H
