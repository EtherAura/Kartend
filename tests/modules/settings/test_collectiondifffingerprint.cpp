// Field-coverage test for the per-cluster qHash overloads that feed
// SettingsManager's hot-reload diff fingerprint (Kartend-lc58a). The
// save/diff baseline no longer keeps full CollectionConfig copies — it keeps a
// hash of each leaf cluster — so dirty detection now relies on each cluster's
// qHash catching ANY field change. A field present in operator== but omitted
// from qHash would make a real edit fingerprint clean and get silently dropped
// (the same failure mode the operator== coverage test guards, one layer down).
//
// This mutates each field operator== compares, in turn, and asserts the hash
// changes — so a future field added to a cluster struct but forgotten in its
// qHash fails here with the field name. Pairs with
// test_collectionconfigequality.cpp (the operator== half).
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTest>

#include "collection/archiveoptions.h"
#include "collection/collectionbackground.h"
#include "collection/collectionfilterpreferences.h"
#include "collection/folderbrowsingoptions.h"
#include "collection/gridlayoutpreferences.h"
#include "collection/launcherconfig.h"
#include "collection/listviewoptions.h"
#include "collection/scraperoverrides.h"
#include "collection/sidebarappearance.h"
#include "collectiontypes.h"

namespace {

// Mutates a default-constructed T via `mutate` and asserts the cluster hash
// differs from the default's — i.e. qHash is sensitive to that field. Also
// pins that two default-constructed values hash equal (equal inputs must always
// fingerprint equal, or the diff would fire spuriously on every save).
template <typename T, typename Mutator> void hashDetects(const char *label, Mutator &&mutate) {
  const T base;
  QVERIFY2(qHash(base) == qHash(T{}),
           "two default-constructed cluster values must produce the same fingerprint hash");
  T changed;
  mutate(changed);
  QVERIFY2(qHash(base) != qHash(changed),
           qPrintable(QStringLiteral("cluster fingerprint hash is insensitive to %1")
                          .arg(QLatin1String(label))));
}

} // namespace

class TestCollectionDiffFingerprint : public QObject {
  Q_OBJECT
private slots:
  void gridLayoutHashTracksEachField();
  void sidebarHashTracksEachField();
  void backgroundHashTracksEachField();
  void listViewHashTracksEachField();
  void archiveHashTracksEachField();
  void folderBrowsingHashTracksEachField();
  void filterHashTracksEachField();
  void scraperOverridesHashTracksEachField();
  void launcherProfileHashTracksEachField();
  void launcherConfigHashTracksEachField();
};

void TestCollectionDiffFingerprint::gridLayoutHashTracksEachField() {
  using T = GridLayoutPreferences;
  hashDetects<T>("gridWidth", [](T &c) { c.gridWidth = 99; });
  hashDetects<T>("horizontalGridHeight", [](T &c) { c.horizontalGridHeight = 99; });
  hashDetects<T>("gridWidthSidebarHidden", [](T &c) { c.gridWidthSidebarHidden = 99; });
  hashDetects<T>("horizontalGridHeightSidebarHidden",
                 [](T &c) { c.horizontalGridHeightSidebarHidden = 99; });
  hashDetects<T>("gridHeightSidebarHidden", [](T &c) { c.gridHeightSidebarHidden = 99; });
  hashDetects<T>("horizontalSpacing", [](T &c) { c.horizontalSpacing = 77; });
  hashDetects<T>("verticalSpacing", [](T &c) { c.verticalSpacing = 77; });
  hashDetects<T>("horizontalScrollbarMode",
                 [](T &c) { c.horizontalScrollbarMode = ScrollbarMode::Autohide; });
  hashDetects<T>("verticalScrollbarMode",
                 [](T &c) { c.verticalScrollbarMode = ScrollbarMode::Hide; });
  hashDetects<T>("itemWidth", [](T &c) { c.itemWidth = 321; });
  hashDetects<T>("itemHeight", [](T &c) { c.itemHeight = 321; });
  hashDetects<T>("fontSize", [](T &c) { c.fontSize = 33; });
  hashDetects<T>("cornerRadius", [](T &c) { c.cornerRadius = 33; });
}

