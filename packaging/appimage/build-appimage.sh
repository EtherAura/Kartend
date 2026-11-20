#!/usr/bin/env bash
# Build Kartend-<version>-x86_64.AppImage from this checkout, and check it.
#
# It runs inside the release base (the container image named in common.sh)
# after prepare-build-env.sh has installed the compiler, CMake, Qt and
# QtKeychain. build-in-container.sh does both in that image; that is the
# command to use:
#
#   packaging/appimage/build-in-container.sh
#
# What it does, in order. Any step that fails stops the script, and the
# AppImage gets its final name only after every check has passed.
#
#   1. Downloads the pinned packaging tools (linuxdeploy, appimagetool, the
#      AppImage runtime) and checks each against its SHA-256.
#   2. Configures and builds Kartend the way the .deb is built (Release,
#      portable flags, zstd, libarchive, SDL2 gamepad) plus QtKeychain, and
#      refuses to go on if one of those backends was not found.
#   3. Installs into an AppDir with the project's own install rules.
#   4. Copies in the Qt plugins Kartend needs (a fixed list, below), Qt's
#      translations and a qt.conf; linuxdeploy then bundles every library
#      those and the executable depend on. AppRun is then made to start
#      usr/bin/kartend itself, so the process is named kartend.
#   5. Checks the AppDir: the expected files are there, every library either
#      is in the bundle or is one the user's system is expected to provide,
#      and nothing asks for a newer C or C++ runtime than the release base has.
#   6. Packs the AppDir with appimagetool and the pinned runtime.
#   7. Checks the AppImage: x86_64 ELF, type-2 AppImage, the payload starts
#      where the runtime ends, and what it unpacks to is identical to the
#      AppDir, file for file.
#   8. Runs smoke-test.sh on it: it has to report its version, start
#      headless with its own Qt plugins, and pass the probe (SQLite, TLS,
#      image formats, and playing an H.264 and a VP9 clip from inside the
#      bundle).
#   9. Lays out sbom-root/ next to the AppImage: what is inside, in a form
#      the release's SBOM scanner can read in full.
#
# The last line on stdout is the path of the finished AppImage; everything
# else goes to stderr.
set -euo pipefail

KARTEND_APPIMAGE_PROG="build-appimage"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
# shellcheck source=packaging/appimage/common.sh
. "$HERE/common.sh"

readonly APP_ID="io.github.EtherAura.Kartend"

# Qt plugins that go into the AppImage, relative to Qt's plugins directory.
# A fixed list, not "whatever the Qt install has": the official Qt build also
# ships plugins Kartend has no use for and whose own dependencies are not on
# the base (GStreamer, MySQL, PostgreSQL, ODBC, GTK), and a list that is
# spelled out is one a Qt update cannot silently change. A file missing from
# the Qt install fails the build.
readonly QT_PLUGINS=(
  # X11, Wayland and the windowless platforms (offscreen is what tests use)
  platforms/libqxcb.so
  platforms/libqwayland-generic.so
  platforms/libqwayland-egl.so
  platforms/libqoffscreen.so
  platforms/libqminimal.so
  xcbglintegrations/libqxcb-glx-integration.so
  xcbglintegrations/libqxcb-egl-integration.so
  wayland-shell-integration/libxdg-shell.so
  wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
  wayland-decoration-client/libbradient.so
  wayland-decoration-client/libadwaita.so
  # input methods
  platforminputcontexts/libcomposeplatforminputcontextplugin.so
  platforminputcontexts/libibusplatforminputcontextplugin.so
  # artwork formats and the SVG icon engine
  imageformats/libqgif.so
  imageformats/libqicns.so
  imageformats/libqico.so
  imageformats/libqjpeg.so
  imageformats/libqsvg.so
  imageformats/libqtga.so
  imageformats/libqtiff.so
  imageformats/libqwbmp.so
  imageformats/libqwebp.so
  iconengines/libqsvgicon.so
  # the library database
  sqldrivers/libqsqlite.so
  # HTTPS for scrapers and artwork downloads
  tls/libqopensslbackend.so
  tls/libqcertonlybackend.so
  # video and audio playback through FFmpeg (Qt ships the FFmpeg libraries)
  multimedia/libffmpegmediaplugin.so
)

# Libraries that must be inside the AppImage, by file name under usr/lib.
# The Qt ones prove the plugins above pulled their Qt modules in; the rest are
# the optional backends (SDL2 gamepad, libarchive, zstd, libsecret for the
# keychain) and OpenSSL. OpenSSL and libsecret are loaded at run time by name.
readonly REQUIRED_LIBRARIES=(
  libQt6Core.so.6 libQt6Gui.so.6 libQt6Widgets.so.6 libQt6Sql.so.6
  libQt6Network.so.6 libQt6Multimedia.so.6 libQt6MultimediaWidgets.so.6
  libQt6Svg.so.6 libQt6DBus.so.6 libQt6XcbQpa.so.6 libQt6WaylandClient.so.6
  libavcodec.so.61 libavformat.so.61 libavutil.so.59 libswresample.so.5 libswscale.so.8
  libSDL2-2.0.so.0 libarchive.so.13 libzstd.so.1 libsecret-1.so.0
  libssl.so.3 libcrypto.so.3
)

