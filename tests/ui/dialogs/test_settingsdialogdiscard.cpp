// SettingsDialog unsaved-changes resolution for GENERAL settings: the
// Save/Discard/Cancel prompt fires on the whole-struct general-settings
// compare, but the Discard branch used to revert only the current collection
// row — accept() then unconditionally persisted the general-settings edits
// the user had just chosen to throw away. Discard must reset the dialog's
// working GeneralSettings to the dialog-open baseline (and refresh the
// panels) so OK persists the baseline; Save must persist the edits on both
// close paths, including when no collection is selected (where
// handleSaveCollection can't run). Driven through a real SettingsDialog with
// a minimal QWidget+IMainWindow host and a counting ISettingsManager
// injected via ApplicationContext; the prompts are answered by a
// zero-interval modal driver. The dialog itself is never shown.

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "imainwindow.h"
#include "settingsdialog.h"

#include "../../integration/mocks/mocksettingsmanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

namespace {

/// Minimal IMainWindow host: generalSettings() is the live struct the
/// dialog mirrors into on save; everything else is inert.
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

/// Counts saveGeneralSettings disk writes and captures the last payload.
class CountingSettingsManager : public KartendTest::MockSettingsManager {
public:
  int saveCount = 0;
  GeneralSettings lastSaved;
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override {
    ++saveCount;
    lastSaved = settings;
    return ErrorUtils::Result<void>::success();
  }
};

/// Answers the next active modal (the unsaved-changes QMessageBox) with the
/// given standard button. Zero-interval repeating timer, same shape as
/// test_settingsdialoglivesave.cpp.
class ModalDriver : public QObject {
public:
  explicit ModalDriver(QMessageBox::StandardButton button) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this, button] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!box) {
        return;
      }
      triggered = true;
      m_timer.stop();
      box->button(button)->click();
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  QTimer m_timer;
};

QList<CollectionConfig> oneCollection() {
  CollectionConfig c;
  c.name = QStringLiteral("First");
  return {c};
}

struct Harness {
  FakeMainWindow host;
  CountingSettingsManager sm;
  ApplicationContext ctx;
  SettingsDialog *dialog = nullptr; // parented to host

  Harness() {
    ctx.managers.settingsManager = &sm;
    dialog = new SettingsDialog(&host, oneCollection(), 0, &ctx);
  }

  /// The attract-mode toggle is a DEFERRED general-settings field: editing it
  /// mutates the dialog's working GeneralSettings immediately but persists
  /// nothing until Save/OK — exactly the state the Discard branch must revert.
  QCheckBox *attractToggle() const {
    return dialog->findChild<QCheckBox *>(QStringLiteral("attractModeCheckBox"));
  }

  /// The collections tree (the dialog's only QTreeWidget).
  QTreeWidget *collectionsTree() const { return dialog->findChild<QTreeWidget *>(); }
};

} // namespace

class TestSettingsDialogDiscard : public QObject {
  Q_OBJECT

private slots:
  void acceptDiscardPersistsBaselineNotTheDiscardedEdits();
  void acceptSavePersistsDeferredGeneralEdits();
  void rejectDiscardRevertsWithoutAnyDiskWrite();
  void rejectSaveWithNoCollectionSelectedPersistsGeneralEdits();
  void formerStubFieldsNowFlipUnsavedChanges();
  void renamingStartupTargetRemapsStoredNameAndCombo();
};

void TestSettingsDialogDiscard::acceptDiscardPersistsBaselineNotTheDiscardedEdits() {
  Harness h;
  auto *toggle = h.attractToggle();
  QVERIFY(toggle);
  const bool original = toggle->isChecked();

  toggle->setChecked(!original);
  // Deferred field: the edit reaches the dialog's working struct only — no
  // disk write, no host mirror.
  QCOMPARE(h.sm.saveCount, 0);
  QCOMPARE(h.host.settings.attract.attractModeEnabled, original);

  // OK with unsaved changes → prompt → Discard. accept() still persists
  // general settings, but it must now persist the BASELINE, not the edit.
  ModalDriver driver(QMessageBox::Discard);
  h.dialog->accept();
  QVERIFY(driver.triggered);
  QCOMPARE(h.dialog->result(), static_cast<int>(QDialog::Accepted));

  QVERIFY(h.sm.saveCount >= 1);
  QCOMPARE(h.sm.lastSaved.attract.attractModeEnabled, original);
  QCOMPARE(h.host.settings.attract.attractModeEnabled, original);
  // The panel widget was refreshed from the restored baseline too.
  QCOMPARE(toggle->isChecked(), original);
}

void TestSettingsDialogDiscard::acceptSavePersistsDeferredGeneralEdits() {
  Harness h;
  auto *toggle = h.attractToggle();
  QVERIFY(toggle);
  const bool original = toggle->isChecked();

  toggle->setChecked(!original);

  ModalDriver driver(QMessageBox::Save);
  h.dialog->accept();
  QVERIFY(driver.triggered);
  QCOMPARE(h.dialog->result(), static_cast<int>(QDialog::Accepted));

  QVERIFY(h.sm.saveCount >= 1);
  QCOMPARE(h.sm.lastSaved.attract.attractModeEnabled, !original);
  QCOMPARE(h.host.settings.attract.attractModeEnabled, !original);
}

