target_sources(kartend_ui PRIVATE
  commandpalettedialog.cpp
  errordialog.cpp
  firstrunwizard.cpp
  shortcutsdialog.cpp
  statisticsdialog.cpp
  commandpalettedialog.h
  errordialog.h
  firstrunwizard.h
  shortcutsdialog.h
  statisticsdialog.h
)

add_subdirectory(settings)
add_subdirectory(collection)
add_subdirectory(item)
add_subdirectory(launcher)
add_subdirectory(scraper)
add_subdirectory(kart)
