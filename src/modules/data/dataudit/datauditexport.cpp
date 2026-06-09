#include "datauditexport.h"

#include <QSet>
#include <QXmlStreamWriter>

namespace DatAudit {

namespace {

// RFC 4180 field escaping: wrap in quotes when the field contains a comma,
// quote, CR or LF, doubling any embedded quote.
QString csvEscape(const QString &field) {
  if (field.contains(QLatin1Char(',')) || field.contains(QLatin1Char('"')) ||
      field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))) {
    QString q = field;
    q.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
  }
  return field;
}

QString missingName(const AuditRow &r) {
  return !r.gameName.isEmpty() ? r.gameName : r.expectedName;
}

} // namespace

QByteArray toCsv(const QList<AuditRow> &rows) {
  QString out;
  out += QLatin1String("status,game,expected_name,actual_name,file_path,size,crc,md5,sha1\n");
  for (const AuditRow &r : rows) {
    const QString sizeField = r.size >= 0 ? QString::number(r.size) : QString();
    const QStringList cols{statusToken(r.status),
                           r.gameName,
                           r.expectedName,
                           r.actualName,
                           r.filePath,
                           sizeField,
                           r.crc,
                           r.md5,
                           r.sha1};
    QStringList escaped;
    escaped.reserve(cols.size());
    for (const QString &c : cols) {
      escaped.append(csvEscape(c));
    }
    out += escaped.join(QLatin1Char(',')) + QLatin1Char('\n');
  }
  return out.toUtf8();
}

QByteArray toFixdat(const QList<AuditRow> &rows, const QString &datName) {
  QByteArray out;
  QXmlStreamWriter w(&out);
  w.setAutoFormatting(true);
  w.writeStartDocument();
  w.writeStartElement(QStringLiteral("datafile"));

  w.writeStartElement(QStringLiteral("header"));
  w.writeTextElement(QStringLiteral("name"), datName);
  w.writeTextElement(QStringLiteral("description"),
                     QStringLiteral("Missing entries exported by Kartend"));
  w.writeEndElement(); // header

  for (const AuditRow &r : rows) {
    if (r.status != Status::Missing) {
      continue;
    }
    w.writeStartElement(QStringLiteral("game"));
    w.writeAttribute(QStringLiteral("name"), r.gameName);
    w.writeStartElement(QStringLiteral("rom"));
    w.writeAttribute(QStringLiteral("name"), r.expectedName);
    if (r.size >= 0) {
      w.writeAttribute(QStringLiteral("size"), QString::number(r.size));
    }
    if (!r.crc.isEmpty()) {
      w.writeAttribute(QStringLiteral("crc"), r.crc);
    }
    if (!r.md5.isEmpty()) {
      w.writeAttribute(QStringLiteral("md5"), r.md5);
    }
    if (!r.sha1.isEmpty()) {
      w.writeAttribute(QStringLiteral("sha1"), r.sha1);
    }
    w.writeEndElement(); // rom
    w.writeEndElement(); // game
  }

  w.writeEndElement(); // datafile
  w.writeEndDocument();
  return out;
}

QString toMissList(const QList<AuditRow> &rows) {
  QSet<QString> seen;
  QStringList names;
  for (const AuditRow &r : rows) {
    if (r.status != Status::Missing) {
      continue;
    }
    const QString name = missingName(r);
    if (name.isEmpty() || seen.contains(name)) {
      continue;
    }
    seen.insert(name);
    names.append(name);
  }
  names.sort(Qt::CaseInsensitive);
  return names.join(QLatin1Char('\n'));
}

} // namespace DatAudit
