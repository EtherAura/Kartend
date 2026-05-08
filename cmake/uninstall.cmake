# Uninstall helper. Reads install_manifest.txt (produced by `cmake --install`)
# and removes every listed file. Honors DESTDIR. Empty parent directories are
# left alone — distros stage installs under their own DESTDIR and the manifest
# itself is not part of the package payload, so removing dirs is unsafe.

set(_manifest "${CMAKE_BINARY_DIR}/install_manifest.txt")

if (NOT EXISTS "${_manifest}")
  message(FATAL_ERROR
    "install_manifest.txt not found at ${_manifest}. Run `cmake --install` "
    "first, or build a target that triggered an install.")
endif()

file(STRINGS "${_manifest}" _files)

set(_destdir "$ENV{DESTDIR}")

foreach (_f IN LISTS _files)
  if (_f STREQUAL "")
    continue()
  endif()
  set(_target "${_destdir}${_f}")
  if (EXISTS "${_target}" OR IS_SYMLINK "${_target}")
    message(STATUS "Uninstall: ${_target}")
    file(REMOVE "${_target}")
    if (EXISTS "${_target}")
      message(WARNING "Failed to remove ${_target}")
    endif()
  else()
    message(STATUS "Uninstall: missing (skipping) ${_target}")
  endif()
endforeach()
