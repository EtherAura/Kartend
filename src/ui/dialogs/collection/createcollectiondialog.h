#ifndef CREATECOLLECTIONDIALOG_H
#define CREATECOLLECTIONDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include "collection/systemicon_settings.h"
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

  /// Reveal an optional "Parent collection" picker seeded with `options`
  /// (each a {display name, uuid} pair) plus a leading "(none)" entry, and
  /// preselect `preselectUuid` when given. Off by default: the Settings
  /// add-collection flow derives the parent from the tree selection and
  /// never calls this, so its dialog is unchanged. Used by the DAT-library
  /// "Add to new collection…" path (Kartend-m6qsb.19).
  void setParentCollectionOptions(const QList<QPair<QString, QString>> &options,
                                  const QString &preselectUuid = QString());

  /// The chosen parent collection's uuid, or empty for "(none)" / when the
  /// picker was never enabled.
  [[nodiscard]] QString parentCollectionUuid() const;

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

  /// The navigation sidebar's RetroArch system glyph for the new collection
  /// (Kartend-1kkk2). `enabled` comes back true only when the media type is a
  /// game category AND a system was actually resolved — a collection created
  /// without RetroArch installed, or one whose name matched nothing, gets the
  /// default (off, no system) rather than an option that draws nothing.
  [[nodiscard]] SystemIconSettings systemIcon() const;

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
  /// Fill the sidebar-icon system combo from the pack that suits the current
  /// subject. Called on construction and whenever the subject changes, since
  /// packs cover different numbers of systems.
  void populateSystemIconCombo();
  /// Re-point the sidebar-icon system combo at the system RetroArchIcons
  /// resolves from the collection name. No-op once the user has picked one by
  /// hand, or for non-game media types. Seeded with the aliases the
  /// ScreenScraper catalog already holds for the detected system, which is
  /// what lets "SNES" reach "Nintendo - Super Nintendo Entertainment System".
  void syncSystemIconToName();
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
  /// Optional parent picker; hidden unless setParentCollectionOptions runs.
  /// item data carries the parent uuid ('' for the "(none)" entry).
  QComboBox *m_parentCombo = nullptr;
  bool m_parentPickerEnabled = false;
  QLineEdit *m_contentEdit = nullptr;
  QLineEdit *m_artworkEdit = nullptr;
  QLineEdit *m_launcherEdit = nullptr;
  QLineEdit *m_coreEdit = nullptr;
  QHBoxLayout *m_coreRow = nullptr;
  QComboBox *m_typeCombo = nullptr;
  QComboBox *m_scraperCombo = nullptr;
  QComboBox *m_screenscraperSystemCombo = nullptr;
  /// Sidebar glyph: what it depicts, and which system it is for.
  QComboBox *m_systemIconSubjectCombo = nullptr;
  QComboBox *m_systemIconCombo = nullptr;
  QHBoxLayout *m_systemIconRow = nullptr;
  /// RetroArch assets tree, resolved once from the same override the core
  /// picker uses. Empty when RetroArch is not installed — the row then offers
  /// nothing and stays disabled.
  QString m_assetsDirectory;
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
  /// Same freeze for the sidebar glyph's system pick.
  bool m_systemIconManuallySet = false;
  /// SS catalog kept for name-driven autodetect (the combo only stores
  /// id + display name, not the aliases autodetect scores against).
  QList<ScreenScraperSystems::System> m_screenscraperSystems;
};

#endif // CREATECOLLECTIONDIALOG_H
