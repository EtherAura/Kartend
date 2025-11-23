cmake_minimum_required(VERSION 3.20)

project(kartend
  VERSION 0.0.1
  LANGUAGES CXX
)

option(MAINTENANCE "Maintenance build mode (-Werror on)" OFF)
option(USE_PGO "Enable Profile-Guided Optimization" OFF)
option(PGO_GENERATE "Generate PGO profile data" OFF)
option(PGO_USE "Use existing PGO profile data" OFF)
option(PGO_PROFILE_DIR "Directory for PGO profile data" "${CMAKE_BINARY_DIR}/pgo_profiles")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

# Ensure UIC can find .ui files referenced as ui_*.h
set(CMAKE_AUTOUIC_SEARCH_PATHS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets
)

# Prefer Qt6, fallback to Qt5
set(QT_REQUIRED_COMPONENTS Core Gui Widgets Sql Concurrent)
find_package(Qt6 COMPONENTS ${QT_REQUIRED_COMPONENTS} QUIET)
if (NOT Qt6_FOUND)
  find_package(Qt5 COMPONENTS ${QT_REQUIRED_COMPONENTS} REQUIRED)
endif()
if (Qt6_FOUND)
  set(QTNS Qt6)
else()
  set(QTNS Qt5)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT DEFINED BUILD_DATE)
  set(BUILD_DATE "unknown")
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SOURCES
  ${SRC_DIR}/core/main.cpp
  ${SRC_DIR}/core/mainwindow.cpp
  ${SRC_DIR}/managers/artworkmanager.cpp
  ${SRC_DIR}/managers/cachemanager.cpp
  ${SRC_DIR}/managers/databasemanager.cpp
  ${SRC_DIR}/managers/databaseworker.cpp
  ${SRC_DIR}/managers/navigationmanager.cpp
  ${SRC_DIR}/managers/settingsmanager.cpp
  ${SRC_DIR}/managers/sidebarmanager.cpp
  ${SRC_DIR}/managers/sessionmanager.cpp
  ${SRC_DIR}/managers/scrollmanager.cpp
  ${SRC_DIR}/managers/interactionmanager.cpp
  ${SRC_DIR}/ui/dialogs/settingsdialog.cpp
  ${SRC_DIR}/ui/widgets/metadatasidebar.cpp
  ${SRC_DIR}/ui/widgets/itemwidget.cpp
  ${SRC_DIR}/utils/pathutils.cpp
  ${SRC_DIR}/utils/extensionutils.cpp
  ${SRC_DIR}/utils/timerutils.cpp
)

set(HEADERS
  ${SRC_DIR}/core/mainwindow.h
  ${SRC_DIR}/managers/artworkmanager.h
  ${SRC_DIR}/managers/cachemanager.h
  ${SRC_DIR}/managers/databasemanager.h
  ${SRC_DIR}/managers/databaseworker.h
  ${SRC_DIR}/managers/navigationmanager.h
  ${SRC_DIR}/managers/settingsmanager.h
  ${SRC_DIR}/managers/sidebarmanager.h
  ${SRC_DIR}/managers/sessionmanager.h
  ${SRC_DIR}/managers/scrollmanager.h
  ${SRC_DIR}/managers/interactionmanager.h
  ${SRC_DIR}/ui/uiconstants.h
  ${SRC_DIR}/ui/dialogs/settingsdialog.h
  ${SRC_DIR}/ui/widgets/metadatasidebar.h
  ${SRC_DIR}/ui/widgets/itemwidget.h
  ${SRC_DIR}/utils/collectionconfig.h
  ${SRC_DIR}/utils/pathutils.h
  ${SRC_DIR}/utils/gridutils.h
  ${SRC_DIR}/utils/extensionutils.h
  ${SRC_DIR}/utils/timerutils.h
  ${SRC_DIR}/utils/propertyutils.h
)

set(UI_FILES
  ${SRC_DIR}/core/mainwindow.ui
  ${SRC_DIR}/ui/dialogs/settingsdialog.ui
  ${SRC_DIR}/ui/widgets/metadatasidebar.ui
  ${SRC_DIR}/ui/widgets/itemwidget.ui
)

set(RESOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/resources.qrc
)

# Generate UI headers from .ui files
qt6_wrap_ui(UI_HEADERS ${UI_FILES})

add_executable(kartend)
target_sources(kartend PRIVATE
  ${SOURCES}
  ${HEADERS}
  ${UI_HEADERS}
  ${RESOURCES}
)

