#ifndef LAUNCHERPROBE_H
#define LAUNCHERPROBE_H

#include <QList>
#include <QString>

/// PATH-probe for well-known launcher binaries.
///
/// Walks a curated list of media-player / reader / image-viewer / emulator
/// command names via ConfigValidation::isCommandInPath() and returns the
/// ones actually installed on the system. The Settings → Launchers panel
/// surfaces the result as a multi-select picker so the user can promote a
/// handful of detected binaries to LauncherPresets in a single click,
/// rather than having to type each launcher path by hand.
///
/// The probe list is intentionally led by **media-player tools** (video,
/// audio, reader, image) — emulators come last because the typical
/// Kartend collection is a media library, not a games library. This
/// matches the project doc convention of defaulting examples to
/// video/audio/reference categories rather than emulators.
namespace LauncherProbe {

enum class Category {
  Video,
  Audio,
  Reader,
  Image,
  Emulator,
};

/// One curated entry. `binary` is the exact command name passed to
/// QStandardPaths::findExecutable (case-sensitive on Linux); `displayName`
/// is the human label used both for the picker row and the new
/// LauncherPreset's name field. `launchParameters` is the command-line
/// template the new preset is created with — `"%1"` for everything in
/// the curated list except retroarch, which carries `"-L %core %1"` so
/// the corePath slot stays meaningful.
struct KnownLauncher {
  QString binary;
  QString displayName;
  QString launchParameters;
  Category category = Category::Video;
};

/// Full curated list (whether installed or not). Static helper that
/// returns the same content every call — useful for tests that want to
/// assert against the list without invoking the filesystem probe.
[[nodiscard]] QList<KnownLauncher> knownLaunchers();

/// Return only those `knownLaunchers()` entries whose `binary` resolves
/// via QStandardPaths::findExecutable. Result order matches the curated
/// order in `knownLaunchers()` (video → audio → reader → image →
/// emulator).
[[nodiscard]] QList<KnownLauncher> probeInstalled();

/// Short human-readable category label for the picker section headers.
/// Translated via QCoreApplication::translate.
[[nodiscard]] QString categoryLabel(Category category);

} // namespace LauncherProbe

#endif // LAUNCHERPROBE_H
