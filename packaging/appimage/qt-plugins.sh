#!/usr/bin/env bash
# The Qt plugins that go into the AppImage, and the two steps that handle
# them: copying them into the AppDir and checking they are still there after
# bundling. Sourced by build-appimage.sh after common.sh; not run.
# shellcheck shell=bash
# shellcheck disable=SC2034  # the variables are read by the scripts that source this file

# Qt plugins that go into the AppImage, relative to Qt's plugins directory.
# A fixed list, not "whatever the Qt install has": the official Qt build also
# ships plugins Kartend has no use for and whose own dependencies are not on
# the base (GStreamer, MySQL, PostgreSQL, ODBC, GTK), and a list that is
# spelled out is one a Qt update cannot silently change. A file missing from
# the Qt install fails the build.
readonly KARTEND_APPIMAGE_QT_PLUGINS=(
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
  # The XDG desktop portal theme (needs only QtDBus, which is bundled). The
  # AppRun hook asks for it when the user has not set QT_QPA_PLATFORMTHEME, so
  # file dialogs go through the desktop portal; a theme the user set wins.
  platformthemes/libqxdgdesktopportal.so
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

# kartend_appimage_stage_qt_plugins PLUGIN_ROOT APPDIR: copy every plugin on
# the list from PLUGIN_ROOT (the pinned Qt's plugins directory, and only that
# one) to APPDIR/usr/plugins, at the same relative path. Stops at the first
# plugin PLUGIN_ROOT does not have; nothing is looked up anywhere else. Sets
# KARTEND_APPIMAGE_QT_PLUGIN_DIRS to the AppDir directories it filled, for
# linuxdeploy.
kartend_appimage_stage_qt_plugins() {
  local root=$1 appdir=$2 plugin dir
  [ -d "$root" ] || kartend_appimage_die "the Qt plugin directory $root does not exist"
  KARTEND_APPIMAGE_QT_PLUGIN_DIRS=()
  for plugin in "${KARTEND_APPIMAGE_QT_PLUGINS[@]}"; do
    [ -f "$root/$plugin" ] ||
      kartend_appimage_die "Qt ${KARTEND_APPIMAGE_QT_VERSION:-} has no plugin $plugin in $root; the plugin list in qt-plugins.sh needs a look"
    install -D -m 0755 "$root/$plugin" "$appdir/usr/plugins/$plugin"
    dir="$appdir/usr/plugins/${plugin%/*}"
    case " ${KARTEND_APPIMAGE_QT_PLUGIN_DIRS[*]:-} " in
      *" $dir "*) ;;
      *) KARTEND_APPIMAGE_QT_PLUGIN_DIRS+=("$dir") ;;
    esac
  done
}

# kartend_appimage_check_qt_plugins APPDIR: stop unless every plugin on the
# list is a non-empty file in APPDIR/usr/plugins.
kartend_appimage_check_qt_plugins() {
  local appdir=$1 plugin
  for plugin in "${KARTEND_APPIMAGE_QT_PLUGINS[@]}"; do
    if [ ! -f "$appdir/usr/plugins/$plugin" ] || [ ! -s "$appdir/usr/plugins/$plugin" ]; then
      kartend_appimage_die "the AppDir lost the Qt plugin $plugin"
    fi
  done
}
