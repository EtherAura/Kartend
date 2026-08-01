// Round-trip + malformed-input tests for PlaylistSerializer (Kartend-c0dwd).
// The serializer is pure format + file I/O (no DB, no signals), extracted
// from PlaylistManager in Kartend-k4alv — these tests pin the v2 JSON
// envelope, the v1 fallback rules, and the M3U dialect so a key typo between
// the export and import halves can no longer slip through silently.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "playlistserializer.h"

class TestPlaylistSerializer : public QObject {
  Q_OBJECT
private slots:
  void init();

  // JSON ---------------------------------------------------------------------
  void testJsonRoundTrip_staticPlaylist();
  void testJsonRoundTrip_smartPlaylist();
  void testJsonRoundTrip_unicodeAndSpacesSurvive();
  void testRoundTrip_pathVariants_data();
  void testRoundTrip_pathVariants();
  void testJsonExport_omitsReservedKind();
  void testParseJson_v1DocumentFallsBackToStatic();
  void testParseJson_missingVersionIsError();
  void testParseJson_malformedJsonIsError();
  void testParseJson_nonObjectRootIsError();
  void testParseJson_missingFileIsError();
  void testExportJson_unwritablePathIsError();

  // M3U ----------------------------------------------------------------------
  void testM3URoundTrip_pathsInOrder();
  void testReadM3UPaths_skipsCommentsAndBlanks();
  void testReadM3UPaths_missingFileIsError();

private:
  QString path(const QString &name) const { return m_dir->filePath(name); }
  std::unique_ptr<QTemporaryDir> m_dir;
};

void TestPlaylistSerializer::init() {
  m_dir = std::make_unique<QTemporaryDir>();
  QVERIFY2(m_dir->isValid(), "Failed to create temporary directory");
}

namespace {

PlaylistRow makeRow() {
  PlaylistRow row;
  row.id = QStringLiteral("pl-1");
  row.name = QStringLiteral("Documentaries");
  row.icon = QStringLiteral("folder-videos");
  row.parentCollectionUuid = QStringLiteral("uuid-parent");
  row.createdAt = QStringLiteral("2026-06-01T10:00:00Z");
  row.updatedAt = QStringLiteral("2026-06-02T11:30:00Z");
  return row;
}

QList<PlaylistItemRef> makeItems() {
  PlaylistItemRef a;
  a.position = 0;
  a.sourceCollectionUuid = QStringLiteral("uuid-a");
  a.sourcePath = QStringLiteral("/media/videos/first one.mkv");
  a.addedAt = QStringLiteral("2026-06-01T10:01:00Z");
  PlaylistItemRef b;
  b.position = 1;
  b.sourceCollectionUuid = QStringLiteral("uuid-b");
  b.sourcePath = QStringLiteral("/media/videos/second.mkv");
  b.addedAt = QStringLiteral("2026-06-01T10:02:00Z");
  return {a, b};
}

} // namespace

void TestPlaylistSerializer::testJsonRoundTrip_staticPlaylist() {
  const QString out = path(QStringLiteral("static.json"));
  const auto items = makeItems();

  const auto exported = PlaylistSerializer::exportJson(makeRow(), items, out);
  QVERIFY2(!exported.isError(), "exportJson failed");
  QCOMPARE(exported.value(), 2);

  const auto parsed = PlaylistSerializer::parseJsonFile(out);
  QVERIFY2(!parsed.isError(), "parseJsonFile failed");
  const auto &pl = parsed.value();
  QCOMPARE(pl.name, QStringLiteral("Documentaries"));
  QCOMPARE(pl.parentCollectionUuid, QStringLiteral("uuid-parent"));
  QCOMPARE(pl.isSmart, false);
  QCOMPARE(pl.items.size(), 2);
  QCOMPARE(pl.items[0].sourceCollectionUuid, QStringLiteral("uuid-a"));
  QCOMPARE(pl.items[0].sourcePath, QStringLiteral("/media/videos/first one.mkv"));
  QCOMPARE(pl.items[1].sourceCollectionUuid, QStringLiteral("uuid-b"));
  QCOMPARE(pl.items[1].sourcePath, QStringLiteral("/media/videos/second.mkv"));
}

