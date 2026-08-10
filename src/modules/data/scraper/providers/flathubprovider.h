#ifndef FLATHUBPROVIDER_H
#define FLATHUBPROVIDER_H

#include "providerbase.h"

#include <QString>
#include <QStringList>
#include <QUrl>

/// Flathub AppStream web API provider (Kartend-2bzbu). No credentials or
/// API key. Its purpose is metadata for Flatpak launcher-import stubs —
/// apps that are on neither ScreenScraper nor the Steam store, so without
/// it their details pane shows File Information and nothing else.
///
/// Identity: for launcher-import stubs (.kartlink whose target is a bare
/// reverse-DNS Flatpak app id, e.g. com.play0ad.zeroad) the id is read
/// straight from the file, so lookup() emits a single exact candidate with
/// no search request at all. There is no key-less GET search endpoint to
/// fall back on for arbitrary names, so a non-stub query only resolves when
/// it is itself shaped like an app id; WebSearch still offers the Flathub
/// site search for the "Look up online" menu.
///
/// Metadata-only by design: the launcher import already copies each app's
/// exported icon as its cover, so this provider fills the text fields
/// (description, developer, genre, release date, licence) and declares no
/// MediaFetch.
class FlathubProvider : public ProviderBase {
public:
  FlathubProvider();

  [[nodiscard]] QString id() const override { return QStringLiteral("flathub"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Flathub"); }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("games")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::WebSearch | Capability::MetadataLookup;
  }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  void lookup(const LookupContext &ctx, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;
};

#endif // FLATHUBPROVIDER_H
