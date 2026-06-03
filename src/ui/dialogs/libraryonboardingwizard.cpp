#include "libraryonboardingwizard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWizardPage>

#include "launcherprobe.h"
#include "uiconstants/color.h"

namespace {

constexpr const char *kFieldName = "collectionName";
constexpr const char *kFieldMediaDir = "mediaDirectory";
constexpr const char *kFieldType = "collectionType";
constexpr const char *kFieldArtworkDir = "artworkDirectory";

/// First page — explanation + Get Started.
class WelcomePage : public QWizardPage {
public:
  explicit WelcomePage(QWidget *parent = nullptr) : QWizardPage(parent) {
    setTitle(LibraryOnboardingWizard::tr("Welcome"));
    auto *layout = new QVBoxLayout(this);
    auto *body =
        new QLabel(LibraryOnboardingWizard::tr(
                       "<p>This wizard creates a new collection in five steps: name + folder, "
                       "media type, artwork, launcher, and a final summary.</p>"
                       "<p>You can re-run it any time from File → New Library Wizard.</p>"),
                   this);
    body->setWordWrap(true);
    layout->addWidget(body);
    layout->addStretch();
  }
};

/// Page 2: name + media-dir browse. Validates non-empty name and an
/// existing directory before isComplete() reports true.
class MediaPage : public QWizardPage {
public:
  explicit MediaPage(QWidget *parent = nullptr) : QWizardPage(parent) {
    setTitle(LibraryOnboardingWizard::tr("Collection details"));
    setSubTitle(
        LibraryOnboardingWizard::tr("Pick a name and the folder containing your media files."));
    auto *form = new QFormLayout(this);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(LibraryOnboardingWizard::tr("e.g. Concerts"));
    form->addRow(LibraryOnboardingWizard::tr("Name:"), m_nameEdit);

    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    m_dirEdit->setPlaceholderText(LibraryOnboardingWizard::tr("/home/you/Media/Concerts"));
    auto *browse = new QPushButton(LibraryOnboardingWizard::tr("Browse…"), this);
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(browse);
    auto *dirContainer = new QWidget(this);
    dirContainer->setLayout(dirRow);
    form->addRow(LibraryOnboardingWizard::tr("Media folder:"), dirContainer);

    m_validation = new QLabel(this);
    m_validation->setStyleSheet(UIConstants::Color::errorLabelStyleSheet());
    m_validation->setWordWrap(true);
    form->addRow(QString(), m_validation);

    registerField(QString::fromLatin1(kFieldName) + "*", m_nameEdit);
    registerField(QString::fromLatin1(kFieldMediaDir) + "*", m_dirEdit);

    connect(browse, &QPushButton::clicked, this, [this]() {
      const QString picked = QFileDialog::getExistingDirectory(
          this, LibraryOnboardingWizard::tr("Pick media folder"), m_dirEdit->text());
      if (!picked.isEmpty()) {
        m_dirEdit->setText(picked);
      }
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, &QWizardPage::completeChanged);
    connect(m_dirEdit, &QLineEdit::textChanged, this, &QWizardPage::completeChanged);
  }

  [[nodiscard]] bool isComplete() const override {
    const QString name = m_nameEdit->text().trimmed();
    const QString dir = m_dirEdit->text().trimmed();
    if (name.isEmpty()) {
      m_validation->setText(LibraryOnboardingWizard::tr("Pick a name."));
      return false;
    }
    if (dir.isEmpty()) {
      m_validation->setText(LibraryOnboardingWizard::tr("Pick a media folder."));
      return false;
    }
    QFileInfo info(dir);
    if (!info.exists() || !info.isDir()) {
      m_validation->setText(
          LibraryOnboardingWizard::tr("That folder doesn't exist — pick another."));
      return false;
    }
    m_validation->clear();
    return true;
  }

private:
  QLineEdit *m_nameEdit = nullptr;
  QLineEdit *m_dirEdit = nullptr;
  mutable QLabel *m_validation = nullptr;
};

/// Page 3: media type + artwork dir.
class TypePage : public QWizardPage {
public:
  explicit TypePage(QWidget *parent = nullptr) : QWizardPage(parent) {
    setTitle(LibraryOnboardingWizard::tr("Media type and artwork"));
    setSubTitle(LibraryOnboardingWizard::tr(
        "These hint the rest of Kartend — type pre-filters the launcher pickers and "
        "artwork is the folder Kartend scans for cover images."));
    auto *form = new QFormLayout(this);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->setEditable(true);
    m_typeCombo->addItems(
        {LibraryOnboardingWizard::tr("Video"), LibraryOnboardingWizard::tr("Audio"),
         LibraryOnboardingWizard::tr("Reference"), LibraryOnboardingWizard::tr("Image"),
         LibraryOnboardingWizard::tr("Other")});
    form->addRow(LibraryOnboardingWizard::tr("Type:"), m_typeCombo);

    auto *artworkRow = new QHBoxLayout();
    m_artworkEdit = new QLineEdit(this);
    m_artworkEdit->setPlaceholderText(LibraryOnboardingWizard::tr("Optional"));
    auto *browse = new QPushButton(LibraryOnboardingWizard::tr("Browse…"), this);
    artworkRow->addWidget(m_artworkEdit);
    artworkRow->addWidget(browse);
    auto *artworkContainer = new QWidget(this);
    artworkContainer->setLayout(artworkRow);
    form->addRow(LibraryOnboardingWizard::tr("Artwork folder:"), artworkContainer);

    registerField(QString::fromLatin1(kFieldType), m_typeCombo, "currentText");
    registerField(QString::fromLatin1(kFieldArtworkDir), m_artworkEdit);

    connect(browse, &QPushButton::clicked, this, [this]() {
      const QString picked = QFileDialog::getExistingDirectory(
          this, LibraryOnboardingWizard::tr("Pick artwork folder"), m_artworkEdit->text());
      if (!picked.isEmpty()) {
        m_artworkEdit->setText(picked);
      }
    });
  }

private:
  QComboBox *m_typeCombo = nullptr;
  QLineEdit *m_artworkEdit = nullptr;
};

/// Page 4: launcher picker — multi-select from LauncherProbe results.
class LauncherPage : public QWizardPage {
public:
  explicit LauncherPage(QWidget *parent = nullptr) : QWizardPage(parent) {
    setTitle(LibraryOnboardingWizard::tr("Launcher"));
    setSubTitle(LibraryOnboardingWizard::tr(
        "These are the launchers Kartend found on your PATH. Pick one or more — "
        "the first checked becomes the collection's primary launcher."));
    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto &launcher : LauncherProbe::probeInstalled()) {
      auto *item = new QListWidgetItem(
          QStringLiteral("%1  —  %2").arg(launcher.displayName, launcher.binary), m_list);
      item->setData(Qt::UserRole, launcher.binary);
      item->setData(Qt::UserRole + 1, launcher.launchParameters);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Unchecked);
    }
    if (m_list->count() == 0) {
      auto *empty = new QListWidgetItem(
          LibraryOnboardingWizard::tr("(no known launchers on PATH — Skip and add one in "
                                      "Settings → Launchers later.)"),
          m_list);
      empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled);
    }
    layout->addWidget(m_list);
  }

  /// Walks the checked rows in their stable list order and returns the
  /// (binary, parameters) of the first one — the wizard uses this to
  /// seed the primary launcher slot. Returns empty fields when nothing
  /// is checked, which is fine — the user can finish without a launcher
  /// configured and add one later.
  struct Selection {
    QString binary;
    QString parameters;
  };
  [[nodiscard]] Selection chosen() const {
    if (!m_list) return {};
    for (int i = 0; i < m_list->count(); ++i) {
      QListWidgetItem *item = m_list->item(i);
      if (item->checkState() == Qt::Checked) {
        Selection out;
        out.binary = item->data(Qt::UserRole).toString();
        out.parameters = item->data(Qt::UserRole + 1).toString();
        return out;
      }
    }
    return {};
  }

