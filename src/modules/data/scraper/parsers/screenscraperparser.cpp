// JSON parser for ScreenScraper.fr jeuInfos.php responses. Their
// payload is deeply nested with region/language-keyed inner arrays;
// the helpers below collapse those to single values. Region-keyed
// fields (title, release date, artwork) follow the matched ROM's own
// region, then the user's configured fallback region, then a fixed
// English-leaning chain. Language-keyed free-text fields (description,
// genres, families, modes) follow the application UI language, then
// the same fixed chain.
#include "screenscraperparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUrlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScreenScraperParser {

namespace {

// SS controls the remote media `type` (used downstream as an on-disk
// subdirectory) and the group/company `scopeKey` (used as a filename). Gate
// both before they can reach a filesystem path in scrapepersistence.

// A safe single path component: non-empty, no separators, not `.`/`..`. A
// hostile `type` like "../../etc" is dropped (Kartend-xncf).
bool isSafePathComponent(const QString &s) {
  return !s.isEmpty() && !s.contains(QLatin1Char('/')) && !s.contains(QLatin1Char('\\')) &&
         s != QLatin1String(".") && s != QLatin1String("..");
}

// group/company ids are numeric SS identifiers; an allowlist is the tightest
// gate for a value that becomes a filename (Kartend-xhbt).
bool isValidScopeKey(const QString &s) {
  static const QRegularExpression re(
      QRegularExpression::anchoredPattern(QStringLiteral("[0-9A-Za-z_-]+")));
  return re.match(s).hasMatch();
}

const QStringList REGION_PREFERENCES = {
    QStringLiteral("us"), QStringLiteral("wor"), QStringLiteral("eu"),
    QStringLiteral("jp"), QStringLiteral("ss"),
};
const QStringList LANGUAGE_PREFERENCES = {
    QStringLiteral("en"),
    QStringLiteral("fr"),
    QStringLiteral("de"),
    QStringLiteral("es"),
};

/// Append a region/language tag to `prefs`, lowercased and trimmed,
/// skipping empties and duplicates so the preference scan stays a
/// clean ordered set.
void addPreferenceTag(QStringList &prefs, const QString &raw) {
  const QString tag = raw.trimmed().toLower();
  if (!tag.isEmpty() && !prefs.contains(tag)) {
    prefs.append(tag);
  }
}

/// Build the region preference order for a single scraped item.
/// The matched ROM's own region wins, so a Japanese cartridge keeps
/// its Japanese title and box art instead of collapsing to the US
/// entry; the user's configured fallback region comes next, then the
/// fixed English-leaning chain backstops everything. SS occasionally
/// reports a multi-region ROM as a comma/space-separated list — each
/// token is folded in, in order.
///
/// When `filenameOverride` is non-empty (Kartend-ou0a), it preempts
/// `itemRegion`. The caller sets this when the hash-based ID didn't
/// run and the filename carries a No-Intro region marker — without
/// the override SS's filename-only fallback often lands on the
/// canonical (US) jeu record even when "(Japan)" was right there in
/// the user's filename.
QStringList buildRegionPreferences(const QString &itemRegion, const QString &fallbackRegion,
                                   const QString &filenameOverride = QString()) {
  QStringList prefs;
  addPreferenceTag(prefs, filenameOverride);
  const auto tokens =
      itemRegion.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
  for (const QString &token : tokens) {
    addPreferenceTag(prefs, token);
  }
  addPreferenceTag(prefs, fallbackRegion);
  for (const QString &region : REGION_PREFERENCES) {
    addPreferenceTag(prefs, region);
  }
  return prefs;
}

/// Build the language preference order for free-text fields
/// (description, genres, families, modes). The application UI
/// language wins so those fields read in the same language as the
/// rest of Kartend; the fixed chain backstops it when SS has no
/// entry in that language.
QStringList buildLanguagePreferences(const QString &appLanguage) {
  QStringList prefs;
  addPreferenceTag(prefs, appLanguage);
  for (const QString &language : LANGUAGE_PREFERENCES) {
    addPreferenceTag(prefs, language);
  }
  return prefs;
}

/// Read a ScreenScraper `rom`-level tag that the API exposes in two
/// shapes: a flat `flatKey` string (e.g. `romregions: "jp"`), and an
/// `objKey` object whose `<objKey>_shortname` is a JSON *array* (e.g.
/// `regions: { "regions_shortname": ["jp"] }`). The flat string wins;
/// otherwise the first array element is taken. A scalar `_shortname`
/// is tolerated too. Used for the matched ROM's region and language.
QString romTagShortname(const QJsonObject &rom, const QString &flatKey, const QString &objKey) {
  const QString flat = rom.value(flatKey).toString().trimmed();
  if (!flat.isEmpty()) {
    return flat;
  }
  const QJsonValue shortname =
      rom.value(objKey).toObject().value(objKey + QStringLiteral("_shortname"));
  if (shortname.isArray()) {
    const QJsonArray arr = shortname.toArray();
    return arr.isEmpty() ? QString() : arr.first().toString().trimmed();
  }
  return shortname.toString().trimmed();
}

/// Parse a ScreenScraper JSON payload, tolerating trailing garbage.
/// SS occasionally appends a PHP warning/notice/stack trace after the
/// closing `}` of an otherwise-valid response (e.g. "Lost World, The —
/// Jurassic Park — Special Edition (USA)"). Qt rejects this as
/// `GarbageAtEnd`. When that happens, retry once on the prefix up to
/// the error offset — that's the position of the first byte past the
/// valid document. Any other error is returned to the caller.
QJsonDocument parseTolerant(const QByteArray &json, QJsonParseError *outErr) {
  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(json, &err);
  if (err.error == QJsonParseError::GarbageAtEnd && err.offset > 0 && err.offset <= json.size()) {
    QJsonParseError retryErr{};
    QJsonDocument retry = QJsonDocument::fromJson(json.left(err.offset), &retryErr);
    if (retryErr.error == QJsonParseError::NoError) {
      if (outErr) *outErr = retryErr;
      return retry;
    }
  }
  if (outErr) *outErr = err;
  return doc;
}

/// Pick the best entry from an array of {region/langue: tag, text:
/// value} objects per the supplied preference list. Falls back to the
/// first entry's text when no preferred tag matches. Returns empty
/// when the array is empty.
QString pickByTag(const QJsonArray &arr, const char *tagKey, const QStringList &preferences,
                  const char *valueKey = "text") {
  if (arr.isEmpty()) return {};
  // Build a tag→text lookup so the preference scan is O(P + N).
  QHash<QString, QString> byTag;
  byTag.reserve(arr.size());
  QString firstNonEmpty;
  for (const auto &v : arr) {
    const QJsonObject obj = v.toObject();
    const QString tag = obj.value(QString::fromLatin1(tagKey)).toString().toLower();
    const QString text = obj.value(QString::fromLatin1(valueKey)).toString();
    if (text.isEmpty()) continue;
    byTag.insert(tag, text);
    if (firstNonEmpty.isEmpty()) firstNonEmpty = text;
  }
  for (const QString &pref : preferences) {
    const auto it = byTag.constFind(pref);
    if (it != byTag.constEnd()) {
      return it.value();
    }
  }
  return firstNonEmpty;
}

/// Pluck the first element's `.text` — used for non-localized scalar
/// objects SS returns as `{"text":"..."}` (editeur, developpeur,
/// joueurs, etc.).
QString readSingleText(const QJsonValue &v) {
  if (v.isString()) return v.toString();
  return v.toObject().value("text").toString();
}

/// SS's media endpoint takes `maxwidth` / `maxheight` query params that
/// trigger server-side downscaling, plus `outputformat=jpg|png` to
/// pick the encoding. Only applied to images — videos
/// (`mediaVideoJeu`) and manuals (`mediaManuelJeu`) ignore these
/// params, so we leave their URLs untouched. JPG output yields a 3-5x
/// size win over the default PNG at typical scrape resolutions but is
/// lossy; gated on the user's explicit preset preference rather than
/// applied unconditionally.
QUrl rewriteMediaUrl(const QUrl &source, int maxDim, bool preferJpg) {
  const QString path = source.path();
  if (path.contains(QStringLiteral("mediaManuelJeu"), Qt::CaseInsensitive) ||
      path.contains(QStringLiteral("mediaVideoJeu"), Qt::CaseInsensitive)) {
    return source;
  }
  if (maxDim <= 0 && !preferJpg) return source;
  QUrlQuery q(source);
  if (maxDim > 0) {
    if (!q.hasQueryItem(QStringLiteral("maxwidth"))) {
      q.addQueryItem(QStringLiteral("maxwidth"), QString::number(maxDim));
    }
    if (!q.hasQueryItem(QStringLiteral("maxheight"))) {
      q.addQueryItem(QStringLiteral("maxheight"), QString::number(maxDim));
    }
  }
  if (preferJpg && !q.hasQueryItem(QStringLiteral("outputformat"))) {
    q.addQueryItem(QStringLiteral("outputformat"), QStringLiteral("jpg"));
  }
  QUrl out(source);
  out.setQuery(q);
  return out;
}

/// Map SS's `medias[]` array to Scraper::MediaAsset. Each entry is
/// `{ type: "box-2D", region: "us", url: "...", format: "png" }`. We
/// keep one asset per (type) — preferring the item's own region per
/// `regionPrefs` — so the dialog's media checkbox panel doesn't show
/// 6 box-2D variants and a Japanese cart gets Japanese box art.
QList<Scraper::MediaAsset> mapMedia(const QJsonArray &medias, int mediaMaxDim, bool preferJpg,
                                    const QHash<QString, QString> &mediaTypeLabels,
                                    const QStringList &regionPrefs) {
  QHash<QString, QPair<int, Scraper::MediaAsset>> byType;
  // Region preference index: lower = better.
  auto regionRank = [&regionPrefs](const QString &region) {
    const int idx = regionPrefs.indexOf(region.toLower());
    return idx < 0 ? 999 : idx;
  };
  for (const auto &v : medias) {
    const QJsonObject m = v.toObject();
    const QString type = m.value("type").toString();
    const QString url = m.value("url").toString();
    if (type.isEmpty() || url.isEmpty()) continue;
    // Kartend-xncf: `type` is the on-disk subdirectory; drop any asset whose
    // type isn't a safe single path component before it can traverse out of
    // artworkDirectory.
    if (!isSafePathComponent(type)) continue;
    const int rank = regionRank(m.value("region").toString());
    Scraper::MediaAsset asset;
    asset.type = type;
    asset.label = type;
    asset.url = rewriteMediaUrl(QUrl(url), mediaMaxDim, preferJpg);
    // Scope detection drives the persistence layer's per-game vs
    // _shared/ routing. SS exposes three media endpoints; the URL
    // path tells us which scope this asset belongs to. The scopeKey
    // (groupid / companyid) is the dedup identifier used when many
    // games share the same theme / publisher art.
    const QUrl assetUrl = asset.url;
    const QString assetPath = assetUrl.path();
    if (assetPath.contains(QStringLiteral("/mediaGroup.php"), Qt::CaseInsensitive)) {
      asset.scope = Scraper::MediaScope::Group;
      asset.scopeKey = QUrlQuery(assetUrl).queryItemValue(QStringLiteral("groupid"));
    } else if (assetPath.contains(QStringLiteral("/mediaCompagnie.php"), Qt::CaseInsensitive)) {
      asset.scope = Scraper::MediaScope::Company;
      asset.scopeKey = QUrlQuery(assetUrl).queryItemValue(QStringLiteral("companyid"));
    } else {
      asset.scope = Scraper::MediaScope::Game;
    }
    // Kartend-xhbt: a group/company scopeKey becomes a shared filename. If SS
    // returns a non-id value, drop back to per-game scope so the write uses the
    // safe per-game baseName instead of the attacker-controlled key.
    if (asset.scope != Scraper::MediaScope::Game && !isValidScopeKey(asset.scopeKey)) {
      asset.scope = Scraper::MediaScope::Game;
      asset.scopeKey.clear();
    }
    auto existing = byType.constFind(type);
    if (existing == byType.constEnd() || rank < existing.value().first) {
      byType.insert(type, qMakePair(rank, asset));
    }
  }
  QList<Scraper::MediaAsset> out;
  out.reserve(byType.size());
  for (const auto &pair : byType) {
    out.append(pair.second);
  }
  // Only normalise SS types that have a *unique* canonical Kartend
  // name — `box-2D` → `front`, `sstitle` → `title`, `screenshot`
  // stays, and `manuel`/`manual` collapse to one locale-neutral
  // `manual` (locale duplicates, not visual variants). Everything
  // else keeps its SS-provided type so distinct visual variants get
  // distinct on-disk subdirectories and never collide:
  //   wheel-hd / wheel / wheel-carbon / wheel-steel — different logo styles,
  //   screenmarquee / marquee — different marquee art,
  //   fanart / theme — different background art,
  //   box-2D-back / box-2D-side / box-3D / box-texture / bezel-* / ...
  //
  // The dialog shows each variant as a separate checkbox; the
  // persistence layer writes each one to {artwork}/<ss-type>/, with
  // its own item_artwork DB row for non-standard types. Storage is
  // collision-free; the only de-dup point is byType above (region
  // preference for the *same* SS-side type).
  for (auto &asset : out) {
    const QString lower = asset.type.toLower();
    QString mapped;
    QString label;
    if (lower == QStringLiteral("box-2d")) {
      mapped = QStringLiteral("front");
      label = QStringLiteral("Box (front)");
    } else if (lower == QStringLiteral("screenshot") || lower == QStringLiteral("ss")) {
      // SS serves the in-game screenshot under the short tag `ss`;
      // some responses also use the long `screenshot`. Same asset —
      // collapse both to the canonical `screenshot` type.
      mapped = QStringLiteral("screenshot");
      label = QStringLiteral("Screenshot");
    } else if (lower == QStringLiteral("sstitle")) {
      mapped = QStringLiteral("title");
      label = QStringLiteral("Title screen");
    } else if (lower == QStringLiteral("manuel") || lower == QStringLiteral("manual")) {
      // SS uses the French "manuel"; some other providers use the
      // English "manual". Same asset, locale spelling — collapse to
      // one tag so the persistence layer's MediaKind dispatch
      // routes the file to `{artwork}/manual/<base>.pdf` (rather
      // than treating it as an image) regardless of which spelling
      // arrived. Not a visual variant.
      mapped = QStringLiteral("manual");
      label = QStringLiteral("Manual");
    }
    if (!mapped.isEmpty()) {
      asset.type = mapped;
      asset.label = label;
    } else if (!mediaTypeLabels.isEmpty()) {
      // Unknown-to-Kartend SS tag: fall back to the runtime catalog's
      // friendly label when we have it. Lookup is case-insensitive
      // because SS mixes case (`box-2D` vs `bezel-16-9`).
      const auto it = mediaTypeLabels.constFind(asset.type.toLower());
      if (it != mediaTypeLabels.constEnd() && !it.value().isEmpty()) {
        asset.label = it.value();
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const Scraper::MediaAsset &a, const Scraper::MediaAsset &b) {
    if (a.type == b.type) return false;
    if (a.type == QStringLiteral("front")) return true;
    if (b.type == QStringLiteral("front")) return false;
    return a.type < b.type;
  });
  return out;
}

/// Map a ScreenScraper `ssuser` JSON object onto the typed
/// ScreenScraperUserInfo struct. SS encodes numeric fields as JSON
/// strings (`"maxthreads": "6"`); `.toString().toInt()` handles both
/// the string and numeric variants, with the toInt fallback to 0
/// covering "field absent". Shared by `parseUserInfoResponse` (the
/// dedicated `ssuserInfos.php` endpoint) and `extractUserInfo` (the
/// `ssuser` block every `jeuInfos.php` reply also carries) so the two
/// callers can never drift apart on field names.
ScreenScraperUserInfo mapUserInfo(const QJsonObject &ssuser) {
  auto asInt = [](const QJsonValue &v) {
    if (v.isDouble()) return v.toInt();
    return v.toString().toInt();
  };
  ScreenScraperUserInfo info;
  info.id = ssuser.value(QStringLiteral("id")).toString();
  info.level = ssuser.value(QStringLiteral("niveau")).toString();
  info.maxThreads = asInt(ssuser.value(QStringLiteral("maxthreads")));
  info.requestsToday = asInt(ssuser.value(QStringLiteral("requeststoday")));
  info.maxRequestsPerDay = asInt(ssuser.value(QStringLiteral("maxrequestsperday")));
  info.maxDownloadSpeedKBps = asInt(ssuser.value(QStringLiteral("maxdownloadspeed")));
  info.maxRequestsPerMinute = asInt(ssuser.value(QStringLiteral("maxrequestspermin")));
  info.maxRequestsKoPerDay = asInt(ssuser.value(QStringLiteral("maxrequestskoperday")));
  info.requestsKoToday = asInt(ssuser.value(QStringLiteral("requestskotoday")));
  return info;
}

QString collectGenres(const QJsonArray &genres, const QStringList &languagePrefs) {
  QStringList names;
  for (const auto &v : genres) {
    const QJsonObject g = v.toObject();
    const QString name = pickByTag(g.value("noms").toArray(), "langue", languagePrefs);
    if (!name.isEmpty() && !names.contains(name)) {
      names.append(name);
    }
  }
  return names.join(QStringLiteral(", "));
}

ErrorUtils::Result<Scraper::ScrapedItem>
parseInner(const QByteArray &json, const ScreenScraperParser::ParseOptions &options = {}) {
  QJsonParseError err;
  const auto doc = parseTolerant(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "ScreenScraper response is not valid JSON",
                               "ScreenScraperParser::parseInner")
        .withDetails(err.errorString());
  }
  const QJsonObject jeu = doc.object().value("response").toObject().value("jeu").toObject();
  if (jeu.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "ScreenScraper returned no game match",
                               "ScreenScraperParser::parseInner");
  }

