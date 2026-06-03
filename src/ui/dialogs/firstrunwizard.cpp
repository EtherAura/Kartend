// Three-page welcome wizard. Each page is a small QWizardPage built inline
// here rather than extracted into a class hierarchy — the wizard is invoked
// from exactly one place (Help menu / first-launch trigger) and only carries
// two pieces of state (collection name, media directory) so the indirection
// would cost more than it saves.
#include "firstrunwizard.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWizardPage>

#include "uiconstants/color.h"
#include "uiconstants/grid.h"

namespace {

constexpr int WIZARD_WIDTH = 640;
constexpr int WIZARD_HEIGHT = 460;

// Field IDs registered on the media-folder page so the validator and the
// finish handler can read them without poking widgets directly.
constexpr const char *FIELD_NAME = "collectionName";
constexpr const char *FIELD_MEDIA_DIR = "mediaDirectory";

} // namespace

FirstRunWizard::FirstRunWizard(QWidget *parent) : QWizard(parent) {
  setWindowTitle(tr("Welcome to Kartend"));
  setWizardStyle(QWizard::ModernStyle);
  // Modern style suppresses the Cancel button by default; bring it back so
  // the user has an explicit Skip path on every page.
  setOption(QWizard::NoCancelButton, false);
  setOption(QWizard::IndependentPages, true);
  resize(WIZARD_WIDTH, WIZARD_HEIGHT);

  buildWelcomePage();
  buildMediaFolderPage();
  buildDonePage();

  connect(this, &QDialog::accepted, this, [this]() {
    m_result.accepted = true;
    m_result.pickedConfig.name = field(FIELD_NAME).toString().trimmed();
    m_result.pickedConfig.mediaDirectory = field(FIELD_MEDIA_DIR).toString().trimmed();
    m_result.pickedConfig.parentCollectionIndex = -1;
    m_result.pickedConfig.isSubcollection = false;
    m_result.pickedConfig.gridLayout.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
  });
}

void FirstRunWizard::buildWelcomePage() {
  auto *page = new QWizardPage(this);
  page->setTitle(tr("Welcome"));
  page->setSubTitle(tr("Let's get your library set up."));

  auto *layout = new QVBoxLayout(page);
  auto *body = new QLabel(page);
  body->setWordWrap(true);
  body->setText(tr("Kartend organizes media collections — videos, audio, reference "
                   "documents, or anything else you keep in folders — and gives them a "
                   "browseable grid with artwork, metadata, and quick launch actions."
                   "\n\n"
                   "The next step asks for one folder to scan as your first collection. "
                   "You can add more, configure launchers, and tweak appearance later "
                   "from the Settings dialog."
                   "\n\n"
                   "Click Next to continue, or Cancel to skip — you can re-run this "
                   "wizard any time from Help → Setup Wizard…"));
  layout->addWidget(body);
  layout->addStretch();

  addPage(page);
}

namespace {

// Subclassed so isComplete() can gate Next on both fields being filled and
// the picked directory actually existing — QWizard's mandatory-field
// plumbing only checks non-empty, not "is a real directory." Page-local
// rather than at file scope because nothing else in the codebase needs it.
class MediaFolderPage : public QWizardPage {
public:
  explicit MediaFolderPage(QWidget *parent) : QWizardPage(parent) {
    setTitle(tr("Pick a media folder"));
    setSubTitle(tr("Kartend will scan this folder as your first collection."));

    auto *grid = new QGridLayout(this);
    grid->setColumnStretch(1, 1);

    auto *nameLabel = new QLabel(tr("Collection name:"), this);
    auto *nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText(tr("e.g. Videos, Audiobooks, Reference"));
    grid->addWidget(nameLabel, 0, 0);
    grid->addWidget(nameEdit, 0, 1, 1, 2);

    auto *dirLabel = new QLabel(tr("Media folder:"), this);
    auto *dirEdit = new QLineEdit(this);
    dirEdit->setPlaceholderText(tr("Path to a folder containing your media"));
    auto *browseButton = new QPushButton(tr("Browse…"), this);
    grid->addWidget(dirLabel, 1, 0);
    grid->addWidget(dirEdit, 1, 1);
    grid->addWidget(browseButton, 1, 2);

    auto *hint = new QLabel(this);
    hint->setWordWrap(true);
    hint->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
    hint->setText(tr("The folder is scanned recursively. Subfolders become subcollections "
                     "you can browse into. Nothing is moved or modified — Kartend only reads "
                     "from the folder."));
    grid->addWidget(hint, 2, 0, 1, 3);
    grid->setRowStretch(3, 1);

    // Trailing '*' marks the field mandatory so the Next button stays
    // disabled until non-empty; isComplete() below adds the dir-exists check.
    registerField(QString::fromUtf8(FIELD_NAME) + "*", nameEdit);
    registerField(QString::fromUtf8(FIELD_MEDIA_DIR) + "*", dirEdit);

    // Re-evaluate isComplete() on every keystroke so Next enables/disables
    // in real time as the user types or browses.
    connect(nameEdit, &QLineEdit::textChanged, this, [this]() { emit completeChanged(); });
    connect(dirEdit, &QLineEdit::textChanged, this, [this]() { emit completeChanged(); });

    connect(browseButton, &QPushButton::clicked, this, [this, dirEdit]() {
      const QString picked =
          QFileDialog::getExistingDirectory(this, tr("Select Media Folder"), dirEdit->text());
      if (!picked.isEmpty()) {
        dirEdit->setText(picked);
      }
    });
  }

  [[nodiscard]] bool isComplete() const override {
    const auto name = field(FIELD_NAME).toString().trimmed();
    const auto dir = field(FIELD_MEDIA_DIR).toString().trimmed();
    if (name.isEmpty() || dir.isEmpty()) {
      return false;
    }
    QFileInfo info(dir);
    return info.exists() && info.isDir();
  }
};

} // namespace

void FirstRunWizard::buildMediaFolderPage() {
  addPage(new MediaFolderPage(this));
}

void FirstRunWizard::buildDonePage() {
  auto *page = new QWizardPage(this);
  page->setTitle(tr("All set"));
  page->setSubTitle(tr("Your collection is ready to scan."));

  auto *layout = new QVBoxLayout(page);
  auto *body = new QLabel(page);
  body->setWordWrap(true);
  body->setText(tr("Kartend will create your collection when you click Finish, then "
                   "scan the folder in the background."
                   "\n\n"
                   "<b>Next steps you can take from the Settings dialog:</b>"
                   "<ul>"
                   "<li><b>Launchers</b> — Configure how items open. Kartend launches "
                   "files with your system default by default; the Launchers tab lets "
                   "you wire up custom commands per file type.</li>"
                   "<li><b>Artwork</b> — Point the collection at a folder of cover "
                   "images, or let Kartend draw procedural placeholders (the default).</li>"
                   "<li><b>Appearance</b> — Tweak grid size, fonts, and colors.</li>"
                   "</ul>"
                   "Re-run this wizard any time from Help → Setup Wizard…"));
  body->setTextFormat(Qt::RichText);
  layout->addWidget(body);
  layout->addStretch();

  addPage(page);
}
