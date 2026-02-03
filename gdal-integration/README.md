# GDAL H2GIS Driver Integration Guide

This directory contains the CMake files needed to integrate the H2GIS driver
into the GDAL source tree for an official PR.

## Files

- `CMakeLists.txt` - Main build configuration following GDAL conventions
- `driver_declaration.cmake` - Driver declaration macro for registration

## Integration Steps

### 1. Copy driver source files

Copy the driver files to `gdal/ogr/ogrsf_frmts/h2gis/`:

```bash
cd gdal/ogr/ogrsf_frmts
mkdir h2gis
cp /path/to/gdal-h2gis-driver/*.cpp h2gis/
cp /path/to/gdal-h2gis-driver/*.h h2gis/
cp /path/to/gdal-h2gis-driver/gdal-integration/CMakeLists.txt h2gis/
cp /path/to/gdal-h2gis-driver/gdal-integration/driver_declaration.cmake h2gis/
```

### 2. Register the driver

Edit `gdal/ogr/ogrsf_frmts/CMakeLists.txt` and add:

```cmake
# After other ogr_optional_driver declarations
include(h2gis/driver_declaration.cmake)
```

### 3. Copy documentation

```bash
cp /path/to/gdal-h2gis-driver/doc/source/drivers/vector/h2gis.rst \
   gdal/doc/source/drivers/vector/
```

Update `gdal/doc/source/drivers/vector/index.rst` to include h2gis in the list.

### 4. Register in driver list

Edit `gdal/ogr/ogrsf_frmts/generic/ogrregisterall.cpp` and add:

```cpp
#ifdef H2GIS_ENABLED
    RegisterOGRH2GIS();
#endif
```

### 5. Build and test

```bash
mkdir build && cd build
cmake .. -DOGR_ENABLE_DRIVER_H2GIS=ON
cmake --build .
ctest -R ogr_h2gis
```

## Driver Characteristics

- **Plugin capable**: YES (NO_DEPS - uses dlopen for libh2gis.so)
- **External dependency**: libh2gis.so (loaded at runtime via dlopen)
- **License**: GPL-3.0-or-later (compatible with GDAL MIT for plugins)

## Notes

The H2GIS driver uses `dlopen()` to load the H2GIS native library at runtime,
which means no build-time dependency on H2GIS libraries is required.
This makes it ideal as an optional plugin driver.

The libh2gis library is a GraalVM Native Image that provides a C interface
to the H2GIS Java spatial database functionality.

## Obtaining the Native Library

The easiest way to get the native libraries for all platforms is via PyPI:

```bash
pip install h2gis
```

The libraries are located in the `h2gis/lib/` directory:
- `h2gis.so` - Linux
- `h2gis.dll` - Windows
- `h2gis.dylib` - macOS

Alternatively, set the `H2GIS_NATIVE_LIB` environment variable to point to the library.
