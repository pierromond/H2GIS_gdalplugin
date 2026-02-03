# GDAL/OGR H2GIS Driver Development Instructions

## Project Status & PR Roadmap

### Current Status (February 2026)
- ✅ **SPDX Headers**: All source files have SPDX-License-Identifier: GPL-3.0-or-later
- ✅ **GDAL 3.10 Compatibility**: ICreateLayer, SetIgnoredFields, CreateField with version guards
- ✅ **SetAttributeFilter Push-down**: Implemented with spatial+attribute filter combination
- ✅ **Code Cleanup**: iostream removed, CPLDebug/CPLError only, no hardcoded paths
- ✅ **Metadata**: GDAL_DCAP_MULTIPLE_VECTOR_LAYERS and other capabilities declared
- ✅ **Tests**: pytest framework with markers and filter push-down tests
- ✅ **RST Documentation**: Created `doc/source/drivers/vector/h2gis.rst`
- ✅ **README Translation**: Translated to English for international audience
- ✅ **Cross-Platform Support**: Windows/macOS platform abstraction layer implemented
- ✅ **GitHub Actions CI**: Created `.github/workflows/ci.yml` for Linux/Windows/macOS

### Cross-Platform Support (Implemented)
**Status**: ✅ Complete

The `h2gis_wrapper.cpp` now includes a platform abstraction layer:

| Component | Linux | Windows | macOS |
|-----------|-------|---------|-------|
| Library loading | `dlopen/dlsym` (dlfcn.h) | `LoadLibraryA/GetProcAddress` | `dlopen/dlsym` |
| Threading | `pthread_create` | `CreateThread` | `pthread_create` |
| Sleep | `usleep` | `Sleep` | `usleep` |
| File check | `access` | `_access` | `access` |
| Library ext | `.so` | `.dll` | `.dylib` |

**Implemented Abstraction Functions**:
- `h2gis_load_library()`, `h2gis_get_symbol()`, `h2gis_free_library()`
- `h2gis_get_load_error()` - Windows uses FormatMessage, Unix uses dlerror
- `h2gis_create_thread_with_stack()`, `h2gis_join_thread()`
- `h2gis_file_exists()`, `h2gis_sleep_ms()`
- `h2gis_get_library_fallback_paths()` - Platform-specific default paths

**Note**: CPL threading (`CPLCreateThread`) does NOT support custom stack size (64MB required for GraalVM), so native thread APIs must be used.

### Next Steps
- 🔲 **CMakeLists.txt Update**: Add Windows/macOS specific configurations
- 🔲 **Verify CI**: Push and verify GitHub Actions builds pass on all platforms
- 🔲 **Integration Testing**: Test with QGIS on Windows/macOS

### GDAL CMake Integration (Ready)
The `gdal-integration/` directory contains files for GDAL source tree integration:
- `CMakeLists.txt` - Uses `add_gdal_driver()` with PLUGIN_CAPABLE NO_DEPS
- `driver_declaration.cmake` - `ogr_optional_driver(h2gis ...)`
- `README.md` - Step-by-step integration guide

### libh2gis Dependency
- **License**: LGPL-3.0 (compatible with GDAL)
- **Source**: Open source, available via PyPI: `pip install h2gis`
- **Technology**: GraalVM Native Image
- **Cross-platform**: Linux (.so), Windows (.dll), macOS (.dylib) included in PyPI package
- **Location in PyPI package**: `h2gis/lib/` directory

## General Guidelines
- Follow the **GDAL OGR Driver Implementation Guidelines**.
- Use the `OGRH2GIS` prefix for all classes.
- File naming convention: `ogr_h2gis.h`, `ogrh2gis*.cpp`.
- Ensure all pointer arguments are checked for `nullptr` before use if not guaranteed by caller.
- Use `CPLDebug()` for debugging information.
- Use `CPLError()` for error reporting.
- **IMPORTANT**: The driver requires file paths to end with `.mv.db` (H2 v2 format) for correct identification.
- **Target**: GDAL 3.4+ with full compatibility up to GDAL 3.10+.
- **SPDX Header Required**: All files must have `SPDX-License-Identifier: GPL-3.0-or-later`

## GDAL API Version Compatibility (Critical)
- Use `#if GDAL_VERSION_NUM >= XXXXX` for API changes between versions:
    - `>= 3090000`: `SetIgnoredFields(const char* const*)`, `CreateField(const OGRFieldDefn*)`
    - `>= 3100000`: `ICreateLayer(const char*, const OGRGeomFieldDefn*, CSLConstList)`
- Note: `TestCapability(const char*)` does NOT have `const` qualifier in any GDAL version.

