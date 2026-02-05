# H2GIS Driver Installer - QGIS Plugin

A QGIS plugin that automates the installation of the GDAL/OGR H2GIS driver.

## Features

- 🔍 **Automatic Detection**: Detects your OS, architecture, and GDAL version
- 📥 **One-Click Install**: Downloads matching pre-compiled driver binary
- 🗄️ **H2GIS Library**: Installs the required H2GIS native library
- 🎯 **Demo Database**: Creates a sample H2GIS database to get started
- 👤 **User Installation**: No admin/root rights required

## Supported Platforms

| OS | Architecture | GDAL Versions |
|----|--------------|---------------|
| Linux | x86_64 | 3.4, 3.8, 3.10+ |
| macOS | ARM64 (M1/M2/M3) | 3.10+ |
| macOS | Intel (x86_64) | 3.3 - 3.10 |
| Windows | x64 | 3.8+ |

## Installation

### From QGIS Plugin Manager

1. Open QGIS
2. Go to **Plugins** → **Manage and Install Plugins**
3. Search for "H2GIS Driver Installer"
4. Click **Install**

### From ZIP File

1. Download the plugin ZIP from [GitHub Releases](https://github.com/pierromond/H2GIS_gdalplugin/releases)
2. In QGIS: **Plugins** → **Manage and Install Plugins** → **Install from ZIP**
3. Select the downloaded ZIP file

## Usage

1. After installation, go to **Database** → **H2GIS Driver Installer**
2. Click **Install Driver**
3. Wait for download and installation to complete
4. Restart QGIS
5. Open any `.mv.db` file directly!

## Demo Database

The plugin can create a sample H2GIS database with:
- French cities (points)
- Major rivers (lines)
- Country boundary (polygon)

Click **Create Demo Database** in the installer dialog.

## Technical Details

- Driver is installed in user profile (no admin needed)
- Uses `GDAL_DRIVER_PATH` environment variable
- Startup script configures QGIS automatically

## Links

- [GitHub Repository](https://github.com/pierromond/H2GIS_gdalplugin)
- [Issue Tracker](https://github.com/pierromond/H2GIS_gdalplugin/issues)
- [H2GIS Project](http://www.h2gis.org/)
- [NoiseModelling](https://noise-planet.org/noisemodelling.html)

## License

GPL-3.0 - See [LICENSE](LICENSE) file
