# items/ and media/ moved to src/chrome/ (the neutral chrome layer)
# under Kartend-jvib, so src/ui/widgets/ now only holds the UI-coupled
# widgets that legitimately belong in the UI layer.
target_sources(kartend_ui PRIVATE
  formbuilders.cpp
  formbuilders.h
  pathstatusglyph.cpp
  pathstatusglyph.h
)
add_subdirectory(overlays)
add_subdirectory(panes)