void TestCollectionDiffFingerprint::sidebarHashTracksEachField() {
  using T = SidebarAppearance;
  hashDetects<T>("sidebarVisible", [](T &c) { c.sidebarVisible = true; });
  hashDetects<T>("sidebarMode", [](T &c) { c.sidebarMode = DetailsPaneMode::Expand; });
  hashDetects<T>("sidebarPosition", [](T &c) { c.sidebarPosition = DetailsPanePosition::Left; });
  hashDetects<T>("sidebarBackgroundType",
                 [](T &c) { c.sidebarBackgroundType = DetailsPaneBackgroundType::Image; });
  hashDetects<T>("sidebarBackgroundColor",
                 [](T &c) { c.sidebarBackgroundColor = QStringLiteral("#123456"); });
  hashDetects<T>("sidebarBackgroundImage",
                 [](T &c) { c.sidebarBackgroundImage = QStringLiteral("/tmp/bg.png"); });
  // sidebarPattern has a single enumerator (Crosshatch), so there is no other
  // value to mutate it to — it is hashed for completeness but can't be tested.
  hashDetects<T>("sidebarPatternIntensity", [](T &c) { c.sidebarPatternIntensity = 17; });
  hashDetects<T>("sidebarPatternColor",
                 [](T &c) { c.sidebarPatternColor = QStringLiteral("#abcdef"); });
  hashDetects<T>("sidebarTextColor", [](T &c) { c.sidebarTextColor = QStringLiteral("#fefefe"); });
  hashDetects<T>("sidebarAccentColor",
                 [](T &c) { c.sidebarAccentColor = QStringLiteral("#0a0a0a"); });
  hashDetects<T>("sidebarHeaderBgColor",
                 [](T &c) { c.sidebarHeaderBgColor = QStringLiteral("#111111"); });
  hashDetects<T>("sidebarSectionBgColor",
                 [](T &c) { c.sidebarSectionBgColor = QStringLiteral("#222222"); });
  hashDetects<T>("sidebarHeaderBgOpacity", [](T &c) { c.sidebarHeaderBgOpacity = 7; });
  hashDetects<T>("sidebarSectionBgOpacity", [](T &c) { c.sidebarSectionBgOpacity = 7; });
  hashDetects<T>("sidebarWidth", [](T &c) { c.sidebarWidth = 999; });
  hashDetects<T>("sidebarHeight", [](T &c) { c.sidebarHeight = 999; });
  hashDetects<T>("sidebarWidthLocked", [](T &c) { c.sidebarWidthLocked = !c.sidebarWidthLocked; });
  hashDetects<T>("sidebarActiveTab", [](T &c) { c.sidebarActiveTab = DetailsPaneTab::Collection; });
  hashDetects<T>("sidebarFontFamily", [](T &c) { c.sidebarFontFamily = QStringLiteral("Mono"); });
  hashDetects<T>("sidebarFontPointSize", [](T &c) { c.sidebarFontPointSize = 14; });
}

void TestCollectionDiffFingerprint::backgroundHashTracksEachField() {
  using T = CollectionBackground;
  hashDetects<T>("backgroundType", [](T &c) { c.backgroundType = BackgroundType::Image; });
  hashDetects<T>("backgroundColor", [](T &c) { c.backgroundColor = QStringLiteral("#1a1a2e"); });
  hashDetects<T>("backgroundImage", [](T &c) { c.backgroundImage = QStringLiteral("/tmp/x.png"); });
  hashDetects<T>("backgroundVideo", [](T &c) { c.backgroundVideo = QStringLiteral("/tmp/x.mp4"); });
  hashDetects<T>("primaryColor", [](T &c) { c.primaryColor = QStringLiteral("#333333"); });
  hashDetects<T>("tileColor", [](T &c) { c.tileColor = QStringLiteral("#444444"); });
  hashDetects<T>("selectionColor", [](T &c) { c.selectionColor = QStringLiteral("#555555"); });
  hashDetects<T>("headerLogoImage",
                 [](T &c) { c.headerLogoImage = QStringLiteral("/tmp/logo.png"); });
  hashDetects<T>("headerLogoPosition",
                 [](T &c) { c.headerLogoPosition = HeaderLogoPosition::TopLeft; });
  hashDetects<T>("vignetteEnabled", [](T &c) { c.vignetteEnabled = true; });
  hashDetects<T>("vignetteIntensity", [](T &c) { c.vignetteIntensity = 11; });
  hashDetects<T>("wallpaperParallax", [](T &c) { c.wallpaperParallax = true; });
  hashDetects<T>("parallaxStrength", [](T &c) { c.parallaxStrength = 11; });
  hashDetects<T>("toolbarBackdropBlur", [](T &c) { c.toolbarBackdropBlur = true; });
  hashDetects<T>("backdropBlurRadius", [](T &c) { c.backdropBlurRadius = 30; });
}

void TestCollectionDiffFingerprint::listViewHashTracksEachField() {
  using T = ListViewOptions;
  hashDetects<T>("listFontSize", [](T &c) { c.listFontSize = 33; });
  hashDetects<T>("listRowHeight", [](T &c) { c.listRowHeight = 99; });
  hashDetects<T>("listRowColor", [](T &c) { c.listRowColor = QStringLiteral("#101010"); });
  hashDetects<T>("listAltRowColor", [](T &c) { c.listAltRowColor = QStringLiteral("#202020"); });
}