## Driver Registration
- Maintain the `H2GIS_DRIVER_NAME` constant in `ogr_h2gis.h`.
- Use `GDAL_CHECK_VERSION("H2GIS driver")` at the beginning of `RegisterOGRH2GIS()`.
- Call `SetDescription(pszFilename)` on DataSource after successful Open.
- Use `SetMetadataItem()` to declare all driver capabilities:
    - `GDAL_DMD_CREATIONFIELDDATATYPES`: Integer, Integer64, Real, String, Date, DateTime, Time, Binary
    - `GDAL_DS_LAYER_CREATIONOPTIONLIST`: Support `GEOMETRY_NAME`, `FID`, `SPATIAL_INDEX`.
    - `GDAL_DMD_SUPPORTED_SQL_DIALECTS`: "NATIVE OGRSQL"
    - `GDAL_DCAP_MEASURED_GEOMETRIES`, `GDAL_DCAP_Z_GEOMETRIES`
    - `GDAL_DCAP_VECTOR`: "YES"
    - `GDAL_DCAP_MULTIPLE_VECTOR_LAYERS`: "YES"
    - `GDAL_DMD_OPENOPTIONLIST`: `USER`, `PASSWORD`

## OGRDataSource Implementation
- `Open()`: Must validate file extension `.mv.db`. Parse connection strings (H2GIS:prefix or typical file paths).
- `Create()`: Handle new dataset creation. Ensure `.mv.db` extension logic is consistent.
- `ExecuteSQL()`: **Must** return a valid `OGRH2GISResultLayer` for `SELECT` statements (not `nullptr`).
    - Use `h2gis_prepare` and `h2gis_execute_prepared`.
- `GetLayerByName()`: Implement case-insensitive search.
- `TestCapability()`: **MUST be `const`** - signature: `int TestCapability(const char *) const override;`

## OGRLayer Implementation
- Implement `OGRLayer` methods with **const correctness** (e.g., `TestCapability() const` must be const).
- **TestCapability signature**: `int TestCapability(const char *) const override;`
- Implement `GetFeature(GIntBig nFID)` for random access (required for `OLCRandomRead`).
    - Use `SELECT ... WHERE ID = ?` or `_ROWID_`.
- `ICreateFeature` (CreateFeature):
    - Must set FID on the feature object after insertion if generated by DB.
    - Use `SELECT "ID" FROM FINAL TABLE (INSERT ...)` for H2 v2+ identity retrieval.
- Capabilities:
    - `OLCRandomRead`: Enable.
    - `OLCFastSpatialFilter`: Enable.
    - `OLCFastSetNextByIndex`: Implement if supported.
    - `OLCTransactions`: Enable (Start/Commit/Rollback).
    - `OLCIgnoreFields`: Implement for performance.

## Filter Push-down (Performance Critical)
- **Spatial Filter**: Push `WHERE geom && ST_MakeEnvelope(...) AND ST_Intersects(...)` to H2GIS.
- **Attribute Filter**: Push user's WHERE clause directly to H2GIS SQL.
    - Implement `SetAttributeFilter(const char* pszQuery)` override.
    - Store filter in `m_osAttributeFilter` member.
    - Integrate into `PrepareQuery()` SQL construction.
    - Handle combination of spatial + attribute filters with `AND`.
- **GetFeatureCount()**: Must apply both spatial and attribute filters when counting.
- **GetExtent()**: Return `OGRERR_FAILURE` if `!bForce` and no cached extent available.

## Testing
- Use **pytest** framework.
- Tests should be located in `tests/ogr_h2gis.py` and `tests/conftest.py`.
- **Environment**: When running tests in venv, use `--system-site-packages` to link against system GDAL (needed for plugin ABI compatibility).
- Set `GDAL_DRIVER_PATH=$PWD/build` before running tests.
- Fixtures: `h2gis_ds` must verify driver availability and create unique temporary databases.
- **Required Tests**:
    - SRID/CRS roundtrip (create with EPSG:4326, verify on read)
    - Geometry type preservation (POINT, LINESTRING, POLYGON, MULTI*)
    - SetAttributeFilter push-down verification
    - Spatial filter verification
    - Transaction commit/rollback

## Code Style
- Use standard GDAL indentation (4 spaces).
- Use `po` prefix for pointers (e.g., `poLayer`, `poDS`).
- Use `psz` prefix for C-strings (e.g., `pszFilename`).
- Use `os` prefix for `CPLString` or `std::string` (e.g., `osTableName`).
- Use `m_` prefix for class members.
- Use `n` prefix for integer counts (e.g., `nLayers`).
- Remove all `<iostream>` includes - use `CPLDebug()`/`CPLError()` only.

## CMake Configuration
- Support configurable H2GIS library path: `-DH2GIS_LIBRARY=/path/to/libh2gis.so`
- Minimum GDAL version: 3.4
- Remove all hardcoded developer paths before PR submission.

