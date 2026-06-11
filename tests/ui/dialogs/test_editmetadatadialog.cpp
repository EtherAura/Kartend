// Kartend-0eeuk: EditMetadataDialog payload mapping. The dialog edits an
// in-memory EditMetadataPayload; these tests cover the value semantics the
// caller depends on: setPayload/payload round-trips, the messy-comma tag
// parsing (split, trim, drop empties), source-URL trimming, the rating
// widget + clear-button + label wiring, and the custom-fields table's
// add/remove row behavior (including the descending-order multi-row
// removal). Driven headlessly — the dialog is never shown or exec()'d.

#include "editmetadatadialog.h"

#include <QApplication>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

namespace {

// The dialog has two QLineEdits; tell them apart by placeholder.
QLineEdit *editWithPlaceholderContaining(const QWidget &dlg, const QString &needle) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->placeholderText().contains(needle)) {
      return e;
    }
  }
  return nullptr;
}

QLineEdit *tagsEdit(const QWidget &dlg) {
  return editWithPlaceholderContaining(dlg, QStringLiteral("Comma-separated"));
}

QLineEdit *sourceUrlEdit(const QWidget &dlg) {
  return editWithPlaceholderContaining(dlg, QStringLiteral("URL"));
}

QPushButton *buttonWithText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

bool hasLabelWithText(const QWidget &dlg, const QString &text) {
  const auto labels = dlg.findChildren<QLabel *>();
  for (const QLabel *l : labels) {
    if (l->text() == text) {
      return true;
    }
  }
  return false;
}

EditMetadataPayload fullPayload() {
  EditMetadataPayload p;
  p.notes = QStringLiteral("first listen: great\nsecond: even better");
  p.tags = {QStringLiteral("live"), QStringLiteral("jazz")};
  p.rating = 7;
  p.sourceUrl = QStringLiteral("https://example.org/source");
  p.customFields = {{QStringLiteral("venue"), QStringLiteral("city hall")},
                    {QStringLiteral("lineage"), QString()}};
  return p;
}

} // namespace

class TestEditMetadataDialog : public QObject {
  Q_OBJECT

private slots:
  void defaultPayloadIsEmpty();
  void payloadRoundTripsThroughTheForm();
  void tagsAreSplitTrimmedAndEmptiesDropped();
  void sourceUrlIsTrimmed();
  void clearButtonResetsRatingToUnset();
  void ratingLabelRendersHalfAndWholeStars();
  void addRowAppendsEmptyCustomField();
  void removeSelectedRowsHandlesNonAdjacentSelection();
};

void TestEditMetadataDialog::defaultPayloadIsEmpty() {
  EditMetadataDialog dlg;
  const EditMetadataPayload p = dlg.payload();
  QVERIFY(p.notes.isEmpty());
  QVERIFY(p.tags.isEmpty());
  QCOMPARE(p.rating, -1);
  QVERIFY(p.sourceUrl.isEmpty());
  QVERIFY(p.customFields.isEmpty());
}

void TestEditMetadataDialog::payloadRoundTripsThroughTheForm() {
  EditMetadataDialog dlg;
  const EditMetadataPayload in = fullPayload();
  dlg.setPayload(in);

  const EditMetadataPayload out = dlg.payload();
  QCOMPARE(out.notes, in.notes);
  QCOMPARE(out.tags, in.tags);
  QCOMPARE(out.rating, in.rating);
  QCOMPARE(out.sourceUrl, in.sourceUrl);
  QCOMPARE(out.customFields, in.customFields); // empty values survive, order kept
}

void TestEditMetadataDialog::tagsAreSplitTrimmedAndEmptiesDropped() {
  EditMetadataDialog dlg;
  QLineEdit *tags = tagsEdit(dlg);
  QVERIFY(tags);
  tags->setText(QStringLiteral("  live ,, jazz ,archive,  ,"));
  QCOMPARE(dlg.payload().tags, (QStringList{QStringLiteral("live"), QStringLiteral("jazz"),
                                            QStringLiteral("archive")}));
}

void TestEditMetadataDialog::sourceUrlIsTrimmed() {
  EditMetadataDialog dlg;
  QLineEdit *url = sourceUrlEdit(dlg);
  QVERIFY(url);
  url->setText(QStringLiteral("  https://example.org/x  "));
  QCOMPARE(dlg.payload().sourceUrl, QStringLiteral("https://example.org/x"));
}

void TestEditMetadataDialog::clearButtonResetsRatingToUnset() {
  EditMetadataDialog dlg;
  EditMetadataPayload p;
  p.rating = 8;
  dlg.setPayload(p);
  QCOMPARE(dlg.payload().rating, 8);

  QPushButton *clear = buttonWithText(dlg, QStringLiteral("Clear"));
  QVERIFY(clear);
  clear->click();
  QCOMPARE(dlg.payload().rating, -1);
  QVERIFY(hasLabelWithText(dlg, QStringLiteral("Unrated"))); // label tracked the change
}

void TestEditMetadataDialog::ratingLabelRendersHalfAndWholeStars() {
  EditMetadataDialog dlg;
  EditMetadataPayload p;
  p.rating = 7; // 3.5 stars
  dlg.setPayload(p);
  QVERIFY(hasLabelWithText(dlg, QStringLiteral("3.5 / 5")));

  p.rating = 8; // 4 stars — whole numbers drop the decimal
  dlg.setPayload(p);
  QVERIFY(hasLabelWithText(dlg, QStringLiteral("4 / 5")));

  p.rating = -1;
  dlg.setPayload(p);
  QVERIFY(hasLabelWithText(dlg, QStringLiteral("Unrated")));
}

void TestEditMetadataDialog::addRowAppendsEmptyCustomField() {
  EditMetadataDialog dlg;
  dlg.setPayload(fullPayload());
  QPushButton *add = buttonWithText(dlg, QStringLiteral("Add row"));
  QVERIFY(add);
  add->click();

  const auto fields = dlg.payload().customFields;
  QCOMPARE(fields.size(), 3);
  QVERIFY(fields.last().first.isEmpty());
  QVERIFY(fields.last().second.isEmpty());
}

void TestEditMetadataDialog::removeSelectedRowsHandlesNonAdjacentSelection() {
  EditMetadataDialog dlg;
  EditMetadataPayload p;
  p.customFields = {{QStringLiteral("a"), QStringLiteral("1")},
                    {QStringLiteral("b"), QStringLiteral("2")},
                    {QStringLiteral("c"), QStringLiteral("3")}};
  dlg.setPayload(p);

  auto *table = dlg.findChild<QTableWidget *>();
  QVERIFY(table);
  // Select rows 0 and 2 — removal must process them high-to-low or the
  // second removal would shift and delete the wrong row.
  auto *model = table->model();
  table->selectionModel()->select(model->index(0, 0),
                                  QItemSelectionModel::Select | QItemSelectionModel::Rows);
  table->selectionModel()->select(model->index(2, 0),
                                  QItemSelectionModel::Select | QItemSelectionModel::Rows);

  QPushButton *remove = buttonWithText(dlg, QStringLiteral("Remove row"));
  QVERIFY(remove);
  remove->click();

  const auto fields = dlg.payload().customFields;
  QCOMPARE(fields.size(), 1);
  QCOMPARE(fields.first().first, QStringLiteral("b")); // the middle row survived
}

QTEST_MAIN(TestEditMetadataDialog)
#include "test_editmetadatadialog.moc"