usage() {
  cat <<EOF
Usage: build-appimage.sh [options]

  --prefix DIR              where prepare-build-env.sh put CMake, Qt and QtKeychain
                            (default: \$KARTEND_APPIMAGE_PREFIX, else $KARTEND_APPIMAGE_DEFAULT_PREFIX)
  --build-dir DIR           work directory (default: <repo>/build/appimage)
  --output-dir DIR          where the AppImage is written (default: the build directory)
  --tools-dir DIR           where the packaging tools are kept (default: <build-dir>/tools)
  --jobs N                  parallel build jobs (default: all cores)
  --compiler-launcher PROG  compiler launcher, e.g. ccache
  --skip-smoke-test         build and check, but do not start the AppImage
  --allow-unsupported-base  build on a system other than $KARTEND_APPIMAGE_BASE_IMAGE. The result
                            needs this system's C library or newer and is for testing
                            only; the checks that would refuse it become warnings.
  --print KEY               print one setting and exit: base-image, qt-version,
                            file-name, max-glibc
  -h, --help                show this help
EOF
}

PREFIX="${KARTEND_APPIMAGE_PREFIX:-$KARTEND_APPIMAGE_DEFAULT_PREFIX}"
BUILD_DIR="$REPO_ROOT/build/appimage"
OUTPUT_DIR=""
TOOLS_DIR=""
JOBS="$(nproc)"
COMPILER_LAUNCHER=""
SMOKE_TEST=1
ALLOW_UNSUPPORTED_BASE=0
PRINT_KEY=""

while (($# > 0)); do
  case "$1" in
    --prefix | --build-dir | --output-dir | --tools-dir | --jobs | --compiler-launcher | --print)
      (($# >= 2)) || kartend_appimage_die "$1 needs a value"
      [ -n "$2" ] || kartend_appimage_die "$1 needs a non-empty value"
      case "$1" in
        --prefix) PREFIX=$2 ;;
        --build-dir) BUILD_DIR=$2 ;;
        --output-dir) OUTPUT_DIR=$2 ;;
        --tools-dir) TOOLS_DIR=$2 ;;
        --jobs) JOBS=$2 ;;
        --compiler-launcher) COMPILER_LAUNCHER=$2 ;;
        --print) PRINT_KEY=$2 ;;
      esac
      shift 2
      ;;
    --skip-smoke-test)
      SMOKE_TEST=0
      shift
      ;;
    --allow-unsupported-base)
      ALLOW_UNSUPPORTED_BASE=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      kartend_appimage_die "unknown argument: $1"
      ;;
  esac
done

VERSION_STRING="$(kartend_appimage_version "$REPO_ROOT")"
[[ "$VERSION_STRING" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.+-][0-9A-Za-z.+-]+)?$ ]] ||
  kartend_appimage_die "the VERSION file holds '$VERSION_STRING', which is not a version"
readonly FILE_NAME="Kartend-$VERSION_STRING-x86_64.AppImage"

if [ -n "$PRINT_KEY" ]; then
  case "$PRINT_KEY" in
    base-image) echo "$KARTEND_APPIMAGE_BASE_IMAGE" ;;
    qt-version) echo "$KARTEND_APPIMAGE_QT_VERSION" ;;
    file-name) echo "$FILE_NAME" ;;
    max-glibc) echo "$KARTEND_APPIMAGE_MAX_GLIBC" ;;
    *) kartend_appimage_die "--print knows base-image, qt-version, file-name and max-glibc, not '$PRINT_KEY'" ;;
  esac
  exit 0
fi

note() { kartend_appimage_note "$@"; }
die() { kartend_appimage_die "$@"; }
step() { printf '\n%s: == %s\n' "$KARTEND_APPIMAGE_PROG" "$*" >&2; }

# A finding that stops a release build, and is only reported on a test build
# made with --allow-unsupported-base.
base_violation() {
  if ((ALLOW_UNSUPPORTED_BASE)); then
    note "warning (not a release build): $*"
  else
    die "$*"
  fi
}

