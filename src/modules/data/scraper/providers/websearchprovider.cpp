#include "websearchprovider.h"

#include <utility>

#include <QUrl>

WebSearchProvider::WebSearchProvider(QString id, QString displayName, QStringList categories,
                                     QString searchUrlTemplate)
    : m_id(std::move(id)), m_displayName(std::move(displayName)),
      m_categories(std::move(categories)), m_searchUrlTemplate(std::move(searchUrlTemplate)) {}

QUrl WebSearchProvider::searchUrl(const QString &query) const {
  if (m_searchUrlTemplate.isEmpty() || query.trimmed().isEmpty()) {
    return {};
  }
  // QUrl::toPercentEncoding handles the spaces / special-character
  // shapes most search endpoints want — '+' for space could be more
  // correct on some hosts but percent-encoding survives in every URL
  // context (path, query, fragment) so we lean on the more
  // conservative encoding.
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(m_searchUrlTemplate.arg(encoded));
}
