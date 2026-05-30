// Tests for the SmartFilter JSON serialization. Pure data + JSON, no DB.
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTest>

#include "smartfilter.h"

class TestSmartFilter : public QObject {
  Q_OBJECT
private slots:
  void kindTagRoundTrip_coversEveryKind();
  void byDateAdded_roundTripsDaysParam();
  void fromJson_defaultsDaysWhenAbsent();
  void byCollection_roundTripsCollectionUuid();
  void byTitleSearch_roundTripsNeedle();
  void toJsonFromJson_recentlyLaunched();
  void toJsonFromJson_byExtension_normalisesEntries();
  void fromJson_rejectsMissingKind();
  void fromJson_rejectsUnknownKind();
  void fromJsonString_rejectsEmpty();
  void fromJson_defaultsLimitWhenAbsent();
  void humanLabel_includesParams();
};

void TestSmartFilter::kindTagRoundTrip_coversEveryKind() {
  // Each Kind must round-trip through the tag string. If a new Kind is
  // added without updating both kindToTag and tagToKind, this loop
  // surfaces the mismatch immediately.
  const SmartFilter::Kind kinds[] = {
      SmartFilter::Kind::RecentlyLaunched, SmartFilter::Kind::TopPlayed,
      SmartFilter::Kind::NeverPlayed,      SmartFilter::Kind::ByExtension,
      SmartFilter::Kind::HasArtwork,       SmartFilter::Kind::ByDateAdded,
      SmartFilter::Kind::Pinned,           SmartFilter::Kind::Hidden,
      SmartFilter::Kind::ContinueLater,    SmartFilter::Kind::ByCollection,
      SmartFilter::Kind::ByTitleSearch,    SmartFilter::Kind::MissingArtwork,
      SmartFilter::Kind::Favorite};
  for (auto k : kinds) {
    const QString tag = SmartFilter::kindToTag(k);
    QVERIFY2(!tag.isEmpty() && tag != "invalid", qPrintable(QString("tag: %1").arg(tag)));
    auto parsed = SmartFilter::tagToKind(tag);
    QVERIFY2(parsed.isOk(), qPrintable(QString("tag '%1' did not parse").arg(tag)));
    QCOMPARE(static_cast<int>(parsed.value()), static_cast<int>(k));
  }
}

void TestSmartFilter::toJsonFromJson_recentlyLaunched() {
  SmartFilter::Filter in;
  in.kind = SmartFilter::Kind::RecentlyLaunched;
  in.limit = 12;
  const auto json = SmartFilter::toJson(in);
  QCOMPARE(json.value("kind").toString(), QStringLiteral("recently_launched"));
  QCOMPARE(json.value("limit").toInt(), 12);
  // Empty extensions array is emitted unconditionally so consumers don't
  // have to special-case its absence.
  QVERIFY(json.contains("extensions"));

  auto parsed = SmartFilter::fromJson(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(static_cast<int>(parsed.value().kind),
           static_cast<int>(SmartFilter::Kind::RecentlyLaunched));
  QCOMPARE(parsed.value().limit, 12);
}

void TestSmartFilter::toJsonFromJson_byExtension_normalisesEntries() {
  SmartFilter::Filter in;
  in.kind = SmartFilter::Kind::ByExtension;
  // Mix of casing, leading dot, and surrounding whitespace — fromJson
  // should normalise them all to lowercase / dot-stripped.
  in.extensions = {".MP4", "  mkv ", "WEBM"};
  const auto json = SmartFilter::toJson(in);
  auto parsed = SmartFilter::fromJson(json);
  QVERIFY(parsed.isOk());
  // Note: toJson preserves input shape (we want round-trip stability for
  // existing rows); only fromJson does the cleanup, mirroring the
  // workflow where the dialog hands clean strings to toJson but
  // hand-edited DB rows might be messy.
  QCOMPARE(parsed.value().extensions, QStringList({"mp4", "mkv", "webm"}));
}

void TestSmartFilter::fromJson_rejectsMissingKind() {
  QJsonObject obj;
  obj["limit"] = 10;
  auto r = SmartFilter::fromJson(obj);
  QVERIFY(r.isError());
}

void TestSmartFilter::fromJson_rejectsUnknownKind() {
  QJsonObject obj;
  obj["kind"] = "asteroid_count";
  auto r = SmartFilter::fromJson(obj);
  QVERIFY(r.isError());
}

void TestSmartFilter::fromJsonString_rejectsEmpty() {
  auto r = SmartFilter::fromJsonString(QString());
  QVERIFY(r.isError());
}

void TestSmartFilter::fromJson_defaultsLimitWhenAbsent() {
  // Older smart_filter rows could be missing limit; the parser falls
  // back to 50 rather than 0 so the playlist doesn't appear empty.
  QJsonObject obj;
  obj["kind"] = "top_played";
  auto r = SmartFilter::fromJson(obj);
  QVERIFY(r.isOk());
  QCOMPARE(r.value().limit, 50);
}

void TestSmartFilter::byDateAdded_roundTripsDaysParam() {
  SmartFilter::Filter in;
  in.kind = SmartFilter::Kind::ByDateAdded;
  in.days = 14;
  const auto json = SmartFilter::toJson(in);
  QCOMPARE(json.value("kind").toString(), QStringLiteral("by_date_added"));
  QCOMPARE(json.value("days").toInt(), 14);

  auto parsed = SmartFilter::fromJson(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(static_cast<int>(parsed.value().kind), static_cast<int>(SmartFilter::Kind::ByDateAdded));
  QCOMPARE(parsed.value().days, 14);
}

void TestSmartFilter::fromJson_defaultsDaysWhenAbsent() {
  // Mirror the limit-default behaviour for older smart_filter rows that
  // pre-date the days field — fall back to 30 (canonical "what's new"
  // window) instead of zero.
  QJsonObject obj;
  obj["kind"] = "by_date_added";
  auto r = SmartFilter::fromJson(obj);
  QVERIFY(r.isOk());
  QCOMPARE(r.value().days, 30);
}

void TestSmartFilter::byCollection_roundTripsCollectionUuid() {
  SmartFilter::Filter in;
  in.kind = SmartFilter::Kind::ByCollection;
  in.collectionUuid = "uuid-abc-123";
  const auto json = SmartFilter::toJson(in);
  QCOMPARE(json.value("kind").toString(), QStringLiteral("by_collection"));
  QCOMPARE(json.value("collection_uuid").toString(), QStringLiteral("uuid-abc-123"));

  auto parsed = SmartFilter::fromJson(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().collectionUuid, QStringLiteral("uuid-abc-123"));
}

void TestSmartFilter::byTitleSearch_roundTripsNeedle() {
  SmartFilter::Filter in;
  in.kind = SmartFilter::Kind::ByTitleSearch;
  in.titleSearch = "concert";
  const auto json = SmartFilter::toJson(in);
  QCOMPARE(json.value("title_search").toString(), QStringLiteral("concert"));

  auto parsed = SmartFilter::fromJson(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().titleSearch, QStringLiteral("concert"));
}

void TestSmartFilter::humanLabel_includesParams() {
  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::TopPlayed;
  f.limit = 25;
  const QString label = SmartFilter::humanLabel(f);
  QVERIFY(label.contains("25"));
  // Kind-specific text — translatable but the substring must persist.
  QVERIFY(label.toLower().contains("most played") || label.toLower().contains("top"));
}

QTEST_MAIN(TestSmartFilter)
#include "test_smartfilter.moc"