void TestCollectionDiffFingerprint::archiveHashTracksEachField() {
  using T = ArchiveOptions;
  hashDetects<T>("extractArchives", [](T &c) { c.extractArchives = true; });
  hashDetects<T>("extractedExtension", [](T &c) { c.extractedExtension = QStringLiteral("iso"); });
}

void TestCollectionDiffFingerprint::folderBrowsingHashTracksEachField() {
  using T = FolderBrowsingOptions;
  hashDetects<T>("includeContentSubfolders", [](T &c) { c.includeContentSubfolders = true; });
  hashDetects<T>("includeArtworkSubfolders", [](T &c) { c.includeArtworkSubfolders = true; });
  hashDetects<T>("showAllSubfolderItems", [](T &c) { c.showAllSubfolderItems = true; });
  hashDetects<T>("hideSubfolderTitles", [](T &c) { c.hideSubfolderTitles = true; });
  hashDetects<T>("showHiddenFolders", [](T &c) { c.showHiddenFolders = true; });
  hashDetects<T>("currentSubfolder", [](T &c) { c.currentSubfolder = QStringLiteral("sub"); });
}

void TestCollectionDiffFingerprint::filterHashTracksEachField() {
  using T = CollectionFilterPreferences;
  hashDetects<T>("titleExclusionPatterns",
                 [](T &c) { c.titleExclusionPatterns = {QStringLiteral("\\s*\\(USA\\)")}; });
  hashDetects<T>("titleExclusionEnabled",
                 [](T &c) { c.titleExclusionEnabled = !c.titleExclusionEnabled; });
}

void TestCollectionDiffFingerprint::scraperOverridesHashTracksEachField() {
  using T = ScraperOverrides;
  hashDetects<T>("screenscraperSystemId", [](T &c) { c.screenscraperSystemId = 7; });
  hashDetects<T>("screenscraperHashArchive",
                 [](T &c) { c.screenscraperHashArchive = !c.screenscraperHashArchive; });
  hashDetects<T>("datFilePaths", [](T &c) { c.datFilePaths = {QStringLiteral("/tmp/a.dat")}; });
  hashDetects<T>("scraperProviderId",
                 [](T &c) { c.scraperProviderId = QStringLiteral("screenscraper"); });
}

void TestCollectionDiffFingerprint::launcherProfileHashTracksEachField() {
  using T = LauncherProfile;
  hashDetects<T>("launcherPath", [](T &c) { c.launcherPath = QStringLiteral("/usr/bin/x"); });
  hashDetects<T>("corePath", [](T &c) { c.corePath = QStringLiteral("/cores/x.so"); });
  hashDetects<T>("launchParameters", [](T &c) { c.launchParameters = QStringLiteral("--fs"); });
  hashDetects<T>("launcherName", [](T &c) { c.launcherName = QStringLiteral("Primary"); });
  hashDetects<T>("defaultLauncherIndex", [](T &c) {
    c.additionalLaunchers.append(LauncherConfig{});
    c.defaultLauncherIndex = 1;
  });
  // The list folds in via qHashRange — adding an entry must change the hash,
  // and so must mutating a field of an existing entry (proves LauncherConfig's
  // own qHash is what qHashRange is calling).
  hashDetects<T>("additionalLaunchers.size",
                 [](T &c) { c.additionalLaunchers.append(LauncherConfig{}); });
  hashDetects<T>("additionalLaunchers[0].launcherPath", [](T &c) {
    LauncherConfig lc;
    lc.launcherPath = QStringLiteral("/usr/bin/extra");
    c.additionalLaunchers.append(lc);
  });
}

void TestCollectionDiffFingerprint::launcherConfigHashTracksEachField() {
  using T = LauncherConfig;
  hashDetects<T>("name", [](T &c) { c.name = QStringLiteral("Name"); });
  hashDetects<T>("launcherPath", [](T &c) { c.launcherPath = QStringLiteral("/usr/bin/x"); });
  hashDetects<T>("corePath", [](T &c) { c.corePath = QStringLiteral("/cores/x.so"); });
  hashDetects<T>("launchParameters", [](T &c) { c.launchParameters = QStringLiteral("--fs"); });
  hashDetects<T>("presetId", [](T &c) { c.presetId = QStringLiteral("preset-1"); });
}

QTEST_APPLESS_MAIN(TestCollectionDiffFingerprint)
#include "test_collectiondifffingerprint.moc"
