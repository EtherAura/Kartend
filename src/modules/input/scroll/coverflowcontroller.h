#ifndef COVERFLOWCONTROLLER_H
#define COVERFLOWCONTROLLER_H

#include "collection/collectioncontext.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QScrollArea;
class QTimer;
class QWidget;
class CoverFlowWidget;
struct CoverFlowCardData;
class ScrollDataStore;
class IDatabaseManager;
class IFilterManager;
#include "applicationcontext_fwd.h"

namespace TimerUtils {
class DebouncedTimer;
}

/**
 * @brief Setup struct for CoverFlowController dependencies.
 *
 * Every field is borrowed — CoverFlowController owns only its CoverFlowWidget
 * (Qt-parented to the items-page content widget) and the resolve debouncer.
 */
struct CoverFlowControllerSetup {
  const ApplicationContext *ctx = nullptr;
  QScrollArea *mediaScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  const CollectionContext *context = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
  ScrollDataStore *dataManager = nullptr;
  // Kartend-yeik: removed FilterManager *filterManager; CoverFlowController
  // now reads filter state via m_ctx->filterManager(), which the
  // initializeAppContext seed populates from ScrollManager.
};

/**
 * @brief Drives the CoverFlow ViewType — owns the CoverFlowWidget and keeps its
 * card list, appearance config, and visibility in sync with the grid.
 *
 * Extracted from ScrollManager. CoverFlowWidget lives as a sibling of the grid
 * container in the items-page layout; when the active collection's view type is
 * CoverFlow the grid + scrollbars hide and the carousel shows. Selection
 * changes from carousel input round-trip back through SelectionManager via the
 * selectItemByIndex signal so sidebar / restore / persistence stay coherent.
 *
 * Borrows every dependency; the per-item preview video + artwork gallery
 * lookups are resolved lazily and debounced so a wheel sweep across the
 * carousel triggers one DB + filesystem pass at the trailing edge.
 */
class CoverFlowController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CoverFlowController)
public:
  explicit CoverFlowController(QObject *parent = nullptr);
  ~CoverFlowController() override;

  void setupReferences(const CoverFlowControllerSetup &setup);

  /// True when the active collection's view type is CoverFlow.
  [[nodiscard]] bool isActive() const;

  /// Idempotent: lazily creates the CoverFlowWidget and wires its signals.
  void ensureWidget();
  /// Push the active collection's appearance config onto the widget.
  void applyConfig();
  /// Show/hide the carousel vs the grid and adjust scrollbar policy. Emits
  /// activeChanged() so listeners can yield viewport space to the carousel.
  void applyVisibility();
  /// Rebuild the flat card list from the current (filtered) visual-index space.
  void rebuildCards();
  /// rebuildCards() gated on the widget existing and being active — for the
  /// data/filter refresh paths that should not pay the per-item descriptor
  /// cost while in grid/list mode.
  void rebuildCardsIfActive();
  /// Incremental sibling of rebuildCardsIfActive() for range-chunk arrivals
  /// (Kartend-x7bn8): patches only the cards whose backing data changed
  /// (@p updatedIndices, in unfiltered visual-index space — exactly what
  /// ScrollDataStore::receiveItemsRange returns) instead of re-deriving all
  /// N descriptors with per-item DB resolution on every chunk. Falls back to
  /// a full rebuildCards() when a filter is active (the chunk indices are
  /// actual-space and IFilterManager has no reverse mapping) or when the
  /// widget's card count no longer matches the store (count change ⇒ the
  /// whole list shifted). No-op while the carousel is hidden.
  void updateCardsIfActive(const QList<int> &updatedIndices);
  /// ensure + config + rebuild + applyVisibility, all unconditional. Safe on
  /// every view-type transition because ensureWidget() is idempotent.
  void refreshForViewTypeChange();

  /// Keep the carousel's centered card in sync with the canonical selection
  /// and (debounced) resolve its preview video + artwork gallery.
  void onSelectionChanged(int selectedIndex);

  [[nodiscard]] CoverFlowWidget *widget() const { return m_widget; }

  /// Number of carousel slots still waiting on a cold artwork-directory
  /// cache (Kartend-6x8tn). Observability for tests / diagnostics only.
  [[nodiscard]] int pendingArtworkCount() const { return m_pendingArtwork.size(); }
  /// True while the trailing artwork-retry timer is armed (Kartend-6x8tn).
  /// Observability for tests / diagnostics only.
  [[nodiscard]] bool artworkRetryActive() const;

