#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Uninstall script for the GDAL H2GIS driver
# Usage: ./uninstall.sh
#

set -e

echo "=========================================="
echo "  GDAL H2GIS Driver Uninstaller"
echo "=========================================="
echo ""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Possible install paths
GDAL_PLUGIN_PATHS=(
    "/usr/lib/x86_64-linux-gnu/gdalplugins/gdal_H2GIS.so"
    "/usr/lib/gdalplugins/gdal_H2GIS.so"
)

REMOVED_PLUGIN=0
for path in "${GDAL_PLUGIN_PATHS[@]}"; do
    if [ -f "$path" ]; then
        echo "Removing $path..."
        sudo rm -f "$path"
        REMOVED_PLUGIN=1
    fi
done

if [ -f "/usr/local/lib/libh2gis.so" ]; then
    echo "Removing /usr/local/lib/libh2gis.so..."
    sudo rm -f /usr/local/lib/libh2gis.so
fi

sudo ldconfig

echo ""
if [ $REMOVED_PLUGIN -eq 1 ]; then
    echo -e "${GREEN}Uninstallation complete.${NC}"
else
    echo -e "${YELLOW}No files found to remove.${NC}"
fi
echo ""
