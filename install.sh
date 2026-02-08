#!/bin/bash
# SPDX-License-Identifier: MIT
#==============================================================================
# install.sh - GDAL H2GIS Driver Installer
#
# Usage: ./install.sh [--uninstall]
#
#==============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/gdalplugins"
LIB_DIR="/usr/local/lib"

print_banner() {
    echo ""
    echo "╔══════════════════════════════════════════════════╗"
    echo "║         🗄️ GDAL H2GIS Driver Installer           ║"
    echo "║              Version 1.0.0                       ║"
    echo "╚══════════════════════════════════════════════════╝"
    echo ""
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${YELLOW}⚠️ This script requires sudo privileges.${NC}"
        exec sudo "$0" "$@"
    fi
}

check_prerequisites() {
    echo "🔍 Checking prerequisites..."
    if ! command -v ogrinfo &> /dev/null; then
        echo -e "${RED}❌ GDAL is not installed!${NC}"
        exit 1
    fi
    GDAL_VERSION=$(ogrinfo --version | grep -oP 'GDAL \K[0-9]+\.[0-9]+')
    echo -e "   ✅ GDAL ${GDAL_VERSION} found"
    [[ ! -d "$PLUGIN_DIR" ]] && mkdir -p "$PLUGIN_DIR"
}

install_driver() {
    echo ""
    echo "📦 Installing driver..."
    
    if [[ -f "$SCRIPT_DIR/build/gdal_H2GIS.so" ]]; then
        DRIVER_SO="$SCRIPT_DIR/build/gdal_H2GIS.so"
    elif [[ -f "$SCRIPT_DIR/gdal_H2GIS.so" ]]; then
        DRIVER_SO="$SCRIPT_DIR/gdal_H2GIS.so"
    else
        echo -e "${RED}❌ gdal_H2GIS.so not found!${NC}"
        exit 1
    fi
    
    if [[ -f "$SCRIPT_DIR/libh2gis.so" ]]; then
        LIB_SO="$SCRIPT_DIR/libh2gis.so"
    else
        echo -e "${RED}❌ libh2gis.so not found!${NC}"
        exit 1
    fi
    
    cp "$DRIVER_SO" "$PLUGIN_DIR/" && chmod 644 "$PLUGIN_DIR/gdal_H2GIS.so"
    cp "$LIB_SO" "$LIB_DIR/" && chmod 644 "$LIB_DIR/libh2gis.so"
    ldconfig
    echo -e "${GREEN}✅ Installation successful!${NC}"
}

uninstall_driver() {
    echo "🗑️ Uninstalling..."
    rm -f "$PLUGIN_DIR/gdal_H2GIS.so" "$LIB_DIR/libh2gis.so"
    ldconfig
    echo -e "${GREEN}✅ Uninstallation complete!${NC}"
    exit 0
}

verify_installation() {
    echo ""
    echo "🧪 Verifying..."
    if ogrinfo --formats 2>/dev/null | grep -q "H2GIS"; then
        echo -e "${GREEN}✅ H2GIS driver registered in GDAL${NC}"
    else
        echo -e "${RED}❌ Driver not detected!${NC}"
        exit 1
    fi
}

print_usage() {
    echo ""
    echo "🎉 Installation complete!"
    echo ""
    echo "  ogrinfo /path/to/database.mv.db"
    echo "  qgis /path/to/database.mv.db"
    echo ""
}

print_banner
[[ "$1" == "--uninstall" ]] && check_root "$@" && uninstall_driver
check_root "$@"
check_prerequisites
install_driver
verify_installation
print_usage
