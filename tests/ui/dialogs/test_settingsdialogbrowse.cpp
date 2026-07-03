// SettingsDialog browse partial (settingsdialogbrowse.cpp): the recursive
// content-import pipeline, driven headlessly through the private
// onRecursiveImportContent slot (QMetaObject::invokeMethod) against real
// QTemporaryDir trees, with every modal QMessageBox answered by a
// zero-interval timer running inside the box's own exec loop (the
// CollectionRemover suite's pattern, extended to a button queue because the
// happy path stacks a confirmation question and a completion info box).
// Covers: the empty-field / missing-directory / no-subdirectories guard
// rails (warn or inform, mutate nothing), subcollection creation from the
// directory structure (template inheritance, parent linkage, per-subdir
// media paths, artwork-structure matching, collectionSaved emission), and
// the traversal-unsafe subdirectory-name skip.
//
// NOT covered here, deliberately: browseLauncher / browseCore /
// browseMediaDir open native QFileDialog::getOpenFileName /
// getExistingDirectory pickers — platform dialogs that block outside Qt's
// widget event dispatch, so a headless test can neither drive nor reliably
// dismiss them. Their one-line "set the line edit on accept" behaviour is
// exercised indirectly by the checks suite mutating the same line edits.

#include "collection/collectionconfig.h"
#include "settingsdialog.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

QList<CollectionConfig> singleCollection(const QString &artworkDir = QString()) {
  CollectionConfig c;
  c.name = QStringLiteral("Template");
  c.hideTitles = true; // distinctive template value the children must inherit
  c.artworkDirectory = artworkDir;
  return {c};
}

/// Answers a queue of modal QMessageBoxes in order, capturing each box's
/// text. The zero-interval timer only fires inside a nested exec loop, so
/// when fewer boxes appear than expected the leftover queue simply reports
/// how far the flow got. Non-QMessageBox modals (the ErrorDialog raised by
/// the host-less general-settings save inside handleSaveCollection) are
/// rejected without consuming the button queue and tallied separately.
class ModalAnswerQueue : public QObject {
public:
  explicit ModalAnswerQueue(QList<QMessageBox::StandardButton> buttons)
      : m_buttons(std::move(buttons)) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      QWidget *modal = QApplication::activeModalWidget();
      // A dynamic property marks answered boxes instead of comparing
      // pointers: consecutive boxes routinely reuse the same heap address,
      // so a pointer guard mistakes box #2 for box #1 and never answers it —
      // the exec loop then spins forever (this wedged a full ctest run).
      if (!modal || modal->property("modalQueueAnswered").toBool()) {
        return;
      }
      auto *box = qobject_cast<QMessageBox *>(modal);
      if (!box) {
        // Non-QMessageBox modal: the standalone dialog (constructed with no
        // settings host) surfaces the general-settings save failure inside
        // handleSaveCollection through a modal ErrorDialog. Left unanswered
        // its exec() loop never exits, and this zero-interval timer then
        // drives that loop at 100% CPU — the suite-wedging spin. Dismiss it
        // without consuming the QMessageBox button queue and record it so
        // slots can assert how many appeared.
        auto *dialog = qobject_cast<QDialog *>(modal);
        if (!dialog) {
          return;
        }
        dialog->setProperty("modalQueueAnswered", true);
        otherDialogTitles.append(dialog->windowTitle());
        dialog->reject();
        return;
      }
      box->setProperty("modalQueueAnswered", true);
      texts.append(box->text());
      // Fail loud, never hang: an unexpected box (queue empty) or a box
      // missing the requested button is dismissed via its escape/default
      // button and recorded — assertions on `texts`/counts catch the
      // mismatch instead of a wedged event loop.
      QAbstractButton *toClick = nullptr;
      if (!m_buttons.isEmpty()) {
        toClick = box->button(m_buttons.takeFirst());
      } else {
        ++unexpectedBoxes;
      }
      if (!toClick) toClick = box->escapeButton();
      if (!toClick) toClick = box->defaultButton();
      if (!toClick && !box->buttons().isEmpty()) toClick = box->buttons().first();
      if (toClick) toClick->click();
    });
    m_timer.start();
  }

  QStringList texts;
  QStringList otherDialogTitles;
  int unexpectedBoxes = 0;

