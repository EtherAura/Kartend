target_sources(kartend_ui PRIVATE
  errordialog.cpp
  firstrunwizard.cpp
  shortcutsdialog.cpp
  statisticsdialog.cpp
  errordialog.h
  firstrunwizard.h
  shortcutsdialog.h
  statisticsdialog.h
)

add_subdirectory(settings)
add_subdirectory(collection)
add_subdirectory(launcher)
add_subdirectory(scraper)
add_subdirectory(kart)