  // SS's `rom` object is the matched ROM variant. Its region drives
  // region-keyed field selection (title, release date, artwork) so a
  // non-US cartridge keeps its own title and box art; the configured
  // fallback region backstops it. Free-text fields (description,
  // genres, families, modes) follow the application UI language
  // instead, so they read in the same language as the rest of the app.
  const QJsonObject rom = jeu.value("rom").toObject();
  const QString itemRegion =
      romTagShortname(rom, QStringLiteral("romregions"), QStringLiteral("regions"));
  const QStringList regionPrefs =
      buildRegionPreferences(itemRegion, options.preferredRegion, options.filenameRegionOverride);
  const QStringList languagePrefs = buildLanguagePreferences(options.preferredLanguage);

  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("screenscraper");
  item.title = pickByTag(jeu.value("noms").toArray(), "region", regionPrefs);
  // SS encodes the synopsis as HTML-escaped text (`&quot;`, `&amp;`,
  // numeric entities, etc.) so it can ride through the JSON string
  // safely. The details pane renders the description as plain text,
  // so decode the entities here once at parse time rather than at
  // every render. QTextDocumentFragment::fromHtml is the cheapest
  // entity decoder in Qt and also strips any stray inline HTML tags
  // that SS occasionally leaves in.
  item.description = QTextDocumentFragment::fromHtml(
                         pickByTag(jeu.value("synopsis").toArray(), "langue", languagePrefs))
                         .toPlainText();
  item.publisher = readSingleText(jeu.value("editeur"));
  item.developer = readSingleText(jeu.value("developpeur"));
  item.players = readSingleText(jeu.value("joueurs"));
  item.releaseDate = pickByTag(jeu.value("dates").toArray(), "region", regionPrefs);
  item.genre = collectGenres(jeu.value("genres").toArray(), languagePrefs);

