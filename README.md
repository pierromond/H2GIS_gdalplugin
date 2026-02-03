# GDAL H2GIS Driver

[![GDAL](https://img.shields.io/badge/GDAL-3.4+-blue.svg)](https://gdal.org/)
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
- **Compatible** - QGIS 3.28+, GDAL 3.4-3.10+, Linux x86_64

---

## Requirements

| Component | Version | Installation |
|-----------|---------|--------------|
| Linux/Windows/macOS | See below | - |
| GDAL | 3.4+ | `sudo apt install gdal-bin libgdal-dev` |
| CMake | 3.16+ | `sudo apt install cmake` |
| GCC | 11+ | `sudo apt install build-essential` |
| libh2gis | 0.0.3+ | `pip install h2gis` (includes native libs) |

---

## Installation

### Option A: Automatic script (recommended)

```bash
tar -xzf gdal-h2gis-driver-linux-x64.tar.gz
cd gdal-h2gis-driver
./install.sh
```

### Option B: Manual installation

```bash
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/
sudo cp libh2gis.so /usr/local/lib/
sudo ldconfig
ogrinfo --formats | grep H2GIS
```

### Option C: Build from source

```bash
# 1. Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install -y build-essential cmake git gdal-bin libgdal-dev

# 2. Get the native H2GIS library
pip install h2gis
# Library is at: $(python -c "import h2gis; print(h2gis.__path__[0])")/lib/h2gis.so

# 3. Clone the repository
git clone https://github.com/orbisgis/gdal-h2gis-driver.git
cd gdal-h2gis-driver

# 4. Build and install
mkdir -p build && cd build
cmake ..
make -j$(nproc)
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/

# 5. Install or link the native library
H2GIS_LIB=$(python -c "import h2gis; print(h2gis.__path__[0])")/lib/h2gis.so
sudo cp $H2GIS_LIB /usr/local/lib/libh2gis.so
sudo ldconfig
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