void TestSettingsDialogDiscard::rejectDiscardRevertsWithoutAnyDiskWrite() {
  Harness h;
  auto *toggle = h.attractToggle();
  QVERIFY(toggle);
  const bool original = toggle->isChecked();

  toggle->setChecked(!original);

  // Cancel → prompt → Discard. Nothing was live-applied, so the close must
  // not touch disk at all — and the working struct + widget revert.
  ModalDriver driver(QMessageBox::Discard);
  h.dialog->reject();
  QVERIFY(driver.triggered);
  QCOMPARE(h.dialog->result(), static_cast<int>(QDialog::Rejected));

  QCOMPARE(h.sm.saveCount, 0);
  QCOMPARE(h.host.settings.attract.attractModeEnabled, original);
  QCOMPARE(toggle->isChecked(), original);
}

void TestSettingsDialogDiscard::rejectSaveWithNoCollectionSelectedPersistsGeneralEdits() {
  Harness h;
  // Deselect while clean, THEN edit: the prompt is now raised by
  // general-settings edits alone, with no collection for
  // handleSaveCollection to route the save through. (The deselect gate
  // passes silently because nothing is dirty yet.)
  auto *tree = h.collectionsTree();
  QVERIFY(tree);
  tree->clearSelection();
  tree->setCurrentItem(nullptr);

  auto *toggle = h.attractToggle();
  QVERIFY(toggle);
  const bool original = toggle->isChecked();
  toggle->setChecked(!original);
  QCOMPARE(h.sm.saveCount, 0);

  ModalDriver driver(QMessageBox::Save);
  h.dialog->reject();
  QVERIFY(driver.triggered);
  QCOMPARE(h.dialog->result(), static_cast<int>(QDialog::Rejected));

  // Choosing Save must persist the general edits even though the dialog was
  // dismissed via Cancel/Esc and no collection was selected.
  QCOMPARE(h.sm.saveCount, 1);
  QCOMPARE(h.sm.lastSaved.attract.attractModeEnabled, !original);
  QCOMPARE(h.host.settings.attract.attractModeEnabled, !original);
}

void TestSettingsDialogDiscard::formerStubFieldsNowFlipUnsavedChanges() {
  // The per-collection dirty check is a whole-struct compare of the row Save
  // would write against the original snapshot — fields that used to sit
  // behind the permanently-false checkExtensionChanges /
  // checkBackgroundChanges stubs must register (and de-register on revert)
  // like any other field.
  Harness h;
  QVERIFY(!h.dialog->hasUnsavedChanges());

  // Extensions (former checkExtensionChanges stub territory).
  auto *extensions = h.dialog->findChild<QLineEdit *>(QStringLiteral("fileExtensionsLineEdit"));
  QVERIFY(extensions);
  extensions->setText(QStringLiteral("mkv, webm"));
  QVERIFY2(h.dialog->hasUnsavedChanges(),
           "An extensions edit must enable unsaved-changes detection");
  extensions->clear();
  QVERIFY2(!h.dialog->hasUnsavedChanges(),
           "Reverting the extensions edit must clear the dirty state");

  // Background vignette (former checkBackgroundChanges stub territory).
  auto *vignette = h.dialog->findChild<QCheckBox *>(QStringLiteral("vignetteEnabledCheckBox"));
  QVERIFY(vignette);
  const bool originalVignette = vignette->isChecked();
  vignette->setChecked(!originalVignette);
  QVERIFY2(h.dialog->hasUnsavedChanges(),
           "A background-field edit must enable unsaved-changes detection");
  vignette->setChecked(originalVignette);
  QVERIFY(!h.dialog->hasUnsavedChanges());
}

void TestSettingsDialogDiscard::renamingStartupTargetRemapsStoredNameAndCombo() {
  Harness h;
  // The combo is populated from the dialog's WORKING collection list, so the
  // session's only collection is pickable even though the host's live list
  // is empty in this harness.
  auto *combo = h.dialog->findChild<QComboBox *>(QStringLiteral("startupCollectionComboBox"));
  QVERIFY(combo);
  const int firstIdx = combo->findData(QStringLiteral("First"));
  QVERIFY(firstIdx >= 0);
  combo->setCurrentIndex(firstIdx); // pick "First" as the startup target

  // Rename the collection via its tree item, then commit with the persistent
  // Save button (the same path the toolbar save icon uses).
  auto *tree = h.collectionsTree();
  QVERIFY(tree);
  auto *item = tree->topLevelItem(0);
  QVERIFY(item);
  item->setText(0, QStringLiteral("Renamed"));
  auto *saveButton = h.dialog->findChild<QPushButton *>(QStringLiteral("saveButton"));
  QVERIFY(saveButton);
  saveButton->click();

  // The stored startup target followed the rename and persisted with it —
  // previously the stale name silently degraded to the "(Default)" sentinel
  // on the next combo write-back.
  QVERIFY(h.sm.saveCount >= 1);
  QCOMPARE(h.sm.lastSaved.startup.startupCollection, QStringLiteral("Renamed"));
  // The combo was repopulated from the working list with the selection
  // preserved under the new name.
  QCOMPARE(combo->currentData().toString(), QStringLiteral("Renamed"));
  QVERIFY(combo->findData(QStringLiteral("First")) < 0);
}

QTEST_MAIN(TestSettingsDialogDiscard)
#include "test_settingsdialogdiscard.moc"