is_elf() {
  [ "$(head -c 4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]
}

# version_le A B: true when version A is not newer than version B.
version_le() {
  [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$2" ]
}

# ------------------------------------------------------------------------------
step "1/9 preflight and packaging tools"

[ "$(uname -m)" = "x86_64" ] || die "the AppImage is x86_64 only; this machine is $(uname -m)"

ON_BASE=0
if kartend_appimage_on_base; then
  ON_BASE=1
elif ((ALLOW_UNSUPPORTED_BASE)); then
  note "warning: this is not $KARTEND_APPIMAGE_BASE_IMAGE; the AppImage is for testing only"
else
  die "this is not $KARTEND_APPIMAGE_BASE_IMAGE. Release AppImages are built in that image so they run on older distributions (see common.sh). For a test build on this system, pass --allow-unsupported-base."
fi

[ -d "$PREFIX" ] || die "$PREFIX does not exist; run prepare-build-env.sh first"
PREFIX="$(cd "$PREFIX" && pwd)"
# The pinned CMake that prepare-build-env.sh installed, ahead of any other.
CMAKE_BIN_DIR="$(kartend_appimage_cmake_dir "$PREFIX")/bin"
[ -x "$CMAKE_BIN_DIR/cmake" ] || die "no CMake in $CMAKE_BIN_DIR; run prepare-build-env.sh first"
export PATH="$CMAKE_BIN_DIR:$PATH"

for tool in cmake ninja curl sha256sum readelf ldd find sort awk sed od cmp diff timeout python3; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' is not installed (prepare-build-env.sh installs the base's packages)"
done

QT_DIR="$PREFIX/qt/$KARTEND_APPIMAGE_QT_VERSION/$KARTEND_APPIMAGE_QT_ARCH_DIR"
KEYCHAIN_DIR="$PREFIX/qtkeychain/$KARTEND_APPIMAGE_QTKEYCHAIN_TAG-qt$KARTEND_APPIMAGE_QT_VERSION"
[ -x "$QT_DIR/bin/qmake" ] || die "no Qt in $QT_DIR; run prepare-build-env.sh first"
[ "$("$QT_DIR/bin/qmake" -query QT_VERSION)" = "$KARTEND_APPIMAGE_QT_VERSION" ] ||
  die "$QT_DIR is not Qt $KARTEND_APPIMAGE_QT_VERSION"
[ -f "$KEYCHAIN_DIR/lib/cmake/Qt6Keychain/Qt6KeychainConfig.cmake" ] ||
  die "no QtKeychain in $KEYCHAIN_DIR; run prepare-build-env.sh first"
QT_PLUGIN_DIR="$("$QT_DIR/bin/qmake" -query QT_INSTALL_PLUGINS)"
QT_TRANSLATION_DIR="$("$QT_DIR/bin/qmake" -query QT_INSTALL_TRANSLATIONS)"
QT_LIB_DIR="$("$QT_DIR/bin/qmake" -query QT_INSTALL_LIBS)"

# The base's g++-12, unless the caller chose a compiler. On the base the
# default g++ is 11, which is older than the project's floor.
if [ -z "${CXX:-}" ] && command -v "$KARTEND_APPIMAGE_BASE_CXX" >/dev/null 2>&1; then
  export CC="$KARTEND_APPIMAGE_BASE_CC" CXX="$KARTEND_APPIMAGE_BASE_CXX"
fi

mkdir -p "$BUILD_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-$BUILD_DIR}"
TOOLS_DIR="${TOOLS_DIR:-$BUILD_DIR/tools}"
mkdir -p "$OUTPUT_DIR" "$TOOLS_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
TOOLS_DIR="$(cd "$TOOLS_DIR" && pwd)"

readonly CMAKE_DIR="$BUILD_DIR/cmake"
readonly APPDIR="$BUILD_DIR/AppDir"
readonly OUTPUT="$OUTPUT_DIR/$FILE_NAME"
readonly PARTIAL="$OUTPUT_DIR/.$FILE_NAME.partial"
readonly UNPACK_DIR="$BUILD_DIR/unpack-check"

# An older result must not be mistaken for this run's, whatever happens below.
rm -f "$OUTPUT" "$PARTIAL"
trap 'rm -f "$PARTIAL"' EXIT

# fetch_tool NAME URL SHA256: get a pinned tool into the tools directory,
# checksum-verified, and make it executable.
fetch_tool() {
  kartend_appimage_fetch "$2" "$3" "$TOOLS_DIR/$1"
  chmod +x "$TOOLS_DIR/$1"
}
fetch_tool linuxdeploy-x86_64.AppImage "$KARTEND_APPIMAGE_LINUXDEPLOY_URL" "$KARTEND_APPIMAGE_LINUXDEPLOY_SHA256"
fetch_tool appimagetool-x86_64.AppImage "$KARTEND_APPIMAGE_APPIMAGETOOL_URL" "$KARTEND_APPIMAGE_APPIMAGETOOL_SHA256"
fetch_tool runtime-x86_64 "$KARTEND_APPIMAGE_RUNTIME_URL" "$KARTEND_APPIMAGE_RUNTIME_SHA256"
readonly LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
readonly APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"
readonly RUNTIME="$TOOLS_DIR/runtime-x86_64"
# Both tools are AppImages themselves. Containers have no FUSE, so they
# unpack to a temporary directory and run from there.
export APPIMAGE_EXTRACT_AND_RUN=1

note "Kartend $VERSION_STRING, Qt $KARTEND_APPIMAGE_QT_VERSION, CMake $(cmake --version | sed -n '1s/.* //p'), compiler ${CXX:-c++ (default)}, $JOBS jobs"

# ------------------------------------------------------------------------------
step "2/9 configure and build"

# The same flag set as release.yml's .deb build. The KARTEND_ENABLE_* flags
# only allow detection; a missing -dev package falls back silently. So the
# configure output is read for the lines that say each backend was found, and
# the build stops if one is absent: an AppImage quietly missing a feature the
# .deb has would be worse than no AppImage.
launcher_args=()
if [ -n "$COMPILER_LAUNCHER" ]; then
  launcher_args=("-DCMAKE_C_COMPILER_LAUNCHER=$COMPILER_LAUNCHER" "-DCMAKE_CXX_COMPILER_LAUNCHER=$COMPILER_LAUNCHER")
fi
rm -rf "$CMAKE_DIR/CMakeCache.txt"
cmake -S "$REPO_ROOT" -B "$CMAKE_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_PREFIX_PATH="$QT_DIR;$KEYCHAIN_DIR" \
  -DKARTEND_BUILD_TESTS=OFF \
  -DKARTEND_ENABLE_CCACHE=OFF \
  -DKARTEND_MAINTENANCE=OFF \
  -DKARTEND_PORTABLE_RELEASE=ON \
  -DKARTEND_ENABLE_ZSTD=ON \
  -DKARTEND_ENABLE_LIBARCHIVE=ON \
  -DKARTEND_ENABLE_SDL2_GAMEPAD=ON \
  "${launcher_args[@]}" 2>&1 | tee "$BUILD_DIR/configure.log" >&2

for want in "using SDL2 gamepad backend" "zstd found" "libarchive found"; do
  grep -qF "$want" "$BUILD_DIR/configure.log" ||
    die "the configure output has no '$want': the AppImage would ship without that feature. Is its -dev package installed?"
done
grep -qF "Qt6Keychain_DIR:PATH=$KEYCHAIN_DIR/" "$CMAKE_DIR/CMakeCache.txt" ||
  die "CMake did not pick up QtKeychain from $KEYCHAIN_DIR: the AppImage would store scraper credentials in plain text"
grep -qF "Qt6_DIR:PATH=$QT_DIR/" "$CMAKE_DIR/CMakeCache.txt" ||
  die "CMake did not pick up Qt from $QT_DIR (it found: $(grep -E '^Qt6_DIR:PATH=' "$CMAKE_DIR/CMakeCache.txt" || echo nothing))"

cmake --build "$CMAKE_DIR" --parallel "$JOBS" >&2

# ------------------------------------------------------------------------------
step "3/9 install into the AppDir"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$CMAKE_DIR" --strip >&2

readonly DESKTOP_FILE="$APPDIR/usr/share/applications/$APP_ID.desktop"
readonly ICON_FILE="$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
for path in "$APPDIR/usr/bin/kartend" "$DESKTOP_FILE" "$ICON_FILE" "$APPDIR/usr/share/metainfo/$APP_ID.metainfo.xml"; do
  [ -s "$path" ] || die "the install step did not produce ${path#"$APPDIR"/}"
done
compgen -G "$APPDIR/usr/share/kartend/translations/kartend_*.qm" >/dev/null ||
  die "the install step produced no translations under usr/share/kartend/translations"

# ------------------------------------------------------------------------------
step "4/9 Qt plugins, translations and library bundling"

plugin_dirs=()
for plugin in "${QT_PLUGINS[@]}"; do
  [ -f "$QT_PLUGIN_DIR/$plugin" ] ||
    die "Qt $KARTEND_APPIMAGE_QT_VERSION has no plugin $plugin; the plugin list in this script needs a look"
  install -D -m 0755 "$QT_PLUGIN_DIR/$plugin" "$APPDIR/usr/plugins/$plugin"
  dir="$APPDIR/usr/plugins/${plugin%/*}"
  case " ${plugin_dirs[*]:-} " in
    *" $dir "*) ;;
    *) plugin_dirs+=("$dir") ;;
  esac
