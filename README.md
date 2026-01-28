# 🗄️ GDAL H2GIS Driver

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GDAL](https://img.shields.io/badge/GDAL-3.4+-blue.svg)](https://gdal.org/)
[![H2GIS](https://img.shields.io/badge/H2GIS-2.2+-green.svg)](http://www.h2gis.org/)

Driver OGR/GDAL natif pour lire les bases de données spatiales **H2GIS** (fichiers `.mv.db`).

> Accédez à vos données H2GIS directement depuis QGIS, ogr2ogr, Python/Fiona, R/sf, et tous les outils compatibles GDAL !

---

## ✨ Fonctionnalités

- ✅ **Lecture des layers** - Tables spatiales et non-spatiales
- ✅ **Support multi-géométrie** - Une couche par colonne géométrique
- ✅ **Filtrage spatial** - Utilise les index R-Tree H2GIS
- ✅ **SRID/CRS** - Reconnaissance automatique des systèmes de coordonnées
- ✅ **Authentification** - Support user/password via URI ou variables d'environnement
- ✅ **Performance** - Fetch par batch (1000 features), pas de dépendance JVM
- ✅ **Compatible** - QGIS 3.28+, GDAL 3.4+, Linux x86_64

---

## 📋 Prérequis

| Composant | Version | Installation |
|-----------|---------|--------------|
| Linux | Ubuntu 22.04+ / Debian 12+ | - |
| GDAL | 3.4+ | `sudo apt install gdal-bin libgdal-dev` |
| CMake | 3.16+ | `sudo apt install cmake` |
| GCC | 11+ | `sudo apt install build-essential` |

---

## 🚀 Installation

### Option A: Script automatique (recommandé)

```bash
tar -xzf gdal-h2gis-driver-linux-x64.tar.gz
cd gdal-h2gis-driver
./install.sh
```

### Option B: Installation manuelle

```bash
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/
sudo cp libh2gis.so /usr/local/lib/
sudo ldconfig
ogrinfo --formats | grep H2GIS
```

### Option C: Compilation depuis les sources

```bash
cd gdal-h2gis-driver
mkdir -p build && cd build
cmake ..
make -j$(nproc)
sudo cp gdal_H2GIS.so /usr/lib/x86_64-linux-gnu/gdalplugins/
sudo cp ../libh2gis.so /usr/local/lib/
sudo ldconfig
```

---

## 📖 Utilisation

### Dans QGIS

1. **Glisser-déposer** un fichier `.mv.db` dans QGIS
2. Sélectionner les couches à afficher
3. C'est tout ! 🎉

### Ligne de commande

```bash
# Lister les couches
ogrinfo /chemin/vers/database.mv.db

# Exporter vers GeoPackage
ogr2ogr -f GPKG output.gpkg /chemin/vers/database.mv.db

# Exporter vers Shapefile
ogr2ogr -f "ESRI Shapefile" output_dir /chemin/vers/database.mv.db NOM_COUCHE
```

### Python

```python
from osgeo import ogr
ds = ogr.Open('/chemin/vers/database.mv.db')
for i in range(ds.GetLayerCount()):
    layer = ds.GetLayer(i)
    print(f"{layer.GetName()}: {layer.GetFeatureCount()} features")
```

---

## 🔐 Authentification

```bash
# Méthode 1: URI
ogrinfo "/chemin/db.mv.db?user=monuser&password=monpass"

# Méthode 2: Style GDAL
ogrinfo "/chemin/db.mv.db|user=monuser|password=monpass"

# Méthode 3: Variables d'environnement
export H2GIS_USER=monuser
export H2GIS_PASSWORD=monpass
ogrinfo /chemin/db.mv.db
```

---

## 🐛 Dépannage

```bash
export H2GIS_DEBUG=1
ogrinfo /chemin/vers/database.mv.db
cat /tmp/h2gis_driver.log
```

| Problème | Solution |
|----------|----------|
| H2GIS non listé | Vérifier gdal_H2GIS.so dans gdalplugins |
| Erreur libh2gis.so | Exécuter `sudo ldconfig` |
| Connect failed | Vérifier credentials |

---

## 📚 Documentation

- [Guide du développeur](docs/DEVELOPER.md) - Architecture, contribution, debugging

---

## 🤝 Contribution

Voir [docs/DEVELOPER.md](docs/DEVELOPER.md)

---

## 📄 Licence

GPLv3 License

---

**Made with ❤️ by the NoiseModelling/H2GIS community** - *28 janvier 2026*
