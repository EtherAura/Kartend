#ifndef CREATECOLLECTIONDIALOG_H
#define CREATECOLLECTIONDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

#include "screenscrapersystems.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

/// Modal dialog for creating a new collection. Collects the name plus the
/// fields a user almost always wants set up front — content folder, artwork
/// folder, launcher, media type, and metadata scraper — so a fresh collection
/// is usable without an immediate follow-up trip through the settings dialog.
///
/// The media-type combo is editable and seeded with curated presets (Video,
/// Audio, Images, Documents, Games); a custom type can still be typed. The
/// scraper combo auto-follows the type (Video → TMDB, Games → ScreenScraper,
/// …) until the user picks one by hand — that explicit override is how a
/// custom-typed collection gets a scraper assigned. Two rows are conditional:
/// a ScreenScraper system row shows only for a game media type, and a
/// libretro core row only when the launcher path is RetroArch. Every field
/// except the name is optional and editable later from the collection's
/// Configuration tab.
class CreateCollectionDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CreateCollectionDialog)
public:
  explicit CreateCollectionDialog(QWidget *parent = nullptr);
  ~CreateCollectionDialog() override;

  /// Show an explanatory paragraph above the form — used by the "no
  /// collections exist yet" prompt. The label stays hidden when this is
  /// never called.
  void setIntroText(const QString &text);

  [[nodiscard]] QString collectionName() const;
  [[nodiscard]] QString contentPath() const;
  [[nodiscard]] QString artworkDirectory() const;
  [[nodiscard]] QString launcherPath() const;
  /// Libretro core path, or empty when the launcher isn't RetroArch — the
  /// row is hidden then and a stale value must not leak into the config.
  [[nodiscard]] QString corePath() const;
  [[nodiscard]] QString collectionType() const;
  /// Scraper override to persist. Empty ("automatic" — resolve from the
  /// media type at scrape time) while the scraper merely tracks the
  /// type; a concrete provider id ("tmdb", "screenscraper", …) once the
  /// user has pinned one by hand.
  [[nodiscard]] QString scraperProviderId() const;
  /// ScreenScraper.fr system id for the new collection, or -1 (Auto-detect).
  /// Always -1 unless the media type is a game category — the row is hidden
  /// and the value irrelevant for other types.
  [[nodiscard]] int screenscraperSystemId() const;

  /// Point the Core picker at a RetroArch install (a retroarch.cfg file
  /// or a core directory). Empty auto-detects the standard location.
  /// Call before exec() so the detected-cores dropdown is populated.
  void setRetroarchConfigOverride(const QString &path);

  /// Security-validate the folder / launcher / core path fields before
  /// accepting, mirroring SettingsDialog::saveCollectionFromUI. A path with
  /// shell metacharacters must be rejected at creation rather than only on a
  /// later re-save (Kartend-nkzx). The name is already gated live via the OK
  /// button, so only the path fields are checked here.
  void accept() override;

private:
  void buildUi();
  /// Re-point the scraper combo at the default provider for the current
  /// media type. No-op once the user has picked a scraper by hand.
  void syncScraperToType();
  /// Re-point the ScreenScraper system combo at the system autodetect
  /// resolves from the collection name + type. No-op once the user has
  /// picked a system by hand, or for non-game media types.
  void syncScreenscraperSystemToName();
  /// Fill the Core combo with libretro cores discovered in the
  /// RetroArch install (per the override / autodetect).
  void populateCoreCombo();
  /// Show/hide the two conditional rows — ScreenScraper system (game media
  /// types) and libretro core (RetroArch launcher) — resizing the dialog
  /// only when a row actually flips.
  void updateConditionalRows();
  /// True when the media type resolves to the games category — the trigger
  /// for revealing the ScreenScraper system row.
  [[nodiscard]] bool isGamesType() const;

  QFormLayout *m_form = nullptr;
  QLabel *m_introLabel = nullptr;
  QLineEdit *m_nameEdit = nullptr;
  QLineEdit *m_contentEdit = nullptr;
  QLineEdit *m_artworkEdit = nullptr;
  QLineEdit *m_launcherEdit = nullptr;
  QLineEdit *m_coreEdit = nullptr;
  QHBoxLayout *m_coreRow = nullptr;
  QComboBox *m_typeCombo = nullptr;
  QComboBox *m_scraperCombo = nullptr;
  QComboBox *m_screenscraperSystemCombo = nullptr;
  /// Dropdown of libretro cores discovered in the RetroArch install;
  /// picking one fills m_coreEdit.
  QComboBox *m_coreCombo = nullptr;
  /// RetroArch override (retroarch.cfg / core dir); empty = autodetect.
  QString m_retroarchOverride;
  QPushButton *m_okButton = nullptr;
  /// Set once the user changes the scraper combo themselves — freezes the
  /// type→scraper auto-association so a deliberate pick survives a later
  /// edit to the media type.
  bool m_scraperManuallySet = false;
  /// Set once the user picks a ScreenScraper system by hand — freezes
  /// the name→system autodetect the same way.
  bool m_screenscraperSystemManuallySet = false;
  /// SS catalog kept for name-driven autodetect (the combo only stores
  /// id + display name, not the aliases autodetect scores against).
  QList<ScreenScraperSystems::System> m_screenscraperSystems;
};

#endif // CREATECOLLECTIONDIALOG_H