done

# Qt's own strings (dialog buttons, file pickers), one catalogue per language.
# Qt ships qt_<lang>.qm as a list of other catalogues that fails to load when
# one of them is absent, so a self-contained qt_<lang>.qm is merged from the
# two modules Kartend uses, the way windeployqt does for the Windows build.
mkdir -p "$APPDIR/usr/translations"
[ -x "$QT_DIR/bin/lconvert" ] || die "Qt's lconvert is missing from $QT_DIR/bin"
qt_languages=0
for base_qm in "$QT_TRANSLATION_DIR"/qtbase_*.qm; do
  [ -f "$base_qm" ] || continue
  lang="$(basename "$base_qm" .qm)"
  lang="${lang#qtbase_}"
  inputs=("$base_qm")
  [ -f "$QT_TRANSLATION_DIR/qtmultimedia_$lang.qm" ] && inputs+=("$QT_TRANSLATION_DIR/qtmultimedia_$lang.qm")
  "$QT_DIR/bin/lconvert" -o "$APPDIR/usr/translations/qt_$lang.qm" "${inputs[@]}" ||
    die "lconvert could not merge Qt's '$lang' translations"
  qt_languages=$((qt_languages + 1))
done
((qt_languages > 0)) || die "no Qt translations found in $QT_TRANSLATION_DIR"

# Tells the bundled Qt where its plugins and translations are, relative to
# the executable, so nothing is looked up in a Qt on the user's system.
cat >"$APPDIR/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Translations = translations
EOF

