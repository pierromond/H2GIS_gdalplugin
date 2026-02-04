# GDAL H2GIS Driver

[![CI](https://github.com/pierromond/H2GIS_gdalplugin/actions/workflows/ci.yml/badge.svg)](https://github.com/pierromond/H2GIS_gdalplugin/actions/workflows/ci.yml)
[![GDAL](https://img.shields.io/badge/GDAL-3.4--3.12-blue.svg)](https://gdal.org/)
[![H2GIS](https://img.shields.io/badge/H2GIS-2.2+-green.svg)](http://www.h2gis.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

Native OGR/GDAL driver for reading and writing **H2GIS** spatial databases (`.mv.db` files).

> Access your H2GIS data directly from QGIS, ogr2ogr, Python/Fiona, R/sf, and all GDAL-compatible tools!

---

## Features

- **Read/Write layers** - Spatial and non-spatial tables
- **Multi-geometry support** - One layer per geometry column
- **Spatial filtering** - Uses H2GIS R-Tree indexes with filter push-down
- **Attribute filtering** - SQL WHERE clause push-down to database
- **SRID/CRS** - Automatic coordinate reference system detection
- **Authentication** - User/password support via URI or environment variables
- **Performance** - Batch fetch (1000 features), no JVM dependency at runtime
- **Cross-platform** - Linux, Windows, macOS

---

## Pre-built Binaries

Pre-built binaries are available from [GitHub Actions](https://github.com/pierromond/H2GIS_gdalplugin/actions) for specific OS/GDAL combinations:

| Binary | OS | GDAL Version | Architecture |
|--------|-----|--------------|--------------|
| `gdal-h2gis-ubuntu22.04-gdal3.4` | Ubuntu 22.04 LTS | 3.4.x | x86_64 |
| `gdal-h2gis-ubuntu24.04-gdal3.8` | Ubuntu 24.04 LTS | 3.8.x | x86_64 |
| `gdal-h2gis-ubuntu25.10-gdal3.10` | Ubuntu 25.10 | 3.10.x | x86_64 |
| `gdal-h2gis-macos-arm64-gdal3.12` | macOS 14 | 3.12.x | ARM64 |
| `gdal-h2gis-windows-x64-gdal3.8` | Windows | 3.8.x | x86_64 |

> ⚠️ **Important**: GDAL plugins are ABI-specific. You **must** use a binary compiled for your exact GDAL version. If your version is not available, [compile from source](#build-from-source).

---

## GDAL/QGIS Compatibility

### Understanding ABI Compatibility

GDAL plugins use C++ and are **not binary-compatible** across different GDAL versions. A plugin compiled with GDAL 3.8 will not work with GDAL 3.4 or 3.10.

### Linux with System GDAL

If you use GDAL from your distribution's package manager, use the matching pre-built binary:

| Distribution | GDAL Version | Pre-built Binary |
|--------------|--------------|------------------|
| Ubuntu 22.04 LTS | 3.4.1 | `gdal-h2gis-ubuntu22.04-gdal3.4` |
| Ubuntu 24.04 LTS | 3.8.4 | `gdal-h2gis-ubuntu24.04-gdal3.8` |
| Ubuntu 25.10 | 3.10.3 | `gdal-h2gis-ubuntu25.10-gdal3.10` |
| Debian 12 | 3.6.2 | Compile from source |

### QGIS Users

QGIS bundles its own GDAL version, which may differ from your system GDAL:

| Scenario | GDAL Used | Solution |
|----------|-----------|----------|
| **Linux + QGIS from system repos** | System GDAL | Use matching pre-built binary |
| **Linux + QGIS from PPA** | QGIS-bundled GDAL | Compile against QGIS's GDAL headers |
| **Windows + QGIS installer** | QGIS-bundled GDAL | Compile from source (see below) |
| **macOS + QGIS.app** | QGIS-bundled GDAL | Compile from source (see below) |

To check which GDAL version your QGIS uses:
```bash
# In QGIS Python console
from osgeo import gdal
print(gdal.__version__)
```

---

## Requirements

| Component | Version | Installation |
|-----------|---------|--------------|
| GDAL | 3.4+ | `sudo apt install gdal-bin libgdal-dev` |
| CMake | 3.16+ | `sudo apt install cmake` |
| GCC/Clang | 11+ | `sudo apt install build-essential` |
| libh2gis | 0.0.3+ | `pip install h2gis` (includes native libs) |

---

## Installation

### Option A: Pre-built binary (if your GDAL version matches)

```bash
# Download the matching artifact from GitHub Actions
# Extract and install:
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/

# Install H2GIS native library
pip install h2gis
H2GIS_LIB=$(python3 -c "import h2gis; print(h2gis.__path__[0])")/lib/libh2gis.so
sudo cp $H2GIS_LIB /usr/local/lib/
sudo ldconfig

# Verify
ogrinfo --formats | grep H2GIS
```

### Option B: Build from source (recommended for QGIS/custom GDAL)

```bash
# 1. Install dependencies
sudo apt update
sudo apt install -y build-essential cmake git gdal-bin libgdal-dev

# 2. Get the native H2GIS library
pip install h2gis

# 3. Clone and build
git clone https://github.com/pierromond/H2GIS_gdalplugin.git
cd H2GIS_gdalplugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 4. Install plugin
sudo cp build/gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/

# 5. Install H2GIS native library
H2GIS_LIB=$(python3 -c "import h2gis; print(h2gis.__path__[0])")/lib/libh2gis.so
sudo cp $H2GIS_LIB /usr/local/lib/
sudo ldconfig

# 6. Verify
ogrinfo --formats | grep H2GIS
```

### Build for QGIS on Windows/macOS

For QGIS with bundled GDAL, you need to compile against QGIS's GDAL headers:

```bash
# macOS example (QGIS.app)
QGIS_GDAL=/Applications/QGIS.app/Contents/MacOS/lib
cmake -B build \
    -DGDAL_INCLUDE_DIR=/Applications/QGIS.app/Contents/MacOS/include \
    -DGDAL_LIBRARY=$QGIS_GDAL/libgdal.dylib
cmake --build build
```

---

## Usage

### In QGIS

1. **Drag and drop** a `.mv.db` file into QGIS
2. Select the layers to display
3. That's it!

### Command line

```bash
# List layers
ogrinfo /path/to/database.mv.db

# Export to GeoPackage
ogr2ogr -f GPKG output.gpkg /path/to/database.mv.db

# Export to Shapefile
ogr2ogr -f "ESRI Shapefile" output_dir /path/to/database.mv.db LAYER_NAME

# Apply attribute and spatial filters
ogrinfo /path/to/database.mv.db my_layer \
    -where "population > 10000" \
    -spat 1.5 48.5 2.5 49.5
```

### Python

```python
from osgeo import ogr
ds = ogr.Open('/path/to/database.mv.db')
for i in range(ds.GetLayerCount()):
    layer = ds.GetLayer(i)
    print(f"{layer.GetName()}: {layer.GetFeatureCount()} features")
```

### Layer creation options

```bash
# Geometry column name, FID column, and spatial index
ogr2ogr -f H2GIS /path/to/db.mv.db source.shp \
    -lco GEOMETRY_NAME=GEOM \
    -lco FID=ID \
    -lco SPATIAL_INDEX=YES \
    -lco SRID=4326
```

---

## Authentication

```bash
# Method 1: URI parameters
ogrinfo "/path/to/db.mv.db?user=myuser&password=mypass"

# Method 2: GDAL-style
ogrinfo "/path/to/db.mv.db|user=myuser|password=mypass"

# Method 3: Environment variables
export H2GIS_USER=myuser
export H2GIS_PASSWORD=mypass
ogrinfo /path/to/db.mv.db
```

---

## Testing

Run the test suite using `pytest`:

```bash
python -m venv .venv --system-site-packages
source .venv/bin/activate
pip install pytest
GDAL_DRIVER_PATH=$PWD/build pytest tests/
```

---

## Troubleshooting

```bash
export H2GIS_DEBUG=1
ogrinfo /path/to/database.mv.db
cat /tmp/h2gis_driver.log
```

| Problem | Solution |
|---------|----------|
| H2GIS not listed | Check gdal_H2GIS.so is in gdalplugins directory |
| libh2gis.so error | Run `sudo ldconfig` |
| Connection failed | Verify credentials |

---

## Current Limitations

- DATE/TIME/DATETIME/BINARY fields can be written, but reading is not yet fully decoded.
- `ExecuteSQL()` results return geometry as **raw WKB** (no EWKB to WKB conversion).

---

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0).

See [LICENSE](LICENSE) for details.

---

## See Also

- [H2GIS Official Website](http://www.h2gis.org/)
- [H2GIS Documentation](https://h2gis.github.io/docs/)
- [GDAL Vector Drivers](https://gdal.org/drivers/vector/)
- [Driver RST Documentation](doc/source/drivers/vector/h2gis.rst)

---

**Made with love by the NoiseModelling/H2GIS community**
