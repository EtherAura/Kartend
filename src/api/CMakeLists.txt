# src/api/ — neutral role-interface layer.
#
# These abstract manager interfaces (i*.h) carry no translation units of
# their own; AUTOMOC produces moc_*.cpp for each QObject-based interface
# (IScrollManager today, possibly more later) and those go into kartend_api's
# object files. They depend only on Qt headers, src/utils/ headers, and
# forward declarations, so any module may #include them without creating a
# module->module library edge.
target_sources(kartend_api PRIVATE
  ianimationmanager.h
  iartworkmanager.h
  iartworkpreviewscroll.h
  icachemanager.h
  idatabasemanager.h
  idetailpagemanager.h
  idetailpageoverlay.h
  idetailspane.h
  idetailspanemanager.h
  ifiltermanager.h
  igridlayoutscroll.h
  iinteractionmanager.h
  ikeyboardmanager.h
  imainwindow.h
  imousemanager.h
  inavigationmanager.h
  iplaylistmanager.h
  iscrolldatasource.h
  iscrollmanager.h
  isearchstatescroll.h
  iselectionmanager.h
  iselectionoverlayscroll.h
  isessionmanager.h
  isettingsdialog.h
  isettingsmanager.h
  ivirtualscrolllifecycle.h
  iviewportmanager.h
)
