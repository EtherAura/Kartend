# src/api/ — neutral header-only role-interface layer.
#
# These abstract manager interfaces (i*.h) carry no translation units. They
# depend only on Qt headers, src/utils/ headers, and forward declarations, so
# any module may #include them without creating a module->module library edge.
# Listed under target_sources purely so IDEs and the build graph see them.
target_sources(${KARTEND_LIB_TARGET} PRIVATE
  ianimationmanager.h
  iartworkmanager.h
  icachemanager.h
  idatabasemanager.h
  idetailpagemanager.h
  idetailpageoverlay.h
  idetailspanemanager.h
  iinteractionmanager.h
  ikeyboardmanager.h
  imainwindow.h
  imousemanager.h
  inavigationmanager.h
  iplaylistmanager.h
  iscrollmanager.h
  iselectionmanager.h
  isessionmanager.h
  isettingsdialog.h
  isettingsmanager.h
  iviewportmanager.h
)
