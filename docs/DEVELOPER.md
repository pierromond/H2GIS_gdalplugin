# 🛠️ Developer Guide - GDAL H2GIS Driver

---

## 📖 Table of Contents

1. [Driver Architecture](#driver-architecture)
2. [File Structure](#file-structure)
3. [Data Flow](#data-flow)
4. [GraalVM and the Worker Thread](#graalvm-and-the-worker-thread)
5. [H2GIS C API](#h2gis-c-api)
6. [SRID Handling](#srid-handling)
7. [Authentication](#authentication)
8. [Debugging](#debugging)
9. [Contributing](#contributing)

---

## 🏗️ Driver Architecture

The GDAL H2GIS driver is structured in 3 main layers:

```
┌─────────────────────────────────────────────────────────────┐
│                    Applications                              │
│         (QGIS, MapServer, ogr2ogr, Python/Fiona, R/sf)      │
└──────────────────────────┬──────────────────────────────────┘
                           │ OGR API (C++)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│               OGR H2GIS Driver (gdal_H2GIS.so)              │
│  ┌─────────────────┐  ┌──────────────────┐  ┌─────────────┐ │
│  │ OGRH2GISDriver  │  │OGRH2GISDataSource│  │OGRH2GISLayer│ │
│  │  (Identification│  │   (Connexion DB) │  │  (Features) │ │
│  │   & Factory)    │  │                  │  │             │ │
│  └─────────────────┘  └──────────────────┘  └─────────────┘ │
└──────────────────────────┬──────────────────────────────────┘
                           │ C API via h2gis_wrapper
                           ▼
┌─────────────────────────────────────────────────────────────┐
│           h2gis_wrapper.cpp (Thread Manager)                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Worker Thread (64MB Stack) ◄── Job Queue ◄── Caller │   │
│  └─────────────────────────────────────────────────────┘   │
└──────────────────────────┬──────────────────────────────────┘
                           │ dlopen/dlsym
                           ▼
┌─────────────────────────────────────────────────────────────┐
│              libh2gis.so (GraalVM Native Image)             │
│                   H2 Database + H2GIS + JTS                 │
└─────────────────────────────────────────────────────────────┘
```

### The 3 OGR Classes

| Class | Responsibility | File |
|-------|----------------|------|
| `OGRH2GISDriver` | `.mv.db` file identification, DataSource creation | `ogrh2gisdriver.cpp` |
| `OGRH2GISDataSource` | Database connection, layer enumeration | `ogrh2gisdatasource.cpp` |
| `OGRH2GISLayer` | Feature reading/writing, spatial filtering | `ogrh2gislayer.cpp` |

---

## 📁 File Structure

```
gdal-h2gis-driver/
├── CMakeLists.txt           # CMake configuration
├── README.md                # User documentation
├── install.sh               # Installation script
├── uninstall.sh             # Uninstallation script
│
├── ogr_h2gis.h              # Main header (OGR classes + helpers)
├── ogrh2gisdriver.cpp       # GDAL entry point (Identify/Open)
├── ogrh2gisdatasource.cpp   # Connection management + layer enumeration
├── ogrh2gislayer.cpp        # Feature reading + spatial filter
│
├── h2gis_wrapper.h          # Wrapper header (declarations)
├── h2gis_wrapper.cpp        # Thread-safe wrapper for GraalVM
│
├── h2gis.h                  # GraalVM-generated C API
├── graal_isolate.h          # GraalVM types (isolate, thread)
├── libh2gis.so              # H2GIS native library
│
├── docs/
│   ├── DEVELOPER.md         # This file!
│   └── ARCHITECTURE.png     # Architecture diagram
│
└── tests/
    └── ogr_h2gis.py         # Automated Python tests
```

### Source File Descriptions

| File | Lines | Description |
|---------|--------|-------------|
| `ogr_h2gis.h` | ~430 | OGR classes, `H2GISColumnInfo`, `MapH2GeometryType()`, `MapH2DataType()` |
| `ogrh2gisdriver.cpp` | ~240 | `Identify()`, `Open()`, `RegisterOGRH2GIS()` |
| `ogrh2gisdatasource.cpp` | ~1420 | Connection, INFORMATION_SCHEMA parsing, layer creation |
| `ogrh2gislayer.cpp` | ~1900 | Features, batch fetching, WKB parsing, spatial filter |
| `h2gis_wrapper.cpp` | ~940 | 64MB worker thread, job queue, wrapper functions |

---

## 🔄 Data Flow

### Opening a File

```
1. QGIS drag & drop "database.mv.db"
       │
       ▼
2. GDAL calls OGRH2GISDriverIdentify()
   → Checks .mv.db extension
       │
       ▼
3. GDAL calls OGRH2GISDriverOpen()
   → Creates OGRH2GISDataSource
       │
       ▼
4. OGRH2GISDataSource::Open()
   → h2gis_wrapper_init() (creates worker thread if needed)
   → h2gis_connect() via worker thread
   → Parse credentials (URI, env vars, defaults)
   → Query INFORMATION_SCHEMA.COLUMNS (single query!)
   → Create OGRH2GISLayer for each table/geometry
       │
       ▼
5. QGIS displays the layers in the panel
```

### Reading Features

```
1. QGIS requests the extent or features
       │
       ▼
2. OGRH2GISLayer::SetSpatialFilter()
   → Stores the filter rectangle
       │
       ▼
3. OGRH2GISLayer::GetNextFeature()
   → PrepareQuery() with ST_Intersects() if spatial filter
   → SELECT _ROWID_, * FROM table WHERE ST_Intersects(...)
       │
       ▼
4. h2gis_fetch_batch() via worker thread
   → Returns columnar binary buffer (1000 rows)
       │
       ▼
5. ParseFeatureFromBatch()
   → Extracts geometry (WKB) via OGRGeometryFactory::createFromWkb()
   → Extracts attributes by type
   → Returns OGRFeature
```

---

## ⚠️ Current Limitations

- DATE/TIME/DATETIME/BINARY fields are not yet decoded on the read side (writing works).
- `ExecuteSQL()` returns geometries as **raw WKB** (no EWKB→WKB conversion).

---

## 🧵 GraalVM and the Worker Thread

### The Stack Overflow Problem

**The problem:**
- GraalVM Native Image requires **~64 MB of stack** for certain complex SQL operations
- QGIS threads only have **8 MB** of stack by default
- Result: **StackOverflowError** on queries with JOINs or complex spatial functions

**The solution:**
- A **dedicated Worker Thread** with 64 MB stack created at startup
- All H2GIS operations are routed to this thread via a **job queue**
- The caller waits for the result via **condition_variable**

### Worker Thread Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        h2gis_wrapper.cpp                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────────────┐│
│  │ Caller Thread│     │  Job Queue   │     │   Worker Thread      ││
│  │  (QGIS, 8MB) │     │ std::queue<> │     │   (64MB stack)       ││
│  └──────┬───────┘     └──────┬───────┘     └──────────┬───────────┘│
│         │                    │                        │            │
│         │  1. Push job       │                        │            │
│         ├───────────────────►│                        │            │
│         │                    │  2. Pop job            │            │
│         │                    ├───────────────────────►│            │
│         │                    │                        │            │
│         │                    │  3. Execute on         │            │
│         │                    │     GraalVM            │            │
│         │                    │                        ▼            │
│         │                    │                   ┌────────────┐    │
│         │                    │                   │ libh2gis.so│    │
│         │                    │                   └────────────┘    │
│         │                    │                        │            │
│         │                    │  4. Set result         │            │
│         │◄────────────────────────────────────────────┤            │
│         │                    │                        │            │
│         │  5. Return         │                        │            │
│         ▼                    │                        │            │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Code

```cpp
// Create the worker thread with 64 MB stack
pthread_attr_t attr;
pthread_attr_init(&attr);
pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);  // 64 MB!
pthread_create(&g_worker_pthread, &attr, worker_thread_func, nullptr);

// Template to execute a function on the worker
template<typename Func>
auto execute_on_worker(Func func) -> decltype(func()) {
    std::promise<decltype(func())> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_task_queue.push([&]() {
            promise.set_value(func());
        });
    }
    g_queue_cv.notify_one();
    
    return future.get();  // Blocks until result is available
}
```

### Worker Thread Lifecycle

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  Premier Open()  │────▶│ h2gis_wrapper_   │────▶│  Worker Thread   │
│                  │     │ init()           │     │  Created (64MB)  │
└──────────────────┘     └──────────────────┘     └──────────────────┘
                                                          │
                                                          ▼
                                                  ┌──────────────────┐
                                                  │   Task Loop      │
                                                  │ while(!shutdown) │
                                                  │   wait(queue_cv) │
                                                  │   execute(task)  │
                                                  └──────────────────┘
                                                          │
┌──────────────────┐     ┌──────────────────┐            │
│  Process Exit    │────▶│  atexit() calls  │            │
│                  │     │  h2gis_wrapper_  │            │
│                  │     │  shutdown()      │            │
└──────────────────┘     └──────────────────┘            │
                                │                        │
                                ▼                        ▼
                         ┌──────────────────┐     ┌──────────────────┐
                         │ g_shutdown=true  │────▶│  pthread_join()  │
                         │ notify_all()     │     │  Clean exit      │
                         └──────────────────┘     └──────────────────┘
```

---

## 📡 H2GIS C API

### Main Functions

| Function | Description | Thread-safe |
|----------|-------------|-------------|
| `h2gis_connect(thread, path, user, pass)` | Connect to database | Via wrapper |
| `h2gis_load(thread, conn)` | Initialize H2GIS functions | Via wrapper |
| `h2gis_prepare(thread, conn, sql)` | Prepare a query | Via wrapper |
| `h2gis_execute_prepared(thread, stmt)` | Execute a query | Via wrapper |
| `h2gis_fetch_batch(thread, rs, size, &len)` | Fetch N rows | Via wrapper |
| `h2gis_fetch_one(thread, rs, &len)` | Fetch 1 row | Via wrapper |
| `h2gis_close_query(thread, handle)` | Close a statement/resultset | Via wrapper |
| `h2gis_close_connection(thread, conn)` | Close the connection | Via wrapper |
| `h2gis_free_result_buffer(thread, buf)` | Free a buffer | Via wrapper |

### Binary Buffer Format (Columnar)

```
┌─────────────────────────────────────────────────────────────┐
│ Header                                                      │
│ ┌──────────────┬──────────────┬───────────────────────────┐ │
│ │ ColCount (4) │ RowCount (4) │ Offsets[ColCount] (8×N)   │ │
│ └──────────────┴──────────────┴───────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│ Column 0 Data                                               │
│ ┌──────────────┬──────────────┬──────────────┬───────────┐ │
│ │ NameLen (4)  │ Name (var)   │ Type (4)     │ DataLen(4)│ │
│ └──────────────┴──────────────┴──────────────┴───────────┘ │
│ ┌───────────────────────────────────────────────────────┐   │
│ │ Data: [Row0_Value, Row1_Value, Row2_Value, ...]       │   │
│ └───────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│ Column 1 Data ...                                           │
├─────────────────────────────────────────────────────────────┤
│ Column N Data ...                                           │
└─────────────────────────────────────────────────────────────┘
```

### H2GIS Data Types

```cpp
#define H2GIS_TYPE_NULL    0   // No data
#define H2GIS_TYPE_INT     1   // 4 bytes, little-endian
#define H2GIS_TYPE_LONG    2   // 8 bytes, little-endian
#define H2GIS_TYPE_FLOAT   3   // 4 bytes, IEEE 754
#define H2GIS_TYPE_DOUBLE  4   // 8 bytes, IEEE 754
#define H2GIS_TYPE_STRING  5   // Length-prefixed UTF-8 (4 + N bytes)
#define H2GIS_TYPE_BLOB    6   // Length-prefixed bytes (4 + N bytes)
#define H2GIS_TYPE_GEOM    7   // Length-prefixed WKB (4 + N bytes)
#define H2GIS_TYPE_DATE    8   // 8 bytes, milliseconds since epoch
#define H2GIS_TYPE_BOOL    9   // 1 byte (0 = false, 1 = true)
```

---

## 🌍 SRID Handling

### SRID Retrieval

The SRID is retrieved from `INFORMATION_SCHEMA.COLUMNS.GEOMETRY_SRID`:

```sql
SELECT 
    c.TABLE_NAME, 
    c.COLUMN_NAME, 
    c.DATA_TYPE,
    c.GEOMETRY_TYPE,    -- Ex: "MULTIPOLYGON Z"
    c.GEOMETRY_SRID     -- Ex: 5490 (peut être INT ou BIGINT!)
FROM INFORMATION_SCHEMA.COLUMNS c
WHERE c.TABLE_SCHEMA = 'PUBLIC'
```

### ⚠️ Critical Pitfall: INT vs BIGINT

H2 may return the SRID as BIGINT. The parser must handle both:

```cpp
static int ParseColumnAsInt(uint8_t* colPtr, int64_t colOffset) {
    // ... parsing header ...
    
    if (type == H2GIS_TYPE_INT && dLen >= 4) {
        int32_t val;
        std::memcpy(&val, ptr, 4);
        return val;
    }
    // IMPORTANT: Also handle BIGINT!
    if (type == H2GIS_TYPE_LONG && dLen >= 8) {
        int64_t val;
        std::memcpy(&val, ptr, 8);
        return (int)val;  // Safe - SRIDs are small integers
    }
    return 0;
}
```

### ⚠️ Critical Pitfall: SRS Cloning

The SRS must be assigned **AFTER** `AddGeomFieldDefn()` because this function **clones** the geometry field:

```cpp
// ✅ CORRECT - SRS is assigned on the cloned field
m_poFeatureDefn->AddGeomFieldDefn(&gfd);
if (nSrid > 0) {
    OGRSpatialReference *poSRS = new OGRSpatialReference();
    poSRS->importFromEPSG(nSrid);
    m_poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);
    poSRS->Release();
}

// ❌ WRONG - The original SRS is cloned, then the original is released
// The clone points to an invalid SRS!
gfd.SetSpatialRef(poSRS);
poSRS->Release();
m_poFeatureDefn->AddGeomFieldDefn(&gfd);  // Clones with invalid SRS!
```

---

## 🔐 Authentication

### 3 Supported Methods (by Priority Order)

1. **URI avec query string** :
   ```
    /path/database.mv.db?user=demo&password=secret
   ```

2. **Style GDAL (pipe)** :
   ```
    /path/database.mv.db|user=demo|password=secret
   ```

3. **Variables d'environnement** :
   ```bash
    export H2GIS_USER=demo
   export H2GIS_PASSWORD=secret
   ```

### Connection Attempt Order

If no credentials are explicitly provided:

1. Provided credentials (URI or env vars)
2. Empty (`""`, `""`) - most common for local databases
3. H2 default (`"sa"`, `""`)
4. Legacy (`"sa"`, `"sa"`)

### Parsing Code

```cpp
// Parse query string format: ?user=xxx&password=yyy
size_t qPos = path.find('?');
if (qPos != std::string::npos) {
    std::string params = path.substr(qPos + 1);
    path = path.substr(0, qPos);
    // Parse key=value pairs separated by &
}

// Parse pipe format: |user=xxx|password=yyy
size_t pipePos = path.find('|');
if (pipePos != std::string::npos) {
    std::string params = path.substr(pipePos);
    path = path.substr(0, pipePos);
    // Parse key=value pairs separated by |
}
```

---

## 🐛 Debugging

### Variables d'environnement

```bash
# Active les logs détaillés
export H2GIS_DEBUG=1

# Lancer QGIS avec debug
H2GIS_DEBUG=1 qgis
```

### Fichiers de log

| Fichier | Contenu |
|---------|---------|
| `/tmp/h2gis_driver.log` | Logs du driver (Open, Identify) |
| `/tmp/h2gis_layer.log` | Logs des layers (features, schema) |
| `/tmp/h2gis_wrapper_debug.log` | Logs du worker thread (connect, SQL) |

### Problèmes courants et solutions

| Symptôme | Cause probable | Solution |
|----------|----------------|----------|
| "SCR inconnu" dans QGIS | SRID non assigné après clone | Assigner SRS après `AddGeomFieldDefn()` |
| Terminal bloqué après exit | Worker thread pas terminé | Vérifier `atexit()` handler |
| StackOverflowError | Appel direct à GraalVM | Toujours passer par `h2gis_wrapper` |
| "Connect failed" | Mauvais credentials | Spécifier user/password via URI |
| Layer vide | Mauvais nom de table | Vérifier la casse (H2 = case-sensitive) |
| Crash au 2ème Open | Double init GraalVM | Vérifier `g_initialized` flag |

### Quick Test with Python

```python
from osgeo import ogr

# Ouvrir la base
ds = ogr.Open('/path/to/database.mv.db')
if ds:
    print(f"Layers: {ds.GetLayerCount()}")
    for i in range(ds.GetLayerCount()):
        lyr = ds.GetLayer(i)
        srs = lyr.GetSpatialRef()
        epsg = srs.GetAuthorityCode(None) if srs else "None"
        print(f"  {lyr.GetName()}: EPSG={epsg}, Features={lyr.GetFeatureCount()}")
else:
    print("Failed to open!")
```

### Test with ogrinfo

```bash
# List layers
ogrinfo /path/to/database.mv.db

# Layer details
ogrinfo -al -so /path/to/database.mv.db LAYER_NAME

# Export to GeoPackage (full roundtrip test)
ogr2ogr -f GPKG output.gpkg /path/to/database.mv.db
```

---

## 🤝 Contributing

### Development Setup

```bash
# 1. Clone the H2GIS repo
git clone https://github.com/orbisgis/h2gis.git
cd h2gis

# 2. Compile libh2gis.so with GraalVM
mvn native:compile -Pnative -pl h2gis-graalvm

# 3. Copy to gdal-h2gis-driver
cp h2gis-graalvm/target/libh2gis.so ../gdal-h2gis-driver/

# 4. Compile the driver
cd ../gdal-h2gis-driver
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 5. Install
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/
sudo cp ../libh2gis.so /usr/local/lib/
sudo ldconfig

# 6. Test
ogrinfo --formats | grep H2GIS
```

### Code Conventions

- **Naming**: `CamelCase` for OGR classes, `snake_case` for C functions
- **Comments**: In English, factual and professional
- **Logging**: Use `LogDebugDS()`, `LogLayer()`, `debug_log()` as appropriate
- **Memory**: ALWAYS call `Release()` on `OGRSpatialReference*`
- **Threads**: NEVER call `fp_h2gis_*` functions directly, always via wrapper

### Pre-commit Checklist

- [ ] `make clean && make` compiles without warnings
- [ ] Python tests pass: `pytest tests/`
- [ ] No memory leaks: `valgrind ogrinfo test.mv.db`
- [ ] Debug logs cleaned (no `printf` leftovers)
- [ ] Documentation updated for new features

### New Feature Structure

1. **Header**: Add declaration in `ogr_h2gis.h`
2. **Implementation**: Code in the appropriate `.cpp` file
3. **Wrapper**: If GraalVM call, add in `h2gis_wrapper.cpp`
4. **Tests**: Add test in `tests/ogr_h2gis.py`
5. **Docs**: Update `README.md` and `DEVELOPER.md`

---

## 📚 References

- [GDAL Vector Driver Tutorial](https://gdal.org/development/dev_vector_driver.html)
- [OGR API Reference](https://gdal.org/api/vector_c_api.html)
- [H2GIS Documentation](http://www.h2gis.org/docs/)
- [H2 Database](https://h2database.com/)
- [GraalVM Native Image](https://www.graalvm.org/reference-manual/native-image/)

---

## 🏆 Hall of Fame

**Contributors:**
- H2GIS Team
- Core contributors
- The QGIS community

---

**Happy contributing! 🎉**

## 📏 Coding Standards

Refer to [.github/copilot-instructions.md](../.github/copilot-instructions.md) for GDAL development standards.

## 🧪 Tests

Tests are located in `tests/`. Use `pytest` to run them.
