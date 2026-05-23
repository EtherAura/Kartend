#ifndef ARTWORKWIZARDDIALOG_H
#define ARTWORKWIZARDDIALOG_H

#include <functional>
#include <QDialog>
#include <QList>

#include "artworkcandidates.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

/// Per-item artwork assignment wizard. Walks a queue of items, shows a
/// ranked list of candidate images from the collection's artwork
/// directory (with thumbnail previews), and lets the user pick one,
/// browse for a manual file, skip, or stop.
///
/// The dialog is intentionally caller-driven: the queue + the
/// per-item "candidates" function are supplied at setup time, and the
/// "pick" callback writes the per-item artwork override. This keeps the
/// dialog free of DB and filesystem dependencies (handed via closures).
class ArtworkWizardDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ArtworkWizardDialog)
public:
  struct Entry {
    QString filePath;
    QString collectionUuid;
    QString itemName;
  };

  /// Returns the ranked candidates for the given item, scanning the
  /// caller-owned artwork directory.
  using CandidatesProvider = std::function<QList<ArtworkCandidates::Candidate>(const Entry &entry)>;
  /// Persists the per-item artwork override. The dialog calls this on
  /// "Pick" and "Browse..." with the chosen file path.
  using PickHandler = std::function<bool(const Entry &entry, const QString &chosenFilePath)>;

  explicit ArtworkWizardDialog(QWidget *parent = nullptr);

  void setQueue(QList<Entry> entries, CandidatesProvider candidatesFor, PickHandler onPick);

private slots:
  void onPickClicked();
  void onBrowseClicked();
  void onSkipClicked();
  void onCandidateChanged();

private:
  void setupUi();
  void renderCurrent();
  void advance();

  QLabel *m_progressLabel = nullptr;
  QLabel *m_headerLabel = nullptr;
  QLabel *m_pathLabel = nullptr;
  QListWidget *m_candidateList = nullptr;
  QLabel *m_previewLabel = nullptr;
  QLabel *m_emptyLabel = nullptr;
  QPushButton *m_pickButton = nullptr;
  QPushButton *m_browseButton = nullptr;
  QPushButton *m_skipButton = nullptr;
  QPushButton *m_closeButton = nullptr;

  QList<Entry> m_entries;
  CandidatesProvider m_candidatesFor;
  PickHandler m_onPick;
  int m_cursor = 0;
};

#endif // ARTWORKWIZARDDIALOG_H
