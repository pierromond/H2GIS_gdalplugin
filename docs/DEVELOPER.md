# 🛠️ Guide du Développeur - GDAL H2GIS Driver

---

## 📖 Table des matières

1. [Architecture du Driver](#architecture-du-driver)
2. [Structure des fichiers](#structure-des-fichiers)
3. [Flux de données](#flux-de-données)
4. [GraalVM et le Worker Thread](#graalvm-et-le-worker-thread)
5. [API C H2GIS](#api-c-h2gis)
6. [Gestion des SRID](#gestion-des-srid)
7. [Authentification](#authentification)
8. [Debugging](#debugging)
9. [Contribuer](#contribuer)

---

## 🏗️ Architecture du Driver

Le driver GDAL H2GIS est structuré en 3 couches principales :

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

### Les 3 classes OGR

| Classe | Responsabilité | Fichier |
|--------|----------------|---------|
| `OGRH2GISDriver` | Identification des fichiers `.mv.db`, création du DataSource | `ogrh2gisdriver.cpp` |
| `OGRH2GISDataSource` | Connexion à la base, énumération des layers | `ogrh2gisdatasource.cpp` |
| `OGRH2GISLayer` | Lecture/écriture des features, filtrage spatial | `ogrh2gislayer.cpp` |

---

## 📁 Structure des fichiers

```
gdal-h2gis-driver/
├── CMakeLists.txt           # Configuration CMake
├── README.md                # Documentation utilisateur
├── install.sh               # Script d'installation
├── uninstall.sh             # Script de désinstallation
│
├── ogr_h2gis.h              # Header principal (classes OGR + helpers)
├── ogrh2gisdriver.cpp       # Point d'entrée GDAL (Identify/Open)
├── ogrh2gisdatasource.cpp   # Gestion connexion + énumération layers
├── ogrh2gislayer.cpp        # Lecture features + spatial filter
│
├── h2gis_wrapper.h          # Header wrapper (déclarations)
├── h2gis_wrapper.cpp        # Wrapper thread-safe pour GraalVM
│
├── h2gis.h                  # API C générée par GraalVM
├── graal_isolate.h          # Types GraalVM (isolate, thread)
├── libh2gis.so              # Bibliothèque native H2GIS
│
├── docs/
│   ├── DEVELOPER.md         # Ce fichier !
│   └── ARCHITECTURE.png     # Diagramme d'architecture
│
└── tests/
    └── test_driver.py       # Tests automatisés Python
```

### Description des fichiers sources

| Fichier | Lignes | Description |
|---------|--------|-------------|
| `ogr_h2gis.h` | ~200 | Classes OGR, `H2GISColumnInfo`, `MapH2GeometryType()`, `MapH2DataType()` |
| `ogrh2gisdriver.cpp` | ~190 | `Identify()`, `Open()`, `RegisterOGRH2GIS()` |
| `ogrh2gisdatasource.cpp` | ~550 | Connexion, parsing INFORMATION_SCHEMA, création layers |
| `ogrh2gislayer.cpp` | ~990 | Features, batch fetching, WKB parsing, spatial filter |
| `h2gis_wrapper.cpp` | ~590 | Worker thread 64MB, job queue, fonctions wrapper |

---

## 🔄 Flux de données

### Ouverture d'un fichier

```
1. QGIS drag & drop "database.mv.db"
       │
       ▼
2. GDAL appelle OGRH2GISDriverIdentify()
   → Vérifie extension .mv.db
       │
       ▼
3. GDAL appelle OGRH2GISDriverOpen()
   → Crée OGRH2GISDataSource
       │
       ▼
4. OGRH2GISDataSource::Open()
   → h2gis_wrapper_init() (crée worker thread si nécessaire)
   → h2gis_connect() via worker thread
   → Parse credentials (URI, env vars, defaults)
   → Requête INFORMATION_SCHEMA.COLUMNS (unique query!)
   → Crée OGRH2GISLayer pour chaque table/geometry
       │
       ▼
5. QGIS affiche les layers dans le panneau
```

### Lecture des features

```
1. QGIS demande l'extent ou les features
       │
       ▼
2. OGRH2GISLayer::SetSpatialFilter()
   → Stocke le rectangle de filtrage
       │
       ▼
3. OGRH2GISLayer::GetNextFeature()
   → PrepareQuery() avec ST_Intersects() si filtre spatial
   → SELECT _ROWID_, * FROM table WHERE ST_Intersects(...)
       │
       ▼
4. h2gis_fetch_batch() via worker thread
   → Retourne buffer binaire columnar (1000 rows)
       │
       ▼
5. ParseFeatureFromBatch()
   → Extrait géométrie (WKB) via OGRGeometryFactory::createFromWkb()
   → Extrait attributs selon leur type
   → Retourne OGRFeature
```

---

## ⚠️ Limitations actuelles

- Les champs DATE/TIME/DATETIME/BINARY ne sont pas encore décodés côté lecture (écriture OK).
- `ExecuteSQL()` renvoie des géométries en **WKB brut** (pas de conversion EWKB→WKB).

---

## 🧵 GraalVM et le Worker Thread

### Le problème du Stack Overflow

**Le problème :**
- GraalVM Native Image nécessite **~64 MB de stack** pour certaines opérations SQL complexes
- Les threads QGIS ont seulement **8 MB** de stack par défaut
- Résultat : **StackOverflowError** lors de requêtes avec JOINs ou fonctions spatiales complexes

**La solution :**
- Un **Worker Thread dédié** avec 64 MB de stack créé au démarrage
- Toutes les opérations H2GIS sont routées vers ce thread via une **job queue**
- Le caller attend le résultat via **condition_variable**

### Architecture du Worker Thread

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

### Code clé

```cpp
// Création du worker thread avec 64 MB de stack
pthread_attr_t attr;
pthread_attr_init(&attr);
pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);  // 64 MB!
pthread_create(&g_worker_pthread, &attr, worker_thread_func, nullptr);

// Template pour exécuter une fonction sur le worker
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
    
    return future.get();  // Bloque jusqu'au résultat
}
```

### Lifecycle du Worker Thread

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

## 📡 API C H2GIS

### Fonctions principales

| Fonction | Description | Thread-safe |
|----------|-------------|-------------|
| `h2gis_connect(thread, path, user, pass)` | Connexion à la base | Via wrapper |
| `h2gis_load(thread, conn)` | Initialise les fonctions H2GIS | Via wrapper |
| `h2gis_prepare(thread, conn, sql)` | Prépare une requête | Via wrapper |
| `h2gis_execute_prepared(thread, stmt)` | Exécute la requête | Via wrapper |
| `h2gis_fetch_batch(thread, rs, size, &len)` | Récupère N lignes | Via wrapper |
| `h2gis_fetch_one(thread, rs, &len)` | Récupère 1 ligne | Via wrapper |
| `h2gis_close_query(thread, handle)` | Ferme un statement/resultset | Via wrapper |
| `h2gis_close_connection(thread, conn)` | Ferme la connexion | Via wrapper |
| `h2gis_free_result_buffer(thread, buf)` | Libère un buffer | Via wrapper |

### Format du buffer binaire (columnar)

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

### Types de données H2GIS

```cpp
#define H2GIS_TYPE_NULL    0   // Pas de données
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

## 🌍 Gestion des SRID

### Récupération du SRID

Le SRID est récupéré depuis `INFORMATION_SCHEMA.COLUMNS.GEOMETRY_SRID` :

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

### ⚠️ Piège critique : INT vs BIGINT

H2 peut retourner le SRID comme BIGINT. Le parser doit gérer les deux :

```cpp
static int ParseColumnAsInt(uint8_t* colPtr, int64_t colOffset) {
    // ... parsing header ...
    
    if (type == H2GIS_TYPE_INT && dLen >= 4) {
        int32_t val;
        std::memcpy(&val, ptr, 4);
        return val;
    }
    // IMPORTANT: Gérer aussi BIGINT!
    if (type == H2GIS_TYPE_LONG && dLen >= 8) {
        int64_t val;
        std::memcpy(&val, ptr, 8);
        return (int)val;  // Safe - les SRID sont petits
    }
    return 0;
}
```

### ⚠️ Piège critique : Clonage du SRS

Le SRS doit être assigné **APRÈS** `AddGeomFieldDefn()` car cette fonction **clone** le geometry field :

```cpp
// ✅ CORRECT - Le SRS est assigné sur le champ cloné
m_poFeatureDefn->AddGeomFieldDefn(&gfd);
if (nSrid > 0) {
    OGRSpatialReference *poSRS = new OGRSpatialReference();
    poSRS->importFromEPSG(nSrid);
    m_poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);
    poSRS->Release();
}

// ❌ FAUX - Le SRS original est cloné, puis l'original est libéré
// Le clone pointe vers un SRS invalide!
gfd.SetSpatialRef(poSRS);
poSRS->Release();
m_poFeatureDefn->AddGeomFieldDefn(&gfd);  // Clone avec SRS invalide!
```

---

## 🔐 Authentification

### 3 méthodes supportées (par ordre de priorité)

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

### Ordre des tentatives de connexion

Si aucun credential n'est fourni explicitement :

1. Credentials fournis (URI ou env vars)
2. Vide (`""`, `""`) - le plus courant pour les bases locales
3. H2 default (`"sa"`, `""`)
4. Legacy (`"sa"`, `"sa"`)

### Code de parsing

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

### Test rapide avec Python

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

### Test avec ogrinfo

```bash
# Lister les layers
ogrinfo /path/to/database.mv.db

# Détails d'un layer
ogrinfo -al -so /path/to/database.mv.db LAYER_NAME

# Exporter vers GeoPackage (test complet)
ogr2ogr -f GPKG output.gpkg /path/to/database.mv.db
```

---

## 🤝 Contribuer

### Setup de développement

```bash
# 1. Cloner le repo H2GIS
git clone https://github.com/orbisgis/h2gis.git
cd h2gis

# 2. Compiler libh2gis.so avec GraalVM
mvn native:compile -Pnative -pl h2gis-graalvm

# 3. Copier dans gdal-h2gis-driver
cp h2gis-graalvm/target/libh2gis.so ../gdal-h2gis-driver/

# 4. Compiler le driver
cd ../gdal-h2gis-driver
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 5. Installer
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/
sudo cp ../libh2gis.so /usr/local/lib/
sudo ldconfig

# 6. Tester
ogrinfo --formats | grep H2GIS
```

### Conventions de code

- **Nommage** : `CamelCase` pour les classes OGR, `snake_case` pour les fonctions C
- **Commentaires** : En anglais, rester factuel et professionnel
- **Logs** : Utiliser `LogDebugDS()`, `LogLayer()`, `debug_log()` selon le contexte
- **Mémoire** : TOUJOURS appeler `Release()` sur les `OGRSpatialReference*`
- **Threads** : JAMAIS appeler directement les fonctions `fp_h2gis_*`, toujours via wrapper

### Checklist avant commit

- [ ] `make clean && make` compile sans warnings
- [ ] Tests Python passent : `pytest tests/`
- [ ] Pas de memory leaks : `valgrind ogrinfo test.mv.db`
- [ ] Logs nettoyés (pas de `printf` debug)
- [ ] Documentation mise à jour si nouvelle feature

### Structure d'un nouveau feature

1. **Header** : Ajouter déclaration dans `ogr_h2gis.h`
2. **Implementation** : Coder dans le fichier `.cpp` approprié
3. **Wrapper** : Si appel GraalVM, ajouter dans `h2gis_wrapper.cpp`
4. **Tests** : Ajouter test dans `tests/test_driver.py`
5. **Docs** : Mettre à jour `README.md` et `DEVELOPER.md`

---

## 📚 Références

- [GDAL Vector Driver Tutorial](https://gdal.org/development/dev_vector_driver.html)
- [OGR API Reference](https://gdal.org/api/vector_c_api.html)
- [H2GIS Documentation](http://www.h2gis.org/docs/)
- [H2 Database](https://h2database.com/)
- [GraalVM Native Image](https://www.graalvm.org/reference-manual/native-image/)

---

## 🏆 Hall of Fame

**Contributeurs :**
- Équipe H2GIS
- Contributeurs principaux
- La communauté QGIS

---

**Bonne contribution ! 🎉**

## 📏 Coding Standards

Referez-vous à [.github/copilot-instructions.md](../.github/copilot-instructions.md) pour les standards de développement GDAL à respecter.

## 🧪 Tests

Les tests sont situés dans `tests/`. Utilisez `pytest` pour les exécuter.
