// AppearanceLayoutPanel load()/save() round-trip (Kartend-t4u6m).
//
// The Item Dimensions and spacing spin boxes used to cap below what
// CollectionConfig::clampValues() keeps (400 px tiles, 50 px gaps), so
// opening Settings and saving without touching those fields wrote the capped
// values back. These tests seed the working collection at the loader's limits,
// load the panel (never shown), save, and require the exact values to survive.

#include "appearancelayoutpanel.h"

#include "collection/collectionconfig.h"
#include "settingsmodel.h"
#include "uiconstants/item.h"
#include "uiconstants/viewport.h"

#include <QList>
#include <QSpinBox>
#include <QTest>

class TestAppearanceLayoutPanel : public QObject {
  Q_OBJECT

private slots:
  void upperLimits_survive_loadSave();
  void lowerLimits_survive_loadSave();
  void issueValues_surviveCornerRadiusEdit();
  void hostNarrowingGapRange_beforeLoad_doesNotClip();
};

namespace {

// Loader ceiling for both gaps (CollectionConfig::clampValues()).
constexpr int kLoaderSpacingMax = 200;

struct Fixture {
  QList<CollectionConfig> cols;
  int idx = 0;
  SettingsModel model;
  AppearanceLayoutPanel panel;

  Fixture(int itemWidth, int itemHeight, int hSpacing, int vSpacing) {
    CollectionConfig c;
    c.name = QStringLiteral("Games");
    c.gridLayout.itemWidth = itemWidth;
    c.gridLayout.itemHeight = itemHeight;
    c.gridLayout.horizontalSpacing = hSpacing;
    c.gridLayout.verticalSpacing = vSpacing;
    cols.append(c);
    model.workingCollections = &cols;
    model.currentIndex = &idx;
    panel.setModel(&model);
  }

  QSpinBox *spin(const char *name) {
    return panel.findChild<QSpinBox *>(QString::fromLatin1(name));
  }

  const GridLayoutPreferences &saved() const { return cols[0].gridLayout; }
};

} // namespace

void TestAppearanceLayoutPanel::upperLimits_survive_loadSave() {
  Fixture f(UIConstants::Item::MAX_WIDTH, UIConstants::Item::MAX_HEIGHT, kLoaderSpacingMax,
            kLoaderSpacingMax);
  f.panel.load();
  f.panel.save();
  QCOMPARE(f.saved().itemWidth, UIConstants::Item::MAX_WIDTH);
  QCOMPARE(f.saved().itemHeight, UIConstants::Item::MAX_HEIGHT);
  QCOMPARE(f.saved().horizontalSpacing, kLoaderSpacingMax);
  QCOMPARE(f.saved().verticalSpacing, kLoaderSpacingMax);
}

void TestAppearanceLayoutPanel::lowerLimits_survive_loadSave() {
  Fixture f(UIConstants::Item::MIN_WIDTH, UIConstants::Item::MIN_HEIGHT,
            UIConstants::Viewport::SPACING_MIN, UIConstants::Viewport::SPACING_MIN);
  // Start the boxes away from the limits so a no-op load() cannot pass.
  f.spin("itemWidthSpinBox")->setValue(250);
  f.spin("itemHeightSpinBox")->setValue(250);
  f.spin("horizontalSpacingSpinBox")->setValue(20);
  f.spin("verticalSpacingSpinBox")->setValue(20);
  f.panel.load();
  f.panel.save();
  QCOMPARE(f.saved().itemWidth, UIConstants::Item::MIN_WIDTH);
  QCOMPARE(f.saved().itemHeight, UIConstants::Item::MIN_HEIGHT);
  QCOMPARE(f.saved().horizontalSpacing, UIConstants::Viewport::SPACING_MIN);
  QCOMPARE(f.saved().verticalSpacing, UIConstants::Viewport::SPACING_MIN);
}

void TestAppearanceLayoutPanel::issueValues_surviveCornerRadiusEdit() {
  // The reported collection: 530 x 656 tiles, only Corner Radius edited.
  Fixture f(530, 656, 120, 75);
  f.panel.load();
  QSpinBox *radius = f.spin("cornerRadiusSpinBox");
  QVERIFY(radius);
  radius->setValue(radius->value() + 5);
  f.panel.save();
  QCOMPARE(f.saved().itemWidth, 530);
  QCOMPARE(f.saved().itemHeight, 656);
  QCOMPARE(f.saved().horizontalSpacing, 120);
  QCOMPARE(f.saved().verticalSpacing, 75);
  QCOMPARE(f.saved().cornerRadius, radius->value());
}

void TestAppearanceLayoutPanel::hostNarrowingGapRange_beforeLoad_doesNotClip() {
  // SettingsDialog::setupUIConstraints() sets the gap boxes to
  // Viewport::SPACING_MAX after the panel is constructed and before load().
  Fixture f(UIConstants::Item::MAX_WIDTH, UIConstants::Item::MAX_HEIGHT, kLoaderSpacingMax,
            kLoaderSpacingMax);
  f.spin("horizontalSpacingSpinBox")->setMaximum(UIConstants::Viewport::SPACING_MAX);
  f.spin("verticalSpacingSpinBox")->setMaximum(UIConstants::Viewport::SPACING_MAX);
  f.panel.load();
  f.panel.save();
  QCOMPARE(f.saved().horizontalSpacing, kLoaderSpacingMax);
  QCOMPARE(f.saved().verticalSpacing, kLoaderSpacingMax);
}

QTEST_MAIN(TestAppearanceLayoutPanel)
#include "test_appearancelayoutpanel.moc"
