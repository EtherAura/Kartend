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

  // FilterSet composition + wire compatibility (Kartend-r5dbe)
  void setToJson_singleRuleIsByteIdenticalToLegacyFilter();
  void setFromJson_legacySpecWithoutRulesParsesAsOneRule();
  void setRoundTrip_multiRulePreservesOrderAndMatchMode();
  void setToJson_multiRuleMirrorsFirstRuleForOlderBuilds();
  void setFromJson_rejectsUnknownMatchMode();
  void setFromJson_rejectsEmptyRulesArray();
  void setFromJson_oneBadRuleFailsTheWholeSet();
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

void TestSmartFilter::setToJson_singleRuleIsByteIdenticalToLegacyFilter() {
  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByTitleSearch;
  f.titleSearch = QStringLiteral("concert");
  f.limit = 25;

  SmartFilter::FilterSet set;
  set.rules = {f};

  // The whole no-migration promise rests on this: a one-rule set must write
  // the exact bytes a single Filter always wrote, so existing smart_filter
  // rows are never rewritten and an older build reads them unchanged. In
  // particular "match" and "rules" must be absent, not present-and-defaulted.
  QCOMPARE(SmartFilter::setToJsonString(set), SmartFilter::toJsonString(f));
  QVERIFY(!SmartFilter::setToJson(set).contains(QStringLiteral("rules")));
  QVERIFY(!SmartFilter::setToJson(set).contains(QStringLiteral("match")));
}

void TestSmartFilter::setFromJson_legacySpecWithoutRulesParsesAsOneRule() {
  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::Favorite;

  const auto parsed = SmartFilter::setFromJsonString(SmartFilter::toJsonString(f));
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().rules.size(), 1);
  QCOMPARE(parsed.value().rules.first().kind, SmartFilter::Kind::Favorite);
  // Absent match mode means All, which is the only reading that keeps a
  // one-rule playlist behaving as it did.
  QCOMPARE(parsed.value().match, SmartFilter::MatchMode::All);
}

void TestSmartFilter::setRoundTrip_multiRulePreservesOrderAndMatchMode() {
  SmartFilter::Filter first;
  first.kind = SmartFilter::Kind::ByExtension;
  first.extensions = {QStringLiteral("mp4")};
  SmartFilter::Filter second;
  second.kind = SmartFilter::Kind::ByTitleSearch;
  second.titleSearch = QStringLiteral("live");

  SmartFilter::FilterSet set;
  set.match = SmartFilter::MatchMode::Any;
  set.rules = {first, second};

  const auto parsed = SmartFilter::setFromJsonString(SmartFilter::setToJsonString(set));
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().match, SmartFilter::MatchMode::Any);
  QCOMPARE(parsed.value().rules.size(), 2);
  // Order is load-bearing: the first rule decides the result ordering, so a
  // round-trip that reorders rules would reorder the playlist.
  QCOMPARE(parsed.value().rules.at(0).kind, SmartFilter::Kind::ByExtension);
  QCOMPARE(parsed.value().rules.at(0).extensions, QStringList{QStringLiteral("mp4")});
  QCOMPARE(parsed.value().rules.at(1).kind, SmartFilter::Kind::ByTitleSearch);
  QCOMPARE(parsed.value().rules.at(1).titleSearch, QStringLiteral("live"));
}

void TestSmartFilter::setToJson_multiRuleMirrorsFirstRuleForOlderBuilds() {
  SmartFilter::Filter first;
  first.kind = SmartFilter::Kind::ByTitleSearch;
  first.titleSearch = QStringLiteral("keynote");
  SmartFilter::Filter second;
  second.kind = SmartFilter::Kind::Favorite;

  SmartFilter::FilterSet set;
  set.match = SmartFilter::MatchMode::All;
  set.rules = {first, second};

  // A build that predates composition — and the call sites that still parse a
  // single Filter to validate a spec — must read a multi-rule playlist as its
  // FIRST rule rather than failing. Narrower than intended, but it opens.
  const auto legacyView = SmartFilter::fromJsonString(SmartFilter::setToJsonString(set));
  QVERIFY(legacyView.isOk());
  QCOMPARE(legacyView.value().kind, SmartFilter::Kind::ByTitleSearch);
  QCOMPARE(legacyView.value().titleSearch, QStringLiteral("keynote"));
}

void TestSmartFilter::setFromJson_rejectsUnknownMatchMode() {
  // Guessing "all" for an unrecognised mode would silently show a strict
  // intersection to someone who asked for something else — missing items look
  // like a Kartend bug, a rejected spec looks like what it is.
  const auto parsed = SmartFilter::setFromJsonString(
      QStringLiteral(R"({"kind":"favorite","match":"most","rules":[{"kind":"favorite"}]})"));
  QVERIFY(parsed.isError());
}

void TestSmartFilter::setFromJson_rejectsEmptyRulesArray() {
  // An unconstrained set must never be read as "match the whole library".
  const auto parsed =
      SmartFilter::setFromJsonString(QStringLiteral(R"({"kind":"favorite","rules":[]})"));
  QVERIFY(parsed.isError());
}

void TestSmartFilter::setFromJson_oneBadRuleFailsTheWholeSet() {
  // Skipping an unparseable rule would change what the playlist MEANS —
  // dropping a rule from an "all" set widens it, surfacing items the user had
  // excluded. Failing leaves the playlist inert, which is what a single
  // unknown kind has always done.
  const auto parsed = SmartFilter::setFromJsonString(QStringLiteral(
      R"({"kind":"favorite","match":"all","rules":[{"kind":"favorite"},{"kind":"from_the_future"}]})"));
  QVERIFY(parsed.isError());
}

QTEST_MAIN(TestSmartFilter)
#include "test_smartfilter.moc"
