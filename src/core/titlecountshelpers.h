#ifndef KARTEND_CORE_TITLECOUNTSHELPERS_H
#define KARTEND_CORE_TITLECOUNTSHELPERS_H

#include "collection/collectionconfig.h"
#include <QList>
#include <QString>

class QWidget;
class LoadingOverlay;
#include "applicationcontext_fwd.h"

/// Kartend-dk2c.5: extracted from MainWindow to slim core/mainwindow.cpp.
///
/// Renders the QMainWindow window title for the active collection — picking
/// between three formats depending on whether the user is in a subfolder, at
/// the root of a collection hierarchy, or on a leaf, and appending child-part
/// counts (subfolders + subcollections) when present. Owns no state: the
/// helper takes the MainWindow as the title host plus the references it
/// needs to read the collection list / scroll totals / session counts /
/// loading-overlay gate.
///
/// Lives in src/core/ because the title is a QMainWindow-level concern; the
/// helper stays a plain free function so MainWindow keeps the public
/// refreshTitleCounts() entry point and just delegates here. Moving it
/// to its own class (TitleCountsController) would add ctor / member-state
/// boilerplate without earning testability the free-function form already
/// provides via explicit parameter passing.
namespace TitleCountsHelpers {

/// Refresh @p titleHost's window title using the current view state. No-op
/// when the database manager is unavailable, when a scan-progress overlay
/// owns the title, or when the current collection index is out of range
/// (title resets to the application name in that last case).
void refreshTitleCounts(QWidget *titleHost, const ApplicationContext &ctx,
                        const QList<CollectionConfig> &collections, int currentCollectionIndex,
                        const LoadingOverlay *loadingOverlay);

} // namespace TitleCountsHelpers

#endif // KARTEND_CORE_TITLECOUNTSHELPERS_H
