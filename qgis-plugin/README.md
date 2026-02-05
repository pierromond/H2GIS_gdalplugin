# H2GIS Driver Installer - QGIS Plugin

A QGIS plugin that automates the installation of the GDAL/OGR H2GIS driver.

## Features

- **Automatic Detection**: Detects your OS, architecture, and GDAL version
- **Easy Installation**: Downloads and installs the matching pre-compiled driver
- **No Admin Required**: Installs in user space using `GDAL_DRIVER_PATH`
- **Demo Database**: Create a sample H2GIS database to test the driver
- **Cross-Platform**: Linux, Windows, macOS support

## Installation

### From QGIS Plugin Manager

1. Open QGIS
2. Go to **Plugins → Manage and Install Plugins**
3. Search for "H2GIS Driver Installer"
4. Click **Install**

### Manual Installation

1. Download the latest release ZIP from [GitHub Releases](https://github.com/pierromond/H2GIS_gdalplugin/releases)
2. In QGIS: **Plugins → Manage and Install Plugins → Install from ZIP**
3. Select the downloaded ZIP file

## Usage

1. After installation, go to **Database → H2GIS Driver → Install H2GIS Driver...**
2. Click **Install Driver** to download and install automatically
3. Restart QGIS to activate the driver
4. (Optional) Click **Create Demo Database** to test with sample data

## How It Works

The plugin:
1. Detects your QGIS GDAL version
2. Downloads the matching pre-compiled `gdal_H2GIS` binary
3. Downloads the H2GIS native library from the official artifact
4. Installs everything in `~/.local/share/QGIS/h2gis/` (or equivalent)
5. Configures `GDAL_DRIVER_PATH` for QGIS to find the driver

## Supported Configurations

| OS | Architecture | GDAL Versions |
|----|--------------|---------------|
| Linux | x86_64 | 3.4, 3.6, 3.8, 3.9, 3.10, 3.11, 3.12 |
| macOS | ARM64 | 3.10, 3.11, 3.12 |
| Windows | x86_64 | 3.8, 3.9 |

If your configuration is not listed, you can still use **Install from File...** with a manually downloaded or compiled binary.

## Development

```bash
# Clone the repository
git clone https://github.com/pierromond/H2GIS_gdalplugin.git
cd H2GIS_gdalplugin/qgis-plugin

# Create a symlink to your QGIS plugins directory
ln -s $(pwd)/h2gis_driver_installer ~/.local/share/QGIS/QGIS3/profiles/default/python/plugins/

# Reload QGIS or use the Plugin Reloader
```

## License

GPL-3.0-or-later