signals:
  void selectItemByIndex(int index);
  void subcollectionEntered(int subcollectionIndex);
  void virtualFolderEntered(const QString &folderPath);
  void itemActivated(int visualIndex);
  void activeChanged(bool active);
  /// Forwarded from CoverFlowWidget: the user double-clicked a gallery-strip
  /// thumbnail and wants @p path shown full size (Kartend-5jtyw). ScrollManager
  /// connects this to its own preview role — see the connect site for why the
  /// controller forwards instead of reaching for ctx->scrollPreview().
  void galleryPreviewRequested(const QString &path, bool isVideo);

private:
  void resolveAndPushVideo(int visualIndex);
  void resolveAndPushGallery(int visualIndex);

  /// Build the card descriptor for one actual (unfiltered) index — the
  /// per-item body shared by rebuildCards() and updateCardsIfActive().
  [[nodiscard]] CoverFlowCardData buildCard(int actualIndex, IDatabaseManager *db) const;

  // ── Pending-artwork retry (Kartend-6x8tn) ────────────────────────────
  // resolveCardArtworkPath is cache-only, so cards built against a cold
  // DirectoryCache come back with an empty artworkPath and would stay
  // blank forever on normal collections (Kartend-x7bn8 removed the
  // per-chunk full rebuilds that used to self-heal them). These helpers
  // track those slots, prewarm their directories off-thread, and patch
  // just the pending cards once the cache is warm.

  /// Queue @p visualIndex for the trailing retry unless every directory its
  /// cover lookup probes is already warm — only then is an empty result a
  /// real "artless" rather than a not-yet-scanned one (Kartend-t4rjw). Adds
  /// the artwork directory to @p dirsToWarm. @p settledByDir memoizes the
  /// per-directory verdict across one pass and must not outlive it.
  void notePendingArtwork(int visualIndex, int actualIndex, IDatabaseManager *db,
                          QSet<QString> &dirsToWarm, QHash<QString, bool> &settledByDir);
  /// schedulePrewarm() the full lookup cascade of every directory in
  /// @p artworkDirs — root plus typed cover subdirs, not just the root
  /// (Kartend-t4rjw). No-op for an empty set.
  static void prewarmArtworkCascades(const QSet<QString> &artworkDirs);
  /// prewarmArtworkCascades() @p dirsToWarm and start the retry timer with a
  /// fresh attempt budget when anything is pending.
  void armArtworkRetry(const QSet<QString> &dirsToWarm);
  /// Timer body: re-run buildCard for pending slots whose directory is now
  /// cached; re-arm (bounded) while still-cold directories remain.
  void retryPendingArtwork();
  /// Drop all pending slots, reset the attempt budget, stop the timer.
  void clearArtworkRetry();
  /// The directory resolveCardArtworkPath would search for @p actualIndex
  /// — empty for non-media indices.
  [[nodiscard]] QString artworkDirForActual(int actualIndex, IDatabaseManager *db) const;

  /// Kartend-yeik: ctx-routed FilterManager accessor. Replaces the old
  /// m_filterManager pointer-as-setup-struct-field pattern. Returns the
  /// IFilterManager interface — the cover-flow controller only needs the
  /// read-side surface (isFiltered/getActualIndex/filteredCount).
  [[nodiscard]] IFilterManager *filterMgr() const;

  // Borrowed dependencies — never owned, never deleted through these.
  const ApplicationContext *m_ctx = nullptr;
  QScrollArea *m_mediaScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  const CollectionContext *m_context = nullptr;
  const QList<CollectionConfig> *m_collections = nullptr;
  ScrollDataStore *m_dataManager = nullptr;

  // The carousel widget is parented to the items-page content widget, not to
  // this controller — Qt owns its lifetime. Raw pointer, never deleted here.
  CoverFlowWidget *m_widget = nullptr;
  TimerUtils::DebouncedTimer *m_resolveDebouncer = nullptr;
  int m_pendingVisualIndex = -1;

  // Kartend-6x8tn: carousel slots whose primary artwork resolved empty
  // against a still-cold DirectoryCache, keyed by visual index → the artwork
  // directory the lookup searches (its root; the probed cascade is derived
  // from that). The bounded trailing retry re-runs buildCard for just these
  // slots once the whole cascade is cached — positive entry patches the card,
  // an all-warm empty means genuinely artless and the slot is dropped
  // (Kartend-t4rjw: keying that decision on the root alone dropped cards
  // whose cover sat in a subdir the prewarm had not reached yet).
  QHash<int, QString> m_pendingArtwork;
  QTimer *m_artworkRetryTimer = nullptr;
  int m_artworkRetryAttempts = 0;
};

#endif
