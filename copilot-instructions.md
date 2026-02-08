# GDAL/OGR H2GIS Driver Development Instructions

## Project Overview

Native OGR/GDAL plugin driver for reading and writing **H2GIS** spatial databases (`.mv.db` files).
The driver loads `libh2gis` (a GraalVM Native Image shared library) at runtime via `dlopen`/`LoadLibrary`.

### Architecture

```
ogrh2gisdriver.cpp     → GDAL driver registration (RegisterOGRH2GIS)
ogrh2gisdatasource.cpp → Database connection, layer enumeration, SQL, transactions
ogrh2gislayer.cpp      → Feature R/W, batch fetch, spatial/attribute filter push-down
ogr_h2gis.h            → Class declarations, type constants, geometry mapping
h2gis_wrapper.cpp/.h   → Runtime abstraction: dlopen, 64 MB-stack worker thread
graal_isolate.h        → GraalVM C API structures (generated, do not edit)
```

### Licensing

| Component | License |
|-----------|---------|
| GDAL H2GIS driver (C++ source) | **MIT** |
| QGIS installer plugin (`qgis-plugin/`) | GPL-3.0-or-later (required by QGIS ecosystem) |
| libh2gis (runtime dependency) | LGPL-3.0 |

All C++ source files and build files must carry `SPDX-License-Identifier: MIT`.

## Project Status (February 2026)

### Completed
- ✅ **SPDX Headers**: All source files have `SPDX-License-Identifier: MIT`
- ✅ **GDAL 3.4–3.12 Compatibility**: `ICreateLayer`, `SetIgnoredFields`, `CreateField` with `#if GDAL_VERSION_NUM` guards
- ✅ **SetAttributeFilter Push-down**: Spatial + attribute filter combination
- ✅ **Code Cleanup**: No `<iostream>`, no `printf`/`sprintf`, no hardcoded paths. Uses `CPLDebug`/`CPLError` and `snprintf` only
- ✅ **Metadata**: `GDAL_DCAP_MULTIPLE_VECTOR_LAYERS` and all driver capabilities declared
- ✅ **Tests**: 13 pytest tests covering open, create, CRUD, filters, transactions, SRID, multi-geometry
- ✅ **RST Documentation**: `doc/source/drivers/vector/h2gis.rst`
- ✅ **Cross-Platform Support**: Windows/macOS platform abstraction in `h2gis_wrapper.cpp`
- ✅ **GitHub Actions CI**: Linux (Ubuntu 22.04/24.04/25.10), macOS ARM64, Windows x64
- ✅ **GetExtent Optimization**: Uses `ST_Extent()` SQL aggregate (O(1) vs O(N) scan)
- ✅ **EWKB Handling**: `GetFeature()` strips SRID prefix from EWKB, consistent with `GetNextFeature()`
- ✅ **Schema Fetch**: Proper `bSchemaFetched` boolean parameter instead of sentinel column
- ✅ **Thread Safety**: `pthread_create` correctly passes task argument; 64 MB stack for GraalVM
- ✅ **Buffer Safety**: All `sprintf` replaced with `snprintf`

### Next Steps
- 🔲 **CMakeLists.txt**: Add Windows/macOS specific link flags
- 🔲 **CI Verification**: Push and verify GitHub Actions builds pass on all platforms
- 🔲 **Integration Testing**: Test with QGIS on Windows/macOS
- 🔲 **H2GIS upstream bug**: `ST_COLLECT` alias issue in GraalVM Native Image context (H2GIS Java-side)

### Cross-Platform Abstraction (`h2gis_wrapper.cpp`)

| Component | Linux | Windows | macOS |
|-----------|-------|---------|-------|
| Library loading | `dlopen/dlsym` | `LoadLibraryA/GetProcAddress` | `dlopen/dlsym` |
| Threading | `pthread_create` | `CreateThread` | `pthread_create` |
| Sleep | `usleep` | `Sleep` | `usleep` |
| File check | `access` | `_access` | `access` |
| Library ext | `.so` | `.dll` | `.dylib` |

**Note**: `CPLCreateThread` does NOT support custom stack size (64 MB required for GraalVM), so native thread APIs are used.

### GDAL CMake Integration

The `gdal-integration/` directory contains files for GDAL source-tree integration:
- `CMakeLists.txt` — `add_gdal_driver()` with `PLUGIN_CAPABLE NO_DEPS`
- `driver_declaration.cmake` — `ogr_optional_driver(h2gis ...)`
- `README.md` — Step-by-step integration guide

### libh2gis Dependency
- **License**: LGPL-3.0 (compatible with GDAL MIT)
- **Source**: Open source, available via PyPI: `pip install h2gis`
- **Technology**: GraalVM Native Image
- **Cross-platform**: Linux (.so), Windows (.dll), macOS (.dylib) in PyPI package `h2gis/lib/`

