// Tests for ScreenScraperMediaTypeCache (Kartend-vyz9u).
//
// SS's mediasJeuListe.php catalog maps canonical media tags (box-2D,
// screenshot, bezel-16-9, ...) to friendly labels + metadata. This module is
// the pure parse + on-disk cache layer (the network fetch lives in the
// provider), so the parsing, case-insensitive lookup, and round-trip can be
// tested without a network or QApplication.
#include <QByteArray>
#include <QDir>
#include <QList>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "screenscrapermediatypecache.h"

using ScreenScraperMediaTypeCache::find;
using ScreenScraperMediaTypeCache::MediaType;
using ScreenScraperMediaTypeCache::parseMediaTypesResponse;

namespace {

// A mediasJeuListe.php reply: response.medias[]. box-2D carries both fr and en
// labels (en should win); multiregions arrives as a real bool here.
QByteArray catalogReply() {
  return QByteArrayLiteral("{\"response\":{\"medias\":["
                           "{\"type\":\"box-2D\",\"categorie\":\"jeu\",\"fileformat\":\"png\","
                           "\"multiregions\":true,\"multisupports\":false,"
                           "\"nom\":[{\"langue\":\"fr\",\"text\":\"Boite "
                           "(2D)\"},{\"langue\":\"en\",\"text\":\"Box (2D)\"}]},"
                           "{\"type\":\"screenshot\",\"categorie\":\"jeu\",\"fileformat\":\"png\","
                           "\"nom\":[{\"langue\":\"en\",\"text\":\"Screenshot\"}]}"
                           "]}}");
}

} // namespace

class TestScreenScraperMediaTypeCache : public QObject {
  Q_OBJECT
private slots:
  void parse_responseShape_mapsFieldsAndPrefersEnglish();
  void parse_rawMediasShape_isTolerated();
  void parse_emptyMedias_isEmptySuccess();
  void parse_invalidJson_isError();
  void parse_defensiveDefaults_skipUntyped_fallbackLabel_stringBool();
  void find_isCaseInsensitive_andMissReturnsNull();
  void saveThenLoad_roundTrips();
  void isCacheStale_trueWhenMissing_falseWhenFresh();
};

void TestScreenScraperMediaTypeCache::parse_responseShape_mapsFieldsAndPrefersEnglish() {
  const auto result = parseMediaTypesResponse(catalogReply());
  QVERIFY(!result.isError());
  const QList<MediaType> types = result.value();
  QCOMPARE(types.size(), 2);

  const MediaType *box = find(types, QStringLiteral("box-2d"));
  QVERIFY(box != nullptr);
  QCOMPARE(box->type, QStringLiteral("box-2d"));          // lowercased
  QCOMPARE(box->displayName, QStringLiteral("Box (2D)")); // en preferred over fr
  QCOMPARE(box->category, QStringLiteral("jeu"));
  QCOMPARE(box->fileFormat, QStringLiteral("png"));
  QVERIFY(box->multiRegions);
  QVERIFY(!box->multiSupports);
}

void TestScreenScraperMediaTypeCache::parse_rawMediasShape_isTolerated() {
  // A round-tripped cache file is the bare {medias:[...]} shape, no response wrapper.
  const auto result = parseMediaTypesResponse(
      QByteArrayLiteral("{\"medias\":[{\"type\":\"wheel\",\"categorie\":\"jeu\"}]}"));
  QVERIFY(!result.isError());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value().first().type, QStringLiteral("wheel"));
}

void TestScreenScraperMediaTypeCache::parse_emptyMedias_isEmptySuccess() {
  const auto result = parseMediaTypesResponse(QByteArrayLiteral("{\"response\":{\"medias\":[]}}"));
  QVERIFY2(!result.isError(), "empty catalog is a successful empty list, not an error");
  QCOMPARE(result.value().size(), 0);
}

void TestScreenScraperMediaTypeCache::parse_invalidJson_isError() {
  const auto result = parseMediaTypesResponse(QByteArrayLiteral("not json at all"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestScreenScraperMediaTypeCache::
    parse_defensiveDefaults_skipUntyped_fallbackLabel_stringBool() {
  const auto result = parseMediaTypesResponse(
      QByteArrayLiteral("{\"medias\":["
                        "{\"categorie\":\"jeu\"},"                         // no type -> skipped
                        "{\"type\":\"manuel\",\"multiregions\":\"true\"}," // no nom -> label falls
                                                                           // back; bool-as-string
                        "{}"                                               // empty -> skipped
                        "]}"));
  QVERIFY(!result.isError());
  const QList<MediaType> types = result.value();
  QCOMPARE(types.size(), 1);
  QCOMPARE(types.first().type, QStringLiteral("manuel"));
  QCOMPARE(types.first().displayName, QStringLiteral("manuel")); // fallback to canonical tag
  QVERIFY(types.first().multiRegions);                           // "true" string parsed as bool
}

void TestScreenScraperMediaTypeCache::find_isCaseInsensitive_andMissReturnsNull() {
  const QList<MediaType> types = parseMediaTypesResponse(catalogReply()).value();
  QVERIFY(find(types, QStringLiteral("BOX-2D")) != nullptr);         // case-insensitive hit
  QVERIFY(find(types, QStringLiteral("  screenshot  ")) != nullptr); // trimmed
  QVERIFY(find(types, QStringLiteral("nonexistent")) == nullptr);
  QVERIFY(find(types, QString()) == nullptr);
}

void TestScreenScraperMediaTypeCache::saveThenLoad_roundTrips() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = QDir(dir.path()).filePath(QStringLiteral("mediatypes.json"));

  const QList<MediaType> original = parseMediaTypesResponse(catalogReply()).value();
  QVERIFY(ScreenScraperMediaTypeCache::saveMediaTypes(path, original));

  const auto loaded = ScreenScraperMediaTypeCache::loadCachedMediaTypes(path);
  QVERIFY(!loaded.isError());
  QCOMPARE(loaded.value().size(), original.size());

  const MediaType *box = find(loaded.value(), QStringLiteral("box-2d"));
  QVERIFY(box != nullptr);
  QCOMPARE(box->displayName, QStringLiteral("Box (2D)"));
  QCOMPARE(box->category, QStringLiteral("jeu"));
  QCOMPARE(box->fileFormat, QStringLiteral("png"));
  QVERIFY(box->multiRegions);
}

void TestScreenScraperMediaTypeCache::isCacheStale_trueWhenMissing_falseWhenFresh() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = QDir(dir.path()).filePath(QStringLiteral("mediatypes.json"));

  QVERIFY2(ScreenScraperMediaTypeCache::isCacheStale(path), "missing file is stale");

  QVERIFY(ScreenScraperMediaTypeCache::saveMediaTypes(
      path, parseMediaTypesResponse(catalogReply()).value()));
  QVERIFY2(!ScreenScraperMediaTypeCache::isCacheStale(path),
           "a freshly-written cache is not stale");
}

QTEST_MAIN(TestScreenScraperMediaTypeCache)
#include "test_screenscrapermediatypecache.moc"
