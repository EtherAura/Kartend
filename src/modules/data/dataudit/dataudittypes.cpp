#include "dataudittypes.h"

namespace DatAudit {

QString statusToken(Status s) {
  switch (s) {
  case Status::Have:
    return QStringLiteral("have");
  case Status::WrongName:
    return QStringLiteral("wrong_name");
  case Status::WrongHash:
    return QStringLiteral("wrong_hash");
  case Status::Duplicate:
    return QStringLiteral("duplicate");
  case Status::Unknown:
    return QStringLiteral("unknown");
  case Status::Corrupt:
    return QStringLiteral("corrupt");
  case Status::Missing:
    return QStringLiteral("missing");
  case Status::Unscanned:
    return QStringLiteral("unscanned");
  case Status::BadDump:
    return QStringLiteral("bad_dump");
  }
  return QStringLiteral("unknown");
}

bool isPresent(Status s) {
  return s == Status::Have || s == Status::WrongName;
}

QString mergeModeToken(MergeMode m) {
  switch (m) {
  case MergeMode::Merged:
    return QStringLiteral("merged");
  case MergeMode::NonMerged:
    return QStringLiteral("nonmerged");
  case MergeMode::Split:
    break;
  }
  return QStringLiteral("split");
}

MergeMode mergeModeFromToken(const QString &token) {
  if (token == QLatin1String("merged")) {
    return MergeMode::Merged;
  }
  if (token == QLatin1String("nonmerged")) {
    return MergeMode::NonMerged;
  }
  return MergeMode::Split;
}

AuditSummary summarize(const QList<AuditRow> &rows) {
  AuditSummary s;
  for (const AuditRow &r : rows) {
    switch (r.status) {
    case Status::Have:
      ++s.have;
      break;
    case Status::WrongName:
      ++s.wrongName;
      break;
    case Status::WrongHash:
      ++s.wrongHash;
      break;
    case Status::Duplicate:
      ++s.duplicate;
      break;
    case Status::Unknown:
      ++s.unknown;
      break;
    case Status::Corrupt:
      ++s.corrupt;
      break;
    case Status::Missing:
      ++s.missing;
      break;
    case Status::Unscanned:
    case Status::BadDump:
      break;
    }
  }
  return s;
}

} // namespace DatAudit
