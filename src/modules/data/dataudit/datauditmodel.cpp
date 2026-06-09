#include "datauditmodel.h"

namespace DatAudit {

DatAuditModel::DatAuditModel(QObject *parent) : QAbstractTableModel(parent) {}

QString DatAuditModel::statusLabel(Status s) {
  switch (s) {
  case Status::Have:
    return tr("Have");
  case Status::WrongName:
    return tr("Wrong name");
  case Status::WrongHash:
    return tr("Wrong content");
  case Status::Duplicate:
    return tr("Duplicate");
  case Status::Unknown:
    return tr("Unknown");
  case Status::Corrupt:
    return tr("Corrupt");
  case Status::Missing:
    return tr("Missing");
  case Status::Unscanned:
    return tr("Unscanned");
  case Status::BadDump:
    return tr("Bad dump");
  }
  return tr("Unknown");
}

void DatAuditModel::setRows(QList<AuditRow> rows) {
  beginResetModel();
  m_rows = std::move(rows);
  rebuildVisible();
  endResetModel();
}

void DatAuditModel::setVisibleStatuses(const std::optional<QSet<Status>> &statuses) {
  beginResetModel();
  m_filter = statuses;
  rebuildVisible();
  endResetModel();
}

void DatAuditModel::rebuildVisible() {
  m_visible.clear();
  m_visible.reserve(m_rows.size());
  for (int i = 0; i < m_rows.size(); ++i) {
    if (!m_filter || m_filter->contains(m_rows.at(i).status)) {
      m_visible.append(i);
    }
  }
}

const AuditRow *DatAuditModel::rowAt(int visibleRow) const {
  if (visibleRow < 0 || visibleRow >= m_visible.size()) {
    return nullptr;
  }
  return &m_rows.at(m_visible.at(visibleRow));
}

int DatAuditModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

int DatAuditModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant DatAuditModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_visible.size()) {
    return {};
  }
  const AuditRow &r = m_rows.at(m_visible.at(index.row()));

  if (role == StatusRole) {
    return static_cast<int>(r.status);
  }
  if (role == FilePathRole) {
    return r.filePath;
  }
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case StatusColumn:
      return statusLabel(r.status);
    case GameColumn:
      return r.gameName;
    case ExpectedNameColumn:
      return r.expectedName;
    case FileColumn:
      return r.actualName.isEmpty() ? r.filePath : r.actualName;
    case SizeColumn:
      return r.size >= 0 ? QVariant(r.size) : QVariant();
    default:
      return {};
    }
  }
  return {};
}

QVariant DatAuditModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
    return {};
  }
  switch (section) {
  case StatusColumn:
    return tr("Status");
  case GameColumn:
    return tr("Game");
  case ExpectedNameColumn:
    return tr("Canonical name");
  case FileColumn:
    return tr("File");
  case SizeColumn:
    return tr("Size");
  default:
    return {};
  }
}

} // namespace DatAudit