void TestPlaylistSerializer::testJsonRoundTrip_smartPlaylist() {
  const QString out = path(QStringLiteral("smart.json"));
  PlaylistRow row = makeRow();
  row.isSmart = true;
  row.smartFilterJson = QStringLiteral(R"({"mode":"recent","days":30})");

  const auto exported = PlaylistSerializer::exportJson(row, {}, out);
  QVERIFY(!exported.isError());
  QCOMPARE(exported.value(), 0);

  const auto parsed = PlaylistSerializer::parseJsonFile(out);
  QVERIFY(!parsed.isError());
  QCOMPARE(parsed.value().isSmart, true);
  QCOMPARE(parsed.value().smartFilterJson, row.smartFilterJson);
  QVERIFY(parsed.value().items.isEmpty());
}

void TestPlaylistSerializer::testJsonRoundTrip_unicodeAndSpacesSurvive() {
  const QString out = path(QStringLiteral("unicode.json"));
  PlaylistRow row = makeRow();
  row.name = QStringLiteral("Vidéos préférées 🎬");
  QList<PlaylistItemRef> items = makeItems();
  items[0].sourcePath = QStringLiteral("/média/vidéos/Übung — テスト 🎞.mkv");

  QVERIFY(!PlaylistSerializer::exportJson(row, items, out).isError());
  const auto parsed = PlaylistSerializer::parseJsonFile(out);
  QVERIFY(!parsed.isError());
  QCOMPARE(parsed.value().name, row.name);
  QCOMPARE(parsed.value().items[0].sourcePath, items[0].sourcePath);
}

void TestPlaylistSerializer::testRoundTrip_pathVariants_data() {
  // Per-shape rows for the user paths the serializer persists verbatim
  // (Kartend-1yev5): each variant must survive BOTH the JSON envelope and the
  // line-oriented M3U dialect unchanged. The M3U side is the riskier one — a
  // trim() or codec slip there silently corrupts the path.
  QTest::addColumn<QString>("sourcePath");

  QTest::newRow("spaces") << QStringLiteral("/media/videos/two  inner spaces.mkv");
  QTest::newRow("diacritics") << QStringLiteral("/média/vidéos/Übung señal.mkv");
  QTest::newRow("cjk") << QStringLiteral("/media/動画/テスト 集.mkv");
  QTest::newRow("emoji") << QStringLiteral("/media/clips/festival 🎬🎞.mkv");
  QTest::newRow("mixed") << QStringLiteral("/média/動画 — clips/nº1 テスト 🎥.mkv");
}

void TestPlaylistSerializer::testRoundTrip_pathVariants() {
  QFETCH(QString, sourcePath);

  QList<PlaylistItemRef> items = makeItems();
  items[0].sourcePath = sourcePath;

  // JSON round trip.
  const QString jsonOut = path(QStringLiteral("variant.json"));
  QVERIFY(!PlaylistSerializer::exportJson(makeRow(), items, jsonOut).isError());
  const auto parsed = PlaylistSerializer::parseJsonFile(jsonOut);
  QVERIFY(!parsed.isError());
  QCOMPARE(parsed.value().items[0].sourcePath, sourcePath);

  // M3U round trip.
  const QString m3uOut = path(QStringLiteral("variant.m3u"));
  QVERIFY(!PlaylistSerializer::exportM3U(items, m3uOut).isError());
  const auto paths = PlaylistSerializer::readM3UPaths(m3uOut);
  QVERIFY(!paths.isError());
  QCOMPARE(paths.value().first(), sourcePath);
}

void TestPlaylistSerializer::testJsonExport_omitsReservedKind() {
  // Exporting a reserved playlist (e.g. favorites) must not write
  // reserved_kind — re-importing would otherwise create a duplicate reserved
  // row that deletePlaylist refuses to clean up (documented in exportJson).
  const QString out = path(QStringLiteral("reserved.json"));
  PlaylistRow row = makeRow();
  row.reservedKind = QStringLiteral("favorites");
  QVERIFY(!PlaylistSerializer::exportJson(row, {}, out).isError());

  QFile f(out);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  QVERIFY(!root.contains(QStringLiteral("reserved_kind")));
  QCOMPARE(root.value(QStringLiteral("kartend_playlist_version")).toInt(), 2);
}