private:
  QList<QMessageBox::StandardButton> m_buttons;
  QTimer m_timer;
};

bool invokeRecursiveImport(SettingsDialog &dialog) {
  return QMetaObject::invokeMethod(&dialog, "onRecursiveImportContent", Qt::DirectConnection);
}

} // namespace

class TestSettingsDialogBrowse : public QObject {
  Q_OBJECT

private slots:
  void recursiveImport_emptyDirField_warnsAndAddsNothing();
  void recursiveImport_missingDirectory_warnsAndAddsNothing();
  void recursiveImport_noSubdirectories_informsAndAddsNothing();
  void recursiveImport_createsSubcollectionsFromStructure();
  void recursiveImport_decliningConfirmationAddsNothing();
  void recursiveImport_skipsTraversalUnsafeNames();
};

void TestSettingsDialogBrowse::recursiveImport_emptyDirField_warnsAndAddsNothing() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("mediaDirLineEdit"));
  QVERIFY(edit);
  edit->clear();

  ModalAnswerQueue answers({QMessageBox::Ok});
  QVERIFY(invokeRecursiveImport(dialog));

  QCOMPARE(answers.texts.size(), 1);
  QVERIFY(answers.texts.first().contains(QStringLiteral("specify a content directory")));
  QCOMPARE(dialog.getCollections().size(), 1);
}

void TestSettingsDialogBrowse::recursiveImport_missingDirectory_warnsAndAddsNothing() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("mediaDirLineEdit"));
  QVERIFY(edit);
  edit->setText(root.filePath(QStringLiteral("does-not-exist")));

  ModalAnswerQueue answers({QMessageBox::Ok});
  QVERIFY(invokeRecursiveImport(dialog));

  QCOMPARE(answers.texts.size(), 1);
  QVERIFY(answers.texts.first().contains(QStringLiteral("does not exist")));
  QCOMPARE(dialog.getCollections().size(), 1);
}

void TestSettingsDialogBrowse::recursiveImport_noSubdirectories_informsAndAddsNothing() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("mediaDirLineEdit"));
  QVERIFY(edit);
  edit->setText(root.path());

  ModalAnswerQueue answers({QMessageBox::Ok});
  QVERIFY(invokeRecursiveImport(dialog));

  QCOMPARE(answers.texts.size(), 1);
  QVERIFY(answers.texts.first().contains(QStringLiteral("No subdirectories")));
  QCOMPARE(dialog.getCollections().size(), 1);
}