# Run by AppRun before kartend starts (linuxdeploy builds AppRun around the
# files in apprun-hooks). The AppImage carries its own Qt. Variables that
# point Qt at plugin directories of the user's system would make it try
# those first, and a platform plugin built for another Qt can crash at
# startup, so they are dropped for this process only.
mkdir -p "$APPDIR/apprun-hooks"
cat >"$APPDIR/apprun-hooks/kartend-own-qt.sh" <<'EOF'
# Kartend's AppImage uses the Qt inside it, never plugins of the system's Qt.
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH
EOF

# Libraries that are loaded at run time by name, so no file lists them as a
# dependency and linuxdeploy has to be told:
#   - OpenSSL, by Qt's TLS plugin and by FFmpeg. Bundled so HTTPS works
#     whatever OpenSSL the user's system has, or none.
#   - libsecret, by QtKeychain (GNOME Keyring and compatible services). The
#     system's copy cannot be used: it needs the system's GLib, and the
#     process already runs on the bundled one.
runtime_loaded_args=()
for soname in libssl.so.3 libcrypto.so.3 libsecret-1.so.0; do
  # No `exit` in awk: closing the pipe early would kill ldconfig with SIGPIPE,
  # which pipefail turns into a failure of this script.
  path="$(ldconfig -p 2>/dev/null | awk -v n="$soname" '!seen && $1 == n && /x86-64/ { print $NF; seen = 1 }')"
  if [ -z "$path" ] || [ ! -f "$path" ]; then
    die "$soname is not installed on this system"
  fi
  runtime_loaded_args+=(--library "$path")
done

deps_args=()
for dir in "${plugin_dirs[@]}"; do
  deps_args+=(--deploy-deps-only "$dir")
done

# linuxdeploy copies in every library the executable and the plugins need
# (leaving out the ones in its exclude list, see KARTEND_APPIMAGE_HOST_LIBRARIES
# in common.sh), points their run paths into the bundle, and puts AppRun, the
# desktop entry and the icon at the AppDir's top. LD_LIBRARY_PATH is how it finds Qt's libraries,
# which are not in a system directory. NO_STRIP: Qt's and the base's
# libraries are stripped already, kartend was stripped by the install step,
# and linuxdeploy's built-in strip is too old for some newer libraries.
LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" NO_STRIP=1 \
  "$LINUXDEPLOY" --appdir "$APPDIR" \
  --desktop-file "$DESKTOP_FILE" \
  --icon-file "$ICON_FILE" \
  "${deps_args[@]}" "${runtime_loaded_args[@]}" >&2

# linuxdeploy's AppRun sources the hooks and then executes AppRun.wrapped, a
# link to usr/bin/kartend. The kernel names the process after the file it ran,
# so Kartend would show up as "AppRun.wrapped" in process lists, its usage line
# and its X11 window class. apprun-handoff.py makes AppRun execute
# usr/bin/kartend directly under the name kartend (the usage line shows that
# name, not the temporary mount path), keeping the hooks and the arguments,
# and stops the build if AppRun is not the script it expects.
python3 "$HERE/apprun-handoff.py" "$APPDIR" ||
  die "AppRun is not the launcher linuxdeploy is expected to write; see the message above"

# ------------------------------------------------------------------------------
step "5/9 check the AppDir"

APPDIR_REAL="$(readlink -f "$APPDIR")"
for path in AppRun "$APP_ID.desktop" "$APP_ID.svg" usr/bin/kartend usr/bin/qt.conf; do
  [ -e "$APPDIR/$path" ] || die "the AppDir has no $path"