  // Classifications: SS returns multiple per-rating-board entries
  // (ESRB, PEGI, USK, CERO, ...). Prefer ESRB for the typed
  // contentRating field; surface every other board's rating in
  // customFields under classification_<boardname> so users can see
  // the full picture.
  const auto classifications = jeu.value("classifications").toArray();
  for (const auto &v : classifications) {
    const QJsonObject c = v.toObject();
    const QString board = c.value("type").toString();
    const QString rating = c.value("text").toString();
    if (board.isEmpty() || rating.isEmpty()) continue;
    if (board.toUpper() == QStringLiteral("ESRB")) {
      item.contentRating = rating;
    }
    item.customFields.insert(QStringLiteral("classification_") + board.toLower(), rating);
  }
  if (item.contentRating.isEmpty() && !classifications.isEmpty()) {
    item.contentRating = classifications.first().toObject().value("text").toString();
  }

  // Insert helper — only writes when value is non-empty so the
  // customFields map stays free of empty rows that would clutter
  // the gallery panel.
  auto putIfFilled = [&item](const QString &key, const QString &value) {
    if (!value.trimmed().isEmpty()) {
      item.customFields.insert(key, value.trimmed());
    }
  };

  // ── Game-level metadata ─────────────────────────────────────────
  putIfFilled(QStringLiteral("screenscraper_id"), jeu.value("id").toString());
  putIfFilled(QStringLiteral("cloneof"), jeu.value("cloneof").toString());
  // Group + company ids let the gallery probe `_shared/<type>/group_<id>.<ext>`
  // for theme/family/publisher art that lives outside the per-game
  // tree. SS may expose either as a scalar id or under a nested
  // object (`{id: ..., text: ...}`); accept both. Empty values stay
  // out of customFields per putIfFilled.
  auto idOrNested = [](const QJsonValue &v) -> QString {
    if (v.isString()) return v.toString().trimmed();
    if (v.isDouble()) return QString::number(v.toInt());
    const QJsonObject obj = v.toObject();
    if (obj.contains(QStringLiteral("id"))) {
      const QJsonValue inner = obj.value(QStringLiteral("id"));
      if (inner.isString()) return inner.toString().trimmed();
      if (inner.isDouble()) return QString::number(inner.toInt());
    }
    return {};
  };
  putIfFilled(QStringLiteral("screenscraper_groupid"),
              idOrNested(jeu.value(QStringLiteral("groupid"))));
  putIfFilled(QStringLiteral("screenscraper_companyid"),
              idOrNested(jeu.value(QStringLiteral("companyid"))));
  // notgame: SS exposes as "true"/"false" string. Forward verbatim.
  putIfFilled(QStringLiteral("notgame"), jeu.value("notgame").toString());
  // notes.text = SS's user rating (0-20 scale per their docs).
  putIfFilled(QStringLiteral("rating"), readSingleText(jeu.value("note")));
  // topstaff: editor's-pick flag.
  putIfFilled(QStringLiteral("topstaff"), jeu.value("topstaff").toString());
  // rotation: arcade vertical/horizontal/cocktail.
  putIfFilled(QStringLiteral("rotation"), jeu.value("rotation").toString());
  putIfFilled(QStringLiteral("resolution"), jeu.value("resolution").toString());
  putIfFilled(QStringLiteral("colors"), jeu.value("couleurs").toString());

