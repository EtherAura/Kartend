# Install rules (Linux) + CPack / Debian packaging config.
#
# Extracted verbatim from the root CMakeLists.txt (Kartend-9fdsd). Behaviour-
# preserving: include()d once near the end of the root in the same directory
# scope, so PROJECT_VERSION, KARTEND_APP_ID, KARTEND_TRANSLATION_QM_FILES,
# KARTEND_TRANSLATION_INSTALL_SUBDIR, and the kartend target are all in scope
# exactly as before. include(CPack) MUST stay last (it reads the CPACK_*
# variables set above it), so the whole packaging block is kept together here.

# Install (Linux)
if (UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS kartend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/${KARTEND_APP_ID}.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES src/assets/icon.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
          RENAME ${KARTEND_APP_ID}.svg)
  install(FILES packaging/${KARTEND_APP_ID}.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
  install(FILES ${KARTEND_TRANSLATION_QM_FILES}
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${KARTEND_TRANSLATION_INSTALL_SUBDIR})
endif()

# CPack: enables `cpack -G TGZ` for binary tarballs and `cpack --config
# CPackSourceConfig.cmake -G TGZ` for source tarballs. The tag-triggered
# release workflow uses `git archive` directly, so CPack is here for
# ad-hoc prerelease bundling and downstream packaging assistance.
set(CPACK_PACKAGE_NAME "kartend")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "EtherAura")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Qt6 KDE frontend for organizing and launching multimedia collections")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/EtherAura/Kartend")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/readme.md")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "Kartend-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
  "/\\\\.git/"
  "/\\\\.beads/"
  "/\\\\.cache/"
  "/\\\\.claude/"
  "/\\\\.obsidian/"
  "/\\\\.vscode/"
  "/build/"
  "/Testing/"
  "/\\\\.backups/"
  "\\\\.dolt/"
  "\\\\.swp$"
  "~$"
)

# Debian (.deb) generator config. Selected at pack time with `cpack -G DEB`.
# Exercised by .scripts/test-install-ubuntu.sh, which builds a .deb here and
# then verifies it installs cleanly in a fresh ubuntu:24.04 container.
# SHLIBDEPS=ON walks the installed ELF with `dpkg-shlibdeps` to discover the
# exact runtime libQt6Core/libSDL2/etc package names — saves us from chasing
# the libqt6*6t64 renames that landed in noble's time_t transition.
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "EtherAura <https://github.com/EtherAura>")
set(CPACK_DEBIAN_PACKAGE_SECTION "kde")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
