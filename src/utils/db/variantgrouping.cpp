#include "variantgrouping.h"

#include <QFileInfo>
#include <QHash>

namespace VariantGrouping {

QList<VariantGroup> groupByBaseName(const QStringList &itemPaths) {
  // First pass: bucket by lowercase completeBaseName. Preserve the original
  // case of the first occurrence as the display name so groups read
  // naturally (we don't want every group title forced to lowercase).
  QHash<QString, VariantGroup> buckets;
  for (const QString &path : itemPaths) {
    if (path.trimmed().isEmpty()) continue;
    const QString base = QFileInfo(path).completeBaseName();
    if (base.isEmpty()) continue;
    const QString key = base.toLower();
    auto it = buckets.find(key);
    if (it == buckets.end()) {
      VariantGroup group;
      group.baseName = base;
      group.paths.append(path);
      buckets.insert(key, group);
    } else {
      it->paths.append(path);
    }
  }

  // Drop single-member buckets — they aren't variants of anything.
  QList<VariantGroup> out;
  out.reserve(buckets.size());
  for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
    if (it->paths.size() < 2) continue;
    out.append(it.value());
  }

  std::sort(out.begin(), out.end(), [](const VariantGroup &a, const VariantGroup &b) {
    return a.baseName.compare(b.baseName, Qt::CaseInsensitive) < 0;
  });
  return out;
}

} // namespace VariantGrouping
