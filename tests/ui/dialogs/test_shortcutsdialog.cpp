// Headless ShortcutsDialog rendering: the six help sections, the
// keybinding-text rendering sourced from the host IMainWindow's live
// GeneralSettings (custom key, unbound-gamepad fallback, artwork-cycle
// modifier label), the useHomeView-gated row, and the showEvent repopulate
// path (clear + rebuild, no duplicated sections). The host is a minimal
// QWidget + IMainWindow fake so the dialog's dynamic_cast<IMainWindow *>
// (parent()) resolves; the parentless case pins the defaults fallback.

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "imainwindow.h"
#include "shortcutsdialog.h"

#include <QCoreApplication>
#include <QGroupBox>
#include <QKeySequence>
#include <QLabel>
#include <QTest>

namespace {

/// Minimal IMainWindow host: only generalSettings() matters to the dialog;
/// everything else is inert.
class FakeMainWindow : public QWidget, public IMainWindow {
public:
  QList<CollectionConfig> collectionsList;
  GeneralSettings settings;

  [[nodiscard]] const QList<CollectionConfig> &collections() const override {
    return collectionsList;
  }
  void updateWindowTitleForCollection(int) override {}
  void openScraperDialog(int, const QString &) override {}
  void openEntityScraperDialog(int) override {}
  void openDatAuditForCollection(const CollectionConfig &) override {}
  [[nodiscard]] DatAuditStatus datAuditStatusForCollection(const QString &) override { return {}; }
  [[nodiscard]] GeneralSettings &generalSettings() override { return settings; }
  [[nodiscard]] const GeneralSettings &generalSettings() const override { return settings; }
  [[nodiscard]] ApplicationManager *applicationManager() const override { return nullptr; }
  void applyGlobalUiFontFromSettings() override {}
  void applyMarqueeSettings() override {}
  void applyToolbarCustomization() override {}
  void applyPixmapCacheBudget(int) override {}
};

bool hasLabelWithText(const QWidget &root, const QString &text) {
  const auto labels = root.findChildren<QLabel *>();
  for (const auto *l : labels) {
    if (l->text() == text) {
      return true;
    }
  }
  return false;
}

QStringList groupBoxTitles(const QWidget &root) {
  QStringList titles;
  const auto boxes = root.findChildren<QGroupBox *>();
  titles.reserve(boxes.size());
  for (const auto *b : boxes) {
    titles << b->title();
  }
  return titles;
}

} // namespace

class TestShortcutsDialog : public QObject {
  Q_OBJECT

private slots:
  void rendersAllSectionsWithoutHost();
  void rendersConfiguredBindingsFromHost();
  void gamepadRowsReflectBindingAndUnbound();
  void homeViewRowGatedOnStartupSetting();
  void artworkCycleModifierLabelFollowsSetting();
  void showEventRepopulatesWithoutDuplicating();
};

void TestShortcutsDialog::rendersAllSectionsWithoutHost() {
  // Parentless construction is the read-only defaults fallback: every help
  // section still renders (from GeneralSettings{}), nothing crashes.
  ShortcutsDialog dlg;
  const QStringList titles = groupBoxTitles(dlg);
  for (const char *expected : {"Navigation", "Search", "Gamepad", "Mouse", "Window", "View"}) {
    QVERIFY2(titles.contains(QString::fromLatin1(expected)),
             qPrintable(QStringLiteral("missing section: %1").arg(expected)));
  }
  QCOMPARE(titles.size(), 6);
}

void TestShortcutsDialog::rendersConfiguredBindingsFromHost() {
  FakeMainWindow host;
  host.settings.keybindings.keyNavUp = Qt::Key_W;
  ShortcutsDialog dlg(&host);

  // The nav-up row must render the HOST's binding, not the compiled default.
  const QString expected = QKeySequence(Qt::Key_W).toString(QKeySequence::NativeText);
  QVERIFY2(hasLabelWithText(dlg, expected),
           "configured key binding (W) must appear as a keys label");
}

void TestShortcutsDialog::gamepadRowsReflectBindingAndUnbound() {
  FakeMainWindow host;
  host.settings.gamepad.gamepadConfirmButton = QStringLiteral("a");
  host.settings.gamepad.gamepadBackButton = QStringLiteral("   "); // whitespace = unbound
  ShortcutsDialog dlg(&host);

  QVERIFY(hasLabelWithText(dlg, QStringLiteral("a")));
  QVERIFY2(hasLabelWithText(dlg, QStringLiteral("(Unbound)")),
           "a blank gamepad binding must render the (Unbound) placeholder");
}

void TestShortcutsDialog::homeViewRowGatedOnStartupSetting() {
  FakeMainWindow host;
  host.settings.startup.useHomeView = false;
  {
    ShortcutsDialog dlg(&host);
    QVERIFY2(!hasLabelWithText(dlg, QStringLiteral("Jump to Home view")),
             "home-view row must be absent while the home view is disabled");
  }
  host.settings.startup.useHomeView = true;
  host.settings.keybindings.keyHomeView = Qt::Key_H;
  {
    ShortcutsDialog dlg(&host);
    QVERIFY(hasLabelWithText(dlg, QStringLiteral("Jump to Home view")));
    QVERIFY(hasLabelWithText(dlg, QKeySequence(Qt::Key_H).toString(QKeySequence::NativeText)));
  }
}

void TestShortcutsDialog::artworkCycleModifierLabelFollowsSetting() {
  FakeMainWindow host;
  {
    // Default (Shift) renders the Shift-prefixed combo.
    ShortcutsDialog dlg(&host);
    QVERIFY(hasLabelWithText(dlg, QStringLiteral("Shift+Middle-click")));
  }
  host.settings.input.artworkCycleModifier = static_cast<int>(Qt::ControlModifier);
  {
    ShortcutsDialog dlg(&host);
    QVERIFY(hasLabelWithText(dlg, QStringLiteral("Ctrl+Middle-click")));
    QVERIFY(!hasLabelWithText(dlg, QStringLiteral("Shift+Middle-click")));
  }
}

void TestShortcutsDialog::showEventRepopulatesWithoutDuplicating() {
  // populateContent() runs once at construction and again on every show so a
  // binding changed while the app runs is reflected. The clear pass uses
  // deleteLater, so flush deferred deletes before counting.
  FakeMainWindow host;
  ShortcutsDialog dlg(&host);
  dlg.show();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCOMPARE(groupBoxTitles(dlg).size(), 6);

  // A settings change between shows lands in the rebuilt content.
  dlg.hide();
  host.settings.gamepad.gamepadConfirmButton = QStringLiteral("start");
  dlg.show();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCOMPARE(groupBoxTitles(dlg).size(), 6);
  QVERIFY(hasLabelWithText(dlg, QStringLiteral("start")));
}

QTEST_MAIN(TestShortcutsDialog)
#include "test_shortcutsdialog.moc"