done
[ -x "$APPDIR/AppRun" ] || die "AppRun is not executable"
case "$(readlink -f "$APPDIR/AppRun")" in
  "$APPDIR_REAL"/*) ;;
  *) die "AppRun points outside the AppDir: $(readlink -f "$APPDIR/AppRun")" ;;
esac
# shellcheck disable=SC2016 # the line is matched as written, not expanded
grep -qxF 'exec -a kartend "$this_dir/usr/bin/kartend" "$@"' "$APPDIR/AppRun" ||
  die "AppRun does not start usr/bin/kartend"
for plugin in "${QT_PLUGINS[@]}"; do
  [ -s "$APPDIR/usr/plugins/$plugin" ] || die "the AppDir lost the Qt plugin $plugin"
done
for lib in "${REQUIRED_LIBRARIES[@]}"; do
  [ -s "$APPDIR/usr/lib/$lib" ] || die "the AppDir has no usr/lib/$lib"
done

# No link may leave the bundle: it would dangle on the user's system.
while IFS= read -r -d '' link; do
  target="$(readlink -f "$link" || true)"
  case "$target" in
    "$APPDIR_REAL"/*) ;;
    *) die "${link#"$APPDIR"/} links outside the AppDir (to '$(readlink "$link")')" ;;
  esac
done < <(find "$APPDIR" -type l -print0)

elf_list="$BUILD_DIR/appdir-elf-files.txt"
: >"$elf_list"
while IFS= read -r -d '' file; do
  is_elf "$file" && printf '%s\n' "$file" >>"$elf_list"
done < <(find "$APPDIR" -type f -print0)
elf_count="$(wc -l <"$elf_list")"
((elf_count > 20)) || die "only $elf_count binaries in the AppDir; the bundling step did not do its work"

# Where does every bundled file get its libraries from? Each file names the
# libraries it needs (its NEEDED entries). One that is in the bundle has to be
# found there through the file's run path; ldd shows where the loader really
# looks, and finding it on this system instead means a wrong run path that
# only works on the build machine. One that is not in the bundle is a demand
# on the user's system, and must be on the list in common.sh. Only the
# file's own entries are read, not what those libraries need in turn, so the
# answer does not depend on how this machine's system libraries are linked.
bundled_names="$BUILD_DIR/appdir-bundled-names.txt"
find "$APPDIR" \( -type f -o -type l \) -printf '%f\n' | sort -u >"$bundled_names"
problems="$BUILD_DIR/appdir-library-problems.txt"
host_needs="$BUILD_DIR/appdir-host-libraries.txt"
: >"$problems"
: >"$host_needs.raw"
while IFS= read -r file; do
  rel="${file#"$APPDIR"/}"
  needed="$(readelf --dynamic --wide "$file" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
  [ -n "$needed" ] || continue
  resolved="$(env -u LD_LIBRARY_PATH -u LD_PRELOAD ldd "$file" 2>/dev/null || true)"
  while IFS= read -r name; do
    if grep -qxF -- "$name" "$bundled_names"; then
      path="$(awk -v n="$name" '$1 == n && $2 == "=>" { print $3 }' <<<"$resolved" | sed -n '1p')"
      case "$(readlink -f "${path:-/nonexistent}")" in
        "$APPDIR_REAL"/*) ;;
        *) printf '%s loads %s from %s, not from the bundle\n' "$rel" "$name" "${path:-nowhere}" >>"$problems" ;;
      esac
    else
      printf '%s %s\n' "$name" "$rel" >>"$host_needs.raw"
    fi
  done <<<"$needed"
done <"$elf_list"

if [ -s "$problems" ]; then
  sed -n '1,30p' "$problems" >&2
  die "$(wc -l <"$problems") bundled libraries are not loaded from the bundle (first ones above)"
fi

awk '{print $1}' "$host_needs.raw" | sort -u >"$host_needs"
unexpected=""
while IFS= read -r name; do
  case " ${KARTEND_APPIMAGE_HOST_LIBRARIES[*]} " in
    *" $name "*) ;;
    *) unexpected="$unexpected $name" ;;
  esac
done <"$host_needs"
note "expected from the user's system, not bundled: $(tr '\n' ' ' <"$host_needs")"
if [ -n "$unexpected" ]; then
  for name in $unexpected; do
    note "  $name is needed by: $(awk -v n="$name" '$1 == n {print $2}' "$host_needs.raw" | sort -u | sed -n "1,5p" | tr '\n' ' ')"
  done
  base_violation "the bundle expects libraries from the user's system that are not on the expected list:$unexpected. Either bundle them or, if every desktop has them, add them to KARTEND_APPIMAGE_HOST_LIBRARIES in common.sh."
fi

# Nothing in the bundle may need a newer C or C++ runtime than the base has.
symver_list="$BUILD_DIR/appdir-symbol-versions.txt"
: >"$symver_list"
while IFS= read -r file; do
  readelf --version-info --wide "$file" 2>/dev/null |
    grep -oE '(GLIBC|GLIBCXX|CXXABI)_[0-9]+(\.[0-9]+)+' | sort -u |
    sed "s|\$| ${file#"$APPDIR"/}|" >>"$symver_list" || true
done <"$elf_list"
max_glibc="$(grep -oE '^GLIBC_[0-9.]+' "$symver_list" | sed 's/^GLIBC_//' | sort -V | tail -n 1)"
max_glibcxx="$(grep -oE '^GLIBCXX_[0-9.]+' "$symver_list" | sed 's/^GLIBCXX_//' | sort -V | tail -n 1)"
max_cxxabi="$(grep -oE '^CXXABI_[0-9.]+' "$symver_list" | sed 's/^CXXABI_//' | sort -V | tail -n 1)"
[ -n "$max_glibc" ] || die "could not read any GLIBC symbol version from the bundle"
note "newest runtime the bundle asks for: glibc $max_glibc, libstdc++ GLIBCXX_${max_glibcxx:-none} and CXXABI_${max_cxxabi:-none}"
if ! version_le "$max_glibc" "$KARTEND_APPIMAGE_MAX_GLIBC"; then
  note "  first files that need it: $(grep -E "^GLIBC_$max_glibc " "$symver_list" | awk '{print $2}' | sed -n "1,5p" | tr '\n' ' ')"
  base_violation "the bundle needs glibc $max_glibc, newer than the base's $KARTEND_APPIMAGE_MAX_GLIBC"
fi
if [ -n "$max_glibcxx" ] && ! version_le "$max_glibcxx" "$KARTEND_APPIMAGE_MAX_GLIBCXX"; then
  note "  first files that need it: $(grep -E "^GLIBCXX_$max_glibcxx " "$symver_list" | awk '{print $2}' | sed -n "1,5p" | tr '\n' ' ')"
  base_violation "the bundle needs libstdc++ GLIBCXX_$max_glibcxx, newer than the base's GLIBCXX_$KARTEND_APPIMAGE_MAX_GLIBCXX"
fi
if [ -n "$max_cxxabi" ] && ! version_le "$max_cxxabi" "$KARTEND_APPIMAGE_MAX_CXXABI"; then
  note "  first files that need it: $(grep -E "^CXXABI_$max_cxxabi " "$symver_list" | awk '{print $2}' | sed -n "1,5p" | tr '\n' ' ')"
  base_violation "the bundle needs libstdc++ CXXABI_$max_cxxabi, newer than the base's CXXABI_$KARTEND_APPIMAGE_MAX_CXXABI"
fi

# ------------------------------------------------------------------------------
step "6/9 pack the AppImage"

# --no-appstream: the metainfo file is validated by CI's script-lint job with
# a current appstreamcli; the base's is older and knows fewer tags.
# VERSION: appimagetool writes it into the desktop entry (X-AppImage-Version),
# which is where AppImage desktop tools read the version from.
ARCH=x86_64 VERSION="$VERSION_STRING" \
  "$APPIMAGETOOL" --no-appstream --runtime-file "$RUNTIME" "$APPDIR" "$PARTIAL" >&2
[ -s "$PARTIAL" ] || die "appimagetool reported success but wrote nothing"
chmod 0755 "$PARTIAL"
# appimagetool adds the .DirIcon link (the icon file managers show).
[ -e "$APPDIR/.DirIcon" ] || die "appimagetool did not add .DirIcon to the AppDir"

# ------------------------------------------------------------------------------
step "7/9 check the AppImage"

elf_header="$(readelf --file-header "$PARTIAL" 2>/dev/null)" || die "the output is not an ELF file"
grep -qE 'Class:[[:space:]]+ELF64' <<<"$elf_header" || die "the output is not a 64-bit ELF file"
grep -qE 'Machine:[[:space:]]+Advanced Micro Devices X86-64' <<<"$elf_header" || die "the output is not an x86_64 executable"
magic="$(od -An -tx1 -j 8 -N 3 "$PARTIAL" | tr -d ' \n')"
[ "$magic" = "414902" ] || die "the output has no type-2 AppImage magic at offset 8 (found $magic)"

# The payload has to start exactly where the runtime ends.
runtime_size="$(stat -c %s "$RUNTIME")"
total_size="$(stat -c %s "$PARTIAL")"
offset="$("$PARTIAL" --appimage-offset)"
[ "$offset" = "$runtime_size" ] || die "the payload starts at byte $offset, but the runtime is $runtime_size bytes"
((total_size > runtime_size + 1048576)) || die "the payload is only $((total_size - runtime_size)) bytes"

# Unpack with the AppImage's own runtime and compare with what went in: every
# file, link and permission. A truncated or damaged payload cannot pass this.
rm -rf "$UNPACK_DIR"
mkdir -p "$UNPACK_DIR"
(cd "$UNPACK_DIR" && "$PARTIAL" --appimage-extract >/dev/null 2>"$BUILD_DIR/unpack.log") || {
  tail -n 20 "$BUILD_DIR/unpack.log" >&2
  die "the AppImage could not unpack itself"
}
diff --recursive --no-dereference "$APPDIR" "$UNPACK_DIR/squashfs-root" >"$BUILD_DIR/unpack-diff.log" 2>&1 || {
  head -n 30 "$BUILD_DIR/unpack-diff.log" >&2
  die "what the AppImage unpacks to differs from the AppDir it was made from"
}
listing() { (cd "$1" && find . -printf '%P %y %m %l\n' | sort); }
diff <(listing "$APPDIR") <(listing "$UNPACK_DIR/squashfs-root") >"$BUILD_DIR/unpack-modes.log" 2>&1 || {
  head -n 30 "$BUILD_DIR/unpack-modes.log" >&2
  die "file types, permissions or links changed between the AppDir and the AppImage"
}
packed_files="$(find "$UNPACK_DIR/squashfs-root" -type f | wc -l)"
rm -rf "$UNPACK_DIR"
note "payload verified: $packed_files files, identical to the AppDir"

# ------------------------------------------------------------------------------
step "8/9 smoke test"

# The probe (probe/main.cpp) is built against the same Qt and run by the smoke
# test from inside the unpacked AppImage, where it exercises what a plain
# start does not: TLS, the image formats, media playback. It is written next
# to the AppImage so the same file can be used to test on other
# distributions; it is not part of the AppImage.
readonly PROBE="$OUTPUT_DIR/kartend-appimage-probe"
rm -f "$PROBE"
if ((SMOKE_TEST)); then
  {
    cmake -S "$HERE/probe" -B "$BUILD_DIR/probe" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" &&
      cmake --build "$BUILD_DIR/probe"
  } >"$BUILD_DIR/probe.log" 2>&1 || {
    tail -n 40 "$BUILD_DIR/probe.log" >&2
    die "the test probe did not build"
  }
  install -m 0755 "$BUILD_DIR/probe/kartend-appimage-probe" "$PROBE"
  "$HERE/smoke-test.sh" --expect-version "$VERSION_STRING" --probe "$PROBE" "$PARTIAL" >&2
else
  note "skipped (--skip-smoke-test)"
fi

mv -f "$PARTIAL" "$OUTPUT"
trap - EXIT

# ------------------------------------------------------------------------------
step "9/9 what is inside, for the SBOM"

# The release attaches a CycloneDX SBOM made by syft (anchore/sbom-action), the
# same tool as for the .deb and the Windows builds. Pointed at the AppImage
# alone, syft sees one opaque file; pointed at the AppDir, it recognises Qt
# and FFmpeg and little else, because a copied library no longer says which
# package it came from. So this step lays out a directory syft can read in
# full, and the workflow scans that:
#
#   sbom-root/AppDir/               the exact files in the AppImage
#   sbom-root/var/lib/dpkg/status   the base's package records, cut down to
#                                   the packages bundled files were copied from
#   sbom-root/etc/os-release        which distribution those packages are from
#   sbom-root/kartend-appimage.cdx.json
#                                   what no file on disk says (sbom-components.py)
readonly SBOM_ROOT="$OUTPUT_DIR/sbom-root"
rm -rf "$SBOM_ROOT"
mkdir -p "$SBOM_ROOT/var/lib/dpkg" "$SBOM_ROOT/etc"
# Hard links where the file system allows it: no second copy on disk.
cp -al "$APPDIR" "$SBOM_ROOT/AppDir" 2>/dev/null || cp -a "$APPDIR" "$SBOM_ROOT/AppDir"

packages_file="$BUILD_DIR/sbom-packages.txt"
unknown_file="$BUILD_DIR/sbom-unknown-origin.txt"
: >"$packages_file"
: >"$unknown_file"
# -s, not -r: a system can have the dpkg tools and an empty package database.
if command -v dpkg-query >/dev/null 2>&1 && [ -s /var/lib/dpkg/status ]; then
  cp /etc/os-release "$SBOM_ROOT/etc/os-release"
  while IFS= read -r -d '' lib; do
    name="$(basename "$lib")"
    # Qt's own libraries (and the FFmpeg and ICU it ships) are not from a
    # package of the base; sbom-components.py and syft's own detection cover them.
    [ -e "$QT_LIB_DIR/$name" ] && continue
    # dpkg-query exits non-zero when nothing owns the file; that is an answer
    # here (the name goes on the unknown list), not an error.
    owners="$({ dpkg-query --search "*/$name" 2>/dev/null || true; } | sed -n 's|: /.*$||p' | tr ',' '\n' | tr -d ' ' | sort -u)"
    if [ -n "$owners" ]; then
      printf '%s\n' "$owners" >>"$packages_file"
    else
      printf '%s\n' "$name" >>"$unknown_file"
    fi
  done < <(find "$APPDIR/usr/lib" -maxdepth 1 \( -type f -o -type l \) -print0)
  sort -u -o "$packages_file" "$packages_file"
  while IFS= read -r package; do
    dpkg-query --status "$package" >>"$SBOM_ROOT/var/lib/dpkg/status"
    printf '\n' >>"$SBOM_ROOT/var/lib/dpkg/status"
  done <"$packages_file"
  note "bundled system libraries come from $(wc -l <"$packages_file") packages of the base"
  if [ -s "$unknown_file" ]; then
    base_violation "no package of the base owns these bundled libraries, so the SBOM cannot say where they are from: $(tr '\n' ' ' <"$unknown_file")"
  fi
