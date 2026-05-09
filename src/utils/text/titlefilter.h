#ifndef TITLEFILTER_H
#define TITLEFILTER_H

#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

struct CollectionConfig;

// Per-collection title cleanup engine.
//
// Each collection can carry a list of regex patterns (CollectionConfig::
// titleExclusionPatterns) plus an enable toggle (titleExclusionEnabled). When
// the toggle is on for a given collection, any displayed item title belonging
// to that collection has every pattern stripped via QString::remove() in
// declaration order, then trailing/leading whitespace is collapsed.
//
// The registry is a process-wide singleton because the title pipeline is
// touched from both the main thread (UI consumers, scroll/filter helpers)
// and from the DatabaseManager interception path; cross-thread reads are
// guarded by a QReadWriteLock so writes from settings save are exclusive.
//
// Patterns that fail to compile are skipped silently (logged once) so a typo
// in a single regex can't wedge the whole collection's titles.
namespace TitleFilter {

// Replaces all stored patterns for the supplied collection list. Call once
// after SettingsManager has loaded/saved CollectionConfigs so the registry
// matches the persisted state.
void rebuildFromCollections(const QList<CollectionConfig> &collections);

// Returns the supplied display name with this collection's enabled patterns
// applied. Returns the original name unchanged when the collection has no
// patterns or is disabled, when collectionIndex is -1, or when the registry
// has not been populated yet.
[[nodiscard]] QString apply(int collectionIndex, const QString &displayName);

// Resets the registry. Used by tests; production code calls
// rebuildFromCollections() instead.
void clearForTests();

} // namespace TitleFilter

#endif // TITLEFILTER_H
