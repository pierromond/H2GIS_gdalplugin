# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.10)
project(gdal_h2gis_driver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Option for portable binary (statically link libstdc++) - Linux/GCC only
option(PORTABLE_BUILD "Build portable binary with static libstdc++" OFF)

if(PORTABLE_BUILD AND NOT WIN32 AND NOT APPLE)
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
# GDAL requires plugins to be named "gdal_X" with platform-specific extension
add_library(gdal_H2GIS MODULE ${DRIVER_SOURCES} ${DRIVER_HEADERS})

# Remove 'lib' prefix - GDAL expects "gdal_H2GIS.so" not "libgdal_H2GIS.so"
set_target_properties(gdal_H2GIS PROPERTIES PREFIX "")

# Platform-specific output suffix for the plugin
if(WIN32)
    set_target_properties(gdal_H2GIS PROPERTIES SUFFIX ".dll")
elseif(APPLE)
    set_target_properties(gdal_H2GIS PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(gdal_H2GIS PROPERTIES SUFFIX ".so")
endif()

# DO NOT link directly to libh2gis - we use dlopen/LoadLibrary at runtime
# in a thread with large stack to avoid StackOverflowError during GraalVM init.
# The H2GIS library is loaded via H2GIS_LIBRARY or H2GIS_NATIVE_LIB env var at runtime.

# Platform-specific library name (for informational message only)
if(WIN32)
    set(H2GIS_LIB_NAME "h2gis.dll")
elseif(APPLE)
    set(H2GIS_LIB_NAME "libh2gis.dylib")
else()
    set(H2GIS_LIB_NAME "libh2gis.so")
endif()

# Check if library exists (optional - just for user feedback)
set(H2GIS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/${H2GIS_LIB_NAME}")
if(EXISTS ${H2GIS_LIB})
    message(STATUS "Found H2GIS Library: ${H2GIS_LIB} (loaded via dlopen at runtime)")
else()
    message(STATUS "H2GIS Library not found at ${H2GIS_LIB}")
    message(STATUS "  -> Set H2GIS_LIBRARY or H2GIS_NATIVE_LIB environment variable at runtime")
    message(STATUS "  -> Or install h2gis Python package: pip install h2gis")
endif()

# Link against GDAL and platform-specific dynamic loading library
if(WIN32)
    # Windows uses LoadLibrary/GetProcAddress from kernel32 (implicit)
    target_link_libraries(gdal_H2GIS ${GDAL_LIBRARY})
else()
    # Unix uses dlopen/dlsym from libdl
    target_link_libraries(gdal_H2GIS ${GDAL_LIBRARY} ${CMAKE_DL_LIBS})
endif()

# Installation - platform-specific paths
if(WIN32)
    install(TARGETS gdal_H2GIS DESTINATION bin/gdalplugins)
else()
    install(TARGETS gdal_H2GIS DESTINATION lib/gdalplugins)
endif()
