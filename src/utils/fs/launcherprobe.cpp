// Curated PATH probe for well-known launcher binaries. The list lives in
// one place — knownLaunchers() — so additions/removals are a single edit
// rather than two parallel arrays to keep in sync. Categories follow the
// project convention: media-player tools come first; emulators sit at the
// bottom for users who actually want them.
#include "launcherprobe.h"

#include <QCoreApplication>
#include <QStandardPaths>

namespace LauncherProbe {

namespace {

/// lupdate attributes a bare Tr::tr() in a namespace to a class it cannot find a
/// Q_OBJECT on and warns; those warnings then mask a real context mismatch
/// elsewhere (Kartend-r4tno — that is how MediaFolderPage stayed latent).
/// Declaring the context on a struct keeps extraction and runtime agreeing on
/// "LauncherProbe" — exactly what the hand-written wrapper did — while giving lupdate
/// something it recognises. Call sites read Tr::tr("…").
struct Tr {
  Q_DECLARE_TR_FUNCTIONS(LauncherProbe)
};

KnownLauncher make(const char *binary, const char *displayName, Category category,
                   const char *params = "%1") {
  KnownLauncher k;
  k.binary = QString::fromLatin1(binary);
  k.displayName = QString::fromLatin1(displayName);
  k.launchParameters = QString::fromLatin1(params);
  k.category = category;
  return k;
}

} // namespace

QList<KnownLauncher> knownLaunchers() {
  // Add new entries in category order so probeInstalled() output stays
  // grouped without a re-sort step. Within a category, list the most
  // common / most-FOSS-typical tools first.
  static const QList<KnownLauncher> list = {
      // ── Video ─────────────────────────────────────────────────────
      make("mpv", "mpv", Category::Video),
      make("vlc", "VLC", Category::Video),
      make("ffplay", "ffplay", Category::Video),
      make("celluloid", "Celluloid", Category::Video),
      make("haruna", "Haruna", Category::Video),
      make("smplayer", "SMPlayer", Category::Video),
      make("totem", "Totem", Category::Video),
      make("dragon", "Dragon Player", Category::Video),
      make("mpc-qt", "Media Player Classic Qute Theater", Category::Video),
      // ── Audio ─────────────────────────────────────────────────────
      make("audacious", "Audacious", Category::Audio),
      make("deadbeef", "DeaDBeeF", Category::Audio),
      make("elisa", "Elisa", Category::Audio),
      make("strawberry", "Strawberry", Category::Audio),
      make("clementine", "Clementine", Category::Audio),
      make("amarok", "Amarok", Category::Audio),
      make("mpg123", "mpg123", Category::Audio),
      make("ffmpeg", "ffmpeg (play)", Category::Audio, "-autoexit -nodisp \"%1\""),
      // ── Readers (PDF / EPUB / DJVU) ────────────────────────────────
      make("okular", "Okular", Category::Reader),
      make("evince", "Evince", Category::Reader),
      make("zathura", "Zathura", Category::Reader),
      make("foliate", "Foliate", Category::Reader),
      make("mupdf", "MuPDF", Category::Reader),
      make("xpdf", "xpdf", Category::Reader),
      // ── Image viewers ──────────────────────────────────────────────
      make("gwenview", "Gwenview", Category::Image),
      make("nomacs", "nomacs", Category::Image),
      make("geeqie", "Geeqie", Category::Image),
      make("feh", "feh", Category::Image),
      make("sxiv", "sxiv", Category::Image),
      // ── Emulators ─────────────────────────────────────────────────
      // Last on purpose — Kartend defaults its examples to media tools
      // per the project doc convention. Emulators are still included
      // for users who actually want them. RetroArch's launchParameters
      // template includes %core so the LauncherPreset's corePath slot
      // remains meaningful (otherwise it'd be a dead field on these
      // rows).
      make("retroarch", "RetroArch", Category::Emulator, "-L %core \"%1\""),
      make("mednafen", "Mednafen", Category::Emulator),
      make("ppsspp", "PPSSPP", Category::Emulator),
      make("dolphin-emu", "Dolphin", Category::Emulator),
      make("pcsx2", "PCSX2", Category::Emulator),
      make("mame", "MAME", Category::Emulator),
  };
  return list;
}

QList<KnownLauncher> probeInstalled() {
  QList<KnownLauncher> out;
  for (const auto &k : knownLaunchers()) {
    // Same one-liner that ConfigValidation::isCommandInPath uses
    // internally — inlined here so this module stays free of the
    // collectionutils dep chain that configvalidation drags in.
    if (!QStandardPaths::findExecutable(k.binary).isEmpty()) {
      out.append(k);
    }
  }
  return out;
}

QString categoryLabel(Category category) {
  switch (category) {
  case Category::Video:
    return Tr::tr("Video");
  case Category::Audio:
    return Tr::tr("Audio");
  case Category::Reader:
    return Tr::tr("Reader");
  case Category::Image:
    return Tr::tr("Image viewer");
  case Category::Emulator:
    return Tr::tr("Emulator");
  }
  return Tr::tr("Other");
}

} // namespace LauncherProbe