private:
  QListWidget *m_list = nullptr;
};

/// Page 5: summary. Echoes the choices back so the user can review
/// before Finish.
class SummaryPage : public QWizardPage {
public:
  explicit SummaryPage(QWidget *parent = nullptr) : QWizardPage(parent) {
    setTitle(LibraryOnboardingWizard::tr("Confirm"));
    setSubTitle(LibraryOnboardingWizard::tr(
        "Review the configuration. Click Finish to create the collection."));
    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary);
    layout->addStretch();
  }

  void initializePage() override {
    const QString name = field(QString::fromLatin1(kFieldName)).toString();
    const QString dir = field(QString::fromLatin1(kFieldMediaDir)).toString();
    const QString type = field(QString::fromLatin1(kFieldType)).toString();
    const QString artwork = field(QString::fromLatin1(kFieldArtworkDir)).toString();

    QStringList lines;
    lines << LibraryOnboardingWizard::tr("<b>Name:</b> %1").arg(name.toHtmlEscaped());
    lines << LibraryOnboardingWizard::tr("<b>Media folder:</b> %1").arg(dir.toHtmlEscaped());
    if (!type.isEmpty()) {
      lines << LibraryOnboardingWizard::tr("<b>Type:</b> %1").arg(type.toHtmlEscaped());
    }
    if (!artwork.trimmed().isEmpty()) {
      lines
          << LibraryOnboardingWizard::tr("<b>Artwork folder:</b> %1").arg(artwork.toHtmlEscaped());
    }
    m_summary->setText(lines.join(QStringLiteral("<br>")));
  }

