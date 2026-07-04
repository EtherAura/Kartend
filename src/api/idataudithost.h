#ifndef IDATAUDITHOST_H
#define IDATAUDITHOST_H

#include <QString>
#include <QtGlobal>

struct CollectionConfig;

/**
 * @brief Narrow role interface: open the DAT-audit window for a collection and
 * report the last audit outcome for its linked profile.
 *
 * One of the per-domain roles IMainWindow unions (sibling of IAppearanceApplier),
 * split out so the collection-settings panel depends on just the DAT-audit
 * surface rather than the whole main-window contract (Kartend-wu2i7, mirroring
 * the IScrollManager six-role split). Takes CollectionConfig by const-ref so
 * this header forward-declares it instead of pulling the collectionconfig.h
 * god-header.
 *
 * Plain abstract class, not a QObject: MainWindow already derives QMainWindow
 * (its single QObject base) and picks this up through IMainWindow as a further
 * non-QObject base. Cross-cast to it with dynamic_cast, not qobject_cast.
 */
class IDatAuditHost {
public:
  virtual ~IDatAuditHost() = default;

  /// Open the DAT Audit window aimed at @p collection: selects the linked audit
  /// profile, or seeds an unsaved one from it. The caller passes the collection
  /// by value so the collection-settings panel can hand over its *working* copy
  /// (unsaved media-dir / DAT-list edits included), not a saved-list lookup
  /// (Kartend-4mqkof / Kartend-6wn0p). Goes through this role so the ui/ panel
  /// need not #include the concrete MainWindow.
  virtual void openDatAuditForCollection(const CollectionConfig &collection) = 0;

  /// Persisted outcome of the most recent completed audit for the profile
  /// linked to a collection. Default-constructed = "never audited / nothing
  /// linked". `present` counts catalogue entries whose content exists on disk
  /// (Have + WrongName). Lives here (not utils/db) so panel code consumes one
  /// interface type instead of the profile store's status-int hash.
  struct DatAuditStatus {
    qint64 lastScanMs = 0;
    int present = 0;
    int missing = 0;
    bool hasResults = false;
  };

  /// Status of the audit profile linked to @p collectionUuid (most recently
  /// updated profile wins, matching the audit dialog's own selection). Lets
  /// the collection-settings panel show "last audited N ago · X present · Y
  /// missing" without DB access of its own (Kartend-4mqkof, Kartend-m6qsb.8).
  [[nodiscard]] virtual DatAuditStatus
  datAuditStatusForCollection(const QString &collectionUuid) = 0;
};

#endif // IDATAUDITHOST_H