## General Guidelines
- Follow the **GDAL OGR Driver Implementation Guidelines**.
- Use the `OGRH2GIS` prefix for all classes.
- File naming convention: `ogr_h2gis.h`, `ogrh2gis*.cpp`.
- Ensure all pointer arguments are checked for `nullptr` before use.
- Use `CPLDebug()` for debugging, `CPLError()` for errors. Never `<iostream>`.
- The driver requires file paths to end with `.mv.db` (H2 v2 format).
- **Target**: GDAL 3.4+ with full compatibility up to GDAL 3.12+.
- **SPDX Header Required**: All files must have `SPDX-License-Identifier: MIT`

## GDAL API Version Compatibility (Critical)
Use `#if GDAL_VERSION_NUM >= XXXXX` for API changes:
- `>= 3090000`: `SetIgnoredFields(const char* const*)`, `CreateField(const OGRFieldDefn*)`
- `>= 3100000`: `ICreateLayer(const char*, const OGRGeomFieldDefn*, CSLConstList)`
- `>= 3120000`: const methods (`GetLayerDefn() const`, `TestCapability() const`, `GetLayer() const`)
- `>= 3120000`: `ISetSpatialFilter`/`IGetExtent` as protected virtual overrides

## Driver Registration
- `H2GIS_DRIVER_NAME` constant in `ogr_h2gis.h`.
- `GDAL_CHECK_VERSION("H2GIS driver")` at the beginning of `RegisterOGRH2GIS()`.
- `SetDescription(pszFilename)` on DataSource after successful Open.
- Declared capabilities via `SetMetadataItem()`:
    - `GDAL_DMD_CREATIONFIELDDATATYPES`: Integer, Integer64, Real, String, Date, DateTime, Time, Binary
    - `GDAL_DS_LAYER_CREATIONOPTIONLIST`: `GEOMETRY_NAME`, `FID`, `SPATIAL_INDEX`
    - `GDAL_DMD_SUPPORTED_SQL_DIALECTS`: "NATIVE OGRSQL"
    - `GDAL_DCAP_MEASURED_GEOMETRIES`, `GDAL_DCAP_Z_GEOMETRIES`
    - `GDAL_DCAP_VECTOR`: "YES"
    - `GDAL_DCAP_MULTIPLE_VECTOR_LAYERS`: "YES"
    - `GDAL_DMD_OPENOPTIONLIST`: `USER`, `PASSWORD`

## OGRDataSource Implementation
- `Open()`: Validate `.mv.db` extension. Parse `H2GIS:` prefix or file paths.
- `Create()`: Handle new dataset creation. Ensure `.mv.db` extension.
- `ExecuteSQL()`: Return `OGRH2GISResultLayer` for `SELECT` (not `nullptr`).
- `GetLayerByName()`: Case-insensitive search.
- `TestCapability()`: **MUST be `const`**.
- `ICreateLayer()`: Uses `bSchemaFetched` parameter to skip schema re-fetch for newly created layers.

## OGRLayer Implementation
- **const correctness** on `TestCapability() const`, `GetLayerDefn() const`, etc.
- `GetFeature(GIntBig nFID)`: Random access via `SELECT ... WHERE _ROWID_ = ?`. Handles EWKB by stripping SRID prefix.
- `GetNextFeature()`: Batch fetch (1000 features). Handles EWKB.
- `GetExtent()`: Uses `ST_Extent()` aggregate for O(1) performance.
- `ICreateFeature`: Sets FID via `SELECT "ID" FROM FINAL TABLE (INSERT ...)`.
- Capabilities: `OLCRandomRead`, `OLCFastSpatialFilter`, `OLCFastSetNextByIndex`, `OLCTransactions`, `OLCIgnoreFields`.

## Filter Push-down
- **Spatial**: `ST_Intersects(geom, ST_MakeEnvelope(...))` pushed to H2GIS.
- **Attribute**: User's WHERE clause pushed directly. Combined with spatial filter via `AND`.
- **GetFeatureCount()**: Applies both filters.
- **GetExtent()**: Returns `OGRERR_FAILURE` if `!bForce` and no cached extent.

## Testing
- **Framework**: pytest
- **Files**: `tests/ogr_h2gis.py`, `tests/conftest.py`
- **Environment**: `--system-site-packages` venv for system GDAL ABI compatibility.
- **Run**: `GDAL_DRIVER_PATH=$PWD/build pytest tests/ogr_h2gis.py -v`
- **Fixtures**: `h2gis_driver` (skips if unavailable), `h2gis_ds` (fresh temp database per test).
- **Coverage**: Open, create, field types, geometry types, SRID/CRS, spatial filter, attribute filter, transactions, multi-geometry, random read, `SetNextByIndex`.

## Code Style
- 4-space indentation (GDAL standard).
- Prefixes: `po` (pointers), `psz` (C-strings), `os` (std::string), `m_` (members), `n` (counts).
- No `<iostream>` — `CPLDebug()`/`CPLError()` only.
- No `sprintf` — use `snprintf` or `CPLSPrintf`.

## CMake Configuration
- Configurable: `-DH2GIS_LIBRARY=/path/to/libh2gis.so`
- Minimum GDAL: 3.4
- No hardcoded developer paths.