  // controles[]: SS may return as array of strings or objects. Join
  // the user-visible names into a single comma-separated value.
  {
    QStringList controls;
    const auto arr = jeu.value("controles").toArray();
    for (const auto &v : arr) {
      QString name;
      if (v.isString()) {
        name = v.toString().trimmed();
      } else {
        const QJsonObject obj = v.toObject();
        name = obj.value("text").toString().trimmed();
        if (name.isEmpty()) name = obj.value("name").toString().trimmed();
      }
      if (!name.isEmpty() && !controls.contains(name)) controls.append(name);
    }
    putIfFilled(QStringLiteral("controls"), controls.join(QStringLiteral(", ")));
  }
  // familles[]: game series (Mario, Zelda, etc.). Same shape as
  // genres — array of objects with localized noms[].
  {
    QStringList families;
    for (const auto &v : jeu.value("familles").toArray()) {
      const QJsonObject f = v.toObject();
      const QString n = pickByTag(f.value("noms").toArray(), "langue", languagePrefs);
      if (!n.isEmpty() && !families.contains(n)) families.append(n);
    }
    putIfFilled(QStringLiteral("families"), families.join(QStringLiteral(", ")));
  }
  // modes[]: game modes (single-player, co-op, etc.). Same shape.
  {
    QStringList modes;
    for (const auto &v : jeu.value("modes").toArray()) {
      const QJsonObject m = v.toObject();
      const QString n = pickByTag(m.value("noms").toArray(), "langue", languagePrefs);
      if (!n.isEmpty() && !modes.contains(n)) modes.append(n);
    }
    putIfFilled(QStringLiteral("modes"), modes.join(QStringLiteral(", ")));
  }

