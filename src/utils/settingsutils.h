#ifndef SETTINGSUTILS_H
#define SETTINGSUTILS_H

#include "collectionconfig.h"
#include <QString>
#include <QList>
#include <QSettings>

class QWidget;

class SettingsUtils {
public:
    static auto getConfigPath() -> QString;
    static auto getFormat() -> QSettings::Format;
    static auto loadMainScreenSettings(MainScreenConfig &config) -> void;
    static auto saveMainScreenSettings(const MainScreenConfig &config) -> void;
    static auto expandConfigVariables(const QString &input, const QString &collectionName) -> QString;
    static auto applyHorizontalScrollbarSetting(QWidget *parent, int collectionIndex, const QList<CollectionConfig> &collections) -> void;
    static auto applyVerticalScrollbarSetting(QWidget *parent, int collectionIndex, const QList<CollectionConfig> &collections) -> void;
};

#endif // SETTINGSUTILS_H
