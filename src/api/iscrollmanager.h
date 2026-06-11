#ifndef ISCROLLMANAGER_H
#define ISCROLLMANAGER_H

#include "iartworkpreviewscroll.h"
#include "igridlayoutscroll.h"
#include "iscrolldatasource.h"
#include "isearchstatescroll.h"
#include "iselectionoverlayscroll.h"
#include "ivirtualscrolllifecycle.h"
#include <QObject>

/**
 * @brief Sibling-facing facade to the virtual-scrolling / grid layer.
 *
 * Kartend-h1l8f: pure union of six role interfaces (interface-segregation),
 * carrying no methods of its own. Each method lives on exactly one role,
 * grouped to match the extracted sub-managers:
 *
 *   IVirtualScrollLifecycle  ctx->scrollLifecycle()  setup/teardown + data feed
 *   IGridLayoutScroll        ctx->scrollGrid()       metrics, layout, view refresh
 *   ISelectionOverlayScroll  ctx->scrollOverlay()    selection overlay + restore
 *   ISearchStateScroll       ctx->scrollSearch()     filter + pre-search state
 *   IArtworkPreviewScroll    ctx->scrollPreview()    artwork/media preview overlay
 *   IScrollDataSource        ctx->scrollData()       raw data + subcollection queries
 *
 * Consumers should reach for the narrowest role accessor(s); the facade stays
 * for consumers spanning three or more roles, for Qt signal connections, and
 * for QObject-based lifetime guards (QPointer, singleShotGuarded) — the roles
 * are plain abstract classes, so QObject-ness is only available here.
 *
 * Unlike INavigationManager / IDetailsPaneManager (plain abstract classes),
 * this facade is a QObject — the same shape as IDatabaseManager. A sibling
 * (SelectionRestoreCoordinator) connects to ScrollManager's
 * virtualScrollSetupComplete signal through ctx, so that one signal is
 * promoted here; QObject + Q_OBJECT is required to declare it, and QObject
 * must stay a single base, which is why the roles cannot carry it.
 * ScrollManager keeps its remaining signals/slots on the concrete class and
 * derives this interface directly (single QObject base).
 *
 * setArtworkPreviewGallery() is intentionally absent: its parameter type
 * (ArtworkPreviewOverlay::GalleryEntry) is a nested type from a ui/ header
 * and cannot reach this neutral api/ header. Its sole caller obtains the
 * concrete ScrollManager for that one call.
 */
class IScrollManager : public QObject,
                       public IVirtualScrollLifecycle,
                       public IGridLayoutScroll,
                       public ISelectionOverlayScroll,
                       public ISearchStateScroll,
                       public IArtworkPreviewScroll,
                       public IScrollDataSource {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(IScrollManager)
public:
  using QObject::QObject;
  ~IScrollManager() override = default;

signals:
  void virtualScrollSetupComplete();
};

#endif // ISCROLLMANAGER_H
