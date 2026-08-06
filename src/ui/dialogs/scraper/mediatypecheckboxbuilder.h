#ifndef MEDIATYPECHECKBOXBUILDER_H
#define MEDIATYPECHECKBOXBUILDER_H

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>

class QCheckBox;
class QGroupBox;
class QWidget;

/// Builds the "What to scrape" group box for ScrapeResultDialog's
/// unified panel: a 32-entry checkbox grid covering every SS media
/// type plus a synthetic `_metadata` key that gates textual fields.
///
/// The curated table is the source of truth for which SS tags surface
/// in the UI. Parser aliases (box-2D → front, manuel → manual,
/// sstitle → title, ss/screenshot → screenshot) are already collapsed
/// upstream, so each entry's key equals the lowercase ScreenScraper
/// `MediaAsset::type` the runtime filter set will match against.
///
/// Translation context is fixed to "ScrapeResultDialog" via
/// Q_DECLARE_TR_FUNCTIONS so existing .ts translations of the same
/// labels (when this lived inline in the dialog) keep matching at
/// runtime — moving the strings to a fresh context would orphan them.
class MediaTypeCheckboxBuilder {
  Q_DECLARE_TR_FUNCTIONS(ScrapeResultDialog)
public:
  /// Construct the group box, populate the 3-column checkbox grid, and
  /// wire the "Select all" / "Select none" header buttons. `outChecks`
  /// is filled with the key → checkbox map the caller uses for filter
  /// derivation; callers retain ownership of the returned widget and
  /// must reparent it via a layout. Passing a null `parent` is supported
  /// (the returned widget can be reparented later by the layout owner).
  [[nodiscard]] static QGroupBox *build(QWidget *parent, QHash<QString, QCheckBox *> &outChecks);

  /// Re-tick the grid for a provider's curated default set
  /// (MetadataProviderRegistry::defaultMediaTypesForCollection): every
  /// media checkbox becomes checked iff its key is in @p types. The
  /// synthetic `_metadata` entry is left as-is — the curated sets
  /// describe media palettes, not whether text fields are wanted. No-op
  /// when @p types is empty so collections whose provider has no curated
  /// set keep the table defaults (Kartend-6e90v).
  static void applyProviderDefaults(const QHash<QString, QCheckBox *> &checks,
                                    const QStringList &types);
};

#endif // MEDIATYPECHECKBOXBUILDER_H
