cmake_minimum_required(VERSION 3.10)
project(gdal_h2gis_driver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option for portable binary (statically link libstdc++)
option(PORTABLE_BUILD "Build portable binary with static libstdc++" OFF)

if(PORTABLE_BUILD)
    message(STATUS "Building PORTABLE binary (static libstdc++)")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
endif()

# Find GDAL
find_package(GDAL REQUIRED)

message(STATUS "GDAL Include Dir: ${GDAL_INCLUDE_DIR}")
message(STATUS "GDAL Library: ${GDAL_LIBRARY}")

include_directories(${GDAL_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Sources
set(DRIVER_SOURCES
    ogrh2gisdriver.cpp
    ogrh2gisdatasource.cpp
    ogrh2gislayer.cpp
    h2gis_wrapper.cpp
)

# Headers
set(DRIVER_HEADERS
    ogr_h2gis.h
    h2gis_wrapper.h
)

# Build as a MODULE (Plugin)
# GDAL requires plugins to be named "gdal_X.so" and have GDALRegister_X() function
add_library(gdal_H2GIS MODULE ${DRIVER_SOURCES} ${DRIVER_HEADERS})

# Remove 'lib' prefix - GDAL expects "gdal_H2GIS.so" not "libgdal_H2GIS.so"
set_target_properties(gdal_H2GIS PROPERTIES PREFIX "")

# DO NOT link directly to libh2gis.so - we use dlopen at runtime in a thread with large stack
# to avoid StackOverflowError during GraalVM initialization in worker threads.
# Link only against GDAL and dl
set(H2GIS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/libh2gis.so")

if (EXISTS ${H2GIS_LIB})
    message(STATUS "Found H2GIS Library: ${H2GIS_LIB} (NOT linking directly, using dlopen)")
    # Only link GDAL and dl
    target_link_libraries(gdal_H2GIS ${GDAL_LIBRARY} ${CMAKE_DL_LIBS})
else()
    message(FATAL_ERROR "libh2gis.so not found at ${H2GIS_LIB}! Please copy from h2gis-graalvm/target/h2gis-graalvm.so")
endif()

# Installation
install(TARGETS gdal_H2GIS DESTINATION lib/gdalplugins)
