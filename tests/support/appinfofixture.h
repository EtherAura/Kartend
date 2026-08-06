#ifndef KARTENDTEST_APPINFOFIXTURE_H
#define KARTENDTEST_APPINFOFIXTURE_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace KartendTest {

/// Builder for synthetic appinfo.vdf files (V28 inline-key and V29
/// string-table formats) so SteamAppInfo tests and the launcher-import
/// metadata tests can stage realistic fixtures without shipping binary
/// blobs. Mirrors the layout documented in src/utils/fs/steamappinfo.h.
class AppInfoFixture {
public:
  explicit AppInfoFixture(bool v29) : m_v29(v29) {}

  // ── keyvalues emission (call between beginApp/endApp) ────────────────
  void beginMap(const QString &key) {
    m_kv.append(char(0x00));
    putKey(key);
  }
  void endMap() { m_kv.append(char(0x08)); }
  void putString(const QString &key, const QString &value) {
    m_kv.append(char(0x01));
    putKey(key);
    putCString(m_kv, value);
  }
  void putInt(const QString &key, qint32 value) {
    m_kv.append(char(0x02));
    putKey(key);
    putU32(m_kv, quint32(value));
  }

  /// Emits a raw byte in the KV stream — for malformed-record tests.
  void putRawByte(quint8 byte) { m_kv.append(char(byte)); }

  // ── app records ──────────────────────────────────────────────────────
  void beginApp(quint32 appId) {
    m_currentAppId = appId;
    m_kv.clear();
  }
  void endApp() {
    QByteArray blob;
    blob.fill('\0', 60); // infoState..sha1(binary) prelude — content unused
    blob.append(m_kv);
    // The real format closes each record's keyvalues with an outer-scope
    // 0x08 beyond the root map's own terminator; the parser requires it.
    blob.append(char(0x08));
    putU32(m_records, m_currentAppId);
    putU32(m_records, quint32(blob.size()));
    m_records.append(blob);
  }

  /// Convenience: a complete plausible game record.
  void addGame(quint32 appId, const QString &name, const QString &developer,
               const QString &publisher, qint32 releaseUtc) {
    beginApp(appId);
    beginMap(QStringLiteral("appinfo"));
    beginMap(QStringLiteral("common"));
    putString(QStringLiteral("name"), name);
    putString(QStringLiteral("type"), QStringLiteral("game"));
    if (releaseUtc > 0) {
      putInt(QStringLiteral("steam_release_date"), releaseUtc);
    }
    beginMap(QStringLiteral("genres"));
    putInt(QStringLiteral("0"), 1);  // Action
    putInt(QStringLiteral("1"), 25); // Adventure
    endMap();
    beginMap(QStringLiteral("category"));
    putInt(QStringLiteral("category_2"), 1);  // Single-player
    putInt(QStringLiteral("category_38"), 1); // Online Co-op
    endMap();
    putInt(QStringLiteral("metacritic_score"), 95);
    putInt(QStringLiteral("review_percentage"), 98);
    putString(QStringLiteral("controller_support"), QStringLiteral("full"));
    endMap(); // common
    beginMap(QStringLiteral("extended"));
    putString(QStringLiteral("developer"), developer);
    putString(QStringLiteral("publisher"), publisher);
    endMap(); // extended
    endMap(); // appinfo
    endApp();
  }

  /// Assembles the final file bytes.
  [[nodiscard]] QByteArray build() const {
    QByteArray out;
    putU32(out, m_v29 ? 0x07564429U : 0x07564428U);
    putU32(out, 1); // universe
    qsizetype tableOffsetPos = -1;
    if (m_v29) {
      tableOffsetPos = out.size();
      out.append(8, '\0'); // string table offset placeholder
    }
    out.append(m_records);
    putU32(out, 0); // appid 0 terminator
    if (m_v29) {
      const qint64 tableOffset = out.size();
      putU32(out, quint32(m_strings.size()));
      for (const QString &entry : m_strings) {
        putCString(out, entry);
      }
      for (int i = 0; i < 8; ++i) {
        out[tableOffsetPos + i] = char(quint64(tableOffset) >> (8 * i) & 0xFF);
      }
    }
    return out;
  }

private:
  void putKey(const QString &key) {
    if (!m_v29) {
      putCString(m_kv, key);
      return;
    }
    if (!m_stringIndex.contains(key)) {
      m_stringIndex.insert(key, quint32(m_strings.size()));
      m_strings.append(key);
    }
    putU32(m_kv, m_stringIndex.value(key));
  }

  static void putU32(QByteArray &out, quint32 value) {
    for (int i = 0; i < 4; ++i) {
      out.append(char(value >> (8 * i) & 0xFF));
    }
  }
  static void putCString(QByteArray &out, const QString &value) {
    out.append(value.toUtf8());
    out.append('\0');
  }

  bool m_v29;
  QByteArray m_records;
  QByteArray m_kv;
  quint32 m_currentAppId = 0;
  QStringList m_strings;
  QHash<QString, quint32> m_stringIndex;
};

} // namespace KartendTest

#endif
