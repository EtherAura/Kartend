# items/ and media/ moved to src/widgets/ (the neutral widget layer)
# under Kartend-jvib, so src/ui/widgets/ now only holds the chrome that
# legitimately belongs in the UI layer.
add_subdirectory(overlays)
add_subdirectory(panes)
