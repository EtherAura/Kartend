#ifndef IFILECOLLECTIONLOOKUP_H
#define IFILECOLLECTIONLOOKUP_H

#include <QString>

/**
 * @brief File → collection-index lookup role of the database layer
 * (Kartend-dl0uz.2).
 *
 * The one query EventManager's double-click dispatch needs: which
 * collection owns a file path. IDatabaseManager unions this role; the rest
 * of the database surface stays on IDatabaseManager itself.
 *
 * Keeps the concrete class's any-thread guarantee — implementers must
 * preserve it (see IDatabaseManager's thread-safety note).
 *
 * Plain abstract class, not a QObject — IDatabaseManager carries the single
 * QObject base. Reached via ctx->fileCollectionLookup().
 */
class IFileCollectionLookup {
public:
  virtual ~IFileCollectionLookup() = default;

  [[nodiscard]] virtual int getCollectionIndexForFile(const QString &filePath) const = 0;
};

#endif // IFILECOLLECTIONLOOKUP_H