target_link_libraries(kartend
  PRIVATE
    ${QTNS}::Core
    ${QTNS}::Gui
    ${QTNS}::Widgets
    ${QTNS}::Sql
    ${QTNS}::Concurrent
)

target_compile_definitions(kartend
  PRIVATE
    APP_NAME="Kartend"
    APP_VERSION="${PROJECT_VERSION}"
    APP_AUTHOR="EtherAura"
    BUILD_DATE="${BUILD_DATE}"
)

target_include_directories(kartend PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${SRC_DIR}
  ${SRC_DIR}/core
  ${SRC_DIR}/managers
  ${SRC_DIR}/ui
  ${SRC_DIR}/ui/widgets
  ${SRC_DIR}/ui/dialogs
  ${SRC_DIR}/utils
)

target_compile_options(kartend PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-unused-lambda-capture)

# Keep GCC-only intent with Clang analogue on RISC-V
if (CMAKE_SYSTEM_PROCESSOR MATCHES "[Rr][Ii][Ss][Cc][Vv]")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(kartend PRIVATE -mno-direct-extern-access)
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(kartend PRIVATE -fno-direct-access-external-data)
  endif()
endif()

# Release flags
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(RELEASE_COMPILE_OPTS -O3 -ffast-math -march=native -flto=auto -fomit-frame-pointer -funroll-loops -fno-signed-zeros -fno-trapping-math -fno-math-errno -fno-semantic-interposition -ftree-vectorize -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -DQT_NO_DEBUG -DQT_NO_DEBUG_OUTPUT -fmerge-all-constants -fdelete-null-pointer-checks)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(RELEASE_COMPILE_OPTS -O3 -ffast-math -march=native -flto=auto -fomit-frame-pointer -funroll-loops -fno-signed-zeros -fno-trapping-math -fno-math-errno -fno-semantic-interposition -ftree-vectorize -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -DQT_NO_DEBUG -DQT_NO_DEBUG_OUTPUT -fmerge-all-constants -fdelete-null-pointer-checks)
else()
  set(RELEASE_COMPILE_OPTS -O3 -ffast-math -march=native -flto=auto -fomit-frame-pointer -funroll-loops -fno-signed-zeros -fno-trapping-math -fno-math-errno -fno-semantic-interposition -ftree-vectorize -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -DQT_NO_DEBUG -DQT_NO_DEBUG_OUTPUT -fmerge-all-constants -fdelete-null-pointer-checks)
endif()
set(RELEASE_LINK_OPTS -fuse-ld=lld -flto=auto -Wl,-O3 -Wl,--gc-sections -Wl,--as-needed -Wl,-z,now -Wl,-z,relro -pie)
target_compile_options(kartend PRIVATE $<$<CONFIG:Release>:${RELEASE_COMPILE_OPTS}>)
target_link_options(kartend PRIVATE $<$<CONFIG:Release>:${RELEASE_LINK_OPTS}>)

# PGO configuration
if(USE_PGO)
    if(PGO_GENERATE)
        message(STATUS "PGO: Generating profile data in ${PGO_PROFILE_DIR}")
        list(APPEND RELEASE_COMPILE_OPTS -fprofile-generate=${PGO_PROFILE_DIR})
        list(APPEND RELEASE_LINK_OPTS -fprofile-generate=${PGO_PROFILE_DIR})
    elseif(PGO_USE)
        message(STATUS "PGO: Using profile data from ${PGO_PROFILE_DIR}")
        list(APPEND RELEASE_COMPILE_OPTS -fprofile-use=${PGO_PROFILE_DIR} -fprofile-correction)
        list(APPEND RELEASE_LINK_OPTS -fprofile-use=${PGO_PROFILE_DIR})
    endif()
endif()

# Strip in Release unless MAINTENANCE
if (NOT MAINTENANCE)
  target_link_options(kartend PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_compile_definitions(kartend PRIVATE $<$<CONFIG:Release>:NDEBUG>)

# Debug-only linker map file output (fixed name)
target_link_options(kartend PRIVATE
  $<$<CONFIG:Debug>:-Wl,-Map,${CMAKE_BINARY_DIR}/../../../.backups/reports/kartend.map>
)

# Install (Linux)
if (UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS kartend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES assets/desktop/kartend.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES assets/icons/kartend.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps)
endif()

# MAINTENANCE adds -Werror
if (MAINTENANCE)
  target_compile_options(kartend PRIVATE -Werror)
endif()