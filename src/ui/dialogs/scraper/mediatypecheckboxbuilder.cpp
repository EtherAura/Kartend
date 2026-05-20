#include "mediatypecheckboxbuilder.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

struct MediaTypeEntry {
  const char *key;
  const char *label;
  bool defaultOn;
};

// Curated source-of-truth list. The leading `_metadata` is a synthetic
// key that gates the textual ScrapedItem fields (title / description
// / etc.) rather than a MediaAsset::type — handled specially when
// building the BatchScrapeRunner filter and at applyResult time.
//
// Every other key must equal a MediaAsset::type in lowercase: the
// runner matches each asset's `type.toLower()` against the filter
// set, and that set is built by lowercasing these keys. Parser
// aliases are already collapsed (box-2D → front, sstitle → title,
// manuel → manual, ss/screenshot → screenshot); every other entry
// keeps its raw ScreenScraper tag, lowercased.
constexpr MediaTypeEntry kMediaTypes[] = {
    {"_metadata", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Metadata (title, description, …)"),
     true},
    {"front", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Front cover"), true},
    {"box-2d-back", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Back cover"), false},
    {"box-2d-side", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Box spine"), false},
    {"box-3d", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Box (3D)"), false},
    {"box-texture", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Box texture"), false},
    {"support-2d", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Cart / disc label"), false},
    {"support-3d", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Cart / disc (3D)"), false},
    {"support-texture", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Cart / disc texture"), false},
    {"screenshot", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Screenshot"), false},
    {"title", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Title screen"), false},
    {"fanart", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Fanart"), false},
    {"background", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Background"), false},
    {"steamgrid", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Steam grid"), false},
    {"wheel", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Wheel / logo"), false},
    {"wheel-carbon", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Wheel (carbon)"), false},
    {"wheel-steel", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Wheel (steel)"), false},
    {"marquee", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Marquee"), false},
    {"screenmarquee", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Screen marquee"), false},
    {"screenmarqueesmall", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Screen marquee (small)"),
     false},
    {"bezel-16-9", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Bezel (16:9)"), false},
    {"bezel-4-3", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Bezel (4:3)"), false},
    {"mixrbv1", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Mix (RBV1)"), false},
    {"mixrbv2", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Mix (RBV2)"), false},
    {"figurine", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Figurine"), false},
    {"pictoliste", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Pictogram (list)"), false},
    {"pictocouleur", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Pictogram (colour)"), false},
    {"pictomonochrome", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Pictogram (mono)"), false},
    {"map", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Map"), false},
    {"manual", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Manual"), false},
    {"video", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Video"), false},
    {"video-normalized", QT_TRANSLATE_NOOP("ScrapeResultDialog", "Video (normalized)"), false},
};

} // namespace

QGroupBox *MediaTypeCheckboxBuilder::build(QWidget *parent,
                                           QHash<QString, QCheckBox *> &outChecks) {
  auto *group = new QGroupBox(tr("What to scrape"), parent);
  auto *vbox = new QVBoxLayout(group);

  auto *buttonsRow = new QHBoxLayout;
  auto *selectAll = new QPushButton(tr("Select all"), group);
  auto *selectNone = new QPushButton(tr("Select none"), group);
  buttonsRow->addWidget(selectAll);
  buttonsRow->addWidget(selectNone);
  buttonsRow->addStretch(1);
  vbox->addLayout(buttonsRow);

  auto *grid = new QGridLayout;
  constexpr int kColumns = 3;
  int row = 0;
  int col = 0;
  for (const auto &entry : kMediaTypes) {
    auto *check = new QCheckBox(tr(entry.label), group);
    check->setChecked(entry.defaultOn);
    grid->addWidget(check, row, col);
    outChecks.insert(QString::fromLatin1(entry.key), check);
    if (++col >= kColumns) {
      col = 0;
      ++row;
    }
  }
  vbox->addLayout(grid);

  // Bulk-toggle every checkbox, including the synthetic _metadata
  // entry. Capture the QHash by reference so additions made by the
  // caller (none today, but defensive against future surgery) stay
  // visible to the bulk toggle.
  QObject::connect(selectAll, &QPushButton::clicked, group, [&outChecks]() {
    for (auto it = outChecks.constBegin(); it != outChecks.constEnd(); ++it) {
      it.value()->setChecked(true);
    }
  });
  QObject::connect(selectNone, &QPushButton::clicked, group, [&outChecks]() {
    for (auto it = outChecks.constBegin(); it != outChecks.constEnd(); ++it) {
      it.value()->setChecked(false);
    }
  });

  return group;
}
