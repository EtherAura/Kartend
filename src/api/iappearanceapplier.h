#ifndef IAPPEARANCEAPPLIER_H
#define IAPPEARANCEAPPLIER_H

/**
 * @brief Narrow role interface: push the main window's live GeneralSettings to
 * the process-wide appearance surfaces (UI font, marquee window, items-page
 * toolbar, pixmap cache).
 *
 * One of the per-domain roles IMainWindow unions (sibling of IDatAuditHost),
 * split out so a settings panel that only needs to re-apply appearance depends
 * on these four methods rather than the whole main-window surface — and so this
 * header pulls in no CollectionConfig / GeneralSettings god-headers
 * (Kartend-wu2i7, mirroring the IScrollManager six-role split). None of the
 * methods name those types: each reads the window's own live GeneralSettings
 * internally.
 *
 * Plain abstract class, not a QObject: MainWindow already derives QMainWindow
 * (its single QObject base) and picks this up through IMainWindow as a further
 * non-QObject base. Cross-cast to it with dynamic_cast, not qobject_cast.
 */
class IAppearanceApplier {
public:
  virtual ~IAppearanceApplier() = default;

  /// Apply this window's current GeneralSettings to the global QApplication
  /// font. Thin instance shim over MainWindow's static applyGlobalUiFont so
  /// settings-dialog callers don't have to name MainWindow at all.
  virtual void applyGlobalUiFontFromSettings() = 0;

  /// Sync the secondary-monitor marquee window to the current
  /// GeneralSettings.marquee* fields after a settings save. Idempotent.
  virtual void applyMarqueeSettings() = 0;

  /// Push per-button visibility flags and custom-text overrides from
  /// GeneralSettings to the items-page toolbar after a settings save.
  /// Idempotent.
  virtual void applyToolbarCustomization() = 0;

  /// Apply the user-configured pixmap cache budget (MB) to BOTH Qt's
  /// process-global QPixmapCache and the CacheManager artworkCache.
  /// Settings dialogs and startup wiring should call this single entry
  /// point rather than touching the two caches independently — historic
  /// drift between them was Kartend-10pb. The implementation also pushes
  /// the sibling on-disk artwork-cache budget (read from the window's live
  /// GeneralSettings) so the two cache budgets never diverge across call
  /// sites. Idempotent.
  virtual void applyPixmapCacheBudget(int megabytes) = 0;
};

#endif // IAPPEARANCEAPPLIER_H