  // ── ROM-level metadata ──────────────────────────────────────────
  // SS's `rom` object is the matched ROM variant (extracted above for
  // region-keyed field selection). All hash + file fields surface so
  // future hash-based re-scrape can round-trip without re-running the
  // lookup.
  putIfFilled(QStringLiteral("rom_filename"), rom.value("romfilename").toString());
  putIfFilled(QStringLiteral("rom_size"), rom.value("romsize").toString());
  putIfFilled(QStringLiteral("rom_md5"), rom.value("rommd5").toString());
  putIfFilled(QStringLiteral("rom_sha1"), rom.value("romsha1").toString());
  putIfFilled(QStringLiteral("rom_crc"), rom.value("romcrc").toString());
  putIfFilled(QStringLiteral("rom_type"), rom.value("romtype").toString());
  putIfFilled(QStringLiteral("rom_serial"), rom.value("romserial").toString());
  // Region + languages: SS gives these as a flat `romregions` /
  // `romlangues` string or a `regions` / `langues` object with an
  // array `*_shortname`. romTagShortname handles both.
  putIfFilled(QStringLiteral("region"), itemRegion);
  putIfFilled(QStringLiteral("languages"),
              romTagShortname(rom, QStringLiteral("romlangues"), QStringLiteral("langues")));

