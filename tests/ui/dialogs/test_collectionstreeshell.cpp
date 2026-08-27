// CollectionsTreeShell — the container at the top of the Collections tab
// (add/remove/duplicate buttons, the settings-scope "Mode:" combo, the tree).
// Driven headlessly (offscreen QPA, never exec()'d). The shell is a passive
// container with a default constructor, so it needs no SettingsDialog to
// instantiate.

#include <QComboBox>
#include <QFontMetrics>
#include <QLayout>
#include <QTest>

#include "collectionstreeshell.h"

class TestCollectionsTreeShell : public QObject {
  Q_OBJECT
private slots:
  void modeCombo_isWideEnoughForItsLongestEntry();
};

// Kartend-kzxcs: the "Mode:" combo truncated mid-word to "Current colle" at the
// dialog's default width — and, per the reporter's follow-up, ALSO when the
// dialog was maximized at 3840x2160 with free space to its right. That rules
// out the dialog squeezing it: the combo was reporting a width that did not
// cover its own longest entry, and the horizontal spacer to its left absorbed
// everything going spare rather than letting it grow.
//
// The requirement is the one the issue states: wide enough for its longest
// entry. Asserted against the WIDEST item rather than the current one, because
// the failure was exactly that the hint tracked the current selection
// ("Current collection") while the list also holds the longer "Current +
// subcollections".
void TestCollectionsTreeShell::modeCombo_isWideEnoughForItsLongestEntry() {
  CollectionsTreeShell shell;
  QComboBox *combo = shell.settingsScopeComboBox();
  QVERIFY(combo);
  QCOMPARE(combo->count(), 3);

  const QFontMetrics metrics(combo->font());
  int widestText = 0;
  QString widestEntry;
  for (int i = 0; i < combo->count(); ++i) {
    const int advance = metrics.horizontalAdvance(combo->itemText(i));
    if (advance > widestText) {
      widestText = advance;
      widestEntry = combo->itemText(i);
    }
  }
  QVERIFY(widestText > 0);

  // The combo's own sizeHint was never the problem — it covers the longest
  // entry comfortably. What truncated it is the width it actually GETS.
  QVERIFY2(combo->sizeHint().width() >= widestText,
           "premise: the combo's own hint covers its longest entry");

  // Reproduce the real constraint. CollectionsTreeShell lives in the settings
  // dialog's navigation rail, which is deliberately held narrow:
  // settingsdialognav.cpp does setStretchFactor(0, 0) + setSizes({260, 880})
  // with the comment "Rail stays compact; every extra pixel of width goes to
  // the content pane", and navPane caps at maximumWidth 440. THAT is why the
  // reporter saw the truncation persist with the dialog maximized at
  // 3840x2160 and "acres of free space" to the right — the free space is
  // outside the rail, which never grows into it.
  //
  // 260 is the seeded rail width verbatim.
  shell.resize(260, 240);
  shell.show();
  QVERIFY(QTest::qWaitForWindowExposed(&shell));
  shell.layout()->activate();

  qDebug() << "widest entry:" << widestEntry << "text width:" << widestText
           << "combo sizeHint:" << combo->sizeHint().width()
           << "combo ACTUAL width in a 260px rail:" << combo->width();

  QVERIFY2(combo->width() >= widestText,
           qPrintable(QStringLiteral("in the rail's real 260px width the combo gets only %1px, "
                                     "but its longest entry '%2' needs %3px of text alone — it "
                                     "renders truncated")
                          .arg(combo->width())
                          .arg(widestEntry)
                          .arg(widestText)));
}

QTEST_MAIN(TestCollectionsTreeShell)
#include "test_collectionstreeshell.moc"
