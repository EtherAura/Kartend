// Pure parsers for the No-Intro daily-download flow. See the header for the
// verified request sequence; everything here is a function of captured bytes.
#include "nointroparse.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

namespace NoIntroParse {

namespace {

// Read attribute `attr` from a single tag string. Tolerates single/double
// quotes and arbitrary attribute order — the page is hand-written PHP output
// whose attribute order we must not depend on.
QString attr(const QString &tag, const QString &name) {
  const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*([\"'])(.*?)\\1").arg(name),
                              QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(tag);
  return m.hasMatch() ? m.captured(2) : QString();
}

// Slice out the `<form name="daily" ...> ... </form>` block. Returns empty
// when absent. Non-greedy to the first </form> after the opening tag.
QString dailyFormBlock(const QString &html) {
  const QRegularExpression open(QStringLiteral("<form[^>]*\\bname\\s*=\\s*[\"']daily[\"'][^>]*>"),
                                QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch om = open.match(html);
  if (!om.hasMatch()) {
    return QString();
  }
  const int start = om.capturedStart();
  const int close = html.indexOf(QStringLiteral("</form>"), om.capturedEnd(), Qt::CaseInsensitive);
  if (close < 0) {
    return html.mid(start); // unterminated; take the rest, still parseable
  }
  return html.mid(start, close - start);
}

} // namespace

DailyForm parseDailyPage(const QByteArray &html) {
  DailyForm out;
  const QString form = dailyFormBlock(QString::fromUtf8(html));
  if (form.isEmpty()) {
    return out;
  }

  // Iterate every <input ...> in the form once; classify by type/name.
  const QRegularExpression inputRe(QStringLiteral("<input\\b[^>]*>"),
                                   QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator it = inputRe.globalMatch(form);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    const QString tag = match.captured(0);
    const QString type = attr(tag, QStringLiteral("type")).toLower();
    const QString name = attr(tag, QStringLiteral("name"));
    const bool checked = !attr(tag, QStringLiteral("checked")).isEmpty() ||
                         tag.contains(QStringLiteral("checked"), Qt::CaseInsensitive);

    if (type == QLatin1String("radio") && name == QLatin1String("dat_type")) {
      const QString value = attr(tag, QStringLiteral("value"));
      if (!value.isEmpty()) {
        out.datTypes.append(value);
        if (checked) {
          out.defaultDatType = value;
        }
      }
    } else if (type == QLatin1String("checkbox") && name.startsWith(QLatin1String("set"))) {
      // setN selection checkboxes. include_numbered lacks the "set" prefix, so
      // the guard already excludes it. The human label is the text node right
      // after the input (e.g. "…> Main"); fall back to the field name.
      DailySet s;
      s.field = name;
      const int textStart = match.capturedEnd();
      const int nextTag = form.indexOf(QLatin1Char('<'), textStart);
      const QString label =
          form.mid(textStart, (nextTag < 0 ? form.size() : nextTag) - textStart).simplified();
      s.label = label.isEmpty() ? name : label;
      s.checkedByDefault = checked;
      out.sets.append(s);
    } else if (type == QLatin1String("submit") && name.startsWith(QLatin1String("daily_dl_"))) {
      out.requestField = name;
      out.packDate = name.mid(QStringLiteral("daily_dl_").size());
    }
  }

  if (out.defaultDatType.isEmpty()) {
    out.defaultDatType = QStringLiteral("dat");
  }
  // The Request button is the load-bearing element — without it there is no
  // download to POST. Sets/dat_type can legitimately be empty on some systems.
  out.valid = !out.requestField.isEmpty();
  return out;
}

QString parseConfirmToken(const QByteArray &html) {
  // <input type="submit" name="<32-hex>" value="Download!!">. Anchor on the
  // value so we never grab some other hex-named field on the page.
  const QString s = QString::fromUtf8(html);
  const QRegularExpression inputRe(QStringLiteral("<input\\b[^>]*>"),
                                   QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator it = inputRe.globalMatch(s);
  while (it.hasNext()) {
    const QString tag = it.next().captured(0);
    if (attr(tag, QStringLiteral("type")).toLower() != QLatin1String("submit")) {
      continue;
    }
    if (!attr(tag, QStringLiteral("value")).startsWith(QLatin1String("Download"))) {
      continue;
    }
    const QString name = attr(tag, QStringLiteral("name"));
    if (QRegularExpression(QStringLiteral("^[0-9a-fA-F]{32}$")).match(name).hasMatch()) {
      return name;
    }
  }
  return QString();
}

QString filenameFromContentDisposition(const QString &headerValue) {
  if (headerValue.isEmpty()) {
    return QString();
  }
  // Prefer RFC 5987 filename*=, then quoted filename=, then bare filename=.
  QRegularExpression ext(QStringLiteral("filename\\*\\s*=\\s*[^']*''([^;]+)"),
                         QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatch m = ext.match(headerValue);
  if (m.hasMatch()) {
    return QString::fromUtf8(QByteArray::fromPercentEncoding(m.captured(1).trimmed().toUtf8()));
  }
  QRegularExpression quoted(QStringLiteral("filename\\s*=\\s*\"([^\"]+)\""),
                            QRegularExpression::CaseInsensitiveOption);
  m = quoted.match(headerValue);
  if (m.hasMatch()) {
    return m.captured(1).trimmed();
  }
  QRegularExpression bare(QStringLiteral("filename\\s*=\\s*([^;]+)"),
                          QRegularExpression::CaseInsensitiveOption);
  m = bare.match(headerValue);
  if (m.hasMatch()) {
    return m.captured(1).trimmed();
  }
  return QString();
}

} // namespace NoIntroParse
