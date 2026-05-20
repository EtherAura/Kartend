# Neutral widget layer — chrome widgets shared between input/media/core/ui
# without those modules taking an upward dep on src/ui/. Sits below the
# modules layer in the conceptual order
#   utils <- widgets <- data <- {input, media} <- behavior <- core/ui
# so callers in src/modules/* and src/ui/* can both reach these widgets
# without input/media -> ui/ edges.
#
# These widgets are deliberately "dumb": they take pixmaps / strings /
# rects and emit Qt-level signals. Anything that needs a manager or a
# settings lookup happens above this layer (in the input/media modules,
# typically the *Manager that drives the widget).
#
# Today everything still links into the single KARTEND_LIB_TARGET OBJECT
# library; the per-module library split (Kartend-hchk) will land later
# and turn this directory into its own STATIC library with
# target_link_libraries declaring the allowed upward edges.
add_subdirectory(items)
add_subdirectory(media)
add_subdirectory(overlays)