  item.media = mapMedia(jeu.value("medias").toArray(), options.mediaMaxDim, options.preferJpg,
                        options.mediaTypeLabels, regionPrefs);
  return item;
}

} // namespace

ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> parseSearchResponse(const QByteArray &json) {
  auto inner = parseInner(json);
  if (inner.isError()) {
    // "No match" is a benign empty list, not a hard error — match the
    // shape of the other parsers so the dialog shows "No matches"
    // instead of an error popup.
    if (inner.error().code == ErrorCode::FileNotFound) {
      return QList<Scraper::ScrapeCandidate>{};
    }
    return inner.error();
  }
  const Scraper::ScrapedItem &item = inner.value();
  Scraper::ScrapeCandidate cand;
  cand.displayName = item.title;
  cand.providerSpecificId = item.customFields.value(QStringLiteral("screenscraper_id"));
  // Subtitle: developer/publisher/year
  QStringList sub;
  if (!item.developer.isEmpty()) sub.append(item.developer);
  if (!item.publisher.isEmpty() && item.publisher != item.developer) sub.append(item.publisher);
  if (!item.releaseDate.isEmpty()) sub.append(item.releaseDate.left(4));
  cand.subtitle = sub.join(QStringLiteral(" · "));
  cand.matchScore = -1; // SS doesn't return a per-match score.
  // Use the front-cover URL as thumbnail when available.
  for (const auto &m : item.media) {
    if (m.type == QStringLiteral("front")) {
      cand.thumbnailUrl = m.url;
      break;
    }
  }
  QList<Scraper::ScrapeCandidate> out;
  out.append(cand);
  return out;
}