else
  note "warning: no dpkg on this system, so the SBOM input does not name the packages the system libraries came from"
fi

runtime_tag="$(sed -E 's|.*/download/([^/]+)/.*|\1|' <<<"$KARTEND_APPIMAGE_RUNTIME_URL")"
python3 "$HERE/sbom-components.py" \
  --appimage "$OUTPUT" \
  --appdir "$APPDIR" \
  --version "$VERSION_STRING" \
  --qt-version "$KARTEND_APPIMAGE_QT_VERSION" \
  --qt-lib-dir "$QT_LIB_DIR" \
  --qtkeychain-tag "$KARTEND_APPIMAGE_QTKEYCHAIN_TAG" \
  --qtkeychain-commit "$KARTEND_APPIMAGE_QTKEYCHAIN_COMMIT" \
  --runtime-tag "$runtime_tag" \
  --runtime-sha256 "$KARTEND_APPIMAGE_RUNTIME_SHA256" \
  --output "$SBOM_ROOT/kartend-appimage.cdx.json"

size_mib="$(awk -v b="$total_size" 'BEGIN { printf "%.1f", b / 1048576 }')"
note "done: $OUTPUT ($size_mib MiB, $elf_count bundled binaries, needs glibc $max_glibc or newer)"
if ((!ON_BASE)); then
  note "NOT a release build: made outside $KARTEND_APPIMAGE_BASE_IMAGE"
fi
printf '%s\n' "$OUTPUT"
