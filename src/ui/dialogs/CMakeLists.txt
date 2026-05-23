target_sources(kartend_ui PRIVATE
  bindingvisualizerdialog.cpp
  commandpalettedialog.cpp
  errordialog.cpp
  firstrunwizard.cpp
  libraryonboardingwizard.cpp
  shortcutsdialog.cpp
  statisticsdialog.cpp
  bindingvisualizerdialog.h
  commandpalettedialog.h
  errordialog.h
  firstrunwizard.h
  libraryonboardingwizard.h
  shortcutsdialog.h
  statisticsdialog.h
)

add_subdirectory(settings)
add_subdirectory(collection)
add_subdirectory(item)
add_subdirectory(launcher)
add_subdirectory(scraper)
add_subdirectory(kart)
