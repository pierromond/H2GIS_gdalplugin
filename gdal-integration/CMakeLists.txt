# GDAL H2GIS Driver - GDAL In-tree CMakeLists.txt
#
# This file is for integration into the GDAL source tree.
# Place this directory at: gdal/ogr/ogrsf_frmts/h2gis/
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_gdal_driver(
  TARGET ogr_H2GIS
  SOURCES
      ogr_h2gis.h
      ogrh2gisdatasource.cpp
      ogrh2gisdriver.cpp
      ogrh2gislayer.cpp
      h2gis_wrapper.cpp
      h2gis_wrapper.h
  PLUGIN_CAPABLE
  NO_DEPS
  NO_CXX_WFLAGS_EFFCXX
  NO_WFLAG_OLD_STYLE_CAST
)

if(NOT TARGET ogr_H2GIS)
    return()
endif()

gdal_standard_includes(ogr_H2GIS)

# H2GIS uses dlopen to load libh2gis.so at runtime
# No external library link required at build time
target_link_libraries(ogr_H2GIS PRIVATE ${CMAKE_DL_LIBS})

# Platform-specific threading support
if(CMAKE_USE_PTHREADS_INIT)
    target_link_libraries(ogr_H2GIS PRIVATE Threads::Threads)
endif()
