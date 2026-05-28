#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

// Standalone enums (HorizontalAlignment, DetailsPane*, BackgroundType,
// ViewType, SortMode) live in collectiontypes.h so files that only need
// the type tags don't pay the cost of including UIConstants, CollectionConfig,
// and the hierarchy cache. This header re-includes that file so existing
// callers compile unchanged.
#include "collectiontypes.h"

#include <algorithm>
#include <QDir>
#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>
// Pull only the four UIConstants sub-namespaces that this header actually
// references in its inline bodies (Grid, Item, ListView, DetailsPane). The
// umbrella <uiconstants.h> aggregates all 29 subheaders (~1015 LOC) — pulling
// it here forced every one of the ~113 TUs that includes collectionutils.h to
// reparse all 29 even though only these four are used. Subheaders are
// self-contained (no includes themselves), so this is a pure preprocessor
// cost cut with no behavioural change.
#include <uiconstants/detailspaneconstants.h>
#include <uiconstants/grid.h>
#include <uiconstants/item.h>
#include <uiconstants/listview.h>

// Leaf structs progressively extracted into src/utils/app/collection/ as part
// of the Kartend-0yz3 god-header split. Re-included here so existing callers
// of collectionutils.h see the same types until the umbrella is retired.
#include "collection/archiveoptions.h"
#include "collection/collectionbackground.h"
#include "collection/collectionconfig.h"
#include "collection/collectioncontext.h"
#include "collection/collectionfilterpreferences.h"
#include "collection/collectionhierarchycache.h"
#include "collection/folderbrowsingoptions.h"
#include "collection/generalsettings.h"
#include "collection/gridlayoutpreferences.h"
#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"
#include "collection/listviewoptions.h"
#include "collection/scraperoverrides.h"
#include "collection/sidebarappearance.h"

// Kartend-jw6k cleanup: stale ErrorContext forward declaration removed —
// no validators live in this header anymore (they moved to settingsutils
// and the collection/ subheaders during the Kartend-0yz3 / -ysyn / -7uia
// splits).

// Kartend-ysyn: enum<->string converters + index validation + grid sizing +
// virtual-folder counting moved to collection/helpers.h. Re-included here so
// existing callers of collectionutils.h pick them up transparently.
#include "collection/helpers.h"

// LauncherPreset moved to collection/launcherpreset.h (Kartend-0yz3 step 1).
// LauncherConfig + LauncherUtils::{usesLibretroCore,resolvePreset} moved to
// collection/launcherconfig.h (Kartend-0yz3 step 10). resolvePreset's
// implementation still lives in collectionutils.cpp.

// CollectionFilterPreferences moved to collection/collectionfilterpreferences.h (Kartend-0yz3 step
// 6).

// LauncherProfile moved to collection/launcherconfig.h (Kartend-0yz3 step 10).

// SidebarAppearance moved to collection/sidebarappearance.h (Kartend-0yz3 step 7).

// GridLayoutPreferences moved to collection/gridlayoutpreferences.h (Kartend-0yz3 step 8).

// CollectionBackground moved to collection/collectionbackground.h (Kartend-0yz3 step 9).

// ArchiveOptions moved to collection/archiveoptions.h (Kartend-0yz3 step 2).

// FolderBrowsingOptions moved to collection/folderbrowsingoptions.h (Kartend-0yz3 step 3).

// ListViewOptions moved to collection/listviewoptions.h (Kartend-0yz3 step 4).

// ScraperOverrides moved to collection/scraperoverrides.h (Kartend-0yz3 step 5).
// CollectionConfig moved to collection/collectionconfig.h (Kartend-0yz3 step 11).

// Kartend-ysyn: index validation, grid sizing, and virtual-folder counting
// helpers moved to collection/helpers.h (already included above).

// CollectionContext moved to collection/collectioncontext.h (Kartend-0yz3 step 12).
// GeneralSettings moved to collection/generalsettings.h (Kartend-0yz3 step 13).
// CollectionHierarchyCache moved to collection/collectionhierarchycache.h
// (Kartend-0yz3 step 14); rebuild() impl still lives in collectionutils.cpp.

// Kartend-9agw: CollectionUtils:: namespace functions (computeCollectionUuid,
// hierarchicalNameFor, ancestorIndexChain, selectionSessionKeyFor,
// wouldCreateCircularReference, directChildrenOf, collectDescendantIndices,
// applyCollectionRemoval, plus the resolveX/effectiveCollectionType/
// standardCollectionTypes family) moved to collection/helpers.h (re-included
// above) so direct callers no longer have to pull in the umbrella. Definitions
// stay in collectionutils.cpp.

Q_DECLARE_METATYPE(CollectionConfig)
Q_DECLARE_METATYPE(CollectionContext)

#endif