void TestPlaylistSerializer::testParseJson_v1DocumentFallsBackToStatic() {
  // v1 documents never carry is_smart / smart_filter; they must parse as
  // static playlists, not error out.
  const QString in = path(QStringLiteral("v1.json"));
  QJsonObject doc;
  doc[QStringLiteral("kartend_playlist_version")] = 1;
  doc[QStringLiteral("name")] = QStringLiteral("Old export");
  QJsonObject item;
  item[QStringLiteral("source_collection_uuid")] = QStringLiteral("uuid-a");
  item[QStringLiteral("source_path")] = QStringLiteral("/media/a.mkv");
  doc[QStringLiteral("items")] = QJsonArray{item};
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(doc).toJson());
  f.close();

  const auto parsed = PlaylistSerializer::parseJsonFile(in);
  QVERIFY(!parsed.isError());
  QCOMPARE(parsed.value().name, QStringLiteral("Old export"));
  QCOMPARE(parsed.value().isSmart, false);
  QVERIFY(parsed.value().smartFilterJson.isEmpty());
  QCOMPARE(parsed.value().items.size(), 1);
}

void TestPlaylistSerializer::testParseJson_missingVersionIsError() {
  const QString in = path(QStringLiteral("noversion.json"));
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QByteArrayLiteral(R"({"name":"x","items":[]})"));
  f.close();
  QVERIFY(PlaylistSerializer::parseJsonFile(in).isError());
}

void TestPlaylistSerializer::testParseJson_malformedJsonIsError() {
  const QString in = path(QStringLiteral("garbage.json"));
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QByteArrayLiteral("{not json at all"));
  f.close();
  QVERIFY(PlaylistSerializer::parseJsonFile(in).isError());
}

void TestPlaylistSerializer::testParseJson_nonObjectRootIsError() {
  const QString in = path(QStringLiteral("array.json"));
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QByteArrayLiteral("[1, 2, 3]"));
  f.close();
  QVERIFY(PlaylistSerializer::parseJsonFile(in).isError());
}

void TestPlaylistSerializer::testParseJson_missingFileIsError() {
  QVERIFY(PlaylistSerializer::parseJsonFile(path(QStringLiteral("absent.json"))).isError());
}

void TestPlaylistSerializer::testExportJson_unwritablePathIsError() {
  // A regular file occupying a directory component — mkpath and open both
  // fail. (A merely nonexistent parent is created by atomicWriteFile.)
  QFile blocker(path(QStringLiteral("blocker")));
  QVERIFY(blocker.open(QIODevice::WriteOnly));
  blocker.close();
  const QString out = path(QStringLiteral("blocker/out.json"));
  QVERIFY(PlaylistSerializer::exportJson(makeRow(), {}, out).isError());
}

void TestPlaylistSerializer::testM3URoundTrip_pathsInOrder() {
  const QString out = path(QStringLiteral("list.m3u"));
  QList<PlaylistItemRef> items = makeItems();
  items[0].sourcePath = QStringLiteral("/média/Übung — テスト 🎞.mkv"); // unicode survives

  const auto exported = PlaylistSerializer::exportM3U(items, out);
  QVERIFY(!exported.isError());
  QCOMPARE(exported.value(), 2);

  const auto paths = PlaylistSerializer::readM3UPaths(out);
  QVERIFY(!paths.isError());
  QCOMPARE(paths.value(), (QStringList{items[0].sourcePath, items[1].sourcePath}));

  // The export is Extended-M3U: marker on line 1, EXTINF per entry.
  QFile f(out);
  QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString content = QString::fromUtf8(f.readAll());
  QVERIFY(content.startsWith(QStringLiteral("#EXTM3U\n")));
  QCOMPARE(content.count(QStringLiteral("#EXTINF:-1,")), 2);
}

void TestPlaylistSerializer::testReadM3UPaths_skipsCommentsAndBlanks() {
  const QString in = path(QStringLiteral("hand.m3u"));
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  f.write(QByteArrayLiteral("#EXTM3U\n"
                            "\n"
                            "#EXTINF:-1,first\n"
                            "/media/a.mkv\n"
                            "   \n"
                            "# a stray comment\n"
                            "  /media/with leading space.mkv  \n"));
  f.close();

  const auto paths = PlaylistSerializer::readM3UPaths(in);
  QVERIFY(!paths.isError());
  QCOMPARE(paths.value(), (QStringList{QStringLiteral("/media/a.mkv"),
                                       QStringLiteral("/media/with leading space.mkv")}));
}

void TestPlaylistSerializer::testReadM3UPaths_missingFileIsError() {
  QVERIFY(PlaylistSerializer::readM3UPaths(path(QStringLiteral("absent.m3u"))).isError());
}

QTEST_GUILESS_MAIN(TestPlaylistSerializer)

#include "test_playlistserializer.moc"
