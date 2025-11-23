#ifndef EXTENSIONUTILS_H
#define EXTENSIONUTILS_H

#include <QStringList>

class ExtensionUtils
{
public:
    static QStringList parseUserExtensionList(const QString& text);
    static QStringList normalizeStoredExtensions(const QStringList& raw);
    static const QStringList& imageBaseExtensions();
    static QStringList imageFilters();

};

#endif