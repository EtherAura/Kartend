#ifndef KARTMERGEDIALOG_H
#define KARTMERGEDIALOG_H

#include <QDialog>

#include "itemmetadata.h"
#include "kartmerge.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
QT_END_NAMESPACE

class KartMergeDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(KartMergeDialog)

public:
  KartMergeDialog(const QString &itemPath, const ItemMetadataStore::ItemMetadata &existing,
                  const ItemMetadataStore::ItemMetadata &incoming, QWidget *parent = nullptr);

  [[nodiscard]] kart::MergeChoice choice() const { return m_choice; }
  [[nodiscard]] kart::MergePolicy policy() const { return m_policy; }
  [[nodiscard]] bool applyToAll() const;

private:
  kart::MergeChoice m_choice = kart::MergeChoice::Skip;
  kart::MergePolicy m_policy;
  QCheckBox *m_applyToAllCheck = nullptr;

  struct FieldRow {
    QCheckBox *preferIncoming = nullptr;
    bool *policyFlag = nullptr;
  };
  QList<FieldRow> m_rows;
};

#endif
