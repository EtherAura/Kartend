#ifndef SETTINGSUTILS_H
#define SETTINGSUTILS_H

#include "collectionutils.h"
#include <QList>
#include <QSettings>
#include <QString>

class QWidget;
class QScrollArea;

class SettingsUtils {
public:
  [[nodiscard]] static auto getConfigPath() -> QString;
  [[nodiscard]] static auto getFormat() -> QSettings::Format;
  [[nodiscard]] static auto expandConfigVariables(const QString &input,
                                                  const QString &collectionName)
      -> QString;
  static auto
  applyHorizontalScrollbarSetting(QScrollArea *scrollArea, int collectionIndex,
                                  const QList<CollectionConfig> &collections)
      -> void;
  static auto
  applyVerticalScrollbarSetting(QScrollArea *scrollArea, int collectionIndex,
                                const QList<CollectionConfig> &collections)
      -> void;
};

#endif // SETTINGSUTILS_H