ErrorUtils::Result<Scraper::ScrapedItem> parseDetailResponse(const QByteArray &json,
                                                             const ParseOptions &options) {
  return parseInner(json, options);
}

ErrorUtils::Result<ScreenScraperUserInfo> parseUserInfoResponse(const QByteArray &json) {
  // SS sometimes replies with plain-text French error blobs ("Erreur
  // de login : ...", "Le quota ... est atteint", etc.) at HTTP 200.
  // Catch those before JSON parsing so the surfaced error is the
  // actual SS message rather than an opaque "invalid JSON".
  const QString head = QString::fromUtf8(json.left(64)).trimmed();
  if (!head.startsWith('{') && head.startsWith(QLatin1String("Erreur"))) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               QStringLiteral("ScreenScraper rejected the user-info request"),
                               "ScreenScraperParser::parseUserInfoResponse")
        .withDetails(QString::fromUtf8(json).trimmed());
  }
  QJsonParseError err{};
  const QJsonDocument doc = parseTolerant(json, &err);
  if (doc.isNull()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               QStringLiteral("Could not parse ssuserInfos.php response"),
                               "ScreenScraperParser::parseUserInfoResponse")
        .withDetails(err.errorString());
  }
  const QJsonObject root = doc.object();
  const QJsonObject response = root.value(QStringLiteral("response")).toObject();
  const QJsonObject ssuser = response.value(QStringLiteral("ssuser")).toObject();
  if (ssuser.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               QStringLiteral("Response missing `ssuser` block"),
                               "ScreenScraperParser::parseUserInfoResponse");
  }
  return mapUserInfo(ssuser);
}

