# kartend-appimage-probe: a test helper for the AppImage, see main.cpp.
#
# Built by packaging/appimage/build-appimage.sh against the Qt the AppImage
# bundles. Not part of Kartend's own build and not installed anywhere.
cmake_minimum_required(VERSION 3.20)
project(kartend-appimage-probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Sql Network Multimedia)

add_executable(kartend-appimage-probe main.cpp)
target_link_libraries(kartend-appimage-probe PRIVATE
  Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network Qt6::Multimedia)

# smoke-test.sh copies the probe into the unpacked AppImage's usr/bin, next
# to kartend. From there the bundle's libraries are one directory up, and the
# run path is all that may lead to them: nothing of the build machine's Qt.
set_target_properties(kartend-appimage-probe PROPERTIES
  BUILD_WITH_INSTALL_RPATH TRUE
  INSTALL_RPATH "\$ORIGIN/../lib"
)