void TestSettingsDialogBrowse::recursiveImport_createsSubcollectionsFromStructure() {
  // Past confirmation, handleSaveCollection's saveGeneralSettingsFromUI fails
  // (the standalone test dialog has no settings host) and surfaces a modal
  // ErrorDialog. ModalAnswerQueue rejects it without consuming the button
  // queue and tallies it in otherDialogTitles — asserted below. A
  // QMessageBox-only queue would leave that dialog's exec loop spinning
  // forever (this slot's historical hang).
  QTemporaryDir contentRoot;
  QTemporaryDir artworkRoot;
  QVERIFY(contentRoot.isValid() && artworkRoot.isValid());
  QVERIFY(QDir(contentRoot.path()).mkdir(QStringLiteral("Alpha")));
  QVERIFY(QDir(contentRoot.path()).mkdir(QStringLiteral("Beta")));
  // Only Alpha has a matching artwork subdirectory.
  QVERIFY(QDir(artworkRoot.path()).mkdir(QStringLiteral("Alpha")));

  SettingsDialog dialog(nullptr, singleCollection(artworkRoot.path()), 0);
  QSignalSpy saved(&dialog, &SettingsDialog::collectionSaved);
  QVERIFY(saved.isValid());
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("mediaDirLineEdit"));
  QVERIFY(edit);
  edit->setText(contentRoot.path());

  // Confirmation question (Yes), then the completion info box (Ok).
  ModalAnswerQueue answers({QMessageBox::Yes, QMessageBox::Ok});
  QVERIFY(invokeRecursiveImport(dialog));

  QCOMPARE(answers.texts.size(), 2);
  QVERIFY(answers.texts.first().contains(QStringLiteral("2 subcollection(s)")));
  QVERIFY(answers.texts.last().contains(QStringLiteral("Successfully created")));
  // handleSaveCollection's general-settings save fails (no settings host in
  // the standalone dialog) and surfaces exactly one modal ErrorDialog.
  QCOMPARE(answers.otherDialogTitles.size(), 1);

  const QList<CollectionConfig> &cols = dialog.getCollections();
  QCOMPARE(cols.size(), 3);
  // entryList sorts by name, so Alpha lands first.
  QCOMPARE(cols[1].name, QStringLiteral("Alpha"));
  QCOMPARE(cols[2].name, QStringLiteral("Beta"));
  for (int idx : {1, 2}) {
    QCOMPARE(cols[idx].parentCollectionIndex, 0);
    QVERIFY(cols[idx].isSubcollection);
    QVERIFY(cols[idx].hideTitles); // inherited from the template collection
    QVERIFY(cols[idx].folderBrowsing.currentSubfolder.isEmpty());
    QCOMPARE(cols[idx].mediaDirectory, QDir(contentRoot.path()).absoluteFilePath(cols[idx].name));
  }
  // Artwork-structure matching: Alpha found its twin, Beta keeps the template's.
  QCOMPARE(cols[1].artworkDirectory,
           QDir(artworkRoot.path()).absoluteFilePath(QStringLiteral("Alpha")));
  QCOMPARE(cols[2].artworkDirectory, artworkRoot.path());
  // The import persists immediately (pre-save + final emit both count).
  QVERIFY(saved.count() >= 1);
}

void TestSettingsDialogBrowse::recursiveImport_decliningConfirmationAddsNothing() {
  QTemporaryDir contentRoot;
  QVERIFY(contentRoot.isValid());
  QVERIFY(QDir(contentRoot.path()).mkdir(QStringLiteral("Alpha")));

  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("mediaDirLineEdit"));
  QVERIFY(edit);
  edit->setText(contentRoot.path());

  ModalAnswerQueue answers({QMessageBox::No});
  QVERIFY(invokeRecursiveImport(dialog));

  QCOMPARE(answers.texts.size(), 1);
  QCOMPARE(dialog.getCollections().size(), 1);
}

void TestSettingsDialogBrowse::recursiveImport_skipsTraversalUnsafeNames() {
  // Same host-less-save ErrorDialog as
  // recursiveImport_createsSubcollectionsFromStructure — see the comment there.
  QTemporaryDir contentRoot;
  QVERIFY(contentRoot.isValid());
  QVERIFY(QDir(contentRoot.path()).mkdir(QStringLiteral("Safe")));
  // A backslash is a legal directory character on Linux but would inject a
  // separator through '%collection%' substitution — the import must skip it.
  if (!QDir(contentRoot.path()).mkdir(QStringLiteral("un\\safe"))) {
    QSKIP("Filesystem refuses backslash directory names; cannot stage the unsafe entry");
  }

  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("mediaDirLineEdit"));
  QVERIFY(edit);
  edit->setText(contentRoot.path());

  ModalAnswerQueue answers({QMessageBox::Yes, QMessageBox::Ok});
  QVERIFY(invokeRecursiveImport(dialog));

  QCOMPARE(answers.texts.size(), 2);
  // One ErrorDialog from the host-less general-settings save, as above.
  QCOMPARE(answers.otherDialogTitles.size(), 1);
  const QList<CollectionConfig> &cols = dialog.getCollections();
  QCOMPARE(cols.size(), 2); // template + Safe; the unsafe name was skipped
  QCOMPARE(cols[1].name, QStringLiteral("Safe"));
}

QTEST_MAIN(TestSettingsDialogBrowse)
#include "test_settingsdialogbrowse.moc"