std::optional<ScreenScraperUserInfo> extractUserInfo(const QByteArray &json) {
  // Best-effort: SS embeds `response.ssuser` in every jeuInfos.php
  // lookup/detail reply, but an anonymous-tier scrape legitimately
  // omits it. Any parse failure (plain-text error blob, garbage,
  // missing block) yields nullopt so the caller keeps its prior
  // quota snapshot rather than treating the absence as an error.
  const QString head = QString::fromUtf8(json.left(64)).trimmed();
  if (!head.startsWith('{')) return std::nullopt;
  QJsonParseError err{};
  const QJsonDocument doc = parseTolerant(json, &err);
  if (doc.isNull() || !doc.isObject()) return std::nullopt;
  const QJsonObject response = doc.object().value(QStringLiteral("response")).toObject();
  const QJsonObject ssuser = response.value(QStringLiteral("ssuser")).toObject();
  if (ssuser.isEmpty()) return std::nullopt;
  return mapUserInfo(ssuser);
}

ErrorUtils::Result<ScreenScraperInfraInfo> parseInfraInfoResponse(const QByteArray &json) {
  // Same plain-text-error guard as parseUserInfoResponse — SS sends
  // French error blobs at HTTP 200 when the dev creds are bad, and
  // those would otherwise come back as opaque JSON failures.
  const QString head = QString::fromUtf8(json.left(64)).trimmed();
  if (!head.startsWith('{') && head.startsWith(QLatin1String("Erreur"))) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               QStringLiteral("ScreenScraper rejected the infra-info request"),
                               "ScreenScraperParser::parseInfraInfoResponse")
        .withDetails(QString::fromUtf8(json).trimmed());
  }
  QJsonParseError err{};
  const QJsonDocument doc = parseTolerant(json, &err);
  if (doc.isNull()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               QStringLiteral("Could not parse ssinfraInfos.php response"),
                               "ScreenScraperParser::parseInfraInfoResponse")
        .withDetails(err.errorString());
  }
  const QJsonObject root = doc.object();
  const QJsonObject response = root.value(QStringLiteral("response")).toObject();
  if (response.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               QStringLiteral("Response missing `response` block"),
                               "ScreenScraperParser::parseInfraInfoResponse");
  }
  // SS encodes numeric + boolean fields as either typed JSON or
  // strings depending on the endpoint; tolerate both. For booleans
  // the SS convention is "1"/"0" or true/false.
  auto asInt = [](const QJsonValue &v) {
    if (v.isDouble()) return v.toInt();
    return v.toString().toInt();
  };
  auto asBool = [](const QJsonValue &v) {
    if (v.isBool()) return v.toBool();
    if (v.isDouble()) return v.toInt() != 0;
    const QString s = v.toString().trimmed().toLower();
    return s == QLatin1String("true") || s == QLatin1String("1");
  };
  // SS exposes the infra fields directly under `response` in the v2
  // shape; older docs nested under `serveurs` so probe both keys.
  QJsonObject src = response;
  if (response.contains(QStringLiteral("serveurs"))) {
    src = response.value(QStringLiteral("serveurs")).toObject();
  }
  ScreenScraperInfraInfo info;
  info.cpu1Percent = asInt(src.value(QStringLiteral("cpu1")));
  info.cpu2Percent = asInt(src.value(QStringLiteral("cpu2")));
  info.cpu3Percent = asInt(src.value(QStringLiteral("cpu3")));
  info.threadsPerMin = asInt(src.value(QStringLiteral("threadsmin")));
  info.activeScrapers = asInt(src.value(QStringLiteral("nbscrapeurs")));
  info.apiAccessToday = asInt(src.value(QStringLiteral("apiacces")));
  info.closedForNonMembers = asBool(src.value(QStringLiteral("closefornomember")));
  info.closedForLeechers = asBool(src.value(QStringLiteral("closeforleecher")));
  return info;
}

} // namespace ScreenScraperParser