private:
  QLabel *m_summary = nullptr;
};

} // namespace

LibraryOnboardingWizard::LibraryOnboardingWizard(QWidget *parent) : QWizard(parent) {
  setWindowTitle(tr("New library wizard"));
  setWizardStyle(QWizard::ModernStyle);
  setOption(QWizard::NoBackButtonOnStartPage, true);

  buildWelcomePage();
  buildMediaPage();
  buildTypePage();
  buildLauncherPage();
  buildSummaryPage();

  // Wire Finish: collect the form values into the result config so the
  // caller can read them off accept(). Done here rather than in the
  // SummaryPage so we can also grab the LauncherPage's checked rows
  // (which aren't field-registered — it's a multi-select list).
  connect(this, &QWizard::accepted, this, [this]() {
    m_result.accepted = true;
    m_result.pickedConfig.name = field(QString::fromLatin1(kFieldName)).toString().trimmed();
    m_result.pickedConfig.mediaDirectory =
        field(QString::fromLatin1(kFieldMediaDir)).toString().trimmed();
    m_result.pickedConfig.type = field(QString::fromLatin1(kFieldType)).toString().trimmed();
    m_result.pickedConfig.artworkDirectory =
        field(QString::fromLatin1(kFieldArtworkDir)).toString().trimmed();
    // Pull the chosen launcher from the LauncherPage. page() ids are
    // assigned in addPage order; the launcher page is the 4th (0-indexed
    // 3).
    if (auto *launcherPage = dynamic_cast<LauncherPage *>(page(3))) {
      const auto sel = launcherPage->chosen();
      if (!sel.binary.isEmpty()) {
        m_result.pickedConfig.launcher.launcherPath = sel.binary;
        m_result.pickedConfig.launcher.launchParameters = sel.parameters;
      }
    }
  });
}

void LibraryOnboardingWizard::buildWelcomePage() {
  addPage(new WelcomePage(this));
}
void LibraryOnboardingWizard::buildMediaPage() {
  addPage(new MediaPage(this));
}
void LibraryOnboardingWizard::buildTypePage() {
  addPage(new TypePage(this));
}
void LibraryOnboardingWizard::buildLauncherPage() {
  addPage(new LauncherPage(this));
}
void LibraryOnboardingWizard::buildSummaryPage() {
  addPage(new SummaryPage(this));
}
